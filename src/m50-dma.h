#ifndef M50_DMA_H
#define M50_DMA_H

#include <stdint.h>

/* Versioned, optional core symbol; other camera modules must still load.
 * Task context only. Caller owns dst until this function returns. A hardware
 * timeout retains ownership and waits for completion rather than freeing an
 * address an unconfirmed transfer could still write to. */
struct m50_dma_request
{
    void *dst;
    const void *src;
    uint32_t src_pitch, dst_pitch, width, height;
    uint32_t source_time_us, max_age_us; /* 0 max_age: private-memory test */
    uint32_t dst_policy; /* enum m50_dma_dst_policy; caller maintains ownership */
    uint32_t lock_us, transfer_us;
    uint32_t wake_us, init_us, config_us;
    uint32_t cache_before_us, cache_after_us, age_at_start_us, age_at_end_us;
};

/* Prepared destinations must first pass m50_raw_dma_prepare_v3, then remain
 * exclusively owned. UNCACHED prohibits cached accesses; READONLY permits
 * cached reads after copy returns, but no cached writes until re-prepared.
 * Caller owns every boundary cache line touched by preparation/maintenance. */
enum m50_dma_dst_policy
{
    M50_DMA_DST_MAINTAINED = 0,
    M50_DMA_DST_UNCACHED = 1,
    M50_DMA_DST_READONLY = 2
};

enum m50_dma_result
{
    M50_DMA_OK = 1,
    M50_DMA_INVALID = -1,
    M50_DMA_STALE = -2,
    M50_DMA_TIMEOUT = -3,
    M50_DMA_BUSY = -4
};

typedef int (*m50_dma_copy_fn)(struct m50_dma_request *request);
typedef int (*m50_dma_prepare_fn)(void *destination, uint32_t bytes);
#endif
