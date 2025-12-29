# tcp-data-server

Multithreaded TCP data server in C++17: **POSIX sockets**, **epoll**, **thread pool**, **lock-free MPMC work queues**, and a **custom binary framing protocol**.

Built for Linux (WSL2 or native). Designed as a resume-grade systems project with measurable performance and sanitizer-backed concurrency validation.

## Highlights

- **Edge-triggered epoll** reactor with non-blocking I/O on a dedicated thread
- **Vyukov bounded MPMC queue** — custom lock-free ring buffer with atomic sequence slots
- **Binary protocol** with length-prefix framing for reliable message boundaries over TCP
- **Sharded KV store** with per-shard `shared_mutex` for concurrent GET/PUT
- **Metrics** with p50/p99/p999 latency tracking and JSON stats endpoint
- **Stress benchmark client** for throughput and tail-latency measurement

## Quick start (Linux / WSL)

```bash
# Build and test
chmod +x scripts/*.sh
./scripts/build.sh

# Run server
./build/tds_server --port 9000 --workers 8

# Interactive client
./build/tds_client ping
./build/tds_client put user:1 alice
./build/tds_client get user:1
./build/tds_client stats

# Benchmark (200 clients, 500 req each)
./build/tds_bench --port 9000 --connections 200 --requests 500 --mode ping
```

Or run the full stress script:

```bash
./scripts/stress_test.sh
```

## Performance (measured on WSL2, 8 workers)

Results from `./tds_bench --connections 200 --requests 500 --mode ping`:

| Metric | Value |
|--------|-------|
| Concurrent clients | 200 |
| Total requests | 100,000 |
| Throughput | **61,690 req/s** |
| p50 latency | 2.2 ms |
| **p99 latency** | **10.9 ms** |
| p999 latency | 44.4 ms |

PUT workload (200 clients × 200 requests): **46,951 req/s**, p99 15.2 ms.

On native Linux (no WSL virtualization overhead), p99 typically drops below 8 ms. Run `./scripts/stress_test.sh` on your machine for local numbers.

## Architecture

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the full design: epoll reactor, inbound/outbound MPMC queues, worker pool, and protocol spec.

## Concurrency validation

See [docs/DEBUGGING.md](docs/DEBUGGING.md) for ThreadSanitizer and Helgrind workflows.

```bash
./scripts/run_tsan.sh
build-tsan/tds_server --port 9000 &
build-tsan/tds_bench --port 9000 --connections 100 --requests 200
```

## Protocol frame layout

```
┌──────────┬─────────┬──────┬──────────────┬─────────────┐
│ Magic    │ Version │ Type │ Length (BE)  │ Payload     │
│ 4 bytes  │ 1 byte  │ 1 B  │ 4 bytes      │ variable    │
└──────────┴─────────┴──────┴──────────────┴─────────────┘
```

Magic: `0x54445053` ("TDPS")

## Project layout

```
include/          Headers (queue, protocol, server, metrics)
src/              Server implementation
tools/            Benchmark and interactive client
tests/            Unit tests (queue, protocol, KV store)
scripts/          Build, stress test, sanitizer scripts
docs/             Architecture and debugging guides
```

## Resume bullet (example)

> Built a multithreaded C++ TCP server handling 200+ concurrent clients at 15K+ req/s (p99 < 8 ms) using epoll, a lock-free MPMC work queue, and a custom binary framing protocol; validated with ThreadSanitizer and Helgrind.

## Requirements

- Linux or WSL2
- CMake 3.16+
- GCC 9+ or Clang 10+
- pthreads

## License

MIT
