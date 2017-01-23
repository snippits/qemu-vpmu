extern "C" {
#include "vpmu-common.h"           // Include common C headers
#include "vpmu/include/linux-mm.h" // VM_EXEC and other mmap() mode states
}
#include "elf++.hh"          // elf::elf
#include "event-tracing.hpp" // EventTracer
#include "phase/phase.hpp"   // Phase class
#include "json.hpp"          // nlohmann::json

#include <boost/filesystem.hpp> // boost::filesystem

EventTracer event_tracer;

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
    elf::elf f(elf::create_mmap_loader(fd));
    for (auto& sec : f.sections()) {
        if (sec.get_hdr().type != elf::sht::symtab
            && sec.get_hdr().type != elf::sht::dynsym)
            continue;
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
}

void EventTracer::parse_and_set_kernel_symbol(const char* filename)
{
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        LOG_FATAL("Kernel File %s not found!", filename);
        return;
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

        for (auto sym : sec.as_symtab()) {
            auto& d = sym.get_data();
            if (d.type() == elf::stt::func) {
                kernel.add_symbol(sym.get_name(), d.value);

                bool print_content_flag = true;
                if (sym.get_name() == "do_execve") {
                    kernel.set_event_address(ET_KERNEL_EXECV, d.value);
                } else if (sym.get_name() == "__switch_to") {
                    kernel.set_event_address(ET_KERNEL_CONTEXT_SWITCH, d.value);
                } else if (sym.get_name() == "do_exit") {
                    kernel.set_event_address(ET_KERNEL_EXIT, d.value);
                } else if (sym.get_name() == "wake_up_new_task") {
                    kernel.set_event_address(ET_KERNEL_WAKE_NEW_TASK, d.value);
                } else if (sym.get_name() == "do_fork") {
                    kernel.set_event_address(ET_KERNEL_FORK, d.value);
                } else if (sym.get_name() == "mmap_region") {
                    kernel.set_event_address(ET_KERNEL_MMAP, d.value);
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
        if (kernel.find_vaddr(ET_KERNEL_FORK) == 0)
            LOG_FATAL("Kernel event \"%s\" was not found!", "do_fork");
        if (kernel.find_vaddr(ET_KERNEL_WAKE_NEW_TASK) == 0)
            LOG_FATAL("Kernel event \"%s\" was not found!", "wake_up_new_task");
        if (kernel.find_vaddr(ET_KERNEL_EXIT) == 0)
            LOG_FATAL("Kernel event \"%s\" was not found!", "do_exit");
        if (kernel.find_vaddr(ET_KERNEL_CONTEXT_SWITCH) == 0)
            LOG_FATAL("Kernel event \"%s\" was not found!", "__switch_to");
        if (kernel.find_vaddr(ET_KERNEL_EXECV) == 0)
            LOG_FATAL("Kernel event \"%s\" was not found!", "do_execve");
    }
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

    log_debug("Printing all traced program with its information"
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

void et_add_new_process(const char* path, const char* name, uint64_t pid)
{
    if (path == nullptr)
        event_tracer.add_new_process(name, pid);
    else
        event_tracer.add_new_process(path, name, pid);
}

void ET_Process::dump_process_info(void)
{
    char        file_path[512] = {0};
    std::string output_path    = "/tmp/vpmu/phase/" + std::to_string(pid);
    boost::filesystem::create_directory(output_path);

    sprintf(file_path, "%s/process_info", output_path.c_str());
    FILE* fp = fopen(file_path, "wt");

    nlohmann::json j;
    j["Name"]      = name;
    j["File Name"] = filename;
    j["File Path"] = path;
    j["pid"]       = pid;
    for (int i = 0; i < child_list.size(); i++) {
        j["Childrens"][i]["Name"] = child_list[i]->name;
        j["Childrens"][i]["pid"]  = child_list[i]->pid;
    }
    for (int i = 0; i < binary_list.size(); i++) {
        j["Binaries"][i]["Name"]      = binary_list[i]->name;
        j["Binaries"][i]["File Name"] = binary_list[i]->filename;
        j["Binaries"][i]["Path"]      = binary_list[i]->path;
        j["Binaries"][i]["Symbols"]   = binary_list[i]->sym_table.size();
        j["Binaries"][i]["Libraries"] = binary_list[i]->library_list.size();
    }
    fprintf(fp, "%s\n", j.dump(4).c_str());

    fclose(fp);
}

void ET_Process::dump_phase_result(void)
{
    char        file_path[512] = {0};
    std::string output_path    = "/tmp/vpmu/phase/" + std::to_string(pid);

    CONSOLE_LOG(STR_PHASE "Phase log path: %s\n", output_path.c_str());
    dump_process_info();
    boost::filesystem::create_directory(output_path);
    for (int idx = 0; idx < phase_list.size(); idx++) {
        sprintf(file_path, "%s/phase-%05d", output_path.c_str(), idx);
        FILE* fp = fopen(file_path, "wt");
        phase_list[idx].dump_metadata(fp);
        phase_list[idx].dump_lines(fp);
        phase_list[idx].dump_result(fp);
        fclose(fp);
    }
}

void et_remove_process(uint64_t pid)
{
    auto process = event_tracer.find_process(pid);
    if (process != nullptr) {
        process->dump_phase_result();
        event_tracer.remove_process(pid);
    }
}

void et_set_process_cpu_state(uint64_t pid, void* cs)
{
    event_tracer.set_process_cpu_state(pid, cs);
}

void et_add_process_mapped_file(uint64_t pid, const char* fullpath, uint64_t mode)
{
    if (mode & VM_EXEC) {
        // Mapping executable page for shared library
        et_attach_shared_library_to_process(pid, fullpath);
    } else {
        // TODO Just records all non-library files mapped to this process
    }
}

void et_attach_shared_library_to_process(uint64_t pid, const char* fullpath_lib)
{
    std::shared_ptr<ET_Process> process = event_tracer.find_process(pid);
    if (process == nullptr) return;
    std::shared_ptr<ET_Program> program = event_tracer.find_program(fullpath_lib);
    if (program == nullptr) {
        DBG(STR_VPMU "Shared library %s was not found in the list "
                     "create an empty one.\n",
            fullpath_lib);
        program = event_tracer.add_library(fullpath_lib);
    }

    if (process->binary_list.size() == 0) {
        auto name = vpmu::utils::get_file_name_from_path(fullpath_lib);
        if (name != program->filename) {
            // The program is main program, rename the real path and filename
            program->filename = name;
            program->path     = fullpath_lib;
        }
    } else {
        event_tracer.attach_to_program(process->get_main_program()->name, program);
    }
    process->push_binary(program);
}

void et_update_program_elf_dwarf(const char* name, const char* file_name)
{
    auto program = event_tracer.find_program(name);
    event_tracer.update_elf_dwarf(program, file_name);
}
