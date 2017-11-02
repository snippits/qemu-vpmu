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
    using Layout    = StreamLayout<T, 1024 * 64>;
    using Model     = typename T::Model;
    using Reference = typename T::Reference;
    using Data      = typename T::Data;
    using Sim_ptr   = std::unique_ptr<VPMUSimulator<T>>;
    // Define ring buffer with lightening
    VPMUStream_Impl() : VPMULog("StreamImpl") {}
    VPMUStream_Impl(std::string name) : VPMULog(name) {}
    virtual ~VPMUStream_Impl() { log_debug("Destructed"); }

public:
    //
    // VPMU stream protocol interface
    //

    // This is for initializing common resources for workers
    virtual void build(void) { LOG_FATAL_NOT_IMPL(); }
    virtual void destroy(void) { LOG_FATAL_NOT_IMPL(); }
    // Initialize resources for individual workers and execute them in parallel.
    virtual void run(std::vector<Sim_ptr>& jobs) { LOG_FATAL_NOT_IMPL(); }

    inline void send(Reference* refs, uint32_t num_refs, uint32_t total_size)
    {
        // Basic safety check
        if (vpmu_stream == nullptr) return;

#ifdef CONFIG_VPMU_DEBUG_MSG
        debug_packet_num_cnt += num_refs;
#endif

        // Periodically sync back counters for timing
        static uint32_t cnt = 0;
        cnt++;
        if (cnt == 4) {
            Reference barrier;

            barrier.type = VPMU_PACKET_BARRIER;
            send(barrier);
            cnt = 0;
        }

        while (vpmu_stream->trace.remained_space() <= total_size) usleep(1);
        vpmu_stream->trace.push(refs, num_refs);
        this->post_semaphore(); // up semaphores
    }

    inline void send(Reference& ref)
    {
        // Basic safety check
        if (vpmu_stream == nullptr) return;

#ifdef CONFIG_VPMU_DEBUG_MSG
        debug_packet_num_cnt++;
        if (ref.type == VPMU_PACKET_DUMP_INFO) {
            CONSOLE_LOG("VPMU sent %'" PRIu64 " packets\n", debug_packet_num_cnt);
            debug_packet_num_cnt = 0;
        }
#endif

        while (vpmu_stream->trace.remained_space() <= 1) usleep(1);
        vpmu_stream->trace.push(ref);
        this->post_semaphore(); // up semaphores
    }

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
        ref.type = VPMU_PACKET_SYNC_DATA;
        send(ref);
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

        // log_debug("sync none blocking");
        send(ref);
    }

    inline void post_semaphore(void)
    {
        for (int i = 0; i < num_workers; i++) post_semaphore(i);
    }

    inline void post_semaphore(int n)
    {
        if (vpmu_stream == nullptr) return;
        sem_post(&vpmu_stream->common[n].job_semaphore);
    }

    inline void wait_semaphore(void)
    {
        for (int i = 0; i < num_workers; i++) wait_semaphore(i);
    }

    inline void wait_semaphore(int n) { sem_wait(&vpmu_stream->common[n].job_semaphore); }

    inline void reset_semaphore(bool process_shared)
    {
        for (int i = 0; i < VPMU_MAX_NUM_WORKERS; i++) reset_semaphore(i, process_shared);
    }

    inline void reset_semaphore(int n, bool process_shared)
    {
        if (vpmu_stream == nullptr) return;
        sem_init(&vpmu_stream->common[n].job_semaphore, process_shared, 0);
    }

    // Get the results from a timing simulator
    inline Data get_data(int n)
    {
        if (pointer_safety_check(n) == false) return {};
        return vpmu_stream->common[n].data;
    }
    // Get model configuration back from timing a simulator
    Model get_model(int n)
    {
        if (pointer_safety_check(n) == false) return {};
        return vpmu_stream->common[n].model;
    }

    // Get number of workers
    uint32_t get_num_workers(void) { return num_workers; }

    bool initialized(void) { return this->vpmu_stream != nullptr; }

    void reset_sync_flags(void)
    {
        if (vpmu_stream == nullptr) return;

        for (int i = 0; i < num_workers; i++) {
            vpmu_stream->common[i].synced_flag = false;
        }
    }

    bool timed_wait_sync_flag(uint64_t mili_sec)
    {
        for (int i = 0; i < mili_sec * 1000; i++) {
            int flag_cnt = 0;
            // Wait all forked process to be initialized
            for (int i = 0; i < num_workers; i++) {
                if (vpmu_stream->common[i].synced_flag) flag_cnt++;
            }
            if (flag_cnt == num_workers) {
                // All synced
                return true;
            } else {
                // Some of them are not synced yet
                usleep(1);
            }
        }
        return false;
    }

protected:
    Layout* vpmu_stream = nullptr;
    // Record how many workers in process
    uint32_t num_workers = 0;

    inline void do_tasks(Sim_ptr& sim, std::vector<Reference>& refs)
    {
        int id = sim->id;

        for (auto& ref : refs) {
            switch (ref.type) {
            case VPMU_PACKET_SYNC_DATA:
                // Wait for the last signal to be cleared
                while (vpmu_stream->common[id].synced_flag)
                    ;
                vpmu_stream->common[id].sync_counter++;
                sim->packet_processor(id, ref, vpmu_stream->common[id].data);
                // Set synced_flag to tell master it's done
                vpmu_stream->common[id].synced_flag = true;
                break;
            case VPMU_PACKET_DUMP_INFO:
                this->wait_token(id);
                sim->packet_processor(id, ref, vpmu_stream->common[id].data);
                this->pass_token(id);
                break;
            default:
                if (ref.type & VPMU_PACKET_HOT) {
                    sim->hot_packet_processor(id, ref, vpmu_stream->common[id].data);
                } else {
                    sim->packet_processor(id, ref, vpmu_stream->common[id].data);
                }
            }
        }
    }

private:
    // The total number of packets counter for debugging
    uint64_t debug_packet_num_cnt = 0;

    inline void reset_token() { vpmu_stream->token = 0; };
    inline void pass_token(uint32_t id) { vpmu_stream->token = id + 1; };
    inline void wait_token(uint32_t id)
    {
        while (vpmu_stream->token != id) std::this_thread::yield();
    }

    bool pointer_safety_check(int n)
    {
        if (n > num_workers) {
            LOG_FATAL("request index %d is grater than total number of workers %d",
                      n,
                      num_workers);
            return false;
        }
        if (vpmu_stream == nullptr) {
            LOG_FATAL("vpmu_stream is nullptr\n");
            return false;
        }
        return true;
    }
};

#endif
