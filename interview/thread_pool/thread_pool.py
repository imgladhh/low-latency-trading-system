from __future__ import annotations

import os
import queue
import threading
from concurrent.futures import Future
from typing import Any, Callable


class ThreadPool:
    def __init__(self, thread_count: int | None = None) -> None:
        if thread_count is None:
            thread_count = os.cpu_count() or 1
        if thread_count <= 0:
            raise ValueError("thread_count must be greater than zero")

        self._tasks: queue.Queue[tuple[Callable[..., Any], tuple[Any, ...], dict[str, Any], Future[Any]] | None] = (
            queue.Queue()
        )
        self._lock = threading.Lock()
        self._accepting = True
        self._workers = [
            threading.Thread(target=self._worker_loop, name=f"ThreadPoolWorker-{i}", daemon=False)
            for i in range(thread_count)
        ]

        for worker in self._workers:
            worker.start()

    def submit(self, fn: Callable[..., Any], *args: Any, **kwargs: Any) -> Future[Any]:
        future: Future[Any] = Future()

        with self._lock:
            if not self._accepting:
                raise RuntimeError("cannot submit task after thread pool shutdown")
            self._tasks.put((fn, args, kwargs, future))

        return future

    def shutdown(self, wait: bool = True) -> None:
        with self._lock:
            if not self._accepting:
                return
            self._accepting = False

            for _ in self._workers:
                self._tasks.put(None)

        if wait:
            for worker in self._workers:
                worker.join()

    @property
    def thread_count(self) -> int:
        return len(self._workers)

    def _worker_loop(self) -> None:
        while True:
            item = self._tasks.get()
            try:
                if item is None:
                    return

                fn, args, kwargs, future = item
                if future.set_running_or_notify_cancel():
                    try:
                        future.set_result(fn(*args, **kwargs))
                    except BaseException as exc:
                        future.set_exception(exc)
            finally:
                self._tasks.task_done()

    def __enter__(self) -> ThreadPool:
        return self

    def __exit__(self, exc_type: object, exc: object, traceback: object) -> None:
        self.shutdown()
