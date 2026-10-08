#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace spsc {

template <typename T, std::size_t N>
class RingBuffer {
    static_assert(N > 0 && (N & (N - 1)) == 0,
                  "RingBuffer: N must be a power of two (and > 0)");

    static constexpr std::size_t kMask = N - 1;

public:
    static constexpr std::size_t capacity() noexcept { return N; }

    // Sadece üretici thread çağırır.
    bool try_push(const T& value) {
        // Kendi index'im: sadece ben yazıyorum, senkronizasyon gerekmez.
        const std::size_t head = head_.load(std::memory_order_relaxed);
        // Tüketicinin index'i: acquire, çünkü tüketicinin o slotu okumayı
        // BİTİRDİĞİNİ görmeden üzerine yazmamalıyım.
        const std::size_t tail = tail_.load(std::memory_order_acquire);

        if (head - tail == N) {
            return false;  // dolu
        }

        buffer_[head & kMask] = value;
        // release: yukarıdaki veri yazımı, yeni head değerinden ÖNCE görünür olur.
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    // Sadece tüketici thread çağırır.
    bool try_pop(T& out) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        // acquire: üreticinin release ile yayınladığı head'i gördüysem,
        // o slottaki veriyi de eksiksiz görürüm.
        const std::size_t head = head_.load(std::memory_order_acquire);

        if (head == tail) {
            return false;  // boş
        }

        out = buffer_[tail & kMask];
        // release: okumam bitti, slotu üreticiye geri veriyorum.
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    // Eşzamanlı kullanımda sadece anlık bir tahmindir; değer hemen eskiyebilir.
    std::size_t size() const noexcept {
        return head_.load(std::memory_order_acquire) -
               tail_.load(std::memory_order_acquire);
    }
    bool empty() const noexcept { return size() == 0; }

private:
    std::array<T, N> buffer_{};
    std::atomic<std::size_t> head_{0};  // SADECE üretici yazar
    std::atomic<std::size_t> tail_{0};  // SADECE tüketici yazar
};

}  