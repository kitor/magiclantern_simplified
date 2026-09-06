#!/usr/bin/env python3
"""Inspect standalone uncompressed mlv_lite clips; optionally repair a NEW copy.

Average saved-frame FPS corrects overall duration, not irregular frame cadence,
missing frames, or tearing. Originals are never opened for writing. RAWI's
sensor frame_size is corrected to sensor pitch * height, not cropped VIDF size.
Only the known v2.0/180-byte RAWI layout is supported. No third-party packages.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import tempfile
from fractions import Fraction


class InvalidMLV(ValueError):
    pass


def inspect(path):
    path = Path(path)
    if path.suffix.lower() != '.mlv':
        raise InvalidMLV('Only the main .MLV file is supported')
    if any(p.stem.lower() == path.stem.lower() and re.fullmatch(r'\.m\d\d', p.suffix.lower())
           for p in path.parent.iterdir()):
        raise InvalidMLV('Spanned clip: sibling .M00/.M01 files exist')
    size = path.stat().st_size
    raw = None
    mlvi = None
    frames = []
    digest = hashlib.sha256()
    with path.open('rb') as f:
        pos = 0
        while pos < size:
            f.seek(pos)
            h = f.read(16)
            if len(h) != 16:
                raise InvalidMLV(f'Truncated block header at {pos}')
            tag, length, timestamp = struct.unpack('<4sIQ', h)
            if length < 16 or length > size - pos:
                raise InvalidMLV(f'Invalid block size at {pos}')
            if pos == 0 and tag != b'MLVI':
                raise InvalidMLV('Missing initial MLVI')
            if tag == b'MLVI':
                if mlvi is not None or pos or length != 52:
                    raise InvalidMLV('Unsupported or repeated MLVI')
                f.seek(pos)
                mlvi = f.read(52)
                version = mlvi[8:16].rstrip(b'\0')
                file_num, file_count, flags, video, audio, count, acount, nom, den = struct.unpack_from('<HHIHHIIII', mlvi, 24)
                if version != b'v2.0' or file_num != 0 or file_count not in (0, 1):
                    raise InvalidMLV('Unsupported version or spanned file')
                if video != 1 or audio or acount or flags & 1:
                    raise InvalidMLV('Only ordered uncompressed RAW without audio is supported')
                if not nom or not den:
                    raise InvalidMLV('Invalid declared FPS')
            elif tag == b'RAWI':
                if raw is not None or frames or length != 180:
                    raise InvalidMLV('Unsupported, late or repeated RAWI')
                f.seek(pos)
                b = f.read(length)
                xres, yres, api, buffer, height, width, pitch, frame_size, bits = struct.unpack_from('<HHIIiiiii', b, 16)
                if api != 1 or bits not in (10, 12, 14) or min(xres, yres, height, width, pitch) <= 0:
                    raise InvalidMLV('Unsupported RAWI geometry/bit depth')
                if width * bits % 8 or pitch != width * bits // 8 or xres > width or yres > height or xres * yres * bits % 8 or pitch * height > 0x7fffffff:
                    raise InvalidMLV('Inconsistent RAWI geometry')
                raw = dict(offset=pos, width=xres, height=yres, sensor_width=width,
                           sensor_height=height, pitch=pitch, bits=bits,
                           frame_size=frame_size, corrected_frame_size=pitch * height)
            elif tag == b'VIDF':
                if raw is None or length < 32:
                    raise InvalidMLV('Missing RAWI or short VIDF')
                b = f.read(16)
                number, cx, cy, px, py, space = struct.unpack('<IHHHHI', b)
                payload = raw['width'] * raw['height'] * raw['bits'] // 8
                available = length - 32 - space
                if available < payload or available - payload > 511:
                    raise InvalidMLV('VIDF size disagrees with RAWI (beyond sector padding)')
                if number != len(frames) or (frames and timestamp <= frames[-1]):
                    raise InvalidMLV('Non-contiguous frame numbers or non-increasing timestamps')
                frames.append(timestamp)
                f.seek(pos + 32 + space)
                remaining = payload
                while remaining:
                    chunk = f.read(min(remaining, 1024 * 1024))
                    if not chunk:
                        raise InvalidMLV('Truncated frame data')
                    digest.update(chunk)
                    remaining -= len(chunk)
            elif tag not in {b'RAWC', b'IDNT', b'EXPO', b'LENS', b'RTCI', b'WBAL', b'VERS', b'NULL', b'MARK', b'STYL', b'INFO', b'DISO'}:
                raise InvalidMLV(f'Unsupported block {tag!r}')
            pos += length
    if mlvi is None or raw is None or len(frames) < 2 or count != len(frames):
        raise InvalidMLV('Missing metadata, fewer than 2 frames, or frame count mismatch')
    fps = Fraction((len(frames) - 1) * 1_000_000, frames[-1] - frames[0]).limit_denominator(1_000_000)
    if fps.numerator > 0xffffffff:
        raise InvalidMLV('FPS cannot be represented safely')
    intervals = [b - a for a, b in zip(frames, frames[1:])]
    return dict(path=str(path), bytes=size, raw=raw, frames=len(frames),
                declared_fps=nom / den, saved_fps=float(fps),
                corrected_fps_numerator=fps.numerator, corrected_fps_denominator=fps.denominator,
                first_timestamp_us=frames[0], last_timestamp_us=frames[-1],
                min_interval_us=min(intervals), max_interval_us=max(intervals),
                corrected_duration_seconds=len(frames) / float(fps),
                pixel_sha256=digest.hexdigest())


def repair_copy(source, destination):
    source, destination = Path(source), Path(destination)
    if source.resolve() == destination.resolve() or destination.exists():
        raise InvalidMLV('Destination must be a new file, never the original')
    before = inspect(source)
    destination.parent.mkdir(parents=True, exist_ok=True)
    fd, tmpname = tempfile.mkstemp(prefix='.mlv-repair-', suffix='.MLV', dir=destination.parent)
    os.close(fd)
    tmp = Path(tmpname)
    try:
        shutil.copyfile(source, tmp)
        with tmp.open('r+b') as f:
            f.seek(44)
            f.write(struct.pack('<II', before['corrected_fps_numerator'], before['corrected_fps_denominator']))
            f.seek(before['raw']['offset'] + 40)
            f.write(struct.pack('<i', before['raw']['corrected_frame_size']))
            f.flush()
            os.fsync(f.fileno())
        after = inspect(tmp)
        if before['pixel_sha256'] != after['pixel_sha256'] or before['bytes'] != after['bytes']:
            raise InvalidMLV('Pixel payload validation failed')
        # Compare every byte except the two explicitly edited metadata fields.
        patches = [(44, 52), (before['raw']['offset'] + 40, before['raw']['offset'] + 44)]
        with source.open('rb') as src, tmp.open('rb') as dst:
            offset = 0
            while True:
                a, b = bytearray(src.read(1024 * 1024)), bytearray(dst.read(1024 * 1024))
                if not a and not b:
                    break
                for start, end in patches:
                    lo, hi = max(start - offset, 0), min(end - offset, len(a))
                    if lo < hi:
                        a[lo:hi] = b'\0' * (hi - lo)
                        b[lo:hi] = b'\0' * (hi - lo)
                if a != b:
                    raise InvalidMLV('Unexpected changes outside repaired metadata')
                offset += len(a)
        # Hard link publishes atomically and refuses to overwrite an existing path.
        os.link(tmp, destination)
        after['path'] = str(destination)
        return dict(original=before, repaired=after, pixels_unchanged=True,
                    limitation='Average FPS fixes duration only; irregular cadence and tearing remain.')
    finally:
        tmp.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('clips', nargs='+', type=Path)
    parser.add_argument('--repair-dir', type=Path, help='Create verified new copies here; refuse overwrites')
    args = parser.parse_args()
    try:
        reports = [repair_copy(p, args.repair_dir / p.name) if args.repair_dir else inspect(p) for p in args.clips]
    except (OSError, InvalidMLV) as error:
        parser.exit(1, f'Error: {error}\n')
    print(json.dumps(reports, indent=2))


if __name__ == '__main__':
    main()
