#!/usr/bin/env python3
"""Host checks of the actual M50 geometry and black-calibration C functions.

Usage: python3 tests/test_m50_geometry.py [camera.MM1 ...]
"""
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src/raw.c").read_text()
geometry = source.split("/* Restrict this geometry", 1)[1].split(
    "#else // ~CONFIG_EDMAC_RAW_SLURP", 1)[0]
geometry = "/* Restrict this geometry" + geometry
black = source[source.index("static void autodetect_black_level_calc("):
               source.index("static int autodetect_white_level(int initial_guess)\n{")]
harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>
#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define ABS(a) abs(a)
#define dbg_printf(...) ((void)0)
#define printf(...) ((void)0)
#define M50_RAW_PITCH 3668
#define M50_RAW_HEIGHT 1164
static int lv = 1, movie = 1, video_mode_resolution, video_mode_crop;
static int lv_dispsize = 1;
static int is_movie_mode(void) { return movie; }
static struct { struct { int x1,x2,y1,y2; } active_area; } raw_info;
static unsigned char *frame;
static size_t frame_size;
/* Independent bit-at-a-time decoder: MSB first in little-endian words. */
static int raw_get_pixel(int x, int y)
{
    unsigned value = 0;
    for (int n = 0; n < 14; n++) {
        unsigned bit = x * 14 + n;
        size_t off = y * 3668 + (bit / 16) * 2;
        assert(off + 1 < frame_size);
        unsigned word = frame[off] | (frame[off+1] << 8);
        value = (value << 1) | ((word >> (15 - bit % 16)) & 1);
    }
    return value;
}
static int geometry(int *width, int *height) {
''' + geometry + "\n}\n" + black + r'''
int main(int argc, char **argv)
{
    int w = 0, h = 0;
    assert(geometry(&w, &h) && w == 2096 && h == 1164);
    movie = 0; assert(!geometry(&w, &h)); movie = 1;
    lv = 0; assert(!geometry(&w, &h)); lv = 1;
    video_mode_resolution = 1; assert(!geometry(&w, &h));
    video_mode_resolution = 3; assert(!geometry(&w, &h));
    video_mode_resolution = 0;
    video_mode_crop = 1; assert(!geometry(&w, &h)); video_mode_crop = 0;
    lv_dispsize = 5; assert(!geometry(&w, &h)); lv_dispsize = 1;
    raw_info.active_area.x1 = 88; raw_info.active_area.x2 = w;
    raw_info.active_area.y1 = 34; raw_info.active_area.y2 = h;
    frame_size = 3668 * 1164;
    frame = calloc(1, frame_size); assert(frame);
    int mean = 0, noise = 0;
    assert(!autodetect_black_level(&mean, &noise));
    free(frame);
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb"); assert(f);
        fseek(f, 0, SEEK_END); frame_size = ftell(f); rewind(f);
        assert(frame_size >= 3668 * 838);
        /* Smaller dumps validate calibration only, not 1080p geometry. */
        raw_info.active_area.y2 = frame_size / 3668 < 1164 ? 838 : 1164;
        frame = malloc(frame_size); assert(frame);
        assert(fread(frame, 1, frame_size, f) == frame_size); fclose(f);
        mean = noise = 0;
        int ok = autodetect_black_level(&mean, &noise);
        fprintf(stderr, "%s: black=%d noise=%d/100 valid=%d\n",
                argv[i], mean, noise, !!ok);
        assert(ok && abs(mean - 2048) < 64);
        free(frame);
    }
    fprintf(stderr, "PASS: geometry gates, zero-frame rejection, dump calibration\n");
}
'''
with tempfile.TemporaryDirectory(prefix="m50-geometry-") as tmp:
    c = Path(tmp) / "test.c"
    binary = Path(tmp) / "test"
    c.write_text(harness)
    subprocess.run(["cc", "-std=c99", "-O1", "-g", "-fsanitize=undefined",
                    str(c), "-lm", "-o", str(binary)], check=True)
    subprocess.run([str(binary), *sys.argv[1:]], check=True)
