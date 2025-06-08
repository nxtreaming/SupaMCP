#include "internal/websocket_server_internal.h"

// Atomic bitmap helper functions for client slot management with bounds checking
void ws_server_set_client_bit(uint32_t* bitmap, int index, int bitmap_size) {
    if (!bitmap || index < 0 || (index >> 5) >= bitmap_size) {
        return;
    }

    uint32_t word_index = index >> 5;
    uint32_t bit_mask = 1U << (index & 31);

    // Use atomic OR operation to set the bit
#ifdef _WIN32
    InterlockedOr((volatile LONG*)&bitmap[word_index], bit_mask);
#else
    __sync_or_and_fetch(&bitmap[word_index], bit_mask);
#endif
}

void ws_server_clear_client_bit(uint32_t* bitmap, int index, int bitmap_size) {
    if (!bitmap || index < 0 || (index >> 5) >= bitmap_size) {
        return;
    }

    uint32_t word_index = index >> 5;
    uint32_t bit_mask = ~(1U << (index & 31));

    // Use atomic AND operation to clear the bit
#ifdef _WIN32
    InterlockedAnd((volatile LONG*)&bitmap[word_index], bit_mask);
#else
    __sync_and_and_fetch(&bitmap[word_index], bit_mask);
#endif
}

bool ws_server_test_client_bit(uint32_t* bitmap, int index, int bitmap_size) {
    if (!bitmap || index < 0 || (index >> 5) >= bitmap_size) {
        return false;
    }

    uint32_t word_index = index >> 5;
    uint32_t bit_mask = 1U << (index & 31);

    // Use atomic load to read the bit
#ifdef _WIN32
    uint32_t word_value = InterlockedOr((volatile LONG*)&bitmap[word_index], 0);
#else
    uint32_t word_value = __sync_or_and_fetch(&bitmap[word_index], 0);
#endif

    return (word_value & bit_mask) != 0;
}

// Find first free client slot using bitmap with optimized bit scanning and atomic operations
int ws_server_find_free_client_slot(ws_server_data_t* data) {
    if (!data || !data->client_bitmap) {
        return -1;
    }

    // Quick atomic check if we're already at max capacity
    uint32_t current_active = MCP_ATOMIC_LOAD(data->active_clients);
    if (current_active >= data->max_clients) {
        return -1;
    }

    // Use bitmap to find first free slot with atomic reads
    const uint32_t num_words = data->bitmap_size;

    for (uint32_t i = 0; i < num_words; i++) {
        // Atomic read of the bitmap word
#ifdef _WIN32
        uint32_t word = InterlockedOr((volatile LONG*)&data->client_bitmap[i], 0);
#else
        uint32_t word = __sync_or_and_fetch(&data->client_bitmap[i], 0);
#endif

        // If word is full (all bits set), skip to next word
        if (word == 0xFFFFFFFF) {
            continue;
        }

        // Find first zero bit in this word using optimized approach
        // ~word gives us 1s where the original had 0s (free slots)
        uint32_t free_bits = ~word;

        // Use compiler intrinsics for bit scanning if available
        #if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
            // MSVC on x86/x64
            unsigned long bit_pos;
            _BitScanForward(&bit_pos, free_bits);
            int j = (int)bit_pos;
        #elif defined(__GNUC__) || defined(__clang__)
            // GCC or Clang
            int j = __builtin_ffs(free_bits) - 1;
        #else
            // Fallback for other compilers - find first set bit manually
            int j = 0;
            while (j < 32 && (free_bits & (1U << j)) == 0) {
                j++;
            }
        #endif

        uint32_t index = (i << 5) + j;
        if (index < data->max_clients) {
            return (int)index;
        }
    }

    return -1; // No free slots found
}

// Optimized locking functions with read-write locks and fine-grained access
mcp_mutex_t* ws_server_get_client_mutex(ws_server_data_t* data, int client_index) {
    if (!data || !data->segment_mutexes || client_index < 0 || client_index >= (int)data->max_clients) {
        return data->global_mutex; // Fallback to global mutex
    }

    int segment = client_index % data->num_segments;
    return data->segment_mutexes[segment];
}

void ws_server_lock_client(ws_server_data_t* data, int client_index) {
    mcp_mutex_t* mutex = ws_server_get_client_mutex(data, client_index);
    if (mutex) {
        mcp_mutex_lock(mutex);
    }
}

void ws_server_unlock_client(ws_server_data_t* data, int client_index) {
    mcp_mutex_t* mutex = ws_server_get_client_mutex(data, client_index);
    if (mutex) {
        mcp_mutex_unlock(mutex);
    }
}

// Read lock for operations that only read client data
void ws_server_read_lock_clients(ws_server_data_t* data) {
    if (!data || !data->clients_rwlock) {
        return;
    }
    mcp_rwlock_read_lock(data->clients_rwlock);
}

void ws_server_read_unlock_clients(ws_server_data_t* data) {
    if (!data || !data->clients_rwlock) {
        return;
    }
    mcp_rwlock_read_unlock(data->clients_rwlock);
}

// Write lock for operations that modify client array structure
void ws_server_write_lock_clients(ws_server_data_t* data) {
    if (!data || !data->clients_rwlock) {
        return;
    }
    mcp_rwlock_write_lock(data->clients_rwlock);
}

void ws_server_write_unlock_clients(ws_server_data_t* data) {
    if (!data || !data->clients_rwlock) {
        return;
    }
    mcp_rwlock_write_unlock(data->clients_rwlock);
}

// Legacy functions for compatibility - now use global mutex only for critical operations
void ws_server_lock_all_clients(ws_server_data_t* data) {
    if (!data || !data->global_mutex) {
        return;
    }
    mcp_mutex_lock(data->global_mutex);
}

void ws_server_unlock_all_clients(ws_server_data_t* data) {
    if (!data || !data->global_mutex) {
        return;
    }
    mcp_mutex_unlock(data->global_mutex);
}
