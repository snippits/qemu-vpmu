#ifndef __VPMU_STREAM_MULTI_PROCESS_HPP_
#define __VPMU_STREAM_MULTI_PROCESS_HPP_
#pragma once

extern "C" {
#include <sys/types.h> // Types of kernel related (pid_t, etc.)
#include <signal.h>    // kill()
}
#include <thread>               // std::thread
#include <memory>               // Smart pointers and mem management
#include "vpmu-stream-impl.hpp" // VPMUStream_Impl
// Boost Library for inter-process communications
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>

template <typename T>
class VPMUStreamMultiProcess : public VPMUStream_Impl<T>
{
public:
    using Reference = typename T::Reference;
    using Sim_ptr   = std::unique_ptr<VPMUSimulator<T>>;
    using Layout    = typename VPMUStream_Impl<T>::Layout;

public:
    VPMUStreamMultiProcess(std::string name, uint64_t num_elems)
        : VPMUStream_Impl<T>(name)
    {
    }

    ~VPMUStreamMultiProcess() { destroy(); }

    void build() override
    {
        using namespace boost::interprocess;

        // TODO Need a hased name for safely initializing (forking) simulators when
        // concurrent VM execution is enabled. If hased named is implemented,
        // the deletion of shared memory needs to be guaranteed when crushing.
        // Erases objects from the system. Returns false on error. Never throws
        shared_memory_object::remove("vpmu_cache_ring_buffer");
        shm = shared_memory_object(create_only, "vpmu_cache_ring_buffer", read_write);

        // Set size
        shm.truncate(sizeof(Layout));

        // Map the whole shared memory in this process
        region = mapped_region(shm, read_write);
        log_debug("Mapped address %p, size %d.", region.get_address(), region.get_size());

        // Write all the memory to 0
        std::memset(region.get_address(), 0, region.get_size());
        // Initialize with constructor
        vpmu_stream = new (region.get_address()) Layout();

        // Copy (by value) the CPU information to simulators
        vpmu_stream->platform_info = VPMU.platform;
        // Initialize semaphores to zero, and set to process-shared!!
        this->reset_semaphore(true);

        log_debug("Common resource allocated");
    }

    void destroy(void) override
    {
        // De-allocating the thread resources appropriately, required by C++ standard
        if (heart_beat_thread.native_handle() != 0) {
            pthread_cancel(heart_beat_thread.native_handle());
            // Standard thread library require this for correct destructor behavior
            heart_beat_thread.join();
        }

        // De-allocating resources must be the opposite order of resource allocation
        for (auto &s : slaves) {
            kill(s, SIGKILL);
        }
        slaves.clear(); // Clear vector data, and call destructor automatically

        // Erases objects from the system. Returns false on error. Never throws
        boost::interprocess::shared_memory_object::remove("vpmu_cache_ring_buffer");
        if (vpmu_stream != nullptr) {
            // delete vpmu_stream;
            vpmu_stream = nullptr;
        }
    }

    void run(std::vector<Sim_ptr> &works) override
    {
        num_workers = works.size();
        // Initialize the data vector for synchronizing counters from worker threads

        for (int id = 0; id < works.size(); id++) {
            pid_t pid = fork();

            if (pid) {
                // Parent
                slaves.push_back(pid);
                vpmu_stream->trace.register_reader();
            } else {
                // This "move" improves the performance a little bit.
                // It doesn't affect the host process. :D
                auto sim = std::move(works[id]);
                // auto &sim = works[id];
                // Local buffers, the size is not necessary to be the same as sender
                const uint32_t local_buffer_size = 1024;
                Reference      local_buffer[local_buffer_size];
                int            num_refs = 0;

                vpmu::utils::name_process(this->get_name() + std::to_string(id));
                // Initialize (build) the target simulation with its configuration
                sim->set_platform_info(vpmu_stream->platform_info);
                sim->build(vpmu_stream->common[id].model);
                // Initialize mutex to one, and set to process-shared
                sem_init(&vpmu_stream->common[id].job_semaphore, true, 0);
                // Set synced_flag to tell master it's done
                vpmu_stream->common[id].synced_flag = true;
                log_debug("worker process %d start", id);
                while (1) {
                    this->wait_semaphore(id); // Down semaphore
                    // Keep draining traces till it's empty
                    while (!vpmu_stream->trace.empty(id)) {
                        num_refs =
                          vpmu_stream->trace.pop(id, local_buffer, local_buffer_size);

                        // Do simulation
                        for (int i = 0; i < num_refs; i++) {
                            switch (local_buffer[i].type) {
                            case VPMU_PACKET_SYNC_DATA:
                                // Wait for the last signal to be cleared
                                while (vpmu_stream->common[id].synced_flag)
                                    ;
                                vpmu_stream->common[id].sync_counter++;
                                sim->packet_processor(
                                  id, local_buffer[i], vpmu_stream->common[id].data);
                                // Set synced_flag to tell master it's done
                                vpmu_stream->common[id].synced_flag = true;
                                break;
                            case VPMU_PACKET_DUMP_INFO:
                                this->wait_token(id);
                                sim->packet_processor(
                                  id, local_buffer[i], vpmu_stream->common[id].data);
                                this->pass_token(id);
                                break;
                            default:
                                if (local_buffer[i].type & VPMU_PACKET_HOT) {
                                    sim->hot_packet_processor(
                                      id, local_buffer[i], vpmu_stream->common[id].data);
                                } else {
                                    sim->packet_processor(
                                      id, local_buffer[i], vpmu_stream->common[id].data);
                                }
                            }
                        }
                    }
                }
                // It should never return!!!
                abort();
            }
        }
        fork_zombie_killer();

        // Wait all forked process to be initialized
        if (this->timed_wait_sync_flag(5000) == false) {
            LOG_FATAL("Some component timing simulators might not be alive!");
            // some simulators are not responding
            // You know, I'm not always going to be around to help you - Charlie Brown
            ERR_MSG(
              "some forked simulators or remote process are not responding. \n"
              "\tThis might because the some zombie cache simulator exists.\n"
              "\tOr custom cache simulator were not executed after qemu's execution\n"
              "\tTry \"killall qemu-system-arm\" to solve zombie processes.\n");
            exit(EXIT_FAILURE);
        }
        this->reset_sync_flags();
    }

    void send(Reference *local_buffer, uint32_t num_refs, uint32_t total_size) override
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
        vpmu_stream->trace.push(local_buffer, num_refs);
        this->post_semaphore(); // up semaphores
    }

    void send(Reference &ref) override
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

private:
    using VPMUStream_Impl<T>::log;
    using VPMUStream_Impl<T>::log_debug;
    using VPMUStream_Impl<T>::log_fatal;

    using VPMUStream_Impl<T>::vpmu_stream;
    using VPMUStream_Impl<T>::num_workers;

    boost::interprocess::shared_memory_object shm;
    boost::interprocess::mapped_region        region;

    std::vector<pid_t> slaves;
    std::thread        heart_beat_thread;

    // The total number of packets counter for debugging
    uint64_t debug_packet_num_cnt = 0;

    void fork_zombie_killer()
    {
        pid_t parent_pid = getpid();
        pid_t pid        = fork();

        if (pid) {
            // Parent
            slaves.push_back(pid);
            heart_beat_thread = std::thread([=] {
                vpmu::utils::name_thread("heart beat");
                while (true) {
                    // Only be cancelable at cancellation points
                    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
                    usleep(100 * 1000); // 0.1 second
                    vpmu_stream->heart_beat += 1;
                }
            });
        } else {
            volatile uint64_t last_heart_beat     = 0;
            bool              print_gdb_once_flag = false;

            vpmu::utils::name_process("zombie-killer");
            while (true) {
                usleep(500 * 1000); // 0.5 second
                if (vpmu_stream->heart_beat == last_heart_beat) {
                    if (kill(parent_pid, 0)) {
                        if (errno == ESRCH) {
                            LOG_FATAL("QEMU stops beating... kill all zombies!!\n");
                            this->destroy();
                            log("Destructed\n");
                            // Exit without calling unnecessary destructor of global
                            // variables.
                            // This prevents double-free shared resources.
                            abort();
                        }
                    } else {
                        if (print_gdb_once_flag == false)
                            log_debug("QEMU stops beating... but still exist. "
                                      "Consider it as stopped by ptrace (gdb).\n");
                        print_gdb_once_flag = true;
                    }
                }
                last_heart_beat = vpmu_stream->heart_beat;
            }
            // It should never return!!!
            abort();
        }
    }
};

#endif
