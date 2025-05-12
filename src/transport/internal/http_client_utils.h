#ifndef HTTP_CLIENT_UTILS_H
#define HTTP_CLIENT_UTILS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

uint64_t extract_request_id(const char* json_data, size_t size);

#ifdef __cplusplus
}
#endif

#endif // HTTP_CLIENT_UTILS_H
