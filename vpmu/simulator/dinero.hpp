#ifndef __CACHE_DINERO_HPP_
#define __CACHE_DINERO_HPP_
#pragma once

#include <string>                   // std::string
#include "vpmu-sim.hpp"             // VPMUSimulator
#include "vpmu-cache-packet.hpp"    // VPMU_Cache
#include "vpmu-utils.hpp"           // miscellaneous functions
#include "vpmu-template-output.hpp" // Template output format

// TODO This feature consume a lot of execution time and lines of codes
// It is still doubtful whether this is a necessary feature
// #define EXPERIMENTAL_PER_CORE_CYCLES

extern "C" {
#include "d4-7/d4.h"
}

// TODO refactor this code
//
#define LRU ((char *)"LRU")
#define FIFO ((char *)"FIFO")
#define RANDOM ((char *)"RANDOM")
#define DEMAND_ONLY ((char *)"DEMAND_ONLY")
#define ALWAYS ((char *)"ALWAYS")
#define MISS ((char *)"MISS")
#define TAGGED ((char *)"TAGGED")
#define LOAD_FORWARD ((char *)"LOAD_FORWARD")
#define SUB_BLOCK ((char *)"SUB_BLOCK")
#define IMPOSSIBLE ((char *)"IMPOSSIBLE")
#define NEVER ((char *)"NEVER")
#define NO_FETCH ((char *)"NO_FETCH")

#define MAX_D4_CACHES 128

using nlohmann::json;
class Cache_Dinero : public VPMUSimulator<VPMU_Cache>
{
    /*    Sample cache topology
     *            L2
     *          /    \
     *        L1      L1
     *      D   I   D   I
     * ----------------------------
     *  [L2, L1D, L1D, L1I, L1I]
     *  is the array representing the tree topology above
     */
    typedef struct D4_CACHE_CONFIG {
        d4cache *cache;
        int      level;
        int      core;
    } D4_CACHE_CONFIG;

    // This is a structure that sums the dinero cache data
    // It's only used in the following situations
    // 1. Synchronizing the data from dinero to VPMU
    // 2. Printing the results of dinero cache simulation
    typedef struct {
        double fetch_data, fetch_read, fetch_alltype;
        double data, data_read, data_alltype;
    } Demand_Data;

    d4cache *d4_mem_create(void)
    {
        d4cache *mem = d4new(NULL);

        if (mem == NULL) ERR_MSG("Main mem error \n");
        mem->name = strdup("dinero Main Memory ");

        return mem;
    }

    void reset_single_d4_cache(d4cache *c)
    {
        // Clear the whole memory region of counters, including
        // General Misses:    fetch, miss, blockmiss,
        // Compulsory Misses: comp_miss, comp_blockmiss
        // Capacity Misse:    scap_miss, cap_blockmiss
        // Conflict Misses:   conf_miss, conf_blockmiss
        memset(c->fetch, 0, sizeof(double) * (2 * D4NUMACCESSTYPES) * 9);
        c->multiblock    = 0;
        c->bytes_read    = 0;
        c->bytes_written = 0;
    }

    Demand_Data inline calculate_data(d4cache *c)
    {
        Demand_Data d;

        d.fetch_data    = c->fetch[D4XMISC] + c->fetch[D4XREAD] + c->fetch[D4XWRITE];
        d.fetch_read    = c->fetch[D4XMISC] + c->fetch[D4XREAD] + c->fetch[D4XINSTRN];
        d.fetch_alltype = d.fetch_read + c->fetch[D4XWRITE];
        d.data          = c->miss[D4XMISC] + c->miss[D4XREAD] + c->miss[D4XWRITE];
        d.data_read     = c->miss[D4XMISC] + c->miss[D4XREAD] + c->miss[D4XINSTRN];
        d.data_alltype  = d.data_read + c->miss[D4XWRITE];

        return d;
    }

    // This is old dump function which prints all info (including GPU)
    void dump_info(int id)
    {
        int i;

        CONSOLE_LOG("  [%d] type : dinero\n", id);
#ifdef EXPERIMENTAL_PER_CORE_CYCLES
        CONSOLE_LOG("    cycles:\n");
        for (i = 0; i < platform_info.cpu.cores; i++) {
            CONSOLE_LOG("      [%d]: %" PRIu64 "\n", i, cycles[0][i]);
        }
#endif
        // Dump info
        CONSOLE_LOG("       (Miss Rate)   "
                    "|    Access Count    "
                    "|   Read Miss Count  "
                    "|  Write Miss Count  "
                    "|\n");
        // Memory
        d4cache *   c = d4_cache[0].cache;
        Demand_Data d = calculate_data(c);
        CONSOLE_LOG("    -> memory (%0.2lf) |%'20" PRIu64 "|%'20" PRIu64 "|%'20" PRIu64
                    "|\n",
                    (double)0.0,
                    (uint64_t)(d.fetch_read),
                    (uint64_t)0,
                    (uint64_t)0);

        for (i = 1; i < MAX_D4_CACHES && d4_cache[i].cache != NULL; i++) {
            d4cache *   c = d4_cache[i].cache;
            Demand_Data d = calculate_data(c);

            if (c->flags & D4F_RO) {
                // i-cache
                CONSOLE_LOG("    -> L%d-I   (%0.2lf) |%'20" PRIu64 "|%'20" PRIu64
                            "|%'20" PRIu64 "|\n",
                            d4_cache[i].level,
                            (double)d.data_read / d.fetch_read,
                            (uint64_t)(d.fetch_read),
                            (uint64_t)(d.data_read),
                            (uint64_t)0);
            } else {
                // d-cache
                CONSOLE_LOG("    -> L%d-D   (%0.2lf) |%'20" PRIu64 "|%'20" PRIu64
                            "|%'20" PRIu64 "|\n",
                            d4_cache[i].level,
                            d.data_alltype / d.fetch_alltype,
                            (uint64_t)(d.fetch_alltype),
                            (uint64_t)(d.data_read),
                            (uint64_t)(c->miss[D4XWRITE]));
            }
        }
    }

    void set_d4_cache(d4cache *c, int extra_flag, const char *key, const char *val)
    {
#define IF_KEY_IS(_k, _callback)                                                         \
    if (strcmp(key, _k) == 0) {                                                          \
        _callback;                                                                       \
        return;                                                                          \
    }

        DBG("\t%s: %s\n", key, val);

        IF_KEY_IS("name", c->name = strdup(val));
        IF_KEY_IS("processor", c->name = strdup(val));
        IF_KEY_IS("blocksize", c->lg2blocksize = vpmu::math::ilog2(atoi(val)));
        IF_KEY_IS("subblocksize", c->lg2subblocksize = vpmu::math::ilog2(atoi(val)));
        IF_KEY_IS("size", c->lg2size = vpmu::math::ilog2(atoi(val)));
        IF_KEY_IS("assoc", c->assoc = atoi(val));
        IF_KEY_IS("split_3c_cnt", c->flags |= atoi(val) ? D4F_CCC : 0);
        c->flags |= extra_flag;

        IF_KEY_IS("prefetch_abortpercent", c->prefetch_abortpercent = atoi(val));
        IF_KEY_IS("prefetch_distance", c->prefetch_distance = atoi(val));

        IF_KEY_IS("replacement", c->name_replacement = strdup(val);
                  if (strcmp(val, LRU) == 0) c->replacementf         = d4rep_lru;
                  else if (strcmp(val, FIFO) == 0) c->replacementf   = d4rep_fifo;
                  else if (strcmp(val, RANDOM) == 0) c->replacementf = d4rep_random;
                  else ERR_MSG("JSON: not a valid option\n %s: %s\n", key, val););

        IF_KEY_IS(
          "prefetch", c->name_prefetch = strdup(val);
          if (strcmp(val, DEMAND_ONLY) == 0) c->prefetchf       = d4prefetch_none;
          else if (strcmp(val, ALWAYS) == 0) c->prefetchf       = d4prefetch_always;
          else if (strcmp(val, MISS) == 0) c->prefetchf         = d4prefetch_miss;
          else if (strcmp(val, TAGGED) == 0) c->prefetchf       = d4prefetch_tagged;
          else if (strcmp(val, LOAD_FORWARD) == 0) c->prefetchf = d4prefetch_loadforw;
          else if (strcmp(val, SUB_BLOCK) == 0) c->prefetchf    = d4prefetch_subblock;
          else ERR_MSG("JSON: not a valid option\n %s: %s\n", key, val););

        IF_KEY_IS("walloc", c->name_walloc = strdup(val);
                  if (strcmp(val, IMPOSSIBLE) == 0) c->wallocf    = d4walloc_impossible;
                  else if (strcmp(val, ALWAYS) == 0) c->wallocf   = d4walloc_always;
                  else if (strcmp(val, NEVER) == 0) c->wallocf    = d4walloc_never;
                  else if (strcmp(val, NO_FETCH) == 0) c->wallocf = d4walloc_nofetch;
                  else ERR_MSG("JSON: not a valid option\n %s: %s\n", key, val););

        IF_KEY_IS("wback", c->name_wback = strdup(val);
                  if (strcmp(val, IMPOSSIBLE) == 0) c->wbackf    = d4wback_impossible;
                  else if (strcmp(val, ALWAYS) == 0) c->wbackf   = d4wback_always;
                  else if (strcmp(val, NEVER) == 0) c->wbackf    = d4wback_never;
                  else if (strcmp(val, NO_FETCH) == 0) c->wbackf = d4wback_nofetch;
                  else ERR_MSG("JSON: not a valid option\n %s: %s\n", key, val););

        ERR_MSG("JSON: option not found\n %s: %s\n", key, val);
#undef IF_KEY_IS
    }

    d4cache *parse_and_set(
      json &root, D4_CACHE_CONFIG *parent, int extra_flag, int level, int core)
    {
        d4cache *child = NULL;

        if (root["name"] != nullptr)
            DBG("level:%d ->  %s\n",
                level,
                vpmu::utils::get_json<std::string>(root, "name").c_str());
        if (root["processor"] != nullptr)
            DBG("level:%d ->  %s\n",
                level,
                vpmu::utils::get_json<std::string>(root, "processor").c_str());
        // Create a new cache configuration
        child                         = d4new(parent->cache);
        d4_cache[d4_num_caches].cache = child;
        d4_cache[d4_num_caches].level = level;
        d4_cache[d4_num_caches].core  = core;
        d4_num_caches++;

        // ERR_MSG("%s\n\n", root.dump().c_str());
        for (json::iterator it = root.begin(); it != root.end(); ++it) {
            // Skip the attribute next
            std::string key = it.key();
            if (key == "next" || root[key].is_null()) continue;
            std::string value;
            if (root[key].is_string())
                value = it.value();
            else if (root[key].is_number())
                value = std::to_string((int)it.value());
            else
                continue;
            set_d4_cache(child, extra_flag, key.c_str(), value.c_str());
        }

        return child;
    }

    int get_processor_index(json obj)
    {
        if (obj["processor"] == "CPU")
            return PROCESSOR_CPU;
        else if (obj["processor"] == "GPU")
            return PROCESSOR_GPU;

        return -1;
    }

    // TODO how to support multiple L2 cache?
    // BFS search to form an one dimensional array
    void recursively_parse_json(json              root,
                                D4_CACHE_CONFIG * c,
                                int               level,
                                std::vector<int> &flag_has_processor)
    {
        int i, index, local_index = 0;

        if (level == VPMU_Cache::NOT_USED) return; // There's no level 0 cache
        if (level != VPMU_Cache::L1_CACHE && root.is_array()) {
            local_index = d4_num_caches;
            for (int i = 0; i < root.size(); i++) {
                // TODO multi-processor, and how to assign core id with multiple L2 cache
                parse_and_set(root[i], c, 0x00, level, 0);
            }
            for (int i = 0; i < root.size(); i++) {
                // If there's next level of cache topology, dive into it!
                if (root[i]["next"] != nullptr) {
                    recursively_parse_json(root[i]["next"],
                                           &d4_cache[local_index],
                                           level - 1,
                                           flag_has_processor);
                }
            }
        } else {
            if (root.is_array()) {
                root = root[0];
            }
            index                     = get_processor_index(root["d-cache"]);
            int leaf_index            = core_num_table[index];
            flag_has_processor[index] = 1;

            index = get_processor_index(root["d-cache"]);
            for (i = 0; i < num_cores[index]; i++) {
                d4_cache_leaf[leaf_index] =
                  parse_and_set(root["d-cache"], c, 0x00, level, i);
                leaf_index++;
            }
            index = get_processor_index(root["i-cache"]);
            for (i = 0; i < num_cores[index]; i++) {
                d4_cache_leaf[leaf_index] =
                  parse_and_set(root["i-cache"], c, D4F_RO, level, i);
                leaf_index++;
            }
        }
    }

    void sync_cache_data(VPMU_Cache::Data &data, VPMU_Cache::Model &model)
    {
        for (int processor = 0; processor < ALL_PROC; processor++) {
            if (num_cores[processor] == 0) continue;
            for (int i = 1; i < MAX_D4_CACHES && d4_cache[i].cache != NULL; i++) {
                d4cache *   c = d4_cache[i].cache;
                Demand_Data d = calculate_data(c);

                int level = d4_cache[i].level;
                int core  = d4_cache[i].core;

                if (c->flags & D4F_RO) {
                    // i-cache
                    auto &cache = data.insn_cache[processor][level][core];
                    // Sync back values
                    cache[VPMU_Cache::READ]       = d.fetch_alltype;
                    cache[VPMU_Cache::WRITE]      = 0;
                    cache[VPMU_Cache::READ_MISS]  = d.data_alltype;
                    cache[VPMU_Cache::WRITE_MISS] = 0;
                } else {
                    // d-cache
                    auto &cache = data.data_cache[processor][level][core];
                    // Sync back values
                    cache[VPMU_Cache::READ]       = d.fetch_alltype - c->fetch[D4XWRITE];
                    cache[VPMU_Cache::WRITE]      = c->fetch[D4XWRITE];
                    cache[VPMU_Cache::READ_MISS]  = d.data_read;
                    cache[VPMU_Cache::WRITE_MISS] = c->miss[D4XWRITE];
                }
            }
        }
        Demand_Data d = calculate_data(d4_cache[0].cache);

        data.memory_accesses = d.fetch_read;
        // TODO separate sequential and random access count, add new field in config
        data.memory_time_ns =
          data.memory_accesses * model.latency[VPMU_Cache::Data_Level::MEMORY];
    }

#ifdef EXPERIMENTAL_PER_CORE_CYCLES
    void take_snapshot(VPMU_Cache::Data &data, int proc, int core)
    {
        // Make a snapshot of current value
        for (int i = VPMU_Cache::Data_Level::L1_CACHE; i < VPMU_Cache::Data_Level::MEMORY;
             i++) {
            auto &&icache = data.insn_cache[proc][i][core];
            auto &&dcache = data.data_cache[proc][i][core];

            auto &&s_icache = cache_data_snapshot.insn_cache[proc][i][core];
            auto &&s_dcache = cache_data_snapshot.data_cache[proc][i][core];
            for (int j = 0; j < VPMU_Cache::SIZE_OF_INDEX; j++) {
                s_icache[j] = icache[j];
                s_dcache[j] = dcache[j];
            }
            cache_data_snapshot.memory_accesses = data.memory_accesses;
        }
    }

    void update_snapshot(VPMU_Cache::Data &data, int proc, int core)
    {
        for (int i = VPMU_Cache::Data_Level::L1_CACHE; i < VPMU_Cache::Data_Level::MEMORY;
             i++) {
            auto &&icache = data.insn_cache[proc][i][core];
            auto &&dcache = data.data_cache[proc][i][core];

            auto &&s_icache = cache_data_snapshot.insn_cache[proc][i][core];
            auto &&s_dcache = cache_data_snapshot.data_cache[proc][i][core];

            icache_miss_counter[core][i] +=
              icache[VPMU_Cache::READ_MISS] - s_icache[VPMU_Cache::READ_MISS];
            icache_access_counter[core][i] +=
              (icache[VPMU_Cache::READ] - s_icache[VPMU_Cache::READ])
              + (icache[VPMU_Cache::WRITE] - s_icache[VPMU_Cache::WRITE]);

            dcache_miss_counter[core][i] +=
              (dcache[VPMU_Cache::READ_MISS] - s_dcache[VPMU_Cache::READ_MISS])
              + (dcache[VPMU_Cache::WRITE_MISS] - s_dcache[VPMU_Cache::WRITE_MISS]);
            dcache_access_counter[core][i] +=
              (dcache[VPMU_Cache::READ] - s_dcache[VPMU_Cache::READ])
              + (dcache[VPMU_Cache::WRITE] - s_dcache[VPMU_Cache::WRITE]);
        }
        memory_access_counter[core] +=
          data.memory_accesses - cache_data_snapshot.memory_accesses;

        take_snapshot(data, proc, core);
    }
#endif

    void sync_back_config_to_vpmu(VPMU_Cache::Model &model, json &config)
    {
        using vpmu::utils::get_json;
        // Copy the model name to VPMU
        auto model_name = vpmu::utils::get_json<std::string>(config, "name");
        strncpy(model.name, model_name.c_str(), sizeof(model.name));
        model.levels = get_json<int>(config, "levels");
        for (int i = VPMU_Cache::L1_CACHE; i <= model.levels; i++) {
            char field_str[128];

            sprintf(field_str, "l%d miss latency", i);
            model.latency[i] = get_json<int>(config, field_str);
            DBG("%s: %d\n", field_str, model.latency[i]);
        }
        // The latency in the spec is defined as inclusion. We need exclusion.
        for (int i = model.levels; i > VPMU_Cache::L1_CACHE; i--) {
            model.latency[i] -= model.latency[i - 1];
        }
        model.latency[VPMU_Cache::Data_Level::MEMORY] =
          get_json<int>(config, "memory_ns");

        // Pass some cache configurations to VPMU. Ex: blocksize, walloc, wback
        if (config["topology"].is_null()) return;

        for (auto elem : config["topology"]) {
            if (elem["name"].is_null()) continue;
            // In this level, there's only one possible
            // Either CPU last level or GPU last level
            if (get_json<std::string>(elem, "name").find("CPU") != std::string::npos) {
                json c_elem = elem;           // Copy of current element
                json n_elem = c_elem["next"]; // Copy of next level element
                int  level  = model.levels;
                while (n_elem != nullptr) {
                    model.d_log2_blocksize[level] =
                      vpmu::math::ilog2(get_json<int>(c_elem, "blocksize"));
                    model.d_log2_blocksize_mask[level] =
                      ~((1 << model.d_log2_blocksize[level]) - 1);
                    model.d_write_alloc[level] = (c_elem["walloc"] == "ALWAYS");
                    model.d_write_back[level]  = (c_elem["wback"] == "ALWAYS");

                    c_elem = n_elem;
                    n_elem = n_elem["next"];
                    level--;
                }

                model.d_log2_blocksize[level] =
                  vpmu::math::ilog2(get_json<int>(c_elem["d-cache"], "blocksize"));
                model.d_log2_blocksize_mask[level] =
                  ~((1 << model.d_log2_blocksize[level]) - 1);
                model.d_write_alloc[level] = (c_elem["d-cache"]["walloc"] == "ALWAYS");
                model.d_write_back[level]  = (c_elem["d-cache"]["wback"] == "ALWAYS");

                model.i_log2_blocksize[level] =
                  vpmu::math::ilog2(get_json<int>(c_elem["i-cache"], "blocksize"));
                model.i_log2_blocksize_mask[level] =
                  ~((1 << model.i_log2_blocksize[level]) - 1);

            } else if (get_json<std::string>(elem, "name").find("GPU")
                       != std::string::npos) {
                // DBG("GPU is not required yet !\n");
            }
        }

        return;
    }

public:
    Cache_Dinero() : VPMUSimulator("Dinero") {}
    ~Cache_Dinero() {}

    void destroy() override
    {
        for (int i = 0; i < MAX_D4_CACHES; i++) {
            if (d4_cache[i].cache != nullptr) {
                free(d4_cache[i].cache);
            }
        }
    }

    VPMU_Cache::Model build(void) override
    {
        log_debug("Initializing");

        log_debug(json_config.dump().c_str());

        num_cores[PROCESSOR_CPU] = platform_info.cpu.cores;
        num_cores[PROCESSOR_GPU] = platform_info.gpu.cores;
        for (int i = 1; i < MAX_D4_CACHES; i++) {
            core_num_table[i] = num_cores[i - 1] * 2 + //*2 for icache and dcache
                                core_num_table[i - 1];
        }

        // Parse JSON config
        d4_levels = vpmu::utils::get_json<int>(json_config, "levels");
        vpmu::utils::json_check_or_exit(json_config, "topology");
        d4_cache[d4_num_caches].cache = d4_mem_create();
        d4_cache[d4_num_caches].level = VPMU_Cache::MEMORY;
        d4_cache[d4_num_caches].core  = 0;
        d4_num_caches++;
        std::vector<int> flag_has_processor(ALL_PROC);
        recursively_parse_json(
          json_config["topology"], &d4_cache[0], d4_levels, flag_has_processor);
        sync_back_config_to_vpmu(cache_model, json_config);

        // Reset the configurations depending on json contents.
        // Ex: some configuration might miss GPU topology while num_gpu_core are set
        for (int i = 0; i < ALL_PROC; i++) {
            if (flag_has_processor[i] == 0) num_cores[PROCESSOR_GPU] = 0;
        }
        if (d4setup() != EXIT_SUCCESS) {
            ERR_MSG("Something wrong with dinero cache\n");
            exit(1);
        }

        log_debug("Initialized");
        return cache_model;
    }

    RetStatus packet_processor(int id, const VPMU_Cache::Reference &ref) override
    {
#ifdef EXPERIMENTAL_PER_CORE_CYCLES
        static int last_core_id = -1; // Remember the core id of last packet
        static int last_proc    = -1; // Remember the processor of last packet
#endif

#ifdef CONFIG_VPMU_DEBUG_MSG
        debug_packet_num_cnt++;
        if (ref.type == VPMU_PACKET_DUMP_INFO) {
            CONSOLE_LOG("    %'" PRIu64 " packets received\n", debug_packet_num_cnt);
            debug_packet_num_cnt = 0;
        }
#endif
        int      i;
        int      index = 0;
        d4memref d4_ref;

        // Calculate the index of target cache reference index
        if (ref.type == CACHE_PACKET_INSN)
            index = core_num_table[ref.processor] + // the offset of processor
                    num_cores[ref.processor] +      // the offset of i-cache
                    ref.core;                       // the offset of core
        else
            index = core_num_table[ref.processor] + // the offset of processor
                    0 +                             // the offset of d-cache
                    ref.core;                       // the offset of core
        d4_ref.address    = ref.addr;
        d4_ref.accesstype = (uint8_t)ref.type;
        d4_ref.size       = ref.size;

        // DBG("ENQ: CORE=%d TYPE=%d ADDR=%x SIZE=%d\n",
        //    ref.core,
        //    d4_ref.accesstype,
        //    d4_ref.address,
        //    d4_ref.size);
        // DBG("ENQ: index=%d (%x)\n", index, d4_cache_leaf[index]);
        // Every simulators should handle VPMU_BARRIER_PACKET to support synchronization
        // The implementation depends on your own packet type and writing style
        switch (ref.type) {
        case VPMU_PACKET_BARRIER:
        case VPMU_PACKET_SYNC_DATA:
            // Sync only ensure the data in the array is up to date to and not ahead of
            // the time packet processor receive the sync packet.
            // Slave can stealthily do simulations as long as it does not have a pending
            // sync job.
            sync_cache_data(cache_data, cache_model);
#ifdef EXPERIMENTAL_PER_CORE_CYCLES
            // TODO After solving the sync in data packets, this should be
            // merged into sync_cache_data function.
            for (int core = 0; core < platform_info.cpu.cores; core++) {
                cycles[PROCESSOR_CPU][core] = 0;
                for (int l = 0; l < VPMU_Cache::MEMORY; l++) {
                    cycles[PROCESSOR_CPU][core] +=
                      (icache_miss_counter[core][l] * cache_model.latency[l]
                       + (icache_access_counter[core][l] - icache_miss_counter[core][l])
                           * cache_model.latency[l]
                       + dcache_miss_counter[core][l] * cache_model.latency[l]
                       + (dcache_access_counter[core][l] - dcache_miss_counter[core][l])
                           * cache_model.latency[l]);
                }
                cycles[PROCESSOR_CPU][core] +=
                  memory_access_counter[core] * cache_model.latency[VPMU_Cache::MEMORY];
            }
            memcpy(cache_data.cycles, cycles, sizeof(cycles));
#endif
            return cache_data;
            break;
        case VPMU_PACKET_DUMP_INFO:
            CONSOLE_LOG("  [%d] type : dinero\n", id);
            vpmu::output::Cache_counters(cache_model, cache_data);

            break;
        case VPMU_PACKET_RESET:
            memset(&cache_data, 0, sizeof(VPMU_Cache::Data));
#ifdef EXPERIMENTAL_PER_CORE_CYCLES
            memset(&cache_data_snapshot, 0, sizeof(VPMU_Cache::Data));
            memset(&cycles, 0, sizeof(cycles));
            memset(&icache_miss_counter, 0, sizeof(icache_miss_counter));
            memset(&dcache_miss_counter, 0, sizeof(dcache_miss_counter));
            memset(&icache_access_counter, 0, sizeof(icache_access_counter));
            memset(&dcache_access_counter, 0, sizeof(dcache_access_counter));
            memset(&memory_access_counter, 0, sizeof(memory_access_counter));
            last_core_id = -1;
            last_proc    = -1;
#endif
            for (i = 0; i < MAX_D4_CACHES; i++)
                if (d4_cache[i].cache) reset_single_d4_cache(d4_cache[i].cache);
            break;
        case CACHE_PACKET_READ:
        case CACHE_PACKET_WRITE:
        case CACHE_PACKET_INSN:
            // DBG("index=%d, %x\n", index, d4_cache_leaf[index]);
            // Ignore all packets if this configuration does not support (GPU/DSP/etc.)
            if (unlikely(num_cores[ref.processor] == 0)) return cache_data;
#ifdef EXPERIMENTAL_PER_CORE_CYCLES
            // Initialize the first core id and proc id if it's not set
            if (unlikely(last_proc == -1 || last_core_id == -1)) {
                last_core_id = ref.core;
                last_proc    = ref.processor;
            }
            if (last_proc != ref.processor || last_core_id != ref.core) {
                // This is for minimizing the number of times accumulating the misses
                // of each core. It's very painful...
                // Only accumulate when the processor or core id changes
                // TODO This sync here will make the async callback having a wrong value
                // (It's future feature)
                sync_cache_data(cache_data, cache_model);
                // Summarize the last transaction
                update_snapshot(cache_data, last_proc, last_core_id);
                // Take a snapshot to record the baseline of this transaction
                take_snapshot(cache_data, ref.processor, ref.core);
            }
            last_core_id = ref.core;
            last_proc    = ref.processor;
#endif
            // Error check before sending to the simulator for safety
            if (likely(d4_cache_leaf[index])) d4ref(d4_cache_leaf[index], d4_ref);
            break;
        default:
            ERR_MSG("Unexpected packet in cache simulators\n");
        }

        return cache_data;
    }

    RetStatus hot_packet_processor(int id, const VPMU_Cache::Reference &ref) override
    {
        int      index = 0;
        uint16_t type  = ref.type & 0xF0FF; // Remove states

        // Calculate the index of target cache reference index
        if (type == CACHE_PACKET_INSN)
            index = core_num_table[ref.processor] + // the offset of processor
                    num_cores[ref.processor] +      // the offset of i-cache
                    ref.core;                       // the offset of core
        else
            index = core_num_table[ref.processor] + // the offset of processor
                    0 +                             // the offset of d-cache
                    ref.core;                       // the offset of core

        if (unlikely(num_cores[ref.processor] == 0)) return cache_data;

        int e_block = ((ref.addr + ref.size) - 1)
                      >> cache_model.i_log2_blocksize[VPMU_Cache::L1_CACHE];
        int s_block = ref.addr >> cache_model.i_log2_blocksize[VPMU_Cache::L1_CACHE];
        int num_of_cacheblks = e_block - s_block + 1;
        switch (type) {
        case CACHE_PACKET_INSN:
            d4_cache_leaf[index]->fetch[D4XINSTRN] += num_of_cacheblks;
#ifdef CONFIG_VPMU_DEBUG_MSG
            debug_packet_num_cnt++;
#endif
            break;
        case CACHE_PACKET_READ:
        case CACHE_PACKET_WRITE:
            if (data_possibly_hit(ref.addr, type, cache_model)) {
                if (type == CACHE_PACKET_READ)
                    d4_cache_leaf[index]->fetch[D4XREAD]++;
                else
                    d4_cache_leaf[index]->fetch[D4XWRITE]++;
#ifdef CONFIG_VPMU_DEBUG_MSG
                debug_packet_num_cnt++;
#endif
            } else {
                goto fallback;
            }
            break;
        default:
            goto fallback;
        }
        return cache_data;

    fallback:
        // Pass it to the default packet_processor and remove states.
        return packet_processor(id, packet_bypass(ref));
    }

    bool data_possibly_hit(uint64_t addr, uint32_t rw, VPMU_Cache::Model &model)
    {
        // pc for dcache sim
        static uint64_t block_addr_start[4] = {
          0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
        static uint8_t counter = 0;

        addr &= model.i_log2_blocksize_mask[VPMU_Cache::L1_CACHE];
        if ((block_addr_start[0] == addr) || (block_addr_start[1] == addr)
            || (block_addr_start[2] == addr)
            || (block_addr_start[3] == addr)) { // hot data access
            return true;
        } else { // cold data access
            // classify cases for write-allocation
            if (rw == CACHE_PACKET_READ || model.d_write_alloc[VPMU_Cache::L1_CACHE]) {
                block_addr_start[counter++] =
                  (addr & model.i_log2_blocksize_mask[VPMU_Cache::L1_CACHE]);
                counter &= 3;
            }
            return false;
        }
    }

private:
#ifdef CONFIG_VPMU_DEBUG_MSG
    // The total number of packets counter for debugging
    uint64_t debug_packet_num_cnt = 0;
#endif
    // The CPU configurations for timing model
    using VPMUSimulator::platform_info;
    VPMU_Cache::Model cache_model;
    /// The tempory data storing the data needs by this branch predictor.
    /// In this case, the data equals to the branch data format in Snippits.
    VPMU_Cache::Data cache_data = {};
#ifdef EXPERIMENTAL_PER_CORE_CYCLES
    VPMU_Cache::Data cache_data_snapshot = {};
#endif

#ifdef EXPERIMENTAL_PER_CORE_CYCLES
    // The core-dependent cycles (from private to shared caches)
    uint64_t icache_miss_counter[VPMU_MAX_CPU_CORES][VPMU_Cache::MAX_LEVEL]   = {};
    uint64_t icache_access_counter[VPMU_MAX_CPU_CORES][VPMU_Cache::MAX_LEVEL] = {};
    uint64_t dcache_miss_counter[VPMU_MAX_CPU_CORES][VPMU_Cache::MAX_LEVEL]   = {};
    uint64_t dcache_access_counter[VPMU_MAX_CPU_CORES][VPMU_Cache::MAX_LEVEL] = {};
    uint64_t memory_access_counter[VPMU_MAX_CPU_CORES]                        = {};
    uint64_t cycles[ALL_PROC][VPMU_MAX_CPU_CORES]                             = {};
#endif

    d4cache *       d4_cache_leaf[MAX_D4_CACHES]  = {};
    D4_CACHE_CONFIG d4_cache[MAX_D4_CACHES]       = {{}};
    uint32_t        num_cores[MAX_D4_CACHES]      = {};
    uint32_t        core_num_table[MAX_D4_CACHES] = {};
    uint32_t        d4_num_caches                 = 0;
    uint32_t        d4_levels                     = 0;
};

#endif
