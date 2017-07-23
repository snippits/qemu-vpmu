#ifndef __VPMU_STREAM_IMPL_HPP_
#define __VPMU_STREAM_IMPL_HPP_
#pragma once

extern "C" {
#include "vpmu-qemu.h" // VPMUPlatformInfo
}
#include "vpmu-sim.hpp"   // VPMUSimulator
#include "vpmu-log.hpp"   // VPMULog
#include "vpmu-utils.hpp" // miscellaneous functions
#include <signal.h>       // Signaling header
#include <semaphore.h>    // Semaphore related header

template <typename T>
class VPMUStream_Impl : public VPMULog
{
public:
    using Model       = typename T::Model;
    using Reference   = typename T::Reference;
    using Data        = typename T::Data;
    using TraceBuffer = typename T::TraceBuffer;
    using Sim_ptr     = std::unique_ptr<VPMUSimulator<T>>;
    // Define ring buffer with lightening
    VPMUStream_Impl() : VPMULog("StreamImpl") {}
    VPMUStream_Impl(std::string name) : VPMULog(name) {}
    virtual ~VPMUStream_Impl() { log_debug("Destructed"); }

protected:
    // The real buffer, use byte address mode
    uint8_t* buffer = nullptr;
    // The pointer to common data of stream
    T* stream_comm;
    // The pointer to the token var for serialization
    volatile uint32_t* token;
    // The pointer to the heart beat var for zombie killer
    volatile uint64_t* heart_beat;
    // Trace buffer
    TraceBuffer* trace_buffer = nullptr;
    // Number of elements in the trace buffer
    uint64_t num_trace_buffer_elems = 0;
    // Record how many workers in process
    uint32_t          num_workers   = 0;
    VPMUPlatformInfo* platform_info = nullptr;

public:
    //
    // VPMU stream protocol interface
    //

    // This is for initializing common resources for workers
    virtual void build() { LOG_FATAL("build is not implemented"); }
    // Initialize resources for individual workers and execute them in parallel.
    virtual void run(std::vector<Sim_ptr>& jobs) { LOG_FATAL("run is not implemented"); }
    virtual void destroy(void) { LOG_FATAL("destroy is not implemented"); }
    virtual void send(Reference* local_buffer, uint32_t num_refs, uint32_t total_size)
    {
        LOG_FATAL("send is not implemented");
    }

    virtual void send(Reference& ref) { LOG_FATAL("send is not implemented"); }

    //
    // VPMU stream protocol implementation
    //

    inline void send_reset(void)
    {
        Reference ref;
        ref.type = VPMU_PACKET_RESET;
        send(ref);
    }

    inline void send_sync(void)
    {
        Reference ref;
        ref.type = VPMU_PACKET_BARRIER;

        // log_debug("sync");
        // Barrier packet also synchronize data back to Cache_Data structure.
        // Push the barrier packet into the queue to
        // ensure everything before the barrier packet is done.
        send(ref);
        shm_waitBufferEmpty(trace_buffer, num_workers);
        send(ref);
        // Wait till it's done "twice" to ensure the property of barrier
        // Note this must be done twice due to the bulk read!
        // Otherwise, you might miss less than 1k/2k references!!!!
        shm_waitBufferEmpty(trace_buffer, num_workers);
    }

    inline void send_dump(void)
    {
        Reference ref;
        ref.type = VPMU_PACKET_DUMP_INFO;

        reset_token();
        send(ref);
        // Block till it's done (token value would be the size of workers)
        wait_token(num_workers);
    }

    inline void send_sync_none_blocking(void)
    {
        Reference ref;
        ref.type = VPMU_PACKET_BARRIER;

        // log_debug("async");
        send(ref);
    }

    inline void post_semaphore(void)
    {
        for (int i = 0; i < num_workers; i++) sem_post(&stream_comm[i].job_semaphore);
    }
    inline void post_semaphore(int n) { sem_post(&stream_comm[n].job_semaphore); }

    inline void wait_semaphore(void)
    {
        for (int i = 0; i < num_workers; i++) sem_wait(&stream_comm[i].job_semaphore);
    }

    inline void wait_semaphore(int n) { sem_wait(&stream_comm[n].job_semaphore); }

    // Get the results from a timing simulator
    inline Data get_data(int n)
    {
        if (pointer_safety_check(n) == false) return {}; // Zero initializer
        return stream_comm[n].data;
    }
    // Get model configuration back from timing a simulator
    Model get_model(int n)
    {
        if (pointer_safety_check(n) == false) return {}; // Zero initializer
        return stream_comm[n].model;
    }

    // Getter functions for C side
    // Get trace buffer
    TraceBuffer* get_trace_buffer(void) { return trace_buffer; }
    // Get number of workers
    uint32_t get_num_workers(void) { return num_workers; }
    // Get semaphore
    sem_t* get_semaphore(int n)
    {
        if (pointer_safety_check(n) == false) return nullptr;
        return &stream_comm[n].job_semaphore;
    }

    bool timed_wait_sync_flag(uint64_t mili_sec)
    {
        for (int i = 0; i < 1000; i++) {
            int flag_cnt = 0;
            // Wait all forked process to be initialized
            for (int i = 0; i < num_workers; i++) {
                if (stream_comm[i].synced_flag) flag_cnt++;
            }
            if (flag_cnt == num_workers) {
                // All synced
                return true;
            } else {
                // Some of them are not synced yet
                usleep(1000);
            }
        }
        return false;
    }

protected:
    inline void pass_token(uint32_t id) { *token = id + 1; };
    inline void wait_token(uint32_t id)
    {
        while (*token != id) std::this_thread::yield();
    }

private:
    inline void reset_token() { *token = 0; };
    bool pointer_safety_check(int n)
    {
        if (n > num_workers) {
            LOG_FATAL("request index %d is grater than total number of workers %d",
                      n,
                      num_workers);
            return false;
        }
        if (stream_comm == nullptr) {
            LOG_FATAL("stream_comm is nullptr\n");
            return false;
        }
        return true;
    }
};

#endif
