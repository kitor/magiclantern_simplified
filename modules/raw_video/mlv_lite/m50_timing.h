/* Saved-frame timing for M50's throughput-limited CPU capture path.
 * Conforming the header to the saved cadence preserves overall duration in
 * constant-rate MLV players; it cannot reconstruct missing frames. */
#ifndef M50_TIMING_H
#define M50_TIMING_H

struct m50_saved_timing
{
    uint64_t first;
    uint64_t last;
    uint32_t count;
    int invalid;
};

static void m50_timing_add(struct m50_saved_timing *t, uint64_t timestamp)
{
    if (!t->count) t->first = timestamp;
    else if (timestamp <= t->last) t->invalid = 1;
    t->last = timestamp;
    t->count++;
}

static uint32_t m50_timing_fps_x1000(const struct m50_saved_timing *t)
{
    if (t->invalid || t->count < 2 || t->last <= t->first) return 0;
    uint64_t duration = t->last - t->first;
    uint64_t rate = ((uint64_t)(t->count - 1) * 1000000000ULL + duration / 2) / duration;
    return rate > 0 && rate <= 1000000 ? rate : 0;
}
#endif
