#ifndef __VPMU_STREAM_SINGLE_THREAD_HPP_
#define __VPMU_STREAM_SINGLE_THREAD_HPP_
#pragma once

#include "vpmu-stream-impl.hpp" // VPMUStream_Impl
#include <thread>               // std::thread
#include <memory>               // Smart pointers and mem management

template <typename T>
class VPMUStreamSingleThread : public VPMUStream_Impl<T>
{
public:
    using Reference   = typename T::Reference;
    using TraceBuffer = typename T::TraceBuffer;
    using Sim_ptr     = std::unique_ptr<VPMUSimulator<T>>;

public:
    VPMUStreamSingleThread(std::string name, uint64_t num_elems)
        : VPMUStream_Impl<T>(name)
    {
        num_trace_buffer_elems = num_elems;
    }

    ~VPMUStreamSingleThread() { destroy(); }

    void build() override
    {
        int total_buffer_size = Stream_Layout<T>::total_size(num_trace_buffer_elems);

        if (buffer != nullptr) {
            free(buffer);
        }
        buffer = (uint8_t *)malloc(total_buffer_size);

        // Copy (by value) the CPU information to ring buffer
        platform_info  = Stream_Layout<T>::get_platform_info(buffer);
        *platform_info = VPMU.platform;
        // Assign pointer of common data
        stream_comm = Stream_Layout<T>::get_stream_comm(buffer);
        // Assign the pointer of token
        token = Stream_Layout<T>::get_token(buffer);
        // Assign pointer of trace buffer
        shm_bufferInit(trace_buffer,           // the pointer of trace buffer
                       num_trace_buffer_elems, // the number of packets
                       Reference,              // the type of packet
                       TraceBuffer,            // the type of trace buffer
                       Stream_Layout<T>::get_trace_buffer(buffer));

        // Initialize semaphores to zero
        for (int i = 0; i < VPMU_MAX_NUM_WORKERS; i++)
            sem_init(&stream_comm[i].job_semaphore, 0, 0);
        log_debug("Common resource allocated");
    }

    void destroy(void) override
    {
        // De-allocating resources must be the opposite order of resource allocation
        if (slave.native_handle() != 0) {
            pthread_cancel(slave.native_handle());
            // Standard thread library require this for correct destructor behavior
            slave.join();
        }

        if (buffer != nullptr) {
            free(buffer);
            buffer       = nullptr;
            trace_buffer = nullptr;
        }
    }

    void run(std::vector<Sim_ptr> &works) override
    {
        num_workers = works.size();
        // Initialize (build) the target simulation with its configuration
        for (int id = 0; id < works.size(); id++) {
            works[id]->set_platform_info(*platform_info);
            works[id]->build(stream_comm[id].model);
        }

        // Create a thread with lambda capturing local variable by reference
        slave = std::thread([&]() {
            // Local buffers, the size is not necessary to be the same as sender
            const uint32_t local_buffer_size = 1024;
            Reference      local_buffer[local_buffer_size];
            uint32_t       num_refs = 0;

            vpmu::utils::name_thread(this->get_name() + std::to_string(0));
            // Only be cancelable at cancellation points, ex: sem_wait
            pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
            // Set synced_flag to tell master it's done
            for (int i = 0; i < num_workers; i++) stream_comm[i].synced_flag = true;
            log_debug("worker thread start");
            while (1) {
                this->wait_semaphore(0); // Down semaphore
                // Keep draining traces till it's empty
                while (likely(shm_isBufferNotEmpty(trace_buffer, 0))) {
                    shm_bulkRead(trace_buffer,      // Pointer to ringbuffer
                                 0,                 // ID of the worker
                                 local_buffer,      // Pointer to local(private) buffer
                                 local_buffer_size, //#elements of local(private) buffer
                                 sizeof(Reference), // Size of each elements
                                 num_refs);         //#elements read successfully

                    // Update all buffer indices first
                    for (auto &s : trace_buffer->start) s = trace_buffer->start[0];
                    // Do simulation
                    for (int id = 0; id < works.size(); id++) {
                        auto &sim = works[id]; // Just an alias of naming
                        for (int i = 0; i < num_refs; i++) {
                            switch (local_buffer[i].type) {
                            case VPMU_PACKET_SYNC_DATA:
                                // Wait for the last signal to be cleared
                                while (stream_comm[id].synced_flag)
                                    ;
                                stream_comm[id].sync_counter++;
                                sim->packet_processor(
                                  id, local_buffer[i], stream_comm[id].data);
                                // Set synced_flag to tell master it's done
                                stream_comm[id].synced_flag = true;
                                break;
                            case VPMU_PACKET_DUMP_INFO:
                                this->wait_token(id);
                                sim->packet_processor(
                                  id, local_buffer[i], stream_comm[id].data);
                                this->pass_token(id);
                                break;
                            default:
                                if (local_buffer[i].type & VPMU_PACKET_HOT) {
                                    sim->hot_packet_processor(
                                      id, local_buffer[i], stream_comm[id].data);
                                } else {
                                    sim->packet_processor(
                                      id, local_buffer[i], stream_comm[id].data);
                                }
                            }
                        }
                    }
                }
            }
        });

        // Wait all forked process to be initialized
        if (this->timed_wait_sync_flag(5000) == false) {
            LOG_FATAL("Some component timing simulators might not be alive!");
        }
        this->reset_sync_flags();
    }

    void send(Reference *local_buffer, uint32_t num_refs, uint32_t total_size) override
    {
        // Basic safety check
        if (trace_buffer == nullptr) return;

#ifdef CONFIG_VPMU_DEBUG_MSG
        debug_packet_num_cnt += num_refs;
#endif
        // JIT Model Selection needs periodically sync back counters
        static uint32_t cnt = 0;
        cnt++;
        if (cnt == 4) {
            Reference barrier;

            barrier.type = VPMU_PACKET_BARRIER;
            send(barrier);
            cnt = 0;
        }

        while (shm_remainedBufferSpace(trace_buffer, 0) <= total_size) usleep(1);
        shm_bulkWrite(trace_buffer,       // Pointer to ringbuffer
                      local_buffer,       // Pointer to local(private) buffer
                      num_refs,           // Number of elements to write
                      sizeof(Reference)); // Size of each elements
        this->post_semaphore(0);          // up semaphore
    }

    void send(Reference &ref) override
    {
        // Basic safety check
        if (trace_buffer == nullptr) return;

#ifdef CONFIG_VPMU_DEBUG_MSG
        debug_packet_num_cnt++;
        if (ref.type == VPMU_PACKET_DUMP_INFO) {
            CONSOLE_LOG("VPMU sent %'" PRIu64 " packets\n", debug_packet_num_cnt);
            debug_packet_num_cnt = 0;
        }
#endif

        while (shm_remainedBufferSpace(trace_buffer, 0) <= 1) usleep(1);
        shm_bufferWrite(trace_buffer, // Pointer to ringbuffer
                        ref);         // The reference elements
        this->post_semaphore(0);      // up semaphore
    }

private:
    using VPMUStream_Impl<T>::log;
    using VPMUStream_Impl<T>::log_debug;
    using VPMUStream_Impl<T>::log_fatal;

    using VPMUStream_Impl<T>::platform_info;
    using VPMUStream_Impl<T>::buffer;
    using VPMUStream_Impl<T>::stream_comm;
    using VPMUStream_Impl<T>::trace_buffer;
    using VPMUStream_Impl<T>::num_trace_buffer_elems;
    using VPMUStream_Impl<T>::token;
    using VPMUStream_Impl<T>::num_workers;

    std::thread slave;

    // The total number of packets counter for debugging
    uint64_t debug_packet_num_cnt = 0;
};

#endif
