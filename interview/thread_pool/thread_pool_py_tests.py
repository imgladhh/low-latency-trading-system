from __future__ import annotations

import threading
import time

from thread_pool import ThreadPool


def test_submit_returns_future_value() -> None:
    with ThreadPool(2) as pool:
        future = pool.submit(lambda lhs, rhs: lhs + rhs, 20, 22)
        assert future.result(timeout=1) == 42


def test_runs_many_tasks() -> None:
    counter = 0
    lock = threading.Lock()

    def increment() -> None:
        nonlocal counter
        with lock:
            counter += 1

    with ThreadPool(4) as pool:
        futures = [pool.submit(increment) for _ in range(128)]
        for future in futures:
            future.result(timeout=1)

    assert counter == 128


def test_shutdown_rejects_new_tasks() -> None:
    pool = ThreadPool(1)
    pool.shutdown()

    try:
        pool.submit(lambda: None)
    except RuntimeError:
        return

    raise AssertionError("submit should fail after shutdown")


def test_shutdown_drains_queued_tasks() -> None:
    counter = 0
    lock = threading.Lock()

    def slow_increment() -> None:
        nonlocal counter
        time.sleep(0.001)
        with lock:
            counter += 1

    with ThreadPool(2) as pool:
        for _ in range(32):
            pool.submit(slow_increment)

    assert counter == 32


def test_task_exception_reaches_future() -> None:
    def fail() -> None:
        raise ValueError("boom")

    with ThreadPool(1) as pool:
        future = pool.submit(fail)
        try:
            future.result(timeout=1)
        except ValueError as exc:
            assert str(exc) == "boom"
            return

    raise AssertionError("future.result should re-raise task exception")


def main() -> None:
    test_submit_returns_future_value()
    test_runs_many_tasks()
    test_shutdown_rejects_new_tasks()
    test_shutdown_drains_queued_tasks()
    test_task_exception_reaches_future()
    print("thread_pool_py_tests: all tests passed")


if __name__ == "__main__":
    main()
