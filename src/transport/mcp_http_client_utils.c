#ifdef _WIN32
#   ifndef _CRT_SECURE_NO_WARNINGS
#       define _CRT_SECURE_NO_WARNINGS
#   endif
#endif

#include "internal/http_client_utils.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief Extract request ID from JSON-RPC request.
 *
 * This function extracts the request ID from a JSON-RPC request string.
 * It uses a simple string search approach rather than full JSON parsing.
 */
uint64_t extract_request_id(const char* json_data, size_t size) {
    (void)size; // Size is not used in this simple implementation
    // Look for "id":
    const char* id_str = "\"id\":";
    char* id_pos = strstr(json_data, id_str);
    if (id_pos == NULL) {
        return 0; // ID not found
    }

    // Skip "id": and any whitespace
    id_pos += strlen(id_str);
    while (*id_pos == ' ' || *id_pos == '\t' || *id_pos == '\r' || *id_pos == '\n') {
        id_pos++;
    }

    // Parse the ID value
    if (*id_pos == '"') {
        // String ID - not supported in this simple implementation
        return 0;
    } else {
        // Numeric ID
        return (uint64_t)atoll(id_pos);
    }
}
