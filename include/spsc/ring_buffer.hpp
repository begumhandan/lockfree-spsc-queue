#pragma once

#include <array>
#include <cstddef>

namespace spsc {

template <typename T, std::size_t N>
class RingBuffer {
    static_assert(N > 0 && (N & (N - 1)) == 0,
                  "RingBuffer: N must be a power of two (and > 0)");

    static constexpr std::size_t kMask = N - 1;

public:
    static constexpr std::size_t capacity() noexcept { return N; }

    // Sadece üretici çağırır.
    bool try_push(const T& value) {
        if (head_ - tail_ == N) {
            return false;                    // dolu
        }
        buffer_[head_ & kMask] = value;
        ++head_;
        return true;
    }

    // Sadece tüketici çağırır.
    bool try_pop(T& out) {
        if (head_ == tail_) {
            return false;                    // boş
        }
        out = buffer_[tail_ & kMask];
        ++tail_;
        return true;
    }

    bool empty() const noexcept { return head_ == tail_; }
    std::size_t size() const noexcept { return head_ - tail_; }

private:
    std::array<T, N> buffer_{};
    // Index'ler hiç sarılmaz, sürekli artar; diziye erişirken maskelenir.
    std::size_t head_ = 0;  // sonraki yazma konumu: SADECE üretici yazar
    std::size_t tail_ = 0;  // sonraki okuma konumu: SADECE tüketici yazar
};

}  // namespace spsc