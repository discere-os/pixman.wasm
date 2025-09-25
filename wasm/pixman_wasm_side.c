#include <pixman.h>

const char* pixman_wasm_version(void) {
  return pixman_version_string();
}

