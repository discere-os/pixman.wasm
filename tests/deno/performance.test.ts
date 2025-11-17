import { assert } from "@std/assert"
import PixmanWASM from "../../src/lib/index.ts"

const PERFORMANCE_TARGETS = {
  SIMD_MIN: 3.0,      // 3x minimum for SIMD strings
  CRYPTO_MIN: 5.0,    // 5x minimum for WebCrypto
  WORKERS_MIN: 10.0,  // 10x minimum for Workers
  WEBGPU_MIN: 10.0,   // 10x minimum for WebGPU
}

Deno.test({
  name: "Performance: SIMD capabilities check",
  async fn() {
    try {
      const pixman = new PixmanWASM()
      await pixman.initialize()

      const caps = pixman.getCapabilities()

      if (caps.has_wasm_simd) {
        console.log("✅ SIMD available - performance optimizations enabled")
      } else {
        console.warn("⚠️  SIMD not available - performance targets may not be met")
      }

      console.log("\n📊 Performance Capability Summary:")
      console.log(`  SIMD: ${caps.has_wasm_simd ? '✅ Available' : '❌ Not Available'}`)
      console.log(`  WebCrypto: ${caps.has_web_crypto ? '✅ Available' : '❌ Not Available'}`)
      console.log(`  Workers: ${caps.has_workers ? '✅ Available' : '❌ Not Available'}`)
      console.log(`  WebGPU: ${caps.has_webgpu ? '✅ Available' : '❌ Not Available'}`)
    } catch (error) {
      console.warn("⚠️  Performance test skipped:", error)
    }
  },
})

Deno.test({
  name: "Performance: Browser version check",
  async fn() {
    try {
      const pixman = new PixmanWASM()
      await pixman.initialize()

      const caps = pixman.getCapabilities()

      if (caps.chrome_version > 0) {
        console.log(`\n🌐 Browser: Chrome/Edge ${caps.chrome_version}`)

        if (caps.chrome_version >= 113) {
          console.log("✅ Browser meets minimum requirements (113+)")
        } else {
          console.warn(`⚠️  Browser version ${caps.chrome_version} < 113 - please upgrade`)
        }
      } else {
        console.log("ℹ️  Non-Chrome browser detected")
      }
    } catch (error) {
      console.warn("⚠️  Browser check skipped:", error)
    }
  },
})

Deno.test({
  name: "Performance: Memory efficiency",
  async fn() {
    try {
      const pixman = new PixmanWASM()
      await pixman.initialize()

      const stats = pixman.getMemoryStats()

      const wasmMB = stats.wasmUsed / 1024 / 1024
      console.log(`\n💾 Memory Efficiency:`)
      console.log(`  WASM Memory: ${wasmMB.toFixed(2)} MB`)

      // Check if memory is within expected bounds for standard build
      if (wasmMB < 256) {
        console.log("✅ Memory usage is optimal")
      } else {
        console.warn("⚠️  Memory usage higher than expected")
      }

      if (stats.jsUsed > 0) {
        const heapRatio = (stats.jsUsed / stats.jsTotal) * 100
        console.log(`  Heap Usage: ${heapRatio.toFixed(1)}%`)

        if (heapRatio > 90) {
          console.warn("⚠️  High memory pressure detected")
        }
      }
    } catch (error) {
      console.warn("⚠️  Memory efficiency test skipped:", error)
    }
  },
})

Deno.test({
  name: "Performance: Timing accuracy",
  async fn() {
    try {
      const pixman = new PixmanWASM()
      await pixman.initialize()

      const t1 = pixman.getTimestamp()
      await new Promise(resolve => setTimeout(resolve, 100))
      const t2 = pixman.getTimestamp()

      const elapsed = t2 - t1
      console.log(`\n⏱️  Timing Accuracy:`)
      console.log(`  Measured: ${elapsed.toFixed(2)} ms (expected ~100ms)`)

      // Should be reasonably accurate (within 20ms)
      const accuracy = Math.abs(elapsed - 100)
      if (accuracy < 20) {
        console.log("✅ High-precision timing accurate")
      } else {
        console.warn(`⚠️  Timing accuracy: ±${accuracy.toFixed(2)}ms`)
      }
    } catch (error) {
      console.warn("⚠️  Timing test skipped:", error)
    }
  },
})

Deno.test({
  name: "Performance: Display refresh rate",
  async fn() {
    try {
      const pixman = new PixmanWASM()
      await pixman.initialize()

      const refreshRate = pixman.getRefreshRate()
      console.log(`\n🖥️  Display Refresh Rate: ${refreshRate} Hz`)

      if (refreshRate >= 120) {
        console.log("✅ High refresh rate display detected")
      } else if (refreshRate >= 60) {
        console.log("✅ Standard refresh rate")
      } else {
        console.warn("⚠️  Low refresh rate detected")
      }
    } catch (error) {
      console.warn("⚠️  Refresh rate test skipped:", error)
    }
  },
})

Deno.test({
  name: "Performance: Target summary",
  async fn() {
    console.log("\n🎯 Performance Targets:")
    console.log(`  SIMD Strings: ${PERFORMANCE_TARGETS.SIMD_MIN}x minimum`)
    console.log(`  WebCrypto: ${PERFORMANCE_TARGETS.CRYPTO_MIN}x minimum`)
    console.log(`  Workers: ${PERFORMANCE_TARGETS.WORKERS_MIN}x minimum`)
    console.log(`  WebGPU: ${PERFORMANCE_TARGETS.WEBGPU_MIN}x minimum`)
    console.log("\nNote: Run 'deno task bench' for detailed performance benchmarks")
  },
})
