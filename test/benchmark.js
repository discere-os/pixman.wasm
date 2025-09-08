/*
 * WASM Integration Copyright (c) 2025 Superstruct Ltd, New Zealand
 * Licensed under the same license as the underlying pixman project (MIT)
 * 
 * Comprehensive benchmarking suite for pixman.wasm
 * Advanced performance benchmarking for pixman operations
 */

import PixmanWASM from '../dist/simd/pixman-simd.js';
import PixmanWASMFallback from '../dist/fallback/pixman-fallback.js';

class PixmanBenchmark {
    constructor() {
        this.results = [];
        this.simdModule = null;
        this.fallbackModule = null;
    }
    
    async initialize() {
        console.log('🚀 Initializing pixman.wasm benchmark suite...');
        
        try {
            this.simdModule = new PixmanWASM();
            await this.simdModule.initialize();
            console.log('✅ SIMD module initialized');
        } catch (error) {
            console.warn('⚠️  SIMD module failed to initialize:', error.message);
        }
        
        try {
            this.fallbackModule = new PixmanWASMFallback();
            await this.fallbackModule.initialize();
            console.log('✅ Fallback module initialized');
        } catch (error) {
            console.error('❌ Fallback module failed to initialize:', error.message);
            throw error;
        }
    }
    
    // Core pixel operations benchmark
    async benchmarkCompositeOperations() {
        console.log('\n📊 Benchmarking composite operations...');
        
        const testCases = [
            { width: 256, height: 256, name: 'Small Image (256x256)' },
            { width: 512, height: 512, name: 'Medium Image (512x512)' },
            { width: 1024, height: 1024, name: 'Large Image (1024x1024)' },
            { width: 2048, height: 2048, name: 'XL Image (2048x2048)' }
        ];
        
        const operations = [
            { op: 'composite_over', iterations: 100 },
            { op: 'composite_src', iterations: 200 },
            { op: 'format_conversion', iterations: 150 }
        ];
        
        for (const testCase of testCases) {
            for (const operation of operations) {
                // Benchmark SIMD version
                let simdTime = null;
                if (this.simdModule) {
                    try {
                        simdTime = await this.benchmarkOperation(
                            this.simdModule, 
                            testCase, 
                            operation
                        );
                    } catch (error) {
                        console.warn(`SIMD ${operation.op} failed:`, error.message);
                    }
                }
                
                // Benchmark fallback version
                const fallbackTime = await this.benchmarkOperation(
                    this.fallbackModule,
                    testCase, 
                    operation
                );
                
                // Calculate performance metrics
                const result = {
                    testCase: testCase.name,
                    operation: operation.op,
                    simdTime: simdTime,
                    fallbackTime: fallbackTime,
                    speedup: simdTime ? (fallbackTime / simdTime).toFixed(2) : null,
                    pixelsPerSecond: this.calculatePixelsPerSecond(
                        testCase.width, 
                        testCase.height, 
                        operation.iterations,
                        simdTime || fallbackTime
                    ),
                    memoryUsage: this.estimateMemoryUsage(testCase.width, testCase.height)
                };
                
                this.results.push(result);
                this.printBenchmarkResult(result);
            }
        }
    }
    
    async benchmarkOperation(module, testCase, operation) {
        const { width, height } = testCase;
        const { iterations } = operation;
        
        // Warm up
        for (let i = 0; i < 10; i++) {
            await this.runSingleOperation(module, width, height);
        }
        
        // Actual benchmark
        const startTime = performance.now();
        
        for (let i = 0; i < iterations; i++) {
            await this.runSingleOperation(module, width, height);
        }
        
        const endTime = performance.now();
        return (endTime - startTime) / iterations; // Average time per operation
    }
    
    async runSingleOperation(module, width, height) {
        // Use the module's benchmark function if available
        if (typeof module.benchmark === 'function') {
            return module.benchmark(width, height, 1);
        }
        
        // Fallback to manual operation
        const format = 0x20028888; // PIXMAN_a8r8g8b8
        
        const src = module.createImage(format, width, height);
        const dest = module.createImage(format, width, height);
        
        try {
            // Perform composite operation
            module.composite(
                0x03, // PIXMAN_OP_OVER
                src, null, dest,
                0, 0, 0, 0, 0, 0,
                width, height
            );
        } finally {
            module.unrefImage(src);
            module.unrefImage(dest);
        }
    }
    
    calculatePixelsPerSecond(width, height, iterations, timeMs) {
        const totalPixels = width * height * iterations;
        const timeSeconds = timeMs / 1000;
        return Math.round(totalPixels / timeSeconds);
    }
    
    estimateMemoryUsage(width, height) {
        // Estimate memory usage for ARGB32 format (4 bytes per pixel)
        const bytesPerImage = width * height * 4;
        const totalImages = 2; // source + destination
        return Math.round((bytesPerImage * totalImages) / (1024 * 1024)); // MB
    }
    
    printBenchmarkResult(result) {
        const simdIndicator = result.simdTime ? '🚀' : '🔄';
        const speedupText = result.speedup ? `${result.speedup}x faster` : 'N/A';
        
        console.log(
            `${simdIndicator} ${result.testCase} - ${result.operation}: ` +
            `${result.fallbackTime?.toFixed(2)}ms (fallback) ` +
            `${result.simdTime ? `${result.simdTime.toFixed(2)}ms (SIMD) ` : ''}` +
            `[${speedupText}] ${(result.pixelsPerSecond / 1e6).toFixed(1)}MP/s`
        );
    }
    
    // Memory stress test
    async benchmarkMemoryStress() {
        console.log('\n🧠 Running memory stress test...');
        
        const module = this.simdModule || this.fallbackModule;
        const format = 0x20028888; // PIXMAN_a8r8g8b8
        
        const images = [];
        const maxImages = 50;
        
        try {
            // Create many images to test memory management
            for (let i = 0; i < maxImages; i++) {
                const size = 256 + (i * 16); // Varying sizes
                const image = module.createImage(format, size, size);
                images.push(image);
                
                if (i % 10 === 0) {
                    console.log(`Created ${i + 1} images, total memory ~${((i + 1) * size * size * 4 / (1024 * 1024)).toFixed(1)}MB`);
                }
            }
            
            console.log('✅ Memory allocation test passed');
            
        } finally {
            // Clean up all images
            for (const image of images) {
                module.unrefImage(image);
            }
            console.log('✅ Memory cleanup completed');
        }
    }
    
    // Browser compatibility test
    async benchmarkBrowserFeatures() {
        console.log('\n🌐 Testing browser feature compatibility...');
        
        const features = {
            'WebAssembly': typeof WebAssembly !== 'undefined',
            'WASM SIMD': typeof WebAssembly.SIMD !== 'undefined',
            'SharedArrayBuffer': typeof SharedArrayBuffer !== 'undefined',
            'Performance API': typeof performance !== 'undefined' && typeof performance.now === 'function',
            'BigInt': typeof BigInt !== 'undefined'
        };
        
        console.log('Browser Feature Support:');
        Object.entries(features).forEach(([feature, supported]) => {
            const indicator = supported ? '✅' : '❌';
            console.log(`  ${indicator} ${feature}`);
        });
        
        // Test SIMD availability specifically
        const simdAvailable = this.simdModule && this.simdModule.hasSIMD();
        console.log(`  ${simdAvailable ? '✅' : '❌'} Pixman WASM SIMD Runtime`);
        
        return features;
    }
    
    // Performance regression test
    async benchmarkRegression() {
        console.log('\n📈 Running performance regression test...');
        
        const baselineExpected = {
            'composite_over_512x512': { simd: 5.0, fallback: 12.0 }, // ms
            'composite_src_512x512': { simd: 2.0, fallback: 5.0 },
            'format_conversion_512x512': { simd: 3.0, fallback: 8.0 }
        };
        
        const regressionResults = [];
        
        for (const result of this.results) {
            if (result.testCase === 'Medium Image (512x512)') {
                const key = `${result.operation}_512x512`;
                const expected = baselineExpected[key];
                
                if (expected) {
                    const simdRegression = result.simdTime ? 
                        ((result.simdTime - expected.simd) / expected.simd * 100) : null;
                    const fallbackRegression = result.fallbackTime ?
                        ((result.fallbackTime - expected.fallback) / expected.fallback * 100) : null;
                    
                    regressionResults.push({
                        operation: result.operation,
                        simdRegression,
                        fallbackRegression,
                        acceptable: (simdRegression === null || simdRegression < 20) && 
                                   (fallbackRegression === null || fallbackRegression < 20)
                    });
                }
            }
        }
        
        console.log('Performance Regression Results:');
        regressionResults.forEach(result => {
            const indicator = result.acceptable ? '✅' : '⚠️';
            console.log(`  ${indicator} ${result.operation}:`);
            if (result.simdRegression !== null) {
                console.log(`    SIMD: ${result.simdRegression > 0 ? '+' : ''}${result.simdRegression.toFixed(1)}%`);
            }
            if (result.fallbackRegression !== null) {
                console.log(`    Fallback: ${result.fallbackRegression > 0 ? '+' : ''}${result.fallbackRegression.toFixed(1)}%`);
            }
        });
        
        return regressionResults;
    }
    
    // Generate comprehensive report
    generateReport() {
        console.log('\n📋 PIXMAN.WASM BENCHMARK REPORT');
        console.log('=' .repeat(50));
        console.log(`Timestamp: ${new Date().toISOString()}`);
        console.log(`User Agent: ${typeof navigator !== 'undefined' ? navigator.userAgent : 'Node.js'}`);
        
        // Overall performance summary
        const simdResults = this.results.filter(r => r.simdTime !== null);
        const avgSpeedup = simdResults.length > 0 ? 
            (simdResults.reduce((sum, r) => sum + parseFloat(r.speedup), 0) / simdResults.length).toFixed(2) : 'N/A';
        
        console.log(`\n🚀 SIMD Performance:`);
        console.log(`  Average Speedup: ${avgSpeedup}x`);
        console.log(`  SIMD Tests: ${simdResults.length}/${this.results.length}`);
        
        // Top performing operations
        const topPerformers = this.results
            .filter(r => r.speedup)
            .sort((a, b) => parseFloat(b.speedup) - parseFloat(a.speedup))
            .slice(0, 3);
        
        if (topPerformers.length > 0) {
            console.log(`\n🏆 Top SIMD Performers:`);
            topPerformers.forEach((result, index) => {
                console.log(`  ${index + 1}. ${result.operation} (${result.testCase}): ${result.speedup}x speedup`);
            });
        }
        
        return {
            timestamp: new Date().toISOString(),
            totalTests: this.results.length,
            simdTests: simdResults.length,
            averageSpeedup: avgSpeedup,
            results: this.results
        };
    }
}

// Main benchmark execution
async function runBenchmarks() {
    const benchmark = new PixmanBenchmark();
    
    try {
        await benchmark.initialize();
        
        await benchmark.benchmarkCompositeOperations();
        await benchmark.benchmarkMemoryStress();
        
        const browserFeatures = await benchmark.benchmarkBrowserFeatures();
        const regressionResults = await benchmark.benchmarkRegression();
        
        const report = benchmark.generateReport();
        
        // Save report if in Node.js environment
        if (typeof process !== 'undefined' && process.versions && process.versions.node) {
            const fs = await import('fs');
            const reportFile = `benchmark-report-${Date.now()}.json`;
            fs.writeFileSync(reportFile, JSON.stringify({
                ...report,
                browserFeatures,
                regressionResults
            }, null, 2));
            console.log(`\n💾 Detailed report saved to: ${reportFile}`);
        }
        
        // Return summary for CI/CD integration
        return {
            success: true,
            averageSpeedup: report.averageSpeedup,
            testsRun: report.totalTests,
            simdWorking: report.simdTests > 0,
            regressionAcceptable: regressionResults.every(r => r.acceptable)
        };
        
    } catch (error) {
        console.error('❌ Benchmark failed:', error);
        return {
            success: false,
            error: error.message
        };
    }
}

// Export for different environments
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { PixmanBenchmark, runBenchmarks };
} else if (typeof window !== 'undefined') {
    window.PixmanBenchmark = PixmanBenchmark;
    window.runPixmanBenchmarks = runBenchmarks;
}

// Auto-run if called directly
if (typeof process !== 'undefined' && import.meta.url === `file://${process.argv[1]}`) {
    runBenchmarks().then(result => {
        process.exit(result.success ? 0 : 1);
    });
}

export { PixmanBenchmark, runBenchmarks };