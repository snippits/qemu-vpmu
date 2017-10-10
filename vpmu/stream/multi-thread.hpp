#ifndef __VPMU_STREAM_MULTI_THREAD_HPP_
#define __VPMU_STREAM_MULTI_THREAD_HPP_
#pragma once

#include "vpmu-stream-impl.hpp" // VPMUStream_Impl
#include <thread>               // std::thread
#include <memory>               // Smart pointers and mem management

template <typename T>
class VPMUStreamMultiThread : public VPMUStream_Impl<T>
{
public:
    using Reference = typename T::Reference;
    using Sim_ptr   = std::unique_ptr<VPMUSimulator<T>>;
    using Layout    = typename VPMUStream_Impl<T>::Layout;

public:
    VPMUStreamMultiThread(std::string name, uint64_t num_elems) : VPMUStream_Impl<T>(name)
    {
    }

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
        for (auto &s : slaves) {
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

    void run(std::vector<Sim_ptr> &works) override
    {
        num_workers = works.size();

        for (int id = 0; id < works.size(); id++) {
            vpmu_stream->trace.register_reader();
            // Create a thread with lambda capturing local variable by reference
            slaves.push_back(std::thread(
              [&](int id) {
                  auto &sim = works[id];
                  // Local buffers, the size is not necessary to be the same as sender
                  const uint32_t local_buffer_size = 1024;
                  Reference      local_buffer[local_buffer_size];
                  int            num_refs = 0;

                  vpmu::utils::name_thread(this->get_name() + std::to_string(id));
                  // Initialize (build) the target simulation with its configuration
                  sim->set_platform_info(vpmu_stream->platform_info);
                  sim->build(vpmu_stream->common[id].model);
                  // Only be cancelable at cancellation points, ex: sem_wait
                  pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
                  // Set synced_flag to tell master it's done
                  vpmu_stream->common[id].synced_flag = true;
                  log_debug("worker thread start");
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
                                        id,
                                        local_buffer[i],
                                        vpmu_stream->common[id].data);
                                  } else {
                                      sim->packet_processor(id,
                                                            local_buffer[i],
                                                            vpmu_stream->common[id].data);
                                  }
                              }
                          }
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

    std::vector<std::thread> slaves;

    // The total number of packets counter for debugging
    uint64_t debug_packet_num_cnt = 0;
};

#endif
