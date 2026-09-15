#!/usr/bin/env python3
"""Generate firmware/src/almirante.h — the Almirante (Vasco mascot) pixel sprite.

Source: assets/almirante/almirante_recorte.png, the user's image with the white
background removed. Each cell of a W x H grid takes the palette color covering
most of its source pixels (black outlines count less; black parts are lifted to dark gray so they
read on the black screen). The animation (fist pump, bob, blink) is built from
that still here. Output uses the splash engine's splash_anim_def_t, like the
Kiro ghost, so the corner mascot and the splash stage draw it the same way.

    python3 tools/make_almirante.py [--preview out.png]

Needs ImageMagick (`magick`) to read the PNG.
"""

import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "assets" / "almirante" / "almirante_recorte.png"
OUT = ROOT / "firmware" / "src" / "almirante.h"

W, H = 25, 41                # cells; the canvas adds BOB rows below for the bob
BOB = 1
FIST_LIFT = 2                # rows the raised fist travels on a pump
ALPHA_MIN = 110              # cells less covered than this are background

# (name, source RGB it matches, RGB565 drawn). Index 0 is the background.
COLORS = [
    ("black",  (18, 14, 16),    (58, 58, 64)),      # hat, gloves, trousers: dark gray on screen
    ("red",    (214, 32, 44),   (224, 28, 36)),     # cross, badge, mouth
    ("white",  (250, 250, 250), (250, 250, 250)),   # shirt, eyes, teeth
    ("shade",  (185, 185, 190), (170, 170, 176)),   # shirt shading
    ("skin",   (246, 205, 170), (246, 200, 160)),
    ("skin2",  (214, 150, 112), (206, 140, 100)),   # skin shadow
    ("beard",  (78, 48, 32),    (96, 58, 36)),
    ("hair",   (125, 80, 48),   (136, 88, 50)),
    ("boot",   (150, 92, 50),   (156, 96, 52)),
    ("boot2",  (100, 60, 32),   (110, 66, 36)),
]


def rgb565(rgb):
    r, g, b = rgb
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


OUTLINE_WEIGHT = 0.45        # black counts less, so thin outlines don't swallow fills


def load_cells() -> list[list[int]]:
    """Each cell takes the palette color covering most of its source pixels."""
    info = subprocess.run(["magick", "identify", "-format", "%w %h", str(SRC)],
                          capture_output=True, text=True, check=True).stdout.split()
    sw, sh = int(info[0]), int(info[1])
    raw = subprocess.run(["magick", str(SRC), "-depth", "8", "rgba:-"], capture_output=True, check=True).stdout
    nearest_cache: dict[tuple, int] = {}

    def nearest(px):
        key = (px[0] >> 3, px[1] >> 3, px[2] >> 3)
        if key not in nearest_cache:
            nearest_cache[key] = min(range(len(COLORS)),
                                     key=lambda i: sum((p - q) ** 2 for p, q in zip(px, COLORS[i][1])))
        return nearest_cache[key]

    grid = []
    for cy in range(H):
        row = []
        y0, y1 = cy * sh // H, (cy + 1) * sh // H
        for cx in range(W):
            x0, x1 = cx * sw // W, (cx + 1) * sw // W
            votes = [0.0] * len(COLORS)
            covered = total = 0
            for y in range(y0, y1):
                for x in range(x0, x1):
                    i = (y * sw + x) * 4
                    total += 1
                    if raw[i + 3] < 128:
                        continue
                    covered += 1
                    c = nearest(raw[i:i + 3])
                    votes[c] += OUTLINE_WEIGHT if c == 0 else 1.0
            if covered * 255 < total * ALPHA_MIN:
                row.append(0)
                continue
            row.append(max(range(len(COLORS)), key=lambda i: votes[i]) + 1)
        grid.append(row)
    return grid


def find(grid, code, x0, x1, y0, y1):
    return [(y, x) for y in range(y0, y1) for x in range(x0, x1) if grid[y][x] == code]


def make_frames(base):
    white, skin, black = 3, 5, 1
    # Raised fist + forearm: upper right of the figure (the glove on his left).
    fist = [(y, x) for y in range(int(H * 0.28), int(H * 0.55)) for x in range(int(W * 0.66), W)
            if base[y][x]]
    # Eyes: white cells in the face band.
    eyes = find(base, white, int(W * 0.30), int(W * 0.75), int(H * 0.26), int(H * 0.40))

    def canvas(grid, bob):
        empty = [0] * W
        return [list(empty) for _ in range(BOB - bob)] + [list(r) for r in grid] + [list(empty) for _ in range(bob)]

    def pumped(lift):
        # Fist and forearm move up; the rows they leave keep the arm, so it stretches.
        g = [list(r) for r in base]
        for y, x in sorted(fist):
            if y - lift >= 0:
                g[y - lift][x] = base[y][x]
        return g

    def blinked(grid):
        g = [list(r) for r in grid]
        for y, x in eyes:
            g[y][x] = black if y == max(e[0] for e in eyes) else skin
        return g

    up = pumped(FIST_LIFT)
    seq = [
        (canvas(base, 0), 700),
        (canvas(up, 1), 180),       # pump: fist up, body dips
        (canvas(base, 0), 180),
        (canvas(up, 1), 180),
        (canvas(base, 0), 900),
        (canvas(blinked(base), 0), 140),
        (canvas(base, 0), 600),
        (canvas(base, 1), 300),     # breathe
        (canvas(base, 0), 300),
    ]
    return seq


def emit(seq) -> str:
    h = H + BOB
    palette = [0x0000] + [rgb565(c[2]) for c in COLORS]
    palette += [0x0000] * (16 - len(palette))
    frames = ",\n    ".join(",".join(str(v) for row in f for v in row) for f, _ in seq)
    holds = ",".join(str(ms) for _, ms in seq)
    return f"""// Generated by tools/make_almirante.py — do not hand-edit.
// Almirante, the Vasco mascot, from assets/almirante/almirante_recorte.png.
#pragma once
#include "splash_animations.h"

static const uint16_t almirante_palette[16] = {{{",".join(f"0x{c:04X}" for c in palette)}}};
static const uint8_t almirante_frames[{len(seq) * W * h}] = {{
    {frames}
}};
static const uint16_t almirante_holds[{len(seq)}] = {{{holds}}};

static const splash_anim_def_t almirante_anim = {{
    "almirante", "vasco", {W}, {h}, 0, 0, {len(seq)}, 0, {len(seq) - 1}, {len(COLORS) + 1},
    almirante_palette, almirante_frames, almirante_holds,
}};
"""


def preview(seq, path, scale=8):
    h = H + BOB
    pal = [(0, 0, 0)] + [c[2] for c in COLORS]
    n = len(seq)
    data = bytearray()
    for y in range(h * scale):
        for i in range(n):
            f = seq[i][0]
            for x in range(W * scale):
                data += bytes(pal[f[y // scale][x // scale]])
            data += bytes((40, 40, 40)) * scale        # gap between frames
    width = n * (W * scale + scale)
    subprocess.run(["magick", "-size", f"{width}x{h * scale}", "-depth", "8", "rgb:-", path],
                   input=bytes(data), check=True)


if __name__ == "__main__":
    seq = make_frames(load_cells())
    OUT.write_text(emit(seq))
    print(f"wrote {OUT} ({len(seq)} frames, {W}x{H + BOB} cells)")
    if "--preview" in sys.argv:
        preview(seq, sys.argv[sys.argv.index("--preview") + 1])
