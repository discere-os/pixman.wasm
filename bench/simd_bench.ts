#!/usr/bin/env deno run --allow-read
/**
 * SIMD Performance Benchmark
 *
 * Measures actual performance gains from web-native optimizations.
 * Target: 3-5x speedup for SIMD operations
 */

import PixmanWASM from "../src/lib/index.ts"

console.log("🚀 Pixman WASM - SIMD Performance Benchmark")
console.log("=".repeat(70))

// Initialize library
const pixman = new PixmanWASM()

try {
  await pixman.initialize()
} catch (error) {
  console.error("❌ Failed to initialize:", error)
  console.error("Build the WASM module first with: deno task build:wasm")
  Deno.exit(1)
}

// Check capabilities
const caps = pixman.getCapabilities()

console.log("\n📊 System Capabilities:")
console.log(`  WASM SIMD: ${caps.has_wasm_simd ? '✅' : '❌'}`)
console.log(`  WebGPU: ${caps.has_webgpu ? '✅' : '❌'}`)
console.log(`  Web Crypto: ${caps.has_web_crypto ? '✅' : '❌'}`)
console.log(`  Workers: ${caps.has_workers ? '✅' : '❌'}`)
console.log(`  Browser: Chrome ${caps.chrome_version}`)

if (!caps.has_wasm_simd) {
  console.warn("\n⚠️  WASM SIMD not available - performance will be degraded")
}

// Helper to run benchmark
async function bench(name: string, fn: () => void, iterations: number = 10000): Promise<number> {
  // Warmup
  for (let i = 0; i < 100; i++) fn()

  // Measure
  const start = pixman.getTimestamp()
  for (let i = 0; i < iterations; i++) {
    fn()
  }
  const end = pixman.getTimestamp()

  const totalTime = end - start
  const avgTime = totalTime / iterations

  return avgTime
}

// Memory Statistics Benchmark
console.log("\n💾 Memory Statistics:")
const stats = pixman.getMemoryStats()
console.log(`  WASM Memory: ${(stats.wasmUsed / 1024 / 1024).toFixed(2)} MB`)
console.log(`  JS Heap Used: ${(stats.jsUsed / 1024 / 1024).toFixed(2)} MB`)
console.log(`  JS Heap Total: ${(stats.jsTotal / 1024 / 1024).toFixed(2)} MB`)

// Timing Benchmark
console.log("\n⏱️  High-Precision Timing:")
const t1 = pixman.getTimestamp()
await new Promise(resolve => setTimeout(resolve, 50))
const t2 = pixman.getTimestamp()
const elapsed = t2 - t1
console.log(`  Sleep(50ms): ${elapsed.toFixed(2)}ms (accuracy: ±${Math.abs(elapsed - 50).toFixed(2)}ms)`)

// Refresh Rate
const refreshRate = pixman.getRefreshRate()
console.log(`  Display: ${refreshRate}Hz`)

// Note: Actual SIMD benchmarks would require the WASM module to be built
// and include SIMD-optimized functions. This is a placeholder showing
// the benchmarking infrastructure.

console.log("\n🎯 Performance Targets:")
console.log("  SIMD Strings: 3-5x speedup (requires WASM build)")
console.log("  WebCrypto: 5-15x speedup (requires WASM build)")
console.log("  Workers: 10x speedup (requires WASM build)")
console.log("  WebGPU: 10x+ speedup (requires WebGPU support)")

console.log("\n✅ Benchmark complete!")
console.log("\nNote: Full SIMD benchmarks require building the WASM module.")
console.log("Run: deno task build:wasm")
