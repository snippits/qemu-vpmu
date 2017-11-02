#ifndef __VPMU_CACHE_PACKET_HPP_
#define __VPMU_CACHE_PACKET_HPP_
#pragma once

extern "C" {
#include "vpmu-conf.h" // VPMU_MAX_CPU_CORES
}

class VPMU_Cache
{
public:
    enum Data_Index { READ, WRITE, READ_MISS, WRITE_MISS, SIZE_OF_INDEX };
    enum Data_Level { NOT_USED, L1_CACHE, L2_CACHE, L3_CACHE, MEMORY, MAX_LEVEL };

#pragma pack(push) // push current alignment to stack
#pragma pack(8)    // set alignment to 8 bytes boundary
    // Packet type of a single trace
    typedef struct {
        uint16_t type;         // Packet Type
        uint8_t  num_ex_slots; // Number of reserved ring buffer slots.
        uint8_t  core;         // Number of CPU core
        uint8_t  processor;    // CPU=0 / GPU=1
        uint64_t addr;         // R/W Address
        uint16_t size;         // Size of this transaction
    } Reference;

    // The data/states of each simulators for VPMU
    class Data
    {
    public:
        //[level][core][r/w miss/hit]
        uint64_t insn_cache[ALL_PROC][MEMORY][VPMU_MAX_CPU_CORES][SIZE_OF_INDEX];
        uint64_t data_cache[ALL_PROC][MEMORY][VPMU_MAX_CPU_CORES][SIZE_OF_INDEX];
        // uint64_t cycles[ALL_PROC][VPMU_MAX_CPU_CORES];
        uint64_t memory_accesses, memory_time_ns;

        void reduce(void)
        {
            for (int c = 0; c < ALL_PROC; c++) {
                // Skip if that processing core does not exist
                if (c == PROCESSOR_GPU && VPMU.platform.gpu.cores == 0) continue;
                // TODO Use variable to set the shared level of cache
                int m = L1_CACHE;
                for (int i = 1; i < VPMU.platform.cpu.cores; i++) {
                    for (int j = 0; j < SIZE_OF_INDEX; j++) {
                        this->insn_cache[c][m][0][j] += this->insn_cache[c][m][i][j];
                        this->data_cache[c][m][0][j] += this->data_cache[c][m][i][j];
                        this->insn_cache[c][m][i][j] = 0;
                        this->data_cache[c][m][i][j] = 0;
                    }
                    // this->cycles[0] += this->cycles[i];
                    // this->cycles[i] = 0;
                }
            }
        }

        void mask_out_except(int core_id)
        {
            for (int c = 0; c < ALL_PROC; c++) {
                // Skip if that processing core does not exist
                if (c == PROCESSOR_GPU && VPMU.platform.gpu.cores == 0) continue;
                // TODO Use variable to set the shared level of cache
                int m = L1_CACHE;
                for (int i = 0; i < VPMU.platform.cpu.cores; i++) {
                    if (i != core_id) {
                        for (int j = 0; j < SIZE_OF_INDEX; j++) {
                            this->insn_cache[c][m][i][j] = 0;
                            this->data_cache[c][m][i][j] = 0;
                        }
                        // this->cycles[i] = 0;
                    }
                }
            }
        }

        Data operator+(const Data &rhs)
        {
            Data out = {}; // Copy elision

            for (int c = 0; c < ALL_PROC; c++) {
                // Skip if that processing core does not exist
                if (c == PROCESSOR_GPU && VPMU.platform.gpu.cores == 0) continue;
                for (int m = L1_CACHE; m < MAX_LEVEL; m++) {
                    for (int i = 0; i < VPMU.platform.cpu.cores; i++) {
                        for (int j = 0; j < SIZE_OF_INDEX; j++) {
                            out.insn_cache[c][m][i][j] =
                              this->insn_cache[c][m][i][j] + rhs.insn_cache[c][m][i][j];
                            out.data_cache[c][m][i][j] =
                              this->data_cache[c][m][i][j] + rhs.data_cache[c][m][i][j];
                        }
                    }
                }
            }
            out.memory_accesses = this->memory_accesses + rhs.memory_accesses;
            out.memory_time_ns  = this->memory_time_ns + rhs.memory_time_ns;

            return out;
        }

        Data operator-(const Data &rhs)
        {
            Data out = {}; // Copy elision

            for (int c = 0; c < ALL_PROC; c++) {
                // Skip if that processing core does not exist
                if (c == PROCESSOR_GPU && VPMU.platform.gpu.cores == 0) continue;
                for (int m = L1_CACHE; m < MAX_LEVEL; m++) {
                    for (int i = 0; i < VPMU.platform.cpu.cores; i++) {
                        for (int j = 0; j < SIZE_OF_INDEX; j++) {
                            out.insn_cache[c][m][i][j] =
                              this->insn_cache[c][m][i][j] - rhs.insn_cache[c][m][i][j];
                            out.data_cache[c][m][i][j] =
                              this->data_cache[c][m][i][j] - rhs.data_cache[c][m][i][j];
                        }
                    }
                }
            }
            out.memory_accesses = this->memory_accesses - rhs.memory_accesses;
            out.memory_time_ns  = this->memory_time_ns - rhs.memory_time_ns;

            return out;
        }
    };

    // The architectural configuration information
    // which VPMU needs to know for some functionalities.
    typedef struct VPMU_Cache_Model {
        char name[64];
        // number of layers this cache configuration has
        int levels;
        // cache latency information
        int latency[MAX_LEVEL];
        // cache block size information
        int d_log2_blocksize[MAX_LEVEL];
        int d_log2_blocksize_mask[MAX_LEVEL];
        // cache block size information
        int i_log2_blocksize[MAX_LEVEL];
        int i_log2_blocksize_mask[MAX_LEVEL];
        // data cache: true -> write allocate; false -> non write allocate
        int d_write_alloc[MAX_LEVEL];
        // data cache: true -> write back; false -> write through
        int d_write_back[MAX_LEVEL];
    } Model;

#pragma pack(pop) // restore original alignment from stack

public:
    // Defining the instances for communication between VPMU and workers.

    // A number representing the ID of current worker (timing simulator)
    uint32_t id;
    // semaphore for signaling worker thread
    sem_t job_semaphore;
    // Timing simulator model information that VPMU required for some functions
    Model model;
    // Synchronization Counter to identify the serial number of synchronized data
    volatile uint32_t sync_counter;
    // Synchronization flag to indicate whether it's done (true/false)
    volatile uint32_t synced_flag;

    // Remain the last 128 bits empty to avoid false sharing due to cache line size
    uint32_t _paddings[4];
};

#endif
