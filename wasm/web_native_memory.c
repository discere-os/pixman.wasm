/**
 * Web-Native Memory Management with WeakRef
 *
 * Integrates WASM memory with browser GC:
 * - WeakRef for automatic cleanup
 * - FinalizationRegistry for resource cleanup
 * - Reduces memory leaks in long-running applications
 */

#include <emscripten.h>
#include <stdlib.h>
#include <stdbool.h>

// Register a pointer with WeakRef for GC integration
EM_JS(void, register_weak_ref_impl, (void* ptr, void (*finalizer)(void*)), {
    if (typeof WeakRef === 'undefined' || typeof FinalizationRegistry === 'undefined') {
        return;
    }

    // Create global registry if not exists
    if (!Module._finalizationRegistry) {
        Module._finalizationRegistry = new FinalizationRegistry(heldValue => {
            if (heldValue.finalizer) {
                dynCall('vi', heldValue.finalizer, [heldValue.ptr]);
            }
        });
    }

    // Register the pointer with finalizer
    const ref = { ptr: ptr, finalizer: finalizer };
    Module._finalizationRegistry.register(ref, ref);
});

EMSCRIPTEN_KEEPALIVE
void web_register_weak_ref(void* ptr, void (*finalizer)(void*)) {
    register_weak_ref_impl(ptr, finalizer);
}

// Allocate memory with automatic GC integration
EMSCRIPTEN_KEEPALIVE
void* web_malloc_gc(size_t size, void (*finalizer)(void*)) {
    void* ptr = malloc(size);
    if (ptr && finalizer) {
        web_register_weak_ref(ptr, finalizer);
    }
    return ptr;
}

// Default finalizer that just calls free
static void default_finalizer(void* ptr) {
    free(ptr);
}

EMSCRIPTEN_KEEPALIVE
void* web_malloc_auto(size_t size) {
    return web_malloc_gc(size, default_finalizer);
}

// Check if WeakRef is available
EMSCRIPTEN_KEEPALIVE
bool web_has_weakref(void) {
    return EM_ASM_INT({
        return typeof WeakRef !== 'undefined' &&
               typeof FinalizationRegistry !== 'undefined';
    });
}

// Memory pressure hint to browser
EMSCRIPTEN_KEEPALIVE
void web_hint_memory_pressure(void) {
    EM_ASM({
        // Hint browser that GC might be beneficial
        if (typeof performance !== 'undefined' && performance.memory) {
            const used = performance.memory.usedJSHeapSize;
            const total = performance.memory.totalJSHeapSize;
            if (used / total > 0.9) {
                console.warn('High memory pressure:', used, '/', total);
            }
        }
    });
}

// Get memory usage statistics
EMSCRIPTEN_KEEPALIVE
void web_get_memory_stats(size_t* js_used, size_t* js_total, size_t* wasm_used) {
    EM_ASM({
        if (typeof performance !== 'undefined' && performance.memory) {
            setValue($0, performance.memory.usedJSHeapSize, 'i32');
            setValue($1, performance.memory.totalJSHeapSize, 'i32');
        }
        // WASM memory size
        setValue($2, HEAP8.length, 'i32');
    }, js_used, js_total, wasm_used);
}
