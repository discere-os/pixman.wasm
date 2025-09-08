/*
 * Copyright © 2025 Superstruct Ltd, New Zealand
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#if defined(PIXMAN_WASM_SIMD) && defined(__wasm_simd128__)

#include <wasm_simd128.h>
#include "pixman-private.h"
#include "pixman-combine32.h"
#include "pixman-inlines.h"

/* WASM SIMD128 implementation for pixman operations */

/* Helper macros for WASM SIMD operations */
#define WASM_SIMD_SPLAT_ALPHA(v) wasm_i32x4_shuffle(v, v, 3, 3, 3, 3)
#define WASM_SIMD_EXTRACT_ALPHA(v) wasm_u32x4_extract_lane(v, 3)

/* Alpha compositing with proper WASM SIMD */
static force_inline v128_t
wasm_composite_over_8888_8888(v128_t src, v128_t dest)
{
    /* Extract alpha channel from source */
    v128_t src_alpha = wasm_u8x16_shr(src, 24);
    v128_t src_alpha_splat = wasm_i8x16_splat(wasm_u8x16_extract_lane(src_alpha, 0));
    
    /* Calculate inverse alpha: 255 - alpha */
    v128_t inv_alpha = wasm_u8x16_sub(wasm_i8x16_splat(255), src_alpha_splat);
    
    /* Multiply destination by inverse alpha */
    v128_t dest_scaled = wasm_u16x8_mul(
        wasm_u16x8_extend_low_u8x16(dest),
        wasm_u16x8_extend_low_u8x16(inv_alpha)
    );
    
    /* Add source + scaled destination */
    v128_t result_16 = wasm_u16x8_add(
        wasm_u16x8_extend_low_u8x16(src),
        wasm_u16x8_shr(dest_scaled, 8)
    );
    
    /* Pack back to 8-bit with saturation */
    return wasm_u8x16_narrow_i16x8(result_16, result_16);
}

/* Optimized source copy with SIMD alignment */
static force_inline v128_t
wasm_composite_src_8888_8888(v128_t src, v128_t dest)
{
    (void)dest; /* unused in source operation */
    return src;
}

/* ARGB to RGB565 format conversion with SIMD */
static force_inline v128_t
wasm_convert_8888_to_0565(v128_t argb)
{
    /* Extract RGB components */
    v128_t r = wasm_v128_and(wasm_u32x4_shr(argb, 16), wasm_i32x4_splat(0xFF));
    v128_t g = wasm_v128_and(wasm_u32x4_shr(argb, 8), wasm_i32x4_splat(0xFF));
    v128_t b = wasm_v128_and(argb, wasm_i32x4_splat(0xFF));
    
    /* Scale to RGB565: R(5), G(6), B(5) */
    r = wasm_u32x4_shr(wasm_u32x4_mul(r, wasm_i32x4_splat(31)), 8);  /* 5 bits */
    g = wasm_u32x4_shr(wasm_u32x4_mul(g, wasm_i32x4_splat(63)), 8);  /* 6 bits */
    b = wasm_u32x4_shr(wasm_u32x4_mul(b, wasm_i32x4_splat(31)), 8);  /* 5 bits */
    
    /* Pack into RGB565 format */
    return wasm_v128_or(wasm_v128_or(
        wasm_u32x4_shl(r, 11),
        wasm_u32x4_shl(g, 5)),
        b);
}

/* Bilinear filtering with WASM SIMD optimization */
static force_inline v128_t
wasm_bilinear_interpolation(v128_t tl, v128_t tr, v128_t bl, v128_t br,
                           uint32_t alpha_x, uint32_t alpha_y)
{
    /* Convert alpha values to SIMD vectors */
    v128_t ax = wasm_i16x8_splat((int16_t)alpha_x);
    v128_t ay = wasm_i16x8_splat((int16_t)alpha_y);
    v128_t inv_ax = wasm_i16x8_splat((int16_t)(256 - alpha_x));
    v128_t inv_ay = wasm_i16x8_splat((int16_t)(256 - alpha_y));
    
    /* Extend pixels to 16-bit for precise arithmetic */
    v128_t tl_16 = wasm_u16x8_extend_low_u8x16(tl);
    v128_t tr_16 = wasm_u16x8_extend_low_u8x16(tr);
    v128_t bl_16 = wasm_u16x8_extend_low_u8x16(bl);
    v128_t br_16 = wasm_u16x8_extend_low_u8x16(br);
    
    /* Horizontal interpolation */
    v128_t top = wasm_u16x8_add(
        wasm_u16x8_shr(wasm_u16x8_mul(tl_16, inv_ax), 8),
        wasm_u16x8_shr(wasm_u16x8_mul(tr_16, ax), 8));
    
    v128_t bottom = wasm_u16x8_add(
        wasm_u16x8_shr(wasm_u16x8_mul(bl_16, inv_ax), 8),
        wasm_u16x8_shr(wasm_u16x8_mul(br_16, ax), 8));
    
    /* Vertical interpolation */
    v128_t result_16 = wasm_u16x8_add(
        wasm_u16x8_shr(wasm_u16x8_mul(top, inv_ay), 8),
        wasm_u16x8_shr(wasm_u16x8_mul(bottom, ay), 8));
    
    /* Pack back to 8-bit */
    return wasm_u8x16_narrow_i16x8(result_16, result_16);
}

/*
 * WASM SIMD composite operations
 */

static void
wasm_simd_composite_over_8888_8888 (pixman_implementation_t *imp,
                                   pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint32_t *dst_line, *dst;
    uint32_t *src_line, *src;
    int dst_stride, src_stride;
    int32_t w;

    PIXMAN_IMAGE_GET_LINE (dest_image, dest_x, dest_y, uint32_t, dst_stride, dst_line, 1);
    PIXMAN_IMAGE_GET_LINE (src_image, src_x, src_y, uint32_t, src_stride, src_line, 1);

    while (height--)
    {
        dst = dst_line;
        dst_line += dst_stride;
        src = src_line;
        src_line += src_stride;
        w = width;

        /* Process 4 pixels at a time with SIMD */
        while (w >= 4)
        {
            v128_t src_pixels = wasm_v128_load((const v128_t*)src);
            v128_t dst_pixels = wasm_v128_load((const v128_t*)dst);
            
            v128_t result = wasm_composite_over_8888_8888(src_pixels, dst_pixels);
            
            wasm_v128_store((v128_t*)dst, result);
            
            src += 4;
            dst += 4;
            w -= 4;
        }

        /* Handle remaining pixels */
        while (w--)
        {
            uint32_t s = *src++;
            uint32_t d = *dst;
            uint32_t alpha = s >> 24;
            
            if (alpha == 0xff)
                *dst = s;
            else if (alpha != 0)
                *dst = s + DIV_UN8 (d * (0xff - alpha), 0xff);
            
            dst++;
        }
    }
}

static void
wasm_simd_composite_src_8888_8888 (pixman_implementation_t *imp,
                                  pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint32_t *dst_line, *dst;
    uint32_t *src_line, *src;
    int dst_stride, src_stride;
    int32_t w;

    PIXMAN_IMAGE_GET_LINE (dest_image, dest_x, dest_y, uint32_t, dst_stride, dst_line, 1);
    PIXMAN_IMAGE_GET_LINE (src_image, src_x, src_y, uint32_t, src_stride, src_line, 1);

    while (height--)
    {
        dst = dst_line;
        dst_line += dst_stride;
        src = src_line;
        src_line += src_stride;
        w = width;

        /* SIMD copy: 4 pixels (16 bytes) at a time */
        while (w >= 4)
        {
            v128_t src_pixels = wasm_v128_load((const v128_t*)src);
            wasm_v128_store((v128_t*)dst, src_pixels);
            
            src += 4;
            dst += 4;
            w -= 4;
        }

        /* Copy remaining pixels */
        while (w--)
            *dst++ = *src++;
    }
}

/* WASM SIMD implementation structure */
static const pixman_fast_path_t wasm_simd_fast_paths[] =
{
    /* Alpha compositing operations */
    PIXMAN_STD_FAST_PATH (OVER, a8r8g8b8, null, a8r8g8b8, wasm_simd_composite_over_8888_8888),
    PIXMAN_STD_FAST_PATH (OVER, a8r8g8b8, null, x8r8g8b8, wasm_simd_composite_over_8888_8888),
    PIXMAN_STD_FAST_PATH (OVER, a8b8g8r8, null, a8b8g8r8, wasm_simd_composite_over_8888_8888),
    PIXMAN_STD_FAST_PATH (OVER, a8b8g8r8, null, x8b8g8r8, wasm_simd_composite_over_8888_8888),

    /* Source copy operations */
    PIXMAN_STD_FAST_PATH (SRC, a8r8g8b8, null, a8r8g8b8, wasm_simd_composite_src_8888_8888),
    PIXMAN_STD_FAST_PATH (SRC, x8r8g8b8, null, x8r8g8b8, wasm_simd_composite_src_8888_8888),
    PIXMAN_STD_FAST_PATH (SRC, a8b8g8r8, null, a8b8g8r8, wasm_simd_composite_src_8888_8888),
    PIXMAN_STD_FAST_PATH (SRC, x8b8g8r8, null, x8b8g8r8, wasm_simd_composite_src_8888_8888),
    
    { PIXMAN_OP_NONE }
};

/* WASM SIMD runtime detection */
PIXMAN_EXPORT pixman_bool_t
pixman_wasm_simd_available (void)
{
#ifdef __wasm_simd128__
    return TRUE;
#else
    return FALSE;
#endif
}

/* Benchmarking function for performance validation */
PIXMAN_EXPORT double
pixman_wasm_benchmark_composite (int width, int height, int iterations)
{
    pixman_image_t *src, *dest;
    double start_time, end_time;
    int i;
    
    /* Create test images */
    src = pixman_image_create_bits (PIXMAN_a8r8g8b8, width, height, NULL, 0);
    dest = pixman_image_create_bits (PIXMAN_a8r8g8b8, width, height, NULL, 0);
    
    if (!src || !dest)
        return -1.0;
    
    /* Fill with test pattern */
    pixman_image_fill_rectangles (PIXMAN_OP_SRC, src, 
        &(pixman_color_t){0x8000, 0x8000, 0x8000, 0x8000},
        1, &(pixman_rectangle16_t){0, 0, width, height});
    
    /* Benchmark composite operations */
    start_time = emscripten_get_now();
    
    for (i = 0; i < iterations; i++)
    {
        pixman_image_composite32 (PIXMAN_OP_OVER,
                                 src, NULL, dest,
                                 0, 0, 0, 0, 0, 0,
                                 width, height);
    }
    
    end_time = emscripten_get_now();
    
    pixman_image_unref (src);
    pixman_image_unref (dest);
    
    return (end_time - start_time) / iterations;
}

pixman_implementation_t *
_pixman_implementation_create_wasm_simd (pixman_implementation_t *fallback)
{
    pixman_implementation_t *imp = _pixman_implementation_create (fallback, wasm_simd_fast_paths);

    imp->composite = _pixman_implementation_create_general()->composite;

    return imp;
}

#endif /* PIXMAN_WASM_SIMD && __wasm_simd128__ */
