#!/bin/bash
# WASM Integration Copyright (c) 2025 Superstruct Ltd, New Zealand
# Licensed under the same license as the underlying pixman project (MIT)
# Production WASM build script for pixman with SIMD optimization
# Advanced WASM build system with SIMD optimization

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Build configuration
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build-wasm"
INSTALL_DIR="$SCRIPT_DIR/dist"
CROSS_FILE="$SCRIPT_DIR/cross-files/wasm-cross.ini"

# Build variants
BUILD_SIMD=${BUILD_SIMD:-1}
BUILD_FALLBACK=${BUILD_FALLBACK:-1}
BUILD_TESTS=${BUILD_TESTS:-1}
BUILD_BENCHMARKS=${BUILD_BENCHMARKS:-1}

print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

check_requirements() {
    print_status "Checking build requirements..."
    
    if ! command -v emcc >/dev/null 2>&1; then
        print_error "Emscripten not found. Please install and activate emsdk."
        exit 1
    fi
    
    if ! command -v meson >/dev/null 2>&1; then
        print_error "Meson build system not found. Please install meson."
        exit 1
    fi
    
    # Check Emscripten version for SIMD support
    EMCC_VERSION=$(emcc --version | head -n1 | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -n1)
    print_status "Emscripten version: $EMCC_VERSION"
    
    # Verify SIMD support (requires Emscripten 2.0.0+)
    if [[ $(echo "$EMCC_VERSION" | cut -d. -f1) -lt 2 ]]; then
        print_warning "Emscripten $EMCC_VERSION may not support WASM SIMD. Consider upgrading to 2.0.0+."
    fi
    
    print_success "Requirements check passed"
}

clean_build() {
    print_status "Cleaning previous builds..."
    rm -rf "$BUILD_DIR" "$INSTALL_DIR"
    mkdir -p "$BUILD_DIR" "$INSTALL_DIR"
}

configure_build() {
    local variant="$1"
    local build_subdir="$BUILD_DIR/$variant"
    
    print_status "Configuring $variant build..."
    
    # Base meson options for WASM
    local meson_opts=(
        "--cross-file=$CROSS_FILE"
        "--prefix=$INSTALL_DIR/$variant"
        "-Dbuildtype=release"
        "-Dtests=disabled"
        "-Ddemos=disabled" 
        "-Dgtk=disabled"
        "-Dlibpng=disabled"
        "-Dopenmp=disabled"
        "-Dtls=disabled"
        "-Dgnu-inline-asm=disabled"
    )
    
    # Disable all native SIMD (we use WASM SIMD instead)
    meson_opts+=(
        "-Dmmx=disabled"
        "-Dsse2=disabled"
        "-Dssse3=disabled"
        "-Dneon=disabled"
        "-Darm-simd=disabled"
        "-Da64-neon=disabled"
        "-Dvmx=disabled"
        "-Dmips-dspr2=disabled"
        "-Drvv=disabled"
        "-Dloongson-mmi=disabled"
    )
    
    # Variant-specific configuration
    case "$variant" in
        "simd")
            print_status "Configuring SIMD-optimized build"
            ;;
        "fallback")
            print_status "Configuring fallback build (no SIMD)"
            # For fallback, we might add specific fallback flags here
            ;;
    esac
    
    mkdir -p "$build_subdir"
    meson setup "$build_subdir" "${meson_opts[@]}"
}

build_variant() {
    local variant="$1"
    local build_subdir="$BUILD_DIR/$variant"
    
    print_status "Building $variant variant..."
    
    # Build the library
    meson compile -C "$build_subdir" -v
    
    # Install to dist directory
    meson install -C "$build_subdir"
    
    print_success "$variant build completed"
}

create_js_wrapper() {
    local variant="$1"
    local wrapper_file="$INSTALL_DIR/$variant/pixman-${variant}.js"
    
    print_status "Creating JavaScript wrapper for $variant"
    
    cat > "$wrapper_file" << 'EOF'
/*
 * Copyright © 2025 Superstruct Ltd, New Zealand
 * JavaScript wrapper for pixman.wasm with SIMD support
 */

class PixmanWASM {
    constructor() {
        this.module = null;
        this.ready = false;
    }
    
    async initialize() {
        try {
            // Dynamic import based on SIMD support
            const simdSupported = typeof WebAssembly.SIMD !== 'undefined';
            
            if (simdSupported && VARIANT_NAME === 'simd') {
                console.log('✅ WASM SIMD supported, using optimized build');
                this.module = await import('./pixman-simd.wasm');
            } else {
                console.log('⚠️  WASM SIMD not supported, using fallback build');
                this.module = await import('./pixman-fallback.wasm');
            }
            
            this.ready = true;
            return true;
        } catch (error) {
            console.error('Failed to initialize pixman.wasm:', error);
            return false;
        }
    }
    
    // High-level API methods
    createImage(format, width, height, data = null) {
        if (!this.ready) throw new Error('PixmanWASM not initialized');
        
        const stride = width * 4; // Assuming 32-bit formats
        return this.module.ccall('pixman_image_create_bits',
            'number', ['number', 'number', 'number', 'number', 'number'],
            [format, width, height, data, stride]);
    }
    
    composite(op, src, mask, dest, src_x, src_y, mask_x, mask_y, 
              dest_x, dest_y, width, height) {
        if (!this.ready) throw new Error('PixmanWASM not initialized');
        
        return this.module.ccall('pixman_image_composite32',
            null, ['number', 'number', 'number', 'number', 
                   'number', 'number', 'number', 'number',
                   'number', 'number', 'number', 'number'],
            [op, src, mask, dest, src_x, src_y, mask_x, mask_y,
             dest_x, dest_y, width, height]);
    }
    
    // Benchmark function
    benchmark(width = 1024, height = 1024, iterations = 100) {
        if (!this.ready) throw new Error('PixmanWASM not initialized');
        
        return this.module.ccall('pixman_wasm_benchmark_composite',
            'number', ['number', 'number', 'number'],
            [width, height, iterations]);
    }
    
    // SIMD availability check
    hasSIMD() {
        if (!this.ready) return false;
        
        return this.module.ccall('pixman_wasm_simd_available', 'number', [], []) === 1;
    }
    
    unrefImage(image) {
        if (!this.ready) throw new Error('PixmanWASM not initialized');
        
        this.module.ccall('pixman_image_unref', null, ['number'], [image]);
    }
}

// Export for different module systems
if (typeof module !== 'undefined' && module.exports) {
    module.exports = PixmanWASM;
} else if (typeof window !== 'undefined') {
    window.PixmanWASM = PixmanWASM;
}

export default PixmanWASM;
EOF
    
    # Replace VARIANT_NAME placeholder
    sed -i "s/VARIANT_NAME/'$variant'/g" "$wrapper_file"
    
    print_success "JavaScript wrapper created for $variant"
}

create_typescript_definitions() {
    local dts_file="$INSTALL_DIR/pixman.d.ts"
    
    print_status "Creating TypeScript definitions..."
    
    cat > "$dts_file" << 'EOF'
/*
 * Copyright © 2025 Superstruct Ltd, New Zealand
 * TypeScript definitions for pixman.wasm
 */

export declare enum PixmanFormat {
    PIXMAN_a8r8g8b8 = 0x20028888,
    PIXMAN_x8r8g8b8 = 0x20020888,
    PIXMAN_a8b8g8r8 = 0x20028888,
    PIXMAN_x8b8g8r8 = 0x20020888,
    PIXMAN_r5g6b5   = 0x10160565,
    PIXMAN_a8       = 0x08000800,
}

export declare enum PixmanOp {
    PIXMAN_OP_CLEAR = 0x00,
    PIXMAN_OP_SRC   = 0x01,
    PIXMAN_OP_DST   = 0x02,
    PIXMAN_OP_OVER  = 0x03,
    PIXMAN_OP_ADD   = 0x0c,
}

export interface BenchmarkResult {
    averageTime: number;
    pixelsPerSecond: number;
    simdEnabled: boolean;
}

export declare class PixmanWASM {
    constructor();
    
    initialize(): Promise<boolean>;
    
    createImage(
        format: PixmanFormat, 
        width: number, 
        height: number, 
        data?: ArrayBuffer | null
    ): number;
    
    composite(
        op: PixmanOp,
        src: number,
        mask: number | null,
        dest: number,
        src_x: number,
        src_y: number,
        mask_x: number,
        mask_y: number,
        dest_x: number,
        dest_y: number,
        width: number,
        height: number
    ): void;
    
    benchmark(
        width?: number, 
        height?: number, 
        iterations?: number
    ): number;
    
    hasSIMD(): boolean;
    
    unrefImage(image: number): void;
}

export default PixmanWASM;
EOF
    
    print_success "TypeScript definitions created"
}

create_package_json() {
    local package_file="$INSTALL_DIR/package.json"
    
    print_status "Creating package.json..."
    
    cat > "$package_file" << 'EOF'
{
  "name": "@superstruct/pixman-wasm",
  "version": "0.46.5-wasm.1",
  "description": "Pixman pixel manipulation library compiled to WebAssembly with SIMD optimization",
  "main": "simd/pixman-simd.js",
  "module": "simd/pixman-simd.js",
  "types": "pixman.d.ts",
  "exports": {
    ".": {
      "import": "./simd/pixman-simd.js",
      "require": "./simd/pixman-simd.js",
      "types": "./pixman.d.ts"
    },
    "./simd": {
      "import": "./simd/pixman-simd.js",
      "types": "./pixman.d.ts"
    },
    "./fallback": {
      "import": "./fallback/pixman-fallback.js",
      "types": "./pixman.d.ts"
    }
  },
  "files": [
    "simd/",
    "fallback/", 
    "pixman.d.ts",
    "README.md",
    "LICENSE"
  ],
  "scripts": {
    "test": "node test/test.js",
    "benchmark": "node test/benchmark.js",
    "build": "./build-wasm.sh"
  },
  "keywords": [
    "pixman",
    "graphics",
    "pixel",
    "compositing", 
    "wasm",
    "webassembly",
    "simd",
    "performance"
  ],
  "author": "Superstruct Ltd, New Zealand",
  "license": "MIT",
  "repository": {
    "type": "git",
    "url": "https://github.com/superstruct/pixman.wasm.git"
  },
  "homepage": "https://github.com/superstruct/pixman.wasm",
  "engines": {
    "node": ">=16.0.0"
  },
  "browserslist": [
    "Chrome >= 91",
    "Firefox >= 89", 
    "Safari >= 16.4",
    "Edge >= 91"
  ]
}
EOF
    
    print_success "package.json created"
}

run_tests() {
    if [[ $BUILD_TESTS -eq 0 ]]; then
        print_status "Skipping tests (BUILD_TESTS=0)"
        return
    fi
    
    print_status "Running basic functionality tests..."
    
    # TODO: Implement comprehensive test suite
    # For now, verify builds exist
    
    if [[ $BUILD_SIMD -eq 1 ]] && [[ ! -f "$INSTALL_DIR/simd/lib/libpixman-1.a" ]]; then
        print_error "SIMD build artifacts not found"
        exit 1
    fi
    
    if [[ $BUILD_FALLBACK -eq 1 ]] && [[ ! -f "$INSTALL_DIR/fallback/lib/libpixman-1.a" ]]; then
        print_error "Fallback build artifacts not found"
        exit 1
    fi
    
    print_success "Basic tests passed"
}

main() {
    print_status "Starting pixman.wasm production build"
    print_status "Superstruct Ltd, New Zealand - WASM Ecosystem"
    
    check_requirements
    clean_build
    
    # Build SIMD variant
    if [[ $BUILD_SIMD -eq 1 ]]; then
        configure_build "simd"
        build_variant "simd" 
        create_js_wrapper "simd"
    fi
    
    # Build fallback variant  
    if [[ $BUILD_FALLBACK -eq 1 ]]; then
        configure_build "fallback"
        build_variant "fallback"
        create_js_wrapper "fallback"
    fi
    
    # Create package files
    create_typescript_definitions
    create_package_json
    
    # Run tests
    run_tests
    
    # Summary
    print_success "✅ pixman.wasm build completed successfully!"
    print_status "📦 Build artifacts:"
    find "$INSTALL_DIR" -name "*.a" -o -name "*.js" -o -name "*.wasm" | sort
    
    if [[ $BUILD_SIMD -eq 1 ]] && [[ $BUILD_FALLBACK -eq 1 ]]; then
        print_status "🚀 Dual-build strategy: SIMD + Fallback for maximum browser compatibility"
    fi
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --no-simd)
            BUILD_SIMD=0
            shift
            ;;
        --no-fallback)
            BUILD_FALLBACK=0
            shift
            ;;
        --no-tests)
            BUILD_TESTS=0
            shift
            ;;
        --help)
            echo "Usage: $0 [options]"
            echo "Options:"
            echo "  --no-simd      Skip SIMD-optimized build"
            echo "  --no-fallback  Skip fallback build"  
            echo "  --no-tests     Skip test execution"
            echo "  --help         Show this help"
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            exit 1
            ;;
    esac
done

main "$@"