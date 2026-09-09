/* RAM-only timing aggregation. Each metric has a single writer. */
#ifndef M50_PROFILE_H
#define M50_PROFILE_H
#define M50_PROFILE_BINS 17
/* Canon's formatter on M50 prints literal "u" for %u. Serialize unsigned
 * CSV fields directly, including values above INT_MAX, without printf. */
static unsigned m50_profile_csv_row(char *out, unsigned capacity,
    const char *name, const uint32_t *values, unsigned count)
{
    unsigned pos = 0;
    while (*name)
    {
        if (pos + 1 >= capacity) return 0;
        out[pos++] = *name++;
    }
    for (unsigned i = 0; i < count; i++)
    {
        char digits[10];
        unsigned n = 0;
        uint32_t value = values[i];
        do { digits[n++] = '0' + value % 10; value /= 10; } while (value);
        if (pos + 1 + n >= capacity) return 0;
        out[pos++] = ',';
        while (n) out[pos++] = digits[--n];
    }
    if (pos + 1 >= capacity) return 0;
    out[pos++] = '\n';
    out[pos] = 0;
    return pos;
}

struct m50_duration
{
    uint32_t count, min_us, max_us;
    uint64_t total_us;
    /* 0..15: [n*5000, (n+1)*5000) us; 16: >=80000 us. */
    uint32_t bins[M50_PROFILE_BINS];
};

static void m50_duration_add(struct m50_duration *d, uint32_t us)
{
    if (!d->count || us < d->min_us) d->min_us = us;
    if (us > d->max_us) d->max_us = us;
    d->count++;
    d->total_us += us;
    unsigned bin = us / 5000;
    if (bin >= M50_PROFILE_BINS) bin = M50_PROFILE_BINS - 1;
    d->bins[bin]++;
}

static uint32_t m50_duration_mean(const struct m50_duration *d)
{
    return d->count ? d->total_us / d->count : 0;
}

struct m50_profile
{
    struct m50_duration hook, dispatch, copy, write;
    struct m50_duration dma_lock, dma_transfer, packing;
    struct m50_duration dma_cache_before, dma_cache_after, dma_age_start, dma_age_end;
    uint32_t dma_enabled, dma_errors, dma_dropped;
    struct m50_duration dma_wake, dma_init, dma_config;
    uint32_t last_hook, hooks, busy_skips, no_slot_skips, post_failures;
    uint32_t write_errors;
    int width, height, bits, canon_fps, canon_resolution, canon_crop, zoom;
    uint64_t write_bytes;
};
#endif
