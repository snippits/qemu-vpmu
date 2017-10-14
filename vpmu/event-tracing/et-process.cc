extern "C" {
#include "vpmu-common.h"   // Include common C headers
#include "vpmu/linux-mm.h" // VM_EXEC and other mmap() mode states
}

#include "event-tracing.hpp" // Event tracing functions
#include "et-process.hpp"    // ET_Process class
#include "region-info.hpp"   // RegionInfo class

#include "vpmu-template-output.hpp" // vpmu::dump::snapshot

#include <boost/filesystem.hpp> // boost::filesystem

RegionInfo RegionInfo::not_found = {};

void ET_Process::append_debug_log(std::string mesg)
{
#ifdef CONFIG_VPMU_DEBUG_MSG
    debug_log += mesg;
#endif
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

void ET_Process::dump_vm_map(std::string path)
{
    FILE* fp = fopen(path.c_str(), "wt");
    if (fp == nullptr) return;
    // Use max_vm_maps instead of vm_maps for coverage
    for (auto& reg : max_vm_maps.regions) {
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
                reg.owner.first.c_str(), // name
                reg.owner.second);       // pc address
    }
    fclose(fp);
}

void ET_Process::dump_process_info(std::string path)
{
    // For shorter line
    using vpmu::str::addr_to_str;

    nlohmann::json j;
    j["apiVersion"]   = SNIPPIT_JSON_API_VERSION;
    j["name"]         = name;
    j["fileName"]     = filename;
    j["filePath"]     = path;
    j["pid"]          = pid;
    j["log"]          = debug_log;
    j["isTopProcess"] = is_top_process;
    for (auto& child : child_list) {
        j["childrens"].push_back({{"name", child->name}, {"pid", child->pid}});
    }
    for (auto& binary : binary_list) {
        nlohmann::json b;
        binary->dump_json(b);

        auto address = vm_maps.find_address(binary, VM_EXEC);
        b["addrBeg"] = addr_to_str(address.beg);
        b["addrEnd"] = addr_to_str(address.end);

        j["binaries"].push_back(b);
    }
    for (auto& region : vm_maps.regions) {
        std::string perm_str  = "";
        std::string prog_name = "";
        perm_str += (region.permission & VM_READ) ? "r" : "-";
        perm_str += (region.permission & VM_WRITE) ? "w" : "-";
        perm_str += (region.permission & VM_EXEC) ? "x" : "-";
        perm_str += (region.permission & VM_SHARED) ? "-" : "p";
        if (region.program) prog_name = region.program->name;

        nlohmann::json m;
        m["addrBeg"]       = addr_to_str(region.address.beg);
        m["addrEnd"]       = addr_to_str(region.address.end);
        m["permission"]    = perm_str;
        m["name"]          = vpmu::file::basename(region.pathname);
        m["filePath"]      = region.pathname;
        m["owner"]         = region.owner.first;
        m["pc"]            = region.owner.second;
        m["bindToProgram"] = prog_name;

        j["vmMaps"].push_back(m);
    }

    FILE* fp = fopen(path.c_str(), "wt");
    if (fp) {
        fprintf(fp, "%s\n", j.dump(2).c_str());
        fclose(fp);
    }
}

void ET_Process::dump_phases(std::string path)
{
    nlohmann::json j;

    j["apiVersion"] = SNIPPIT_JSON_API_VERSION;
    j["timeline"]   = phase_history;

    for (auto& phase : phase_list) {
        nlohmann::json p;
        p["fingerprint"] = phase.json_fingerprint();
        p["counters"]    = vpmu::dump_json::snapshot(phase.snapshot);
        auto mapping     = get_code_mapping(phase); // Note: This is sorted by std::map
        p["codes"]       = mapping;

        j["phase"][phase.id] = p;
    }

    FILE* fp = fopen(path.c_str(), "wt");
    if (fp) {
        fprintf(fp, "%s\n", j.dump(2).c_str());
        fclose(fp);
    }
}

std::map<std::string, uint64_t> ET_Process::get_code_mapping(const Phase& phase)
{
    struct RegionWithCounters {
        Pair_beg_end                addr;
        std::shared_ptr<ET_Program> program;
        bool                        has_dwarf;
        std::vector<uint64_t>       walk_count;
    };
    std::vector<struct RegionWithCounters> walk_count_vectors;

    std::map<std::string, uint64_t> ret;

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
            ret[region.program->name] = region.walk_count[0];
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
            ret[key] = region.walk_count[offset];
        }
    }

    return ret;
}

void ET_Process::dump(void)
{
    auto output_dir = std::string(VPMU.output_path) + "/proc/" + std::to_string(pid);
    boost::filesystem::create_directory(output_dir);

    CONSOLE_LOG(STR_PHASE "Phase log path: %s\n", output_dir.c_str());

    // process_info
    dump_process_info(output_dir + "/process_info");
    dump_vm_map(output_dir + "/vm_maps");
    dump_phases(output_dir + "/phases");

    auto  file_path = output_dir + "/profiling";
    FILE* fp        = fopen(file_path.c_str(), "wt");
    fprintf(fp, "====== Per-core information  ======\n");
    vpmu::dump::snapshot(fp, this->prof_counters);
    fprintf(fp, "\n\n====== Total information  ======\n");
    this->prof_counters.sum_cores();
    vpmu::dump::snapshot(fp, this->prof_counters);
    fclose(fp);
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
    if (functions.call(vaddr, env, this)) {
        functions.update_return_key(vaddr, et_get_ret_addr(env));
        return true;
    }
    if (functions.call_return(vaddr, env, this)) return true;
    return false;
}
