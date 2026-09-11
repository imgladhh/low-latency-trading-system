#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>

#include "spsc_ring_buffer.h"

namespace {

bool check_true(const char* label, const bool value) {
    if (value) {
        return true;
    }

    std::cerr << label << " expected=true actual=false\n";
    return false;
}

bool check_false(const char* label, const bool value) {
    if (!value) {
        return true;
    }

    std::cerr << label << " expected=false actual=true\n";
    return false;
}

bool check_eq(const char* label, const int actual, const int expected) {
    if (actual == expected) {
        return true;
    }

    std::cerr << label << " expected=" << expected << " actual=" << actual << '\n';
    return false;
}

bool test_fifo_behavior() {
    llt::SpscRingBuffer<int> queue(4);
    int value = 0;

    return check_true("fifo.push1", queue.try_push(1)) &&
        check_true("fifo.push2", queue.try_push(2)) &&
        check_true("fifo.pop1", queue.try_pop(value)) &&
        check_eq("fifo.value1", value, 1) &&
        check_true("fifo.pop2", queue.try_pop(value)) &&
        check_eq("fifo.value2", value, 2) &&
        check_false("fifo.pop_empty", queue.try_pop(value));
}

bool test_full_and_reuse() {
    llt::SpscRingBuffer<int> queue(2);
    int value = 0;

    return check_true("full.push1", queue.try_push(10)) &&
        check_true("full.push2", queue.try_push(20)) &&
        check_false("full.push3", queue.try_push(30)) &&
        check_true("full.pop1", queue.try_pop(value)) &&
        check_eq("full.value1", value, 10) &&
        check_true("full.push_after_pop", queue.try_push(30)) &&
        check_true("full.pop2", queue.try_pop(value)) &&
        check_eq("full.value2", value, 20) &&
        check_true("full.pop3", queue.try_pop(value)) &&
        check_eq("full.value3", value, 30);
}

bool run_concurrent_stress_iteration(
    const int iteration,
    const std::chrono::steady_clock::time_point deadline) {
    constexpr std::size_t queue_capacity = 8;
    constexpr std::uint64_t sequence_count = 500'000;

    llt::SpscRingBuffer<std::uint64_t> queue(queue_capacity);
    std::atomic<bool> saturation_observed{false};
    std::atomic<bool> timed_out{false};
    std::atomic<bool> sequence_error{false};
    std::atomic<std::uint64_t> produced{0};
    std::atomic<std::uint64_t> consumed{0};
    std::atomic<std::uint64_t> full_retries{0};
    std::atomic<std::uint64_t> expected_at_error{0};
    std::atomic<std::uint64_t> actual_at_error{0};

    const auto deadline_expired = [&]() {
        if (std::chrono::steady_clock::now() < deadline) {
            return false;
        }
        timed_out.store(true, std::memory_order_release);
        return true;
    };

    std::thread producer([&]() {
        for (std::uint64_t sequence = 1; sequence <= sequence_count; ++sequence) {
            while (!queue.try_push(sequence)) {
                full_retries.fetch_add(1, std::memory_order_relaxed);
                saturation_observed.store(true, std::memory_order_release);
                if (sequence_error.load(std::memory_order_acquire) || deadline_expired()) {
                    return;
                }
                std::this_thread::yield();
            }
            produced.store(sequence, std::memory_order_release);
            if (sequence_error.load(std::memory_order_acquire) || timed_out.load(std::memory_order_acquire)) {
                return;
            }
        }
    });

    std::thread consumer([&]() {
        // Deliberately let the producer fill the small queue once. This proves saturation,
        // then consumption demonstrates recovery while both queue owners remain concurrent.
        while (!saturation_observed.load(std::memory_order_acquire)) {
            if (deadline_expired()) {
                return;
            }
            std::this_thread::yield();
        }

        std::uint64_t expected = 1;
        while (expected <= sequence_count) {
            std::uint64_t actual = 0;
            if (!queue.try_pop(actual)) {
                if (sequence_error.load(std::memory_order_acquire) || deadline_expired()) {
                    return;
                }
                std::this_thread::yield();
                continue;
            }
            if (actual != expected) {
                expected_at_error.store(expected, std::memory_order_relaxed);
                actual_at_error.store(actual, std::memory_order_relaxed);
                sequence_error.store(true, std::memory_order_release);
                return;
            }
            consumed.store(expected, std::memory_order_release);
            ++expected;
        }
    });

    producer.join();
    consumer.join();

    const bool ok = !timed_out.load(std::memory_order_acquire) &&
        !sequence_error.load(std::memory_order_acquire) &&
        saturation_observed.load(std::memory_order_acquire) &&
        full_retries.load(std::memory_order_relaxed) > 0 &&
        produced.load(std::memory_order_acquire) == sequence_count &&
        consumed.load(std::memory_order_acquire) == sequence_count &&
        queue.empty();
    if (!ok) {
        std::cerr
            << "concurrent stress failed: iteration=" << iteration
            << " timed_out=" << timed_out.load(std::memory_order_relaxed)
            << " sequence_error=" << sequence_error.load(std::memory_order_relaxed)
            << " expected=" << expected_at_error.load(std::memory_order_relaxed)
            << " actual=" << actual_at_error.load(std::memory_order_relaxed)
            << " produced=" << produced.load(std::memory_order_relaxed)
            << " consumed=" << consumed.load(std::memory_order_relaxed)
            << " target=" << sequence_count
            << " full_retries=" << full_retries.load(std::memory_order_relaxed)
            << " queue_empty=" << queue.empty()
            << '\n';
    }
    return ok;
}

bool test_concurrent_stress() {
    constexpr int repeat_count = 3;
#if defined(LLT_THREAD_SANITIZER)
    // TSan instrumentation is intentionally given a larger hang guard than the normal
    // Release test. The CI process-level timeout remains the final 30-second backstop.
    constexpr auto release_deadline = std::chrono::seconds(25);
#else
    constexpr auto release_deadline = std::chrono::seconds(10);
#endif
    const auto deadline = std::chrono::steady_clock::now() + release_deadline;
    for (int iteration = 1; iteration <= repeat_count; ++iteration) {
        if (!run_concurrent_stress_iteration(iteration, deadline)) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main() {
    const bool ok = test_fifo_behavior() && test_full_and_reuse() && test_concurrent_stress();
    if (!ok) {
        return EXIT_FAILURE;
    }

    std::cout << "spsc_ring_buffer_tests: all tests passed\n";
    return EXIT_SUCCESS;
}
