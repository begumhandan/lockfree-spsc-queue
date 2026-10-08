#include <spsc/ring_buffer.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// Karşılaştırma için: aynı arayüz, sınırlı kapasite, tek bir mutex.
template <typename T, std::size_t N>
class MutexQueue {
public:
    bool try_push(const T& value) {
        std::lock_guard<std::mutex> lock(m_);
        if (q_.size() == N) return false;
        q_.push(value);
        return true;
    }
    bool try_pop(T& out) {
        std::lock_guard<std::mutex> lock(m_);
        if (q_.empty()) return false;
        out = q_.front();
        q_.pop();
        return true;
    }

private:
    std::mutex m_;
    std::queue<T> q_;
};

constexpr std::size_t kCapacity = 1024;
constexpr std::uint64_t kCount = 10'000'000;
constexpr int kRuns = 5;

// Bir koşu: kCount elemanı üreticiden tüketiciye geçir, Mops/s döndür.
template <typename Queue>
double run_once() {
    Queue q;
    std::uint64_t sum = 0;

    const auto start = std::chrono::steady_clock::now();

    std::thread producer([&] {
        for (std::uint64_t i = 0; i < kCount; ++i) {
            while (!q.try_push(i)) { /* dolu: bekle */ }
        }
    });
    std::thread consumer([&] {
        std::uint64_t v;
        for (std::uint64_t i = 0; i < kCount; ++i) {
            while (!q.try_pop(v)) { /* boş: bekle */ }
            sum += v;
        }
    });

    producer.join();
    consumer.join();

    const auto end = std::chrono::steady_clock::now();

    // Doğruluk kontrolü: 0 + 1 + ... + (kCount-1)
    if (sum != kCount * (kCount - 1) / 2) {
        std::fprintf(stderr, "HATA: checksum tutmadı\n");
        std::exit(1);
    }

    const double seconds = std::chrono::duration<double>(end - start).count();
    return static_cast<double>(kCount) / seconds / 1e6;
}

template <typename Queue>
void report(const char* name, double baseline) {
    std::vector<double> results;
    for (int r = 0; r < kRuns; ++r) results.push_back(run_once<Queue>());
    std::sort(results.begin(), results.end());

    const double median = results[kRuns / 2];
    std::printf("| %-24s | %8.1f | %8.1f | %8.1f | %6.2fx |\n",
                name, median, results.front(), results.back(),
                baseline > 0 ? median / baseline : 1.0);
}

int main() {
    std::printf("%llu eleman, kapasite %zu, %d koşu (medyan)\n\n",
                static_cast<unsigned long long>(kCount), kCapacity, kRuns);
    std::printf("| %-24s | %8s | %8s | %8s | %7s |\n",
                "Kuyruk", "Medyan", "Min", "Max", "Oran");
    std::printf("|%s|%s|%s|%s|%s|\n",
                "--------------------------", "----------", "----------",
                "----------", "---------");

    // Önce mutex'li kuyruğu ölç, onu referans (1.00x) al.
    std::vector<double> base;
    for (int r = 0; r < kRuns; ++r)
        base.push_back(run_once<MutexQueue<std::uint64_t, kCapacity>>());
    std::sort(base.begin(), base.end());
    const double baseline = base[kRuns / 2];
    std::printf("| %-24s | %8.1f | %8.1f | %8.1f | %6.2fx |\n",
                "std::mutex + std::queue", baseline, base.front(), base.back(), 1.0);

    report<spsc::RingBuffer<std::uint64_t, kCapacity>>("spsc::RingBuffer", baseline);

    std::printf("\nBirim: milyon eleman/saniye (Mops/s)\n");
}