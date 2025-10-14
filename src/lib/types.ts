/**
 * Type definitions for Pixman WASM
 */

export interface PIXMANModule {
  _malloc: (size: number) => number
  _free: (ptr: number) => void
  HEAPU8: Uint8Array
  setValue: (ptr: number, value: number, type: string) => void
  getValue: (ptr: number, type: string) => number
}

export class PIXMANError extends Error {
  constructor(message: string) {
    super(message)
    this.name = 'PIXMANError'
  }
}
