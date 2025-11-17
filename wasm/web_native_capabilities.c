/**
 * Web-Native Capabilities Detection
 *
 * Detects browser capabilities for optimized code paths:
 * - WASM SIMD (3-5x speedup for vector operations)
 * - WebGPU (10x+ for GPU-accelerated compute)
 * - SharedArrayBuffer (required for threading)
 * - Web Crypto (5-15x for cryptographic operations)
 * - OPFS (3-4x faster than IDBFS)
 * - Web Workers (10x vs pthread emulation)
 */

#include <stdbool.h>
#include <emscripten.h>

typedef struct {
    bool has_wasm_simd;
    bool has_webgpu;
    bool has_shared_array_buffer;
    bool has_web_crypto;
    bool has_opfs;
    bool has_workers;
    int chrome_version;
} WebCapabilities;

static WebCapabilities g_caps = {0};
static bool g_initialized = false;

EMSCRIPTEN_KEEPALIVE
const WebCapabilities* web_get_capabilities(void) {
    if (!g_initialized) {
        // Detect WASM SIMD support
        g_caps.has_wasm_simd = EM_ASM_INT({
            try {
                return typeof WebAssembly !== 'undefined' &&
                       typeof WebAssembly.validate === 'function' &&
                       WebAssembly.validate(new Uint8Array([
                           0,97,115,109,1,0,0,0,1,4,1,96,0,0,3,2,1,0,
                           10,9,1,7,0,65,0,253,15,26,11
                       ]));
            } catch (e) {
                return false;
            }
        });

        // Detect WebGPU support
        g_caps.has_webgpu = EM_ASM_INT({
            return typeof navigator !== 'undefined' &&
                   typeof navigator.gpu !== 'undefined';
        });

        // Detect SharedArrayBuffer support
        g_caps.has_shared_array_buffer = EM_ASM_INT({
            return typeof SharedArrayBuffer !== 'undefined';
        });

        // Detect Web Crypto support
        g_caps.has_web_crypto = EM_ASM_INT({
            return typeof crypto !== 'undefined' &&
                   typeof crypto.subtle !== 'undefined';
        });

        // Detect OPFS support
        g_caps.has_opfs = EM_ASM_INT({
            return typeof navigator !== 'undefined' &&
                   typeof navigator.storage !== 'undefined' &&
                   typeof navigator.storage.getDirectory === 'function';
        });

        // Detect Web Workers support
        g_caps.has_workers = EM_ASM_INT({
            return typeof Worker !== 'undefined';
        });

        // Detect Chrome version
        g_caps.chrome_version = EM_ASM_INT({
            if (typeof navigator === 'undefined') return 0;
            var match = navigator.userAgent.match(/Chrome\/(\d+)/);
            return match ? parseInt(match[1]) : 0;
        });

        g_initialized = true;
    }
    return &g_caps;
}

EMSCRIPTEN_KEEPALIVE
bool web_has_simd(void) {
    return web_get_capabilities()->has_wasm_simd;
}

EMSCRIPTEN_KEEPALIVE
bool web_has_webgpu(void) {
    return web_get_capabilities()->has_webgpu;
}

EMSCRIPTEN_KEEPALIVE
bool web_has_crypto(void) {
    return web_get_capabilities()->has_web_crypto;
}

EMSCRIPTEN_KEEPALIVE
int web_get_chrome_version(void) {
    return web_get_capabilities()->chrome_version;
}
