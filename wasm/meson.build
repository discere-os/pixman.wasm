# Pixman WASM Build System - Unified Dual Architecture
# SIDE_MODULE: Production builds (70-200KB) for runtime dlopen()
# MAIN_MODULE: Testing/NPM builds (self-contained) for standalone use

# Conditional compilation flags based on build type
wasm_common_flags = [
  '-fPIC',
  '-sWASM_BIGINT=1',
]

wasm_simd_flags = get_option('wasm_simd') ? ['-msimd128', '-DPIXMAN_WASM_SIMD=1'] : []
wasm_threading_flags = get_option('wasm_threading') ? ['-pthread', '-sPTHREAD_POOL_SIZE=4'] : []

# Build type specific flags
if get_option('wasm_build_type') == 'minimal'
  wasm_type_flags = [
    '-sINITIAL_MEMORY=67108864',  # 64MB
    '-sSTACK_SIZE=2097152',       # 2MB
  ]
elif get_option('wasm_build_type') == 'webgpu'
  wasm_type_flags = wasm_simd_flags + wasm_threading_flags + [
    '-sINITIAL_MEMORY=134217728',  # 128MB
    '-sMAXIMUM_MEMORY=1073741824', # 1GB
    '-sUSE_WEBGPU=1',
    '-sASYNCIFY=1',
  ]
else  # standard
  wasm_type_flags = wasm_simd_flags + wasm_threading_flags + [
    '-sINITIAL_MEMORY=134217728',  # 128MB
    '-sMAXIMUM_MEMORY=1073741824', # 1GB
  ]
endif

# Optimization flags
if get_option('wasm_optimize') == 'size'
  wasm_opt_flags = ['-Oz', '-flto']
elif get_option('wasm_optimize') == 'speed'
  wasm_opt_flags = ['-O3', '-flto']
else  # balanced
  wasm_opt_flags = ['-O3', '-flto']
endif

# Combine all flags
pixman_wasm_cargs = wasm_common_flags + wasm_type_flags + wasm_opt_flags

# Web-native capability sources
web_native_sources = files(
  'web_native_capabilities.c',
  'web_native_simd_strings.c',
  'web_native_crypto.c',
  'web_native_threading.c',
  'web_native_networking.c',
  'web_native_filesystem.c',
  'web_native_memory.c',
  'web_native_mainloop.c',
)

# MAIN_MODULE target (testing, NPM distribution, standalone)
pixman_main_link_args = [
  '-sWASM=1',
  '-sMODULARIZE=1',
  '-sEXPORT_ES6=1',
  '-sEXPORT_NAME=PixmanModule',
  '-sALLOW_MEMORY_GROWTH=1',
  '-sENVIRONMENT=web,webview,worker',
  '-sNO_FILESYSTEM=1',
  '-sEXPORTED_RUNTIME_METHODS=["cwrap","ccall","UTF8ToString","HEAPU8"]',
  '-sEXPORTED_FUNCTIONS=["_web_get_capabilities","_web_simd_strlen","_web_simd_memcmp","_pixman_wasm_version"]',
] + wasm_opt_flags + wasm_type_flags

executable(
  'pixman-main',
  files('pixman_wasm_module.c') + web_native_sources,
  dependencies: [idep_pixman],
  c_args: pixman_wasm_cargs,
  link_args: pixman_main_link_args,
  install: true,
  install_dir: get_option('bindir'),
)

# SIDE_MODULE target (production, dynamic loading, minimal size)
pixman_side_link_args = [
  '-sSIDE_MODULE=2',
  '-fPIC',
  '-sERROR_ON_UNDEFINED_SYMBOLS=0',  # Host provides dependencies
  '-sEXPORTED_FUNCTIONS=["_pixman_wasm_version","_web_get_capabilities"]',
] + wasm_opt_flags + wasm_simd_flags

shared_module(
  'pixman-side',
  files('pixman_wasm_side.c') + web_native_sources,
  dependencies: [idep_pixman],
  name_prefix: '',
  name_suffix: 'wasm',
  c_args: pixman_wasm_cargs,
  link_args: pixman_side_link_args,
  install: true,
  install_dir: get_option('libdir'),
)
