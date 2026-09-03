# tcp-data-server

A multithreaded TCP server I wrote in C++ to practice concurrency. Uses epoll, a thread pool, and a lock-free queue. Has a simple key-value store and a custom binary protocol.

Runs on Linux or WSL2.

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

## Results (WSL2, 8 workers)

Ping test with 200 clients, 500 requests each:
- ~62k req/s
- p50: 2.2 ms
- p99: 10.9 ms

## Protocol

Binary frames with magic bytes `TDPS`, then version, type, length, payload. Supports ping, echo, get, put, and stats.

More details in `docs/ARCHITECTURE.md`.

## Requirements

Linux or WSL2, CMake, GCC or Clang
