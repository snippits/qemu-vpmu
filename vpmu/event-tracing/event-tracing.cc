extern "C" {
#include "vpmu-common.h"   // Include common C headers
#include "vpmu/linux-mm.h" // VM_EXEC and other mmap() mode states
}
#include "elf++.hh"          // elf::elf
#include "dwarf++.hh"        // dwarf::dwarf
#include "event-tracing.hpp" // EventTracer
#include "phase/phase.hpp"   // Phase class
#include "json.hpp"          // nlohmann::json

#include "function-tracing.hpp" // Tracing user functions and custom callbacks

#include <boost/algorithm/string.hpp> // boost::algorithm::to_lower
#include <boost/filesystem.hpp>       // boost::filesystem

EventTracer event_tracer;

std::shared_ptr<ET_Program> EventTracer::add_program(std::string name)
{
    auto program = std::make_shared<ET_Program>(name);
    // Lock when updating the program_list (thread shared resource)
    std::lock_guard<std::mutex> lock(program_list_lock);

    log_debug("Add new binary '%s'", program->name.c_str());
    program_list.push_back(program);
    // debug_dump_program_map();
    return program;
}

std::shared_ptr<ET_Program> EventTracer::add_library(std::string name)
{
    if (name.length() == 0) return nullptr;
    auto program = std::make_shared<ET_Program>(name);
    // Lock when updating the program_list (thread shared resource)
    std::lock_guard<std::mutex> lock(program_list_lock);

    // Set this flag to true if it's a library
    program->is_shared_library = true;
    log_debug("Add new library '%s'", program->name.c_str());
    program_list.push_back(program);
    // debug_dump_program_map();
    return program;
}

std::shared_ptr<ET_Program> EventTracer::find_program(const char* path)
{
    if (path == nullptr || strlen(path) == 0) return nullptr;

    for (auto& p : program_list) {
        if (p->fuzzy_compare_name(path)) return p;
    }
    return nullptr;
}

void EventTracer::remove_program(std::string name)
{
    if (program_list.size() == 0) return;
    std::lock_guard<std::mutex> lock(program_list_lock);

    log_debug("Try remove program '%s'", name.c_str());
    // The STL way cost a little more time than hand coding,
    // but it's much more SAFE!
    program_list.erase(std::remove_if(program_list.begin(),
                                      program_list.end(),
                                      [&](std::shared_ptr<ET_Program>& p) {
                                          return p->fuzzy_compare_name(name);
                                      }),
                       program_list.end());
    debug_dump_program_map();
}

void EventTracer::clear_shared_libraries(void)
{
    std::lock_guard<std::mutex> lock(program_list_lock);
    // The STL way cost a little more time than hand coding, but it's much more SAFE!
    program_list.erase(std::remove_if(program_list.begin(),
                                      program_list.end(),
                                      [&](std::shared_ptr<ET_Program>& p) {
                                          return p->is_shared_library;
                                      }),
                       program_list.end());
}

void EventTracer::update_elf_dwarf(std::shared_ptr<ET_Program>& program,
                                   const char*                  file_name)
{
    if (program == nullptr) return;
    int fd = open(file_name, O_RDONLY);
    if (fd < 0) {
        LOG_FATAL("File %s not found!", file_name);
        return;
    }

    log_debug("Loading symbol table to %s", program->name.c_str());
    try {
        elf::elf ef(elf::create_mmap_loader(fd));
        for (auto& sec : ef.sections()) {
            { // Read section information
                auto& hdr = sec.get_hdr();
                // Updata section map table
                program->section_table[sec.get_name()].beg = hdr.addr;
                program->section_table[sec.get_name()].end = hdr.addr + hdr.size;
                if (hdr.type != elf::sht::symtab && hdr.type != elf::sht::dynsym)
                    continue;
            }
// Read only symbol sections
#if 0
            log_debug("Symbol table '%s':", sec.get_name().c_str());
            log_debug("%-16s %-5s %-7s %-7s %-5s %s",
                      "Value",
                      "Size",
                      "Type",
                      "Binding",
                      "Index",
                      "Name");
#endif
            for (auto sym : sec.as_symtab()) {
                auto& d = sym.get_data();
                if (d.type() == elf::stt::func) {
#if 0
                    log_debug("%016" PRIx64 " %5" PRId64 " %-7s %-7s %5s %s",
                              d.value,
                              d.size,
                              to_string(d.type()).c_str(),
                              to_string(d.binding()).c_str(),
                              to_string(d.shnxd).c_str(),
                              sym.get_name().c_str());
#endif
                    program->sym_table[sym.get_name()] = d.value;
                }
            }
        }

        dwarf::dwarf dw(dwarf::elf::create_loader(ef));
        for (auto cu : dw.compilation_units()) {
            // log_debug("--- <%x>\n", (unsigned int)cu.get_section_offset());
            auto& lt = cu.get_line_table();
            for (auto& line : lt) {
                program->line_table[line.address] =
                  line.file->path + ":" + std::to_string(line.line);
#if 0
                if (line.end_sequence)
                    log_debug("\n");
                else
                    log_debug("%-40s%8d%#20" PRIx64 "\n",
                              line.file->path.c_str(),
                              line.line,
                              line.address);
#endif
            }
            // log_debug("\n");
        }
    } catch (elf::format_error e) {
        // Do nothing if the file is not a valid ELF binary
        // LOG_FATAL("bad ELF magic number");
        return;
    } catch (dwarf::format_error e) {
        log_debug("Warning: Target binary '%s' does not contatin dwarf sections",
                  program->name.c_str());
        return;
    }
}

void EventTracer::attach_mapped_region(std::shared_ptr<ET_Process>& process,
                                       MMapInfo                     mmap_info)
{
    std::shared_ptr<ET_Program> program = nullptr;

    uint64_t start_addr = mmap_info.vaddr;
    uint64_t end_addr   = mmap_info.vaddr + mmap_info.len;
    uint64_t mode       = mmap_info.mode;
    char*    fullpath   = mmap_info.fullpath;
    // auto     process    = event_tracer.find_process(pid);
    if (process == nullptr) return;

    if (process->binary_loaded == false && (mode & VM_EXEC)) {
        // The program is main program, rename the real path and filename
        program           = process->get_main_program();
        program->filename = vpmu::file::basename(fullpath);
        program->path     = fullpath;

        process->binary_loaded = true;
        process->append_debug_log("Main Program\n");
    } else {
        if (strlen(fullpath) == 0) {
            process->append_debug_log("Anonymous Mapping\n");
        } else {
            // Mapping executable page for shared library
            program = event_tracer.find_program(fullpath);
            if ((mode & VM_EXEC) && program == nullptr) {
                DBG(STR_VPMU "Shared library %s was not found in the list "
                             "create an empty one.\n",
                    fullpath);
                program = event_tracer.add_library(fullpath);
            }
            if (program != nullptr) {
                if (mode & VM_EXEC) {
                    // TODO Recording the library info for binaries might be unnecessary
                    event_tracer.attach_to_program(process->get_main_program(), program);
                }
                process->push_binary(program);
                process->append_debug_log("Library\n");
            } else {
                process->append_debug_log("Normal Files / Others\n");
            }
        }
    }

    // Push the memory region to process with/without pointer to target program
    uint64_t pc = process->pc_called_mmap;
    if (program == nullptr) {
        process->vm_maps.map_region(pc, start_addr, end_addr, mode, fullpath);
    } else {
        process->vm_maps.map_region(program, pc, start_addr, end_addr, mode, fullpath);
    }
    if (mode & VM_EXEC) ft_load_callbacks(process, program);
}

uint64_t EventTracer::parse_and_set_kernel_symbol(const char* filename)
{
    std::string version     = vpmu::utils::get_version_from_vmlinux(filename);
    auto        s_strs      = vpmu::str::split(version);
    uint64_t    version_num = 0;

    auto ver    = vpmu::str::split(s_strs[2], ".");
    version_num = KERNEL_VERSION(atoi(ver[0].c_str()),  // Major number
                                 atoi(ver[1].c_str()),  // Minor number
                                 atoi(ver[2].c_str())); // Revision number

    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        LOG_FATAL("Kernel File %s not found!", filename);
        return 0;
    }

    elf::elf f(elf::create_mmap_loader(fd));
    for (auto& sec : f.sections()) {
        if (sec.get_hdr().type != elf::sht::symtab
            && sec.get_hdr().type != elf::sht::dynsym)
            continue;

        for (auto sym : sec.as_symtab()) {
            auto& d = sym.get_data();
            if (d.type() == elf::stt::func) {
                kernel.add_symbol(sym.get_name(), d.value);
                kernel.set_symbol_address(sym.get_name(), d.value);
            }
        }
    }

    if (kernel.find_vaddr(ET_KERNEL_MMAP) == 0)
        LOG_FATAL("Kernel event \"%s\" was not found!", "mmap_region");
    if (kernel.find_vaddr(ET_KERNEL_MPROTECT) == 0)
        LOG_FATAL("Kernel event \"%s\" was not found!", "mprotect_fixup");
    if (kernel.find_vaddr(ET_KERNEL_MUNMAP) == 0)
        LOG_FATAL("Kernel event \"%s\" was not found!", "unmap_region");
    if (kernel.find_vaddr(ET_KERNEL_FORK) == 0)
        LOG_FATAL("Kernel event \"%s\" was not found!", "_do_fork");
    if (kernel.find_vaddr(ET_KERNEL_WAKE_NEW_TASK) == 0)
        LOG_FATAL("Kernel event \"%s\" was not found!", "wake_up_new_task");
    if (kernel.find_vaddr(ET_KERNEL_EXIT) == 0)
        LOG_FATAL("Kernel event \"%s\" was not found!", "do_exit");
    if (kernel.find_vaddr(ET_KERNEL_CONTEXT_SWITCH) == 0)
        LOG_FATAL("Kernel event \"%s\" was not found!", "__switch_to");
    if (kernel.find_vaddr(ET_KERNEL_EXECV) == 0)
        LOG_FATAL("Kernel event \"%s\" was not found!", "do_execveat_common");

    // TODO Make some functions work without setting structure offset
    // This must be done when kernel symbol is set, or emulation would hang or SEGV
    // TODO This should be written in x86/ARM protable and use KERNEL_VERSION macro
    et_set_default_linux_struct_offset(version_num);
    close(fd);
    // TODO load kernel elf,dwarf information to an appropriate place.
    return version_num;
}

void EventTracer::debug_dump_process_map(void)
{
#ifdef CONFIG_VPMU_DEBUG_MSG
    log_debug("Printing all traced processes with its information "
              "(first level of children only)");
    for (auto& p_pair : process_id_map) {
        auto& p = p_pair.second;
        if (p_pair.first != p->pid)
            log_fatal("PID not match!! map(%p) != p(%p)", p_pair.first, p->pid);
        log_debug("    pid: %5" PRIu64 ", Pointer %p Name: '%s'",
                  p->pid,
                  p.get(),
                  p->name.c_str());
        debug_dump_child_list(*p);
        log_debug("    Binaries loaded into this process");
        debug_dump_binary_list(*p);
        log_debug("    ==============================================");
    }
#endif
}

void EventTracer::debug_dump_process_map(std::shared_ptr<ET_Process> marked_process)
{
#ifdef CONFIG_VPMU_DEBUG_MSG
    char indent_space[16] = "    ";

    log_debug("Printing all traced processes with its information "
              "(first level of children only)");
    for (auto& p_pair : process_id_map) {
        auto& p = p_pair.second;
        if (p_pair.first != p->pid)
            log_fatal("PID not match!! map(%p) != p(%p)", p_pair.first, p->pid);
        if (p == marked_process) {
            indent_space[2] = '*';
        } else {
            indent_space[2] = ' ';
        }
        log_debug("%spid: %5" PRIu64 ", Pointer %p Name: '%s'",
                  indent_space,
                  p->pid,
                  p.get(),
                  p->name.c_str());
        debug_dump_child_list(*p);
        log_debug("    Binaries loaded into this process");
        debug_dump_binary_list(*p);
        log_debug("    ==============================================");
    }
#endif
}

void EventTracer::debug_dump_program_map(void)
{
#ifdef CONFIG_VPMU_DEBUG_MSG
    log_debug("Printing all traced program with its information"
              "(first level of dependent libraries only)");
    for (auto& program : program_list) {
        log_debug("    Pointer %p, Name: '%s' (%s), size of sym_table: %d"
                  ", num libraries loaded: %d",
                  program.get(),
                  program->name.c_str(),
                  program->filename.c_str(),
                  program->sym_table.size(),
                  program->library_list.size());
        debug_dump_library_list(*program);
        log_debug("    ============================================");
    }
#endif
}

void EventTracer::debug_dump_program_map(std::shared_ptr<ET_Program> marked_program)
{
#ifdef CONFIG_VPMU_DEBUG_MSG
    char indent_space[16] = "    ";

    log_debug("Printing all traced program(marked) with its information"
              "(first level of dependent libraries only)");
    for (auto& program : program_list) {
        if (program == marked_program) {
            indent_space[2] = '*';
        } else {
            indent_space[2] = ' ';
        }
        log_debug("%sPointer %p, Name: '%s' (%s), size of sym_table: %d"
                  ", num libraries loaded: %d",
                  indent_space,
                  program.get(),
                  program->name.c_str(),
                  program->filename.c_str(),
                  program->sym_table.size(),
                  program->library_list.size());
        debug_dump_library_list(*program);
    }
    log_debug("    ============================================");
#endif
}

void EventTracer::debug_dump_child_list(const ET_Process& process)
{
#ifdef CONFIG_VPMU_DEBUG_MSG
    for (auto& child : process.child_list) {
        log_debug("        pid:%5" PRIu64 ", Pointer %p, Name: '%s'",
                  child->pid,
                  child.get(),
                  child->name.c_str());
    }
#endif
}

void EventTracer::debug_dump_binary_list(const ET_Process& process)
{
#ifdef CONFIG_VPMU_DEBUG_MSG
    for (auto& binary : process.binary_list) {
        log_debug("        Pointer %p, Name: '%s' (%s), size of sym_table:%d"
                  ", num libraries loaded:%d",
                  binary.get(),
                  binary->name.c_str(),
                  binary->filename.c_str(),
                  binary->sym_table.size(),
                  binary->library_list.size());
    }
#endif
}

void EventTracer::debug_dump_library_list(const ET_Program& program)
{
#ifdef CONFIG_VPMU_DEBUG_MSG
    for (auto& binary : program.library_list) {
        log_debug("        Pointer %p, Name: '%s', size of sym_table: %d",
                  binary.get(),
                  binary->name.c_str(),
                  binary->sym_table.size());
    }
#endif
}

void et_set_linux_sym_addr(const char* sym_name, uint64_t addr)
{
    event_tracer.get_kernel().set_symbol_address(sym_name, addr);
}

enum ET_KERNEL_EVENT_TYPE et_find_kernel_event(uint64_t vaddr)
{
    return event_tracer.get_kernel().find_event(vaddr);
}

void et_add_program_to_list(const char* name)
{
    if (vpmu::str::simple_match(vpmu::file::basename(name), "*.so*"))
        event_tracer.add_library(name);
    else
        event_tracer.add_program(name);
}

void et_remove_program_from_list(const char* name)
{
    event_tracer.remove_program(name);
}

bool et_find_program_in_list(const char* name)
{
    return (event_tracer.find_program(name) != nullptr);
}

bool et_find_process_in_list(const char* name)
{
    return (event_tracer.find_process(name) != nullptr);
}

void et_update_program_elf_dwarf(const char* name, const char* host_file_path)
{
    auto program = event_tracer.find_program(vpmu::file::basename(name).c_str());
    // Some processes are monitored but binary does not exist in the list.
    // This usually happens when using attach mode (attach to a running process).
    if (program == nullptr) {
        // Get the binary from process directly and update its info.
        // Do not push this binary to the program list for keeping life-cycle of
        // binary to the monitored process.
        if (auto process = event_tracer.find_process(name)) {
            program = process->get_main_program();
        }
    }
    event_tracer.update_elf_dwarf(program, host_file_path);
}

void et_check_function_call(void*    env,
                            uint64_t core_id,
                            bool     user_mode,
                            uint64_t target_addr)
{
    if (user_mode) {
        auto process = event_tracer.find_process(VPMU.core[core_id].current_pid);
        if (process != nullptr) {
            process->call_event(env, target_addr);
        }
    } else {
        et_kernel_call_event(env, core_id, target_addr);
    }
    return;
}
