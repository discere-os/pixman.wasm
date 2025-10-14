/**
 * Pixman WASM Benchmarks
 */

import PixmanWASM from "../src/lib/index.ts"

Deno.bench("pixman initialization", {
  baseline: true
}, async () => {
  const lib = new PixmanWASM()
  await lib.initialize()
})
