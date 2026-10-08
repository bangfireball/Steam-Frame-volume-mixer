#!/usr/bin/env python3
"""Generate original launcher icons using only Python's standard library."""
import pathlib
import struct
import zlib

ROOT = pathlib.Path(__file__).resolve().parents[1] / "packaging" / "icons"
def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
for size in (48, 128, 256):
    pixels = bytearray()
    for y in range(size):
        pixels.append(0)
        for x in range(size):
            color = (14, 19, 28, 255)
            for n in range(3):
                cx, cy = size * (.25 + n * .25), size * (.35 if n == 1 else .65)
                if abs(x-cx) <= size*.025 and size*.18 <= y <= size*.82:
                    color = (64, 199, 222, 255)
                if abs(x-cx) <= size*.10 and abs(y-cy) <= size*.06:
                    color = (240, 245, 255, 255)
            pixels.extend(color)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b"")
    ROOT.mkdir(parents=True, exist_ok=True)
    (ROOT / f"frame-volume-mixer-{size}.png").write_bytes(png)
