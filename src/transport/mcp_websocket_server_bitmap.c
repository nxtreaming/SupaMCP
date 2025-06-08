#include "internal/websocket_server_internal.h"

// Optimized bitmap helper functions for client slot management with bounds checking
void ws_server_set_client_bit(uint32_t* bitmap, int index, int bitmap_size) {
    if (!bitmap || index < 0 || (index >> 5) >= bitmap_size) {
        return;
    }
    bitmap[index >> 5] |= (1U << (index & 31));
}

void ws_server_clear_client_bit(uint32_t* bitmap, int index, int bitmap_size) {
    if (!bitmap || index < 0 || (index >> 5) >= bitmap_size) {
        return;
    }
    bitmap[index >> 5] &= ~(1U << (index & 31));
}

bool ws_server_test_client_bit(uint32_t* bitmap, int index, int bitmap_size) {
    if (!bitmap || index < 0 || (index >> 5) >= bitmap_size) {
        return false;
    }
    return (bitmap[index >> 5] & (1U << (index & 31))) != 0;
}

// Find first free client slot using bitmap with optimized bit scanning
int ws_server_find_free_client_slot(ws_server_data_t* data) {
    if (!data || !data->client_bitmap) {
        return -1;
    }

    // Quick check if we're already at max capacity
    if (data->active_clients >= data->max_clients) {
        return -1;
    }

    // Use bitmap to find first free slot
    const uint32_t num_words = data->bitmap_size;

    for (uint32_t i = 0; i < num_words; i++) {
        uint32_t word = data->client_bitmap[i];

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

// Mutex helper functions for client access
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
