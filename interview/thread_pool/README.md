# Auxiliary Thread-Pool Exercise

This directory is a standalone interview exercise in C++ and Python. It demonstrates futures,
worker shutdown, queue draining, and exception propagation. It is **not** part of the trading
kernel, is not linked to `trading_kernel`, and is not evidence for the kernel's hot-path design.

The trading application deliberately uses a single-producer/single-consumer ring buffer for its
optional event side channel. A general-purpose thread pool is not its intended concurrency model.

The C++ exercise is excluded from normal builds. Enable and test it explicitly with:

```powershell
cmake -S . -B build-interview -DLLT_BUILD_INTERVIEW_EXERCISES=ON
cmake --build build-interview --target thread_pool_interview_tests
ctest --test-dir build-interview -R thread_pool_interview_tests --output-on-failure
```

The Python version has no dependency on the C++ project:

```powershell
python interview/thread_pool/thread_pool_py_tests.py
```
