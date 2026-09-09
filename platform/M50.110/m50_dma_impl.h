/* M50 1.1.0 only. Included by function_overrides.c; host tests mock the ROM.
 * Verified Layer2 clients acquire {0x5001f,0x50023}, protecting both the pair
 * and its shared callback state at 0x16f38. Do not call from the EVF ISR. */
#include "m50-dma.h"
#include "timer.h"
#include "mmu_utils.h"

extern void m50_outer_invalidate(uint32_t start, uint32_t bytes);
extern void m50_dcache_invalidate(uint32_t start, uint32_t bytes);
extern void m2m_InitMem2MemModule(void *pair);
extern void m2m_store_struct(void (*callback)(void *), uint32_t ctx);
extern void m2m_reinit_struct(void);
extern void m2m_cleanup_channels(void);
extern void m2m_set_addrs(uint32_t *addresses);
extern void m2m_set_geom_mode(uint32_t *geometry);
extern void m2m_start_connect(void);
extern uint32_t m2m_resource_lock(void *ids, uint32_t count);
extern void m2m_resource_unlock(uint32_t entry);
extern void m2m_pwr_wake(void);
extern void m2m_pwr_sleep(void);
extern void eld_edmac_set_enable(uint32_t channel);
extern void eld_edmac_irq_enable(uint32_t channel);

#ifndef M50_DMA_BARRIER
#define M50_DMA_BARRIER() asm volatile ("dsb sy" ::: "memory")
#endif
#ifndef M50_DMA_ISR_BITS
#define M50_DMA_ISR_BITS (*(volatile uint32_t *)0x16f48)
#endif

static volatile uint32_t m50_dma_done;
static volatile uint32_t m50_dma_owned;

static void m50_dma_complete(void *ctx)
{
    (void)ctx;
    /* Canon invokes this only after BOTH channel IRQs. It clears its bits
     * after we return. No cleanup, task API or next transfer in this callback. */
    M50_DMA_BARRIER();
    m50_dma_done = 1;
}

static int m50_dma_stale(const struct m50_dma_request *r)
{
    return r->max_age_us &&
        (uint32_t)((uint32_t)get_us_clock() - r->source_time_us) >= r->max_age_us;
}

/* Preparation is outside the capture window. Chunking bounds each cache
 * sweep and periodically lets Canon tasks run; no active DMA may share it. */
int m50_raw_dma_prepare_v3(void *destination, uint32_t bytes)
{
    uintptr_t dst = (uintptr_t)CACHEABLE(destination);
    if (!destination || !bytes || dst < 0x02000000 ||
        (uint64_t)dst + bytes > 0x40000000ULL)
        return M50_DMA_INVALID;
    uint32_t irq = cli();
    if (m50_dma_owned) { sei(irq); return M50_DMA_BUSY; }
    m50_dma_owned = 1;
    sei(irq);
    uint32_t processed = 0;
    while (processed < bytes)
    {
        uint32_t size = bytes - processed;
        if (size > 256 * 1024) size = 256 * 1024;
        dcache_clean(dst + processed, size);
        dcache_clean_multicore(dst + processed, size);
        m50_dcache_invalidate(dst + processed, size);
        processed += size;
        if (processed < bytes && !(processed % (1024 * 1024))) msleep(1);
    }
    M50_DMA_BARRIER();
    m50_dma_owned = 0;
    return M50_DMA_OK;
}

int m50_raw_dma_copy_v3(struct m50_dma_request *r)
{
    if (!r) return M50_DMA_INVALID;
    r->lock_us = r->transfer_us = 0;
    r->wake_us = r->init_us = r->config_us = 0;
    r->cache_before_us = r->cache_after_us = 0;
    r->age_at_start_us = r->age_at_end_us = 0;
    if (r->dst_policy > M50_DMA_DST_READONLY ||
        (r->dst_policy == M50_DMA_DST_UNCACHED &&
         !((uintptr_t)r->dst & 0x40000000)))
        return M50_DMA_INVALID;
    if (!r->src || !r->dst || !r->width || !r->height ||
        r->width > 65535 || r->height > 65536 ||
        r->src_pitch < r->width || r->dst_pitch < r->width ||
        ((uintptr_t)r->src | (uintptr_t)r->dst | r->width |
         r->src_pitch | r->dst_pitch) & 1)
        return M50_DMA_INVALID;
    uint64_t src_size = (uint64_t)(r->height - 1) * r->src_pitch + r->width;
    uint64_t dst_size = (uint64_t)(r->height - 1) * r->dst_pitch + r->width;
    uintptr_t src = (uintptr_t)CACHEABLE(r->src);
    uintptr_t dst = (uintptr_t)CACHEABLE(r->dst);
    /* Only ordinary, externally shared RAM; exclude CPU-local low memory,
     * MMIO, wraparound and overlapping transfers. */
    if (src < 0x02000000 || dst < 0x02000000 ||
        src + src_size > 0x40000000ULL || dst + dst_size > 0x40000000ULL ||
        (src < dst + dst_size && dst < src + src_size))
        return M50_DMA_INVALID;

    uint32_t irq = cli();
    if (m50_dma_owned) { sei(irq); return M50_DMA_BUSY; }
    m50_dma_owned = 1;
    sei(irq);

    uint32_t before_lock = (uint32_t)get_us_clock();
    uint32_t entry = m2m_resource_lock((void *)0xe0f72638, 2);
    r->lock_us = (uint32_t)get_us_clock() - before_lock;
    int result = M50_DMA_STALE;
    if (m50_dma_stale(r)) goto unlock;

    /* Never clean the live sensor source: dirty cache lines could overwrite
     * newer sensor DMA data. Private test input is explicitly cleaned. */
    uint32_t cache_before = (uint32_t)get_us_clock();
    if (!r->max_age_us)
    {
        dcache_clean(src, src_size);
        dcache_clean_multicore(src, src_size);
    }
    if (r->dst_policy == M50_DMA_DST_MAINTAINED)
    {
        dcache_clean(dst, dst_size);
        dcache_clean_multicore(dst, dst_size);
        m50_dcache_invalidate(dst, dst_size);
    }
    r->cache_before_us = (uint32_t)get_us_clock() - cache_before;
    if (m50_dma_stale(r)) goto unlock;

    struct edmac_info sg = {0}, dg = {0};
    sg.xb = dg.xb = r->width;
    sg.yb = dg.yb = r->height - 1;
    sg.off1b = r->src_pitch - r->width;
    dg.off1b = r->dst_pitch - r->width;
    uint32_t addresses[] = { (uint32_t)src, (uint32_t)dst };
    uint32_t geometry[] = { (uint32_t)&sg, (uint32_t)&dg, 1 };

    uint32_t wake_start = (uint32_t)get_us_clock();
    m2m_pwr_wake();
    uint32_t init_start = (uint32_t)get_us_clock();
    r->wake_us = init_start - wake_start;
    m2m_InitMem2MemModule((void *)0xe0f72640);
    uint32_t config_start = (uint32_t)get_us_clock();
    r->init_us = config_start - init_start;
    m50_dma_done = 0;
    m2m_store_struct(m50_dma_complete, 0);
    m2m_set_addrs(addresses);
    m2m_set_geom_mode(geometry);
    eld_edmac_set_enable(0x18);
    eld_edmac_set_enable(0x3d);
    /* This is a Boomer VD-kick enable, not generic CPU IRQ enable.
     * Only the destination (port 0x66, VdType 1) accepts it. The source
     * (port 0xf2, VdType 2) ASSERTs in E0541712. Layer2 Init already
     * registers completion IRQ handlers for BOTH channels via E0549F76. */
    eld_edmac_irq_enable(0x18);
    M50_DMA_BARRIER();
    uint32_t started = (uint32_t)get_us_clock();
    r->config_us = started - config_start;
    r->age_at_start_us = started - r->source_time_us;
    /* Setup may block or be preempted. Revalidate immediately before the
     * trigger, even if the source was fresh when cache preparation ended.
     * Neither channel has been armed/connected yet: ordinary teardown is
     * sufficient here, without pretending to abort an active transfer. */
    if (r->max_age_us && r->age_at_start_us >= r->max_age_us)
    {
        r->age_at_end_us = r->age_at_start_us;
        result = M50_DMA_STALE;
        goto cleanup_channels;
    }
    m2m_start_connect();
    result = M50_DMA_OK;
    /* A real clock deadline replaces the old instruction-count loop. Poll
     * briefly with interrupts enabled; yield on slower transfers. A timeout
     * is NOT evidence the engine has stopped. Retain all ownership until the
     * callback and the remainder of Canon's ISR have finished. */
    while (!m50_dma_done || M50_DMA_ISR_BITS)
    {
        uint32_t elapsed = (uint32_t)get_us_clock() - started;
        if (elapsed >= 250000 && result != M50_DMA_TIMEOUT)
        {
            result = M50_DMA_TIMEOUT;
            NotifyBox(10000, "DMA stalled. Buffers held; restart camera.");
        }
        if (elapsed >= 2000) msleep(1);
    }
    M50_DMA_BARRIER();
    uint32_t completed = (uint32_t)get_us_clock();
    r->transfer_us = completed - started;
    r->age_at_end_us = completed - r->source_time_us;
    if (result == M50_DMA_OK && r->max_age_us &&
        r->age_at_end_us >= r->max_age_us) result = M50_DMA_STALE;
    /* Coherence deadline ends when the snapshot is immutable, not after
     * subsequent cache invalidation or CPU packing. */
    uint32_t cache_after = (uint32_t)get_us_clock();
    if (r->dst_policy != M50_DMA_DST_UNCACHED)
    {
        m50_outer_invalidate(dst, dst_size);
        m50_dcache_invalidate(dst, dst_size);
    }
    r->cache_after_us = (uint32_t)get_us_clock() - cache_after;
cleanup_channels:
    m2m_reinit_struct();
    m2m_cleanup_channels();
    m2m_pwr_sleep();
unlock:
    m2m_resource_unlock(entry);
    M50_DMA_BARRIER();
    m50_dma_owned = 0;
    return result;
}
