#ifndef __VPMU_STREAM_HPP_
#define __VPMU_STREAM_HPP_
#include "vpmu-sim.hpp"         // VPMUSimulator
#include "vpmu-stream-impl.hpp" // VPMUStream_Impl
#include "json.hpp"             // nlohmann::json

class VPMUStream : public VPMULog
{
public:
    VPMUStream() {}
    virtual ~VPMUStream() {}
    VPMUStream(const char* module_name) : VPMULog(module_name) {}
    VPMUStream(std::string module_name) : VPMULog(module_name) {}
    // VPMUStream is neither copyable nor movable.
    VPMUStream(const VPMUStream&) = delete;
    VPMUStream& operator=(const VPMUStream&) = delete;

    virtual void set_default_stream_impl(void)
    {
        log_fatal("set_default_stream_impl is not implemented");
    }
    virtual void bind(nlohmann::json) { log_fatal("bind is not implemented"); }
    virtual void build() { log_fatal("build is not implemented"); }
    virtual void destroy(void) { log_fatal("destroy is not implemented"); }
    virtual void reset(void) { log_fatal("reset is not implemented"); }
    virtual void sync(void) { log_fatal("sync is not implemented"); }
    virtual void sync_none_blocking(void)
    {
        log_fatal("sync_none_blocking is not implemented");
    }
    virtual void dump(void) { log_fatal("dump is not implemented"); }
};

template <typename T>
class VPMUStream_T : public VPMUStream
{
public:
    // Aliasing the real implementation this stream is using
    using Sim_ptr     = std::unique_ptr<VPMUSimulator<T>>;
    using Impl_ptr    = std::unique_ptr<VPMUStream_Impl<T>>;
    using TraceBuffer = typename T::TraceBuffer;
    using Reference   = typename T::Reference;
    using Model       = typename T::Model;
    using Data        = typename T::Data;

    VPMUStream_T() {}
    VPMUStream_T(const char* module_name) : VPMUStream(module_name) {}
    VPMUStream_T(std::string module_name) : VPMUStream(module_name) {}
    virtual ~VPMUStream_T()
    {
        destroy();
        log_debug("Destructed");
    }
    // VPMUStream_T is neither copyable nor movable.
    VPMUStream_T(const VPMUStream_T&) = delete;
    VPMUStream_T& operator=(const VPMUStream_T&) = delete;

    void bind(nlohmann::json configs) override
    {
        // std::cout << configs.dump();
        if (configs.size() < 1) {
            log_fatal("There is no content!");
        }
        target_configs = configs;
    }

    void build() override
    {
        // Destroy all stuff from last build
        destroy();
        log_debug("Initializing");

        // Get the default implementation of stream interface.
        if (impl == nullptr) {
            // Call child default implementation builder
            this->set_default_stream_impl();
        }

        // Locate and create instances of simulator according to the name.
        if (target_configs.is_array()) {
            for (auto sim_config : target_configs) {
                attach_simulator(sim_config);
            }
        } else {
            attach_simulator(target_configs);
        }

        log_debug("attaching %d simulators", jobs.size());
        // Start worker threads/processes with its ring buffer implementation
        impl->run(jobs);

        log_debug("Initialized");
    }

    void destroy(void) override
    {
        // Only release resources here.
        // Do not clear states, ex: target_configs
        impl.reset(nullptr);
        // Call de-allocation of each simulator manually
        jobs.clear(); // Clear arrays and call destructors
        local_buffer_index = 0;
    }

    void reset(void) override
    {
        clean_out_local_buff();
        if (impl != nullptr) impl->send_reset();
    }

    void sync(void) override
    {
        clean_out_local_buff();
        if (impl != nullptr) impl->send_sync();
    }

    void dump(void) override
    {
        clean_out_local_buff();
        if (impl != nullptr) impl->send_dump();
    }

    void sync_none_blocking(void) override
    {
        // log_debug("async");
        clean_out_local_buff();
        if (impl != nullptr) impl->send_sync_none_blocking();
    }

    // Below are non-virtual public functions
    inline void send_ref(Reference& new_ref)
    {
        // Basic safety check
        if (impl == nullptr) return;

        local_buffer[local_buffer_index] = new_ref;

        local_buffer_index++;
        if (unlikely(local_buffer_index == local_buffer_size)) {
            impl->send(local_buffer, local_buffer_index, local_buffer_size);
            local_buffer_index = 0;
        }
    }

    void attach_simulator(nlohmann::json sim_config)
    {
        std::string sim_name = sim_config["name"];

        log("Attaching... " BASH_COLOR_CYAN "%s" BASH_COLOR_NONE, sim_name.c_str());

        auto ptr = create_sim(sim_name);
        if (ptr == nullptr) {
            log(BASH_COLOR_RED "    not found" BASH_COLOR_NONE);
            return;
        }
        ptr->bind(sim_config);
        jobs.push_back(std::move(ptr));
    }

    void set_stream_impl(Impl_ptr&& s) { impl = std::move(s); }

    void set_default_stream_impl(void) override
    {
        log_fatal("set_stream_impl is not implemented");
    }

    // Getter functions for C side to use.
    // Must use the inline hint to ensure the performance.
    inline Model        get_model(void) { return impl->get_model(0); }
    inline Data         get_data(void) { return impl->get_data(0); }
    inline TraceBuffer* get_ringbuffer(void) { return impl->get_trace_buffer(); }
    inline sem_t*       get_semaphore(void) { return impl->get_semaphore(0); }
    inline uint32_t     get_num_workers(void) { return impl->get_num_workers(); }

    // Overloading of get model function
    inline Model get_model(int n) { return impl->get_model(n); }
    // Overloading of get data function
    inline Data get_data(int n) { return impl->get_data(n); }

protected:
    // Force to clean out local buffer whenever the packet is a control packet
    void clean_out_local_buff(void)
    {
        // Basic safety check
        if (impl == nullptr) return;
        // Flush out local buffer when index is not zero
        if (local_buffer_index != 0) {
            impl->send(local_buffer, local_buffer_index, local_buffer_size);
            local_buffer_index = 0;
        }
    }

    // The array of jobs (timing simulators)
    std::vector<Sim_ptr> jobs;
    // Impl
    Impl_ptr impl;

private:
    // Using vector has some performance issue.
    // Statically assigining the size for best performance
    static const uint32_t local_buffer_size = 256;
    Reference             local_buffer[local_buffer_size];
    uint32_t              local_buffer_index = 0;
    // A copy of configuration sent to simulators
    nlohmann::json target_configs;

    virtual Sim_ptr create_sim(std::string sim_name)
    {
        log_fatal("create_sim is not defined");
        return nullptr;
    };
};

#endif
