#pragma once

#include <stddef.h>

#if defined(_WIN32)
#if defined(SENTINEL_RUNTIME_CONTRACT_EXPORTS)
#define SENTINEL_RUNTIME_API __declspec(dllexport)
#else
#define SENTINEL_RUNTIME_API __declspec(dllimport)
#endif
#else
#define SENTINEL_RUNTIME_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Returns 0 on success, non-zero error code on failure.
SENTINEL_RUNTIME_API int sentinel_runtime_initialize(void);

// Returns 0 on success, non-zero error code on failure.
SENTINEL_RUNTIME_API int sentinel_runtime_shutdown(void);

// Returns 1 if ready, 0 if not ready.
SENTINEL_RUNTIME_API int sentinel_runtime_is_ready(void);

// Writes the runtime version into buffer as a null-terminated string.
// Returns required characters excluding null terminator.
SENTINEL_RUNTIME_API int sentinel_runtime_get_version(char* buffer, size_t buffer_size);

#ifdef __cplusplus
}
#endif
