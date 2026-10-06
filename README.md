# PulseGrid (tcp-server)

PulseGrid is a multithreaded C++17 TCP server with a custom binary protocol and a sharded in-memory key-value store. One edge-triggered epoll thread handles I/O, and a fixed worker pool pulls requests from lock-free bounded MPMC queues.

Runs on Linux or WSL2.

## Architecture

- **I/O:** one edge-triggered epoll thread over nonblocking POSIX sockets, with eventfd wakeups.
- **Work queues:** bounded Vyukov-style MPMC rings. The queue is lock-free: `std::atomic` and CAS on the head, tail, and per-slot sequence numbers, with no mutexes, spinlocks, or condition variables. A full or empty queue returns instead of blocking. It is not wait-free (contended CAS retries).
- **Workers:** a fixed pool; idle workers busy-yield.
- **Framing:** incremental binary framing that handles partial and coalesced frames.
- **Key-value store:** 64 shards with `shared_mutex` locking. Metrics use a mutex.

Only the work queue is lock-free; the store and metrics use locks.

## Build and run

```bash
chmod +x scripts/*.sh
./scripts/build.sh

./build/tds_server --port 9000 --workers 8

./build/tds_client ping
./build/tds_client put user:1 alice
./build/tds_client get user:1
```

Benchmark:
```bash
./build/tds_bench --port 9000 --connections 200 --requests 500 --mode ping
```

## Results (WSL2, 200 clients)

Lock-free MPMC queue vs a mutex-protected queue, 20 randomized runs per implementation and workload:

| Workload | Median throughput, MPMC vs mutex |
|---|---|
| GET | 41,232 vs 36,844 req/s (+11.9%) |
| PUT | +1.5% |
| PING | -0.5% |

- Zero errors or queue drops across all 120 runs.
- GET throughput varied more with the MPMC queue (standard deviation 9,796 vs 6,037 req/s), and the mutex queue had slightly lower median p95 and p99 latency.
- The gain is GET-specific in this test; PUT and PING were roughly unchanged.

## Testing

- Framing tests for partial and coalesced frames.
- A 100K-item MPMC concurrency test with two producers and two consumers.
- An 8-writer, 8-reader sharded-store test.

Per-connection write queues exist, but backpressure under `EAGAIN` is not fully hardened yet.

**Sanitizers (WSL2):**

- AddressSanitizer build (`TDS_ENABLE_ASAN=ON`, RelWithDebInfo): all 4 unit tests pass, and a 50x200 ping/put/get load ran with 0 errors and no ASan reports.
- ThreadSanitizer build: all 4 unit tests pass, and a 20x100 ping/put/get load ran with no TSan warnings. On WSL2, plain `ctest` fails with "FATAL: ThreadSanitizer: unexpected memory mapping", so run it under `setarch x86_64 -R`.

## Protocol

Binary frames with magic bytes `TDPS`, then version, type, length, payload. Supports ping, echo, get, put, and stats.

## Requirements

Linux or WSL2, CMake, GCC or Clang
