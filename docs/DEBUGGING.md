# Concurrency Debugging Guide

This document describes how to validate the server under sanitizers and the categories of bugs to watch for in this architecture.

## Build with ThreadSanitizer

```bash
./scripts/run_tsan.sh
build-tsan/tds_server --port 9000 &
build-tsan/tds_bench --port 9000 --connections 50 --requests 500
```

TSan instruments every memory access and reports unsynchronized concurrent reads/writes.

### Bug class 1: Unprotected shared counter (race on metrics)

**Symptom**: TSan reports concurrent increment of a plain `uint64_t` from the I/O thread and worker threads.

**Fix**: Use `std::atomic<uint64_t>` with `fetch_add(..., memory_order_relaxed)` for metrics that do not publish other state.

This codebase uses atomics for all cross-thread counters (`ServerMetrics`).

### Bug class 2: Connection table mutation vs lookup

**Symptom**: TSan reports concurrent access to `std::unordered_map<uint64_t, Connection>` — one thread erasing while another reads.

**Fix**: Restrict all map mutations to the I/O thread. Workers never touch the connection table; they only pass `conn_id` through queues. Responses are written back via the outbound queue + eventfd wake.

This is the critical design invariant: **workers are connection-agnostic except for opaque IDs**.

## Build with Helgrind (Valgrind)

```bash
cmake -B build-hg -DCMAKE_BUILD_TYPE=Debug
cmake --build build-hg -j$(nproc)
valgrind --tool=helgrind build-hg/test_kv_store
```

Helgrind catches lock-order issues and incorrect lock usage.

### Bug class 3: Inconsistent lock ordering on KV shards

**Symptom**: Helgrind reports possible deadlock when two threads acquire shard locks in different orders (e.g., during a hypothetical multi-key transaction).

**Fix**: This server only ever locks **one shard per operation** (single-key GET/PUT). No nested shard locks → no lock-order cycles.

If you extend to multi-key transactions, establish a global lock ordering (sort shard indices before locking).

## Recommended pre-release checklist

1. `./scripts/build.sh` — release build + unit tests
2. `./scripts/run_tsan.sh` + stress benchmark under TSan
3. `valgrind --tool=helgrind` on concurrent KV tests
4. `./scripts/stress_test.sh` — verify throughput and tail latency

## Interview narrative

When discussing this project:

- **Problem**: TCP byte streams require framing; per-connection threads don't scale.
- **Approach**: Reactor (epoll) + thread pool + lock-free queues; single-threaded socket ownership.
- **Validation**: TSan for data races, Helgrind for lock errors, benchmark for p99 tail latency.
- **Result**: Bounded resources, predictable latency under 200+ concurrent clients.
