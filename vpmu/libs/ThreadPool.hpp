#ifndef __THREAD_POOL_HPP_
#define __THREAD_POOL_HPP_
#pragma once

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>
#include <type_traits>

class ThreadPool
{
public:
    ThreadPool(const ThreadPool& rhs) = delete;            // Non-copyable.
    ThreadPool& operator=(const ThreadPool& rhs) = delete; // Non-assignable.

    // the constructor just launches some amount of workers
    ThreadPool(const char* new_name, size_t threads) : stop(false)
    {
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this, new_name, i] {
                char thread_name[LINUX_NAMELEN] = {0};
                snprintf(thread_name, LINUX_NAMELEN, "%s-%zu", new_name, i);
                pthread_setname_np(pthread_self(), thread_name);

                for (;;) {
                    std::function<void()> task;

                    {
                        // This is now thread independent lock
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(
                          lock, [this] { return this->stop || !this->tasks.empty(); });
                        if (this->stop && this->tasks.empty()) return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }

                    task();
                }
            });
        }
    }

    ThreadPool(size_t threads) : stop(false)
    {
        ThreadPool("threadpool", threads);
    }

    ~ThreadPool()
    {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread& worker : workers) worker.join();
    }

    // add new work item to the pool
    template <class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
    {
        auto boundTask   = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        using ResultType = std::result_of_t<decltype(boundTask)()>;

        // These two are for un-used local typedef
        ResultType t;
        (void)t;

        // Help to suppress compiler errors when args does not match args in lambda
        return enqueue_helper(boundTask, f, std::forward<Args>(args)...);
    }

    void enqueue_static(std::function<void()> task)
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        // don't allow enqueueing after stopping the pool
        if (stop) throw std::runtime_error("enqueue on stopped ThreadPool");
        tasks.emplace([task]() { task(); });
        condition.notify_one();
    }

    void enqueue_static(std::function<void(void*)> task, void* ptr)
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        // don't allow enqueueing after stopping the pool
        if (stop) throw std::runtime_error("enqueue on stopped ThreadPool");
        tasks.emplace([task, ptr]() { task(ptr); });
        condition.notify_one();
    }

    void enqueue_static(std::function<void(void*, void*)> task, void* env, void* ptr)
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        // don't allow enqueueing after stopping the pool
        if (stop) throw std::runtime_error("enqueue on stopped ThreadPool");
        tasks.emplace([task, env, ptr]() { task(env, ptr); });
        condition.notify_one();
    }

private:
    // Help to suppress compiler errors when args does not match args in lambda
    template <class B, class F, class... Args>
    inline auto enqueue_helper(B&& boundTask, F&& f, Args&&... args)
    {
        using ResultType = std::result_of_t<decltype(boundTask)()>;
        auto task        = std::make_shared<std::packaged_task<ResultType()>>(boundTask);

        std::future<ResultType> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);

            // don't allow enqueueing after stopping the pool
            if (stop) throw std::runtime_error("enqueue on stopped ThreadPool");

            tasks.emplace([task]() { (*task)(); });
        }
        condition.notify_one();
        return res;
    }

private:
    // need to keep track of threads so we can join them
    std::vector<std::thread> workers;
    // the task queue
    std::queue<std::function<void()>> tasks;

    // synchronization
    std::mutex              queue_mutex;
    std::condition_variable condition;
    std::atomic_bool        stop;
};

#endif
