#ifndef __CACHE_DINERO_HPP__
#define __CACHE_DINERO_HPP__
#include <string>          // std::string
#include "vpmu-sim.hpp"    // VPMUSimulator
#include "vpmu-packet.hpp" // VPMU_Cache::Reference
#include "vpmu-utils.hpp"  // miscellaneous functions

extern "C" {
#include "d4-7/d4.h"
}

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

    void dump_info(int id)
    {
        int i;

        CONSOLE_LOG("  [%d] type : dinero\n", id);
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
        IF_KEY_IS("blocksize", c->lg2blocksize = vpmu::utils::clog2(atoi(val)));
        IF_KEY_IS("subblocksize", c->lg2subblocksize = vpmu::utils::clog2(atoi(val)));
        IF_KEY_IS("size", c->lg2size = vpmu::utils::clog2(atoi(val)));
        IF_KEY_IS("assoc", c->assoc = atoi(val));
        IF_KEY_IS("split_3c_cnt", c->flags |= atoi(val) ? D4F_CCC : 0);
        c->flags |= extra_flag;

        IF_KEY_IS("prefetch_abortpercent", c->prefetch_abortpercent = atoi(val));
        IF_KEY_IS("prefetch_distance", c->prefetch_distance = atoi(val));

        IF_KEY_IS("replacement", c->name_replacement                 = strdup(val);
                  if (strcmp(val, LRU) == 0) c->replacementf         = d4rep_lru;
                  else if (strcmp(val, FIFO) == 0) c->replacementf   = d4rep_fifo;
                  else if (strcmp(val, RANDOM) == 0) c->replacementf = d4rep_random;
                  else ERR_MSG("JSON: not a valid option\n %s: %s\n", key, val););

        IF_KEY_IS(
          "prefetch", c->name_prefetch                          = strdup(val);
          if (strcmp(val, DEMAND_ONLY) == 0) c->prefetchf       = d4prefetch_none;
          else if (strcmp(val, ALWAYS) == 0) c->prefetchf       = d4prefetch_always;
          else if (strcmp(val, MISS) == 0) c->prefetchf         = d4prefetch_miss;
          else if (strcmp(val, TAGGED) == 0) c->prefetchf       = d4prefetch_tagged;
          else if (strcmp(val, LOAD_FORWARD) == 0) c->prefetchf = d4prefetch_loadforw;
          else if (strcmp(val, SUB_BLOCK) == 0) c->prefetchf    = d4prefetch_subblock;
          else ERR_MSG("JSON: not a valid option\n %s: %s\n", key, val););

        IF_KEY_IS("walloc", c->name_walloc                        = strdup(val);
                  if (strcmp(val, IMPOSSIBLE) == 0) c->wallocf    = d4walloc_impossible;
                  else if (strcmp(val, ALWAYS) == 0) c->wallocf   = d4walloc_always;
                  else if (strcmp(val, NEVER) == 0) c->wallocf    = d4walloc_never;
                  else if (strcmp(val, NO_FETCH) == 0) c->wallocf = d4walloc_nofetch;
                  else ERR_MSG("JSON: not a valid option\n %s: %s\n", key, val););

        IF_KEY_IS("wback", c->name_wback                         = strdup(val);
                  if (strcmp(val, IMPOSSIBLE) == 0) c->wbackf    = d4wback_impossible;
                  else if (strcmp(val, ALWAYS) == 0) c->wbackf   = d4wback_always;
                  else if (strcmp(val, NEVER) == 0) c->wbackf    = d4wback_never;
                  else if (strcmp(val, NO_FETCH) == 0) c->wbackf = d4wback_nofetch;
                  else ERR_MSG("JSON: not a valid option\n %s: %s\n", key, val););

        ERR_MSG("JSON: option not found\n %s: %s\n", key, val);
#undef IF_KEY_IS
    }

    d4cache *parse_and_set(json &root, D4_CACHE_CONFIG *parent, int extra_flag, int level)
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

    // BFS search to form an one dimensional array
    void recursively_parse_json(json &root, D4_CACHE_CONFIG *c, int level)
    {
        int i, index, local_index = 0;

        if (level == VPMU_Cache::NOT_USED) return; // There's no level 0 cache
        if (root.is_array()) {
            local_index = d4_num_caches;
            for (int i = 0; i < root.size(); i++) {
                parse_and_set(root[i], c, 0x00, level);
            }
            for (int i = 0; i < root.size(); i++) {
                // If there's next level of cache topology, dive into it!
                if (root[i]["next"] != nullptr) {
                    recursively_parse_json(
                      root[i]["next"], &d4_cache[local_index], level - 1);
                }
            }
        } else {
            index                     = get_processor_index(root["d-cache"]);
            int leaf_index            = core_num_table[index];
            flag_has_processor[index] = 1;

            index = get_processor_index(root["d-cache"]);
            for (i = 0; i < num_cores[index]; i++) {
                d4_cache_leaf[leaf_index] =
                  parse_and_set(root["d-cache"], c, 0x00, level);
                leaf_index++;
            }
            index = get_processor_index(root["i-cache"]);
            for (i = 0; i < num_cores[index]; i++) {
                d4_cache_leaf[leaf_index] =
                  parse_and_set(root["i-cache"], c, D4F_RO, level);
                leaf_index++;
            }
        }
    }

    void sync_cache_data(VPMU_Cache::Data &data)
    {
        int         i, level, processor;
        Demand_Data d;

        for (processor = 0; processor < ALL_PROC; processor++) {
            if (num_cores[processor] == 0) continue;
            // Loop through from L1 to max level of current cache configuration
            for (level = VPMU_Cache::L1_CACHE; level <= d4_levels; level++) {
                // Loop through all the processor cores
                for (i = 0; i < num_cores[processor]; i++) {
                    int index;

                    index = core_num_table[processor] + // the offset of processor
                            num_cores[processor] +      // the offset of i-cache
                            i;                          // the offset of core
                    d        = calculate_data(d4_cache_leaf[index]);
                    auto &ti = data.inst_cache[processor][level][i];
                    // Sync back values
                    ti[VPMU_Cache::READ]       = d.fetch_alltype;
                    ti[VPMU_Cache::WRITE]      = 0;
                    ti[VPMU_Cache::READ_MISS]  = d.data_alltype;
                    ti[VPMU_Cache::WRITE_MISS] = 0;

                    index = core_num_table[processor] + // the offset of processor
                            0 +                         // the offset of d-cache
                            i;                          // the offset of core
                    d        = calculate_data(d4_cache_leaf[index]);
                    auto &td = data.data_cache[processor][level][i];
                    // Sync back values
                    td[VPMU_Cache::READ]       = d.fetch_read;
                    td[VPMU_Cache::WRITE]      = d4_cache_leaf[index]->fetch[D4XWRITE];
                    td[VPMU_Cache::READ_MISS]  = d.data_read;
                    td[VPMU_Cache::WRITE_MISS] = d4_cache_leaf[index]->miss[D4XWRITE];
                }
            }
        }
        d = calculate_data(d4_cache[0].cache);

        data.memory_accesses = d.fetch_read;
    }

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
                      vpmu::utils::clog2(get_json<int>(c_elem, "blocksize"));
                    model.d_log2_blocksize_mask[level] =
                      ~((1 << model.d_log2_blocksize[level]) - 1);
                    model.d_write_alloc[level] = (c_elem["walloc"] == "ALWAYS");
                    model.d_write_back[level]  = (c_elem["wback"] == "ALWAYS");

                    c_elem = n_elem;
                    n_elem = n_elem["next"];
                    level--;
                }

                model.d_log2_blocksize[level] =
                  vpmu::utils::clog2(get_json<int>(c_elem["d-cache"], "blocksize"));
                model.d_log2_blocksize_mask[level] =
                  ~((1 << model.d_log2_blocksize[level]) - 1);
                model.d_write_alloc[level] = (c_elem["d-cache"]["walloc"] == "ALWAYS");
                model.d_write_back[level]  = (c_elem["d-cache"]["wback"] == "ALWAYS");

                model.i_log2_blocksize[level] =
                  vpmu::utils::clog2(get_json<int>(c_elem["i-cache"], "blocksize"));
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
    Cache_Dinero() : VPMUSimulator("Dinero") { log_debug("Constructed"); }
    ~Cache_Dinero() { log_debug("Destructed"); }

    void build(VPMU_Cache &cache) override
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
        d4_num_caches++;
        recursively_parse_json(json_config["topology"], &d4_cache[0], d4_levels);
        sync_back_config_to_vpmu(cache.model, json_config);

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
    }

    inline void
    packet_processor(int id, VPMU_Cache::Reference &ref, VPMU_Cache &cache) override
    {

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
        if (ref.type == CACHE_PACKET_INSTRN)
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
            sync_cache_data(cache.data);
            break;
        case VPMU_PACKET_SYNC_DATA:
            // Sync only ensure the data in the array is up to date to and not ahead of
            // the time packet processor receive the sync packet.
            // Slave can stealthily do simulations as long as it does not have a pending
            // sync job.
            sync_cache_data(cache.data);
            // pkg->sync_counter++;  // Increase the timestamp of counter
            // pkg->synced_flag = 1; // Set up the flag
            break;
        case VPMU_PACKET_DUMP_INFO:
            dump_info(id);
            break;
        case VPMU_PACKET_RESET:
            for (i = 0; i < MAX_D4_CACHES; i++)
                if (d4_cache[i].cache) reset_single_d4_cache(d4_cache[i].cache);
            break;
        case CACHE_PACKET_READ:
        case CACHE_PACKET_WRITE:
        case CACHE_PACKET_INSTRN:
            // DBG("index=%d, %x\n", index, d4_cache_leaf[index]);
            // Ignore all packets if this configuration does not support
            if (unlikely(num_cores[ref.processor] == 0)) return;
            // Error check
            if (likely(d4_cache_leaf[index])) d4ref(d4_cache_leaf[index], d4_ref);
            break;
        default:
            ERR_MSG("Unexpected packet in cache simulators\n");
        }
    }

    inline void
    hot_packet_processor(int id, VPMU_Cache::Reference &ref, VPMU_Cache &cache) override
    {
        int      index = 0;
        uint16_t type  = ref.type & 0xF0FF; // Remove states

        // Calculate the index of target cache reference index
        if (type == CACHE_PACKET_INSTRN)
            index = core_num_table[ref.processor] + // the offset of processor
                    num_cores[ref.processor] +      // the offset of i-cache
                    ref.core;                       // the offset of core
        else
            index = core_num_table[ref.processor] + // the offset of processor
                    0 +                             // the offset of d-cache
                    ref.core;                       // the offset of core

        if (unlikely(num_cores[ref.processor] == 0)) return;

        int e_block = ((ref.addr + ref.size) - 1)
                      >> cache.model.i_log2_blocksize[VPMU_Cache::L1_CACHE];
        int s_block = ref.addr >> cache.model.i_log2_blocksize[VPMU_Cache::L1_CACHE];
        int num_of_cacheblks = e_block - s_block + 1;
        switch (type) {
        case CACHE_PACKET_INSTRN:
            d4_cache_leaf[index]->fetch[D4XINSTRN] += num_of_cacheblks;
#ifdef CONFIG_VPMU_DEBUG_MSG
            debug_packet_num_cnt++;
#endif
            break;
        case CACHE_PACKET_READ:
        case CACHE_PACKET_WRITE:
            if (data_possibly_hit(ref.addr, type, cache.model)) {
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
        return;

    fallback:
        // Remove states
        VPMU_Cache::Reference p_ref = ref;
        p_ref.type                  = p_ref.type & 0xF0FF;

        packet_processor(id, p_ref, cache);
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

    d4cache *       d4_cache_leaf[MAX_D4_CACHES]  = {0};
    D4_CACHE_CONFIG d4_cache[MAX_D4_CACHES]       = {{0}};
    uint32_t        num_cores[MAX_D4_CACHES]      = {0};
    uint32_t        core_num_table[MAX_D4_CACHES] = {0};
    uint32_t        d4_num_caches                 = 0;
    uint32_t        d4_levels                     = 0;
    int             flag_has_processor[ALL_PROC]  = {0};
};

#endif
