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

#ifdef HAVE_CONFIG_H
#include <pixman-config.h>
#endif

#include "pixman-private.h"
#include "pixman-mips-msa.h"
#include <msa.h>



static v8i16 mask_0080;
static v8i16 mask_00ff;
static v8i16 mask_0101;
static v8i16 mask_ffff;
static v4i32 mask_ff000000;
static v4i32 mask_alpha;

static v4i32 mask_565_r;
static v4i32 mask_565_g1, mask_565_g2;
static v4i32 mask_565_b;
static v4i32 mask_red;
static v4i32 mask_green;
static v4i32 mask_blue;

static v4i32 mask_565_fix_rb;
static v4i32 mask_565_fix_g;

static v4i32 mask_565_rb;
static v4i32 mask_565_pack_multiplier;

static force_inline v16i8
unpack_32_1x128 (uint32_t data)
{
    v4i32 filled_data;
    v16i8 unpacked;

    filled_data = __msa_insert_w(zero, 0, data);
    unpacked = __msa_ilvr_b(__msa_fill_b(0), (v16i8)filled_data);

    return unpacked;
}

static force_inline void
unpack_128_2x128 (v16i8 data, void* data_lo, void* data_hi)
{

    v16i8 vdata_lo;
    v16i8 vdata_hi;
    vdata_lo = __msa_ld_b(data_lo, 0);
    vdata_hi = __msa_ld_b(data_hi, 0);
    vdata_lo = __msa_ilvr_b((v16i8)zero, data);
    vdata_hi = __msa_ilvl_b((v16i8)zero, data);

    __msa_st_b(vdata_hi, data_hi, 0);
    __msa_st_b(vdata_lo, data_lo, 0);

}

static force_inline v4i32
unpack_565_to_8888 (v4i32 lo) 
{
    v4i32 r, g, b, dr, dg, db;
    v4i32 argb;
    argb = __msa_fill_w (0);
    r = (v4i32)__msa_and_v ((v16u8)__msa_srli_w (lo, 11),  (v16u8)__msa_fill_w (0x1F));
    g = (v4i32)__msa_and_v ((v16u8)__msa_srli_w (lo,  5),  (v16u8)__msa_fill_w (0x3F));
    b = (v4i32)__msa_and_v ((v16u8)                   lo,  (v16u8)__msa_fill_w (0x1F));

    dr = __msa_srli_w (r, 2);
    dg = __msa_srli_w (g, 4);
    db = __msa_srli_w (b, 2);

    r = __msa_adds_s_w (__msa_slli_w (r, 3), dr);
    g = __msa_adds_s_w (__msa_slli_w (g, 2), dg);
    b = __msa_adds_s_w (__msa_slli_w (b, 3), db);

    r = __msa_slli_w (r, 16);
    g = __msa_slli_w (g, 8);

    argb = (v4i32)__msa_or_v ((v16u8)argb, (v16u8)r);
    argb = (v4i32)__msa_or_v ((v16u8)argb, (v16u8)g);
    argb = (v4i32)__msa_or_v ((v16u8)argb, (v16u8)b);

    return argb; 
}

static force_inline void
unpack_565_128_4x128 (v4i32  data,
                      void* data0,
                      void* data1,
                      void* data2,
                      void* data3)
{
    v4i32 lo, hi;

    lo = (v4i32)__msa_ilvr_h ((v8i16)zero, (v8i16)data);
    hi = (v4i32)__msa_ilvl_h ((v8i16)zero, (v8i16)data);

    lo = unpack_565_to_8888 (lo);
    hi = unpack_565_to_8888 (hi);

    unpack_128_2x128 ((v16i8)lo, data0, data1);
    unpack_128_2x128 ((v16i8)hi, data2, data3);
}

static force_inline int16_t
pack_565_32_16 (int32_t pixel)
{
    return (int16_t) (((pixel >> 8) & 0xf800) |
                   ((pixel >> 5) & 0x07e0) |
                   ((pixel >> 3) & 0x001f));
}

static force_inline v16i8
pack_2x128_128 (v8i16 lo, v8i16 hi)
{
    v8u16 los, his;
    v16i8 result;

    los = __msa_sat_u_h ((v8u16)lo, 7);
    his = __msa_sat_u_h ((v8u16)hi, 7);
    
    result = __msa_pckev_b ((v16i8)his, (v16i8)los);
    return result;
}

 static force_inline v8i16
pack_565_2packedx128_128 (v4i32 lo, v4i32 hi)
{
    /* This big block of sh*t simulates _mm_madd_epi16 */
    v4i32 rb0, rb1, t0, t1, g0, g1;

    rb0 = (v4i32)__msa_and_v ((v16u8)lo, (v16u8)mask_565_rb);
    rb1 = (v4i32)__msa_and_v ((v16u8)hi, (v16u8)mask_565_rb);

    t0 = __msa_dotp_s_w ((v8i16)rb0, (v8i16)mask_565_pack_multiplier);
    t1 = __msa_dotp_s_w ((v8i16)rb1, (v8i16)mask_565_pack_multiplier);

    g0 = (v4i32)__msa_and_v ((v16u8)lo, (v16u8)mask_green);
    g1 = (v4i32)__msa_and_v ((v16u8)hi, (v16u8)mask_green);

    t0 = (v4i32)__msa_or_v ((v16u8)t0, (v16u8)g0);
    t1 = (v4i32)__msa_or_v ((v16u8)t1, (v16u8)g1);

    /* Simulates _mm_packus_epi32 */
    t0 = __msa_slli_w (t0, 16 - 5);
    t1 = __msa_slli_w (t1, 16 - 5);
    t0 = __msa_srli_w (t0, 16);
    t1 = __msa_srli_w (t1, 16);

    /* Simulates _mm_packs_epi32 */
    return __msa_ilvr_h ((v8i16)t0, (v8i16)t1);
}

static force_inline v4i32
pack_565_2x128_128 (v4i32 lo, v4i32 hi)
{
    v4i32 data, pp;
    v16u8 r, g1, g2, b;
    v16i8 packed;

    data = (v4i32)pack_2x128_128((v8i16)lo, (v8i16)hi);

    r  = __msa_and_v ((v16u8)                    data, (v16u8)mask_565_r);
    g1 = __msa_and_v ((v16u8)(__msa_slli_w (data, 3)), (v16u8)mask_565_g1);
    g2 = __msa_and_v ((v16u8)(__msa_srli_w (data, 5)), (v16u8)mask_565_g2);
    b  = __msa_and_v ((v16u8)(__msa_srli_w (data, 3)), (v16u8)mask_565_b);

    packed = (v16i8)__msa_or_v (__msa_or_v (__msa_or_v(r, g1), g2),
                                             b);
    pp = (v4i32)packed;
    return pp;
    
}

static force_inline v16i8
pack_565_4x128_128 (void* w0, void* w1, void* w2, void* w3)
{
    v4i32 v0;
    v4i32 v1;
    v4i32 v2;
    v4i32 v3;

    v0 = __msa_ld_w (w0, 0);
    v1 = __msa_ld_w (w1, 0);
    v2 = __msa_ld_w (w2, 0);
    v3 = __msa_ld_w (w3, 0);

    return __msa_pckev_b ((v16i8)pack_565_2x128_128 (v0, v1),
                             (v16i8)pack_565_2x128_128 (v2, v3));
}

static force_inline int
is_opaque (v16i8 x)
{
    v16i8 ffs, cmpv;
    uint8_t cmp = 1;

    ffs  = __msa_ceq_b (x, x);
    cmpv = __msa_ceq_b( (v16i8)__msa_and_v ((v16u8)__msa_ceq_b (ffs, x), (v16u8)__msa_fill_b (0x88)),
                                __msa_fill_b (0x88));

    for (int i = 0; i < 4; i++)
    {
        if (cmpv[i] != -1)
        {
                cmp = 0;
                break;
        }
    }

    return cmp;
}

static force_inline int
is_zero (v16i8 x)
{

    int32_t cmpresult;

    cmpresult = __msa_test_bz_v ((v16u8) x);

    return cmpresult;
}

static force_inline int
is_transparent (v16i8 x)
{
    v4i32 cmpv;
    int32_t cmpresult;

    cmpv = (v4i32)__msa_ceq_b (x, __msa_fill_b (0));
    cmpresult = 1;

    for (int i = 0; i < 4; i++)
    {
    	if ((msa_getq_lane_s32 (cmpv, i) & 0xff000000) != 0xff000000)
        {
    	    cmpresult = 0;
    	    break;
    	}
    }
    return cmpresult;
}

static force_inline v4i32
expand_pixel_32_1x128 (uint32_t data)
{
    return __msa_shf_w ((v4i32)unpack_32_1x128 (data), _MSA_SHUFFLE (1,0,1,0));
}

static force_inline v8i16
expand_alpha_1x128 (v8i16 data)
{
    v8i16 d = data;
    return __msa_shf_h (d, _MSA_SHUFFLE (3, 3, 3, 3));
}

static force_inline void
expand_alpha_2x128 (v8i16  data_lo,
                    v8i16  data_hi,
                    void* alpha_lo,
                    void* alpha_hi)
{
    v8i16 valpha_lo, valpha_hi;

    valpha_lo = __msa_shf_h (data_lo, _MSA_SHUFFLE (3, 3, 3, 3));
    valpha_hi = __msa_shf_h (data_hi, _MSA_SHUFFLE (3, 3, 3, 3));

    __msa_st_h (valpha_lo, alpha_lo, 0);
    __msa_st_h (valpha_hi, alpha_hi, 0);
}

static force_inline void
expand_alpha_rev_2x128 (v8i16  data_lo,
                        v8i16  data_hi,
                        void* alpha_lo,
                        void* alpha_hi)
{
    v8i16 valpha_lo, valpha_hi;

    valpha_lo = __msa_shf_h (data_lo, _MSA_SHUFFLE (0, 0, 0, 0));
    valpha_hi = __msa_shf_h (data_hi, _MSA_SHUFFLE (0, 0, 0, 0));

    __msa_st_h(valpha_lo, alpha_lo, 0);
    __msa_st_h(valpha_hi, alpha_hi, 0);
}

static force_inline void
pix_multiply_2x128 (void* data_lo,
                    void* data_hi,
                    void* alpha_lo,
                    void* alpha_hi,
                    void* ret_lo,
                    void* ret_hi)
{
    v8u16 lo, hi;
    v8i16 vdata_lo, vdata_hi, valpha_lo, valpha_hi, vret_lo, vret_hi;

    vdata_lo  = __msa_ld_h (data_lo, 0);
    vdata_hi  = __msa_ld_h (data_hi, 0);
    valpha_lo = __msa_ld_h (alpha_lo, 0);
    valpha_hi = __msa_ld_h (alpha_hi, 0);


    lo = __msa_adds_u_h ((v8u16)__msa_mulv_h (vdata_lo, valpha_lo), (v8u16)mask_0080);
    hi = __msa_adds_u_h ((v8u16)__msa_mulv_h (vdata_hi, valpha_hi), (v8u16)mask_0080);

    vret_lo = msa_mul_u_h (lo, (v8u16)mask_0101);
    vret_hi = msa_mul_u_h (hi, (v8u16)mask_0101);

    __msa_st_h(vret_lo, ret_lo, 0);
    __msa_st_h(vret_hi, ret_hi, 0);
}

static force_inline void
pix_add_multiply_2x128 (void* src_lo,
                        void* src_hi,
                        void* alpha_dst_lo,
                        void* alpha_dst_hi,
                        void* dst_lo,
                        void* dst_hi,
                        void* alpha_src_lo,
                        void* alpha_src_hi,
                        void* ret_lo,
                        void* ret_hi)
{
    v16u8 t1_lo, t1_hi;
    v16u8 t2_lo, t2_hi;
    v16u8 vret_lo, vret_hi;

    pix_multiply_2x128 (src_lo, src_hi, alpha_dst_lo, alpha_dst_hi, &t1_lo, &t1_hi);
    pix_multiply_2x128 (dst_lo, dst_hi, alpha_src_lo, alpha_src_hi, &t2_lo, &t2_hi);

    vret_lo = __msa_adds_u_b (t1_lo, t2_lo);
    vret_hi = __msa_adds_u_b (t1_hi, t2_hi);

    __msa_st_b((v16i8)vret_lo, ret_lo, 0);
    __msa_st_b((v16i8)vret_hi, ret_hi, 0);
}

static force_inline void
negate_2x128 (v8i16  data_lo,
              v8i16  data_hi,
              void*  neg_lo,
              void*  neg_hi)
{
    v16u8 dlo, dhi;
    dlo = (v16u8)data_lo;
    dhi = (v16u8)data_hi;

    __msa_st_h ((v8i16)__msa_xor_v (dlo, (v16u8)mask_00ff), neg_lo, 0);
    __msa_st_h ((v8i16)__msa_xor_v (dhi, (v16u8)mask_00ff), neg_hi, 0);
}

static force_inline void
invert_colors_2x128 (v8i16  data_lo,
                     v8i16  data_hi,
                     void*  inv_lo,
                     void*  inv_hi)
{
    v8i16 vinv_lo, vinv_hi;

    vinv_lo = __msa_shf_h (data_lo, _MSA_SHUFFLE (3, 0, 1, 2));
    vinv_hi = __msa_shf_h (data_hi, _MSA_SHUFFLE (3, 0, 1, 2));

    __msa_st_h (vinv_lo, inv_lo, 0);
    __msa_st_h (vinv_hi, inv_hi, 0);    
}

static force_inline void
over_2x128 (void* src_lo,
            void* src_hi,
            void* alpha_lo,
            void* alpha_hi,
            void* dst_lo,
            void* dst_hi)
{
    v8i16 t1, t2;

    v8i16 valpha_lo, valpha_hi;
    v8i16 vdst_lo, vdst_hi, vsrc_lo, vsrc_hi;

    valpha_lo = __msa_ld_h (alpha_lo, 0);
    valpha_hi = __msa_ld_h (alpha_hi, 0);       

    negate_2x128(valpha_lo, valpha_hi, &t1, &t2);

    pix_multiply_2x128 (dst_lo, dst_hi, &t1, &t2, dst_lo, dst_hi);

    vdst_lo = __msa_ld_h (dst_lo, 0);
    vdst_hi = __msa_ld_h (dst_hi, 0);
    vsrc_lo = __msa_ld_h (src_lo, 0);
    vsrc_hi = __msa_ld_h (src_hi, 0);

    vdst_lo = (v8i16)__msa_adds_u_b ((v16u8)vsrc_lo, (v16u8)vdst_lo);
    vdst_hi = (v8i16)__msa_adds_u_b ((v16u8)vsrc_hi, (v16u8)vdst_hi);

    __msa_st_h (vdst_hi, dst_hi, 0);
    __msa_st_h (vdst_lo, dst_lo, 0);

}

static force_inline void
over_rev_non_pre_2x128 (v8i16  src_lo,
                        v8i16  src_hi,
                        void*  dst_lo,
                        void*  dst_hi)
{
    v8i16 lo, hi;
    v8i16 alpha_lo, alpha_hi;
    v8i16 vdst_lo, vdst_hi;

    vdst_lo = __msa_ld_h (dst_lo, 0);
    vdst_hi = __msa_ld_h (dst_hi, 0);
    expand_alpha_2x128 (src_lo, src_hi,  &alpha_lo,  &alpha_hi);

    lo = (v8i16)__msa_or_v ((v16u8)alpha_lo, (v16u8)mask_alpha);
    hi = (v8i16)__msa_or_v ((v16u8)alpha_hi, (v16u8)mask_alpha);

    invert_colors_2x128 (src_lo, src_hi, &src_lo, &src_hi);     

    pix_multiply_2x128 (&src_lo, &src_hi, &lo, &hi, &lo, &hi);

    over_2x128 (&lo,  &hi, &alpha_lo, &alpha_hi, dst_lo, dst_hi);

    vdst_lo = __msa_ld_h (dst_lo, 0);
    vdst_hi = __msa_ld_h (dst_hi, 0);

    __msa_st_h (vdst_hi, dst_hi, 0);
    __msa_st_h (vdst_lo, dst_lo, 0);
}

static force_inline void
in_over_2x128 (void* src_lo,
               void* src_hi,
               void* alpha_lo,
               void* alpha_hi,
               void* mask_lo,
               void* mask_hi,
               void* dst_lo,
               void* dst_hi)
{
    v8u16 s_lo, s_hi;
    v8u16 a_lo, a_hi;

    pix_multiply_2x128 (src_lo,   src_hi, mask_lo, mask_hi, &s_lo, &s_hi);
    pix_multiply_2x128 (alpha_lo, alpha_hi, mask_lo, mask_hi, &a_lo, &a_hi);

    over_2x128 (&s_lo, &s_hi, &a_lo, &a_hi, dst_lo, dst_hi);
}

/* load 4 pixels from a 16-byte boundary aligned address */
static force_inline v4i32
load_128_aligned (const void* src)
{ 
    return __msa_ld_w (src, 0);
}

/* load 4 pixels from a unaligned address */
static force_inline v4u32
load_128_unaligned (const void* src)
{
    return (v4u32)__msa_ld_w (src, 0);
}

/* save 4 pixels on a 16-byte boundary aligned address */
static force_inline void
save_128_aligned (void*  dst,
                  v4i32  data)
{
    __msa_st_w (data, dst, 0);
}

static force_inline v4u32
load_32_1x128 (uint32_t data)
{
    v4i32 zero;
    zero = __msa_fill_w (0);
    return (v4u32)__msa_insert_w (zero, 0, data);
}

static force_inline v8i16
expand_alpha_rev_1x128 (v8i16 data)
{
    v2u64 bdata, shuffle;

    bdata   = (v2u64)data;
    shuffle = (v2u64)__msa_shf_h ((v8i16)data, _MSA_SHUFFLE (0, 0, 0, 0));

    return (v8i16)__msa_insve_d ((v2i64) bdata, 0, (v2i64) shuffle);
}

static force_inline v8i16
expand_pixel_8_1x128 (uint8_t data)
{
    v2i64 v_data, v_ret;

    v_ret  = __msa_fill_d (0);
    v_data = (v2i64)__msa_shf_h ((v8i16)unpack_32_1x128 ((uint32_t) data), _MSA_SHUFFLE (0, 0, 0, 0));
    v_ret  = __msa_insve_d (v_ret, 0, v_data);

    return (v8i16)v_ret;
}

static force_inline v8u16
pix_multiply_1x128 (v8i16 data,
		    v8i16 alpha)
{
    return (v8u16)msa_mul_u_h (
            __msa_adds_u_h ((v8u16)mask_0080, (v8u16)__msa_mulv_h (data, alpha)), 
            (v8u16)mask_0101);
}

static force_inline v16u8
pix_add_multiply_1x128 (void* src,
			void* alpha_dst,
			void* dst,
			void* alpha_src)
{
    v8i16 v_src, v_alpha_dst, v_dst, v_alpha_src;
    v8u16 t1, t2;

    v_src       = __msa_ld_h (src, 0);
    v_alpha_dst = __msa_ld_h (alpha_dst, 0);
    v_dst       = __msa_ld_h (dst, 0);
    v_alpha_src = __msa_ld_h (alpha_src, 0);

    t1 = pix_multiply_1x128 (v_src, v_alpha_dst);
    t2 = pix_multiply_1x128 (v_dst, v_alpha_src);

    return __msa_adds_u_b ((v16u8)t1, (v16u8)t2);
}

static force_inline v16u8
negate_1x128 (v16u8 data)
{
    return __msa_xor_v ((v16u8)data, (v16u8)mask_00ff);
}

static force_inline v8i16
invert_colors_1x128 (v8i16 data)
{
    v8i16 vecA = data;
    return (v8i16)__msa_insve_d ((v2i64) data, 0, 
                                 (v2i64) __msa_shf_h (vecA, _MSA_SHUFFLE (3, 0, 1, 2)));
}

static force_inline v16u8
over_1x128 (v16u8 src, v16u8 alpha, v16u8 dst)
{
    return __msa_adds_u_b (src, (v16u8)pix_multiply_1x128 ((v8i16)dst,
                                                            (v8i16)negate_1x128 (alpha)));
}

static force_inline v16u8
in_over_1x128 (void* src, void* alpha, void* mask, void* dst)
{
    v8i16 v_src, v_alpha, v_mask, v_dst;

    v_src = __msa_ld_h(src, 0);
    v_alpha = __msa_ld_h(alpha, 0);
    v_dst = __msa_ld_h(dst, 0);
    v_mask = __msa_ld_h(mask, 0);

    return over_1x128 ((v16u8)pix_multiply_1x128 (v_src, v_mask),
		       (v16u8)pix_multiply_1x128 (v_alpha, v_mask),
		       (v16u8)v_dst);
}

static force_inline v16u8
over_rev_non_pre_1x128 (v8i16 src, v8i16 dst)
{
    v8i16 alpha = expand_alpha_1x128 (src);

    return over_1x128 ((v16u8)pix_multiply_1x128 (invert_colors_1x128 (src),
					           (v8i16)__msa_or_v ((v16u8)alpha, (v16u8)mask_alpha)),
		       (v16u8)alpha,
		       (v16u8)dst);
}

static force_inline uint32_t
pack_1x128_32 (v8i16 data)
{
    return (uint32_t) __msa_copy_s_w ((v4i32)__msa_pckev_b (
                                                            __msa_fill_b (0), 
                                                            (v16i8)__msa_sat_u_h ((v8u16)data, 7)), 
                                      0);
}

static force_inline v16i8
expand565_16_1x128 (uint16_t pixel)
{
    v4i32 m;
    m = __msa_fill_w (0);

    __msa_insert_w (m, 0, (int32_t)pixel);

    m = unpack_565_to_8888 (m);

    return __msa_ilvr_b (__msa_fill_b (0), (v16i8)m);
}

static force_inline int32_t
core_combine_over_u_pixel_msa (uint32_t src, uint32_t dst)
{
    uint8_t a;
    v16i8 ws;

    a = src >> 24;

    if (a == 0xff)
    {
        return src;
    }
    else if (src)
    {
        ws = unpack_32_1x128 (src);
        return pack_1x128_32 (
            (v8i16)over_1x128 ( (v16u8)ws, 
                              (v16u8)expand_alpha_1x128 ((v8i16)ws),
                              (v16u8)unpack_32_1x128 (dst)));
    }

    return dst;
}

static force_inline uint32_t
combine1 (const uint32_t *pointer_source,
          const uint32_t *pointer_mask)
{
    uint32_t s;
    memcpy(&s, pointer_source, sizeof (uint32_t));

    if (pointer_mask)
    {
        v8i16 ms, mm;
        
        uint32_t pm;
        memcpy(&pm, pointer_mask, sizeof (uint32_t));

        mm = (v8i16)unpack_32_1x128 (pm);
        mm = expand_alpha_1x128 (mm);

        ms = (v8i16)unpack_32_1x128 (s);
        ms = (v8i16)pix_multiply_1x128 (ms, mm);

        s = pack_1x128_32 (ms);
    }

    return s;
}

static force_inline v16i8
combine4 (const void *ps, const void *pm)
{
    v8i16 w_src_lo, w_src_hi;
    v8i16 w_msk_lo, w_msk_hi;
    v16i8 s;

    if (pm)
    {
        w_msk_lo = (v8i16)load_128_unaligned (pm);

        if (is_transparent ( (v16i8)w_msk_lo))
        {
            return __msa_fill_b (0);
        }
    }

    s = (v16i8)load_128_unaligned (ps);

    if (pm)
    {
        unpack_128_2x128 (s, &w_src_lo, &w_src_hi);
        unpack_128_2x128 ((v16i8)w_msk_lo, &w_msk_lo, &w_msk_hi);

        expand_alpha_2x128 (w_msk_lo, w_msk_hi, &w_msk_lo, &w_msk_hi);

        pix_multiply_2x128 (&w_src_lo, &w_src_hi,
                            &w_msk_lo, &w_msk_hi,
                            &w_src_lo, &w_src_hi);

        s = pack_2x128_128 (w_src_lo, w_src_hi);
    }

    return s;
}


static force_inline void
core_combine_over_u_msa_mask (uint32_t *         pd,
                              const uint32_t *   ps,
                              const uint32_t *   pm,
                              int                w)
{
    int32_t s, d;

    /* Align dst on a 16-byte boundary */
    while (w && ((intptr_t) pd & 15))
    {
        d = *pd;
        s = combine1 (ps, pm);

        if (s)
            *pd = core_combine_over_u_pixel_msa (s, d);
        pd++;
        ps++;
        pm++;
        w--;
    }

    while (w >= 4)
    {
        v16i8 mask;
        mask = (v16i8)load_128_unaligned (pm);

        if (!is_zero (mask))
        {
            v4i32 src;
            v8i16 src_hi, src_lo;
            v8i16 mask_hi, mask_lo;
            v8i16 alpha_hi, alpha_lo;

            src = (v4i32)load_128_unaligned (ps);

            if (is_opaque ((v16i8)__msa_and_v ((v16u8)src, (v16u8)mask)))
            {
                save_128_aligned (pd, src);
            }
            else
            {
                v4i32 dst;
                v8i16 dst_hi, dst_lo;
                dst = load_128_aligned (pd);

                unpack_128_2x128 (mask, &mask_lo,  &mask_hi);
                unpack_128_2x128 ((v16i8)src,  &src_lo,  &src_hi);

                expand_alpha_2x128 (mask_lo, mask_hi, &mask_lo, &mask_hi);
                pix_multiply_2x128 (&src_lo, &src_hi,
                                    &mask_lo, &mask_hi,
                                    &src_lo, &src_hi);

                unpack_128_2x128 ((v16i8)dst, &dst_lo, &dst_hi);

                expand_alpha_2x128 (src_lo, src_hi,
                                    &alpha_lo, &alpha_hi);

                over_2x128 (&src_lo, &src_hi, &alpha_lo, &alpha_hi,
                            &dst_lo, &dst_hi);

                save_128_aligned (
                    pd,
                    (v4i32)pack_2x128_128 (dst_lo, dst_hi));
            }
        }

        pm += 4;
        ps += 4;
        pd += 4;
        w -= 4;
    }
    while (w)
    {
        d = *pd;
        s = combine1 (ps, pm);

        if (s)
            *pd = core_combine_over_u_pixel_msa (s, d);
        pd++;
        ps++;
        pm++;

        w--;
    }
}

static force_inline void
core_combine_over_u_msa_no_mask ( uint32_t *          pd,
                                  const uint32_t *    ps,
                                  int                 w)
{
    int32_t s, d;

    /* Align dst on a 16-byte boundary */
    while (w && ((intptr_t)pd & 15))
    {
        d = *pd;
        s = *ps;

        if (s)
            *pd = core_combine_over_u_pixel_msa (s, d);
        pd++;
        ps++;
        w--;
    }

    while (w >= 4)
    {
        v4i32 src;
        v8i16 src_hi, src_lo, dst_hi, dst_lo;
        v8i16 alpha_hi, alpha_lo;

        src = (v4i32)load_128_unaligned (ps);

        if (!is_zero ((v16i8)src))
        {
            if (is_opaque ((v16i8)src))
            {
                save_128_aligned (pd, src);
            }
            else
            {
                v4i32 dst;
                dst = load_128_aligned (pd);

                unpack_128_2x128 ((v16i8)src, &src_lo, &src_hi);
                unpack_128_2x128 ((v16i8)dst, &dst_lo, &dst_hi);

                expand_alpha_2x128 (src_lo, src_hi,
                                    &alpha_lo, &alpha_hi);
                over_2x128 (&src_lo, &src_hi, &alpha_lo, &alpha_hi,
                            &dst_lo, &dst_hi);

                save_128_aligned (
                    pd,
                    (v4i32)pack_2x128_128 (dst_lo, dst_hi));
            }
        }

        ps += 4;
        pd += 4;
        w -= 4;
    }
    while (w)
    {
        d = *pd;
        s = *ps;

        if (s)
            *pd = core_combine_over_u_pixel_msa (s, d);
        pd++;
        ps++;

        w--;
    }
}


static force_inline void
msa_combine_over_u (pixman_implementation_t *imp,
                     pixman_op_t             op,
                     uint32_t *              pd,
                     const uint32_t *        ps,
                     const uint32_t *        pm,
                     int                     w)
{
    if (pm)
        core_combine_over_u_msa_mask (pd, ps, pm, w);
    else
        core_combine_over_u_msa_no_mask (pd, ps, w);
}

static void
msa_combine_over_reverse_u (pixman_implementation_t *imp,
                             pixman_op_t             op,
                             uint32_t *              pd,
                             const uint32_t *        ps,
                             const uint32_t *        pm,
                             int                     w)
{
    int32_t s, d;

    v8i16 w_dst_lo, w_dst_hi;
    v8i16 w_src_lo, w_src_hi;
    v8i16 w_alpha_lo, w_alpha_hi;

    /* Align dst on a 16-byte boundary */
    while (w &&
           ((intptr_t)pd & 15))
    {
        d = *pd;
        s = combine1 (ps, pm);

        *pd++ = core_combine_over_u_pixel_msa (d, s);
        w--;
        ps++;
        if (pm)
            pm++;
    }

    while (w >= 4)
    {
        /* I'm loading unaligned because I'm not sure
         * about the address alignment.
         */
        w_src_hi = (v8i16)combine4 (ps, pm);
        w_dst_hi = (v8i16)load_128_aligned (pd);

        unpack_128_2x128 ((v16i8)w_src_hi, &w_src_lo, &w_src_hi);
        unpack_128_2x128 ((v16i8)w_dst_hi, &w_dst_lo, &w_dst_hi);

        expand_alpha_2x128 (w_dst_lo, w_dst_hi,
                            &w_alpha_lo, &w_alpha_hi);

        over_2x128 (&w_dst_lo, &w_dst_hi,
                    &w_alpha_lo, &w_alpha_hi,
                    &w_src_lo, &w_src_hi);

        /* rebuid the 4 pixel data and save*/
        save_128_aligned (pd,
                          (v4i32)pack_2x128_128 (w_src_lo, w_src_hi));

        w -= 4;
        ps += 4;
        pd += 4;

        if (pm)
            pm += 4;
    }

    while (w)
    {
        d = *pd;
        s = combine1 (ps, pm);

        *pd++ = core_combine_over_u_pixel_msa (d, s);
        ps++;
        w--;
        if (pm)
            pm++;
    }
}

static force_inline uint32_t
core_combine_in_u_pixel_msa (uint32_t src, uint32_t dst)
{
    int32_t maska = src >> 24;

    if (maska == 0)
    {
        return 0;
    }
    else if (maska != 0xff)
    {
        return pack_1x128_32 (
            (v8i16)pix_multiply_1x128 ((v8i16)unpack_32_1x128 (dst),
                                        expand_alpha_1x128 ((v8i16)unpack_32_1x128 (src))));
    }

    return dst;
}

static void
msa_combine_in_u (pixman_implementation_t *imp,
                   pixman_op_t             op,
                   uint32_t *              pd,
                   const uint32_t *        ps,
                   const uint32_t *        pm,
                   int                     w)
{
    int32_t s, d;

    v8i16 w_src_lo, w_src_hi;
    v8i16 w_dst_lo, w_dst_hi;

    while (w && ((intptr_t)pd & 15))
    {
        s = combine1 (ps, pm);
        d = *pd;

        *pd++ = core_combine_in_u_pixel_msa (d, s);
        w--;
        ps++;
        if (pm)
            pm++;
    }

    while (w >= 4)
    {
        w_dst_hi = (v8i16)load_128_aligned (pd);
        w_src_hi = (v8i16)combine4 (ps, pm);

        unpack_128_2x128 ((v16i8)w_dst_hi, &w_dst_lo, &w_dst_hi);
        expand_alpha_2x128 (w_dst_lo, w_dst_hi, &w_dst_lo, &w_dst_hi);

        unpack_128_2x128 ((v16i8)w_src_hi,  &w_src_lo, &w_src_hi);
        pix_multiply_2x128 (&w_src_lo, &w_src_hi,
                            &w_dst_lo, &w_dst_hi,
                            &w_dst_lo, &w_dst_hi);

        save_128_aligned (pd,
                          (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));

        ps += 4;
        pd += 4;
        w -= 4;
        if (pm)
            pm += 4;
    }

    while (w)
    {
        s = combine1 (ps, pm);
        d = *pd;

        *pd++ = core_combine_in_u_pixel_msa (d, s);
        w--;
        ps++;
        if (pm)
            pm++;
    }
}

static void
msa_combine_in_reverse_u (pixman_implementation_t *imp,
                          pixman_op_t             op,
                          uint32_t *              pd,
                          const uint32_t *        ps,
                          const uint32_t *        pm,
                          int                     w)
{
    int32_t s, d;

    v8i16 w_src_lo, w_src_hi;
    v8i16 w_dst_lo, w_dst_hi;

    while (w && ((intptr_t)pd & 15))
    {
        s = combine1 (ps, pm);
        d = *pd;

        *pd++ = core_combine_in_u_pixel_msa (s, d);
        ps++;
        w--;
        if (pm)
            pm++;
    }

    while (w >= 4)
    {
        w_dst_hi = (v8i16)load_128_aligned (pd);
        w_src_hi = (v8i16)combine4 (ps, pm);

        unpack_128_2x128 ( (v16i8)w_src_hi, &w_src_lo, &w_src_hi);
        expand_alpha_2x128 (w_src_lo, w_src_hi, &w_src_lo, &w_src_hi);

        unpack_128_2x128 ( (v16i8)w_dst_hi, &w_dst_lo, &w_dst_hi);
        pix_multiply_2x128 (&w_dst_lo, &w_dst_hi,
                            &w_src_lo, &w_src_hi,
                            &w_dst_lo, &w_dst_hi);

        save_128_aligned (
            pd, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));

        ps += 4;
        pd += 4;
        w -= 4;
        if (pm)
            pm += 4;
    }

    while (w)
    {
        s = combine1 (ps, pm);
        d = *pd;

        *pd++ = core_combine_in_u_pixel_msa (s, d);
        w--;
        ps++;
        if (pm)
            pm++;
    }
}

static void
msa_combine_out_reverse_u (pixman_implementation_t *imp,
                           pixman_op_t              op,
                           uint32_t *               pd,
                           const uint32_t *         ps,
                           const uint32_t *         pm,
                           int                      w)
{
    while (w && ((uintptr_t)pd & 15))
    {
	uint32_t s = combine1 (ps, pm);
	uint32_t d = *pd;

	*pd++ = pack_1x128_32 (
	    (v8i16)pix_multiply_1x128 (
		(v8i16)unpack_32_1x128 (d), (v8i16)negate_1x128 (
		    (v16u8)expand_alpha_1x128 ((v8i16)unpack_32_1x128 (s)))));

	if (pm)
	    pm++;
	ps++;
	w--;
    }

    while (w >= 4)
    {
	v8i16 w_src_lo, w_src_hi;
	v8i16 w_dst_lo, w_dst_hi;

	w_src_hi = (v8i16)combine4 (ps, pm);
	w_dst_hi = (v8i16)load_128_aligned (pd);

	unpack_128_2x128 ((v16i8)w_src_hi, &w_src_lo, &w_src_hi);
	unpack_128_2x128 ((v16i8)w_dst_hi, &w_dst_lo, &w_dst_hi);

	expand_alpha_2x128 (w_src_lo, w_src_hi, &w_src_lo, &w_src_hi);
	negate_2x128       (w_src_lo, w_src_hi, &w_src_lo, &w_src_hi);

	pix_multiply_2x128 (&w_dst_lo, &w_dst_hi,
			    &w_src_lo, &w_src_hi,
			    &w_dst_lo, &w_dst_hi);

	save_128_aligned (
	    (int32_t*) pd, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));

	ps += 4;
	pd += 4;
	if (pm)
	    pm += 4;

	w -= 4;
    }

    while (w)
    {
	uint32_t s = combine1 (ps, pm);
	uint32_t d = *pd;

	*pd++ = pack_1x128_32 (
	    (v8i16)pix_multiply_1x128 (
		(v8i16)unpack_32_1x128 (d), (v8i16)negate_1x128 (
		    (v16u8)expand_alpha_1x128 ((v8i16)unpack_32_1x128 (s)))));
	ps++;
	if (pm)
	    pm++;
	w--;
    }
}

static void
msa_combine_out_u (pixman_implementation_t *imp,
                    pixman_op_t             op,
                    uint32_t *              pd,
                    const uint32_t *        ps,
                    const uint32_t *        pm,
                    int                     w)
{
    while (w && ((uintptr_t)pd & 15))
    {
	uint32_t s = combine1 (ps, pm);
	uint32_t d = *pd;

	*pd++ = pack_1x128_32 (
	    (v8i16)pix_multiply_1x128 (
		(v8i16)unpack_32_1x128 (s), (v8i16)negate_1x128 (
		    (v16u8)expand_alpha_1x128 ((v8i16)unpack_32_1x128 (d)))));
	w--;
	ps++;
	if (pm)
	    pm++;
    }

    while (w >= 4)
    {
	v8i16 w_src_lo, w_src_hi;
	v8i16 w_dst_lo, w_dst_hi;

	w_src_hi = (v8i16)combine4 (ps, pm);
	w_dst_hi = (v8i16)load_128_aligned ((int32_t*) pd);

	unpack_128_2x128 ((v16i8)w_src_hi, &w_src_lo, &w_src_hi);
	unpack_128_2x128 ((v16i8)w_dst_hi, &w_dst_lo, &w_dst_hi);

	expand_alpha_2x128 (w_dst_lo, w_dst_hi, &w_dst_lo, &w_dst_hi);
	negate_2x128       (w_dst_lo, w_dst_hi, &w_dst_lo, &w_dst_hi);

	pix_multiply_2x128 (&w_src_lo, &w_src_hi,
			    &w_dst_lo, &w_dst_hi,
			    &w_dst_lo, &w_dst_hi);

	save_128_aligned (
	    (int32_t*) pd, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));

	ps += 4;
	pd += 4;
	w -= 4;
	if (pm)
	    pm += 4;
    }

    while (w)
    {
	uint32_t s = combine1 (ps, pm);
	uint32_t d = *pd;

	*pd++ = pack_1x128_32 (
	    (v8i16)pix_multiply_1x128 (
		(v8i16)unpack_32_1x128 (s), (v8i16)negate_1x128 (
		    (v16u8)expand_alpha_1x128 ((v8i16)unpack_32_1x128 (d)))));
	w--;
	ps++;
	if (pm)
	    pm++;
    }
}

static force_inline uint32_t
core_combine_atop_u_pixel_msa (uint32_t src,
                               uint32_t dst)
{
    v16i8 s, d;
    v8i16 sa, da;

    s = unpack_32_1x128 (src);
    d = unpack_32_1x128 (dst);

    sa = (v8i16)negate_1x128 ((v16u8)expand_alpha_1x128 ((v8i16)s));
    da = expand_alpha_1x128 ((v8i16)d);

    return pack_1x128_32 ((v8i16)pix_add_multiply_1x128 (&s, &da, &d, &sa));
}

static void
msa_combine_atop_u (pixman_implementation_t *imp,
                     pixman_op_t             op,
                     uint32_t *              pd,
                     const uint32_t *        ps,
                     const uint32_t *        pm,
                     int                     w)
{
    uint32_t s, d;

    v8i16 w_src_lo, w_src_hi;
    v8i16 w_dst_lo, w_dst_hi;
    v8i16 w_alpha_src_lo, w_alpha_src_hi;
    v8i16 w_alpha_dst_lo, w_alpha_dst_hi;

    while (w && ((uintptr_t)pd & 15))
    {
	s = combine1 (ps, pm);
	d = *pd;

	*pd++ = core_combine_atop_u_pixel_msa (s, d);
	w--;
	ps++;
	if (pm)
	    pm++;
    }

    while (w >= 4)
    {
	w_src_hi = (v8i16)combine4 (ps, pm);
	w_dst_hi = (v8i16)load_128_aligned ((int32_t*) pd);

	unpack_128_2x128 ((v16i8)w_src_hi, &w_src_lo, &w_src_hi);
	unpack_128_2x128 ((v16i8)w_dst_hi, &w_dst_lo, &w_dst_hi);

	expand_alpha_2x128 (w_src_lo, w_src_hi,
			    &w_alpha_src_lo, &w_alpha_src_hi);
	expand_alpha_2x128 (w_dst_lo, w_dst_hi,
			    &w_alpha_dst_lo, &w_alpha_dst_hi);

	negate_2x128 (w_alpha_src_lo, w_alpha_src_hi,
		      &w_alpha_src_lo, &w_alpha_src_hi);

	pix_add_multiply_2x128 (
	    &w_src_lo, &w_src_hi, &w_alpha_dst_lo, &w_alpha_dst_hi,
	    &w_dst_lo, &w_dst_hi, &w_alpha_src_lo, &w_alpha_src_hi,
	    &w_dst_lo, &w_dst_hi);

	save_128_aligned (
	    (int32_t*) pd, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));

	ps += 4;
	pd += 4;
	w -= 4;
	if (pm)
	    pm += 4;
    }

    while (w)
    {
	s = combine1 (ps, pm);
	d = *pd;

	*pd++ = core_combine_atop_u_pixel_msa (s, d);
	w--;
	ps++;
	if (pm)
	    pm++;
    }
}

static force_inline uint32_t
core_combine_reverse_atop_u_pixel_msa (uint32_t src,
                                       uint32_t dst)
{
    v8i16 s, d;
    v8i16 sa, da;

    s = (v8i16)unpack_32_1x128 (src);
    d = (v8i16)unpack_32_1x128 (dst);

    sa = expand_alpha_1x128 (s);
    da = (v8i16)negate_1x128 ((v16u8)expand_alpha_1x128 (d));

    return pack_1x128_32 ((v8i16)pix_add_multiply_1x128 (&s, &da, &d, &sa));
}

static void
msa_combine_atop_reverse_u (pixman_implementation_t *imp,
                             pixman_op_t             op,
                             uint32_t *              pd,
                             const uint32_t *        ps,
                             const uint32_t *        pm,
                             int                     w)
{
    uint32_t s, d;

    v8i16 w_src_lo, w_src_hi;
    v8i16 w_dst_lo, w_dst_hi;
    v8i16 w_alpha_src_lo, w_alpha_src_hi;
    v8i16 w_alpha_dst_lo, w_alpha_dst_hi;

    while (w && ((uintptr_t)pd & 15))
    {
	s = combine1 (ps, pm);
	d = *pd;

	*pd++ = core_combine_reverse_atop_u_pixel_msa (s, d);
	ps++;
	w--;
	if (pm)
	    pm++;
    }

    while (w >= 4)
    {
	w_src_hi = (v8i16)combine4 (ps, pm);
	w_dst_hi = (v8i16)load_128_aligned ((int32_t*) pd);

	unpack_128_2x128 ((v16i8)w_src_hi, &w_src_lo, &w_src_hi);
	unpack_128_2x128 ((v16i8)w_dst_hi, &w_dst_lo, &w_dst_hi);

	expand_alpha_2x128 (w_src_lo, w_src_hi,
			    &w_alpha_src_lo, &w_alpha_src_hi);
	expand_alpha_2x128 (w_dst_lo, w_dst_hi,
			    &w_alpha_dst_lo, &w_alpha_dst_hi);

	negate_2x128 (w_alpha_dst_lo, w_alpha_dst_hi,
		      &w_alpha_dst_lo, &w_alpha_dst_hi);

	pix_add_multiply_2x128 (
	    &w_src_lo, &w_src_hi, &w_alpha_dst_lo, &w_alpha_dst_hi,
	    &w_dst_lo, &w_dst_hi, &w_alpha_src_lo, &w_alpha_src_hi,
	    &w_dst_lo, &w_dst_hi);

	save_128_aligned (
	    (int32_t*) pd, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));

	ps += 4;
	pd += 4;
	w -= 4;
	if (pm)
	    pm += 4;
    }

    while (w)
    {
	s = combine1 (ps, pm);
	d = *pd;

	*pd++ = core_combine_reverse_atop_u_pixel_msa (s, d);
	ps++;
	w--;
	if (pm)
	    pm++;
    }
}

static force_inline uint32_t
core_combine_xor_u_pixel_msa (uint32_t src,
                               uint32_t dst)
{
    v8i16 s, d;
    v8i16 neg_s, neg_d;
    s = (v8i16)unpack_32_1x128 (src);
    d = (v8i16)unpack_32_1x128 (dst);

    neg_d = (v8i16)negate_1x128 ((v16u8)expand_alpha_1x128 (d));
    neg_s = (v8i16)negate_1x128 ((v16u8)expand_alpha_1x128 (s));

    return pack_1x128_32 ((v8i16)pix_add_multiply_1x128 (&s, &neg_d, &d, &neg_s));
}

static void
msa_combine_xor_u (pixman_implementation_t *imp,
                    pixman_op_t             op,
                    uint32_t *              dst,
                    const uint32_t *        src,
                    const uint32_t *        mask,
                    int                     width)
{
    int w = width;
    uint32_t s, d;
    uint32_t* pd = dst;
    const uint32_t* ps = src;
    const uint32_t* pm = mask;

    v8i16 w_src, w_src_lo, w_src_hi;
    v8i16 w_dst, w_dst_lo, w_dst_hi;
    v8i16 w_alpha_src_lo, w_alpha_src_hi;
    v8i16 w_alpha_dst_lo, w_alpha_dst_hi;

    while (w && ((uintptr_t)pd & 15))
    {
	s = combine1 (ps, pm);
	d = *pd;

	*pd++ = core_combine_xor_u_pixel_msa (s, d);
	w--;
	ps++;
	if (pm)
	    pm++;
    }

    while (w >= 4)
    {
	w_src = (v8i16)combine4 (ps, pm);
	w_dst = (v8i16)load_128_aligned ((int32_t*) pd);

	unpack_128_2x128 ((v16i8)w_src, &w_src_lo, &w_src_hi);
	unpack_128_2x128 ((v16i8)w_dst, &w_dst_lo, &w_dst_hi);

	expand_alpha_2x128 (w_src_lo, w_src_hi,
			    &w_alpha_src_lo, &w_alpha_src_hi);
	expand_alpha_2x128 (w_dst_lo, w_dst_hi,
			    &w_alpha_dst_lo, &w_alpha_dst_hi);

	negate_2x128 (w_alpha_src_lo, w_alpha_src_hi,
		      &w_alpha_src_lo, &w_alpha_src_hi);
	negate_2x128 (w_alpha_dst_lo, w_alpha_dst_hi,
		      &w_alpha_dst_lo, &w_alpha_dst_hi);

	pix_add_multiply_2x128 (
	    &w_src_lo, &w_src_hi, &w_alpha_dst_lo, &w_alpha_dst_hi,
	    &w_dst_lo, &w_dst_hi, &w_alpha_src_lo, &w_alpha_src_hi,
	    &w_dst_lo, &w_dst_hi);

	save_128_aligned (
	    (int32_t*)pd, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));

	ps += 4;
	pd += 4;
	w -= 4;
	if (pm)
	    pm += 4;
    }

    while (w)
    {
	s = combine1 (ps, pm);
	d = *pd;

	*pd++ = core_combine_xor_u_pixel_msa (s, d);
	w--;
	ps++;
	if (pm)
	    pm++;
    }
}

static force_inline void
msa_combine_add_u (pixman_implementation_t *imp,
                    pixman_op_t             op,
                    uint32_t *              dst,
                    const uint32_t *        src,
                    const uint32_t *        mask,
                    int                     width)
{
    int w = width;
    uint32_t s, d;
    uint32_t* pd = dst;
    const uint32_t* ps = src;
    const uint32_t* pm = mask;

    while (w && (uintptr_t)pd & 15)
    {
        v4i32 v_s, v_d;
	s = combine1 (ps, pm);
	d = *pd;

	ps++;
	if (pm)
	    pm++;
	*pd++ = __msa_copy_s_w (
	    (v4i32)__msa_adds_u_b ((v16u8)__msa_insert_w (v_s, 0, s), (v16u8)__msa_insert_w (v_d, 0, d)), 0);
	w--;
    }

    while (w >= 4)
    {
	v4i32 s;

	s = (v4i32)combine4 (ps, pm);

	save_128_aligned (
	    (int32_t*)pd, (v4i32)__msa_adds_u_b ((v16u8)s, (v16u8)load_128_aligned ((int32_t*)pd)));

	pd += 4;
	ps += 4;
	if (pm)
	    pm += 4;
	w -= 4;
    }

    while (w--)
    {
        v4i32 v_s, v_d;
	s = combine1 (ps, pm);
	d = *pd;

	ps++;
	*pd++ = __msa_copy_s_w (
	    (v4i32)__msa_adds_u_b ((v16u8)__msa_insert_w (v_s, 0, s), (v16u8)__msa_insert_w (v_d, 0, d)), 0);
	if (pm)
	    pm++;
    }
}

static force_inline uint32_t
core_combine_saturate_u_pixel_msa (uint32_t src,
                                   uint32_t dst)
{
    v8i16 ms, md;
    uint32_t sa, da;

    ms = (v8i16)unpack_32_1x128 (src);
    md = (v8i16)unpack_32_1x128 (dst);
    sa = src >> 24;
    da = ~dst >> 24;

    if (sa > da)
    {
	ms = (v8i16)pix_multiply_1x128 (
	    ms, expand_alpha_1x128 ((v8i16)unpack_32_1x128 (DIV_UN8 (da, sa) << 24)));
    }

    return pack_1x128_32 ((v8i16)__msa_adds_u_h ((v8u16)md, (v8u16)ms));
}

static void
msa_combine_saturate_u (pixman_implementation_t *imp,
                         pixman_op_t             op,
                         uint32_t *              pd,
                         const uint32_t *        ps,
                         const uint32_t *        pm,
                         int                     w)
{
    int32_t s, d;

    uint32_t pack_cmp;
    v8i16 w_src, w_dst;

    pack_cmp = 0;

    while (w && (intptr_t)pd & 15)
    {
        s = combine1 (ps, pm);
        d = *pd;

        *pd++ = core_combine_saturate_u_pixel_msa (s, d);
        w--;
        ps++;
        if (pm)
            pm++;
    }

    while (w >= 4)
    {
        v16i8 cmp_mask;
        uint64_t cmp_mask_low, cmp_mask_high; 

        w_dst = (v8i16)load_128_aligned (pd);
        w_src = (v8i16)combine4 (ps, pm);   

        cmp_mask = __msa_srli_b((v16i8)__msa_clt_s_w(
                                          __msa_srli_w((v4i32)__msa_xor_v((v16u8)w_dst, (v16u8)mask_ff000000), 24),
                                          __msa_srli_w((v4i32)w_src, 24)),
                                      7);
        /* simulates _mm_movemask_epi8 */
        cmp_mask_low  = __msa_copy_u_d((v2i64) cmp_mask, 0);
        cmp_mask_high = __msa_copy_u_d((v2i64) cmp_mask, 1);
        pack_cmp = 0;

        // Extract the lower 8 bytes
        for (int i = 0; i < 8; i++)
        {
            pack_cmp |= ((cmp_mask_low >> (i * 8)) & 0x1) << i;
        }

        // Extract the higher 8 bytes
        for (int i = 0; i < 8; i++)
        {
            pack_cmp |= ((cmp_mask_high >> (i * 8)) & 0x1) << (i + 8);
        }

        /* if some alpha src is grater than respective ~alpha dst */
        if (pack_cmp)
        {
            s = combine1 (ps++, pm);
            d = *pd;
            *pd++ = core_combine_saturate_u_pixel_msa (s, d);
            if (pm)
                pm++;

            s = combine1 (ps++, pm);
            d = *pd;
            *pd++ = core_combine_saturate_u_pixel_msa (s, d);
            if (pm)
                pm++;

            s = combine1 (ps++, pm);
            d = *pd;
            *pd++ = core_combine_saturate_u_pixel_msa (s, d);
            if (pm)
                pm++;

            s = combine1 (ps++, pm);
            d = *pd;
            *pd++ = core_combine_saturate_u_pixel_msa (s, d);
            if (pm)
                pm++;
        }
        else
        {
            save_128_aligned (pd, (v4i32)__msa_adds_s_b ((v16i8)w_dst, (v16i8)w_src));

            pd += 4;
            ps += 4;
            if (pm)
                pm += 4;
        }

        w -= 4;
    }

    while (w--)
    {
        s = combine1 (ps, pm);
        d = *pd;

        *pd++ = core_combine_saturate_u_pixel_msa (s, d);
        ps++;
        if (pm)
            pm++;
    }
}


static void
msa_combine_src_ca (pixman_implementation_t *imp,
                     pixman_op_t             op,
                     uint32_t *              pd,
                     const uint32_t *        ps,
                     const uint32_t *        pm,
                     int                     w)
{
    uint32_t s, m;

    v8i16 w_src_lo, w_src_hi;
    v8i16 w_mask_lo, w_mask_hi;
    v8i16 w_dst_lo, w_dst_hi;

    while (w && (intptr_t)pd & 15)
    {
        s = *ps++;
        m = *pm++;
        *pd++ = pack_1x128_32 ( (v8i16)
            pix_multiply_1x128 ((v8i16)unpack_32_1x128 (s), (v8i16)unpack_32_1x128 (m)));
        w--;
    }

    while (w >= 4)
    {
        w_src_hi  = (v8i16)load_128_unaligned (ps);
        w_mask_hi = (v8i16)load_128_unaligned (pm);

        unpack_128_2x128 ((v16i8)w_src_hi,  &w_src_lo, &w_src_hi);
        unpack_128_2x128 ((v16i8)w_mask_hi,  &w_mask_lo, &w_mask_hi);

        pix_multiply_2x128 (&w_src_lo, &w_src_hi,
                            &w_mask_lo, &w_mask_hi,
                            &w_dst_lo, &w_dst_hi);

        save_128_aligned (
            pd, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));

        ps += 4;
        pd += 4;
        pm += 4;
        w -= 4;
    }

    while (w)
    {
        s = *ps++;
        m = *pm++;
        *pd++ = pack_1x128_32 ( (v8i16)
            pix_multiply_1x128 ((v8i16)unpack_32_1x128 (s),
            (v8i16)unpack_32_1x128 (m)));
        w--;
    }
}


static force_inline uint32_t
core_combine_over_ca_pixel_msa (uint32_t src,
                                uint32_t mask,
                                uint32_t dst)
{
    v8i16 s, expAlpha, unpk_mask, unpk_dst;

    s         = (v8i16)unpack_32_1x128 (src);
    expAlpha  = expand_alpha_1x128 (s);
    unpk_mask = (v8i16)unpack_32_1x128 (mask);
    unpk_dst  = (v8i16)unpack_32_1x128 (dst);

    return pack_1x128_32 ((v8i16)in_over_1x128 (&s, &expAlpha, &unpk_mask, &unpk_dst));
}

static void
msa_combine_over_ca (pixman_implementation_t *imp,
                      pixman_op_t             op,
                      uint32_t *              pd,
                      const uint32_t *        ps,
                      const uint32_t *        pm,
                      int                     w)
{
    uint32_t s, m, d;

    v8i16 w_alpha_lo, w_alpha_hi;
    v8i16 w_src_lo, w_src_hi;
    v8i16 w_dst_lo, w_dst_hi;
    v8i16 w_mask_lo, w_mask_hi;

    while (w && (intptr_t)pd & 15)
    {
        s = *ps++;
        m = *pm++;
        d = *pd;

        *pd++ = core_combine_over_ca_pixel_msa (s, m, d);
        w--;
    }

    while (w >= 4)
    {
        w_dst_hi  = (v8i16)load_128_aligned   (pd);
        w_src_hi  = (v8i16)load_128_unaligned (ps);
        w_mask_hi = (v8i16)load_128_unaligned (pm);

        unpack_128_2x128 ((v16i8)w_dst_hi, &w_dst_lo, &w_dst_hi);
        unpack_128_2x128 ((v16i8)w_src_hi, &w_src_lo, &w_src_hi);
        unpack_128_2x128 ((v16i8)w_mask_hi,&w_mask_lo, &w_mask_hi);

        expand_alpha_2x128 (w_src_lo, w_src_hi,
                            &w_alpha_lo, &w_alpha_hi);

        in_over_2x128 (&w_src_lo, &w_src_hi,
                       &w_alpha_lo, &w_alpha_hi,
                       &w_mask_lo, &w_mask_hi,
                       &w_dst_lo, &w_dst_hi);

        save_128_aligned (
            pd, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));

        ps += 4;
        pd += 4;
        pm += 4;
        w -= 4;
    }

    while (w)
    {
        s = *ps++;
        m = *pm++;
        d = *pd;

        *pd++ = core_combine_over_ca_pixel_msa (s, m, d);
        w--;
    }
}


static force_inline uint32_t
core_combine_over_reverse_ca_pixel_msa (uint32_t src,
                                        uint32_t mask,
                                        uint32_t dst)
{
    v16u8 d;
    
    d = (v16u8)unpack_32_1x128 (dst);

    return pack_1x128_32 ( (v8i16)
        over_1x128 (d, (v16u8)expand_alpha_1x128 ((v8i16)d),
                    (v16u8)pix_multiply_1x128 ((v8i16)unpack_32_1x128 (src),
                                               (v8i16)unpack_32_1x128 (mask))));
}

static void
msa_combine_over_reverse_ca (pixman_implementation_t *imp,
                              pixman_op_t             op,
                              uint32_t *              pd,
                              const uint32_t *        ps,
                              const uint32_t *        pm,
                              int                     w)
{
    int32_t s, m, d;

    v8i16 w_alpha_lo, w_alpha_hi;
    v8i16 w_src_lo, w_src_hi;
    v8i16 w_dst_lo, w_dst_hi;
    v8i16 w_mask_lo, w_mask_hi;

    while (w && (intptr_t)pd & 15)
    {
        s = *ps++;
        m = *pm++;
        d = *pd;

        *pd++ = core_combine_over_reverse_ca_pixel_msa (s, m, d);
        w--;
    }

    while (w >= 4)
    {
        w_dst_hi  = (v8i16)load_128_aligned   (pd);
        w_src_hi  = (v8i16)load_128_unaligned (ps);
        w_mask_hi = (v8i16)load_128_unaligned (pm);

        unpack_128_2x128 ((v16i8)w_dst_hi,  &w_dst_lo, &w_dst_hi);
        unpack_128_2x128 ((v16i8)w_src_hi,  &w_src_lo, &w_src_hi);
        unpack_128_2x128 ((v16i8)w_mask_hi, &w_mask_lo, &w_mask_hi);

        expand_alpha_2x128 (w_dst_lo, w_dst_hi,
                            &w_alpha_lo,  &w_alpha_hi);
        pix_multiply_2x128 (&w_src_lo, &w_src_hi,
                            &w_mask_lo,  &w_mask_hi,
                            &w_mask_lo,  &w_mask_hi);

        over_2x128 (&w_dst_lo, &w_dst_hi,
                    &w_alpha_lo, &w_alpha_hi,
                    &w_mask_lo, &w_mask_hi);

        save_128_aligned (
            pd, (v4i32)pack_2x128_128 (w_mask_lo, w_mask_hi));

        ps += 4;
        pd += 4;
        pm += 4;
        w -= 4;
    }

    while (w)
    {
        s = *ps++;
        m = *pm++;
        d = *pd;

        *pd++ = core_combine_over_reverse_ca_pixel_msa (s, m, d);
        w--;
    }
}


static void
msa_combine_in_ca (pixman_implementation_t *imp,
                    pixman_op_t              op,
                    uint32_t *               pd,
                    const uint32_t *         ps,
                    const uint32_t *         pm,
                    int                      w)
{
    uint32_t s, m, d;

    v8i16 w_alpha_lo, w_alpha_hi;
    v8i16 w_src_lo, w_src_hi;
    v8i16 w_dst_lo, w_dst_hi;
    v8i16 w_mask_lo, w_mask_hi;

    while (w && (intptr_t)pd & 15)
    {
        s = *ps++;
        m = *pm++;
        d = *pd;

        *pd++ = pack_1x128_32 ( 
            (v8i16)pix_multiply_1x128 (
                (v8i16)pix_multiply_1x128 ((v8i16)unpack_32_1x128 (s), (v8i16)unpack_32_1x128 (m)),
                expand_alpha_1x128 ((v8i16)unpack_32_1x128 (d))));

        w--;
    }

    while (w >= 4)
    {
        w_dst_hi  = (v8i16)load_128_aligned   (pd);
        w_src_hi  = (v8i16)load_128_unaligned (ps);
        w_mask_hi = (v8i16)load_128_unaligned (pm);

        unpack_128_2x128 ((v16i8)w_dst_hi,  &w_dst_lo, &w_dst_hi);
        unpack_128_2x128 ((v16i8)w_src_hi,  &w_src_lo, &w_src_hi);
        unpack_128_2x128 ((v16i8)w_mask_hi, &w_mask_lo, &w_mask_hi);

        expand_alpha_2x128 (w_dst_lo, w_dst_hi,
                            &w_alpha_lo, &w_alpha_hi);

        pix_multiply_2x128 (&w_src_lo, &w_src_hi,
                            &w_mask_lo, &w_mask_hi,
                            &w_dst_lo, &w_dst_hi);

        pix_multiply_2x128 (&w_dst_lo, &w_dst_hi,
                            &w_alpha_lo, &w_alpha_hi,
                            &w_dst_lo, &w_dst_hi);

        save_128_aligned (
            pd, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));

        ps += 4;
        pd += 4;
        pm += 4;
        w  -= 4;
    }

    while (w)
    {
        s = *ps++;
        m = *pm++;
        d = *pd;

        *pd++ = pack_1x128_32 (
            (v8i16)pix_multiply_1x128 (
                (v8i16)pix_multiply_1x128 ((v8i16)unpack_32_1x128 (s), (v8i16)unpack_32_1x128 (m)),
                expand_alpha_1x128 ((v8i16)unpack_32_1x128 (d))));

        w--;
    }
}

static void
msa_combine_in_reverse_ca (pixman_implementation_t *imp,
                            pixman_op_t              op,
                            uint32_t *               pd,
                            const uint32_t *         ps,
                            const uint32_t *         pm,
                            int                      w)
{
    uint32_t s, m, d;

    v8i16 w_alpha_lo, w_alpha_hi;
    v8i16 w_src_lo, w_src_hi;
    v8i16 w_dst_lo, w_dst_hi;
    v8i16 w_mask_lo, w_mask_hi;

    while (w && (intptr_t)pd & 15)
    {
        s = *ps++;
        m = *pm++;
        d = *pd;

        *pd++ = pack_1x128_32 (
            (v8i16)pix_multiply_1x128 ((v8i16)unpack_32_1x128 (d),
                                       (v8i16)pix_multiply_1x128 ((v8i16)unpack_32_1x128 (m),
                                            expand_alpha_1x128 ((v8i16)unpack_32_1x128 (s)))));
        w--;
    }

    while (w >= 4)
    {
        w_dst_hi  = (v8i16)load_128_aligned   (pd);
        w_src_hi  = (v8i16)load_128_unaligned (ps);
        w_mask_hi = (v8i16)load_128_unaligned (pm);

        unpack_128_2x128 ((v16i8)w_dst_hi,  &w_dst_lo, &w_dst_hi);
        unpack_128_2x128 ((v16i8)w_src_hi,  &w_src_lo, &w_src_hi);
        unpack_128_2x128 ((v16i8)w_mask_hi, &w_mask_lo,  &w_mask_hi);

        expand_alpha_2x128 (w_src_lo, w_src_hi,
                            &w_alpha_lo,  &w_alpha_hi);
        pix_multiply_2x128 (&w_mask_lo, &w_mask_hi,
                            &w_alpha_lo,  &w_alpha_hi,
                            &w_alpha_lo,  &w_alpha_hi);

        pix_multiply_2x128 (&w_dst_lo, &w_dst_hi,
                            &w_alpha_lo, &w_alpha_hi,
                            &w_dst_lo, &w_dst_hi);

        save_128_aligned (
            pd, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));

        ps += 4;
        pd += 4;
        pm += 4;
        w -= 4;
    }

    while (w)
    {
        s = *ps++;
        m = *pm++;
        d = *pd;

        *pd++ = pack_1x128_32 (
            (v8i16)pix_multiply_1x128 ((v8i16)unpack_32_1x128 (d),
                                       (v8i16)pix_multiply_1x128 ((v8i16)unpack_32_1x128 (m),
                                            expand_alpha_1x128 ((v8i16)unpack_32_1x128 (s)))));
        w--;
    }
}

static void
msa_combine_out_ca (pixman_implementation_t *imp,
                     pixman_op_t              op,
                     uint32_t *               pd,
                     const uint32_t *         ps,
                     const uint32_t *         pm,
                     int                      w)
{
    uint32_t s, m, d;

    v8i16 w_alpha_lo, w_alpha_hi;
    v8i16 w_src_lo, w_src_hi;
    v8i16 w_dst_lo, w_dst_hi;
    v8i16 w_mask_lo, w_mask_hi;

    while (w && (intptr_t)pd & 15)
    {
        s = *ps++;
        m = *pm++;
        d = *pd;

        *pd++ = pack_1x128_32 (
            (v8i16)pix_multiply_1x128 (
                (v8i16)pix_multiply_1x128 (
                (v8i16)unpack_32_1x128 (s), (v8i16)unpack_32_1x128 (m)),
                (v8i16)negate_1x128 ((v16u8)expand_alpha_1x128 ((v8i16)unpack_32_1x128 (d)))));
        w--;
    }

    while (w >= 4)
    {
        w_dst_hi  = (v8i16)load_128_aligned   (pd);
        w_src_hi  = (v8i16)load_128_unaligned (ps);
        w_mask_hi = (v8i16)load_128_unaligned (pm);

        unpack_128_2x128 ((v16i8)w_dst_hi,  &w_dst_lo, &w_dst_hi);
        unpack_128_2x128 ((v16i8)w_src_hi,  &w_src_lo, &w_src_hi);
        unpack_128_2x128 ((v16i8)w_mask_hi, &w_mask_lo, &w_mask_hi);

        expand_alpha_2x128 (w_dst_lo, w_dst_hi,
                            &w_alpha_lo, &w_alpha_hi);
        negate_2x128 (w_alpha_lo, w_alpha_hi,
                      &w_alpha_lo, &w_alpha_hi);

        pix_multiply_2x128 (&w_src_lo, &w_src_hi,
                            &w_mask_lo, &w_mask_hi,
                            &w_dst_lo, &w_dst_hi);
        pix_multiply_2x128 (&w_dst_lo, &w_dst_hi,
                            &w_alpha_lo, &w_alpha_hi,
                            &w_dst_lo, &w_dst_hi);

        save_128_aligned (
            pd, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));

        ps += 4;
        pd += 4;
        pm += 4;
        w -= 4;
    }

    while (w)
    {
        s = *ps++;
        m = *pm++;
        d = *pd;

        *pd++ = pack_1x128_32 (
            (v8i16)pix_multiply_1x128 (
                (v8i16)pix_multiply_1x128 (
                (v8i16)unpack_32_1x128 (s), (v8i16)unpack_32_1x128 (m)),
                (v8i16)negate_1x128 ((v16u8)expand_alpha_1x128 ((v8i16)unpack_32_1x128 (d)))));

        w--;
    }
}

static void
msa_combine_out_reverse_ca (pixman_implementation_t *imp,
                             pixman_op_t             op,
                             uint32_t *              pd,
                             const uint32_t *        ps,
                             const uint32_t *        pm,
                             int                     w)
{
    int32_t s, m, d;

    v8i16 w_alpha_lo, w_alpha_hi;
    v8i16 w_src_lo, w_src_hi;
    v8i16 w_dst_lo, w_dst_hi;
    v8i16 w_mask_lo, w_mask_hi;

    while (w && (intptr_t)pd & 15)
    {
        s = *ps++;
        m = *pm++;
        d = *pd;

        *pd++ = pack_1x128_32 (
            (v8i16)pix_multiply_1x128 (
                (v8i16)unpack_32_1x128 (d),
                (v8i16)negate_1x128 ((v16u8)pix_multiply_1x128 (
                                 (v8i16)unpack_32_1x128 (m),
                                 expand_alpha_1x128 ((v8i16)unpack_32_1x128 (s))))));
        w--;
    }

    while (w >= 4)
    {
        w_dst_hi  = (v8i16)load_128_aligned   (pd);
        w_src_hi  = (v8i16)load_128_unaligned (ps);
        w_mask_hi = (v8i16)load_128_unaligned (pm);

        unpack_128_2x128 ((v16i8)w_dst_hi,  &w_dst_lo, &w_dst_hi);
        unpack_128_2x128 ((v16i8)w_src_hi,  &w_src_lo, &w_src_hi);
        unpack_128_2x128 ((v16i8)w_mask_hi, &w_mask_lo, &w_mask_hi);

        expand_alpha_2x128 (w_src_lo, w_src_hi,
                            &w_alpha_lo, &w_alpha_hi);

        pix_multiply_2x128 (&w_mask_lo, &w_mask_hi,
                            &w_alpha_lo, &w_alpha_hi,
                            &w_mask_lo, &w_mask_hi);

        negate_2x128 (w_mask_lo, w_mask_hi,
                      &w_mask_lo, &w_mask_hi);

        pix_multiply_2x128 (&w_dst_lo, &w_dst_hi,
                            &w_mask_lo, &w_mask_hi,
                            &w_dst_lo, &w_dst_hi);

        save_128_aligned (
            pd, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));

        ps += 4;
        pd += 4;
        pm += 4;
        w -= 4;
    }

    while (w)
    {
        s = *ps++;
        m = *pm++;
        d = *pd;

        *pd++ = pack_1x128_32 (
            (v8i16)pix_multiply_1x128 (
                (v8i16)unpack_32_1x128 (d),
                (v8i16)negate_1x128 ((v16u8)pix_multiply_1x128 (
                                 (v8i16)unpack_32_1x128 (m),
                                 expand_alpha_1x128 ((v8i16)unpack_32_1x128 (s))))));
        w--;
    }
}

static force_inline uint32_t
core_combine_atop_ca_pixel_msa (uint32_t src,
                                uint32_t mask,
                                uint32_t dst)
{
    v8i16 m, s, d;
    v8i16 sa, da;

    m = (v8i16)unpack_32_1x128 (mask);
    s = (v8i16)unpack_32_1x128 (src);
    d = (v8i16)unpack_32_1x128 (dst);
    sa = expand_alpha_1x128 (s);
    da = expand_alpha_1x128 (d);

    s = (v8i16)pix_multiply_1x128 (s, m);
    m = (v8i16)negate_1x128 ((v16u8)pix_multiply_1x128 (m, sa));

    return pack_1x128_32 ((v8i16)pix_add_multiply_1x128 (&d, &m, &s, &da));
}

static void
msa_combine_atop_ca (pixman_implementation_t *imp,
                      pixman_op_t             op,
                      uint32_t *              pd,
                      const uint32_t *        ps,
                      const uint32_t *        pm,
                      int                     w)
{
    uint32_t s, m, d;

    v8i16 w_src_lo, w_src_hi;
    v8i16 w_dst_lo, w_dst_hi;
    v8i16 w_alpha_src_lo, w_alpha_src_hi;
    v8i16 w_alpha_dst_lo, w_alpha_dst_hi;
    v8i16 w_mask_lo, w_mask_hi;

    while (w && (uintptr_t)pd & 15)
    {
	s = *ps++;
	m = *pm++;
	d = *pd;

	*pd++ = core_combine_atop_ca_pixel_msa (s, m, d);
	w--;
    }

    while (w >= 4)
    {
	w_dst_hi  = (v8i16)load_128_aligned   ((int32_t*)pd);
	w_src_hi  = (v8i16)load_128_unaligned ((uint32_t*)ps);
	w_mask_hi = (v8i16)load_128_unaligned ((uint32_t*)pm);

	unpack_128_2x128 ((v16i8)w_dst_hi, &w_dst_lo, &w_dst_hi);
	unpack_128_2x128 ((v16i8)w_src_hi, &w_src_lo, &w_src_hi);
	unpack_128_2x128 ((v16i8)w_mask_hi, &w_mask_lo, &w_mask_hi);

	expand_alpha_2x128 (w_src_lo, w_src_hi,
			    &w_alpha_src_lo, &w_alpha_src_hi);
	expand_alpha_2x128 (w_dst_lo, w_dst_hi,
			    &w_alpha_dst_lo, &w_alpha_dst_hi);

	pix_multiply_2x128 (&w_src_lo, &w_src_hi,
			    &w_mask_lo, &w_mask_hi,
			    &w_src_lo, &w_src_hi);
	pix_multiply_2x128 (&w_mask_lo, &w_mask_hi,
			    &w_alpha_src_lo, &w_alpha_src_hi,
			    &w_mask_lo, &w_mask_hi);

	negate_2x128 (w_mask_lo, w_mask_hi, &w_mask_lo, &w_mask_hi);

	pix_add_multiply_2x128 (
	    &w_dst_lo, &w_dst_hi, &w_mask_lo, &w_mask_hi,
	    &w_src_lo, &w_src_hi, &w_alpha_dst_lo, &w_alpha_dst_hi,
	    &w_dst_lo, &w_dst_hi);

	save_128_aligned (
	    (int32_t*)pd, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));

	ps += 4;
	pd += 4;
	pm += 4;
	w -= 4;
    }

    while (w)
    {
	s = *ps++;
	m = *pm++;
	d = *pd;

	*pd++ = core_combine_atop_ca_pixel_msa (s, m, d);
	w--;
    }
}

static force_inline uint32_t
core_combine_reverse_atop_ca_pixel_msa (uint32_t src,
                                        uint32_t mask,
                                        uint32_t dst)
{
    v8i16 m, s, d;
    v8i16 da, sa;
    
    m = (v8i16)unpack_32_1x128 (mask);
    s = (v8i16)unpack_32_1x128 (src);
    d = (v8i16)unpack_32_1x128 (dst);

    da = (v8i16)negate_1x128 ((v16u8)expand_alpha_1x128 (d));
    sa = expand_alpha_1x128 (s);

    s = (v8i16)pix_multiply_1x128 (s, m);
    m = (v8i16)pix_multiply_1x128 (m, sa);

    return pack_1x128_32 ((v8i16)pix_add_multiply_1x128 (&d, &m, &s, &da));
}

static void
msa_combine_atop_reverse_ca (pixman_implementation_t *imp,
                              pixman_op_t             op,
                              uint32_t *              pd,
                              const uint32_t *        ps,
                              const uint32_t *        pm,
                              int                     w)
{
    uint32_t s, m, d;

    v8i16 w_src_lo, w_src_hi;
    v8i16 w_dst_lo, w_dst_hi;
    v8i16 w_alpha_src_lo, w_alpha_src_hi;
    v8i16 w_alpha_dst_lo, w_alpha_dst_hi;
    v8i16 w_mask_lo, w_mask_hi;

    while (w && (uintptr_t)pd & 15)
    {
	s = *ps++;
	m = *pm++;
	d = *pd;

	*pd++ = core_combine_reverse_atop_ca_pixel_msa (s, m, d);
	w--;
    }

    while (w >= 4)
    {
	w_dst_hi  = (v8i16)load_128_aligned   ((int32_t*)pd);
	w_src_hi  = (v8i16)load_128_unaligned ((uint32_t*)ps);
	w_mask_hi = (v8i16)load_128_unaligned ((uint32_t*)pm);

	unpack_128_2x128 ((v16i8)w_dst_hi, &w_dst_lo, &w_dst_hi);
	unpack_128_2x128 ((v16i8)w_src_hi, &w_src_lo, &w_src_hi);
	unpack_128_2x128 ((v16i8)w_mask_hi, &w_mask_lo, &w_mask_hi);

	expand_alpha_2x128 (w_src_lo, w_src_hi,
			    &w_alpha_src_lo, &w_alpha_src_hi);
	expand_alpha_2x128 (w_dst_lo, w_dst_hi,
			    &w_alpha_dst_lo, &w_alpha_dst_hi);

	pix_multiply_2x128 (&w_src_lo, &w_src_hi,
			    &w_mask_lo, &w_mask_hi,
			    &w_src_lo, &w_src_hi);
	pix_multiply_2x128 (&w_mask_lo, &w_mask_hi,
			    &w_alpha_src_lo, &w_alpha_src_hi,
			    &w_mask_lo, &w_mask_hi);

	negate_2x128 (w_alpha_dst_lo, w_alpha_dst_hi,
		      &w_alpha_dst_lo, &w_alpha_dst_hi);

	pix_add_multiply_2x128 (
	    &w_dst_lo, &w_dst_hi, &w_mask_lo, &w_mask_hi,
	    &w_src_lo, &w_src_hi, &w_alpha_dst_lo, &w_alpha_dst_hi,
	    &w_dst_lo, &w_dst_hi);

	save_128_aligned (
	    (int32_t*)pd, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));

	ps += 4;
	pd += 4;
	pm += 4;
	w -= 4;
    }

    while (w)
    {
	s = *ps++;
	m = *pm++;
	d = *pd;

	*pd++ = core_combine_reverse_atop_ca_pixel_msa (s, m, d);
	w--;
    }
}

static force_inline uint32_t
core_combine_xor_ca_pixel_msa (uint32_t src,
                               uint32_t mask,
                               uint32_t dst)
{
    v8i16 a, s, d;
    v8i16 alpha_dst, dest, alpha_src;

    a = (v8i16)unpack_32_1x128 (mask);
    s = (v8i16)unpack_32_1x128 (src);
    d = (v8i16)unpack_32_1x128 (dst);

    alpha_dst = (v8i16)negate_1x128 ((v16u8)pix_multiply_1x128 (
				       a, expand_alpha_1x128 (s)));
    dest      = (v8i16)pix_multiply_1x128 (s, a);
    alpha_src = (v8i16)negate_1x128 ((v16u8)expand_alpha_1x128 (d));

    return pack_1x128_32 ((v8i16)pix_add_multiply_1x128 (&d,
                                                &alpha_dst,
                                                &dest,
                                                &alpha_src));
}

static void
msa_combine_xor_ca (pixman_implementation_t *imp,
                     pixman_op_t              op,
                     uint32_t *               pd,
                     const uint32_t *         ps,
                     const uint32_t *         pm,
                     int                      w)
{
    uint32_t s, m, d;

    v8i16 w_src_lo, w_src_hi;
    v8i16 w_dst_lo, w_dst_hi;
    v8i16 w_alpha_src_lo, w_alpha_src_hi;
    v8i16 w_alpha_dst_lo, w_alpha_dst_hi;
    v8i16 w_mask_lo, w_mask_hi;

    while (w && (uintptr_t)pd & 15)
    {
	s = *ps++;
	m = *pm++;
	d = *pd;

	*pd++ = core_combine_xor_ca_pixel_msa (s, m, d);
	w--;
    }

    while (w >= 4)
    {
	w_dst_hi  = (v8i16)load_128_aligned   ((int32_t*)pd);
	w_src_hi  = (v8i16)load_128_unaligned ((uint32_t*)ps);
	w_mask_hi = (v8i16)load_128_unaligned ((uint32_t*)pm);

	unpack_128_2x128 ((v16i8)w_dst_hi, &w_dst_lo, &w_dst_hi);
	unpack_128_2x128 ((v16i8)w_src_hi, &w_src_lo, &w_src_hi);
	unpack_128_2x128 ((v16i8)w_mask_hi, &w_mask_lo, &w_mask_hi);

	expand_alpha_2x128 (w_src_lo, w_src_hi,
			    &w_alpha_src_lo, &w_alpha_src_hi);
	expand_alpha_2x128 (w_dst_lo, w_dst_hi,
			    &w_alpha_dst_lo, &w_alpha_dst_hi);

	pix_multiply_2x128 (&w_src_lo, &w_src_hi,
			    &w_mask_lo, &w_mask_hi,
			    &w_src_lo, &w_src_hi);
	pix_multiply_2x128 (&w_mask_lo, &w_mask_hi,
			    &w_alpha_src_lo, &w_alpha_src_hi,
			    &w_mask_lo, &w_mask_hi);

	negate_2x128 (w_alpha_dst_lo, w_alpha_dst_hi,
		      &w_alpha_dst_lo, &w_alpha_dst_hi);
	negate_2x128 (w_mask_lo, w_mask_hi,
		      &w_mask_lo, &w_mask_hi);

	pix_add_multiply_2x128 (
	    &w_dst_lo, &w_dst_hi, &w_mask_lo, &w_mask_hi,
	    &w_src_lo, &w_src_hi, &w_alpha_dst_lo, &w_alpha_dst_hi,
	    &w_dst_lo, &w_dst_hi);

	save_128_aligned (
	    (int32_t*)pd, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));

	ps += 4;
	pd += 4;
	pm += 4;
	w -= 4;
    }

    while (w)
    {
	s = *ps++;
	m = *pm++;
	d = *pd;

	*pd++ = core_combine_xor_ca_pixel_msa (s, m, d);
	w--;
    }
}

static void
msa_combine_add_ca (pixman_implementation_t *imp,
                     pixman_op_t             op,
                     uint32_t *              pd,
                     const uint32_t *        ps,
                     const uint32_t *        pm,
                     int                     w)
{
    uint32_t s, m, d;

    v8i16 w_src_lo, w_src_hi;
    v8i16 w_dst_lo, w_dst_hi;
    v8i16 w_mask_lo, w_mask_hi;

    while (w && (uintptr_t)pd & 15)
    {
	s = *ps++;
	m = *pm++;
	d = *pd;

	*pd++ = pack_1x128_32 (
	    (v8i16)__msa_adds_u_h ((v8u16)pix_multiply_1x128 ((v8i16)unpack_32_1x128 (s),
					                      (v8i16)unpack_32_1x128 (m)),
			           (v8u16)unpack_32_1x128 (d)));
	w--;
    }

    while (w >= 4)
    {
	w_src_hi  = (v8i16)load_128_unaligned ((uint32_t*)ps);
	w_mask_hi = (v8i16)load_128_unaligned ((uint32_t*)pm);
	w_dst_hi  = (v8i16)load_128_aligned   ((int32_t*)pd);

	unpack_128_2x128 ((v16i8)w_src_hi, &w_src_lo, &w_src_hi);
	unpack_128_2x128 ((v16i8)w_mask_hi, &w_mask_lo, &w_mask_hi);
	unpack_128_2x128 ((v16i8)w_dst_hi, &w_dst_lo, &w_dst_hi);

	pix_multiply_2x128 (&w_src_lo, &w_src_hi,
			    &w_mask_lo, &w_mask_hi,
			    &w_src_lo, &w_src_hi);

	save_128_aligned (
	    (int32_t*)pd, (v4i32)pack_2x128_128 (
		(v8i16)__msa_adds_u_h ((v8u16)w_src_lo, (v8u16)w_dst_lo),
		(v8i16)__msa_adds_u_h ((v8u16)w_src_hi, (v8u16)w_dst_hi)));

	ps += 4;
	pd += 4;
	pm += 4;
	w -= 4;
    }

    while (w)
    {
	s = *ps++;
	m = *pm++;
	d = *pd;

	*pd++ = pack_1x128_32 (
	    (v8i16)__msa_adds_u_h ((v8u16)pix_multiply_1x128 ((v8i16)unpack_32_1x128 (s),
					                      (v8i16)unpack_32_1x128 (m)),
			           (v8u16)unpack_32_1x128 (d)));
	w--;
    }
}

static force_inline v8i16
create_mask_16_128 (uint16_t mask)
{
    return __msa_fill_h ((int16_t)mask);
}

/* Work around a code generation bug in Sun Studio 12. */
#if defined(__SUNPRO_C) && (__SUNPRO_C >= 0x590)
# define create_mask_2x32_128(mask0, mask1)				\
    (__msa_set_s_w (mask0, mask1, mask0, mask1))
#else
static force_inline v4i32
create_mask_2x32_128 (uint32_t mask0,
                      uint32_t mask1)
{
    v4i32 result = __msa_set_s_w (mask0, mask1, mask0, mask1);
    return result;
}
#endif

static void
msa_composite_over_n_8888 (pixman_implementation_t *imp,
                           pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint32_t  src;
    uint32_t    *dst_line, *dst, d;
    int32_t   w;
    int dst_stride;
    v8i16 w_src, w_alpha;
    v8i16 w_dst, w_dst_lo, w_dst_hi;

    src = _pixman_image_get_solid (imp, src_image, dest_image->bits.format);

    if (src == 0)
	return;

    PIXMAN_IMAGE_GET_LINE (
	dest_image, dest_x, dest_y, uint32_t, dst_stride, dst_line, 1);

    w_src = (v8i16)expand_pixel_32_1x128 (src);
    w_alpha = expand_alpha_1x128 (w_src);

    while (height--)
    {
	dst = dst_line;

	dst_line += dst_stride;
	w = width;

	while (w && (uintptr_t)dst & 15)
	{
	    d = *dst;
	    *dst++ = pack_1x128_32 ((v8i16)over_1x128 ((v16u8)w_src,
						       (v16u8)w_alpha,
						       (v16u8)unpack_32_1x128 (d)));
	    w--;
	}

	while (w >= 4)
	{
	    w_dst = (v8i16)load_128_aligned ((int32_t*)dst);

	    unpack_128_2x128 ((v16i8)w_dst, &w_dst_lo, &w_dst_hi);

	    over_2x128 (&w_src, &w_src,
			&w_alpha, &w_alpha,
			&w_dst_lo, &w_dst_hi);

	    /* rebuid the 4 pixel data and save*/
	    save_128_aligned (
		(int32_t*)dst, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));

	    w -= 4;
	    dst += 4;
	}

	while (w)
	{
	    d = *dst;
	    *dst++ = pack_1x128_32 ((v8i16)over_1x128 ((v16u8)w_src,
						       (v16u8)w_alpha,
						       (v16u8)unpack_32_1x128 (d)));
	    w--;
	}

    }
}

static void
msa_composite_over_n_0565 (pixman_implementation_t *imp,
                           pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint32_t src;
    uint16_t    *dst_line, *dst, d;
    uint32_t w;
    int dst_stride;
    v4i32 w_src, w_alpha;
    v8i16 w_dst, w_dst0, w_dst1, w_dst2, w_dst3;

    src = _pixman_image_get_solid (imp, src_image, dest_image->bits.format);

    if (src == 0)
        return;

    PIXMAN_IMAGE_GET_LINE (
        dest_image, dest_x, dest_y, uint16_t, dst_stride, dst_line, 1);

    w_src   =        expand_pixel_32_1x128 (src);
    w_alpha = (v4i32)expand_alpha_1x128 ((v8i16)w_src);

    while (height--)
    {
        dst = dst_line;

        dst_line += dst_stride;
        w = width;

        while (w && (intptr_t)dst & 15)
        {
            d = *dst;

            *dst++ = pack_565_32_16 (
                pack_1x128_32 ((v8i16)over_1x128 ((v16u8)w_src,
                                                  (v16u8)w_alpha,
                                                  (v16u8)expand565_16_1x128 (d))));
            w--;
        }

        while (w >= 8)
        {
            w_dst = (v8i16)load_128_aligned (dst);

            unpack_565_128_4x128 ((v4i32)w_dst,
                                  &w_dst0, &w_dst1, &w_dst2, &w_dst3);

            over_2x128 (&w_src, &w_src,
                        &w_alpha,  &w_alpha,
                        &w_dst0,  &w_dst1);
            over_2x128 (&w_src, &w_src,
                        &w_alpha,  &w_alpha,
                        &w_dst2,  &w_dst3);

            w_dst = (v8i16)pack_565_4x128_128 (
                &w_dst0, &w_dst1, &w_dst2, &w_dst3);

            save_128_aligned ((uint32_t *) dst, (v4i32)w_dst);

            dst += 8;
            w -= 8;
        }

        while (w--)
        {
            d = *dst;
            *dst++ = pack_565_32_16 (
                pack_1x128_32 ((v8i16)over_1x128 ((v16u8)w_src, (v16u8)w_alpha,
                                                  (v16u8)expand565_16_1x128 (d))));
        }
    }

}


static void
msa_composite_add_n_8888_8888_ca (pixman_implementation_t *imp,
                                  pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint32_t src;
    uint32_t    *dst_line, d;
    uint32_t    *mask_line, m;
    uint32_t pack_cmp;
    int dst_stride, mask_stride;

    v8i16 w_src;
    v8i16 w_dst;
    v8i16 w_mask, w_mask_lo, w_mask_hi;

    v8i16 wf_src, wf_mask, wf_dest;

    pack_cmp = 0;

    src = _pixman_image_get_solid (imp, src_image, dest_image->bits.format);

    if (src == 0)
        return;

    PIXMAN_IMAGE_GET_LINE (
        dest_image, dest_x, dest_y, uint32_t, dst_stride, dst_line, 1);
    PIXMAN_IMAGE_GET_LINE (
        mask_image, mask_x, mask_y, uint32_t, mask_stride, mask_line, 1);

    w_src = (v8i16)__msa_ilvr_b ((v16i8)__msa_fill_w (src), (v16i8)zero);
    wf_src = w_src;

    while (height--)
    {
        int w = width;
        const uint32_t *pm = (uint32_t *)mask_line;
        uint32_t *pd = (uint32_t *)dst_line;

        dst_line += dst_stride;
        mask_line += mask_stride;

        while (w && (intptr_t)pd & 15)
        {
            m = *pm++;

            if (m)
            {
                d = *pd;

                wf_mask = (v8i16)unpack_32_1x128 (m);
                wf_dest = (v8i16)unpack_32_1x128 (d);

                *pd = pack_1x128_32 ((v8i16)
                    __msa_adds_s_b ((v16i8)pix_multiply_1x128 (wf_mask, wf_src),
                                    (v16i8)wf_dest));
            }

            pd++;
            w--;
        }

        while (w >= 4)
        {
            v16i8 cmp_mask;
            uint64_t cmp_mask_low, cmp_mask_high;

            w_mask = (v8i16)load_128_unaligned (pm);
            cmp_mask = __msa_srli_b (__msa_ceq_b ((v16i8)w_mask, (v16i8)zero), 7);

            /* simulates _mm_movemask_epi8 */

            cmp_mask_low  = __msa_copy_u_d ((v2i64) cmp_mask, 0);
            cmp_mask_high = __msa_copy_u_d ((v2i64) cmp_mask, 1);
            pack_cmp = 0;

            // Extract the lower 8 bytes
            for (int i = 0; i < 8; i++)
            {
                pack_cmp |= ((cmp_mask_low >> (i * 8)) & 0x1) << i;
            }
    
            // Extract the higher 8 bytes
            for (int i = 0; i < 8; i++)
            {
                pack_cmp |= ((cmp_mask_high >> (i * 8)) & 0x1) << (i + 8);
            }

            /* End */

            /* if all bits in mask are zero, pack_cmp are equal to 0xffff */
            if (pack_cmp != 0xffff)
            {
                w_dst = (v8i16)load_128_aligned (pd);

                unpack_128_2x128 ((v16i8)w_mask, &w_mask_lo, &w_mask_hi);

                pix_multiply_2x128 (&w_src,     &w_src,
                                    &w_mask_lo, &w_mask_hi,
                                    &w_mask_lo, &w_mask_hi);
                w_mask_hi = (v8i16)pack_2x128_128 (w_mask_lo, w_mask_hi);

                save_128_aligned (
                    pd, (v4i32)__msa_adds_u_b ((v16u8)w_mask_hi, (v16u8)w_dst));
            }

            pd += 4;
            pm += 4;
            w -= 4;
        }

        while (w)
        {
            m = *pm++;

            if (m)
            {
                d = *pd;

                wf_mask = (v8i16)unpack_32_1x128 (m);
                wf_dest = (v8i16)unpack_32_1x128 (d);

                *pd = pack_1x128_32 (
                    (v8i16)__msa_adds_s_b ((v16i8)pix_multiply_1x128 ((v8i16)wf_mask, (v8i16)wf_src),
                                           (v16i8)wf_dest));
            }

            pd++;
            w--;
        }
    }

}


static void
msa_composite_over_n_8888_8888_ca (pixman_implementation_t *imp,
                                   pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint32_t  src;
    uint32_t    *dst_line, d;
    uint32_t    *mask_line, m;
    uint32_t  pack_cmp;
    int dst_stride, mask_stride;

    v8i16 w_src, w_alpha;
    v8i16 w_dst, w_dst_lo, w_dst_hi;
    v8i16 w_mask, w_mask_lo, w_mask_hi;

    v8i16 wf_src, wf_alpha, wf_mask, wf_dest;

    src = _pixman_image_get_solid (imp, src_image, dest_image->bits.format);

    if (src == 0)
        return;

    PIXMAN_IMAGE_GET_LINE (
        dest_image, dest_x, dest_y, uint32_t, dst_stride, dst_line, 1);
    PIXMAN_IMAGE_GET_LINE (
        mask_image, mask_x, mask_y, uint32_t, mask_stride, mask_line, 1);

    
    w_src   = (v8i16)__msa_ilvr_b ((v16i8)__msa_fill_w (src), (v16i8)zero);
    w_alpha = expand_alpha_1x128 (w_src);
    wf_src   = w_src;
    wf_alpha = w_alpha;

    while (height--)
    {
        int w = width;
        const uint32_t *pm = (uint32_t *)mask_line;
        uint32_t *pd = (uint32_t *)dst_line;

        dst_line += dst_stride;
        mask_line += mask_stride;

        while (w && (intptr_t)pd & 15)
        {
            m = *pm++;

            if (m)
            {
                d = *pd;
                wf_mask = (v8i16)unpack_32_1x128 (m);
                wf_dest = (v8i16)unpack_32_1x128 (d);

                *pd = pack_1x128_32 ((v8i16)in_over_1x128 (&wf_src,
                                                           &wf_alpha,
                                                           &wf_mask,
                                                           &wf_dest));
            }

            pd++;
            w--;
        }

        while (w >= 4)
        {
            uint64_t cmp_mask_low;
            uint64_t cmp_mask_high;
            v16i8 cmp_mask;
            pack_cmp = 0;

            w_mask = (v8i16)load_128_unaligned (pm);

            cmp_mask = __msa_srli_b (__msa_ceq_b ((v16i8)w_mask, (v16i8)zero), 7);

            /* simulates _mm_movemask_epi8 */

            cmp_mask_low  = __msa_copy_u_d ((v2i64) cmp_mask, 0);
            cmp_mask_high = __msa_copy_u_d ((v2i64) cmp_mask, 1);  

            // Extract the lower 8 bytes
            for (int i = 0; i < 8; i++)
            {
                pack_cmp |= ((cmp_mask_low >> (i * 8)) & 0x1) << i;
            }
    
            // Extract the higher 8 bytes
            for (int i = 0; i < 8; i++)
            {
                pack_cmp |= ((cmp_mask_high >> (i * 8)) & 0x1) << (i + 8);
            }

            /* End */

            /* if all bits in mask are zero, pack_cmp are equal to 0xffff */
            if (pack_cmp != 0xffff)
            {
                w_dst = (v8i16)load_128_aligned (pd);

                unpack_128_2x128 ((v16i8)w_mask, &w_mask_lo, &w_mask_hi);
                unpack_128_2x128 ((v16i8)w_dst,  &w_dst_lo, &w_dst_hi);

                in_over_2x128 (&w_src, &w_src,
                               &w_alpha,  &w_alpha,
                               &w_mask_lo,  &w_mask_hi,
                               &w_dst_lo,  &w_dst_hi);

                save_128_aligned (
                    pd, (v4i32)pack_2x128_128 ((v8i16)w_dst_lo, (v8i16)w_dst_hi));
            }

            pd += 4;
            pm += 4;
            w -= 4;
        }

        while (w)
        {
            m = *pm++;

            if (m)
            {
                d = *pd;
                wf_mask = (v8i16)unpack_32_1x128 (m);
                wf_dest = (v8i16)unpack_32_1x128 (d);

                *pd = pack_1x128_32 ( (v8i16)
                    in_over_1x128 (&wf_src, &wf_alpha, &wf_mask, &wf_dest));
            }

            pd++;
            w--;
        }
    }

}


static void
msa_composite_over_8888_n_8888 (pixman_implementation_t *imp,
                                pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint32_t    *dst_line, *dst;
    uint32_t    *src_line, *src;
    uint32_t mask;
    uint32_t w;
    int dst_stride, src_stride;

    v8i16 w_mask;
    v8i16 w_src, w_src_lo, w_src_hi;
    v8i16 w_dst, w_dst_lo, w_dst_hi;
    v8i16 w_alpha_lo, w_alpha_hi;

    PIXMAN_IMAGE_GET_LINE (
        dest_image, dest_x, dest_y, uint32_t, dst_stride, dst_line, 1);
    PIXMAN_IMAGE_GET_LINE (
        src_image, src_x, src_y, uint32_t, src_stride, src_line, 1);

    mask = _pixman_image_get_solid (imp, mask_image, PIXMAN_a8r8g8b8);

    w_mask = create_mask_16_128 (mask >> 24);

    while (height--)
    {
        dst = dst_line;
        dst_line += dst_stride;
        src = src_line;
        src_line += src_stride;
        w = width;

        while (w && (intptr_t)dst & 15)
        {
            int32_t s;
            
            s = *src++;

            if (s)
            {
                int32_t d;
                v8i16 ms, alpha, dest, alpha_dst;
                d = *dst;

                ms        = (v8i16)unpack_32_1x128 (s);
                alpha     = expand_alpha_1x128 (ms);
                dest      = w_mask;
                alpha_dst = (v8i16)unpack_32_1x128 (d);
                
                *dst = pack_1x128_32 ( (v8i16)
                    in_over_1x128 (&ms, &alpha, &dest, &alpha_dst));
            }
            dst++;
            w--;
        }

        while (w >= 4)
        {
            w_src = (v8i16)load_128_unaligned (src);

            if (!is_zero ((v16i8)w_src))
            {
                w_dst = (v8i16)load_128_aligned (dst);
                
                unpack_128_2x128 ((v16i8)w_src, &w_src_lo,  &w_src_hi);
                unpack_128_2x128 ((v16i8)w_dst, &w_dst_lo,  &w_dst_hi);
                expand_alpha_2x128 (w_src_lo, w_src_hi,
                                    &w_alpha_lo, &w_alpha_hi);
                
                in_over_2x128 (&w_src_lo,   &w_src_hi,
                               &w_alpha_lo, &w_alpha_hi,
                               &w_mask,     &w_mask,
                               &w_dst_lo,   &w_dst_hi);
                
                save_128_aligned (
                    dst, (v4i32)pack_2x128_128 ((v8i16)w_dst_lo, (v8i16)w_dst_hi));
            }
                
            dst += 4;
            src += 4;
            w -= 4;
        }

        while (w)
        {
            int32_t s;
            s = *src++;

            if (s)
            {
                int32_t d;
                v8i16 ms, alpha, dest, mask;

                d = *dst;

                ms    = (v8i16)unpack_32_1x128 (s);
                alpha = expand_alpha_1x128 (ms);
                mask  = w_mask;
                dest  = (v8i16)unpack_32_1x128 (d);
                
                *dst = pack_1x128_32 ( (v8i16)
                    in_over_1x128 (&ms, &alpha, &mask, &dest));
            }

            dst++;
            w--;
        }
    }

}


static void
msa_composite_src_x888_0565 (pixman_implementation_t *imp,
                             pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint16_t    *dst_line, *dst;
    uint32_t    *src_line, *src, s;
    int dst_stride, src_stride;
    uint32_t w;

    PIXMAN_IMAGE_GET_LINE (src_image, src_x, src_y, uint32_t, src_stride, src_line, 1);
    PIXMAN_IMAGE_GET_LINE (dest_image, dest_x, dest_y, uint16_t, dst_stride, dst_line, 1);

    while (height--)
    {
        dst = dst_line;
        dst_line += dst_stride;
        src = src_line;
        src_line += src_stride;
        w = width;

        while (w && (intptr_t)dst & 15)
        {
            s = *src++;
            *dst = convert_8888_to_0565 (s);
            dst++;
            w--;
        }

        while (w >= 8)
        {
            v4i32 w_src0, w_src1;

            w_src0 = (v4i32)load_128_unaligned (src + 0);
            w_src1 = (v4i32)load_128_unaligned (src + 1);

            save_128_aligned ((uint32_t *)dst, (v4i32)pack_565_2packedx128_128 (w_src0, w_src1));

            w -= 8;
            src += 8;
            dst += 8;
        }

        while (w)
        {
            s = *src++;
            *dst = convert_8888_to_0565 (s);
            dst++;
            w--;
        }
    }
}


static void
msa_composite_src_x888_8888 (pixman_implementation_t *imp,
                             pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint32_t    *dst_line, *dst;
    uint32_t    *src_line, *src;
    uint32_t w;
    int dst_stride, src_stride;

    PIXMAN_IMAGE_GET_LINE (
        dest_image, dest_x, dest_y, uint32_t, dst_stride, dst_line, 1);
    PIXMAN_IMAGE_GET_LINE (
        src_image, src_x, src_y, uint32_t, src_stride, src_line, 1);

    while (height--)
    {
        dst = dst_line;
        dst_line += dst_stride;
        src = src_line;
        src_line += src_stride;
        w = width;

        while (w && (intptr_t)dst & 15)
        {
            *dst++ = *src++ | 0xff000000;
            w--;
        }

        while (w >= 16)
        {
            v4i32 w_src1, w_src2, w_src3, w_src4;
            
            w_src1 = (v4i32)load_128_unaligned ((uint32_t *)src + 0);
            w_src2 = (v4i32)load_128_unaligned ((uint32_t *)src + 1);
            w_src3 = (v4i32)load_128_unaligned ((uint32_t *)src + 2);
            w_src4 = (v4i32)load_128_unaligned ((uint32_t *)src + 3);
            
            save_128_aligned ((uint32_t *)dst + 0, (v4i32)__msa_or_v ((v16u8)w_src1, (v16u8)mask_ff000000));
            save_128_aligned ((uint32_t *)dst + 1, (v4i32)__msa_or_v ((v16u8)w_src2, (v16u8)mask_ff000000));
            save_128_aligned ((uint32_t *)dst + 2, (v4i32)__msa_or_v ((v16u8)w_src3, (v16u8)mask_ff000000));
            save_128_aligned ((uint32_t *)dst + 3, (v4i32)__msa_or_v ((v16u8)w_src4, (v16u8)mask_ff000000));
            
            dst += 16;
            src += 16;
            w -= 16;
        }

        while (w)
        {
            *dst++ = *src++ | 0xff000000;
            w--;
        }
    }

}


static void
msa_composite_over_x888_n_8888 (pixman_implementation_t *imp,
                                pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint32_t    *dst_line, *dst;
    uint32_t    *src_line, *src;
    uint32_t mask;
    int dst_stride, src_stride;
    uint32_t w;

    v8i16 w_mask, w_alpha;
    v8i16 w_src, w_src_lo, w_src_hi;
    v8i16 w_dst, w_dst_lo, w_dst_hi;

    PIXMAN_IMAGE_GET_LINE (
        dest_image, dest_x, dest_y, uint32_t, dst_stride, dst_line, 1);
    PIXMAN_IMAGE_GET_LINE (
        src_image, src_x, src_y, uint32_t, src_stride, src_line, 1);

    mask = _pixman_image_get_solid (imp, mask_image, PIXMAN_a8r8g8b8);

    w_mask = create_mask_16_128 (mask >> 24);
    w_alpha = mask_00ff;

    while (height--)
    {
        dst = dst_line;
        dst_line += dst_stride;
        src = src_line;
        src_line += src_stride;
        w = width;

        while (w && (intptr_t)dst & 15)
        {
            uint32_t s, d;
            uint32_t s_temp;
            v8i16 vsrc, alpha, mask, dest;

            s_temp = *src++;
            s = s_temp | 0xff000000;
            d = *dst;

            vsrc  = (v8i16)unpack_32_1x128 (s);
            alpha = w_alpha;
            mask  = w_mask;
            dest  = (v8i16)unpack_32_1x128 (d);

            *dst++ = pack_1x128_32 ((v8i16)
                in_over_1x128 (&vsrc, &alpha, &mask, &dest));
            w--;
        }

        while (w >= 4)
        {
           /* Note: not sure if it's gonna need to change to `vsrc`.
            * Will further check later */
            w_src = (v8i16)__msa_or_v ((v16u8)load_128_unaligned (src), (v16u8)mask_ff000000);
            w_dst = (v8i16)load_128_aligned ((int32_t *)dst);

            unpack_128_2x128 ((v16i8)w_src, &w_src_lo, &w_src_hi);
            unpack_128_2x128 ((v16i8)w_dst, &w_dst_lo, &w_dst_hi);

            in_over_2x128 (&w_src_lo,  &w_src_hi,
                           &w_alpha,  &w_alpha,
                           &w_mask,  &w_mask,
                           &w_dst_lo,  &w_dst_hi);

            save_128_aligned (
                (int32_t *)dst, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));

            dst += 4;
            src += 4;
            w -= 4;
        }

        while (w)
        {
            uint32_t s, d;
            uint32_t s_temp;
            v8i16 vsrc, alpha, mask, dest;

            s_temp = *src++;
            s = s_temp | 0xff000000;
            d = *dst;

            vsrc  = (v8i16)unpack_32_1x128 (s);
            alpha = w_alpha;
            mask  = w_mask;
            dest  = (v8i16)unpack_32_1x128 (d);

            *dst++ = pack_1x128_32 ( (v8i16)
                in_over_1x128 (&vsrc, &alpha, &mask, &dest));

            w--;
        }
    }
}


static void
msa_composite_over_8888_8888 (pixman_implementation_t *imp,
                              pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    int dst_stride, src_stride;
    uint32_t    *dst_line, *dst;
    uint32_t    *src_line, *src;

    PIXMAN_IMAGE_GET_LINE (
        dest_image, dest_x, dest_y, uint32_t, dst_stride, dst_line, 1);
    PIXMAN_IMAGE_GET_LINE (
        src_image, src_x, src_y, uint32_t, src_stride, src_line, 1);

    dst = dst_line;
    src = src_line;

    while (height--)
    {
        msa_combine_over_u (imp, op, dst, src, NULL, width);

        dst += dst_stride;
        src += src_stride;
    }
}


static force_inline uint16_t
composite_over_8888_0565pixel (uint32_t src, uint16_t dst)
{
    v4i32 ms;

    ms = (v4i32)unpack_32_1x128 (src);
    return pack_565_32_16 (
        pack_1x128_32 ( (v8i16)
            over_1x128 (
                (v16u8)ms, (v16u8)expand_alpha_1x128 ((v8i16)ms), (v16u8)expand565_16_1x128 (dst))));
}


static void
msa_composite_over_8888_0565 (pixman_implementation_t *imp,
                              pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint16_t    *dst_line, *dst, d;
    uint32_t    *src_line, *src, s;
    int dst_stride, src_stride;
    uint32_t w;

    v8i16 w_alpha_lo, w_alpha_hi;
    v8i16 w_src, w_src_lo, w_src_hi;
    v8i16 w_dst, w_dst0, w_dst1, w_dst2, w_dst3;

    PIXMAN_IMAGE_GET_LINE (
        dest_image, dest_x, dest_y, uint16_t, dst_stride, dst_line, 1);
    PIXMAN_IMAGE_GET_LINE (
        src_image, src_x, src_y, uint32_t, src_stride, src_line, 1);

    while (height--)
    {
        dst = dst_line;
        src = src_line;

        dst_line += dst_stride;
        src_line += src_stride;
        w = width;

        /* Align dst on a 16-byte boundary */
        while (w &&
               ((intptr_t)dst & 15))
        {
            s = *src++;
            d = *dst;

            *dst++ = composite_over_8888_0565pixel (s, d);
            w--;
        }

        /* It's a 8 pixel loop */
        while (w >= 8)
        {
            /* I'm loading unaligned because I'm not sure
             * about the address alignment.
             */
            w_src = (v8i16)load_128_unaligned (src);
            w_dst = (v8i16)load_128_aligned ((int32_t *)dst);

            /* Unpacking */
            unpack_128_2x128 ((v16i8)w_src, &w_src_lo, &w_src_hi);
            unpack_565_128_4x128 ((v4i32)w_dst,
                                  &w_dst0, &w_dst1, &w_dst2, &w_dst3);
            expand_alpha_2x128 (w_src_lo, w_src_hi,
                                &w_alpha_lo, &w_alpha_hi);

            /* I'm loading next 4 pixels from memory
             * before to optimze the memory read.
             */
            w_src = (v8i16)load_128_unaligned ((src + 4));

            over_2x128 (&w_src_lo,   &w_src_hi,
                        &w_alpha_lo, &w_alpha_hi,
                        &w_dst0,     &w_dst1);

            /* Unpacking */
            unpack_128_2x128 ((v16i8)w_src, &w_src_lo, &w_src_hi);
            expand_alpha_2x128 (w_src_lo, w_src_hi,
                                &w_alpha_lo, &w_alpha_hi);

            over_2x128 (&w_src_lo,   &w_src_hi,
                        &w_alpha_lo, &w_alpha_hi,
                        &w_dst2,     &w_dst3);

            save_128_aligned (
                (uint32_t *) dst, (v4i32)pack_565_4x128_128 (
                    &w_dst0, &w_dst1, &w_dst2, &w_dst3));

            w -= 8;
            dst += 8;
            src += 8;
        }

        while (w--)
        {
            s = *src++;
            d = *dst;

            *dst++ = composite_over_8888_0565pixel (s, d);
        }
    }
}

static void
msa_composite_over_n_8_8888 (pixman_implementation_t *imp,
                             pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint32_t src, srca;
    uint32_t *dst_line, *dst;
    uint8_t *mask_line, *mask;
    int dst_stride, mask_stride;
    int32_t w;
    uint32_t d;

    v8i16 w_src, w_alpha, w_def;
    v8i16 w_dst, w_dst_lo, w_dst_hi;
    v8i16 w_mask, w_mask_lo, w_mask_hi;

    v8i16 wf_src, wf_alpha, wf_mask, wf_dest;

    src = _pixman_image_get_solid (imp, src_image, dest_image->bits.format);

    srca = src >> 24;
    if (src == 0)
	return;

    PIXMAN_IMAGE_GET_LINE (
	dest_image, dest_x, dest_y, uint32_t, dst_stride, dst_line, 1);
    PIXMAN_IMAGE_GET_LINE (
	mask_image, mask_x, mask_y, uint8_t, mask_stride, mask_line, 1);

    w_def = (v8i16)create_mask_2x32_128 (src, src);
    w_src = (v8i16)expand_pixel_32_1x128 (src);
    w_alpha = expand_alpha_1x128 (w_src);
    wf_src   = w_src;
    wf_alpha = w_alpha;

    while (height--)
    {
	dst = dst_line;
	dst_line += dst_stride;
	mask = mask_line;
	mask_line += mask_stride;
	w = width;

	while (w && (uintptr_t)dst & 15)
	{
	    uint8_t m = *mask++;

	    if (m)
	    {
		d = *dst;
		wf_mask = expand_pixel_8_1x128 (m);
		wf_dest = (v8i16)unpack_32_1x128 (d);

		*dst = pack_1x128_32 ((v8i16)in_over_1x128 (&wf_src,
		                                   &wf_alpha,
		                                   &wf_mask,
		                                   &wf_dest));
	    }

	    w--;
	    dst++;
	}

	while (w >= 4)
	{
            uint32_t m;
            memcpy(&m, mask, sizeof(uint32_t));

	    if (srca == 0xff && m == 0xffffffff)
	    {
		save_128_aligned ((int32_t*)dst, (v4i32)w_def);
	    }
	    else if (m)
	    {
		w_dst  = (v8i16)load_128_aligned ((int32_t*) dst);
		w_mask = (v8i16)unpack_32_1x128 (m);
		w_mask = (v8i16)__msa_ilvr_b (__msa_fill_b (0), (v16i8)w_mask);

		/* Unpacking */
		unpack_128_2x128 ((v16i8)w_dst, &w_dst_lo, &w_dst_hi);
		unpack_128_2x128 ((v16i8)w_mask, &w_mask_lo, &w_mask_hi);

		expand_alpha_rev_2x128 (w_mask_lo, w_mask_hi,
					&w_mask_lo, &w_mask_hi);

		in_over_2x128 (&w_src, &w_src,
			       &w_alpha, &w_alpha,
			       &w_mask_lo, &w_mask_hi,
			       &w_dst_lo, &w_dst_hi);

		save_128_aligned (
		    (int32_t*)dst, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));
	    }

	    w -= 4;
	    dst += 4;
	    mask += 4;
	}

	while (w)
	{
	    uint8_t m = *mask++;

	    if (m)
	    {
		d = *dst;
		wf_mask = expand_pixel_8_1x128 (m);
		wf_dest = (v8i16)unpack_32_1x128 (d);

		*dst = pack_1x128_32 ((v8i16)in_over_1x128 (&wf_src,
		                                   &wf_alpha,
		                                   &wf_mask,
		                                   &wf_dest));
	    }

	    w--;
	    dst++;
	}
    }

}

/* #if defined(__GNUC__) && !defined(__x86_64__) && !defined(__amd64__)
__attribute__((__force_align_arg_pointer__))
#endif */
static pixman_bool_t
msa_fill (pixman_implementation_t *imp,
           uint32_t *              bits,
           int                     stride,
           int                     bpp,
           int                     x,
           int                     y,
           int                     width,
           int                     height,
           uint32_t		   filler)
{
    uint32_t byte_width;
    uint8_t *byte_line;

    v4i32 w_def;

    if (bpp == 8)
    {
	uint32_t b;
	uint32_t w;

	stride = stride * (int) sizeof (uint32_t) / 1;
	byte_line = (uint8_t *)(((uint8_t *)bits) + stride * y + x);
	byte_width = width;
	stride *= 1;

	b = filler & 0xff;
	w = (b << 8) | b;
	filler = (w << 16) | w;
    }
    else if (bpp == 16)
    {
	stride = stride * (int) sizeof (uint32_t) / 2;
	byte_line = (uint8_t *)(((uint16_t *)bits) + stride * y + x);
	byte_width = 2 * width;
	stride *= 2;

        filler = (filler & 0xffff) * 0x00010001;
    }
    else if (bpp == 32)
    {
	stride = stride * (int) sizeof (uint32_t) / 4;
	byte_line = (uint8_t *)(((uint32_t *)bits) + stride * y + x);
	byte_width = 4 * width;
	stride *= 4;
    }
    else
    {
	return FALSE;
    }

    w_def = create_mask_2x32_128 (filler, filler);

    while (height--)
    {
	int w;
	uint8_t *d = byte_line;
	byte_line += stride;
	w = byte_width;

	if (w >= 1 && ((uintptr_t)d & 1))
	{
	    *(uint8_t *)d = filler;
	    w -= 1;
	    d += 1;
	}

	while (w >= 2 && ((uintptr_t)d & 3))
	{
	    *(uint16_t *)d = filler;
	    w -= 2;
	    d += 2;
	}

	while (w >= 4 && ((uintptr_t)d & 15))
	{
	    *(uint32_t *)d = filler;

	    w -= 4;
	    d += 4;
	}

	while (w >= 128)
	{
	    save_128_aligned ((int32_t*)(d),     w_def);
	    save_128_aligned ((int32_t*)(d + 16),  w_def);
	    save_128_aligned ((int32_t*)(d + 32),  w_def);
	    save_128_aligned ((int32_t*)(d + 48),  w_def);
	    save_128_aligned ((int32_t*)(d + 64),  w_def);
	    save_128_aligned ((int32_t*)(d + 80),  w_def);
	    save_128_aligned ((int32_t*)(d + 96),  w_def);
	    save_128_aligned ((int32_t*)(d + 112), w_def);

	    d += 128;
	    w -= 128;
	}

	if (w >= 64)
	{
	    save_128_aligned ((int32_t*)(d),     w_def);
	    save_128_aligned ((int32_t*)(d + 16),  w_def);
	    save_128_aligned ((int32_t*)(d + 32),  w_def);
	    save_128_aligned ((int32_t*)(d + 48),  w_def);

	    d += 64;
	    w -= 64;
	}

	if (w >= 32)
	{
	    save_128_aligned ((int32_t*)(d),     w_def);
	    save_128_aligned ((int32_t*)(d + 16),  w_def);

	    d += 32;
	    w -= 32;
	}

	if (w >= 16)
	{
	    save_128_aligned ((int32_t*)(d),     w_def);

	    d += 16;
	    w -= 16;
	}

	while (w >= 4)
	{
	    *(uint32_t *)d = filler;

	    w -= 4;
	    d += 4;
	}

	if (w >= 2)
	{
	    *(uint16_t *)d = filler;
	    w -= 2;
	    d += 2;
	}

	if (w >= 1)
	{
	    *(uint8_t *)d = filler;
	    w -= 1;
	    d += 1;
	}
    }

    return TRUE;
}

static void
msa_composite_src_n_8_8888 (pixman_implementation_t *imp,
                            pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint32_t src, srca;
    uint32_t    *dst_line, *dst;
    uint8_t     *mask_line, *mask;
    int dst_stride, mask_stride;
    int32_t w;

    v8i16 w_src, w_def;
    v8i16 w_mask, w_mask_lo, w_mask_hi;

    src = _pixman_image_get_solid (imp, src_image, dest_image->bits.format);

    srca = src >> 24;
    if (src == 0)
    {
	msa_fill (imp, dest_image->bits.bits, dest_image->bits.rowstride,
		   PIXMAN_FORMAT_BPP (dest_image->bits.format),
		   dest_x, dest_y, width, height, 0);
	return;
    }

    PIXMAN_IMAGE_GET_LINE (
	dest_image, dest_x, dest_y, uint32_t, dst_stride, dst_line, 1);
    PIXMAN_IMAGE_GET_LINE (
	mask_image, mask_x, mask_y, uint8_t, mask_stride, mask_line, 1);

    w_def = (v8i16)create_mask_2x32_128 (src, src);
    w_src = (v8i16)expand_pixel_32_1x128 (src);

    while (height--)
    {
	dst = dst_line;
	dst_line += dst_stride;
	mask = mask_line;
	mask_line += mask_stride;
	w = width;

	while (w && (uintptr_t)dst & 15)
	{
	    uint8_t m = *mask++;

	    if (m)
	    {
		*dst = pack_1x128_32 (
		    (v8i16)pix_multiply_1x128 (w_src, expand_pixel_8_1x128 (m)));
	    }
	    else
	    {
		*dst = 0;
	    }

	    w--;
	    dst++;
	}

	while (w >= 4)
	{
            uint32_t m;
            memcpy(&m, mask, sizeof(uint32_t));

	    if (srca == 0xff && m == 0xffffffff)
	    {
		save_128_aligned ((int32_t*)dst, (v4i32)w_def);
	    }
	    else if (m)
	    {
		w_mask = (v8i16)unpack_32_1x128 (m);
		w_mask = (v8i16)__msa_ilvr_b (__msa_fill_b (0), (v16i8)w_mask);

		/* Unpacking */
		unpack_128_2x128 ((v16i8)w_mask, &w_mask_lo, &w_mask_hi);

		expand_alpha_rev_2x128 (w_mask_lo, w_mask_hi,
					&w_mask_lo, &w_mask_hi);

		pix_multiply_2x128 (&w_src, &w_src,
				    &w_mask_lo, &w_mask_hi,
				    &w_mask_lo, &w_mask_hi);

		save_128_aligned (
		    (int32_t*)dst, (v4i32)pack_2x128_128 (w_mask_lo, w_mask_hi));
	    }
	    else
	    {
		save_128_aligned ((int32_t*)dst, __msa_fill_w (0));
	    }

	    w -= 4;
	    dst += 4;
	    mask += 4;
	}

	while (w)
	{
	    uint8_t m;
            m = *mask++;

	    if (m)
	    {
		*dst = pack_1x128_32 (
		    (v8i16)pix_multiply_1x128 (
			w_src, expand_pixel_8_1x128 (m)));
	    }
	    else
	    {
		*dst = 0;
	    }

	    w--;
	    dst++;
	}
    }

}

static void
msa_composite_over_n_8_0565 (pixman_implementation_t *imp,
                             pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint32_t  src;
    uint16_t *dst_line,  *dst, d;
    uint8_t  *mask_line, *mask;
    int       dst_stride, mask_stride;
    int32_t   w;
    v8i16 wf_src, wf_alpha, wf_mask, wf_dest;

    v8i16 w_src, w_alpha;
    v8i16 w_mask, w_mask_lo, w_mask_hi;
    v8i16 w_dst, w_dst0, w_dst1, w_dst2, w_dst3;

    src = _pixman_image_get_solid (imp, src_image, dest_image->bits.format);

    if (src == 0)
	return;

    PIXMAN_IMAGE_GET_LINE (
	dest_image, dest_x, dest_y, uint16_t, dst_stride, dst_line, 1);
    PIXMAN_IMAGE_GET_LINE (
	mask_image, mask_x, mask_y, uint8_t, mask_stride, mask_line, 1);

    w_src = (v8i16)expand_pixel_32_1x128 (src);
    w_alpha = expand_alpha_1x128 (w_src);
    wf_src = w_src;
    wf_alpha = w_alpha;

    while (height--)
    {
	dst = dst_line;
	dst_line += dst_stride;
	mask = mask_line;
	mask_line += mask_stride;
	w = width;

	while (w && (uintptr_t)dst & 15)
	{
	    uint8_t m = *mask++;

	    if (m)
	    {
		d = *dst;
		wf_mask = expand_alpha_rev_1x128 ((v8i16)unpack_32_1x128 (m));
		wf_dest = (v8i16)expand565_16_1x128 (d);

		*dst = pack_565_32_16 (
		    pack_1x128_32 (
			(v8i16)in_over_1x128 (
			    &wf_src, &wf_alpha, &wf_mask, &wf_dest)));
	    }

	    w--;
	    dst++;
	}

	while (w >= 8)
	{
            uint32_t m;

	    w_dst = (v8i16)load_128_aligned ((int32_t*) dst);
	    unpack_565_128_4x128 ((v4i32)w_dst,
				  &w_dst0, &w_dst1, &w_dst2, &w_dst3);

            memcpy(&m, mask, sizeof(uint32_t));
	    mask += 4;

	    if (m)
	    {
		w_mask = (v8i16)unpack_32_1x128 (m);
		w_mask = (v8i16)__msa_ilvr_b (__msa_fill_b (0), (v16i8)w_mask);

		/* Unpacking */
		unpack_128_2x128 ((v16i8)w_mask, &w_mask_lo, &w_mask_hi);

		expand_alpha_rev_2x128 (w_mask_lo, w_mask_hi,
					&w_mask_lo, &w_mask_hi);

		in_over_2x128 (&w_src, &w_src,
			       &w_alpha, &w_alpha,
			       &w_mask_lo, &w_mask_hi,
			       &w_dst0, &w_dst1);
	    }

            memcpy(&m, mask, sizeof(uint32_t));
	    mask += 4;

	    if (m)
	    {
		w_mask = (v8i16)unpack_32_1x128 (m);
		w_mask = (v8i16)__msa_ilvr_b (__msa_fill_b (0), (v16i8)w_mask);

		/* Unpacking */
		unpack_128_2x128 ((v16i8)w_mask, &w_mask_lo, &w_mask_hi);

		expand_alpha_rev_2x128 (w_mask_lo, w_mask_hi,
					&w_mask_lo, &w_mask_hi);
		in_over_2x128 (&w_src, &w_src,
			       &w_alpha, &w_alpha,
			       &w_mask_lo, &w_mask_hi,
			       &w_dst2, &w_dst3);
	    }

	    save_128_aligned (
		(int32_t*)dst, (v4i32)pack_565_4x128_128 (
		    &w_dst0, &w_dst1, &w_dst2, &w_dst3));

	    w -= 8;
	    dst += 8;
	}

	while (w)
	{
	    uint8_t m;
            m = *mask++;

	    if (m)
	    {
		d = *dst;
		wf_mask = expand_alpha_rev_1x128 ((v8i16)unpack_32_1x128 (m));
		wf_dest = (v8i16)expand565_16_1x128 (d);

		*dst = pack_565_32_16 (
		    pack_1x128_32 (
			(v8i16)in_over_1x128 (
			    &wf_src, &wf_alpha, &wf_mask, &wf_dest)));
	    }

	    w--;
	    dst++;
	}
    }

}

static void
msa_composite_over_pixbuf_0565 (pixman_implementation_t *imp,
                                pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint16_t    *dst_line, *dst, d;
    uint32_t    *src_line, *src, s;
    int dst_stride, src_stride;
    int32_t w;
    uint32_t opaque, zero;

    v8i16 ms;
    v8i16 w_src, w_src_lo, w_src_hi;
    v8i16 w_dst, w_dst0, w_dst1, w_dst2, w_dst3;

    PIXMAN_IMAGE_GET_LINE (
	dest_image, dest_x, dest_y, uint16_t, dst_stride, dst_line, 1);
    PIXMAN_IMAGE_GET_LINE (
	src_image, src_x, src_y, uint32_t, src_stride, src_line, 1);

    while (height--)
    {
	dst = dst_line;
	dst_line += dst_stride;
	src = src_line;
	src_line += src_stride;
	w = width;

	while (w && (uintptr_t)dst & 15)
	{
	    s = *src++;
	    d = *dst;

	    ms = (v8i16)unpack_32_1x128 (s);

	    *dst++ = pack_565_32_16 (
		pack_1x128_32 (
		    (v8i16)over_rev_non_pre_1x128 (ms, (v8i16)expand565_16_1x128 (d))));
	    w--;
	}

	while (w >= 8)
	{
	    /* First round */
	    w_src = (v8i16)load_128_unaligned ((uint32_t*)src);
	    w_dst = (v8i16)load_128_aligned  ((int32_t*)dst);

	    opaque = (uint32_t)is_opaque ((v16i8)w_src);
	    zero   = (uint32_t)is_zero ((v16i8)w_src);

	    unpack_565_128_4x128 ((v4i32)w_dst,
				  &w_dst0, &w_dst1, &w_dst2, &w_dst3);
	    unpack_128_2x128 ((v16i8)w_src, &w_src_lo, &w_src_hi);

	    /* preload next round*/
	    w_src = (v8i16)load_128_unaligned ((uint32_t*)(src + 4));

	    if (opaque)
	    {
		invert_colors_2x128 (w_src_lo, w_src_hi,
				     &w_dst0, &w_dst1);
	    }
	    else if (!zero)
	    {
		over_rev_non_pre_2x128 (w_src_lo, w_src_hi,
					&w_dst0, &w_dst1);
	    }

	    /* Second round */
	    opaque = (uint32_t)is_opaque ((v16i8)w_src);
	    zero = (uint32_t)is_zero ((v16i8)w_src);

	    unpack_128_2x128 ((v16i8)w_src, &w_src_lo, &w_src_hi);

	    if (opaque)
	    {
		invert_colors_2x128 (w_src_lo, w_src_hi,
				     &w_dst2, &w_dst3);
	    }
	    else if (!zero)
	    {
		over_rev_non_pre_2x128 (w_src_lo, w_src_hi,
					&w_dst2, &w_dst3);
	    }

	    save_128_aligned (
		(int32_t*)dst, (v4i32)pack_565_4x128_128 (
		    &w_dst0, &w_dst1, &w_dst2, &w_dst3));

	    w -= 8;
	    src += 8;
	    dst += 8;
	}

	while (w)
	{
	    s = *src++;
	    d = *dst;

	    ms = (v8i16)unpack_32_1x128 (s);

	    *dst++ = pack_565_32_16 (
		pack_1x128_32 (
		    (v8i16)over_rev_non_pre_1x128 (ms, (v8i16)expand565_16_1x128 (d))));
	    w--;
	}
    }

}

static void
msa_composite_over_pixbuf_8888 (pixman_implementation_t *imp,
                                pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint32_t    *dst_line, *dst, d;
    uint32_t    *src_line, *src, s;
    int dst_stride, src_stride;
    int32_t w;
    uint32_t opaque, zero;

    v8i16 w_src_lo, w_src_hi;
    v8i16 w_dst_lo, w_dst_hi;

    PIXMAN_IMAGE_GET_LINE (
	dest_image, dest_x, dest_y, uint32_t, dst_stride, dst_line, 1);
    PIXMAN_IMAGE_GET_LINE (
	src_image, src_x, src_y, uint32_t, src_stride, src_line, 1);

    while (height--)
    {
	dst = dst_line;
	dst_line += dst_stride;
	src = src_line;
	src_line += src_stride;
	w = width;

	while (w && (uintptr_t)dst & 15)
	{
	    s = *src++;
	    d = *dst;

	    *dst++ = pack_1x128_32 (
		(v8i16)over_rev_non_pre_1x128 (
		    (v8i16)unpack_32_1x128 (s), (v8i16)unpack_32_1x128 (d)));

	    w--;
	}

	while (w >= 4)
	{
	    w_src_hi = (v8i16)load_128_unaligned ((uint32_t*)src);

	    opaque = (uint32_t)is_opaque ((v16i8)w_src_hi);
	    zero = (uint32_t)is_zero ((v16i8)w_src_hi);

	    unpack_128_2x128 ((v16i8)w_src_hi, &w_src_lo, &w_src_hi);

	    if (opaque)
	    {
		invert_colors_2x128 (w_src_lo, w_src_hi,
				     &w_dst_lo, &w_dst_hi);

		save_128_aligned (
		    (int32_t*)dst, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));
	    }
	    else if (!zero)
	    {
		w_dst_hi = (v8i16)load_128_aligned  ((int32_t*)dst);

		unpack_128_2x128 ((v16i8)w_dst_hi, &w_dst_lo, &w_dst_hi);

		over_rev_non_pre_2x128 (w_src_lo, w_src_hi,
					&w_dst_lo, &w_dst_hi);

		save_128_aligned (
		    (int32_t*)dst, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));
	    }

	    w -= 4;
	    dst += 4;
	    src += 4;
	}

	while (w)
	{
	    s = *src++;
	    d = *dst;

	    *dst++ = pack_1x128_32 (
		(v8i16)over_rev_non_pre_1x128 (
		    (v8i16)unpack_32_1x128 (s), (v8i16)unpack_32_1x128 (d)));

	    w--;
	}
    }

}

static void
msa_composite_over_n_8888_0565_ca (pixman_implementation_t *imp,
                                   pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint32_t src;
    uint16_t    *dst_line, *dst, d;
    uint32_t    *mask_line, *mask, m;
    int dst_stride, mask_stride;
    int w;
    uint32_t pack_cmp;

    v8i16 w_src, w_alpha;
    v8i16 w_mask, w_mask_lo, w_mask_hi;
    v8i16 w_dst, w_dst0, w_dst1, w_dst2, w_dst3;

    v8i16 wf_src, wf_alpha, wf_mask, wf_dest;

    src = _pixman_image_get_solid (imp, src_image, dest_image->bits.format);

    if (src == 0)
	return;

    PIXMAN_IMAGE_GET_LINE (
	dest_image, dest_x, dest_y, uint16_t, dst_stride, dst_line, 1);
    PIXMAN_IMAGE_GET_LINE (
	mask_image, mask_x, mask_y, uint32_t, mask_stride, mask_line, 1);

    w_src = (v8i16)expand_pixel_32_1x128 (src);
    w_alpha = expand_alpha_1x128 (w_src);
    wf_src = w_src;
    wf_alpha = w_alpha;

    while (height--)
    {
	w = width;
	mask = mask_line;
	dst = dst_line;
	mask_line += mask_stride;
	dst_line += dst_stride;

	while (w && ((uintptr_t)dst & 15))
	{
	    m = *(uint32_t *) mask;

	    if (m)
	    {
		d = *dst;
		wf_mask = (v8i16)unpack_32_1x128 (m);
		wf_dest = (v8i16)expand565_16_1x128 (d);

		*dst = pack_565_32_16 (
		    pack_1x128_32 (
			(v8i16)in_over_1x128 (
			    &wf_src, &wf_alpha, &wf_mask, &wf_dest)));
	    }

	    w--;
	    dst++;
	    mask++;
	}

	while (w >= 8)
	{
	    /* First round */
	    w_mask = (v8i16)load_128_unaligned ((uint32_t*)mask);
	    w_dst  = (v8i16)load_128_aligned ((int32_t*)dst);

		pack_cmp = (uint32_t)is_zero((v16i8)w_mask);

	    unpack_565_128_4x128 ((v4i32)w_dst,
				  &w_dst0, &w_dst1, &w_dst2, &w_dst3);
	    unpack_128_2x128 ((v16i8)w_mask, &w_mask_lo, &w_mask_hi);

	    /* preload next round */
	    w_mask = (v8i16)load_128_unaligned ((uint32_t*)(mask + 4));

	    /* preload next round */
	    if (pack_cmp == 0x0000)
	    {
		in_over_2x128 (&w_src, &w_src,
			       &w_alpha, &w_alpha,
			       &w_mask_lo, &w_mask_hi,
			       &w_dst0, &w_dst1);
	    }

	    /* Second round */
		pack_cmp = (uint32_t)is_zero((v16i8)w_mask);

	    unpack_128_2x128 ((v16i8)w_mask, &w_mask_lo, &w_mask_hi);

	    if (pack_cmp == 0x0000)
	    {
		in_over_2x128 (&w_src, &w_src,
			       &w_alpha, &w_alpha,
			       &w_mask_lo, &w_mask_hi,
			       &w_dst2, &w_dst3);
	    }

	    save_128_aligned (
		(int32_t*)dst, (v4i32)pack_565_4x128_128 (
		    &w_dst0, &w_dst1, &w_dst2, &w_dst3));

	    w -= 8;
	    dst += 8;
	    mask += 8;
	}

	while (w)
	{
	    m = *(uint32_t *) mask;

	    if (m)
	    {
		d = *dst;
		wf_mask = (v8i16)unpack_32_1x128 (m);
		wf_dest = (v8i16)expand565_16_1x128 (d);

		*dst = pack_565_32_16 (
		    pack_1x128_32 (
			(v8i16)in_over_1x128 (
			    &wf_src, &wf_alpha, &wf_mask, &wf_dest)));
	    }

	    w--;
	    dst++;
	    mask++;
	}
    }

}

static void
msa_composite_in_n_8_8 (pixman_implementation_t *imp,
                        pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint8_t     *dst_line, *dst;
    uint8_t     *mask_line, *mask;
    int dst_stride, mask_stride;
    uint32_t d;
    uint32_t src;
    int32_t w;

    v8i16 w_alpha;
    v8i16 w_mask, w_mask_lo, w_mask_hi;
    v8i16 w_dst, w_dst_lo, w_dst_hi;

    PIXMAN_IMAGE_GET_LINE (
	dest_image, dest_x, dest_y, uint8_t, dst_stride, dst_line, 1);
    PIXMAN_IMAGE_GET_LINE (
	mask_image, mask_x, mask_y, uint8_t, mask_stride, mask_line, 1);

    src = _pixman_image_get_solid (imp, src_image, dest_image->bits.format);

    w_alpha = expand_alpha_1x128 ((v8i16)expand_pixel_32_1x128 (src));

    while (height--)
    {
	dst = dst_line;
	dst_line += dst_stride;
	mask = mask_line;
	mask_line += mask_stride;
	w = width;

	while (w && ((uintptr_t)dst & 15))
	{
	    uint8_t m = *mask++;
	    d = (uint32_t) *dst;

	    *dst++ = (uint8_t) pack_1x128_32 (
		(v8i16)pix_multiply_1x128 (
		    (v8i16)pix_multiply_1x128 (w_alpha,
				       (v8i16)unpack_32_1x128 (m)),
		    (v8i16)unpack_32_1x128 (d)));
	    w--;
	}

	while (w >= 16)
	{
	    w_mask = (v8i16)load_128_unaligned ((uint32_t*)mask);
	    w_dst = (v8i16)load_128_aligned ((int32_t*)dst);

	    unpack_128_2x128 ((v16i8)w_mask, &w_mask_lo, &w_mask_hi);
	    unpack_128_2x128 ((v16i8)w_dst, &w_dst_lo, &w_dst_hi);

	    pix_multiply_2x128 (&w_alpha, &w_alpha,
				&w_mask_lo, &w_mask_hi,
				&w_mask_lo, &w_mask_hi);

	    pix_multiply_2x128 (&w_mask_lo, &w_mask_hi,
				&w_dst_lo, &w_dst_hi,
				&w_dst_lo, &w_dst_hi);

	    save_128_aligned (
		(int32_t*)dst, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));

	    mask += 16;
	    dst += 16;
	    w -= 16;
	}

	while (w)
	{
	    uint8_t m;
            m = *mask++;
	    d = (uint32_t) *dst;

	    *dst++ = (uint8_t) pack_1x128_32 (
		(v8i16)pix_multiply_1x128 (
		    (v8i16)pix_multiply_1x128 (
			(v8i16)w_alpha, (v8i16)unpack_32_1x128 (m)),
		    (v8i16)unpack_32_1x128 (d)));
	    w--;
	}
    }

}

static void
msa_composite_in_n_8 (pixman_implementation_t *imp,
		      pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint8_t     *dst_line, *dst;
    int dst_stride;
    uint32_t d;
    uint32_t src;
    int32_t w;

    v8i16 w_alpha;
    v8i16 w_dst, w_dst_lo, w_dst_hi;

    PIXMAN_IMAGE_GET_LINE (
	dest_image, dest_x, dest_y, uint8_t, dst_stride, dst_line, 1);

    src = _pixman_image_get_solid (imp, src_image, dest_image->bits.format);

    w_alpha = expand_alpha_1x128 ((v8i16)expand_pixel_32_1x128 (src));

    src = src >> 24;

    if (src == 0xff)
	return;

    if (src == 0x00)
    {
	pixman_fill (dest_image->bits.bits, dest_image->bits.rowstride,
		     8, dest_x, dest_y, width, height, src);

	return;
    }

    while (height--)
    {
	dst = dst_line;
	dst_line += dst_stride;
	w = width;

	while (w && ((uintptr_t)dst & 15))
	{
	    d = (uint32_t) *dst;

	    *dst++ = (uint8_t) pack_1x128_32 (
		(v8i16)pix_multiply_1x128 (
		    w_alpha,
		    (v8i16)unpack_32_1x128 (d)));
	    w--;
	}

	while (w >= 16)
	{
	    w_dst = (v8i16)load_128_aligned ((int32_t*)dst);

	    unpack_128_2x128 ((v16i8)w_dst, &w_dst_lo, &w_dst_hi);
	    
	    pix_multiply_2x128 (&w_alpha, &w_alpha,
				&w_dst_lo, &w_dst_hi,
				&w_dst_lo, &w_dst_hi);

	    save_128_aligned (
		(int32_t*)dst, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));

	    dst += 16;
	    w -= 16;
	}

	while (w)
	{
	    d = (uint32_t) *dst;

	    *dst++ = (uint8_t) pack_1x128_32 (
		(v8i16)pix_multiply_1x128 (
		    w_alpha,
		    (v8i16)unpack_32_1x128 (d)));
	    w--;
	}
    }

}

static void
msa_composite_in_8_8 (pixman_implementation_t *imp,
                      pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint8_t     *dst_line, *dst;
    uint8_t     *src_line, *src;
    int src_stride, dst_stride;
    int32_t w;
    uint32_t s, d;

    v8i16 w_src, w_src_lo, w_src_hi;
    v8i16 w_dst, w_dst_lo, w_dst_hi;

    PIXMAN_IMAGE_GET_LINE (
	dest_image, dest_x, dest_y, uint8_t, dst_stride, dst_line, 1);
    PIXMAN_IMAGE_GET_LINE (
	src_image, src_x, src_y, uint8_t, src_stride, src_line, 1);

    while (height--)
    {
	dst = dst_line;
	dst_line += dst_stride;
	src = src_line;
	src_line += src_stride;
	w = width;

	while (w && ((uintptr_t)dst & 15))
	{
	    s = (uint32_t) *src++;
	    d = (uint32_t) *dst;

	    *dst++ = (uint8_t) pack_1x128_32 (
		(v8i16)pix_multiply_1x128 (
		    (v8i16)unpack_32_1x128 (s), (v8i16)unpack_32_1x128 (d)));
	    w--;
	}

	while (w >= 16)
	{
	    w_src = (v8i16)load_128_unaligned ((uint32_t*)src);
	    w_dst = (v8i16)load_128_aligned ((int32_t*)dst);

	    unpack_128_2x128 ((v16i8)w_src, &w_src_lo, &w_src_hi);
	    unpack_128_2x128 ((v16i8)w_dst, &w_dst_lo, &w_dst_hi);

	    pix_multiply_2x128 (&w_src_lo, &w_src_hi,
				&w_dst_lo, &w_dst_hi,
				&w_dst_lo, &w_dst_hi);

	    save_128_aligned (
		(int32_t*)dst, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));

	    src += 16;
	    dst += 16;
	    w -= 16;
	}

	while (w)
	{
	    s = (uint32_t) *src++;
	    d = (uint32_t) *dst;

	    *dst++ = (uint8_t) pack_1x128_32 (
		(v8i16)pix_multiply_1x128 ((v8i16)unpack_32_1x128 (s), (v8i16)unpack_32_1x128 (d)));
	    w--;
	}
    }

} 

static void
msa_composite_add_n_8_8 (pixman_implementation_t *imp,
                         pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint8_t     *dst_line, *dst;
    uint8_t     *mask_line, *mask;
    int dst_stride, mask_stride;
    uint32_t w;
    uint32_t src;
    uint32_t d;

    v8i16 w_alpha;
    v8i16 w_mask, w_mask_lo, w_mask_hi;
    v8i16 w_dst, w_dst_lo, w_dst_hi;

    PIXMAN_IMAGE_GET_LINE (
        dest_image, dest_x, dest_y, uint8_t, dst_stride, dst_line, 1);
    PIXMAN_IMAGE_GET_LINE (
        mask_image, mask_x, mask_y, uint8_t, mask_stride, mask_line, 1);

    src = _pixman_image_get_solid (imp, src_image, dest_image->bits.format);

    w_alpha = (v8i16)expand_alpha_1x128 ((v8i16)expand_pixel_32_1x128 (src));

    while (height--)
    {
        dst = dst_line;
        dst_line += dst_stride;
        mask = mask_line;
        mask_line += mask_stride;
        w = width;

        while (w && ((intptr_t)dst & 15))
        {
            int8_t m;
            m = *mask++;
            d = (int32_t)*dst;

            *dst++ = (uint8_t) pack_1x128_32 (
                __msa_adds_s_h (
                    (v8i16)pix_multiply_1x128 (
                        w_alpha, (v8i16)unpack_32_1x128 (m)),
                    (v8i16)unpack_32_1x128 (d)));
            w--;
        }

        while (w >= 16)
        {
            w_mask = (v8i16)load_128_unaligned (mask);
            w_dst  = (v8i16)load_128_aligned ((int32_t *)dst);

            unpack_128_2x128 ((v16i8)w_mask,  &w_mask_lo, &w_mask_hi);
            unpack_128_2x128 ((v16i8)w_dst,  &w_dst_lo, &w_dst_hi);

            pix_multiply_2x128 (&w_alpha, &w_alpha,
                                &w_mask_lo, &w_mask_hi,
                                &w_mask_lo, &w_mask_hi);

            w_dst_lo = (v8i16)__msa_adds_s_h (w_mask_lo, w_dst_lo);
            w_dst_hi = (v8i16)__msa_adds_s_h (w_mask_hi, w_dst_hi);

            save_128_aligned (
                (int32_t *)dst, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));

            mask += 16;
            dst += 16;
            w -= 16;
        }

        while (w)
        {
            int8_t m;
            m = (int32_t)*mask++;
            d = (int32_t)*dst;

            *dst++ = (int8_t) pack_1x128_32 (
                __msa_adds_s_h ( 
                    (v8i16)pix_multiply_1x128 (w_alpha, (v8i16)unpack_32_1x128 (m)),
                    (v8i16)unpack_32_1x128 (d)));
            w--;
        }
    }
}

static void
msa_composite_add_n_8 (pixman_implementation_t *imp,
                       pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint8_t     *dst_line, *dst;
    int dst_stride;
    uint32_t w;
    uint32_t src;

    v4i32 w_src;

    PIXMAN_IMAGE_GET_LINE (
        dest_image, dest_x, dest_y, uint8_t, dst_stride, dst_line, 1);

    src = _pixman_image_get_solid (imp, src_image, dest_image->bits.format);

    src >>= 24;

    if (src == 0x00)
        return;

    if (src == 0xff)
    {
        pixman_fill (dest_image->bits.bits, dest_image->bits.rowstride,
                     8, dest_x, dest_y, width, height, 0xff);

        return;
    }

    src = (src << 24) | (src << 16) | (src << 8) | src;
    w_src = __msa_fill_w (src);

    while (height--)
    {
        v4i32 tmp;
        dst = dst_line;
        dst_line += dst_stride;
        w = width;

        while (w && ((intptr_t)dst & 15))
        {
            tmp = zero;
            *dst = (int8_t) __msa_copy_s_b (
                __msa_adds_s_b ( (v16i8)w_src, (v16i8)__msa_insert_w (tmp, 0, *dst)),
                0);

            w--;
            dst++;
        }

        while (w >= 16)
        {
            save_128_aligned (
                (int32_t *)dst, __msa_adds_s_w (w_src,  load_128_aligned ((int32_t *)dst)));

            dst += 16;
            w -= 16;
        }

        while (w)
        {
            tmp = zero;
            *dst = (int8_t) __msa_copy_s_b (
                __msa_adds_s_b ((v16i8)w_src, (v16i8)__msa_insert_w (tmp, 0, *dst)),
                0);

            w--;
            dst++;
        }
    }

}


static void
msa_composite_add_8_8 (pixman_implementation_t *imp,
                        pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint8_t     *dst_line, *dst;
    uint8_t     *src_line, *src;
    int dst_stride, src_stride;
    uint32_t w;
    uint16_t t;

    PIXMAN_IMAGE_GET_LINE (
        src_image, src_x, src_y, uint8_t, src_stride, src_line, 1);
    PIXMAN_IMAGE_GET_LINE (
        dest_image, dest_x, dest_y, uint8_t, dst_stride, dst_line, 1);

    while (height--)
    {
        dst = dst_line;
        src = src_line;

        dst_line += dst_stride;
        src_line += src_stride;
        w = width;

        /* Small head */
        while (w && (intptr_t)dst & 3)
        {
            t = (*dst) + (*src++);
            *dst++ = t | (0 - (t >> 8));
            w--;
        }

        msa_combine_add_u (imp, op,
                            (uint32_t*)dst, (uint32_t*)src, NULL, w >> 2);

        /* Small tail */
        dst += w & 0xfffc;
        src += w & 0xfffc;

        w &= 3;

        while (w)
        {
            t = (*dst) + (*src++);
            *dst++ = t | (0 - (t >> 8));
            w--;
        }
    }
}


static void
msa_composite_add_8888_8888 (pixman_implementation_t *imp,
                              pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint32_t    *dst_line, *dst;
    uint32_t    *src_line, *src;
    int dst_stride, src_stride;

    PIXMAN_IMAGE_GET_LINE (
        src_image, src_x, src_y, uint32_t, src_stride, src_line, 1);
    PIXMAN_IMAGE_GET_LINE (
        dest_image, dest_x, dest_y, uint32_t, dst_stride, dst_line, 1);

    while (height--)
    {
        dst = dst_line;
        dst_line += dst_stride;
        src = src_line;
        src_line += src_stride;

        msa_combine_add_u (imp, op, dst, src, NULL, width);
    }
}

static void
msa_composite_add_n_8888 (pixman_implementation_t *imp,
                           pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint32_t *dst_line, *dst, src;
    int dst_stride;

    v4i32 w_src;

    PIXMAN_IMAGE_GET_LINE (dest_image, dest_x, dest_y, uint32_t, dst_stride, dst_line, 1);

    src = _pixman_image_get_solid (imp, src_image, dest_image->bits.format);
    if (src == 0)
        return;

    if (src == ~0)
    {
        pixman_fill (dest_image->bits.bits, dest_image->bits.rowstride, 32,
                     dest_x, dest_y, width, height, ~0);

        return;
    }

    w_src = __msa_fill_w (src);
    while (height--)
    {
        int w = width;
        int32_t d;
        v4i32 temp;

        dst = dst_line;
        dst_line += dst_stride;

        while (w && (intptr_t)dst & 15)
        {
            temp = zero;
            d = *dst;
            *dst++ = 
                __msa_copy_s_w ((v4i32)__msa_adds_u_b ((v16u8)w_src, (v16u8)__msa_insert_w (temp, 0, d)), 0);
            w--;
        }

        while (w >= 4)
        {
            
            save_128_aligned
                ((uint32_t *)dst,
                (v4i32)__msa_adds_u_b ((v16u8)w_src, (v16u8)load_128_aligned ((uint32_t *)dst)));

            dst += 4;
            w -= 4;
        }

        while (w--)
        {
            temp = zero;
            d = *dst;
            *dst++ = 
                __msa_copy_s_w ((v4i32)__msa_adds_u_b ((v16u8)w_src, (v16u8)__msa_insert_w (temp, 0, d)), 0);
        }
    }
}


static void
msa_composite_add_n_8_8888 (pixman_implementation_t *imp,
                             pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint32_t     *dst_line, *dst;
    uint8_t     *mask_line, *mask;
    int dst_stride, mask_stride;
    uint32_t w;
    uint32_t src;

    v4i32 w_src;

    src = _pixman_image_get_solid (imp, src_image, dest_image->bits.format);
    if (src == 0)
        return;
    w_src = expand_pixel_32_1x128 (src);

    PIXMAN_IMAGE_GET_LINE (
        dest_image, dest_x, dest_y, uint32_t, dst_stride, dst_line, 1);
    PIXMAN_IMAGE_GET_LINE (
        mask_image, mask_x, mask_y, uint8_t, mask_stride, mask_line, 1);

    while (height--)
    {
        dst = dst_line;
        dst_line += dst_stride;
        mask = mask_line;
        mask_line += mask_stride;
        w = width;

        while (w && ((intptr_t)dst & 15))
        {
            int8_t m;
            m = *mask++;
            if (m)
            {
                *dst = pack_1x128_32
                    ((v8i16)__msa_adds_u_h
                     (pix_multiply_1x128 ((v8i16)w_src, (v8i16)expand_pixel_8_1x128 (m)),
                      (v8u16)unpack_32_1x128 (*dst)));
            }
            dst++;
            w--;
        }

        while (w >= 4)
        {
            int32_t m;
            memcpy(&m, mask, sizeof(int32_t));

            if (m)
            {
                v8i16 w_mask_lo, w_mask_hi;
                v8i16 w_dst_lo, w_dst_hi;

                v8i16 w_dst = (v8i16)load_128_aligned ((int32_t *)dst);
                v16i8 w_mask =
                    __msa_ilvr_b (unpack_32_1x128 (m),
                                       (v16i8)zero);

                unpack_128_2x128 ((v16i8)w_mask, &w_mask_lo, &w_mask_hi);
                unpack_128_2x128 ((v16i8)w_dst,  &w_dst_lo,  &w_dst_hi);

                expand_alpha_rev_2x128 (w_mask_lo, w_mask_hi,
                                        &w_mask_lo, &w_mask_hi);

                pix_multiply_2x128 (&w_src,      &w_src,
                                    &w_mask_lo,  &w_mask_hi,
                                    &w_mask_lo,  &w_mask_hi);

                w_dst_lo = (v8i16)__msa_adds_u_h ((v8u16)w_mask_lo, (v8u16)w_dst_lo);
                w_dst_hi = (v8i16)__msa_adds_u_h ((v8u16)w_mask_hi, (v8u16)w_dst_hi);

                save_128_aligned (
                    (uint32_t *)dst, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));
            }

            w -= 4;
            dst += 4;
            mask += 4;
        }

        while (w)
        {
            int8_t m;
            m = *mask++;
            if (m)
            {
                *dst = pack_1x128_32
                    ((v8i16)__msa_adds_u_h
                     (pix_multiply_1x128 ((v8i16)w_src, expand_pixel_8_1x128 (m)),
                      (v8u16)unpack_32_1x128 (*dst)));
            }
            dst++;
            w--;
        }
    }
}


static pixman_bool_t
msa_blt (pixman_implementation_t *imp,
          uint32_t *               src_bits,
          uint32_t *               dst_bits,
          int                      src_stride,
          int                      dst_stride,
          int                      src_bpp,
          int                      dst_bpp,
          int                      src_x,
          int                      src_y,
          int                      dest_x,
          int                      dest_y,
          int                      width,
          int                      height)
{
    uint8_t *   src_bytes;
    uint8_t *   dst_bytes;
    int byte_width;

    if (src_bpp != dst_bpp)
        return FALSE;

    if (src_bpp == 16)
    {
        src_stride = src_stride * (int) sizeof (uint32_t) / 2;
        dst_stride = dst_stride * (int) sizeof (uint32_t) / 2;
        src_bytes =(uint8_t *)(((uint16_t *)src_bits) + src_stride * (src_y) + (src_x));
        dst_bytes = (uint8_t *)(((uint16_t *)dst_bits) + dst_stride * (dest_y) + (dest_x));
        byte_width = 2 * width;
        src_stride *= 2;
        dst_stride *= 2;
    }
    else if (src_bpp == 32)
    {
        src_stride = src_stride * (int) sizeof (uint32_t) / 4;
        dst_stride = dst_stride * (int) sizeof (uint32_t) / 4;
        src_bytes = (uint8_t *)(((uint32_t *)src_bits) + src_stride * (src_y) + (src_x));
        dst_bytes = (uint8_t *)(((uint32_t *)dst_bits) + dst_stride * (dest_y) + (dest_x));
        byte_width = 4 * width;
        src_stride *= 4;
        dst_stride *= 4;
    }
    else
    {
        return FALSE;
    }

    while (height--)
    {
        int w;
        uint8_t *s = src_bytes;
        uint8_t *d = dst_bytes;
        src_bytes += src_stride;
        dst_bytes += dst_stride;
        w = byte_width;

        while (w >= 2 && ((intptr_t)d & 3))
        {
            memmove(d, s, 2);
            w -= 2;
            s += 2;
            d += 2;
        }

        while (w >= 4 && ((intptr_t)d & 15))
        {
            memmove(d, s, 4);

            w -= 4;
            s += 4;
            d += 4;
        }

        while (w >= 64)
        {
            v4i32 w0, w1, w2, w3;

            w0 = (v4i32)load_128_unaligned ((s));
            w1 = (v4i32)load_128_unaligned ((s + 16));
            w2 = (v4i32)load_128_unaligned ((s + 32));
            w3 = (v4i32)load_128_unaligned ((s + 48));

            save_128_aligned ((uint32_t *) (d),    w0);
            save_128_aligned ((uint32_t *) (d + 16), w1);
            save_128_aligned ((uint32_t *) (d + 32), w2);
            save_128_aligned ((uint32_t *) (d + 48), w3);

            s += 64;
            d += 64;
            w -= 64;
        }

        while (w >= 16)
        {
            save_128_aligned ((uint32_t *)d, (v4i32)load_128_unaligned (s) );

            w -= 16;
            d += 16;
            s += 16;
        }

        while (w >= 4)
        {
            memmove(d, s, 4);

            w -= 4;
            s += 4;
            d += 4;
        }

        if (w >= 2)
        {
            memmove(d, s, 2);
            w -= 2;
            s += 2;
            d += 2;
        }
    }

    return TRUE;
}


static void
msa_composite_copy_area (pixman_implementation_t *imp,
                          pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    msa_blt (imp, src_image->bits.bits,
              dest_image->bits.bits,
              src_image->bits.rowstride,
              dest_image->bits.rowstride,
              PIXMAN_FORMAT_BPP (src_image->bits.format),
              PIXMAN_FORMAT_BPP (dest_image->bits.format),
              src_x, src_y, dest_x, dest_y, width, height);
}

static void
msa_composite_over_x888_8_8888 (pixman_implementation_t *imp,
                                 pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint32_t    *src, *src_line, s;
    uint32_t    *dst, *dst_line, d;
    uint8_t         *mask, *mask_line;
    int src_stride, mask_stride, dst_stride;
    uint32_t w;
    v16i8 ms;

    v8i16 w_src, w_src_lo, w_src_hi;
    v8i16 w_dst, w_dst_lo, w_dst_hi;
    v8i16 w_mask, w_mask_lo, w_mask_hi;

    PIXMAN_IMAGE_GET_LINE (
        dest_image, dest_x, dest_y, uint32_t, dst_stride, dst_line, 1);
    PIXMAN_IMAGE_GET_LINE (
        mask_image, mask_x, mask_y, uint8_t, mask_stride, mask_line, 1);
    PIXMAN_IMAGE_GET_LINE (
        src_image, src_x, src_y, uint32_t, src_stride, src_line, 1);

    while (height--)
    {
        src = src_line;
        src_line += src_stride;
        dst = dst_line;
        dst_line += dst_stride;
        mask = mask_line;
        mask_line += mask_stride;

        w = width;

        while (w && (intptr_t)dst & 15)
        {
            int8_t m;
            m = *mask++;
            s = 0xff000000 | *src++;
            d = *dst;
            ms = unpack_32_1x128 (s);

            if (m != 0xff)
            {
                v8i16 ma = expand_alpha_rev_1x128 ((v8i16)unpack_32_1x128 (m));
                v16i8 md = unpack_32_1x128 (d);

                ms = (v16i8)in_over_1x128 (&ms, &mask_00ff, &ma, &md);
            }

            *dst++ = pack_1x128_32 ( (v8i16)ms);
            w--;
        }

        while (w >= 4)
        {
            uint32_t m;
            memcpy(&m, mask, sizeof(uint32_t));
            w_src = (v8i16)__msa_or_v (
                (v16u8)load_128_unaligned ((uint32_t *)src), (v16u8)mask_ff000000);

            if (m == 0xffffffff)
            {
                save_128_aligned (dst, (v4i32)w_src);
            }
            else
            {
                w_dst = (v8i16)load_128_aligned (dst);

                w_mask = __msa_ilvr_h ((v8i16)unpack_32_1x128 (m), (v8i16)zero);

                unpack_128_2x128 ((v16i8)w_src, &w_src_lo, &w_src_hi);
                unpack_128_2x128 ((v16i8)w_mask, &w_mask_lo, &w_mask_hi);
                unpack_128_2x128 ((v16i8)w_dst, &w_dst_lo, &w_dst_hi);

                expand_alpha_rev_2x128 (
                    w_mask_lo, w_mask_hi, &w_mask_lo, &w_mask_hi);

                in_over_2x128 (&w_src_lo, &w_src_hi,
                               &mask_00ff, &mask_00ff, &w_mask_lo, &w_mask_hi,
                               &w_dst_lo, &w_dst_hi);

                save_128_aligned (dst, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));
            }

            src += 4;
            dst += 4;
            mask += 4;
            w -= 4;
        }

        while (w)
        {
            uint8_t m;
            m = *mask++;

            if (m)
            {
                s = 0xff000000 | *src;

                if (m == 0xff)
                {
                    *dst = s;
                }
                else
                {
                    v16i8 ma, md, ms;

                    d = *dst;

                    ma = (v16i8)expand_alpha_rev_1x128 ((v8i16)unpack_32_1x128 (m));
                    md = unpack_32_1x128 (d);
                    ms = unpack_32_1x128 (s);

                    *dst = pack_1x128_32 ((v8i16)in_over_1x128 (&ms, &mask_00ff, &ma, &md));
                }

            }

            src++;
            dst++;
            w--;
        }
    }

}

static void
msa_composite_over_8888_8_8888 (pixman_implementation_t *imp,
                                 pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint32_t    *src, *src_line, s;
    uint32_t    *dst, *dst_line, d;
    uint8_t         *mask, *mask_line;
    int src_stride, mask_stride, dst_stride;
    uint32_t w;

    v8i16 w_src, w_src_lo, w_src_hi, w_srca_lo, w_srca_hi;
    v8i16 w_dst, w_dst_lo, w_dst_hi;
    v8i16 w_mask, w_mask_lo, w_mask_hi;

    PIXMAN_IMAGE_GET_LINE (
        dest_image, dest_x, dest_y, uint32_t, dst_stride, dst_line, 1);
    PIXMAN_IMAGE_GET_LINE (
        mask_image, mask_x, mask_y, uint8_t, mask_stride, mask_line, 1);
    PIXMAN_IMAGE_GET_LINE (
        src_image, src_x, src_y, uint32_t, src_stride, src_line, 1);

    while (height--)
    {
        src = src_line;
        src_line += src_stride;
        dst = dst_line;
        dst_line += dst_stride;
        mask = mask_line;
        mask_line += mask_stride;

        w = width;

        while (w && (intptr_t)dst & 15)
        {
            uint32_t sa;
            uint8_t m = *mask++;

            s = *src++;
            d = *dst;

            sa = s >> 24;

            if (m)
            {
                if (sa == 0xff && m == 0xff)
                {
                    *dst = s;
                }
                else
                {
                    v8i16 ms, md, ma, msa;

                    ma = expand_alpha_rev_1x128 ((v8i16)load_32_1x128 (m));
                    ms = (v8i16)unpack_32_1x128 (s);
                    md = (v8i16)unpack_32_1x128 (d);

                    msa = expand_alpha_rev_1x128 ((v8i16)load_32_1x128 (sa));

                    *dst = pack_1x128_32 ( (v8i16)in_over_1x128 ( &ms, &msa, &ma, &md));
                }
            }

            dst++;
            w--;
        }

        while (w >= 4)
        {
            uint32_t m;
            memcpy(&m, mask, sizeof(int32_t));

            if (m)
            {
                w_src = (v8i16)load_128_unaligned ((uint32_t*)src);

                if (m == 0xffffffff && is_opaque ((v16i8)w_src))
                {
                    save_128_aligned (dst, (v4i32)w_src);
                }
                else
                {
                    w_dst = (v8i16)load_128_aligned (dst);

                    w_mask = __msa_ilvr_h ((v8i16)unpack_32_1x128 (m), (v8i16)zero);

                    unpack_128_2x128 ((v16i8)w_src, &w_src_lo, &w_src_hi);
                    unpack_128_2x128 ((v16i8)w_mask, &w_mask_lo, &w_mask_hi);
                    unpack_128_2x128 ((v16i8)w_dst, &w_dst_lo, &w_dst_hi);

                    expand_alpha_2x128 (w_src_lo, w_src_hi, &w_srca_lo, &w_srca_hi);
                    expand_alpha_rev_2x128 (w_mask_lo, w_mask_hi, &w_mask_lo, &w_mask_hi);

                    in_over_2x128 (&w_src_lo,  &w_src_hi,  &w_srca_lo, &w_srca_hi,
                                   &w_mask_lo, &w_mask_hi, &w_dst_lo,  &w_dst_hi);

                    save_128_aligned (dst, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));
                }
            }

            src += 4;
            dst += 4;
            mask += 4;
            w -= 4;
        }

        while (w)
        {
            int32_t sa;
            int8_t m;
            m = *mask++;

            s = *src++;
            d = *dst;

            sa = s >> 24;

            if (m)
            {
                if (sa == 0xff && m == 0xff)
                {
                    *dst = s;
                }
                else
                {
                    
                    v16i8 ms, md, ma, msa;

                    ma = (v16i8)expand_alpha_rev_1x128 ((v8i16)load_32_1x128 (m));
                    ms = unpack_32_1x128 (s);
                    md = unpack_32_1x128 (d);

                    msa = (v16i8)expand_alpha_rev_1x128 ((v8i16)load_32_1x128 (sa));

                    *dst = pack_1x128_32 ((v8i16)in_over_1x128 (&ms, &msa, &ma, &md));
                }
            }

            dst++;
            w--;
        }
    }

}


static void
msa_composite_over_reverse_n_8888 (pixman_implementation_t *imp,
                                    pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint32_t src;
    uint32_t    *dst_line, *dst;
    v8i16 w_src;
    v8i16 w_dst, w_dst_lo, w_dst_hi;
    v8i16 w_dsta_hi, w_dsta_lo;
    int dst_stride;
    uint32_t w;

    src = _pixman_image_get_solid (imp, src_image, dest_image->bits.format);

    if (src == 0)
        return;

    PIXMAN_IMAGE_GET_LINE (
        dest_image, dest_x, dest_y, uint32_t, dst_stride, dst_line, 1);

    w_src = (v8i16)expand_pixel_32_1x128 (src);

    while (height--)
    {
        dst = dst_line;

        dst_line += dst_stride;
        w = width;

        while (w && (intptr_t)dst & 15)
        {
            v16i8 vd;

            vd = unpack_32_1x128 (*dst);

            *dst = pack_1x128_32 ( (v8i16)over_1x128 ( (v16u8)vd, (v16u8)expand_alpha_1x128 ((v8i16)vd),
                                              (v16u8)w_src));
            w--;
            dst++;
        }

        while (w >= 4)
        {
            v8i16 tmp_lo, tmp_hi;

            w_dst = (v8i16)load_128_aligned ((uint32_t *)dst);

            unpack_128_2x128 ((v16i8)w_dst, &w_dst_lo, &w_dst_hi);
            expand_alpha_2x128 (w_dst_lo, w_dst_hi, &w_dsta_lo, &w_dsta_hi);

            tmp_lo = w_src;
            tmp_hi = w_src;

            over_2x128 (&w_dst_lo, &w_dst_hi,
                        &w_dsta_lo, &w_dsta_hi,
                        &tmp_lo, &tmp_hi);

            save_128_aligned (
                (int32_t *)dst, (v4i32)pack_2x128_128 (tmp_lo, tmp_hi));

            w -= 4;
            dst += 4;
        }

        while (w)
        {
            v8i16 vd;

            vd = (v8i16)unpack_32_1x128 (*dst);

            *dst = pack_1x128_32 ( (v8i16)over_1x128 ((v16u8)vd, (v16u8)expand_alpha_1x128 (vd),
                                              (v16u8)w_src));
            w--;
            dst++;
        }

    }

}

static void
msa_composite_over_8888_8888_8888 (pixman_implementation_t *imp,
				    pixman_composite_info_t *info)
{
    PIXMAN_COMPOSITE_ARGS (info);
    uint32_t    *src, *src_line, s;
    uint32_t    *dst, *dst_line, d;
    uint32_t    *mask, *mask_line;
    uint32_t    m;
    int src_stride, mask_stride, dst_stride;
    int32_t w;

    v8i16 w_src, w_src_lo, w_src_hi, w_srca_lo, w_srca_hi;
    v8i16 w_dst, w_dst_lo, w_dst_hi;
    v8i16 w_mask, w_mask_lo, w_mask_hi;

    PIXMAN_IMAGE_GET_LINE (
	dest_image, dest_x, dest_y, uint32_t, dst_stride, dst_line, 1);
    PIXMAN_IMAGE_GET_LINE (
	mask_image, mask_x, mask_y, uint32_t, mask_stride, mask_line, 1);
    PIXMAN_IMAGE_GET_LINE (
	src_image, src_x, src_y, uint32_t, src_stride, src_line, 1);

    while (height--)
    {
        src = src_line;
        src_line += src_stride;
        dst = dst_line;
        dst_line += dst_stride;
        mask = mask_line;
        mask_line += mask_stride;

        w = width;

        while (w && (uintptr_t)dst & 15)
        {
	    uint32_t sa;

            s = *src++;
            m = (*mask++) >> 24;
            d = *dst;

	    sa = s >> 24;

	    if (m)
	    {
		if (sa == 0xff && m == 0xff)
		{
		    *dst = s;
		}
		else
		{
		    v8i16 ms, md, ma, msa;

		    ma = expand_alpha_rev_1x128 ((v8i16)load_32_1x128 (m));
		    ms = (v8i16)unpack_32_1x128 (s);
		    md = (v8i16)unpack_32_1x128 (d);

		    msa = expand_alpha_rev_1x128 ((v8i16)load_32_1x128 (sa));

		    *dst = pack_1x128_32 ((v8i16)in_over_1x128 (&ms, &msa, &ma, &md));
		}
	    }

	    dst++;
            w--;
        }

        while (w >= 4)
        {
	    w_mask = (v8i16)load_128_unaligned ((uint32_t*)mask);

	    if (!is_transparent ((v16i8)w_mask))
	    {
		w_src = (v8i16)load_128_unaligned ((uint32_t*)src);

		if (is_opaque ((v16i8)w_mask) && is_opaque ((v16i8)w_src))
		{
		    save_128_aligned ((int32_t *)dst, (v4i32)w_src);
		}
		else
		{
		    w_dst = (v8i16)load_128_aligned ((int32_t *)dst);

		    unpack_128_2x128 ((v16i8)w_src, &w_src_lo, &w_src_hi);
		    unpack_128_2x128 ((v16i8)w_mask, &w_mask_lo, &w_mask_hi);
		    unpack_128_2x128 ((v16i8)w_dst, &w_dst_lo, &w_dst_hi);

		    expand_alpha_2x128 (w_src_lo, w_src_hi, &w_srca_lo, &w_srca_hi);
		    expand_alpha_2x128 (w_mask_lo, w_mask_hi, &w_mask_lo, &w_mask_hi);

		    in_over_2x128 (&w_src_lo, &w_src_hi, &w_srca_lo, &w_srca_hi,
				   &w_mask_lo, &w_mask_hi, &w_dst_lo, &w_dst_hi);

		    save_128_aligned ((int32_t*)dst, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));
		}
	    }

            src += 4;
            dst += 4;
            mask += 4;
            w -= 4;
        }

        while (w)
        {
	    uint32_t sa;

            s = *src++;
            m = (*mask++) >> 24;
            d = *dst;

	    sa = s >> 24;

	    if (m)
	    {
		if (sa == 0xff && m == 0xff)
		{
		    *dst = s;
		}
		else
		{
		    v8i16 ms, md, ma, msa;

		    ma = expand_alpha_rev_1x128 ((v8i16)load_32_1x128 (m));
		    ms = (v8i16)unpack_32_1x128 (s);
		    md = (v8i16)unpack_32_1x128 (d);

		    msa = expand_alpha_rev_1x128 ((v8i16)load_32_1x128 (sa));

		    *dst = pack_1x128_32 ((v8i16)in_over_1x128 (&ms, &msa, &ma, &md));
		}
	    }

	    dst++;
            w--;
        }
    }

}

/* A variant of 'msa_combine_over_u' with minor tweaks */
static force_inline void
scaled_nearest_scanline_msa_8888_8888_OVER (uint32_t*        pd,
                                             const uint32_t* ps,
                                             int32_t         w,
                                             pixman_fixed_t  vx,
                                             pixman_fixed_t  unit_x,
                                             pixman_fixed_t  src_width_fixed,
                                             pixman_bool_t   fully_transparent_src)
{
    uint32_t s, d;
    const uint32_t* pm;

    v8i16 w_dst_lo, w_dst_hi;
    v8i16 w_src_lo, w_src_hi;
    v8i16 w_alpha_lo, w_alpha_hi;

    pm = NULL;

    if (fully_transparent_src)
	return;

    /* Align dst on a 16-byte boundary */
    while (w && ((uintptr_t)pd & 15))
    {
	d = *pd;
	s = combine1 (ps + pixman_fixed_to_int (vx), pm);
	vx += unit_x;
	while (vx >= 0)
	    vx -= src_width_fixed;

	*pd++ = core_combine_over_u_pixel_msa (s, d);
	if (pm)
	    pm++;
	w--;
    }

    while (w >= 4)
    {
	v4u32 tmp;
	uint32_t tmp1, tmp2, tmp3, tmp4;

	tmp1 = *(ps + pixman_fixed_to_int (vx));
	vx += unit_x;
	while (vx >= 0)
	    vx -= src_width_fixed;
	tmp2 = *(ps + pixman_fixed_to_int (vx));
	vx += unit_x;
	while (vx >= 0)
	    vx -= src_width_fixed;
	tmp3 = *(ps + pixman_fixed_to_int (vx));
	vx += unit_x;
	while (vx >= 0)
	    vx -= src_width_fixed;
	tmp4 = *(ps + pixman_fixed_to_int (vx));
	vx += unit_x;
	while (vx >= 0)
	    vx -= src_width_fixed;

	tmp = __msa_set_u_w (tmp4, tmp3, tmp2, tmp1);

	w_src_hi = (v8i16)combine4 (&tmp, pm);

	if (is_opaque ((v16i8)w_src_hi))
	{
	    save_128_aligned ((int32_t*)pd, (v4i32)w_src_hi);
	}
	else if (!is_zero ((v16i8)w_src_hi))
	{
	    w_dst_hi = (v8i16)load_128_aligned ((int32_t*) pd);

	    unpack_128_2x128 ((v16i8)w_src_hi, &w_src_lo, &w_src_hi);
	    unpack_128_2x128 ((v16i8)w_dst_hi, &w_dst_lo, &w_dst_hi);

	    expand_alpha_2x128 (
		w_src_lo, w_src_hi, &w_alpha_lo, &w_alpha_hi);

	    over_2x128 (&w_src_lo, &w_src_hi,
			&w_alpha_lo, &w_alpha_hi,
			&w_dst_lo, &w_dst_hi);

	    /* rebuid the 4 pixel data and save*/
	    save_128_aligned ((int32_t*)pd,
			      (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));
	}

	w -= 4;
	pd += 4;
	if (pm)
	    pm += 4;
    }

    while (w)
    {
	d = *pd;
	s = combine1 (ps + pixman_fixed_to_int (vx), pm);
	vx += unit_x;
	while (vx >= 0)
	    vx -= src_width_fixed;

	*pd++ = core_combine_over_u_pixel_msa (s, d);
	if (pm)
	    pm++;

	w--;
    }
}

FAST_NEAREST_MAINLOOP (msa_8888_8888_cover_OVER,
		       scaled_nearest_scanline_msa_8888_8888_OVER,
		       uint32_t, uint32_t, COVER)
FAST_NEAREST_MAINLOOP (msa_8888_8888_none_OVER,
		       scaled_nearest_scanline_msa_8888_8888_OVER,
		       uint32_t, uint32_t, NONE)
FAST_NEAREST_MAINLOOP (msa_8888_8888_pad_OVER,
		       scaled_nearest_scanline_msa_8888_8888_OVER,
		       uint32_t, uint32_t, PAD)
FAST_NEAREST_MAINLOOP (msa_8888_8888_normal_OVER,
		       scaled_nearest_scanline_msa_8888_8888_OVER,
		       uint32_t, uint32_t, NORMAL)

static force_inline void
scaled_nearest_scanline_msa_8888_n_8888_OVER (const uint32_t  * mask,
					       uint32_t *       dst,
					       const uint32_t * src,
					       int32_t          w,
					       pixman_fixed_t   vx,
					       pixman_fixed_t   unit_x,
					       pixman_fixed_t   src_width_fixed,
					       pixman_bool_t    zero_src)
{
    v8i16 w_mask;
    v8i16 w_src, w_src_lo, w_src_hi;
    v8i16 w_dst, w_dst_lo, w_dst_hi;
    v8i16 w_alpha_lo, w_alpha_hi;

    if (zero_src || (*mask >> 24) == 0)
	return;

    w_mask = create_mask_16_128 (*mask >> 24);

    while (w && (uintptr_t)dst & 15)
    {
	uint32_t s;
        s = *(src + pixman_fixed_to_int (vx));
	vx += unit_x;
	while (vx >= 0)
	    vx -= src_width_fixed;

	if (s)
	{
	    uint32_t d = *dst;

	    v8i16 ms = (v8i16)unpack_32_1x128 (s);
	    v8i16 alpha     = expand_alpha_1x128 (ms);
	    v8i16 dest      = w_mask;
	    v8i16 alpha_dst = (v8i16)unpack_32_1x128 (d);

	    *dst = pack_1x128_32 (
		(v8i16)in_over_1x128 (&ms, &alpha, &dest, &alpha_dst));
	}
	dst++;
	w--;
    }

    while (w >= 4)
    {
	uint32_t tmp1, tmp2, tmp3, tmp4;

	tmp1 = *(src + pixman_fixed_to_int (vx));
	vx += unit_x;
	while (vx >= 0)
	    vx -= src_width_fixed;
	tmp2 = *(src + pixman_fixed_to_int (vx));
	vx += unit_x;
	while (vx >= 0)
	    vx -= src_width_fixed;
	tmp3 = *(src + pixman_fixed_to_int (vx));
	vx += unit_x;
	while (vx >= 0)
	    vx -= src_width_fixed;
	tmp4 = *(src + pixman_fixed_to_int (vx));
	vx += unit_x;
	while (vx >= 0)
	    vx -= src_width_fixed;

	w_src = (v8i16)__msa_set_s_w (tmp4, tmp3, tmp2, tmp1);

	if (!is_zero ((v16i8)w_src))
	{
	    w_dst = (v8i16)load_128_aligned ((int32_t*)dst);

	    unpack_128_2x128 ((v16i8)w_src, &w_src_lo, &w_src_hi);
	    unpack_128_2x128 ((v16i8)w_dst, &w_dst_lo, &w_dst_hi);
	    expand_alpha_2x128 (w_src_lo, w_src_hi,
			        &w_alpha_lo, &w_alpha_hi);

	    in_over_2x128 (&w_src_lo, &w_src_hi,
			   &w_alpha_lo, &w_alpha_hi,
			   &w_mask, &w_mask,
			   &w_dst_lo, &w_dst_hi);

	    save_128_aligned (
		(int32_t*)dst, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));
	}

	dst += 4;
	w -= 4;
    }

    while (w)
    {
	uint32_t s;
        s = *(src + pixman_fixed_to_int (vx));
	vx += unit_x;
	while (vx >= 0)
	    vx -= src_width_fixed;

	if (s)
	{
	    uint32_t d = *dst;

	    v8i16 ms = (v8i16)unpack_32_1x128 (s);
	    v8i16 alpha = expand_alpha_1x128 (ms);
	    v8i16 mask  = w_mask;
	    v8i16 dest  = (v8i16)unpack_32_1x128 (d);

	    *dst = pack_1x128_32 (
		(v8i16)in_over_1x128 (&ms, &alpha, &mask, &dest));
	}

	dst++;
	w--;
    }

}

FAST_NEAREST_MAINLOOP_COMMON (msa_8888_n_8888_cover_OVER,
			      scaled_nearest_scanline_msa_8888_n_8888_OVER,
			      uint32_t, uint32_t, uint32_t, COVER, TRUE, TRUE)
FAST_NEAREST_MAINLOOP_COMMON (msa_8888_n_8888_pad_OVER,
			      scaled_nearest_scanline_msa_8888_n_8888_OVER,
			      uint32_t, uint32_t, uint32_t, PAD, TRUE, TRUE)
FAST_NEAREST_MAINLOOP_COMMON (msa_8888_n_8888_none_OVER,
			      scaled_nearest_scanline_msa_8888_n_8888_OVER,
			      uint32_t, uint32_t, uint32_t, NONE, TRUE, TRUE)
FAST_NEAREST_MAINLOOP_COMMON (msa_8888_n_8888_normal_OVER,
			      scaled_nearest_scanline_msa_8888_n_8888_OVER,
			      uint32_t, uint32_t, uint32_t, NORMAL, TRUE, TRUE)

/************************************************************************/

# define BILINEAR_DECLARE_VARIABLES                                                             \
    const v8i16 w_wt = __msa_set_s_h (wt, wt, wt, wt, wt, wt, wt, wt);                        \
    const v8i16 w_wb = __msa_set_s_h (wb, wb, wb, wb, wb, wb, wb, wb);                        \
    const v8i16 w_addc = __msa_set_s_h (0, 1, 0, 1, 0, 1, 0, 1);                              \
    const v8i16 w_ux1 = __msa_set_s_h (unit_x, -unit_x, unit_x, -unit_x,                      \
                                          unit_x, -unit_x, unit_x, -unit_x);                    \
    const v8i16 w_ux4 = __msa_set_s_h (unit_x * 4, -unit_x * 4,                               \
                                           unit_x * 4, -unit_x * 4,                             \
                                           unit_x * 4, -unit_x * 4,                             \
                                           unit_x * 4, -unit_x * 4);                            \
    const v8i16 w_zero = {0, 0, 0, 0};                                                        \
    v8i16 w_x = __msa_set_s_h (vx, -(vx + 1), vx, -(vx + 1),                                  \
                                   vx, -(vx + 1), vx, -(vx + 1))

#define BILINEAR_INTERPOLATE_ONE_PIXEL_HELPER(pix, phase)                                       \
do {                                                                                            \
    v8i16 w_wh, w_a, w_b;                                                                 \
    /* fetch 2x2 pixel block into sse2 registers */                                             \
    v2i64 tltr = __msa_pckev_d (__msa_ld_d (&src_top[vx >> 16], 0), (v2i64) zero);               \
    v2i64 blbr = __msa_pckev_d (__msa_ld_d (&src_bottom[vx >> 16], 0), (v2i64) zero);            \
    (void)w_ux4; /* suppress warning: unused variable 'w_ux4' */                            \
    vx += unit_x;                                                                               \
    /* vertical interpolation */                                                                \
    w_a = __msa_mulv_h ((v8i16)__msa_ilvr_b ((v16i8)tltr, (v16i8)w_zero), w_wt);       \
    w_b = __msa_mulv_h ((v8i16)__msa_ilvr_b ((v16i8)blbr, (v16i8)w_zero), w_wb);       \
    w_a = __msa_adds_s_h (w_a, w_b);                                                      \
    /* calculate horizontal weights */                                                          \
    w_wh = __msa_adds_s_h (w_addc, __msa_srli_h (w_x,                                     \
                                        16 - BILINEAR_INTERPOLATION_BITS));                     \
    w_x = __msa_adds_s_h (w_x, w_ux1);                                                    \
    /* horizontal interpolation */                                                              \
    w_b = (v8i16)__msa_ilvr_d (/* any value is fine here */ (v2i64) w_b, (v2i64) w_a);   \
    w_a = (v8i16)msa_madd_h (__msa_ilvr_h (w_b, w_a), w_wh);                           \
    /* shift the result */                                                                      \
    pix = (v8i16)__msa_srli_w ((v4i32)w_a, BILINEAR_INTERPOLATION_BITS * 2);                \
} while (0)

/***********************************************************************************/

#define BILINEAR_INTERPOLATE_ONE_PIXEL(pix);					                \
do {										                \
	v8i16 w_pix;							                        \
	BILINEAR_INTERPOLATE_ONE_PIXEL_HELPER (w_pix, -1);			                \
	w_pix = __msa_pckev_h ((v8i16)__msa_sat_s_w ((v4i32)w_pix, 15),                      \
                                 (v8i16)__msa_sat_s_w ((v4i32)w_pix, 15));			\
	w_pix = (v8i16)__msa_pckev_b((v16i8)__msa_sat_u_h ((v8u16)w_pix, 7),                \
                                       (v16i8)__msa_sat_u_h ((v8u16)w_pix, 7));		\
	pix = __msa_copy_u_w ((v4i32)w_pix, 0);                                               \
} while(0)

#define BILINEAR_INTERPOLATE_FOUR_PIXELS(pix);					                \
do {										                \
	v8i16 w_pix1, w_pix2, w_pix3, w_pix4;				                \
	BILINEAR_INTERPOLATE_ONE_PIXEL_HELPER (w_pix1, 0);			                \
	BILINEAR_INTERPOLATE_ONE_PIXEL_HELPER (w_pix2, 1);			                \
	BILINEAR_INTERPOLATE_ONE_PIXEL_HELPER (w_pix3, 2);			                \
	BILINEAR_INTERPOLATE_ONE_PIXEL_HELPER (w_pix4, 3);			                \
	w_pix1 = __msa_pckev_h ((v8i16)__msa_sat_s_w ((v4i32)w_pix2, 15),                    \
                                  (v8i16)__msa_sat_s_w ((v4i32)w_pix1, 15));			\
	w_pix3 = __msa_pckev_h ((v8i16)__msa_sat_s_w ((v4i32)w_pix4, 15),                    \
                                  (v8i16)__msa_sat_s_w ((v4i32)w_pix3, 15));			\
	pix = (v8i16)__msa_pckev_b ((v16i8)__msa_sat_u_h ((v8u16)w_pix3, 7),                   \
                                    (v16i8)__msa_sat_u_h ((v8u16)w_pix1, 7));                  \
} while(0)

#define BILINEAR_SKIP_ONE_PIXEL()						                \
do {										                \
    vx += unit_x;								                \
    w_x = __msa_addv_h (w_x, w_ux1);					                \
} while(0)

#define BILINEAR_SKIP_FOUR_PIXELS()						                \
do {										                \
    vx += unit_x * 4;								                \
    w_x = __msa_addv_h (w_x, w_ux4);					                \
} while(0)

/***********************************************************************************/


static force_inline void
scaled_bilinear_scanline_msa_8888_8888_SRC (uint32_t *        dst,
					     const uint32_t * mask,
					     const uint32_t * src_top,
					     const uint32_t * src_bottom,
					     int32_t          w,
					     int              wt,
					     int              wb,
					     pixman_fixed_t   vx_,
					     pixman_fixed_t   unit_x_,
					     pixman_fixed_t   max_vx,
					     pixman_bool_t    zero_src)
{
    intptr_t vx = vx_;
    intptr_t unit_x = unit_x_;
    BILINEAR_DECLARE_VARIABLES;
    uint32_t pix1, pix2;

    while (w && ((uintptr_t)dst & 15))
    {
	BILINEAR_INTERPOLATE_ONE_PIXEL (pix1);
	*dst++ = pix1;
	w--;
    }

    while ((w -= 4) >= 0) {
	v8i16 w_src;
	BILINEAR_INTERPOLATE_FOUR_PIXELS (w_src);
	__msa_st_h (w_src, dst, 0);
	dst += 4;
    }

    if (w & 2)
    {
	BILINEAR_INTERPOLATE_ONE_PIXEL (pix1);
	BILINEAR_INTERPOLATE_ONE_PIXEL (pix2);
	*dst++ = pix1;
	*dst++ = pix2;
    }

    if (w & 1)
    {
	BILINEAR_INTERPOLATE_ONE_PIXEL (pix1);
	*dst = pix1;
    }

}

FAST_BILINEAR_MAINLOOP_COMMON (msa_8888_8888_cover_SRC,
			       scaled_bilinear_scanline_msa_8888_8888_SRC,
			       uint32_t, uint32_t, uint32_t,
			       COVER, FLAG_NONE)
FAST_BILINEAR_MAINLOOP_COMMON (msa_8888_8888_pad_SRC,
			       scaled_bilinear_scanline_msa_8888_8888_SRC,
			       uint32_t, uint32_t, uint32_t,
			       PAD, FLAG_NONE)
FAST_BILINEAR_MAINLOOP_COMMON (msa_8888_8888_none_SRC,
			       scaled_bilinear_scanline_msa_8888_8888_SRC,
			       uint32_t, uint32_t, uint32_t,
			       NONE, FLAG_NONE)
FAST_BILINEAR_MAINLOOP_COMMON (msa_8888_8888_normal_SRC,
			       scaled_bilinear_scanline_msa_8888_8888_SRC,
			       uint32_t, uint32_t, uint32_t,
			       NORMAL, FLAG_NONE)

static force_inline void
scaled_bilinear_scanline_msa_x888_8888_SRC (uint32_t *        dst,
					     const uint32_t * mask,
					     const uint32_t * src_top,
					     const uint32_t * src_bottom,
					     int32_t          w,
					     int              wt,
					     int              wb,
					     pixman_fixed_t   vx_,
					     pixman_fixed_t   unit_x_,
					     pixman_fixed_t   max_vx,
					     pixman_bool_t    zero_src)
{
    intptr_t vx = vx_;
    intptr_t unit_x = unit_x_;
    BILINEAR_DECLARE_VARIABLES;
    uint32_t pix1, pix2;

    while (w && ((uintptr_t)dst & 15))
    {
	BILINEAR_INTERPOLATE_ONE_PIXEL (pix1);
	*dst++ = pix1 | 0xFF000000;
	w--;
    }

    while ((w -= 4) >= 0) {
	v8i16 w_src;
	BILINEAR_INTERPOLATE_FOUR_PIXELS (w_src);
	__msa_st_h ((v8i16)__msa_or_v ((v16u8)w_src, (v16u8)mask_ff000000), dst, 0);
	dst += 4;
    }

    if (w & 2)
    {
	BILINEAR_INTERPOLATE_ONE_PIXEL (pix1);
	BILINEAR_INTERPOLATE_ONE_PIXEL (pix2);
	*dst++ = pix1 | 0xFF000000;
	*dst++ = pix2 | 0xFF000000;
    }

    if (w & 1)
    {
	BILINEAR_INTERPOLATE_ONE_PIXEL (pix1);
	*dst = pix1 | 0xFF000000;
    }
}

FAST_BILINEAR_MAINLOOP_COMMON (msa_x888_8888_cover_SRC,
			       scaled_bilinear_scanline_msa_x888_8888_SRC,
			       uint32_t, uint32_t, uint32_t,
			       COVER, FLAG_NONE)
FAST_BILINEAR_MAINLOOP_COMMON (msa_x888_8888_pad_SRC,
			       scaled_bilinear_scanline_msa_x888_8888_SRC,
			       uint32_t, uint32_t, uint32_t,
			       PAD, FLAG_NONE)
FAST_BILINEAR_MAINLOOP_COMMON (msa_x888_8888_normal_SRC,
			       scaled_bilinear_scanline_msa_x888_8888_SRC,
			       uint32_t, uint32_t, uint32_t,
			       NORMAL, FLAG_NONE)

static force_inline void
scaled_bilinear_scanline_msa_8888_8888_OVER (uint32_t *       dst,
					      const uint32_t * mask,
					      const uint32_t * src_top,
					      const uint32_t * src_bottom,
					      int32_t          w,
					      int              wt,
					      int              wb,
					      pixman_fixed_t   vx_,
					      pixman_fixed_t   unit_x_,
					      pixman_fixed_t   max_vx,
					      pixman_bool_t    zero_src)
{
    intptr_t vx = vx_;
    intptr_t unit_x = unit_x_;
    BILINEAR_DECLARE_VARIABLES;
    uint32_t pix1, pix2;

    while (w && ((uintptr_t)dst & 15))
    {
	BILINEAR_INTERPOLATE_ONE_PIXEL (pix1);

	if (pix1)
	{
	    pix2 = *dst;
	    *dst = core_combine_over_u_pixel_msa (pix1, pix2);
	}

	w--;
	dst++;
    }

    while (w  >= 4)
    {
	v8i16 w_src;
	v8i16 w_src_hi, w_src_lo, w_dst_hi, w_dst_lo;
	v8i16 w_alpha_hi, w_alpha_lo;

	BILINEAR_INTERPOLATE_FOUR_PIXELS (w_src);

	if (!is_zero ((v16i8)w_src))
	{
	    if (is_opaque ((v16i8)w_src))
	    {
		save_128_aligned ((int32_t *)dst, (v4i32)w_src);
	    }
	    else
	    {
		v8i16 w_dst = (v8i16)load_128_aligned ((int32_t *)dst);

		unpack_128_2x128 ((v16i8)w_src, &w_src_lo, &w_src_hi);
		unpack_128_2x128 ((v16i8)w_dst, &w_dst_lo, &w_dst_hi);

		expand_alpha_2x128 (w_src_lo, w_src_hi, &w_alpha_lo, &w_alpha_hi);
		over_2x128 (&w_src_lo, &w_src_hi, &w_alpha_lo, &w_alpha_hi,
			    &w_dst_lo, &w_dst_hi);

		save_128_aligned ((int32_t *)dst, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));
	    }
	}

	w -= 4;
	dst += 4;
    }

    while (w)
    {
	BILINEAR_INTERPOLATE_ONE_PIXEL (pix1);

	if (pix1)
	{
	    pix2 = *dst;
	    *dst = core_combine_over_u_pixel_msa (pix1, pix2);
	}

	w--;
	dst++;
    }
}

FAST_BILINEAR_MAINLOOP_COMMON (msa_8888_8888_cover_OVER,
			       scaled_bilinear_scanline_msa_8888_8888_OVER,
			       uint32_t, uint32_t, uint32_t,
			       COVER, FLAG_NONE)
FAST_BILINEAR_MAINLOOP_COMMON (msa_8888_8888_pad_OVER,
			       scaled_bilinear_scanline_msa_8888_8888_OVER,
			       uint32_t, uint32_t, uint32_t,
			       PAD, FLAG_NONE)
FAST_BILINEAR_MAINLOOP_COMMON (msa_8888_8888_none_OVER,
			       scaled_bilinear_scanline_msa_8888_8888_OVER,
			       uint32_t, uint32_t, uint32_t,
			       NONE, FLAG_NONE)
FAST_BILINEAR_MAINLOOP_COMMON (msa_8888_8888_normal_OVER,
			       scaled_bilinear_scanline_msa_8888_8888_OVER,
			       uint32_t, uint32_t, uint32_t,
			       NORMAL, FLAG_NONE)

static force_inline void
scaled_bilinear_scanline_msa_8888_8_8888_OVER (uint32_t *        dst,
						const uint8_t  * mask,
						const uint32_t * src_top,
						const uint32_t * src_bottom,
						int32_t          w,
						int              wt,
						int              wb,
						pixman_fixed_t   vx_,
						pixman_fixed_t   unit_x_,
						pixman_fixed_t   max_vx,
						pixman_bool_t    zero_src)
{
    intptr_t vx = vx_;
    intptr_t unit_x = unit_x_;
    BILINEAR_DECLARE_VARIABLES;
    uint32_t pix1, pix2;

    while (w && ((uintptr_t)dst & 15))
    {
	uint32_t sa;
	uint8_t m = *mask++;

	if (m)
	{
	    BILINEAR_INTERPOLATE_ONE_PIXEL (pix1);
	    sa = pix1 >> 24;

	    if (sa == 0xff && m == 0xff)
	    {
		*dst = pix1;
	    }
	    else
	    {
		v8i16 ms, md, ma, msa;

		pix2 = *dst;
		ma = expand_alpha_rev_1x128 ((v8i16)load_32_1x128 (m));
		ms = (v8i16)unpack_32_1x128 (pix1);
		md = (v8i16)unpack_32_1x128 (pix2);

		msa = expand_alpha_rev_1x128 ((v8i16)load_32_1x128 (sa));

		*dst = pack_1x128_32 ((v8i16)in_over_1x128 (&ms, &msa, &ma, &md));
	    }
	}
	else
	{
	    BILINEAR_SKIP_ONE_PIXEL ();
	}

	w--;
	dst++;
    }

    while (w >= 4)
    {
        uint32_t m;

	v8i16 w_src, w_src_lo, w_src_hi, w_srca_lo, w_srca_hi;
	v8i16 w_dst, w_dst_lo, w_dst_hi;
	v8i16 w_mask, w_mask_lo, w_mask_hi;

        memcpy(&m, mask, sizeof(uint32_t));

	if (m)
	{
	    BILINEAR_INTERPOLATE_FOUR_PIXELS (w_src);

	    if (m == 0xffffffff && is_opaque ((v16i8)w_src))
	    {
		save_128_aligned ((int32_t *)dst, (v4i32)w_src);
	    }
	    else
	    {
		w_dst = (v8i16)load_128_aligned ((int32_t *)dst);

		w_mask = __msa_ilvr_h (__msa_fill_h (0), (v8i16)unpack_32_1x128 (m));

		unpack_128_2x128 ((v16i8)w_src,  &w_src_lo,  &w_src_hi);
		unpack_128_2x128 ((v16i8)w_mask, &w_mask_lo, &w_mask_hi);
		unpack_128_2x128 ((v16i8)w_dst,  &w_dst_lo,  &w_dst_hi);

		expand_alpha_2x128 (w_src_lo, w_src_hi, &w_srca_lo, &w_srca_hi);
		expand_alpha_rev_2x128 (w_mask_lo, w_mask_hi, &w_mask_lo, &w_mask_hi);

		in_over_2x128 (&w_src_lo, &w_src_hi, &w_srca_lo, &w_srca_hi,
			       &w_mask_lo, &w_mask_hi, &w_dst_lo, &w_dst_hi);

		save_128_aligned ((int32_t*)dst, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));
	    }
	}
	else
	{
	    BILINEAR_SKIP_FOUR_PIXELS ();
	}

	w -= 4;
	dst += 4;
	mask += 4;
    }

    while (w)
    {
	uint32_t sa;
	uint8_t m = *mask++;

	if (m)
	{
	    BILINEAR_INTERPOLATE_ONE_PIXEL (pix1);
	    sa = pix1 >> 24;

	    if (sa == 0xff && m == 0xff)
	    {
		*dst = pix1;
	    }
	    else
	    {
		v8i16 ms, md, ma, msa;

		pix2 = *dst;
		ma = expand_alpha_rev_1x128 ((v8i16)load_32_1x128 (m));
		ms = (v8i16)unpack_32_1x128 (pix1);
		md = (v8i16)unpack_32_1x128 (pix2);

		msa = expand_alpha_rev_1x128 ((v8i16)load_32_1x128 (sa));

		*dst = pack_1x128_32 ((v8i16)in_over_1x128 (&ms, &msa, &ma, &md));
	    }
	}
	else
	{
	    BILINEAR_SKIP_ONE_PIXEL ();
	}

	w--;
	dst++;
    }
}

FAST_BILINEAR_MAINLOOP_COMMON (msa_8888_8_8888_cover_OVER,
			       scaled_bilinear_scanline_msa_8888_8_8888_OVER,
			       uint32_t, uint8_t, uint32_t,
			       COVER, FLAG_HAVE_NON_SOLID_MASK)
FAST_BILINEAR_MAINLOOP_COMMON (msa_8888_8_8888_pad_OVER,
			       scaled_bilinear_scanline_msa_8888_8_8888_OVER,
			       uint32_t, uint8_t, uint32_t,
			       PAD, FLAG_HAVE_NON_SOLID_MASK)
FAST_BILINEAR_MAINLOOP_COMMON (msa_8888_8_8888_none_OVER,
			       scaled_bilinear_scanline_msa_8888_8_8888_OVER,
			       uint32_t, uint8_t, uint32_t,
			       NONE, FLAG_HAVE_NON_SOLID_MASK)
FAST_BILINEAR_MAINLOOP_COMMON (msa_8888_8_8888_normal_OVER,
			       scaled_bilinear_scanline_msa_8888_8_8888_OVER,
			       uint32_t, uint8_t, uint32_t,
			       NORMAL, FLAG_HAVE_NON_SOLID_MASK)


static force_inline void
scaled_bilinear_scanline_msa_8888_n_8888_OVER (uint32_t *        dst,
						const uint32_t * mask,
						const uint32_t * src_top,
						const uint32_t * src_bottom,
						int32_t          w,
						int              wt,
						int              wb,
						pixman_fixed_t   vx_,
						pixman_fixed_t   unit_x_,
						pixman_fixed_t   max_vx,
						pixman_bool_t    zero_src)
{
    intptr_t vx = vx_;
    intptr_t unit_x = unit_x_;
    BILINEAR_DECLARE_VARIABLES;
    uint32_t pix1;
    v8i16 w_mask;

    if (zero_src || (*mask >> 24) == 0)
	return;

    w_mask = create_mask_16_128 (*mask >> 24);

    while (w && ((uintptr_t)dst & 15))
    {
	BILINEAR_INTERPOLATE_ONE_PIXEL (pix1);
	if (pix1)
	{
		uint32_t d = *dst;

		v8i16 ms        = (v8i16)unpack_32_1x128 (pix1);
		v8i16 alpha     = expand_alpha_1x128 (ms);
		v8i16 dest      = w_mask;
		v8i16 alpha_dst = (v8i16)unpack_32_1x128 (d);

		*dst = pack_1x128_32
			((v8i16)in_over_1x128 (&ms, &alpha, &dest, &alpha_dst));
	}

	dst++;
	w--;
    }

    while (w >= 4)
    {
	v8i16 w_src;
	BILINEAR_INTERPOLATE_FOUR_PIXELS (w_src);

	if (!is_zero ((v16i8)w_src))
	{
	    v8i16 w_src_lo, w_src_hi;
	    v8i16 w_dst, w_dst_lo, w_dst_hi;
	    v8i16 w_alpha_lo, w_alpha_hi;

	    w_dst = (v8i16)load_128_aligned ((int32_t*)dst);

	    unpack_128_2x128 ((v16i8)w_src, &w_src_lo, &w_src_hi);
	    unpack_128_2x128 ((v16i8)w_dst, &w_dst_lo, &w_dst_hi);
	    expand_alpha_2x128 (w_src_lo, w_src_hi,
				&w_alpha_lo, &w_alpha_hi);

	    in_over_2x128 (&w_src_lo, &w_src_hi,
			   &w_alpha_lo, &w_alpha_hi,
			   &w_mask, &w_mask,
			   &w_dst_lo, &w_dst_hi);

	    save_128_aligned
		((int32_t*)dst, (v4i32)pack_2x128_128 (w_dst_lo, w_dst_hi));
	}

	dst += 4;
	w -= 4;
    }

    while (w)
    {
	BILINEAR_INTERPOLATE_ONE_PIXEL (pix1);
	if (pix1)
	{
		uint32_t d = *dst;

		v8i16 ms        = (v8i16)unpack_32_1x128 (pix1);
		v8i16 alpha     = expand_alpha_1x128 (ms);
		v8i16 dest      = w_mask;
		v8i16 alpha_dst = (v8i16)unpack_32_1x128 (d);

		*dst = pack_1x128_32
			((v8i16)in_over_1x128 (&ms, &alpha, &dest, &alpha_dst));
	}

	dst++;
	w--;
    }
}


FAST_BILINEAR_MAINLOOP_COMMON (msa_8888_n_8888_cover_OVER,
			       scaled_bilinear_scanline_msa_8888_n_8888_OVER,
			       uint32_t, uint32_t, uint32_t,
			       COVER, FLAG_HAVE_SOLID_MASK)
FAST_BILINEAR_MAINLOOP_COMMON (msa_8888_n_8888_pad_OVER,
			       scaled_bilinear_scanline_msa_8888_n_8888_OVER,
			       uint32_t, uint32_t, uint32_t,
			       PAD, FLAG_HAVE_SOLID_MASK)
FAST_BILINEAR_MAINLOOP_COMMON (msa_8888_n_8888_none_OVER,
			       scaled_bilinear_scanline_msa_8888_n_8888_OVER,
			       uint32_t, uint32_t, uint32_t,
			       NONE, FLAG_HAVE_SOLID_MASK)
FAST_BILINEAR_MAINLOOP_COMMON (msa_8888_n_8888_normal_OVER,
			       scaled_bilinear_scanline_msa_8888_n_8888_OVER,
			       uint32_t, uint32_t, uint32_t,
			       NORMAL, FLAG_HAVE_SOLID_MASK)

static const pixman_fast_path_t msa_fast_paths[] =
{
    /* PIXMAN_OP_OVER */
    PIXMAN_STD_FAST_PATH (OVER, solid, a8, r5g6b5, msa_composite_over_n_8_0565),
    PIXMAN_STD_FAST_PATH (OVER, solid, a8, b5g6r5, msa_composite_over_n_8_0565),
    PIXMAN_STD_FAST_PATH (OVER, solid, null, a8r8g8b8, msa_composite_over_n_8888),
    PIXMAN_STD_FAST_PATH (OVER, solid, null, x8r8g8b8, msa_composite_over_n_8888),
    PIXMAN_STD_FAST_PATH (OVER, solid, null, r5g6b5, msa_composite_over_n_0565),
    PIXMAN_STD_FAST_PATH (OVER, solid, null, b5g6r5, msa_composite_over_n_0565),
    PIXMAN_STD_FAST_PATH (OVER, a8r8g8b8, null, a8r8g8b8, msa_composite_over_8888_8888),
    PIXMAN_STD_FAST_PATH (OVER, a8r8g8b8, null, x8r8g8b8, msa_composite_over_8888_8888),
    PIXMAN_STD_FAST_PATH (OVER, a8b8g8r8, null, a8b8g8r8, msa_composite_over_8888_8888),
    PIXMAN_STD_FAST_PATH (OVER, a8b8g8r8, null, x8b8g8r8, msa_composite_over_8888_8888),
    PIXMAN_STD_FAST_PATH (OVER, a8r8g8b8, null, r5g6b5, msa_composite_over_8888_0565),
    PIXMAN_STD_FAST_PATH (OVER, a8b8g8r8, null, b5g6r5, msa_composite_over_8888_0565),
    PIXMAN_STD_FAST_PATH (OVER, solid, a8, a8r8g8b8, msa_composite_over_n_8_8888),
    PIXMAN_STD_FAST_PATH (OVER, solid, a8, x8r8g8b8, msa_composite_over_n_8_8888),
    PIXMAN_STD_FAST_PATH (OVER, solid, a8, a8b8g8r8, msa_composite_over_n_8_8888),
    PIXMAN_STD_FAST_PATH (OVER, solid, a8, x8b8g8r8, msa_composite_over_n_8_8888),
    PIXMAN_STD_FAST_PATH (OVER, a8r8g8b8, a8r8g8b8, a8r8g8b8, msa_composite_over_8888_8888_8888),
    PIXMAN_STD_FAST_PATH (OVER, a8r8g8b8, a8, x8r8g8b8, msa_composite_over_8888_8_8888),
    PIXMAN_STD_FAST_PATH (OVER, a8r8g8b8, a8, a8r8g8b8, msa_composite_over_8888_8_8888),
    PIXMAN_STD_FAST_PATH (OVER, a8b8g8r8, a8, x8b8g8r8, msa_composite_over_8888_8_8888),
    PIXMAN_STD_FAST_PATH (OVER, a8b8g8r8, a8, a8b8g8r8, msa_composite_over_8888_8_8888),
    PIXMAN_STD_FAST_PATH (OVER, x8r8g8b8, a8, x8r8g8b8, msa_composite_over_x888_8_8888),
    PIXMAN_STD_FAST_PATH (OVER, x8r8g8b8, a8, a8r8g8b8, msa_composite_over_x888_8_8888),
    PIXMAN_STD_FAST_PATH (OVER, x8b8g8r8, a8, x8b8g8r8, msa_composite_over_x888_8_8888),
    PIXMAN_STD_FAST_PATH (OVER, x8b8g8r8, a8, a8b8g8r8, msa_composite_over_x888_8_8888),
    PIXMAN_STD_FAST_PATH (OVER, x8r8g8b8, solid, a8r8g8b8, msa_composite_over_x888_n_8888),
    PIXMAN_STD_FAST_PATH (OVER, x8r8g8b8, solid, x8r8g8b8, msa_composite_over_x888_n_8888),
    PIXMAN_STD_FAST_PATH (OVER, x8b8g8r8, solid, a8b8g8r8, msa_composite_over_x888_n_8888),
    PIXMAN_STD_FAST_PATH (OVER, x8b8g8r8, solid, x8b8g8r8, msa_composite_over_x888_n_8888),
    PIXMAN_STD_FAST_PATH (OVER, a8r8g8b8, solid, a8r8g8b8, msa_composite_over_8888_n_8888),
    PIXMAN_STD_FAST_PATH (OVER, a8r8g8b8, solid, x8r8g8b8, msa_composite_over_8888_n_8888),
    PIXMAN_STD_FAST_PATH (OVER, a8b8g8r8, solid, a8b8g8r8, msa_composite_over_8888_n_8888),
    PIXMAN_STD_FAST_PATH (OVER, a8b8g8r8, solid, x8b8g8r8, msa_composite_over_8888_n_8888),
    PIXMAN_STD_FAST_PATH_CA (OVER, solid, a8r8g8b8, a8r8g8b8, msa_composite_over_n_8888_8888_ca),
    PIXMAN_STD_FAST_PATH_CA (OVER, solid, a8r8g8b8, x8r8g8b8, msa_composite_over_n_8888_8888_ca),
    PIXMAN_STD_FAST_PATH_CA (OVER, solid, a8b8g8r8, a8b8g8r8, msa_composite_over_n_8888_8888_ca),
    PIXMAN_STD_FAST_PATH_CA (OVER, solid, a8b8g8r8, x8b8g8r8, msa_composite_over_n_8888_8888_ca),
    PIXMAN_STD_FAST_PATH_CA (OVER, solid, a8r8g8b8, r5g6b5, msa_composite_over_n_8888_0565_ca),
    PIXMAN_STD_FAST_PATH_CA (OVER, solid, a8b8g8r8, b5g6r5, msa_composite_over_n_8888_0565_ca),
    PIXMAN_STD_FAST_PATH (OVER, pixbuf, pixbuf, a8r8g8b8, msa_composite_over_pixbuf_8888),
    PIXMAN_STD_FAST_PATH (OVER, pixbuf, pixbuf, x8r8g8b8, msa_composite_over_pixbuf_8888),
    PIXMAN_STD_FAST_PATH (OVER, rpixbuf, rpixbuf, a8b8g8r8, msa_composite_over_pixbuf_8888),
    PIXMAN_STD_FAST_PATH (OVER, rpixbuf, rpixbuf, x8b8g8r8, msa_composite_over_pixbuf_8888),
    PIXMAN_STD_FAST_PATH (OVER, pixbuf, pixbuf, r5g6b5, msa_composite_over_pixbuf_0565),
    PIXMAN_STD_FAST_PATH (OVER, rpixbuf, rpixbuf, b5g6r5, msa_composite_over_pixbuf_0565),
    PIXMAN_STD_FAST_PATH (OVER, x8r8g8b8, null, x8r8g8b8, msa_composite_copy_area),
    PIXMAN_STD_FAST_PATH (OVER, x8b8g8r8, null, x8b8g8r8, msa_composite_copy_area),
    
    /* PIXMAN_OP_OVER_REVERSE */
    PIXMAN_STD_FAST_PATH (OVER_REVERSE, solid, null, a8r8g8b8, msa_composite_over_reverse_n_8888),
    PIXMAN_STD_FAST_PATH (OVER_REVERSE, solid, null, a8b8g8r8, msa_composite_over_reverse_n_8888),

    /* PIXMAN_OP_ADD */
    PIXMAN_STD_FAST_PATH_CA (ADD, solid, a8r8g8b8, a8r8g8b8, msa_composite_add_n_8888_8888_ca),
    PIXMAN_STD_FAST_PATH (ADD, a8, null, a8, msa_composite_add_8_8),
    PIXMAN_STD_FAST_PATH (ADD, a8r8g8b8, null, a8r8g8b8, msa_composite_add_8888_8888),
    PIXMAN_STD_FAST_PATH (ADD, a8b8g8r8, null, a8b8g8r8, msa_composite_add_8888_8888),
    PIXMAN_STD_FAST_PATH (ADD, solid, a8, a8, msa_composite_add_n_8_8),
    PIXMAN_STD_FAST_PATH (ADD, solid, null, a8, msa_composite_add_n_8),
    PIXMAN_STD_FAST_PATH (ADD, solid, null, x8r8g8b8, msa_composite_add_n_8888),
    PIXMAN_STD_FAST_PATH (ADD, solid, null, a8r8g8b8, msa_composite_add_n_8888),
    PIXMAN_STD_FAST_PATH (ADD, solid, null, x8b8g8r8, msa_composite_add_n_8888),
    PIXMAN_STD_FAST_PATH (ADD, solid, null, a8b8g8r8, msa_composite_add_n_8888),
    PIXMAN_STD_FAST_PATH (ADD, solid, a8, x8r8g8b8, msa_composite_add_n_8_8888),
    PIXMAN_STD_FAST_PATH (ADD, solid, a8, a8r8g8b8, msa_composite_add_n_8_8888),
    PIXMAN_STD_FAST_PATH (ADD, solid, a8, x8b8g8r8, msa_composite_add_n_8_8888),
    PIXMAN_STD_FAST_PATH (ADD, solid, a8, a8b8g8r8, msa_composite_add_n_8_8888),

    /* PIXMAN_OP_SRC */
    PIXMAN_STD_FAST_PATH (SRC, solid, a8, a8r8g8b8, msa_composite_src_n_8_8888),
    PIXMAN_STD_FAST_PATH (SRC, solid, a8, x8r8g8b8, msa_composite_src_n_8_8888),
    PIXMAN_STD_FAST_PATH (SRC, solid, a8, a8b8g8r8, msa_composite_src_n_8_8888),
    PIXMAN_STD_FAST_PATH (SRC, solid, a8, x8b8g8r8, msa_composite_src_n_8_8888),
    PIXMAN_STD_FAST_PATH (SRC, a8r8g8b8, null, r5g6b5, msa_composite_src_x888_0565),
    PIXMAN_STD_FAST_PATH (SRC, a8b8g8r8, null, b5g6r5, msa_composite_src_x888_0565),
    PIXMAN_STD_FAST_PATH (SRC, x8r8g8b8, null, r5g6b5, msa_composite_src_x888_0565),
    PIXMAN_STD_FAST_PATH (SRC, x8b8g8r8, null, b5g6r5, msa_composite_src_x888_0565),
    PIXMAN_STD_FAST_PATH (SRC, x8r8g8b8, null, a8r8g8b8, msa_composite_src_x888_8888),
    PIXMAN_STD_FAST_PATH (SRC, x8b8g8r8, null, a8b8g8r8, msa_composite_src_x888_8888),
    PIXMAN_STD_FAST_PATH (SRC, a8r8g8b8, null, a8r8g8b8, msa_composite_copy_area),
    PIXMAN_STD_FAST_PATH (SRC, a8b8g8r8, null, a8b8g8r8, msa_composite_copy_area),
    PIXMAN_STD_FAST_PATH (SRC, a8r8g8b8, null, x8r8g8b8, msa_composite_copy_area),
    PIXMAN_STD_FAST_PATH (SRC, a8b8g8r8, null, x8b8g8r8, msa_composite_copy_area),
    PIXMAN_STD_FAST_PATH (SRC, x8r8g8b8, null, x8r8g8b8, msa_composite_copy_area),
    PIXMAN_STD_FAST_PATH (SRC, x8b8g8r8, null, x8b8g8r8, msa_composite_copy_area),
    PIXMAN_STD_FAST_PATH (SRC, r5g6b5, null, r5g6b5, msa_composite_copy_area),
    PIXMAN_STD_FAST_PATH (SRC, b5g6r5, null, b5g6r5, msa_composite_copy_area),

    /* PIXMAN_OP_IN */
    PIXMAN_STD_FAST_PATH (IN, a8, null, a8, msa_composite_in_8_8),
    PIXMAN_STD_FAST_PATH (IN, solid, a8, a8, msa_composite_in_n_8_8),
    PIXMAN_STD_FAST_PATH (IN, solid, null, a8, msa_composite_in_n_8),

    SIMPLE_NEAREST_FAST_PATH (OVER, a8r8g8b8, x8r8g8b8, msa_8888_8888),
    SIMPLE_NEAREST_FAST_PATH (OVER, a8b8g8r8, x8b8g8r8, msa_8888_8888),
    SIMPLE_NEAREST_FAST_PATH (OVER, a8r8g8b8, a8r8g8b8, msa_8888_8888),
    SIMPLE_NEAREST_FAST_PATH (OVER, a8b8g8r8, a8b8g8r8, msa_8888_8888),

    SIMPLE_NEAREST_SOLID_MASK_FAST_PATH (OVER, a8r8g8b8, a8r8g8b8, msa_8888_n_8888),
    SIMPLE_NEAREST_SOLID_MASK_FAST_PATH (OVER, a8b8g8r8, a8b8g8r8, msa_8888_n_8888),
    SIMPLE_NEAREST_SOLID_MASK_FAST_PATH (OVER, a8r8g8b8, x8r8g8b8, msa_8888_n_8888),
    SIMPLE_NEAREST_SOLID_MASK_FAST_PATH (OVER, a8b8g8r8, x8b8g8r8, msa_8888_n_8888),

    SIMPLE_BILINEAR_FAST_PATH (SRC, a8r8g8b8, a8r8g8b8, msa_8888_8888),
    SIMPLE_BILINEAR_FAST_PATH (SRC, a8r8g8b8, x8r8g8b8, msa_8888_8888),
    SIMPLE_BILINEAR_FAST_PATH (SRC, x8r8g8b8, x8r8g8b8, msa_8888_8888),
    SIMPLE_BILINEAR_FAST_PATH (SRC, a8b8g8r8, a8b8g8r8, msa_8888_8888),
    SIMPLE_BILINEAR_FAST_PATH (SRC, a8b8g8r8, x8b8g8r8, msa_8888_8888),
    SIMPLE_BILINEAR_FAST_PATH (SRC, x8b8g8r8, x8b8g8r8, msa_8888_8888),

    SIMPLE_BILINEAR_FAST_PATH_COVER  (SRC, x8r8g8b8, a8r8g8b8, msa_x888_8888),
    SIMPLE_BILINEAR_FAST_PATH_COVER  (SRC, x8b8g8r8, a8b8g8r8, msa_x888_8888),
    SIMPLE_BILINEAR_FAST_PATH_PAD    (SRC, x8r8g8b8, a8r8g8b8, msa_x888_8888),
    SIMPLE_BILINEAR_FAST_PATH_PAD    (SRC, x8b8g8r8, a8b8g8r8, msa_x888_8888),
    SIMPLE_BILINEAR_FAST_PATH_NORMAL (SRC, x8r8g8b8, a8r8g8b8, msa_x888_8888),
    SIMPLE_BILINEAR_FAST_PATH_NORMAL (SRC, x8b8g8r8, a8b8g8r8, msa_x888_8888),

    SIMPLE_BILINEAR_FAST_PATH (OVER, a8r8g8b8, x8r8g8b8, msa_8888_8888),
    SIMPLE_BILINEAR_FAST_PATH (OVER, a8b8g8r8, x8b8g8r8, msa_8888_8888),
    SIMPLE_BILINEAR_FAST_PATH (OVER, a8r8g8b8, a8r8g8b8, msa_8888_8888),
    SIMPLE_BILINEAR_FAST_PATH (OVER, a8b8g8r8, a8b8g8r8, msa_8888_8888),

    SIMPLE_BILINEAR_SOLID_MASK_FAST_PATH (OVER, a8r8g8b8, x8r8g8b8, msa_8888_n_8888),
    SIMPLE_BILINEAR_SOLID_MASK_FAST_PATH (OVER, a8b8g8r8, x8b8g8r8, msa_8888_n_8888),
    SIMPLE_BILINEAR_SOLID_MASK_FAST_PATH (OVER, a8r8g8b8, a8r8g8b8, msa_8888_n_8888),
    SIMPLE_BILINEAR_SOLID_MASK_FAST_PATH (OVER, a8b8g8r8, a8b8g8r8, msa_8888_n_8888),

    SIMPLE_BILINEAR_A8_MASK_FAST_PATH (OVER, a8r8g8b8, x8r8g8b8, msa_8888_8_8888),
    SIMPLE_BILINEAR_A8_MASK_FAST_PATH (OVER, a8b8g8r8, x8b8g8r8, msa_8888_8_8888),
    SIMPLE_BILINEAR_A8_MASK_FAST_PATH (OVER, a8r8g8b8, a8r8g8b8, msa_8888_8_8888),
    SIMPLE_BILINEAR_A8_MASK_FAST_PATH (OVER, a8b8g8r8, a8b8g8r8, msa_8888_8_8888),

    { PIXMAN_OP_NONE },
};

static uint32_t *
msa_fetch_x8r8g8b8 (pixman_iter_t *iter, const uint32_t *mask)
{
    int w = iter->width;
    v8i16 ff000000 = (v8i16)mask_ff000000;
    uint32_t *dst = iter->buffer;
    uint32_t *src = (uint32_t *)iter->bits;

    iter->bits += iter->stride;

    while (w && ((uintptr_t)dst) & 0x0f)
    {
	*dst++ = (*src++) | 0xff000000;
	w--;
    }

    while (w >= 4)
    {
	save_128_aligned (
	    (int32_t *)dst, (v4i32)__msa_or_v (
		(v16u8)load_128_unaligned ((uint32_t *)src), (v16u8)ff000000));

	dst += 4;
	src += 4;
	w -= 4;
    }

    while (w)
    {
	*dst++ = (*src++) | 0xff000000;
	w--;
    }

    return iter->buffer;
}

static uint32_t *
msa_fetch_r5g6b5 (pixman_iter_t *iter, const uint32_t *mask)
{
    int w = iter->width;
    uint32_t *dst = iter->buffer;
    uint16_t *src = (uint16_t *)iter->bits;
    v8i16 ff000000 = (v8i16)mask_ff000000;

    iter->bits += iter->stride;

    while (w && ((uintptr_t)dst) & 0x0f)
    {
	uint16_t s = *src++;

	*dst++ = convert_0565_to_8888 (s);
	w--;
    }

    while (w >= 8)
    {
	v8i16 lo, hi, s;

	s = __msa_ld_h (src, 0);

	lo = (v8i16)unpack_565_to_8888 ((v4i32)__msa_ilvr_h (__msa_fill_h (0), s));
	hi = (v8i16)unpack_565_to_8888 ((v4i32)__msa_ilvl_h (__msa_fill_h (0), s));

	save_128_aligned ((int32_t *)(dst + 0), (v4i32)__msa_or_v ((v16u8)lo, (v16u8)ff000000));
	save_128_aligned ((int32_t *)(dst + 4), (v4i32)__msa_or_v ((v16u8)hi, (v16u8)ff000000));

	dst += 8;
	src += 8;
	w -= 8;
    }

    while (w)
    {
	uint16_t s = *src++;

	*dst++ = convert_0565_to_8888 (s);
	w--;
    }

    return iter->buffer;
}

static uint32_t *
msa_fetch_a8 (pixman_iter_t *iter, const uint32_t *mask)
{
    int w = iter->width;
    uint32_t *dst = iter->buffer;
    uint8_t *src = iter->bits;
    v16i8 w0, w1, w2;
    v8i16 w3, w4, w5, w6;

    iter->bits += iter->stride;

    while (w && (((uintptr_t)dst) & 15))
    {
        *dst++ = (uint32_t)(*(src++)) << 24;
        w--;
    }

    while (w >= 16)
    {
	w0 = __msa_ld_b(src, 0);

	w1 = __msa_ilvr_b (w0, __msa_fill_b (0));
	w2 = __msa_ilvl_b (w0, __msa_fill_b (0));
	w3 = __msa_ilvr_h ((v8i16)w1, __msa_fill_h (0));
	w4 = __msa_ilvl_h ((v8i16)w1, __msa_fill_h (0));
	w5 = __msa_ilvr_h ((v8i16)w2, __msa_fill_h (0));
	w6 = __msa_ilvl_h ((v8i16)w2, __msa_fill_h (0));

	__msa_st_w((v4i32)w3, (dst +  0), 0);
	__msa_st_w((v4i32)w4, (dst +  4), 0);
	__msa_st_w((v4i32)w5, (dst +  8), 0);
	__msa_st_w((v4i32)w6, (dst + 12), 0);

	dst += 16;
	src += 16;
	w -= 16;
    }

    while (w)
    {
	*dst++ = (uint32_t)(*(src++)) << 24;
	w--;
    }

    return iter->buffer;
}

#define IMAGE_FLAGS							\
    (FAST_PATH_STANDARD_FLAGS | FAST_PATH_ID_TRANSFORM |		\
     FAST_PATH_BITS_IMAGE | FAST_PATH_SAMPLES_COVER_CLIP_NEAREST)

static const pixman_iter_info_t msa_iters[] = 
{
    { PIXMAN_x8r8g8b8, IMAGE_FLAGS, ITER_NARROW,
      _pixman_iter_init_bits_stride, msa_fetch_x8r8g8b8, NULL
    },
    { PIXMAN_r5g6b5, IMAGE_FLAGS, ITER_NARROW,
      _pixman_iter_init_bits_stride, msa_fetch_r5g6b5, NULL
    },
    { PIXMAN_a8, IMAGE_FLAGS, ITER_NARROW,
      _pixman_iter_init_bits_stride, msa_fetch_a8, NULL
    },
    { PIXMAN_null },
};

pixman_implementation_t *
_pixman_implementation_create_mips_msa (pixman_implementation_t *fallback)
{
    pixman_implementation_t *imp = _pixman_implementation_create (fallback, msa_fast_paths);

    /* MSA constants */
    mask_565_r  = create_mask_2x32_128 (0x00f80000, 0x00f80000);
    mask_565_g1 = create_mask_2x32_128 (0x00070000, 0x00070000);
    mask_565_g2 = create_mask_2x32_128 (0x000000e0, 0x000000e0);
    mask_565_b  = create_mask_2x32_128 (0x0000001f, 0x0000001f);
    mask_red   = create_mask_2x32_128 (0x00f80000, 0x00f80000);
    mask_green = create_mask_2x32_128 (0x0000fc00, 0x0000fc00);
    mask_blue  = create_mask_2x32_128 (0x000000f8, 0x000000f8);
    mask_565_fix_rb = create_mask_2x32_128 (0x00e000e0, 0x00e000e0);
    mask_565_fix_g = create_mask_2x32_128  (0x0000c000, 0x0000c000);
    mask_0080 = create_mask_16_128 (0x0080);
    mask_00ff = create_mask_16_128 (0x00ff);
    mask_0101 = create_mask_16_128 (0x0101);
    mask_ffff = create_mask_16_128 (0xffff);
    mask_ff000000 = create_mask_2x32_128 (0xff000000, 0xff000000);
    mask_alpha = create_mask_2x32_128 (0x00ff0000, 0x00000000);
    mask_565_rb = create_mask_2x32_128 (0x00f800f8, 0x00f800f8);
    mask_565_pack_multiplier = create_mask_2x32_128 (0x20000004, 0x20000004);

    /* Set up function pointers */
    imp->combine_32[PIXMAN_OP_OVER] = msa_combine_over_u;
    imp->combine_32[PIXMAN_OP_OVER_REVERSE] = msa_combine_over_reverse_u;
    imp->combine_32[PIXMAN_OP_IN] = msa_combine_in_u;
    imp->combine_32[PIXMAN_OP_IN_REVERSE] = msa_combine_in_reverse_u;
    imp->combine_32[PIXMAN_OP_OUT] = msa_combine_out_u;
    imp->combine_32[PIXMAN_OP_OUT_REVERSE] = msa_combine_out_reverse_u;
    imp->combine_32[PIXMAN_OP_ATOP] = msa_combine_atop_u;
    imp->combine_32[PIXMAN_OP_ATOP_REVERSE] = msa_combine_atop_reverse_u;
    imp->combine_32[PIXMAN_OP_XOR] = msa_combine_xor_u;
    imp->combine_32[PIXMAN_OP_ADD] = msa_combine_add_u;

    imp->combine_32[PIXMAN_OP_SATURATE] = msa_combine_saturate_u;

    imp->combine_32_ca[PIXMAN_OP_SRC] = msa_combine_src_ca;
    imp->combine_32_ca[PIXMAN_OP_OVER] = msa_combine_over_ca;
    imp->combine_32_ca[PIXMAN_OP_OVER_REVERSE] = msa_combine_over_reverse_ca;
    imp->combine_32_ca[PIXMAN_OP_IN] = msa_combine_in_ca;
    imp->combine_32_ca[PIXMAN_OP_IN_REVERSE] = msa_combine_in_reverse_ca;
    imp->combine_32_ca[PIXMAN_OP_OUT] = msa_combine_out_ca;
    imp->combine_32_ca[PIXMAN_OP_OUT_REVERSE] = msa_combine_out_reverse_ca;
    imp->combine_32_ca[PIXMAN_OP_ATOP] = msa_combine_atop_ca;
    imp->combine_32_ca[PIXMAN_OP_ATOP_REVERSE] = msa_combine_atop_reverse_ca;
    imp->combine_32_ca[PIXMAN_OP_XOR] = msa_combine_xor_ca;
    imp->combine_32_ca[PIXMAN_OP_ADD] = msa_combine_add_ca;

    imp->blt = msa_blt;
    imp->fill = msa_fill;

    imp->iter_info = msa_iters;

    return imp;
}