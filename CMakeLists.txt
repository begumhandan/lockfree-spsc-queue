cmake_minimum_required(VERSION 3.16)
project(spsc_ringbuf LANGUAGES CXX)
option(SPSC_TSAN "Build with ThreadSanitizer" OFF)
if(SPSC_TSAN)
  add_compile_options(-fsanitize=thread -g -O1)
  add_link_options(-fsanitize=thread)
  add_compile_definitions(SPSC_UNDER_TSAN)
endif()

find_package(Threads REQUIRED)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Header-only kütüphane
add_library(spsc INTERFACE)
target_include_directories(spsc INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Testler
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)  # Windows/MSVC için
FetchContent_MakeAvailable(googletest)

enable_testing()
add_executable(test_ring_buffer tests/test_ring_buffer.cpp)
target_link_libraries(test_ring_buffer PRIVATE spsc GTest::gtest_main Threads::Threads)
target_compile_options(test_ring_buffer PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>)

include(GoogleTest)
gtest_discover_tests(test_ring_buffer)