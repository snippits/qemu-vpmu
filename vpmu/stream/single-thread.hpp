#ifndef __VPMU_STREAM_SINGLE_THREAD_HPP_
#define __VPMU_STREAM_SINGLE_THREAD_HPP_
#pragma once

#include "vpmu-stream-impl.hpp" // VPMUStream_Impl
#include <thread>               // std::thread
#include <memory>               // Smart pointers and mem management

template <typename T>
class VPMUStreamSingleThread : public VPMUStream_Impl<T>
{
private:
    using VPMUStream_Impl<T>::log;
    using VPMUStream_Impl<T>::log_debug;
    using VPMUStream_Impl<T>::log_fatal;

    using VPMUStream_Impl<T>::vpmu_stream;
    using VPMUStream_Impl<T>::num_workers;

public:
    using Reference = typename T::Reference;
    using Sim_ptr   = std::unique_ptr<VPMUSimulator<T>>;
    using Layout    = typename VPMUStream_Impl<T>::Layout;

public:
    VPMUStreamSingleThread(std::string name) : VPMUStream_Impl<T>(name) {}

    ~VPMUStreamSingleThread() { destroy(); }

    void build() override
    {
        if (vpmu_stream != nullptr) delete vpmu_stream;
        vpmu_stream = new Layout();

        // Copy (by value) the CPU information to simulators
        vpmu_stream->platform_info = VPMU.platform;
        // Initialize semaphores to zero, without process-shared flag
        this->reset_semaphore(false);

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

        if (vpmu_stream != nullptr) {
            delete vpmu_stream;
            vpmu_stream = nullptr;
        }
    }

    void run(std::vector<Sim_ptr>& works) override
    {
        num_workers = works.size();
        // Initialize (build) the target simulation with its configuration
        for (int id = 0; id < works.size(); id++) {
            works[id]->id  = id;
            works[id]->pid = vpmu::utils::getpid();
            works[id]->tid = std::this_thread::get_id();
            works[id]->set_platform_info(vpmu_stream->platform_info);
            vpmu_stream->common[id].model = works[id]->build();
        }
        // Register only one listener
        vpmu_stream->trace.register_reader();

        // Create a thread with lambda capturing local variable by reference
        slave = std::thread([&]() {
            vpmu::utils::name_thread(this->get_name() + std::to_string(0));
            // Only be cancelable at cancellation points, ex: sem_wait
            pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

            // Set synced_flag to tell master it's done
            for (int i = 0; i < num_workers; i++)
                vpmu_stream->common[i].synced_flag = true;
            log_debug("worker thread start");
            while (1) {
                this->wait_semaphore(0); // Down semaphore
                // Keep draining traces till it's empty
                while (!vpmu_stream->trace.empty(0)) {
                    auto refs = vpmu_stream->trace.pop(0, 256);
                    for (int id = 0; id < num_workers; id++) {
                        this->do_tasks(works[id], refs);
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

private:
    std::thread slave;
};

#endif
