# lockfree-spsc-queue

A header-only, lock-free **single-producer / single-consumer (SPSC) ring buffer** in C++17,
benchmarked against a `std::mutex` + `std::queue` baseline.

> 🚧 **Work in progress.** The concurrent core is implemented and verified under
> ThreadSanitizer; benchmarks are next. See [Roadmap](#roadmap).

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

## Testing

- **Unit tests:** empty/full, FIFO order, wrap-around, full-after-wrap.
- **Stress test:** one producer and one consumer pass 10M sequential integers
  through a 1024-slot buffer; the consumer checks that every value arrives
  exactly once and in order.
- **ThreadSanitizer:** the whole suite runs clean under TSan. As a sanity check,
  weakening the producer's `head` store to `relaxed` makes TSan report a data
  race on the buffer slot, confirming the release/acquire pair is load-bearing.

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

## Roadmap

- [x] CMake skeleton + GoogleTest
- [x] Single-threaded ring buffer with tests (empty/full, FIFO, wrap-around)
- [x] Atomic indices with acquire/release ordering
- [x] Two-thread stress test under ThreadSanitizer
- [ ] Benchmark vs. `std::mutex` + `std::queue`
- [ ] 1 kHz IMU producer/consumer demo
- [ ] CI: GCC + Clang, TSan job

## Limitations (by design)

- Exactly one producer and one consumer (not MPMC)
- Fixed capacity, no dynamic resizing
- No blocking push/pop
- Single-process only (no shared memory)