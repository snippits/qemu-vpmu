#ifndef __VPMU_PACKET_HPP__
#define __VPMU_PACKET_HPP__

extern "C" {
#include "config-target.h"  // Target Configuration (CONFIG_ARM)
#include "vpmu-conf.h"      // VPMU_MAX_CPU_CORES
#include "vpmu-extratb.h"   // Extra TB Information
#include "vpmu-packet.h"    // VPMU Packet Types
#include "shm-ringbuffer.h" // Lightening Ring Buffer Implementation
}

// This static class defines the layout of VPMU ring buffer with its common data, etc.
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
template <typename T>
class Stream_Layout
{
public:
    using TraceBuffer = typename T::TraceBuffer;

    static inline uint32_t total_size(uint64_t buffer_size)
    {
        return sizeof(Layout)                                 // Size of common data
               + sizeof(typename T::Reference) * buffer_size; // Data transfer stream
    };

    // Buffer size must be given to calaulate the total size
    static inline uint32_t total_size() = delete; // Not allowed !!!!!!!!

    static inline VPMUPlatformInfo *get_platform_info(void *buff)
    {
        return &(           // Cast to pointer
          ((Layout *)buff)  // Cast to Layout structure
            ->platform_info // Find the offset
          );
    }

    static inline T *get_stream_comm(void *buff)
    {
        return (           // Cast to pointer (because it's array, do nothing)
          ((Layout *)buff) // Cast to Layout structure
            ->stream_comm  // Find the offset
          );
    }

    static inline uint32_t *get_token(void *buff)
    {
        return &(          // Cast to pointer
          ((Layout *)buff) // Cast to Layout structure
            ->token        // Find the offset
          );
    }

    static inline uint64_t *get_heart_beat(void *buff)
    {
        return &(          // Cast to pointer
          ((Layout *)buff) // Cast to Layout structure
            ->heart_beat   // Find the offset
          );
    }

    static inline TraceBuffer *get_trace_buffer(void *buff)
    {
        return &(          // Cast to pointer
          ((Layout *)buff) // Cast to Layout structure
            ->trace_buffer // Find the offset
          );
    }

private:
#pragma pack(push) // push current alignment to stack
#pragma pack(8)    // set alignment to 8 bytes boundary
    // This is just a dummy type for strict layout alignment of memory.
    typedef struct {
        VPMUPlatformInfo platform_info;                     // The cpu information
        T                stream_comm[VPMU_MAX_NUM_WORKERS]; // Configs/states
        uint32_t         token;                             // Token variable
        uint64_t         heart_beat;                        // Heartbeat signals
        uint64_t         padding[8];                        // 8 words of padding
        TraceBuffer      trace_buffer;                      // The trace buffer
    } Layout;
#pragma pack(pop) // restore original alignment from stack
};

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
    typedef struct {
        uint64_t correct[VPMU_MAX_CPU_CORES]; // branch_predict_correct counter
        uint64_t wrong[VPMU_MAX_CPU_CORES];   // branch_predict_wrong counter
        // uint64_t cycles[VPMU_MAX_CPU_CORES];
    } Data;

    // The architectural configuration information
    // which VPMU needs to know for some functionalities.
    typedef struct {
        char     name[128];
        uint32_t latency;
    } Model;
#pragma pack(pop) // restore original alignment from stack

    // Define the buffer type here because we need to use it in normal C program
    shm_ringBuffer_typedef(Reference, TraceBuffer, VPMU_MAX_NUM_WORKERS);

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
    typedef struct {
        //[level][core][r/w miss/hit]
        uint64_t insn_cache[ALL_PROC][MEMORY][VPMU_MAX_CPU_CORES][SIZE_OF_INDEX];
        uint64_t data_cache[ALL_PROC][MEMORY][VPMU_MAX_CPU_CORES][SIZE_OF_INDEX];
        // uint64_t cycles[ALL_PROC][VPMU_MAX_CPU_CORES];
        uint64_t memory_accesses, memory_time_ns;
    } Data;

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

    // Define the buffer type here because we need to use it in normal C program
    shm_ringBuffer_typedef(Reference, TraceBuffer, VPMU_MAX_NUM_WORKERS);

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

    typedef struct Insn_Data_Cell {
        uint64_t total_insn;
        uint64_t load;
        uint64_t store;
        uint64_t branch;
    } Insn_Data_Cell;

    // The data/states of each simulators for VPMU
    typedef struct {
        // TODO This should be core independent
        Insn_Data_Cell user, system, interrupt, system_call, rest, fpu, co_processor;

        uint64_t cycles[VPMU_MAX_CPU_CORES];   // Total cycles
        uint64_t insn_cnt[VPMU_MAX_CPU_CORES]; // Total instruction count
    } Data;

    // The architectural configuration information
    // which VPMU needs to know for some functionalities.
    typedef struct {
        char     name[128];
        uint64_t frequency;
        uint8_t  dual_issue;
    } Model;
#pragma pack(pop) // restore original alignment from stack

    // Define the buffer type here because we need to use it in normal C program
    shm_ringBuffer_typedef(Reference, TraceBuffer, VPMU_MAX_NUM_WORKERS);

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
