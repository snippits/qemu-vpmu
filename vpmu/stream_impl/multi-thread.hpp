#ifndef __VPMU_STREAM_MULTI_THREAD_HPP_
#define __VPMU_STREAM_MULTI_THREAD_HPP_
#include "vpmu-stream-impl.hpp" // VPMUStream_Impl
#include <thread>               // std::thread
#include <memory>               // Smart pointers and mem management

template <typename T>
class VPMUStreamMultiThread : public VPMUStream_Impl<T>
{
public:
    using Reference   = typename T::Reference;
    using TraceBuffer = typename T::TraceBuffer;
    using Sim_ptr     = std::unique_ptr<VPMUSimulator<T>>;

public:
    VPMUStreamMultiThread(std::string name) : VPMUStream_Impl<T>(name) {}

    ~VPMUStreamMultiThread()
    {
        destroy();
        log_debug("Destructed");
    }

    void build(int buffer_size) override
    {
        int total_buffer_size = Stream_Layout<T>::total_size(buffer_size);

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
        shm_bufferInit(trace_buffer, // the pointer of trace buffer
                       buffer_size,  // the number of packets (size of buffer)
                       Reference,    // the type of packet
                       TraceBuffer,  // the type of trace buffer
                       Stream_Layout<T>::get_trace_buffer(buffer));

        // Initialize semaphores to zero
        for (int i = 0; i < VPMU_MAX_NUM_WORKERS; i++)
            sem_init(&stream_comm[i].job_semaphore, 0, 0);
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

        if (buffer != nullptr) {
            free(buffer);
            buffer       = nullptr;
            trace_buffer = nullptr;
        }
    }

    void run(std::vector<Sim_ptr> &works) override
    {
        num_workers = works.size();

        for (int id = 0; id < works.size(); id++) {
            // Create a thread with lambda capturing local variable by reference
            slaves.push_back(std::thread(
              [&](int id) {
                  auto &sim = works[id];
                  // Local buffers, the size is not necessary to be the same as sender
                  const uint32_t local_buffer_size = 1024;
                  Reference      local_buffer[local_buffer_size];
                  int            num_refs = 0;

                  // Initialize (build) the target simulation with its configuration
                  sim->set_platform_info(*platform_info);
                  sim->build(stream_comm[id]);
                  // Only be cancelable at cancellation points, ex: sem_wait
                  pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
                  // Set synced_flag to tell master it's done
                  stream_comm[id].synced_flag = true;
                  log_debug("worker thread start");
                  while (1) {
                      this->wait_semaphore(id); // Down semaphore
                      // Keep draining traces till it's empty
                      while (likely(shm_isBufferNotEmpty(trace_buffer, id))) {
                          shm_bulkRead(
                            trace_buffer,      // Pointer to ringbuffer
                            id,                // ID of the worker
                            local_buffer,      // Pointer to local(private) buffer
                            local_buffer_size, //#elements of local(private) buffer
                            sizeof(Reference), // Size of each elements
                            num_refs);         //#elements read successfully

                          // Do simulation
                          for (int i = 0; i < num_refs; i++) {
                              if (unlikely(local_buffer[i].type
                                           == VPMU_PACKET_DUMP_INFO)) {
                                  this->wait_token(id);
                                  sim->packet_processor(
                                    id, local_buffer[i], stream_comm[id]);
                                  this->pass_token(id);
                              } else
                                  sim->packet_processor(
                                    id, local_buffer[i], stream_comm[id]);
                          }
                      }
                  }
              },
              id));
        }

        for (int id = 0; id < slaves.size(); id++) {
            vpmu::utils::name_thread(slaves[id], this->get_name() + std::to_string(id));
        }

        // Wait all forked process to be initialized
        if (this->timed_wait_sync_flag(5000) == false) {
            log_fatal("Some component timing simulators might not be alive!");
        }
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
        if (cnt == 2) {
            Reference barrier;

            barrier.type = VPMU_PACKET_BARRIER;
            send(barrier);
            cnt = 0;
        }

        for (int i = 0; i < num_workers; i++)
            while (shm_remainedBufferSpace(trace_buffer, i) <= total_size) usleep(1);
        shm_bulkWrite(trace_buffer,       // Pointer to ringbuffer
                      local_buffer,       // Pointer to local(private) buffer
                      num_refs,           // Number of elements to write
                      sizeof(Reference)); // Size of each elements
        this->post_semaphore();           // up semaphores
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

        for (int i = 0; i < num_workers; i++)
            while (shm_remainedBufferSpace(trace_buffer, i) <= 1) usleep(1);
        shm_bufferWrite(trace_buffer, // Pointer to ringbuffer
                        ref);         // The reference elements
        this->post_semaphore();       // up semaphores
    }

private:
    using VPMUStream_Impl<T>::log;
    using VPMUStream_Impl<T>::log_debug;
    using VPMUStream_Impl<T>::log_fatal;

    using VPMUStream_Impl<T>::platform_info;
    using VPMUStream_Impl<T>::buffer;
    using VPMUStream_Impl<T>::stream_comm;
    using VPMUStream_Impl<T>::trace_buffer;
    using VPMUStream_Impl<T>::token;
    using VPMUStream_Impl<T>::num_workers;

    std::vector<std::thread> slaves;

    // The total number of packets counter for debugging
    uint64_t debug_packet_num_cnt = 0;
};

#endif
