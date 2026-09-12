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
extern void eld_edmac_reset(uint32_t channel);
extern void eld_edmac_irq_enable(uint32_t channel);
extern void m50_edmac_pack_config(uint32_t channel, const uint32_t *cfg);
extern void m50_edmac_unpack_config(uint32_t channel, const uint32_t *cfg);
extern void m50_edmac_pack_xmode(uint32_t channel, uint32_t mode);

#ifndef M50_DMA_MMIO
#define M50_DMA_MMIO(address) (*(volatile uint32_t *)(uintptr_t)(address))
#endif

/* Only the configuration registers touched by the private packing experiments.
 * +30 is unclassified: accept observed 0/1, but never write a guessed 1.
 * Recreate 1 via Canon reset and require readback, replay only known zero.
 * +28 is observed but never written by this experiment. */
static const uint32_t m50_identity_registers[] = {
    0xd0487d20, 0xd0487d24, 0xd0487d30, 0xd04876d0,
    0xd0487920, 0xd0487924, 0xd0487930, 0xd04871d0
};

#ifndef M50_DMA_BARRIER
#define M50_DMA_BARRIER() asm volatile ("dsb sy" ::: "memory")
#endif
#ifndef M50_DMA_ISR_BITS
#define M50_DMA_ISR_BITS (*(volatile uint32_t *)0x16f48)
#endif

/* Last private identity or conversion attempt only; ordinary recording never updates this.
 * words: stage, failure mask, then before/configured/restored snapshots.
 * Each snapshot: src20/24/30/D0, dst20/24/30/D0, src28, dst28. */
static uint32_t m50_identity_report[32];
static void m50_identity_snapshot(uint32_t *report, unsigned offset)
{
    for (unsigned i = 0; i < 8; i++)
        report[offset + i] = M50_DMA_MMIO(m50_identity_registers[i]);
    report[offset + 8] = M50_DMA_MMIO(0xd0487d28);
    report[offset + 9] = M50_DMA_MMIO(0xd0487928);
}

int m50_raw_dma_identity_report_v1(uint32_t *report, uint32_t words)
{
    if (!report || words != 32) return M50_DMA_INVALID;
    for (unsigned i = 0; i < 32; i++) report[i] = m50_identity_report[i];
    return M50_DMA_OK;
}

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

static int m50_dma_copy_internal(struct m50_dma_request *r, uint32_t output_bits, int live_pack)
{
    if (!r) return M50_DMA_INVALID;
    uint32_t live_report[32] = {0};
    uint32_t *report = live_pack ? live_report : m50_identity_report;
    if (live_pack && ((output_bits != 10 && output_bits != 12) ||
        !r->max_age_us || r->dst_policy != M50_DMA_DST_UNCACHED))
        return M50_DMA_INVALID;
    const int packing_enabled = output_bits != 0;
    if (output_bits && output_bits != 10 && output_bits != 12 && output_bits != 14)
        return M50_DMA_INVALID;
    if (packing_enabled && r->width % 14) return M50_DMA_INVALID;
    const uint32_t dst_width = packing_enabled ? r->width / 14 * output_bits : r->width;
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
        r->src_pitch < r->width || r->dst_pitch < dst_width ||
        ((uintptr_t)r->src | (uintptr_t)r->dst | r->width |
         r->src_pitch | r->dst_pitch) & 1)
        return M50_DMA_INVALID;
    uint64_t src_size = (uint64_t)(r->height - 1) * r->src_pitch + r->width;
    uint64_t dst_size = (uint64_t)(r->height - 1) * r->dst_pitch + dst_width;
    uintptr_t src = (uintptr_t)CACHEABLE(r->src);
    uintptr_t dst = (uintptr_t)CACHEABLE(r->dst);
    /* Only ordinary, externally shared RAM; exclude CPU-local low memory,
     * MMIO, wraparound and overlapping transfers. */
    if (src < 0x02000000 || dst < 0x02000000 ||
        src + src_size > 0x40000000ULL || dst + dst_size > 0x40000000ULL ||
        (src < dst + dst_size && dst < src + src_size))
        return M50_DMA_INVALID;

    /* Diagnostic only: private maintained buffers, eight-pixel 14-bit
     * groups, and at most 64 KiB in each independently owned span. */
    if (packing_enabled && !live_pack && (r->max_age_us || r->dst_policy != M50_DMA_DST_MAINTAINED ||
        r->width % 14 || src_size > 65536 || dst_size > 65536))
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
    sg.xb = r->width;
    dg.xb = dst_width;
    sg.yb = dg.yb = r->height - 1;
    sg.off1b = r->src_pitch - r->width;
    dg.off1b = r->dst_pitch - dst_width;
    uint32_t addresses[] = { (uint32_t)src, (uint32_t)dst };
    /* Canon E084E4C8 uses mode0 with these same packer channels/configs.
     * V8 mode1 lost the upper half of every32-bit word in the private test.
     * Keep the proven plain-copy recording path at mode1. */
    uint32_t geometry[] = { (uint32_t)&sg, (uint32_t)&dg, packing_enabled ? 0 : 1 };

    uint32_t wake_start = (uint32_t)get_us_clock();
    m2m_pwr_wake();
    uint32_t saved[8], extra_src = 0, extra_dst = 0;
    if (packing_enabled)
    {
        for (unsigned i = 0; i < 8; i++) saved[i] = M50_DMA_MMIO(m50_identity_registers[i]);
        extra_src = M50_DMA_MMIO(0xd0487d28);
        extra_dst = M50_DMA_MMIO(0xd0487928);
        report[0] = 1;
        m50_identity_snapshot(report, 2);
        if (saved[2] > 1) report[1] |= 1;
        if (saved[6] > 1) report[1] |= 2;
        if (saved[2] > 1 || saved[6] > 1)
        {
            result = M50_DMA_RESTORE;
            goto sleep;
        }
    }
    uint32_t init_start = (uint32_t)get_us_clock();
    r->wake_us = init_start - wake_start;
    m2m_InitMem2MemModule((void *)0xe0f72640);
    if (packing_enabled)
    {
        /* Test reset reproducibility BEFORE changing packing. The observed
         * camera state is +30=1; its meaning is not assumed to be bypass.
         * Zero is also restorable using Canon's demonstrated zero write. */
        M50_DMA_BARRIER();
        m50_identity_snapshot(report, 12);
        report[0] = 2;
        if (saved[2] == 1 && report[14] != 1) report[1] |= 1 << 18;
        if (saved[6] == 1 && report[18] != 1) report[1] |= 1 << 19;
        if (report[1])
        {
            result = M50_DMA_RESTORE;
            goto cleanup_channels;
        }
    }
    uint32_t config_start = (uint32_t)get_us_clock();
    r->init_us = config_start - init_start;
    m50_dma_done = 0;
    m2m_store_struct(m50_dma_complete, 0);
    m2m_set_addrs(addresses);
    m2m_set_geom_mode(geometry);
    if (packing_enabled)
    {
        /* V11 mode0/order0 passed full-ramp 14/12/10-bit pixel and
         * guard checks on camera, including restoration and plain-copy recovery. */
        const uint32_t unpack[] = {0, 2, 0, 0};
        const uint32_t pack_format = (output_bits - 10) / 2;
        const uint32_t pack[] = {0, 0, pack_format, 0, 0};
        m50_edmac_unpack_config(0x3d, unpack);
        m50_edmac_pack_xmode(0x3d, 0);
        m50_edmac_pack_config(0x18, pack);
        m50_edmac_pack_xmode(0x18, 0);
        /* Report each check separately; do not relax hardware guards. */
        M50_DMA_BARRIER();
        report[0] = 2;
        m50_identity_snapshot(report, 12);
        if ((report[13] & 0x00130131) != 0x20) report[1] |= 1 << 2;
        if ((report[17] & 0x00130131) != (pack_format << 4)) report[1] |= 1 << 3;
        if (report[12]) report[1] |= 1 << 4;
        if (report[16]) report[1] |= 1 << 5;
        if (report[15] != 3) report[1] |= 1 << 6;
        if (report[19] != 1) report[1] |= 1 << 7;
        if (report[1])
        {
            result = M50_DMA_RESTORE;
            goto cleanup_channels;
        }
    }
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
    if (packing_enabled) report[0] = 3;
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
    if (packing_enabled) report[0] = 4;
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
    if (packing_enabled)
    {
        /* DMA was never started, or both callbacks AND Canon's ISR epilogue
         * completed before here.
         * Restore while powered and locked, including exact format and route configuration. */
        /* Full reset is already used by Canon Layer2 Init. Never use this
         * to abandon an in-flight transfer: completion ownership above holds.
         * Its +30=1 effect is verified, not forced with an unproven write. */
        eld_edmac_reset(0x18);
        eld_edmac_reset(0x3d);
        M50_DMA_BARRIER();
        for (unsigned i = 0; i < 8; i++)
        {
            if ((i == 2 || i == 6) && saved[i] == 1) continue;
            M50_DMA_MMIO(m50_identity_registers[i]) = saved[i];
        }
        M50_DMA_BARRIER();
        m50_identity_snapshot(report, 22);
        for (unsigned i = 0; i < 8; i++)
            if (report[22 + i] != saved[i]) report[1] |= 1 << (8 + i);
        if (report[30] != extra_src) report[1] |= 1 << 16;
        if (report[31] != extra_dst) report[1] |= 1 << 17;
        if (report[1]) result = M50_DMA_RESTORE;
        /* Stage5 means restoration attempted; failure bits retain earlier errors. */
        report[0] = 5;
    }
sleep:
    m2m_pwr_sleep();
unlock:
    m2m_resource_unlock(entry);
    M50_DMA_BARRIER();
    m50_dma_owned = 0;
    return result;
}

/* Preserve the v3 ABI and all existing recording behavior. */
int m50_raw_dma_copy_v3(struct m50_dma_request *r)
{
    return m50_dma_copy_internal(r, 0, 0);
}

/* Identity only; successful transfer is not a pixel-accuracy verdict.
 * Caller compares every byte and guards, then exercises ordinary copy. */
int m50_raw_dma_identity_v1(struct m50_dma_request *r)
{
    for (unsigned i = 0; i < 32; i++) m50_identity_report[i] = 0;
    return m50_dma_copy_internal(r, 14, 0);
}

/* Private conversion experiment only. Width/pitch of the source describe
 * packed 14-bit input; destination width is derived from eight-pixel groups.
 * No live-frame age policy or prepared recording destination is accepted.
 * Success reports hardware completion/restoration, not pixel accuracy. */
int m50_raw_dma_convert_test_v1(struct m50_dma_request *r, uint32_t output_bits)
{
    for (unsigned i = 0; i < 32; i++) m50_identity_report[i] = 0;
    if (output_bits != 10 && output_bits != 12) return M50_DMA_INVALID;
    return m50_dma_copy_internal(r, output_bits, 0);
}

/* Live 14-bit sensor crop directly into an exclusively owned, prepared
 * uncached recording slot. The caller preserves ownership until completion
 * and must stop recording on configuration/restoration errors. Unlike private
 * diagnostics this accepts full frame spans and never publishes report state. */
int m50_raw_dma_pack_v1(struct m50_dma_request *r, uint32_t output_bits)
{
    return m50_dma_copy_internal(r, output_bits, 1);
}
