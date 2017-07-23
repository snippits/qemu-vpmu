extern "C" {
#include "vpmu-common.h"   // Include common C headers
#include "vpmu/linux-mm.h" // VM_EXEC and other mmap() mode states
}

#include "event-tracing.hpp" // Event tracing functions
#include "et-process.hpp"    // ET_Process class
#include "region-info.hpp"   // RegionInfo class

#include <boost/filesystem.hpp> // boost::filesystem

RegionInfo RegionInfo::not_found = {};

void ET_Process::append_debug_log(std::string mesg)
{
#ifdef CONFIG_VPMU_DEBUG_MSG
    debug_log += mesg;
#endif
}

void ET_Process::attach_child_pid(uint64_t child_pid)
{
    child_list.push_back(std::make_shared<ET_Process>(this, child_pid));
}

void ET_Process::push_child_process(std::shared_ptr<ET_Process>& process)
{
    if (process != nullptr) child_list.push_back(process);
}

void ET_Process::push_binary(std::shared_ptr<ET_Program>& program)
{
    // Check repeated pointer
    for (auto& binary : binary_list) {
        if (binary == program) return;
    }
    if (program != nullptr) binary_list.push_back(program);
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
                "%-5s %-40s %-20s %16s@%-16lx"
                "\n",
                vpmu::str::addr_to_str(reg.address.beg).c_str(),
                vpmu::str::addr_to_str(reg.address.end).c_str(),
                out_str.c_str(),
                reg.pathname.c_str(),
                prog_name.c_str(),
                reg.mapper.first.c_str(), // name
                reg.mapper.second);       // pc address
    }
    fclose(fp);
}

void ET_Process::dump_process_info(void)
{
    using vpmu::str::addr_to_str;
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
        j["VM Maps"][i]["Mapped By"]       = vm_maps.regions[i].mapper.first;
        j["VM Maps"][i]["Map PC"]          = vm_maps.regions[i].mapper.second;
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

void ET_Process::dump_phase_code_mapping(FILE* fp, const Phase& phase)
{
    struct RegionWithCounters {
        Pair_beg_end                addr;
        std::shared_ptr<ET_Program> program;
        bool                        has_dwarf;
        std::vector<uint64_t>       walk_count;
    };
    std::vector<struct RegionWithCounters> walk_count_vectors;

    nlohmann::json j;

    // Find out all executable and tracked regions
    for (auto& region : vm_maps.regions) {
        if (region.permission & VM_EXEC && region.program != nullptr) {
            walk_count_vectors.push_back({region.address, region.program, false, {}});
        }
    }

    // Reset all walk count vectors
    for (auto& region : walk_count_vectors) {
        if (region.program->line_table.size() > 0) {
            region.walk_count.resize(region.addr.end - region.addr.beg);
            std::fill(region.walk_count.begin(), region.walk_count.end(), 0);
            region.has_dwarf = true;
        } else {
            // No DRAWF info, use only one counter
            region.walk_count.resize(1);
            region.walk_count[0] = 0;
        }
    }

    for (auto&& wc : phase.code_walk_count) {
        auto&&   addr       = wc.first;
        auto&&   value      = wc.second;
        uint64_t start_addr = addr.beg;
        uint64_t end_addr   = addr.end;
        for (auto& region : walk_count_vectors) {
            // Find regions that matches
            if (addr.end >= region.addr.beg && addr.end <= region.addr.end) {
                uint64_t base_addr = region.addr.beg;
                if (region.has_dwarf) {
                    for (uint64_t i = start_addr; i <= end_addr; i++) {
                        region.walk_count[i - base_addr] += value;
                    }
                } else {
                    // Let's roughly count the byte (ranges / 4) as the counts of walks
                    // This is not correct... but there is no way to solve it here...
                    region.walk_count[0] += 1 + (end_addr - start_addr) / 4;
                }
            }
        }
    }

    for (auto& region : walk_count_vectors) {
        if (!region.has_dwarf) {
            j["[" + region.program->name + "]"] = region.walk_count[0];
            continue;
        }

        auto&    program   = region.program;
        uint64_t base_addr = region.addr.beg;
        for (int offset = 0; offset < region.walk_count.size(); offset++) {
            if (region.walk_count[offset] == 0) continue;
            uint64_t    addr = (program->is_shared_library) ? offset : base_addr + offset;
            std::string key  = program->find_code_line_number(addr);
            if (key.size() == 0) continue; // Skip unknown lines
            // If two keys are the same, this assignment would solve it anyway
            j[key] = region.walk_count[offset];
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

std::string ET_Process::find_code_line_number(uint64_t pc)
{
    for (auto& binary : binary_list) {
        if (binary->line_table.size() == 0) continue;
        auto ret = binary->find_code_line_number(pc);
        if (ret != "") return ret;
    }
    return "";
}

uint64_t ET_Process::get_symbol_addr(std::string name)
{
    for (auto& region : this->vm_maps.regions) {
        if (auto binary = region.program) {
            if (binary->sym_table.size() == 0) continue;
            if (binary->sym_table.find(name) == binary->sym_table.end()) continue;

            // addr == 0 means the symbol is externed from other libs (relocation sym.)
            if (uint64_t addr = binary->sym_table[name]) {
                // Add offsets if it is a shared library
                if (binary->is_shared_library)
                    return addr + region.address.beg;
                else
                    return addr;
            }
        }
    }
    return 0;
}

bool ET_Process::call_event(void* env, uint64_t vaddr)
{
    if (functions.call(vaddr, et_get_ret_addr(env), env, this)) return true;
    if (functions.call_return(vaddr, env, this)) return true;
    return false;
}
