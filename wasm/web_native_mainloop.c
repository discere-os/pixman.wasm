/**
 * Web-Native Main Loop with RequestAnimationFrame
 *
 * Provides RAF-based main loop for UI libraries:
 * - Synchronized with browser refresh (60/120Hz)
 * - Better battery life than setTimeout
 * - Automatic throttling when tab inactive
 *
 * Note: Pixman is a rendering library, so RAF integration
 * is beneficial for animation and compositing use cases.
 */

#include <emscripten.h>
#include <stdbool.h>

typedef void (*mainloop_callback_t)(double timestamp);

static mainloop_callback_t g_mainloop_callback = NULL;
static bool g_mainloop_running = false;

// Internal callback wrapper
EM_JS(void, setup_raf_loop_impl, (mainloop_callback_t callback), {
    if (typeof requestAnimationFrame === 'undefined') {
        console.warn('requestAnimationFrame not available');
        return;
    }

    Module._rafCallback = callback;
    Module._rafRunning = true;

    function loop(timestamp) {
        if (!Module._rafRunning) return;

        // Call C callback with timestamp
        dynCall('vd', Module._rafCallback, [timestamp]);

        // Schedule next frame
        requestAnimationFrame(loop);
    }

    requestAnimationFrame(loop);
});

// Start main loop with RAF
EMSCRIPTEN_KEEPALIVE
void web_mainloop_start(mainloop_callback_t callback) {
    if (g_mainloop_running) {
        return;
    }

    g_mainloop_callback = callback;
    g_mainloop_running = true;
    setup_raf_loop_impl(callback);
}

// Stop main loop
EMSCRIPTEN_KEEPALIVE
void web_mainloop_stop(void) {
    g_mainloop_running = false;
    EM_ASM({
        Module._rafRunning = false;
    });
}

// Check if main loop is running
EMSCRIPTEN_KEEPALIVE
bool web_mainloop_is_running(void) {
    return g_mainloop_running;
}

// Get current timestamp (high precision)
EMSCRIPTEN_KEEPALIVE
double web_get_timestamp(void) {
    return EM_ASM_DOUBLE({
        return performance.now();
    });
}

// Request single animation frame (one-shot)
EM_JS(void, web_request_animation_frame_impl, (mainloop_callback_t callback), {
    if (typeof requestAnimationFrame === 'undefined') {
        dynCall('vd', callback, [performance.now()]);
        return;
    }

    requestAnimationFrame(timestamp => {
        dynCall('vd', callback, [timestamp]);
    });
});

EMSCRIPTEN_KEEPALIVE
void web_request_animation_frame(mainloop_callback_t callback) {
    web_request_animation_frame_impl(callback);
}

// Get display refresh rate
EMSCRIPTEN_KEEPALIVE
int web_get_refresh_rate(void) {
    return EM_ASM_INT({
        // Try to detect refresh rate
        if (typeof screen !== 'undefined' && screen.refreshRate) {
            return screen.refreshRate;
        }
        // Default to 60Hz
        return 60;
    });
}

// Check if page is visible (for throttling)
EMSCRIPTEN_KEEPALIVE
bool web_is_page_visible(void) {
    return EM_ASM_INT({
        return typeof document !== 'undefined' &&
               document.visibilityState === 'visible';
    });
}
