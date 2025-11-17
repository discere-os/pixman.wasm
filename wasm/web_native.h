/**
 * Web-Native Capabilities Header
 * Forward declarations for all web-native functions
 */

#ifndef WEB_NATIVE_H
#define WEB_NATIVE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

// Capabilities detection
typedef struct {
    bool has_wasm_simd;
    bool has_webgpu;
    bool has_shared_array_buffer;
    bool has_web_crypto;
    bool has_opfs;
    bool has_workers;
    int chrome_version;
} WebCapabilities;

const WebCapabilities* web_get_capabilities(void);
bool web_has_simd(void);
bool web_has_webgpu(void);
bool web_has_crypto(void);
int web_get_chrome_version(void);

// SIMD string operations
size_t web_simd_strlen(const char* str);
int web_simd_memcmp(const void* s1, const void* s2, size_t n);
void* web_simd_memcpy(void* dest, const void* src, size_t n);
void* web_simd_memset(void* s, int c, size_t n);

// Crypto operations
void web_crypto_random_bytes(uint8_t* buffer, size_t length);
void web_crypto_sha256_async(const uint8_t* data, size_t len, uint8_t* hash, void (*callback)(void));

// Threading
typedef void (*worker_func_t)(void*);
void web_spawn_worker(worker_func_t func, void* data);
bool web_has_threading(void);

// Networking
typedef void (*fetch_callback_t)(const char* data, size_t length);
void web_fetch_get(const char* url, fetch_callback_t callback);
void web_fetch_post(const char* url, const char* data, fetch_callback_t callback);
bool web_is_online(void);

// Filesystem (OPFS)
void* web_storage_read_opfs(const char* path, size_t* out_size);
bool web_storage_write_opfs(const char* path, const void* data, size_t size);
bool web_storage_delete_opfs(const char* path);

// Memory management
void web_register_weak_ref(void* ptr, void (*finalizer)(void*));
void* web_malloc_gc(size_t size, void (*finalizer)(void*));
void* web_malloc_auto(size_t size);
bool web_has_weakref(void);
void web_get_memory_stats(size_t* js_used, size_t* js_total, size_t* wasm_used);

// Main loop
typedef void (*mainloop_callback_t)(double timestamp);
void web_mainloop_start(mainloop_callback_t callback);
void web_mainloop_stop(void);
bool web_mainloop_is_running(void);
double web_get_timestamp(void);
int web_get_refresh_rate(void);
bool web_is_page_visible(void);

#endif /* WEB_NATIVE_H */
