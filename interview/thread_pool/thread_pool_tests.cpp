#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <future>
#include <iostream>
#include <thread>
#include <vector>

#include "thread_pool.h"

namespace {

bool check_true(const char* label, const bool value) {
    if (value) {
        return true;
    }

    std::cerr << label << " expected=true actual=false\n";
    return false;
}

bool check_eq(const char* label, const int actual, const int expected) {
    if (actual == expected) {
        return true;
    }

    std::cerr << label << " expected=" << expected << " actual=" << actual << '\n';
    return false;
}

bool test_submit_returns_future_value() {
    interview::ThreadPool pool(2);
    std::future<int> result = pool.submit([](int lhs, int rhs) {
        return lhs + rhs;
    }, 20, 22);

    return check_eq("future.value", result.get(), 42);
}

bool test_runs_many_tasks() {
    interview::ThreadPool pool(4);
    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;
    futures.reserve(128);

    for (int i = 0; i < 128; ++i) {
        futures.push_back(pool.submit([&counter] {
            counter.fetch_add(1, std::memory_order_relaxed);
        }));
    }

    for (std::future<void>& future : futures) {
        future.get();
    }

    return check_eq("many_tasks.counter", counter.load(std::memory_order_relaxed), 128);
}

bool test_shutdown_rejects_new_tasks() {
    interview::ThreadPool pool(1);
    pool.shutdown();

    bool threw = false;
    try {
        (void)pool.submit([] {});
    } catch (const std::runtime_error&) {
        threw = true;
    }

    return check_true("shutdown.rejects_submit", threw);
}

bool test_destructor_drains_queued_tasks() {
    std::atomic<int> counter{0};

    {
        interview::ThreadPool pool(2);
        for (int i = 0; i < 32; ++i) {
            (void)pool.submit([&counter] {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                counter.fetch_add(1, std::memory_order_relaxed);
            });
        }
    }

    return check_eq("destructor.drains", counter.load(std::memory_order_relaxed), 32);
}

}  // namespace

int main() {
    const bool ok =
        test_submit_returns_future_value() &&
        test_runs_many_tasks() &&
        test_shutdown_rejects_new_tasks() &&
        test_destructor_drains_queued_tasks();

    if (!ok) {
        return EXIT_FAILURE;
    }

    std::cout << "thread_pool_tests: all tests passed\n";
    return EXIT_SUCCESS;
}
