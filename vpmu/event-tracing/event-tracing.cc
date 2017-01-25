extern "C" {
#include "vpmu-common.h"           // Include common C headers
#include "vpmu/include/linux-mm.h" // VM_EXEC and other mmap() mode states
}
#include "elf++.hh"          // elf::elf
#include "dwarf++.hh"        // dwarf::dwarf
#include "event-tracing.hpp" // EventTracer
#include "phase/phase.hpp"   // Phase class
#include "json.hpp"          // nlohmann::json

#include <boost/filesystem.hpp> // boost::filesystem

EventTracer event_tracer;

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
    elf::elf ef(elf::create_mmap_loader(fd));
    for (auto& sec : ef.sections()) {
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

    try {
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
    } catch (dwarf::format_error e) {
        log_debug("Warning: Target binary '%s' does not contatin dwarf sections",
                  program->name.c_str());
        return;
    }
}

void EventTracer::parse_and_set_kernel_symbol(const char* filename, const char* version)
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
                } else if (sym.get_name() == "_do_fork") {
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
            LOG_FATAL("Kernel event \"%s\" was not found!", "_do_fork");
        if (kernel.find_vaddr(ET_KERNEL_WAKE_NEW_TASK) == 0)
            LOG_FATAL("Kernel event \"%s\" was not found!", "wake_up_new_task");
        if (kernel.find_vaddr(ET_KERNEL_EXIT) == 0)
            LOG_FATAL("Kernel event \"%s\" was not found!", "do_exit");
        if (kernel.find_vaddr(ET_KERNEL_CONTEXT_SWITCH) == 0)
            LOG_FATAL("Kernel event \"%s\" was not found!", "__switch_to");
        if (kernel.find_vaddr(ET_KERNEL_EXECV) == 0)
            LOG_FATAL("Kernel event \"%s\" was not found!", "do_execve");

        // This must be done when kernel symbol is set, or emulation would hang or SEGV
        et_set_default_linux_struct_offset(version);
    }
    close(fd);
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
    char        temp_str[512]  = {0};
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
    for (int i = 0; i < child_list.size(); i++) {
        j["Childrens"][i]["Name"] = child_list[i]->name;
        j["Childrens"][i]["pid"]  = child_list[i]->pid;
    }
    for (int i = 0; i < binary_list.size(); i++) {
        j["Binaries"][i]["Name"]      = binary_list[i]->name;
        j["Binaries"][i]["File Name"] = binary_list[i]->filename;
        j["Binaries"][i]["Path"]      = binary_list[i]->path;
        j["Binaries"][i]["Symbols"]   = binary_list[i]->sym_table.size();
        j["Binaries"][i]["DWARF"]     = binary_list[i]->line_table.size();
        j["Binaries"][i]["Libraries"] = binary_list[i]->library_list.size();
        // TODO these vars would be moved away
        sprintf(temp_str, "%p", (void*)binary_list[i]->address_start);
        j["Binaries"][i]["Addr Beg"] = temp_str;
        sprintf(temp_str, "%p", (void*)binary_list[i]->address_end);
        j["Binaries"][i]["Addr End"] = temp_str;
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
    FILE* fp = fopen(file_path, "wt");
    for (int i = 0; i < phase_history.size() - 1; i++) {
        fprintf(fp, "%" PRIu64 ",", phase_history[i]);
    }
    fprintf(fp, "%" PRIu64, phase_history.back());
    fclose(fp);
}

std::string ET_Program::find_code_line_number(uint64_t pc)
{
    if (is_shared_library) {
        pc -= address_start;
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

// TODO Find a better way
void et_update_last_mmaped_binary(uint64_t pid, uint64_t vaddr, uint64_t len)
{
    auto process = event_tracer.find_process(pid);
    if (process != nullptr) {
        process->binary_list.back()->set_mapped_address(vaddr, vaddr + len);
    }
}
