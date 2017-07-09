extern "C" {
#include "vpmu-common.h"   // Include common C headers
#include "vpmu/linux-mm.h" // VM_EXEC and other mmap() mode states
}
#include "elf++.hh"          // elf::elf
#include "dwarf++.hh"        // dwarf::dwarf
#include "event-tracing.hpp" // EventTracer
#include "phase/phase.hpp"   // Phase class
#include "json.hpp"          // nlohmann::json
#include "region-info.hpp"   // RegionInfo class

#include <boost/algorithm/string.hpp> // boost::algorithm::to_lower
#include <boost/filesystem.hpp>       // boost::filesystem

EventTracer event_tracer;
RegionInfo  RegionInfo::not_found = {};

void et_set_linux_sym_addr(const char* sym_name, uint64_t addr)
{
    event_tracer.get_kernel().set_symbol_address(sym_name, addr);
}

// TODO Implement updating dwarf as well.
void EventTracer::update_elf_dwarf(std::shared_ptr<ET_Program> program,
                                   const char*                 file_name)
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
            }
            if (sec.get_hdr().type != elf::sht::symtab
                && sec.get_hdr().type != elf::sht::dynsym)
                continue;
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
        LOG_FATAL("bad ELF magic number");
    } catch (dwarf::format_error e) {
        log_debug("Warning: Target binary '%s' does not contatin dwarf sections",
                  program->name.c_str());
        return;
    }
}

uint64_t EventTracer::parse_and_set_kernel_symbol(const char* filename)
{
    std::string version     = vpmu::utils::get_version_from_vmlinux(filename);
    auto        s_strs      = vpmu::utils::str_split(version);
    uint64_t    version_num = 0;

    auto v = vpmu::utils::str_split(s_strs[2], ".");
    version_num =
      KERNEL_VERSION(atoi(v[0].c_str()), atoi(v[1].c_str()), atoi(v[2].c_str()));

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
        log_debug("Symbol table '%s':", sec.get_name().c_str());
        log_debug("%-16s %-5s %-7s %-7s %-5s %s",
                  "Value",
                  "Size",
                  "Type",
                  "Binding",
                  "Index",
                  "Name");

        // NOTE: The following functions are core functions for these events.
        // We use these instead of system calls because some other system calls
        // might also trigger events of others.
        // Ex: In mmap syscall, i.e. mmap_region(), unmap_region is called in order to
        // undo any partial mapping done by a device driver.
        // If one uses system calls instead of these functions,
        // all mechanisms should still work..... well, in most cases. :P
        for (auto sym : sec.as_symtab()) {
            auto& d = sym.get_data();
            if (d.type() == elf::stt::func) {
                kernel.add_symbol(sym.get_name(), d.value);

                bool        print_content_flag = true;
                std::string sym_name           = sym.get_name();
                // Linux system call series functions might be either sys_XXXX or SyS_XXXX
                boost::algorithm::to_lower(sym_name);
                if (sym_name.find("do_execveat_common") != std::string::npos) {
                    kernel.set_event_address(ET_KERNEL_EXECV, d.value);
                } else if (sym_name == "__switch_to") {
                    kernel.set_event_address(ET_KERNEL_CONTEXT_SWITCH, d.value);
                } else if (sym_name == "do_exit") {
                    kernel.set_event_address(ET_KERNEL_EXIT, d.value);
                } else if (sym_name == "wake_up_new_task") {
                    kernel.set_event_address(ET_KERNEL_WAKE_NEW_TASK, d.value);
                } else if (sym_name == "_do_fork" || sym_name == "do_fork") {
                    kernel.set_event_address(ET_KERNEL_FORK, d.value);
                } else if (sym_name == "mmap_region") {
                    kernel.set_event_address(ET_KERNEL_MMAP, d.value);
                } else if (sym_name == "mprotect_fixup") {
                    kernel.set_event_address(ET_KERNEL_MPROTECT, d.value);
                } else if (sym_name == "unmap_region") {
                    kernel.set_event_address(ET_KERNEL_MUNMAP, d.value);
                } else {
                    print_content_flag = false;
                }
                if (print_content_flag) {
                    log_debug("%016" PRIx64 " %5" PRId64 " %-7s %-7s %5s %s\n",
                              d.value,
                              d.size,
                              to_string(d.type()).c_str(),
                              to_string(d.binding()).c_str(),
                              to_string(d.shnxd).c_str(),
                              sym.get_name().c_str());
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
    }
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

enum ET_KERNEL_EVENT_TYPE et_find_kernel_event(uint64_t vaddr)
{
    return event_tracer.get_kernel().find_event(vaddr);
}

void et_add_program_to_list(const char* name)
{
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

bool et_find_traced_pid(uint64_t pid)
{
    return (event_tracer.find_process(pid) != nullptr);
}

bool et_find_traced_process(const char* name)
{
    return (event_tracer.find_process(name) != nullptr);
}

void et_attach_to_parent_pid(uint64_t parent_pid, uint64_t child_pid)
{
    return event_tracer.attach_to_parent(parent_pid, child_pid);
}

void ET_Process::dump_vm_map(void)
{
    char        file_path[512] = {0};
    std::string output_path =
      std::string(VPMU.output_path) + "/phase/" + std::to_string(pid);
    boost::filesystem::create_directory(output_path);

    // Output in plain text format
    sprintf(file_path, "%s/vm_maps", output_path.c_str());
    FILE* fp = fopen(file_path, "wt");
    for (auto& reg : vm_maps.regions) {
        std::string out_str   = "";
        std::string prog_name = (reg.program) ? reg.program->name : "";
        out_str += (reg.permission & VM_READ) ? "r" : "-";
        out_str += (reg.permission & VM_WRITE) ? "w" : "-";
        out_str += (reg.permission & VM_EXEC) ? "x" : "-";
        out_str += (reg.permission & VM_SHARED) ? "-" : "p";
        fprintf(fp,
                "%16s - %-16s  "
                "%-5s %-40s %-20s"
                "\n",
                vpmu::utils::addr_to_str(reg.address.beg).c_str(),
                vpmu::utils::addr_to_str(reg.address.end).c_str(),
                out_str.c_str(),
                reg.pathname.c_str(),
                prog_name.c_str());
    }
    fclose(fp);
}

void ET_Process::dump_process_info(void)
{
    using vpmu::utils::addr_to_str;
    char        file_path[512] = {0};
    std::string output_path =
      std::string(VPMU.output_path) + "/phase/" + std::to_string(pid);
    boost::filesystem::create_directory(output_path);

    sprintf(file_path, "%s/process_info", output_path.c_str());
    FILE* fp = fopen(file_path, "wt");

    nlohmann::json j;
    j["Name"]      = name;
    j["File Name"] = filename;
    j["File Path"] = path;
    j["pid"]       = pid;
    j["debug_log"] = debug_log;
    for (int i = 0; i < child_list.size(); i++) {
        j["Childrens"][i]["Name"] = child_list[i]->name;
        j["Childrens"][i]["pid"]  = child_list[i]->pid;
    }
    for (int i = 0; i < binary_list.size(); i++) {
        auto address = vm_maps.find_address(binary_list[i], VM_EXEC);

        j["Binaries"][i]["Name"]      = binary_list[i]->name;
        j["Binaries"][i]["File Name"] = binary_list[i]->filename;
        j["Binaries"][i]["Path"]      = binary_list[i]->path;
        j["Binaries"][i]["Symbols"]   = binary_list[i]->sym_table.size();
        j["Binaries"][i]["DWARF"]     = binary_list[i]->line_table.size();
        j["Binaries"][i]["Libraries"] = binary_list[i]->library_list.size();
        j["Binaries"][i]["isLibrary"] = binary_list[i]->is_shared_library;
        j["Binaries"][i]["Addr Beg"]  = addr_to_str(address.beg).c_str();
        j["Binaries"][i]["Addr End"]  = addr_to_str(address.end).c_str();
    }
    for (int i = 0; i < vm_maps.regions.size(); i++) {
        auto        address   = vm_maps.regions[i].address;
        std::string out_str   = "";
        std::string prog_name = "";
        out_str += (vm_maps.regions[i].permission & VM_READ) ? "r" : "-";
        out_str += (vm_maps.regions[i].permission & VM_WRITE) ? "w" : "-";
        out_str += (vm_maps.regions[i].permission & VM_EXEC) ? "x" : "-";
        out_str += (vm_maps.regions[i].permission & VM_SHARED) ? "-" : "p";
        if (vm_maps.regions[i].program) prog_name = vm_maps.regions[i].program->name;

        j["VM Maps"][i]["Addr Beg"]        = addr_to_str(address.beg).c_str();
        j["VM Maps"][i]["Addr End"]        = addr_to_str(address.end).c_str();
        j["VM Maps"][i]["Permission"]      = out_str;
        j["VM Maps"][i]["Path Name"]       = vm_maps.regions[i].pathname;
        j["VM Maps"][i]["Bind to Program"] = prog_name;
    }
    fprintf(fp, "%s\n", j.dump(4).c_str());

    fclose(fp);
}

void ET_Process::dump_phase_history(void)
{
    char        file_path[512] = {0};
    std::string output_path =
      std::string(VPMU.output_path) + "/phase/" + std::to_string(pid);
    boost::filesystem::create_directory(output_path);

    // Output in plain text format
    sprintf(file_path, "%s/phase_history", output_path.c_str());
    if (phase_history.size() == 0) return;
    FILE* fp = fopen(file_path, "wt");
    for (int i = 0; i < phase_history.size() - 1; i++) {
        fprintf(fp, "%" PRIu64 ",", phase_history[i].second);
    }
    fprintf(fp, "%" PRIu64, phase_history.back().second);
    fclose(fp);

    sprintf(file_path, "%s/phase_timestamp", output_path.c_str());
    if (phase_history.size() == 0) return;
    fp = fopen(file_path, "wt");
    for (int i = 0; i < phase_history.size() - 1; i++) {
        fprintf(fp, "%" PRIu64 ",", phase_history[i].first);
    }
    fprintf(fp, "%" PRIu64, phase_history.back().first);
    fclose(fp);
}

std::string ET_Program::find_code_line_number(uint64_t pc)
{
    if (is_shared_library) {
        pc -= address_start;
    }
    if (pc < section_table[".text"].beg || pc > section_table[".text"].end) {
        // Return not found to all non-text sections
        return "";
    }
    auto low = line_table.lower_bound(pc);
    if (low == line_table.end()) {
        // Not found
    } else if (low == line_table.begin()) {
        // Found first element
        if (low->first == pc) return low->second;
        // Not found
    } else {
        // Found but it's greater than pc
        if (low->first > pc) low--;
        // low is now the biggest smaller/equal to pc
        return low->second;
    }
    return "";
}

std::string ET_Process::find_code_line_number(uint64_t pc)
{
    for (auto& binary : binary_list) {
        if (binary->line_table.size() == 0) continue;
        auto ret = binary->find_code_line_number(pc);
        if (ret != "") return ret;
    }
    return "";
}

void ET_Process::dump_phase_code_mapping(FILE* fp, const Phase& phase)
{
    nlohmann::json j;

    // Reset this walk count vector for counting current phase only
    for (auto& binary : binary_list) {
        binary->reset_walk_count();
    }

    for (auto&& wc : phase.code_walk_count) {
        auto&& key   = wc.first;
        auto&& value = wc.second;
        for (auto& binary : binary_list) {
            if (binary->address_start == 0) continue;
            if (key.first >= binary->address_start && key.second <= binary->address_end) {
                // Consider both ARM and Thumb mode
                for (uint64_t i = key.first; i < key.second; i += 2) {
                    binary->walk_count_vector[i - binary->address_start] += value;
                }
            }
        }
    }

    for (auto& binary : binary_list) {
        for (int offset = 0; offset < binary->walk_count_vector.size(); offset++) {
            if (binary->walk_count_vector[offset] != 0) {
                auto key = binary->find_code_line_number(offset + binary->address_start);
                if (key.size() == 0) continue; // Skip unknown lines
                // If two keys are the same, this assignment would solve it anyway
                j[key] = binary->walk_count_vector[offset];
            }
        }
    }
    fprintf(fp, "%s\n\n\n", j.dump(4).c_str());
}

void ET_Process::dump_phase_result(void)
{
    char        file_path[512] = {0};
    std::string output_path =
      std::string(VPMU.output_path) + "/phase/" + std::to_string(pid);

    CONSOLE_LOG(STR_PHASE "Phase log path: %s\n", output_path.c_str());
    boost::filesystem::create_directory(output_path);

    dump_process_info();
    dump_vm_map();
    dump_phase_history();
    for (int idx = 0; idx < phase_list.size(); idx++) {
        sprintf(file_path, "%s/phase-%05d", output_path.c_str(), idx);
        FILE* fp = fopen(file_path, "wt");
        phase_list[idx].dump_metadata(fp);
        dump_phase_code_mapping(fp, phase_list[idx]);
        phase_list[idx].dump_result(fp);
        fclose(fp);
    }
}

void et_set_process_cpu_state(uint64_t pid, void* cs)
{
    event_tracer.set_process_cpu_state(pid, cs);
}

static std::shared_ptr<ET_Program> find_library(const char* fullpath)
{
    if (fullpath == nullptr || strlen(fullpath) == 0) return nullptr;
    return event_tracer.find_program(fullpath);
}

void et_add_process_mapped_region(uint64_t pid, MMapInfo mmap_info)
{
    std::shared_ptr<ET_Program> program = nullptr;

    uint64_t start_addr = mmap_info.vaddr;
    uint64_t end_addr   = mmap_info.vaddr + mmap_info.len;
    uint64_t mode       = mmap_info.mode;
    char*    fullpath   = mmap_info.fullpath;
    auto     process    = event_tracer.find_process(pid);
    if (process == nullptr) return;

    if (process->binary_loaded_flag == false && (mode & VM_EXEC)) {
        // The program is main program, rename the real path and filename
        program           = process->get_main_program();
        program->filename = vpmu::utils::get_file_name_from_path(fullpath);
        program->path     = fullpath;

        process->binary_loaded_flag = true;
        process->append_debug_log("Main Program\n");
    } else {
        if (strlen(fullpath) == 0) {
            process->append_debug_log("Anonymous Mapping\n");
        } else {
            // Mapping executable page for shared library
            program = find_library(fullpath);
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
    if (program == nullptr) {
        process->vm_maps.map_region(start_addr, end_addr, mode, fullpath);
    } else {
        process->vm_maps.map_region(program, start_addr, end_addr, mode, fullpath);
    }
}

void et_update_program_elf_dwarf(const char* name, const char* file_name)
{
    auto program = event_tracer.find_program(name);
    event_tracer.update_elf_dwarf(program, file_name);
}
