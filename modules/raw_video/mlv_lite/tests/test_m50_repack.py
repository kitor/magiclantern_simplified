#!/usr/bin/env python3
"""Host regression tests for the actual mlv_lite software repackers.

Run with Python 3; optionally pass a 2096-pixel-wide Canon 14-bit sensor dump.
The independent oracle models the documented stream as MSB-first samples in
little-endian words, without copying the production bit-shift expressions.
"""
import ctypes
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest


def pack(samples, depth):
    bits = ''.join(f'{p:0{depth}b}' for p in samples)
    assert len(bits) % 16 == 0
    return b''.join(int(bits[i:i + 16], 2).to_bytes(2, 'little')
                    for i in range(0, len(bits), 16))


def unpack(data, depth):
    bits = ''.join(f'{int.from_bytes(data[i:i + 2], "little"):016b}'
                   for i in range(0, len(data), 2))
    return [int(bits[i:i + depth], 2) for i in range(0, len(bits), depth)]


DUMP = Path(sys.argv.pop(1)) if len(sys.argv) > 1 else None


class RepackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        source = (Path(__file__).resolve().parents[1] / 'mlv_lite.c').read_text()
        start = source.index('static inline uint32_t repack_load_word')
        end = source.index('static void compress_task()', start)
        code = '#include <stdint.h>\n#define FAST\n' + source[start:end]
        for depth in (10, 12):
            code += (f'void run{depth}(uint8_t *d, const uint8_t *s, int n) '
                     f'{{ repack_14_to_{depth}(d, s, n); }}\n')
        cfile = Path(cls.tmp.name) / 'repack.c'
        library = cfile.with_suffix('.so')
        cfile.write_text(code)
        subprocess.run(['cc', '-shared', '-fPIC', '-O2', '-Wall', '-Wextra',
                        '-Werror', str(cfile), '-o', str(library)], check=True)
        cls.lib = ctypes.CDLL(str(library))
        for depth in (10, 12):
            getattr(cls.lib, f'run{depth}').argtypes = [ctypes.c_void_p,
                                                      ctypes.c_void_p,
                                                      ctypes.c_int]

    def check(self, data):
        original = unpack(data, 14)
        # Exercise deliberately unaligned input/output and guard output bounds.
        src = ctypes.create_string_buffer(b'\xa5' + data + b'\xa5')
        for depth in (10, 12):
            expected = pack([p >> (14 - depth) for p in original], depth)
            dst = ctypes.create_string_buffer(b'\xa5' * (len(expected) + 2))
            getattr(self.lib, f'run{depth}')(ctypes.byref(dst, 1),
                                            ctypes.byref(src, 1), len(original))
            self.assertEqual(dst.raw[1:1 + len(expected)], expected)
            self.assertEqual(dst.raw[0], 0xa5)
            self.assertEqual(dst.raw[len(expected) + 1], 0xa5)
            self.assertEqual(unpack(expected, depth),
                             [p >> (14 - depth) for p in original])
        self.assertEqual(src.raw[1:1 + len(data)], data)

    def test_known_word_order(self):
        # The first sample occupies bits 15:2 of the first LE word.
        self.assertEqual(pack([4096] + [0] * 7, 14).hex(),
                         '0040000000000000000000000000')
        self.check(pack([0, 1, 2, 3, 2048, 8191, 8192, 16383], 14))

    def test_all_sample_values_at_every_block_position(self):
        # Different neighbours expose spill/carry mistakes at word boundaries.
        for position in range(8):
            samples = []
            for value in range(16384):
                row = [0, 16383, 2048, 8191, 8192, 12345, 1, 4095]
                row[position] = value
                samples.extend(row)
            self.check(pack(samples, 14))

    def test_recording_rows(self):
        rng = random.Random(50)
        for width in (0, 8, 16, 32, 640, 1920, 2008, 2096):
            self.check(pack([rng.randrange(16384) for _ in range(width)], 14))

    @unittest.skipIf(DUMP is None, 'pass a Canon 14-bit sensor dump to test real rows')
    def test_real_sensor_dump(self):
        data = DUMP.read_bytes()
        pitch = 3668
        self.assertEqual(len(data) % pitch, 0)
        for y in range(len(data) // pitch):
            row = data[y * pitch:(y + 1) * pitch]
            self.check(row)
            # Optical-black crop: 88 pixels; 2008 pixels remain.
            self.check(row[88 * 14 // 8:])


if __name__ == '__main__':
    unittest.main()
