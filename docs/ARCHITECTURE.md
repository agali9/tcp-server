# Architecture

## Overview

`tcp-data-server` is a Linux TCP server that separates **I/O** from **request processing**:

```
                    ┌──────────────┐
  Clients ──TCP──► │  I/O Thread  │  epoll (edge-triggered)
                    │  accept/read │
                    │  write flush │
                    └──────┬───────┘
                           │ complete frames
                           ▼
                    ┌──────────────┐
                    │ Inbound MPMC │  lock-free bounded queue
                    │    Queue     │
                    └──────┬───────┘
                           │
              ┌────────────┼────────────┐
              ▼            ▼            ▼
         Worker 1     Worker 2     Worker N
         (thread pool, fixed size)
              └────────────┼────────────┘
                           ▼
                    ┌──────────────┐
                    │ Outbound MPMC│  lock-free bounded queue
                    │    Queue     │
                    └──────┬───────┘
                           │ eventfd wake
                           ▼
                    I/O thread writes responses
```

## Why not one thread per client?

The naive pattern `accept → std::thread(handle).detach()` creates unbounded threads under load. Thread creation has kernel overhead, stack memory cost (~8 MB default), and scheduling pressure.

A fixed worker pool reuses threads and bounds resource usage.

## Lock-free MPMC queue

Implementation: **Vyukov bounded MPMC ring buffer** (`include/mpmc_queue.hpp`).

- **MPMC**: multiple I/O/event producers push work; multiple workers pop concurrently.
- **Synchronization**: per-slot sequence atomics + CAS on head/tail.
- **Memory ordering**: `memory_order_acquire` on sequence reads, `memory_order_release` on sequence writes, relaxed CAS on head/tail.
- **False sharing**: head and tail are `alignas(64)` on separate cache lines.

Push/pop are wait-free in the common case; full/empty checks return immediately without blocking.

## Binary framing protocol

TCP is a byte stream. Messages use a fixed header:

| Offset | Size | Field |
|--------|------|-------|
| 0 | 4 | Magic `0x54445053` ("TDPS") |
| 4 | 1 | Protocol version |
| 5 | 1 | Message type |
| 6 | 4 | Payload length (big-endian) |
| 10 | N | Payload |

The connection read buffer accumulates bytes until a full frame is available, then dispatches to workers. This handles arbitrary TCP segmentation/coalescing.

### Message types

| Type | Request | Response |
|------|---------|----------|
| Ping | empty | Pong |
| Echo | bytes | same bytes |
| Get | key | value or NotFound |
| Put | key + value | ack |
| Stats | empty | JSON metrics |

## I/O model

- **epoll** edge-triggered (`EPOLLET`) for listen socket and client fds
- **Non-blocking** sockets throughout
- **Single I/O thread** owns all `recv`/`send` — avoids per-socket write locks
- **eventfd** wakes the I/O thread when workers enqueue responses
- **TCP_NODELAY** enabled to reduce small-packet latency

## Data store

Sharded in-memory hash map (`KvStore`): 64 shards, each with a `shared_mutex`. Reads take shared locks; writes take exclusive locks. Reduces contention vs one global map lock.

## Metrics

Atomic counters for throughput/connections plus a reservoir-sampled latency tracker for p50/p99/p999. Exposed via `Stats` RPC and printed by the benchmark client.

## Concurrency debugging

See [DEBUGGING.md](DEBUGGING.md) for ThreadSanitizer / Helgrind workflows and the class of bugs this architecture is designed to avoid.
