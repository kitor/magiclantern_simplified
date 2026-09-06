#!/usr/bin/env python3
"""Exercise firmware playback-rate calculation against observed clip timing."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
code = r'''
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include "m50_timing.h"
int main(void)
{
    struct m50_saved_timing t = {0};
    assert(m50_timing_fps_x1000(&t) == 0);
    m50_timing_add(&t, 100000);
    assert(m50_timing_fps_x1000(&t) == 0);
    m50_timing_add(&t, 140000);
    assert(m50_timing_fps_x1000(&t) == 25000);
    m50_timing_add(&t, 140000);
    assert(m50_timing_fps_x1000(&t) == 0);
    t = (struct m50_saved_timing){97761, 33057762, 497, 0};
    assert(m50_timing_fps_x1000(&t) == 15049);
    t = (struct m50_saved_timing){113172, 2513172, 54, 0};
    assert(m50_timing_fps_x1000(&t) == 22083);
    t = (struct m50_saved_timing){108308, 2668303, 57, 0};
    assert(m50_timing_fps_x1000(&t) == 21875);
    /* Use 64-bit arithmetic for long recordings, and clear state at splits. */
    t = (struct m50_saved_timing){1000000, 1000000ULL + 40000ULL * 999999, 1000000, 0};
    assert(m50_timing_fps_x1000(&t) == 25000);
    memset(&t, 0, sizeof(t));
    m50_timing_add(&t, 90000000000ULL);
    m50_timing_add(&t, 90000040000ULL);
    assert(m50_timing_fps_x1000(&t) == 25000);
    m50_timing_add(&t, 89999999999ULL);
    assert(m50_timing_fps_x1000(&t) == 0);
}
'''
with tempfile.TemporaryDirectory(prefix="m50-timing-") as tmp:
    c = Path(tmp) / "test.c"
    binary = Path(tmp) / "test"
    c.write_text(code)
    subprocess.run(["cc", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                    "-I", str(root), str(c), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: saved cadence, real clip timing, invalid timestamps, long clips, chunk reset")
