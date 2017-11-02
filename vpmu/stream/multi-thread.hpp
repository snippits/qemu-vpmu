#ifndef __VPMU_STREAM_MULTI_THREAD_HPP_
#define __VPMU_STREAM_MULTI_THREAD_HPP_
#pragma once

#include "vpmu-stream-impl.hpp" // VPMUStream_Impl
#include <thread>               // std::thread
#include <memory>               // Smart pointers and mem management

template <typename T>
class VPMUStreamMultiThread : public VPMUStream_Impl<T>
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
    VPMUStreamMultiThread(std::string name) : VPMUStream_Impl<T>(name) {}

    ~VPMUStreamMultiThread() { destroy(); }

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
        for (auto& s : slaves) {
            if (s.native_handle() != 0) {
                pthread_cancel(s.native_handle());
            }
            // Standard thread library require this for correct destructor behavior
            s.join();
        }
        slaves.clear(); // Clear vector data, and call destructor automatically

        if (vpmu_stream != nullptr) {
            delete vpmu_stream;
            vpmu_stream = nullptr;
        }
    }

    void run(std::vector<Sim_ptr>& works) override
    {
        num_workers = works.size();

        for (int id = 0; id < works.size(); id++) {
            vpmu_stream->trace.register_reader();
            // Create a thread with lambda capturing local variable by reference
            slaves.push_back(std::thread(
              [&](int id) {
                  vpmu::utils::name_thread(this->get_name() + std::to_string(id));
                  // Only be cancelable at cancellation points, ex: sem_wait
                  pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

                  auto& sim = works[id];

                  sim->id  = id;
                  sim->pid = vpmu::utils::getpid();
                  sim->tid = std::this_thread::get_id();
                  // Initialize (build) the target simulation with its configuration
                  sim->set_platform_info(vpmu_stream->platform_info);
                  vpmu_stream->common[id].model = sim->build();
                  // Set synced_flag to tell master it's done
                  vpmu_stream->common[id].synced_flag = true;
                  log_debug("worker thread %d start", id);
                  while (1) {
                      this->wait_semaphore(id); // Down semaphore
                      // Keep draining traces till it's empty
                      while (!vpmu_stream->trace.empty(id)) {
                          auto refs = vpmu_stream->trace.pop(id, 256);
                          this->do_tasks(sim, refs);
                      }
                  }
              },
              id));
        }

        // Wait all forked process to be initialized
        if (this->timed_wait_sync_flag(5000) == false) {
            LOG_FATAL("Some component timing simulators might not be alive!");
        }
        this->reset_sync_flags();
    }

private:
    std::vector<std::thread> slaves;
};

#endif
