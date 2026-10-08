# lockfree-spsc-queue

[![CI](https://github.com/begumhandan/lockfree-spsc-queue/actions/workflows/ci.yml/badge.svg)](https://github.com/begumhandan/lockfree-spsc-queue/actions/workflows/ci.yml)

A header-only, lock-free **single-producer / single-consumer (SPSC) ring buffer** in C++17,
benchmarked against a `std::mutex` + `std::queue` baseline.

**Highlights:** ~9.4× the throughput of a mutex-based queue · verified under ThreadSanitizer · CI on GCC & Clang · 1 kHz sensor demo with zero dropped samples

## Why?

SPSC queues are a staple of real-time and embedded systems: a sensor thread
writes samples while a logging/telemetry thread reads them, and **neither ever
waits on a lock**. This project builds one from scratch, verifies it under
ThreadSanitizer, and measures whether it is actually faster than the naive approach.

## Usage

```cpp
#include <spsc/ring_buffer.hpp>

spsc::RingBuffer<int, 1024> rb;   // capacity must be a power of two

// producer thread
if (!rb.try_push(42)) { /* full: drop or retry */ }

// consumer thread
int value;
if (rb.try_pop(value)) { /* got one */ }
```

Both operations are non-blocking and return `bool`. Exactly one thread may call
`try_push` and exactly one (other) thread may call `try_pop`.

## Design decisions

### Capacity is a compile-time power of two
`RingBuffer<T, N>` enforces this with a `static_assert`. It lets index wrapping
be a single bitwise AND (`i & (N - 1)`) instead of a modulo.

### Full vs. empty: unbounded indices
`head == tail` is ambiguous in a ring buffer: it can mean empty or full. Two
common fixes exist:

| Approach | Usable slots | Full check |
|---|---|---|
| Sacrifice one slot | `N - 1` | `next(head) == tail` |
| **Never wrap indices, mask on access** (chosen) | `N` | `head - tail == N` |

Indices only ever increase and are masked when indexing the array. All `N`
slots are usable, and unsigned subtraction keeps `head - tail` correct even if
the counters overflow.

### Ownership
`head` is written **only** by the producer and `tail` **only** by the consumer.
This single-writer rule is what makes a lock-free SPSC design possible.

### Memory ordering
Both indices are `std::atomic<std::size_t>`. Each side forms a
**release → acquire** pair with the other:

| Index | Written with `release` by | Read with `acquire` by | Guarantees |
|---|---|---|---|
| `head` | producer, *after* writing the slot | consumer, *before* reading the slot | the slot's data is visible |
| `tail` | consumer, *after* reading the slot | producer, *before* overwriting the slot | the slot is free to reuse |

Each thread reads **its own** index with `relaxed`, since no other thread ever
writes it. Each operation loads the atomics once into locals so the bounds
check and the access see the same values.

**Why not `relaxed` everywhere?** Without the release/acquire pair, the compiler
or CPU may make the new `head` visible before the data it guards. On x86
(TSO) stores are not reordered with each other, so a naive test often still
passes — but on ARM it can read garbage, and the compiler is free to reorder
on any architecture. ThreadSanitizer flags this immediately (see below).


## Benchmark

One producer and one consumer pass 10M `uint64_t` values through a 1024-slot
queue; both sides busy-wait when full/empty. Median of 5 runs, Release build.

| Queue | Median (Mops/s) | Min | Max | Speedup |
|---|---:|---:|---:|---:|
| `std::mutex` + `std::queue` (bounded) | 4.5 | 4.4 | 4.7 | 1.00× |
| `spsc::RingBuffer` | 42.7 | 35.1 | 70.2 | **9.4×** |

*Intel Core i9-13900H (6P + 8E cores, 20 threads), GCC 15.2, Ubuntu on WSL2.*

**Reading the numbers.** The mutex queue is stable because lock contention
dominates regardless of where threads run. The lock-free queue is bounded by
cache-line transfer between cores, so its throughput depends on scheduling:
two hyperthreads of one P-core share L1/L2 (fastest), while a P-core/E-core
pair goes through a slower cache level. `head` and `tail` currently share a
cache line (false sharing). Separating them is the next optimization.

## Demo: 1 kHz IMU pipeline

`examples/imu_demo.cpp` simulates the classic flight-software pattern: a sensor
thread produces an IMU sample every 1 ms and **never blocks** (if the queue is
full, the sample is dropped and counted), while a logger thread drains the
queue at its own pace and prints per-second statistics.

```
[ 1 s] 1000 samples ( 999.8 Hz) | mean az =  9.811 m/s² | latency avg/max =  150.1 /   667.9 µs | lost: 0
[ 2 s] 1000 samples ( 999.8 Hz) | mean az =  9.811 m/s² | latency avg/max =  167.7 /  5457.6 µs | lost: 0
[ 3 s] 1000 samples ( 999.7 Hz) | mean az =  9.809 m/s² | latency avg/max =  150.9 /   646.2 µs | lost: 0
[ 4 s] 1000 samples ( 999.9 Hz) | mean az =  9.809 m/s² | latency avg/max =  148.7 /   749.2 µs | lost: 0
```

- Average latency is dominated by the logger's polling interval, not the queue.
- The 5.4 ms spike in second 2 is the OS preempting the logger thread. The
  256-slot buffer (~256 ms at 1 kHz) absorbed it with **zero lost samples**.
- With an 8-slot buffer and a logger polling every 20 ms, the logger receives
  ~400 samples/s and ~600/s are dropped, but **received + lost stays at ~1000
  every second**: the sensor loop never slows down.

```bash
./build-release/imu_demo
```


## Testing

- **Unit tests:** empty/full, FIFO order, wrap-around, full-after-wrap.
- **Stress test:** one producer and one consumer pass 10M sequential integers
  through a 1024-slot buffer; the consumer checks that every value arrives
  exactly once and in order.
- **ThreadSanitizer:** the whole suite runs clean under TSan. As a sanity check,
  weakening the producer's `head` store to `relaxed` makes TSan report a data
  race between the slot write in `try_push` and the slot read in `try_pop`,
  **even though the stress test itself still passes** on x86. Tests alone
  would not have caught this; TSan checks against the C++ memory model.

## Build & test

Requires CMake ≥ 3.16 and a C++17 compiler (GCC or Clang). GoogleTest is fetched automatically.

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

With ThreadSanitizer:

```bash
cmake -S . -B build-tsan -G Ninja -DSPSC_TSAN=ON
cmake --build build-tsan
ctest --test-dir build-tsan --output-on-failure
```

> On WSL2 / recent kernels, TSan may abort with *"unexpected memory mapping"*.
> Run `sudo sysctl vm.mmap_rnd_bits=28` and retry.

Benchmark and demo (Release build):

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
./build-release/bench_vs_mutex
./build-release/imu_demo
```

## Roadmap

- [x] CMake skeleton + GoogleTest
- [x] Single-threaded ring buffer with tests (empty/full, FIFO, wrap-around)
- [x] Atomic indices with acquire/release ordering
- [x] Two-thread stress test under ThreadSanitizer
- [x] Benchmark vs. `std::mutex` + `std::queue`
- [x] 1 kHz IMU producer/consumer demo
- [x] CI: GCC + Clang, TSan job

### Next
- [ ] Separate `head`/`tail` onto different cache lines (`alignas(64)`) and measure false-sharing impact
- [ ] Cache the other side's index locally to reduce cross-core traffic
- [ ] Batch `push_n` / `pop_n`
- [ ] Latency histogram (p50/p99)

## Limitations (by design)

- Exactly one producer and one consumer (not MPMC)
- Fixed capacity, no dynamic resizing
- No blocking push/pop
- Single-process only (no shared memory)