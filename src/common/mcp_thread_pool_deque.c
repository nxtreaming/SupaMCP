/**
 * @file mcp_thread_pool_deque.c
 * @brief Work-stealing deque implementation for thread pool.
 *
 * This file implements lock-free work-stealing deques based on the Chase-Lev algorithm.
 * Each worker thread has its own deque where it can push/pop tasks from the bottom,
 * while other threads can steal tasks from the top.
 */

#include "internal/mcp_thread_pool_internal.h"

// Push task onto the bottom of the deque (owner thread only)
bool deque_push_bottom(work_stealing_deque_t* deque, mcp_task_t task) {
    size_t b = load_size(&deque->bottom);
    // Do not need to load top for push, only check against capacity later if needed
    // size_t t = load_size(&deque->top);
    // size_t size = b - t;
    // if (size >= (deque->capacity_mask + 1)) { return false; } // Check happens implicitly below

    size_t index = b & deque->capacity_mask;
    deque->buffer[index] = task;

    // Ensure buffer write is visible before bottom increment (memory barrier)
#ifdef _WIN32
    MemoryBarrier();
#else
    __sync_synchronize();
#endif

    // Increment bottom (owner only, volatile write is sufficient)
    deque->bottom = b + 1;
    return true;
}

// Pop task from the bottom of the deque (owner thread only)
bool deque_pop_bottom(work_stealing_deque_t* deque, mcp_task_t* task) {
    size_t b = load_size(&deque->bottom);
    if (b == 0) return false;
    b = b - 1;
    // Volatile write
    deque->bottom = b;

    // Ensure bottom write is visible before reading top (memory barrier)
#ifdef _WIN32
    MemoryBarrier();
#else
    __sync_synchronize();
#endif

    size_t t = load_size(&deque->top);
    // Use signed difference
    long size = (long)b - (long)t;

    if (size < 0) {
        // Deque was empty or became empty due to concurrent steal
        // Reset bottom to match top
        deque->bottom = t;
        return false;
    }

    // Get task from bottom
    size_t index = b & deque->capacity_mask;
    *task = deque->buffer[index];

    if (size == 0) {
        // Last item case: Race with thieves stealing top
        if (!compare_and_swap_size(&deque->top, t, t + 1)) {
            // Thief stole the item first
            deque->bottom = t + 1; // Acknowledge thief won
            return false; // Failed to pop
        }
        // Successfully took the last item
        deque->bottom = t + 1; // Reset bottom
    }
    // If size > 0, we successfully popped without contention on the last item

    return true;
}

// Steal task from the top of the deque (thief threads only)
bool deque_steal_top(work_stealing_deque_t* deque, mcp_task_t* task) {
    size_t t = load_size(&deque->top);

    // Ensure top read happens before reading bottom (memory barrier)
#ifdef _WIN32
    MemoryBarrier();
#else
    __sync_synchronize();
#endif

    size_t b = load_size(&deque->bottom);

    if ((long)t >= (long)b) { // Use signed comparison
        // Deque appears empty
        return false;
    }

    // Get task from top
    size_t index = t & deque->capacity_mask;
    *task = deque->buffer[index]; // Read task data first

    // Ensure task read happens before CAS (memory barrier)
#ifdef _WIN32
    MemoryBarrier();
#else
    __sync_synchronize();
#endif

    // Attempt to increment top using CAS
    if (compare_and_swap_size(&deque->top, t, t + 1)) {
        // Successfully stole the item
        return true;
    } else {
        // Another thief or the owner modified top/bottom concurrently
        return false;
    }
}
