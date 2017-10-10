#ifndef __VPMU_PACKET_HPP_
#define __VPMU_PACKET_HPP_
#pragma once

extern "C" {
#include "config-target.h"  // Target Configuration (CONFIG_ARM)
#include "vpmu-qemu.h"      // VPMUPlatformInfo
#include "vpmu-conf.h"      // VPMU_MAX_CPU_CORES
#include "vpmu-extratb.h"   // Extra TB Information
#include "vpmu-packet.h"    // VPMU Packet Types
}

#include "ringbuffer.hpp" // RingBuffer class

// This class defines the layout of VPMU ring buffer with its common data, etc.
// We only use this as layout mapping, not object instance because the underlying memory
// might be a file or pure memory.
// The overall layout of memory in heap is as follows
//
// Size: sizeof(CPU_Info)  sizeof(T) * N          4           16          T::TraceBuffer
//                                                                        + T::Reference
//                                                                        * buffer_size
// Name:   <CPU_Info>     <Stream Common Data>  <Token>     <Padding>       <Ring Buffer>
// Type:    Struct              Array                        512 bits      T::TraceBuffer
//                                T             uint32_t
//
// Where the template type "T" could be one of the following:
//     VPMU_Insn, VPMU_Branch, VPMU_Cache
// Where the number "N" is the default maximum number of workers: VPMU_MAX_NUM_WORKERS
// Where "buffer_size" is a given number from stream implementation

#pragma pack(push) // push current alignment to stack
#pragma pack(8)    // set alignment to 8 bytes boundary
template <typename T, int SIZE = 0>
class StreamLayout
{
public:
    using Reference = typename T::Reference;

    VPMUPlatformInfo platform_info;                ///< The cpu information
    T                common[VPMU_MAX_NUM_WORKERS]; ///< Configs/states
    uint32_t         token;                        ///< Token variable
    uint64_t         heart_beat;                   ///< Heartbeat signals
    uint64_t         padding[8];                   ///< 8 words of padding
    /// The buffer for sending traces. This must be the last member for correct layout.
    RingBuffer<Reference, SIZE, VPMU_MAX_NUM_WORKERS> trace;
};
#pragma pack(pop) // restore original alignment from stack

class VPMU_Branch
{
public:
// Defining the types (struct) for communication

#pragma pack(push) // push current alignment to stack
#pragma pack(8)    // set alignment to 8 bytes boundary
    // Packet type of a single trace
    typedef struct {
        uint16_t type;         // Packet Type
        uint8_t  num_ex_slots; // Number of reserved ring buffer slots.
        uint8_t  core;         // Number of CPU core
        uint64_t pc;           // PC Address of branch instruction
        uint8_t  taken;        // Is this a taken branch
    } Reference;

    // The data/states of each simulators for VPMU
    class Data
    {
    public:
        uint64_t correct[VPMU_MAX_CPU_CORES]; // branch_predict_correct counter
        uint64_t wrong[VPMU_MAX_CPU_CORES];   // branch_predict_wrong counter
        // uint64_t cycles[VPMU_MAX_CPU_CORES];

        void reduce(void)
        {
            for (int i = 1; i < VPMU.platform.cpu.cores; i++) {
                this->correct[0] += this->correct[i];
                this->wrong[0] += this->wrong[i];
                // this->cycles[0] += this->cycles[i];
                this->correct[i] = 0;
                this->wrong[i]   = 0;
                // this->cycles[i] = 0;
            }
        }

        void mask_out_except(int core_id)
        {
            for (int i = 0; i < VPMU.platform.cpu.cores; i++) {
                if (i != core_id) {
                    this->correct[i] = 0;
                    this->wrong[i]   = 0;
                    // this->cycles[i] = 0;
                }
            }
        }

        Data operator+(const Data &rhs)
        {
            Data out = {}; // Copy elision

            for (int i = 0; i < VPMU.platform.cpu.cores; i++) {
                out.correct[i] = this->correct[i] + rhs.correct[i];
                out.wrong[i]   = this->wrong[i] + rhs.wrong[i];
            }
            return out;
        }

        Data operator-(const Data &rhs)
        {
            Data out = {}; // Copy elision

            for (int i = 0; i < VPMU.platform.cpu.cores; i++) {
                out.correct[i] = this->correct[i] - rhs.correct[i];
                out.wrong[i]   = this->wrong[i] - rhs.wrong[i];
            }
            return out;
        }
    };

    // The architectural configuration information
    // which VPMU needs to know for some functionalities.
    typedef struct {
        char     name[128];
        uint32_t latency;
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
    // cache counter information
    Data data;
    // Synchronization Counter to identify the serial number of synchronized data
    volatile uint32_t sync_counter;
    // Synchronization flag to indicate whether it's done (true/false)
    volatile uint32_t synced_flag;

    // Remain the last 128 bits empty to avoid false sharing due to cache line size
    uint32_t _paddings[4];
};

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
    // cache counter information
    Data data;
    // Synchronization Counter to identify the serial number of synchronized data
    volatile uint32_t sync_counter;
    // Synchronization flag to indicate whether it's done (true/false)
    volatile uint32_t synced_flag;

    // Remain the last 128 bits empty to avoid false sharing due to cache line size
    uint32_t _paddings[4];
};

class VPMU_Insn
{
public:
#pragma pack(push) // push current alignment to stack
#pragma pack(8)    // set alignment to 8 bytes boundary
    // Packet type of a single trace
    typedef struct {
        uint16_t     type;            // Packet Type
        uint8_t      num_ex_slots;    // Number of reserved ring buffer slots.
        uint8_t      core;            // Number of CPU core
        uint8_t      mode;            // CPU mode
        ExtraTBInfo *tb_counters_ptr; // A pointer pointing to TB info
    } Reference;

    /// This is the counters in each mode (user/system).
    class DataCell
    {
    public:
        uint64_t cycles[VPMU_MAX_CPU_CORES];
        uint64_t total_insn[VPMU_MAX_CPU_CORES];
        uint64_t load[VPMU_MAX_CPU_CORES];
        uint64_t store[VPMU_MAX_CPU_CORES];
        uint64_t branch[VPMU_MAX_CPU_CORES];

        void reduce(void)
        {
            for (int i = 1; i < VPMU.platform.cpu.cores; i++) {
                this->cycles[0] += this->cycles[i];
                this->total_insn[0] += this->total_insn[i];
                this->load[0] += this->load[i];
                this->store[0] += this->store[i];
                this->branch[0] += this->branch[i];

                this->cycles[i]     = 0;
                this->total_insn[i] = 0;
                this->load[i]       = 0;
                this->store[i]      = 0;
                this->branch[i]     = 0;
            }
        }

        void mask_out_except(int core_id)
        {
            for (int i = 0; i < VPMU.platform.cpu.cores; i++) {
                if (i != core_id) {
                    this->cycles[i]     = 0;
                    this->total_insn[i] = 0;
                    this->load[i]       = 0;
                    this->store[i]      = 0;
                    this->branch[i]     = 0;
                }
            }
        }

        uint64_t *operator[](std::size_t idx)
        {
            // The number of elements in struct DataCell
            const int cell_length = sizeof(DataCell)     // Total bytes
                                    / VPMU_MAX_CPU_CORES // Length of each member
                                    / sizeof(uint64_t);  // Size of each member
            struct ArrayView {
                uint64_t element[cell_length][VPMU_MAX_CPU_CORES];
            };
            struct ArrayView *l = (struct ArrayView *)this;

            return l->element[idx];
        }

        const uint64_t *operator[](std::size_t idx) const
        {
            // The number of elements in struct DataCell
            const int cell_length = sizeof(DataCell)     // Total bytes
                                    / VPMU_MAX_CPU_CORES // Length of each member
                                    / sizeof(uint64_t);  // Size of each member
            struct ArrayView {
                uint64_t element[cell_length][VPMU_MAX_CPU_CORES];
            };
            struct ArrayView *l = (struct ArrayView *)this;

            return l->element[idx];
        }

        int size(void)
        {
            return sizeof(DataCell)     // Total bytes
                   / VPMU_MAX_CPU_CORES // Length of each member
                   / sizeof(uint64_t);  // Size of each member
        }

        DataCell operator+(const DataCell &rhs)
        {
            DataCell  out = {}; // Copy elision
            DataCell &lhs = *this;

            for (int i = 0; i < out.size(); i++) {
                for (int j = 0; j < VPMU.platform.cpu.cores; j++) {
                    out[i][j] = lhs[i][j] + rhs[i][j];
                }
            }

            return out;
        }

        DataCell operator-(const DataCell &rhs)
        {
            DataCell  out = {}; // Copy elision
            DataCell &lhs = *this;

            for (int i = 0; i < out.size(); i++) {
                for (int j = 0; j < VPMU.platform.cpu.cores; j++) {
                    out[i][j] = lhs[i][j] - rhs[i][j];
                }
            }

            return out;
        }
    };

    typedef struct {
        uint64_t cycles;
        uint64_t total_insn;
        uint64_t load;
        uint64_t store;
        uint64_t branch;
    } DataCell_Summed;

    // The data/states of each simulators for VPMU
    class Data
    {
    public:
        DataCell user, system;

        DataCell sum_all_mode(void)
        {
            DataCell out = this->user + this->system; // Copy elision

            return out;
        }

        DataCell_Summed sum_all(void)
        {
            DataCell        p_result = this->sum_all_mode(); // Copy elision
            DataCell_Summed result   = {};                   // Copy elision

            // The number of elements in struct DataCell
            const int cell_length = sizeof(DataCell_Summed) // Total bytes
                                    / sizeof(uint64_t);     // Size of each member
            struct ArrayView_Summed {
                uint64_t element[cell_length];
            };
            struct ArrayView_Summed *res = (struct ArrayView_Summed *)&result;

            for (int i = 0; i < p_result.size(); i++) {
                for (int j = 0; j < VPMU.platform.cpu.cores; j++) {
                    res->element[i] += p_result[i][j];
                }
            }

            return result;
        }

        void reduce(void)
        {
            this->user.reduce();
            this->system.reduce();
        }

        void mask_out_except(int core_id)
        {
            this->user.mask_out_except(core_id);
            this->system.mask_out_except(core_id);
        }

        Data operator+(const Data &rhs)
        {
            Data out = {}; // Copy elision

            out.user   = this->user + rhs.user;
            out.system = this->system + rhs.system;

            return out;
        }

        Data operator-(const Data &rhs)
        {
            Data out = {}; // Copy elision

            out.user   = this->user - rhs.user;
            out.system = this->system - rhs.system;

            return out;
        }
    };

    // The architectural configuration information
    // which VPMU needs to know for some functionalities.
    typedef struct {
        char     name[128];
        uint64_t frequency;
        uint8_t  dual_issue;
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
    // cache counter information
    Data data;
    // Synchronization Counter to identify the serial number of synchronized data
    volatile uint32_t sync_counter;
    // Synchronization flag to indicate whether it's done (true/false)
    volatile uint32_t synced_flag;

    // Remain the last 128 bits empty to avoid false sharing due to cache line size
    uint32_t _paddings[4];
};

#endif
