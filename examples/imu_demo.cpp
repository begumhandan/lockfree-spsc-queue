#include <spsc/ring_buffer.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>
#include <thread>

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

struct ImuSample {
    std::uint64_t seq = 0;   // sıra numarası: kayıp tespiti için
    Clock::time_point t{};   // üretildiği an: gecikme ölçümü için
    float ax = 0, ay = 0, az = 0;  // ivme (m/s²)
    float gx = 0, gy = 0, gz = 0;  // açısal hız (rad/s)
};

// 256 slot = 1 kHz'de ~256 ms'lik tampon
using ImuQueue = spsc::RingBuffer<ImuSample, 256>;

// Sensör thread'i: 1 kHz'de örnek üretir, ASLA bloklanmaz.
void sensor_thread(ImuQueue& q, const std::atomic<bool>& running,
                   std::atomic<std::uint64_t>& dropped) {
    std::mt19937 rng(42);
    std::normal_distribution<float> noise(0.0f, 0.05f);

    constexpr auto kPeriod = 1ms;
    auto next = Clock::now();
    std::uint64_t seq = 0;

    while (running.load(std::memory_order_relaxed)) {
        next += kPeriod;                       // birikmeyen zamanlama
        std::this_thread::sleep_until(next);

        const float t = static_cast<float>(seq) * 0.001f;
        ImuSample s;
        s.seq = seq++;
        s.t = Clock::now();
        s.ax = noise(rng);
        s.ay = noise(rng);
        s.az = 9.81f + noise(rng);             // yerçekimi + gürültü
        s.gx = 0.10f * std::sin(t);            // hafif salınım
        s.gy = 0.05f * std::cos(t);
        s.gz = noise(rng) * 0.1f;

        if (!q.try_push(s)) {
            dropped.fetch_add(1, std::memory_order_relaxed);  // dolu: düşür
        }
    }
}

// Kayıt thread'i: örnekleri toplar, saniyede bir özet basar.
void logger_thread(ImuQueue& q, const std::atomic<bool>& running) {
    ImuSample s;
    std::uint64_t count = 0, gaps = 0, expected_seq = 0;
    double sum_az = 0, sum_lat_us = 0, max_lat_us = 0;
    int second = 0;
    auto window_start = Clock::now();

    while (running.load(std::memory_order_relaxed) || !q.empty()) {
        while (q.try_pop(s)) {
            const double lat_us =
                std::chrono::duration<double, std::micro>(Clock::now() - s.t).count();
            sum_lat_us += lat_us;
            if (lat_us > max_lat_us) max_lat_us = lat_us;
            sum_az += s.az;

            if (s.seq != expected_seq) gaps += s.seq - expected_seq;  // atlanan örnek
            expected_seq = s.seq + 1;
            ++count;
        }

        const auto now = Clock::now();
        if (now - window_start >= 1s) {
            const double secs = std::chrono::duration<double>(now - window_start).count();
            std::printf("[%2d s] %4llu örnek (%6.1f Hz) | ort. az = %6.3f m/s² | "
                        "gecikme ort/max = %6.1f / %7.1f µs | kayıp: %llu\n",
                        ++second, static_cast<unsigned long long>(count), count / secs,
                        count ? sum_az / count : 0.0,
                        count ? sum_lat_us / count : 0.0, max_lat_us,
                        static_cast<unsigned long long>(gaps));
            count = gaps = 0;
            sum_az = sum_lat_us = max_lat_us = 0;
            window_start = now;
        }

        std::this_thread::sleep_for(200us);  // yoklama aralığı
    }
}

int main() {
    ImuQueue q;
    std::atomic<bool> running{true};
    std::atomic<std::uint64_t> dropped{0};

    std::printf("IMU demo: 1 kHz sensör → SPSC ring buffer (256) → kayıt thread'i, 5 sn\n\n");

    std::thread logger(logger_thread, std::ref(q), std::cref(running));
    std::thread sensor(sensor_thread, std::ref(q), std::cref(running), std::ref(dropped));

    std::this_thread::sleep_for(5s);
    running.store(false, std::memory_order_relaxed);

    sensor.join();
    logger.join();

    std::printf("\nToplam düşürülen örnek (tampon dolu): %llu\n",
                static_cast<unsigned long long>(dropped.load()));
}