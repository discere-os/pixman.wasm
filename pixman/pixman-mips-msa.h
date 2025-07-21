/*
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Red Hat not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  Red Hat makes no representations about the
 * suitability of this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
 * AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 *
 *    Copyright (C) 2024 by Amartoros Huang
 *    junjia.huang@oss.cipunited.com
 *
 *    Copyright (C) 2024 by Xinyue Zhang
 *    xinyue.zhang@oss.cipunited.com
 * 
 *    Based on work by Rodrigo Kumpera and André Tupinambá
 */

#ifndef PIXMAN_MIPS_MSA_H
#define PIXMAN_MIPS_MSA_H

#include <msa.h>
#include "pixman-private.h"
#include "pixman-combine32.h"
#include "pixman-inlines.h"
#include "pixman-compiler.h"

#ifdef _MIPSEB
#define LANE_IMM0_1(x)	(0b1 - ((x) & 0b1))
#define LANE_IMM0_3(x)	(0b11 - ((x) & 0b11))
#define LANE_IMM0_7(x)	(0b111 - ((x) & 0b111))
#define LANE_IMM0_15(x)	(0b1111 - ((x) & 0b1111))
#else
#define LANE_IMM0_1(x)	((x) & 0b1)
#define LANE_IMM0_3(x)	((x) & 0b11)
#define LANE_IMM0_7(x)	((x) & 0b111)
#define LANE_IMM0_15(x)	((x) & 0b1111)
#endif

/* get lane */
#define msa_getq_lane_s16(__a, __b)	((int16_t)(__a)[LANE_IMM0_7(__b)])
#define msa_getq_lane_s32(__a, __b)     ((int32_t)(__a)[LANE_IMM0_3(__b)])
#define msa_getq_lane_s8(__a, imm0_15)  ((int8_t)__builtin_msa_copy_s_b(__a, imm0_15))

/* MSA_SHUFFLE */
#define _MSA_SHUFFLE(z, y, x, w) (((z) << 6) | ((y) << 4) | ((x) << 2) | (w))

/*fill*/
#define msa_dupq_n_s32(__a, __b, __c, __d)	((v4i32){__d, __c, __b, __a})

/*
 * Make sure that input order is the same as _mm_set_epixx in sse2
 *         Usage: v4i32   test = __msa_set_s_w(1, 2, 3, 4);
 *       Same as: v4i32   test =              {4, 3, 2, 1};
 * Equivalent to: __m128i test = _mm_set_epi32(1, 2, 3, 4);
*/
#define __msa_set_s_b(elem15, elem14, elem13, elem12, elem11, elem10, elem9, elem8, elem7, elem6, elem5, elem4, elem3, elem2, elem1, elem0) \
        {elem0, elem1, elem2, elem3, elem4, elem5, elem6, elem7, elem8, elem9, elem10, elem11, elem12, elem13, elem14, elem15}
#define __msa_set_s_h(elem7, elem6, elem5, elem4, elem3, elem2, elem1, elem0) \
        {elem0, elem1, elem2, elem3, elem4, elem5, elem6, elem7}
#define __msa_set_s_w(elem3, elem2, elem1, elem0) {elem0, elem1, elem2, elem3}
#define __msa_set_s_d(elem1, elem0) {elem0, elem1}
#define __msa_set_u_b  (v16u8)__msa_set_s_b
#define __msa_set_u_h  (v8u16)__msa_set_s_h
#define __msa_set_u_w  (v4u32)__msa_set_s_w
#define __msa_set_u_d  (v2u64)__msa_set_s_d

/* zero */
const v4i32 zero = {0, 0, 0, 0};

/* mul */
static force_inline v8i16
msa_mul_u_h (v8u16 __a, v8u16 __b)
{
    v8i16 zero    = __msa_fill_h (0);
    v4i32 right_a = (v4i32)__msa_ilvev_h (zero, (v8i16)__a);
    v4i32 left_a  = (v4i32)__msa_ilvod_h (zero, (v8i16)__a);
    v4i32 right_b = (v4i32)__msa_ilvev_h (zero, (v8i16)__b);
    v4i32 left_b  = (v4i32)__msa_ilvod_h (zero, (v8i16)__b);
    v4i32 ab_1    = __msa_mulv_w (right_a, right_b);
    v4i32 ab_2    = __msa_mulv_w (left_a, left_b);
    v8i16 result  = __msa_ilvod_h ((v8i16)ab_2, (v8i16)ab_1);
    return result;
}

/* Simulates _mm_madd_epi16 */
static force_inline v4i32
msa_madd_h (v8i16 dst, v8i16 data) {
    v4i32 result = __msa_dotp_s_w ((v8i16) dst, (v8i16) data);
    return result;
}

#endif //PIXMAN_MIPS_MSA_H