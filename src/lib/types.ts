/**
 * Type definitions for Pixman WASM with Web-Native optimizations
 *
 * @module @discere-os/pixman.wasm/types
 */

/**
 * Configuration options for Pixman WASM library
 */
export interface PixmanConfig {
  /** Enable SIMD optimizations (3-5x speedup) */
  enableSIMD?: boolean
  /** Enable WebGPU acceleration (10x+ speedup) */
  enableWebGPU?: boolean
  /** Initial memory size in bytes (default: 128MB) */
  initialMemory?: number
  /** Maximum memory size in bytes (default: 1GB) */
  maximumMemory?: number
  /** Enable threading with Workers */
  enableThreading?: boolean
}

/**
 * Web-native capabilities detected at runtime
 */
export interface WebCapabilities {
  /** WASM SIMD support (3-5x speedup for vector operations) */
  has_wasm_simd: boolean
  /** WebGPU support (10x+ speedup for GPU operations) */
  has_webgpu: boolean
  /** SharedArrayBuffer support (required for threading) */
  has_shared_array_buffer: boolean
  /** Web Crypto API support (5-15x speedup for crypto) */
  has_web_crypto: boolean
  /** Origin Private File System support (3-4x faster than IDBFS) */
  has_opfs: boolean
  /** Web Workers support (10x faster than pthread emulation) */
  has_workers: boolean
  /** Chrome/Edge version number */
  chrome_version: number
}

/**
 * Performance metrics for web-native optimizations
 */
export interface PerformanceMetrics {
  /** SIMD speedup multiplier (target: 3-5x) */
  simdSpeedup: number
  /** WebGPU speedup multiplier (target: 10x+) */
  webgpuSpeedup: number
  /** Workers speedup multiplier (target: 10x) */
  workersSpeedup: number
  /** Crypto speedup multiplier (target: 5-15x) */
  cryptoSpeedup: number
  /** OPFS speedup multiplier (target: 3-4x) */
  opfsSpeedup: number
}

/**
 * Memory statistics
 */
export interface MemoryStats {
  /** JavaScript heap used (bytes) */
  jsUsed: number
  /** JavaScript heap total (bytes) */
  jsTotal: number
  /** WASM memory used (bytes) */
  wasmUsed: number
}

/**
 * Storage tier for filesystem operations
 */
export enum StorageTier {
  /** In-memory (fastest, temporary) */
  Memory = 0,
  /** OPFS (fast, persistent) */
  OPFS = 1,
  /** Browser cache (medium speed) */
  Cache = 2,
  /** Remote storage (slowest) */
  Remote = 3,
}

/**
 * Emscripten module interface
 */
export interface PIXMANModule {
  /** Call C function */
  ccall: (funcName: string, returnType: string, argTypes: string[], args: unknown[]) => unknown
  /** Wrap C function */
  cwrap: (funcName: string, returnType: string, argTypes: string[]) => Function
  /** Filesystem API */
  FS: unknown
  /** 8-bit unsigned heap */
  HEAPU8: Uint8Array
  /** Allocate memory */
  _malloc: (size: number) => number
  /** Free memory */
  _free: (ptr: number) => void
  /** Set value in memory */
  setValue: (ptr: number, value: number, type: string) => void
  /** Get value from memory */
  getValue: (ptr: number, type: string) => number
}

/**
 * Error class for Pixman WASM operations
 */
export class PixmanError extends Error {
  constructor(message: string) {
    super(message)
    this.name = 'PixmanError'
  }
}

/**
 * Callback type for fetch operations
 */
export type FetchCallback = (data: string | null, length: number) => void

/**
 * Callback type for main loop
 */
export type MainLoopCallback = (timestamp: number) => void
