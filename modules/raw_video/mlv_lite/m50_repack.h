#ifndef M50_REPACK_H
#define M50_REPACK_H

#include <stdint.h>

/* Canon/MLV packing: MSB-first samples in little-endian 16-bit words.
 * Source and destination must not overlap; count must be a multiple of eight.
 * The aligned path requires only two-byte alignment: the 14-byte input groups
 * alternate their four-byte alignment. No reads extend beyond the input group.
 * GCC may_alias permits word access to raw byte buffers without type-punning UB.
 * Byte access remains available for deliberately unaligned buffers.
 * DMA users must establish completion/cache coherency before calling these.
 */
#if defined(__GNUC__) && defined(__BYTE_ORDER__) && \
    __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
typedef uint16_t m50_repack_word __attribute__((__may_alias__));
#define M50_REPACK_WORD_ACCESS 1
#else
#define M50_REPACK_WORD_ACCESS 0
#endif

static inline uint32_t m50_repack_load(const uint8_t *src, int aligned)
{
#if M50_REPACK_WORD_ACCESS
    if (aligned) return *(const m50_repack_word *)src;
#else
    (void)aligned;
#endif
    return src[0] | ((uint32_t)src[1] << 8);
}

static inline void m50_repack_store(uint8_t *dst, uint32_t value, int aligned)
{
#if M50_REPACK_WORD_ACCESS
    if (aligned)
    {
        *(m50_repack_word *)dst = (uint16_t)value;
        return;
    }
#else
    (void)aligned;
#endif
    dst[0] = value;
    dst[1] = value >> 8;
}

/* A constant aligned argument lets the compiler remove alignment decisions
 * from the loop entirely. Both paths use the same packing expressions. */
static inline __attribute__((always_inline)) void
m50_repack_14_to_10_loop(uint8_t *dst, const uint8_t *src,
                            int num_pixels, int aligned)
{
    for (int i = 0; i < num_pixels / 8; i++, src += 14, dst += 10)
    {
        uint32_t w0 = m50_repack_load(src + 0, aligned);
        uint32_t w1 = m50_repack_load(src + 2, aligned);
        uint32_t w2 = m50_repack_load(src + 4, aligned);
        uint32_t w3 = m50_repack_load(src + 6, aligned);
        uint32_t w4 = m50_repack_load(src + 8, aligned);
        uint32_t w5 = m50_repack_load(src + 10, aligned);
        uint32_t w6 = m50_repack_load(src + 12, aligned);
        uint32_t p0 = (w0 >> 2) >> 4;
        uint32_t p1 = (((w0 & 3) << 12) | (w1 >> 4)) >> 4;
        uint32_t p2 = (((w1 & 15) << 10) | (w2 >> 6)) >> 4;
        uint32_t p3 = (((w2 & 63) << 8) | (w3 >> 8)) >> 4;
        uint32_t p4 = (((w3 & 255) << 6) | (w4 >> 10)) >> 4;
        uint32_t p5 = (((w4 & 1023) << 4) | (w5 >> 12)) >> 4;
        uint32_t p6 = (((w5 & 4095) << 2) | (w6 >> 14)) >> 4;
        uint32_t p7 = (w6 & 16383) >> 4;
        m50_repack_store(dst + 0, (p0 << 6) | (p1 >> 4), aligned);
        m50_repack_store(dst + 2, (p1 << 12) | (p2 << 2) | (p3 >> 8), aligned);
        m50_repack_store(dst + 4, (p3 << 8) | (p4 >> 2), aligned);
        m50_repack_store(dst + 6, (p4 << 14) | (p5 << 4) | (p6 >> 6), aligned);
        m50_repack_store(dst + 8, (p6 << 10) | p7, aligned);
    }
}

static inline void m50_repack_14_to_10(uint8_t *dst, const uint8_t *src,
                                        int num_pixels)
{
    if ((((uintptr_t)src | (uintptr_t)dst) & 1) == 0)
        m50_repack_14_to_10_loop(dst, src, num_pixels, 1);
    else
        m50_repack_14_to_10_loop(dst, src, num_pixels, 0);
}

static inline __attribute__((always_inline)) void
m50_repack_14_to_12_loop(uint8_t *dst, const uint8_t *src,
                            int num_pixels, int aligned)
{
    for (int i = 0; i < num_pixels / 8; i++, src += 14, dst += 12)
    {
        uint32_t w0 = m50_repack_load(src + 0, aligned);
        uint32_t w1 = m50_repack_load(src + 2, aligned);
        uint32_t w2 = m50_repack_load(src + 4, aligned);
        uint32_t w3 = m50_repack_load(src + 6, aligned);
        uint32_t w4 = m50_repack_load(src + 8, aligned);
        uint32_t w5 = m50_repack_load(src + 10, aligned);
        uint32_t w6 = m50_repack_load(src + 12, aligned);
        uint32_t p0 = (w0 >> 2) >> 2;
        uint32_t p1 = (((w0 & 3) << 12) | (w1 >> 4)) >> 2;
        uint32_t p2 = (((w1 & 15) << 10) | (w2 >> 6)) >> 2;
        uint32_t p3 = (((w2 & 63) << 8) | (w3 >> 8)) >> 2;
        uint32_t p4 = (((w3 & 255) << 6) | (w4 >> 10)) >> 2;
        uint32_t p5 = (((w4 & 1023) << 4) | (w5 >> 12)) >> 2;
        uint32_t p6 = (((w5 & 4095) << 2) | (w6 >> 14)) >> 2;
        uint32_t p7 = (w6 & 16383) >> 2;
        m50_repack_store(dst + 0, (p0 << 4) | (p1 >> 8), aligned);
        m50_repack_store(dst + 2, (p1 << 8) | (p2 >> 4), aligned);
        m50_repack_store(dst + 4, (p2 << 12) | p3, aligned);
        m50_repack_store(dst + 6, (p4 << 4) | (p5 >> 8), aligned);
        m50_repack_store(dst + 8, (p5 << 8) | (p6 >> 4), aligned);
        m50_repack_store(dst + 10, (p6 << 12) | p7, aligned);
    }
}

static inline void m50_repack_14_to_12(uint8_t *dst, const uint8_t *src,
                                        int num_pixels)
{
    if ((((uintptr_t)src | (uintptr_t)dst) & 1) == 0)
        m50_repack_14_to_12_loop(dst, src, num_pixels, 1);
    else
        m50_repack_14_to_12_loop(dst, src, num_pixels, 0);
}

#endif /* M50_REPACK_H */
