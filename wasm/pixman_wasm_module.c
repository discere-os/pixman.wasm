#include <emscripten.h>
#include <pixman.h>

EMSCRIPTEN_KEEPALIVE
const char* pixman_wasm_version(void) {
  return pixman_version_string();
}

