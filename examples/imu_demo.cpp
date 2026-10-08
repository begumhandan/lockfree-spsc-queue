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
    std::uint64_t seq = 0;          // sequence number: used to detect lost samples
    Clock::time_point t{};          // creation time: used to measure latency
    float ax = 0, ay = 0, az = 0;   // acceleration (m/s²)
    float gx = 0, gy = 0, gz = 0;   // angular rate (rad/s)
};

// 256 slots = ~256 ms of buffering at 1 kHz
using ImuQueue = spsc::RingBuffer<ImuSample, 256>;

// Sensor thread: produces samples at 1 kHz and NEVER blocks.
void sensor_thread(ImuQueue& q, const std::atomic<bool>& running,
                   std::atomic<std::uint64_t>& dropped) {
    std::mt19937 rng(42);
    std::normal_distribution<float> noise(0.0f, 0.05f);

    constexpr auto kPeriod = 1ms;
    auto next = Clock::now();
    std::uint64_t seq = 0;

    while (running.load(std::memory_order_relaxed)) {
        next += kPeriod;                       // absolute deadline: no drift
        std::this_thread::sleep_until(next);

        const float t = static_cast<float>(seq) * 0.001f;
        ImuSample s;
        s.seq = seq++;
        s.t = Clock::now();
        s.ax = noise(rng);
        s.ay = noise(rng);
        s.az = 9.81f + noise(rng);             // gravity + noise
        s.gx = 0.10f * std::sin(t);            // gentle oscillation
        s.gy = 0.05f * std::cos(t);
        s.gz = noise(rng) * 0.1f;

        if (!q.try_push(s)) {
            dropped.fetch_add(1, std::memory_order_relaxed);  // full: drop it
        }
    }
}

// Logger thread: drains the queue and prints a summary once per second.
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

            if (s.seq != expected_seq) gaps += s.seq - expected_seq;  // skipped samples
            expected_seq = s.seq + 1;
            ++count;
        }

        const auto now = Clock::now();
        if (now - window_start >= 1s) {
            const double secs = std::chrono::duration<double>(now - window_start).count();
            std::printf("[%2d s] %4llu samples (%6.1f Hz) | mean az = %6.3f m/s² | "
                        "latency avg/max = %6.1f / %7.1f µs | lost: %llu\n",
                        ++second, static_cast<unsigned long long>(count), count / secs,
                        count ? sum_az / count : 0.0,
                        count ? sum_lat_us / count : 0.0, max_lat_us,
                        static_cast<unsigned long long>(gaps));
            count = gaps = 0;
            sum_az = sum_lat_us = max_lat_us = 0;
            window_start = now;
        }

        std::this_thread::sleep_for(200us);  // polling interval
    }
}

int main() {
    ImuQueue q;
    std::atomic<bool> running{true};
    std::atomic<std::uint64_t> dropped{0};

    std::printf("IMU demo: 1 kHz sensor -> SPSC ring buffer (256) -> logger thread, 5 s\n\n");

    std::thread logger(logger_thread, std::ref(q), std::cref(running));
    std::thread sensor(sensor_thread, std::ref(q), std::cref(running), std::ref(dropped));

    std::this_thread::sleep_for(5s);
    running.store(false, std::memory_order_relaxed);

    sensor.join();
    logger.join();

    std::printf("\nTotal dropped samples (queue full): %llu\n",
                static_cast<unsigned long long>(dropped.load()));
}