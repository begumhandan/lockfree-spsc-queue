# lockfree-spsc-queue

A header-only, lock-free **single-producer / single-consumer (SPSC) ring buffer** in C++17,
benchmarked against a `std::mutex` + `std::queue` baseline.

> 🚧 **Work in progress.** The single-threaded core is done; atomic indices and
> memory ordering are next. See [Roadmap](#roadmap).

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

Both operations are non-blocking and return `bool`.

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
*Coming in the next step.*

## Build & test

Requires CMake ≥ 3.16 and a C++17 compiler. GoogleTest is fetched automatically.

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## Roadmap

- [x] CMake skeleton + GoogleTest
- [x] Single-threaded ring buffer with tests (empty/full, FIFO, wrap-around)
- [ ] Atomic indices with acquire/release ordering
- [ ] Two-thread stress test under ThreadSanitizer
- [ ] Benchmark vs. `std::mutex` + `std::queue`
- [ ] 1 kHz IMU producer/consumer demo
- [ ] CI: GCC + Clang, TSan job

## Limitations (by design)

- Exactly one producer and one consumer (not MPMC)
- Fixed capacity, no dynamic resizing
- No blocking push/pop
- Single-process only (no shared memory)