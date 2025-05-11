#ifndef MCP_ATOM_H
#define MCP_ATOM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Platform-specific atomic operations
 * 
 * This header provides cross-platform atomic operations for thread-safe
 * counter updates without requiring locks.
 */

// Platform-specific atomic operations
#ifdef _WIN32
#include <windows.h>
#define ATOMIC_INCREMENT(var) InterlockedIncrement64((LONG64*)&(var))
#define ATOMIC_DECREMENT(var) InterlockedDecrement64((LONG64*)&(var))
#define ATOMIC_ADD(var, val) InterlockedAdd64((LONG64*)&(var), (LONG64)(val))
#define ATOMIC_SUBTRACT(var, val) InterlockedAdd64((LONG64*)&(var), -(LONG64)(val))
#define ATOMIC_EXCHANGE_MAX(var, val) do { \
    LONG64 old_val, new_val; \
    do { \
        old_val = *(LONG64*)&(var); \
        new_val = (old_val < (LONG64)(val)) ? (LONG64)(val) : old_val; \
    } while (InterlockedCompareExchange64((LONG64*)&(var), new_val, old_val) != old_val); \
} while(0)
#else
#include <stdatomic.h>
// For non-Windows platforms, we'll use GCC atomic builtins
#define ATOMIC_INCREMENT(var) __sync_add_and_fetch(&(var), 1)
#define ATOMIC_DECREMENT(var) __sync_sub_and_fetch(&(var), 1)
#define ATOMIC_ADD(var, val) __sync_add_and_fetch(&(var), (val))
#define ATOMIC_SUBTRACT(var, val) __sync_sub_and_fetch(&(var), (val))
#define ATOMIC_EXCHANGE_MAX(var, val) do { \
    size_t old_val; \
    do { \
        old_val = (var); \
        if (old_val >= (val)) break; \
    } while (!__sync_bool_compare_and_swap(&(var), old_val, (val))); \
} while(0)
#endif

#ifdef __cplusplus
}
#endif

#endif /* MCP_ATOM_H */
