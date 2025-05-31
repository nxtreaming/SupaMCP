/**
 * @file mcp_thread_pool_atomic.c
 * @brief Atomic operations for thread pool implementation.
 *
 * This file contains platform-specific atomic operations used by the thread pool
 * for lock-free synchronization between worker threads and task submission.
 */
#include "internal/mcp_thread_pool_internal.h"

// Atomic Compare-and-Swap for size_t
bool compare_and_swap_size(volatile size_t* ptr, size_t expected, size_t desired) {
#ifdef _WIN32
    return InterlockedCompareExchangePointer((volatile PVOID*)ptr, (PVOID)desired, (PVOID)expected) == (PVOID)expected;
#else
    return __sync_bool_compare_and_swap(ptr, expected, desired);
#endif
}

// Atomic Load for size_t
size_t load_size(volatile size_t* ptr) {
#ifdef _WIN32
    size_t value = *ptr;
    _ReadWriteBarrier();
    return value;
#else
    return __sync_fetch_and_add(ptr, 0);
#endif
}

// Atomic Load for int
int load_int(volatile int* ptr) {
#ifdef _WIN32
    int value = *ptr;
    _ReadWriteBarrier();
    return value;
#else
    return __sync_fetch_and_add(ptr, 0);
#endif
}

// Atomic Store for int
void store_int(volatile int* ptr, int value) {
#ifdef _WIN32
    InterlockedExchange((volatile LONG*)ptr, (LONG)value);
#else
    __sync_lock_test_and_set(ptr, value);
#endif
}

// Atomic Fetch-and-Add for size_t
size_t fetch_add_size(volatile size_t* ptr, size_t value) {
#ifdef _WIN32
    // Use appropriate Windows atomic function based on pointer size
#if defined(_WIN64)
    // 64-bit Windows
    return (size_t)InterlockedExchangeAdd64((volatile LONGLONG*)ptr, (LONGLONG)value);
#   else
    // 32-bit Windows
    return (size_t)InterlockedExchangeAdd((volatile LONG*)ptr, (LONG)value);
#   endif
#else
    return __sync_fetch_and_add(ptr, value);
#endif
}
