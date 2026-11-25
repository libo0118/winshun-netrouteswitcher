#!/usr/bin/env python3
"""Generate a 64x64 RGBA PNG plugin icon using Python standard library only."""

from __future__ import annotations

import argparse
import math
import struct
import zlib
from pathlib import Path

WIDTH = 64
HEIGHT = 64
SUPERSAMPLE = 5
BG_COLOR = (37, 99, 235)  # #2563EB
FG_COLOR = (255, 255, 255)
STROKE_RADIUS = 2.35

ARROW_SEGMENTS = (
    # Top arrow pointing right
    (16.5, 21.5, 46.5, 21.5),
    (39.0, 14.5, 47.0, 21.5),
    (39.0, 28.5, 47.0, 21.5),
    # Bottom arrow pointing left
    (17.5, 42.5, 47.5, 42.5),
    (25.0, 35.5, 17.0, 42.5),
    (25.0, 49.5, 17.0, 42.5),
)


def sd_rounded_box(px: float, py: float, cx: float, cy: float, half: float, r: float) -> float:
    dx = abs(px - cx) - (half - r)
    dy = abs(py - cy) - (half - r)
    return math.hypot(max(dx, 0.0), max(dy, 0.0)) + min(max(dx, dy), 0.0) - r


def sd_segment(px: float, py: float, ax: float, ay: float, bx: float, by: float) -> float:
    pax, pay = px - ax, py - ay
    bax, bay = bx - ax, by - ay
    denom = bax * bax + bay * bay
    h = max(0.0, min(1.0, (pax * bax + pay * bay) / denom)) if denom > 0.0 else 0.0
    return math.hypot(pax - bax * h, pay - bay * h)


def render_rgba() -> bytes:
    rows = bytearray()
    samples = SUPERSAMPLE * SUPERSAMPLE
    step = 1.0 / SUPERSAMPLE
    for py in range(HEIGHT):
        rows.append(0)  # PNG filter type 0 (None)
        for px in range(WIDTH):
            sum_r = sum_g = sum_b = sum_a = 0.0
            for sy in range(SUPERSAMPLE):
                y = py + (sy + 0.5) * step
                for sx in range(SUPERSAMPLE):
                    x = px + (sx + 0.5) * step
                    if sd_rounded_box(x, y, 32.0, 32.0, 28.0, 13.0) > 0.0:
                        continue
                    d_arrow = min(sd_segment(x, y, *seg) for seg in ARROW_SEGMENTS) - STROKE_RADIUS
                    r, g, b = FG_COLOR if d_arrow <= 0.0 else BG_COLOR
                    sum_r += r
                    sum_g += g
                    sum_b += b
                    sum_a += 1.0
            if sum_a <= 0.0:
                rows.extend((0, 0, 0, 0))
            else:
                alpha = round(255.0 * sum_a / samples)
                rows.extend((
                    round(sum_r / sum_a),
                    round(sum_g / sum_a),
                    round(sum_b / sum_a),
                    max(0, min(255, alpha)),
                ))
    return bytes(rows)


def png_chunk(tag: bytes, payload: bytes) -> bytes:
    crc = zlib.crc32(tag + payload) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + tag + payload + struct.pack(">I", crc)


def write_png(out_path: Path) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    raw = render_rgba()
    ihdr = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6, 0, 0, 0)
    idat = zlib.compress(raw, level=9)
    png_bytes = (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", ihdr)
        + png_chunk(b"IDAT", idat)
        + png_chunk(b"IEND", b"")
    )
    out_path.write_bytes(png_bytes)


def main() -> int:
    default_out = Path(__file__).resolve().parent.parent / "icon.png"
    parser = argparse.ArgumentParser(description="Generate 64x64 RGBA PNG icon")
    parser.add_argument("--out", default=str(default_out), help="Output PNG path")
    args = parser.parse_args()
    out_path = Path(args.out).resolve()
    write_png(out_path)
    print(f"[icon] Generated {out_path} ({WIDTH}x{HEIGHT} RGBA, {out_path.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
