#!/usr/bin/env python3
"""Build sekrio_ico.ico -- the icon embedded into ModEngineFixer.exe.

    input : sekiro.jpg        (repo root, portrait art, 1221x988)
    output: sekrio_ico.ico    (repo root -- consumed by src/fixer_gui/fixer.rc)

A "correct" Windows icon is not a renamed bitmap: it is a container that holds
one frame per size, and the shell picks whichever frame matches the display
scale.  This script renders every size straight from the full-resolution art
(so the small frames are not mushy rescales of a 256px master) and mixes the
two frame encodings Windows expects:

    16 .. 96 px   32-bit BMP/DIB frames + AND mask  (readable by every shell)
    128, 256 px   PNG frames                        (standard for large sizes)

The source picture is not square, so it is centre-cropped to a square before
being scaled down -- stretching it would distort Wolf's face.

Usage:
    py -3 src\\fixer_gui\\make_icon.py
    py -3 src\\fixer_gui\\make_icon.py --png-only      # PNG frames at every size
    py -3 src\\fixer_gui\\make_icon.py --tighten 1.6   # face close-up crop
"""

from __future__ import annotations

import argparse
import io
import struct
from pathlib import Path

from PIL import Image

# One frame per size Windows actually asks for, including the sizes it wants at
# 125 % / 150 % / 175 % display scaling.
BMP_SIZES = (16, 20, 24, 32, 40, 48, 60, 64, 72, 96)
PNG_SIZES = (128, 256)

# 0 = left/top, 0.5 = centre, 1 = right/bottom.  Centre keeps the face centred.
ANCHOR_X = 0.5
ANCHOR_Y = 0.5


def square_crop(im: Image.Image, tighten: float = 1.0) -> Image.Image:
    """Centre-crop to the largest square that fits; return RGBA.

    `tighten` > 1 crops a smaller square, zoomed in on the same anchor, which
    makes the face readable in the 16/24/32 px frames.
    """
    w, h = im.size
    side = min(w, h)
    left = round((w - side) * ANCHOR_X)
    top = round((h - side) * ANCHOR_Y)
    box = im.crop((left, top, left + side, top + side))
    if tighten > 1.0:
        inner = max(8, round(side / tighten))
        ox = round((side - inner) * ANCHOR_X)
        oy = round((side - inner) * ANCHOR_Y)
        box = box.crop((ox, oy, ox + inner, oy + inner))
    return box.convert("RGBA")


def render(master: Image.Image, size: int) -> Image.Image:
    """Downscale the square master to `size` with a good filter."""
    if master.width == size:
        return master
    # reducing_gap does the shrink in steps; capped at 3.0 per Pillow's advice.
    gap = min(3.0, max(1.0, master.width / size))
    return master.resize((size, size), Image.Resampling.LANCZOS, reducing_gap=gap)


def bmp_frame(im: Image.Image) -> bytes:
    """A 32-bit bottom-up DIB frame plus the (all-zero) 1-bit AND mask."""
    w, h = im.size
    raw = im.tobytes("raw", "BGRA")
    stride_in = w * 4
    # DIB pixel rows run bottom-up.
    xor = b"".join(raw[y * stride_in:(y + 1) * stride_in] for y in range(h - 1, -1, -1))
    # AND-mask rows pad to 4 bytes; zero bits == "not transparent".
    and_mask = b"\x00" * ((((w + 31) // 32) * 4) * h)
    # BITMAPINFOHEADER: size, w, h*2 (XOR + AND), planes, bpp, BI_RGB, sizeImage.
    header = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0, len(xor), 0, 0, 0, 0)
    return header + xor + and_mask


def png_frame(im: Image.Image) -> bytes:
    """Encode a frame as PNG (the large-size convention inside .ico)."""
    buf = io.BytesIO()
    im.save(buf, "PNG", optimize=True)
    return buf.getvalue()


def build_ico(frames: list[tuple[int, bytes]]) -> bytes:
    """Assemble ICONDIR + one ICONDIRENTRY per frame + the frame payloads."""
    out = io.BytesIO()
    out.write(struct.pack("<HHH", 0, 1, len(frames)))  # reserved, type=icon, count
    offset = 6 + 16 * len(frames)
    for size, payload in frames:
        dim = 0 if size >= 256 else size  # 0 is how 256 is stored
        out.write(struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(payload), offset))
        offset += len(payload)
    for _, payload in frames:
        out.write(payload)
    return out.getvalue()


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Build sekrio_ico.ico from sekiro.jpg")
    ap.add_argument("--source", type=Path, default=None,
                    help="source image (default: <repo>\\sekiro.jpg)")
    ap.add_argument("--output", type=Path, default=None,
                    help="output .ico (default: <repo>\\sekrio_ico.ico)")
    ap.add_argument("--png-only", action="store_true",
                    help="encode every frame as PNG instead of small BMP frames")
    ap.add_argument("--tighten", type=float, default=1.0, metavar="N",
                    help="zoom the crop by N (e.g. 1.6 = face close-up; default 1.0)")
    args = ap.parse_args(argv)

    repo = Path(__file__).resolve().parents[2]
    src = args.source or repo / "sekiro.jpg"
    dst = args.output or repo / "sekrio_ico.ico"

    with Image.open(src) as opened:
        src_size = opened.size
        master = square_crop(opened, args.tighten)

    frames: list[tuple[int, bytes]] = []
    for size in BMP_SIZES:
        frame = render(master, size)
        frames.append((size, png_frame(frame) if args.png_only else bmp_frame(frame)))
    for size in PNG_SIZES:
        frames.append((size, png_frame(render(master, size))))

    ico = build_ico(frames)
    dst.write_bytes(ico)

    print(f"[icon] source {src.name} {src_size[0]}x{src_size[1]} -> square {master.width}x{master.height}")
    print(f"[icon] wrote  {dst}")
    print(f"[icon] {len(frames)} frames, {len(ico):,} bytes")
    for size, payload in frames:
        kind = "PNG" if payload[:8] == b"\x89PNG\r\n\x1a\n" else "BMP"
        print(f"[icon]   {size:>3}x{size:<3} {kind} {len(payload):>9,} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
