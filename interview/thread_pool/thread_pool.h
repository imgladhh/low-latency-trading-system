#pragma once

#include <condition_variable>
#include <cstddef>
#include <future>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace interview {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t thread_count = std::thread::hardware_concurrency()) {
        if (thread_count == 0) {
            throw std::invalid_argument("thread_count must be greater than zero");
        }

        workers_.reserve(thread_count);
        for (std::size_t i = 0; i < thread_count; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    ~ThreadPool() {
        shutdown();
    }

    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>>;

    void shutdown() noexcept {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            accepting_ = false;
        }

        cv_.notify_all();
        for (std::thread& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    [[nodiscard]] std::size_t thread_count() const noexcept {
        return workers_.size();
    }

private:
    void worker_loop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return !accepting_ || !tasks_.empty(); });

                if (!accepting_ && tasks_.empty()) {
                    return;
                }

                task = std::move(tasks_.front());
                tasks_.pop();
            }

            task();
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool accepting_{true};
};

template <typename F, typename... Args>
auto ThreadPool::submit(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>> {
    using Result = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;

    auto task = std::make_shared<std::packaged_task<Result()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    std::future<Result> result = task->get_future();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!accepting_) {
            throw std::runtime_error("cannot submit task after thread pool shutdown");
        }

        tasks_.emplace([task] { (*task)(); });
    }

    cv_.notify_one();
    return result;
}

}  // namespace interview
