#include <cstdint>
#include <thread>
#include <gtest/gtest.h>
#include <spsc/ring_buffer.hpp>


using spsc::RingBuffer;

TEST(RingBuffer, StartsEmpty) {
    RingBuffer<int, 8> rb;
    int x = -1;
    EXPECT_TRUE(rb.empty());
    EXPECT_FALSE(rb.try_pop(x));
    EXPECT_EQ(x, -1);  // başarısız pop, out'a dokunmamalı
}

TEST(RingBuffer, FillsToFullCapacity) {
    RingBuffer<int, 8> rb;
    for (int i = 0; i < 8; ++i) {
        EXPECT_TRUE(rb.try_push(i)) << "i=" << i;
    }
    EXPECT_EQ(rb.size(), 8u);
    EXPECT_FALSE(rb.try_push(99));  // dolu
}

TEST(RingBuffer, PreservesFifoOrder) {
    RingBuffer<int, 8> rb;
    for (int i = 0; i < 5; ++i) rb.try_push(i);
    for (int i = 0; i < 5; ++i) {
        int x;
        ASSERT_TRUE(rb.try_pop(x));
        EXPECT_EQ(x, i);
    }
    EXPECT_TRUE(rb.empty());
}

TEST(RingBuffer, WrapsAround) {
    RingBuffer<int, 4> rb;
    int next_in = 0, next_out = 0;
    // Her turda 3 yaz, 3 oku: index'ler dizinin sonunu defalarca geçer.
    for (int round = 0; round < 10; ++round) {
        for (int k = 0; k < 3; ++k) ASSERT_TRUE(rb.try_push(next_in++));
        for (int k = 0; k < 3; ++k) {
            int x;
            ASSERT_TRUE(rb.try_pop(x));
            EXPECT_EQ(x, next_out++);
        }
    }
    EXPECT_TRUE(rb.empty());
}

TEST(RingBuffer, FullAfterWrapThenDrains) {
    RingBuffer<int, 4> rb;
    int x;
    // index'leri kaydır
    for (int i = 0; i < 3; ++i) { rb.try_push(i); rb.try_pop(x); }
    // şimdi sarmayı geçerek doldur
    for (int i = 0; i < 4; ++i) ASSERT_TRUE(rb.try_push(100 + i));
    EXPECT_FALSE(rb.try_push(999));
    for (int i = 0; i < 4; ++i) {
        ASSERT_TRUE(rb.try_pop(x));
        EXPECT_EQ(x, 100 + i);
    }
}

TEST(RingBufferConcurrent, ProducerConsumerPreservesOrder) {
#ifdef SPSC_UNDER_TSAN
    constexpr std::uint64_t kCount = 1'000'000;   // TSan ~10x yavaşlatır
#else
    constexpr std::uint64_t kCount = 10'000'000;
#endif
    RingBuffer<std::uint64_t, 1024> rb;

    std::thread producer([&] {
        for (std::uint64_t i = 0; i < kCount; ++i) {
            while (!rb.try_push(i)) {
                std::this_thread::yield();   // dolu: tüketiciye fırsat ver
            }
        }
    });

    // gtest assert'lerini yan thread'de çağırmak yerine
    // sonucu kaydedip ana thread'de kontrol ediyoruz.
    std::uint64_t received = 0;
    std::uint64_t first_bad_index = kCount;   // kCount = hata yok
    std::uint64_t first_bad_value = 0;

    std::thread consumer([&] {
        std::uint64_t value;
        while (received < kCount) {
            if (rb.try_pop(value)) {
                if (value != received && first_bad_index == kCount) {
                    first_bad_index = received;
                    first_bad_value = value;
                }
                ++received;
            } else {
                std::this_thread::yield();   // boş
            }
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(received, kCount);
    EXPECT_EQ(first_bad_index, kCount)
        << "Sıra bozuldu: " << first_bad_index << ". elemanda "
        << first_bad_index << " beklenirken " << first_bad_value << " geldi";
    EXPECT_TRUE(rb.empty());
}