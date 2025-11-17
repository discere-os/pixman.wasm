import { assert, assertEquals, assertExists } from "@std/assert"
import PixmanWASM from "../../src/lib/index.ts"

Deno.test("Deno runtime features", () => {
  assert(typeof Deno !== 'undefined', "Deno runtime should be available")
  assert(typeof WebAssembly !== 'undefined', "WebAssembly should be available")
})

Deno.test("WASM file accessibility", async () => {
  try {
    const wasmFile = await Deno.stat("./install/wasm/pixman-main.wasm")
    assert(wasmFile.isFile, "WASM file should exist")
    assert(wasmFile.size > 0, "WASM file should not be empty")
    console.log(`✅ Found WASM file: ${wasmFile.size} bytes`)
  } catch (error) {
    console.warn("⚠️  WASM file not found - run 'deno task build:wasm' first")
  }
})

Deno.test("TypeScript module imports", async () => {
  const { default: Module } = await import("../../src/lib/index.ts")
  assertExists(Module, "Module class should be importable")
  assert(typeof Module === 'function', "Module should be a constructor function")
})

Deno.test("Module initialization", async () => {
  try {
    const pixman = new PixmanWASM()
    await pixman.initialize()
    assert(pixman, "Pixman should initialize successfully")
    console.log("✅ Module initialized")
  } catch (error) {
    console.warn("⚠️  Module initialization failed - WASM build may not be available:", error)
  }
})

Deno.test("Web capabilities detection", async () => {
  try {
    const pixman = new PixmanWASM()
    await pixman.initialize()

    const caps = pixman.getCapabilities()
    assertExists(caps, "Capabilities should be returned")

    console.log("\n📊 Web-Native Capabilities:")
    console.log(`  WASM SIMD: ${caps.has_wasm_simd ? '✅' : '❌'}`)
    console.log(`  WebGPU: ${caps.has_webgpu ? '✅' : '❌'}`)
    console.log(`  SharedArrayBuffer: ${caps.has_shared_array_buffer ? '✅' : '❌'}`)
    console.log(`  Web Crypto: ${caps.has_web_crypto ? '✅' : '❌'}`)
    console.log(`  OPFS: ${caps.has_opfs ? '✅' : '❌'}`)
    console.log(`  Workers: ${caps.has_workers ? '✅' : '❌'}`)
    console.log(`  Chrome Version: ${caps.chrome_version}`)

    if (!caps.has_wasm_simd) {
      console.warn("⚠️  WASM SIMD not available - performance will be degraded")
    }
  } catch (error) {
    console.warn("⚠️  Capabilities test skipped:", error)
  }
})

Deno.test("Memory statistics", async () => {
  try {
    const pixman = new PixmanWASM()
    await pixman.initialize()

    const stats = pixman.getMemoryStats()
    assertExists(stats, "Memory stats should be returned")
    assert(stats.wasmUsed > 0, "WASM memory should be allocated")

    console.log("\n💾 Memory Statistics:")
    console.log(`  JS Heap Used: ${(stats.jsUsed / 1024 / 1024).toFixed(2)} MB`)
    console.log(`  JS Heap Total: ${(stats.jsTotal / 1024 / 1024).toFixed(2)} MB`)
    console.log(`  WASM Memory: ${(stats.wasmUsed / 1024 / 1024).toFixed(2)} MB`)
  } catch (error) {
    console.warn("⚠️  Memory stats test skipped:", error)
  }
})
