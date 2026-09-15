#!/usr/bin/env python3
"""Generate firmware/src/almirante.h — the Almirante (Vasco mascot) pixel sprite.

Source: assets/almirante/almirante_recorte.png, the user's image with the white
background removed. Each cell of a W x H grid takes the palette color covering
most of its source pixels (black outlines count less; black parts are lifted to dark gray so they
read on the black screen). The animations (idle fist pump/blink/breathe, walk cycle, goal jump) are
built from that still here. Output uses the splash engine's splash_anim_def_t, like the
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


JUMP = 4                     # rows of headroom for the goal celebration jump


def pad(grid, below, total):
    """Bottom-anchored canvas `total` rows taller than the art, raised `below` rows."""
    empty = [0] * W
    return ([list(empty) for _ in range(total - below)] + [list(r) for r in grid]
            + [list(empty) for _ in range(below)])


def make_anims(base):
    """{name: [(canvas, hold ms), ...]} for the idle, walk and celebrate animations."""
    white, skin, black = 3, 5, 1
    # Raised fist + forearm: upper right of the figure (the glove on his left).
    fist = [(y, x) for y in range(int(H * 0.28), int(H * 0.55)) for x in range(int(W * 0.66), W)
            if base[y][x]]
    # Eyes: white cells in the face band.
    eyes = find(base, white, int(W * 0.30), int(W * 0.75), int(H * 0.26), int(H * 0.40))
    leg_top, leg_split = int(H * 0.68), int(W * 0.48)

    def pumped(grid, lift):
        # Fist and forearm move up; the rows they leave keep the arm, so it stretches.
        g = [list(r) for r in grid]
        for y, x in sorted(fist):
            if y - lift >= 0:
                g[y - lift][x] = grid[y][x]
        return g

    def blinked(grid):
        g = [list(r) for r in grid]
        for y, x in eyes:
            g[y][x] = black if y == max(e[0] for e in eyes) else skin
        return g

    def step(left: bool, lift: int = 2):
        # One leg (and boot) lifts `lift` rows; the other stays planted.
        g = [list(r) for r in base]
        cols = range(0, leg_split) if left else range(leg_split, W)
        for x in cols:
            for y in range(leg_top, H):
                g[y][x] = 0
            for y in range(leg_top, H):
                if base[y][x]:
                    g[y - lift][x] = base[y][x]
            for y in range(leg_top - lift, leg_top):   # keep the hips joined to the lifted leg
                g[y][x] = g[y][x] or base[y][x]
        return g

    up = pumped(base, FIST_LIFT)
    idle = [
        (pad(base, 0, BOB), 700),
        (pad(up, 0, BOB)[1:] + [[0] * W], 180),   # pump: fist up, body dips
        (pad(base, 0, BOB), 180),
        (pad(up, 0, BOB)[1:] + [[0] * W], 180),
        (pad(base, 0, BOB), 900),
        (pad(blinked(base), 0, BOB), 140),
        (pad(base, 0, BOB), 600),
        (pad(base, 0, BOB)[1:] + [[0] * W], 300),  # breathe
        (pad(base, 0, BOB), 300),
    ]
    # Walk cycle, bottom-anchored like the idle pose (no bob row).
    walk = [
        (pad(step(True), 0, 1), 150),
        (pad(base, 0, 1)[1:] + [[0] * W], 110),    # passing pose dips a row
        (pad(step(False), 0, 1), 150),
        (pad(base, 0, 1)[1:] + [[0] * W], 110),
    ]
    # Goal: crouch, jump with the fist up, land, pump twice.
    high = pumped(base, FIST_LIFT + 1)
    crouch = [list(r) for r in base][:-1] + [list(base[-1])]
    celebrate = [
        (pad(crouch, 0, JUMP), 160),
        (pad(high, 2, JUMP), 90),
        (pad(high, 4, JUMP), 260),
        (pad(high, 2, JUMP), 90),
        (pad(base, 0, JUMP), 160),
        (pad(up, 0, JUMP), 170),
        (pad(base, 0, JUMP), 170),
        (pad(up, 0, JUMP), 170),
        (pad(base, 0, JUMP), 300),
    ]
    return {"almirante": idle, "almirante_walk": walk, "almirante_celebrate": celebrate}


def emit(anims) -> str:
    palette = [0x0000] + [rgb565(c[2]) for c in COLORS]
    palette += [0x0000] * (16 - len(palette))
    out = [f"""// Generated by tools/make_almirante.py — do not hand-edit.
// Almirante, the Vasco mascot, from assets/almirante/almirante_recorte.png.
// almirante_anim: idle in place · almirante_walk_anim: walk cycle ·
// almirante_celebrate_anim: goal jump. All share one palette and width.
#pragma once
#include "splash_animations.h"

static const uint16_t almirante_palette[16] = {{{",".join(f"0x{c:04X}" for c in palette)}}};
"""]
    for name, seq in anims.items():
        h = len(seq[0][0])
        frames = ",\n    ".join(",".join(str(v) for row in f for v in row) for f, _ in seq)
        holds = ",".join(str(ms) for _, ms in seq)
        out.append(f"""static const uint8_t {name}_frames[{len(seq) * W * h}] = {{
    {frames}
}};
static const uint16_t {name}_holds[{len(seq)}] = {{{holds}}};
static const splash_anim_def_t {name}_anim = {{
    "{name}", "vasco", {W}, {h}, 0, 0, {len(seq)}, 0, {len(seq) - 1}, {len(COLORS) + 1},
    almirante_palette, {name}_frames, {name}_holds,
}};
""")
    return "\n".join(out)


def preview(anims, path, scale=6):
    pal = [(0, 0, 0)] + [c[2] for c in COLORS]
    rows = []
    hmax = max(len(seq[0][0]) for seq in anims.values())
    for seq in anims.values():
        n = len(seq)
        data = bytearray()
        for y in range(hmax * scale):
            for f, _ in seq:
                off = hmax - len(f)
                for x in range(W * scale):
                    gy = y // scale - off
                    data += bytes(pal[f[gy][x // scale]] if gy >= 0 else (0, 0, 0))
                data += bytes((40, 40, 40)) * scale
        rows.append((n * (W * scale + scale), data))
    width = max(w for w, _ in rows)
    blob = bytearray()
    for w, data in rows:
        line = w // 1
        for y in range(hmax * scale):
            chunk = data[y * line * 3:(y + 1) * line * 3]
            blob += chunk + bytes((20, 20, 20)) * (width - w)
    subprocess.run(["magick", "-size", f"{width}x{hmax * scale * len(rows)}", "-depth", "8", "rgb:-", path],
                   input=bytes(blob), check=True)


if __name__ == "__main__":
    anims = make_anims(load_cells())
    OUT.write_text(emit(anims))
    print(f"wrote {OUT}: " + ", ".join(f"{k} {len(v)} frames" for k, v in anims.items()))
    if "--preview" in sys.argv:
        preview(anims, sys.argv[sys.argv.index("--preview") + 1])
