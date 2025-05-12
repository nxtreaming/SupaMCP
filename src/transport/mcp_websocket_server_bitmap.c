#include "internal/websocket_server_internal.h"

// Optimized bitmap helper functions for client slot management
void ws_server_set_client_bit(uint32_t* bitmap, int index) {
    bitmap[index >> 5] |= (1U << (index & 31));
}

void ws_server_clear_client_bit(uint32_t* bitmap, int index) {
    bitmap[index >> 5] &= ~(1U << (index & 31));
}

bool ws_server_test_client_bit(uint32_t* bitmap, int index) {
    return (bitmap[index >> 5] & (1U << (index & 31))) != 0;
}

// Find first free client slot using bitmap with optimized bit scanning
int ws_server_find_free_client_slot(ws_server_data_t* data) {
    // Quick check if we're already at max capacity
    if (data->active_clients >= MAX_WEBSOCKET_CLIENTS) {
        return -1;
    }

    // Use bitmap to find first free slot
    const int num_words = (MAX_WEBSOCKET_CLIENTS >> 5) + 1;

    for (int i = 0; i < num_words; i++) {
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

        int index = (i << 5) + j;
        if (index < MAX_WEBSOCKET_CLIENTS) {
            return index;
        }
    }

    return -1; // No free slots found
}
