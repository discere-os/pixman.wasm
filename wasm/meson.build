pixman_wasm_cargs = ['-msimd128', '-DPIXMAN_WASM=1']

pixman_main_link_args = [
  '-sWASM=1',
  '-sMODULARIZE=1',
  '-sEXPORT_ES6=1',
  '-sEXPORT_NAME=PixmanModule',
  '-sALLOW_MEMORY_GROWTH=1',
  '-sENVIRONMENT=web,webview,worker',
  '-sNO_FILESYSTEM=1',
  '-sEXPORTED_RUNTIME_METHODS=["cwrap","ccall","UTF8ToString"]',
  '-O3',
  '-flto',
]

executable(
  'pixman-main',
  files('pixman_wasm_module.c'),
  dependencies: [idep_pixman],
  c_args: pixman_wasm_cargs,
  link_args: pixman_main_link_args,
  install: true,
  install_dir: get_option('bindir'),
)

pixman_side_link_args = [
  '-sSIDE_MODULE=2',
  '-fPIC',
  '-O3',
  '-flto',
  '-sEXPORTED_FUNCTIONS=["_pixman_wasm_version"]',
]

shared_module(
  'pixman-side',
  files('pixman_wasm_side.c'),
  dependencies: [idep_pixman],
  name_prefix: '',
  c_args: pixman_wasm_cargs,
  link_args: pixman_side_link_args,
  install: true,
  install_dir: get_option('libdir'),
)

