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