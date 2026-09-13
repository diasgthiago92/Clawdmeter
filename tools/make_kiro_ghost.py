#!/usr/bin/env python3
"""Generate firmware/src/kiro_ghost.h — the Kiro ghost mascot (plain and in a Vasco shirt).

The silhouette is the official Kiro logo (https://kiro.dev/icon.svg) sampled
onto an 18x22 cell grid and tidied by hand below; the animation (float, tail
sway, blink, glance) is built from it here. Output uses the splash engine's
splash_anim_def_t so the mascot renderer can draw it like any Clawd act.

    python3 tools/make_kiro_ghost.py
"""

from pathlib import Path

# '#' body, 'o' eye, '.' empty. Bottom two rows are the tail, swayed per frame.
BASE = [
    ".......#####......",
    ".....#########....",
    "....###########...",
    "...#############..",
    "...##############.",
    "..###############.",
    "..######oo##oo###.",
    "..######oo##oo###.",
    "..######oo##oo###.",
    "..######oo##oo###.",
    "..###############.",
    "..###############.",
    ".################.",
    ".################.",
    "#################.",
    "################..",
    ".###############..",
    "...############...",
    "...############...",
    "...###########....",
    "....####..###.....",
    ".....##....#......",
]
W, H = len(BASE[0]), len(BASE)
BOB = 2                      # cells of float travel; canvas is H + BOB tall
PALETTE = [0x0000, 0xFFFF, 0x10A2]   # background, white body, near-black eyes
# Vasco kit: black shirt (lifted to dark gray so it reads on the black screen),
# white diagonal sash, red cross on the sash.
VASCO_PALETTE = PALETTE + [0x3186, 0xFFFF, 0xD003]
SHIRT_TOP, SHIRT_BOTTOM = 11, 19      # BASE rows the shirt covers (tail stays bare)
FRAME_MS = 220
BLINK_MS = 140


def frame(bob: int, sway: int, eyes: str) -> list[str]:
    rows = [list(r) for r in BASE]
    # Eyes: "open", "closed" (one squint row), or glance "left"/"right".
    eye_cells = [(y, x) for y, r in enumerate(BASE) for x, c in enumerate(r) if c == "o"]
    for y, x in eye_cells:
        rows[y][x] = "#"
    if eyes == "closed":
        for y, x in eye_cells:
            if y == 8:
                rows[y][x] = "o"
    else:
        dx = {"open": 0, "left": -1, "right": 1}[eyes]
        for y, x in eye_cells:
            rows[y][x + dx] = "o"
    # Tail sway: shift the bottom two rows sideways.
    for y in (H - 2, H - 1):
        r = "".join(rows[y])
        rows[y] = list(r[-sway:] + r[:-sway]) if sway else list(r)
    canvas = ["." * W] * (BOB - bob) + ["".join(r) for r in rows] + ["." * W] * bob
    return canvas


def dress_vasco(canvas: list[str], bob: int) -> list[str]:
    """Overlay the shirt: 's' shirt, 'w' sash, 'x' cross, on body cells only."""
    rows = [list(r) for r in canvas]
    top = BOB - bob                                   # canvas row of BASE row 0
    for by in range(SHIRT_TOP, SHIRT_BOTTOM + 1):
        y = top + by
        t = (by - SHIRT_TOP) / (SHIRT_BOTTOM - SHIRT_TOP)
        sash_x = round(3 + t * 10)                    # wearer's right shoulder -> left hip
        for x, c in enumerate(rows[y]):
            if c != "#":
                continue
            rows[y][x] = "w" if sash_x <= x <= sash_x + 1 else "s"
    # Red cross on the upper part of the sash.
    cross_row = SHIRT_TOP + 3
    cy = top + cross_row
    t = (cross_row - SHIRT_TOP) / (SHIRT_BOTTOM - SHIRT_TOP)
    cx = round(3 + t * 10) + 1
    for dy, dx in ((0, 0), (-1, 0), (1, 0), (0, -1), (0, 1)):   # plus sign, 3x3
        if rows[cy + dy][cx + dx] != ".":
            rows[cy + dy][cx + dx] = "x"
    return ["".join(r) for r in rows]


def sequence(vasco: bool = False) -> list[tuple[list[str], int]]:
    out = []
    for i in range(24):
        bob = (0, 1, 2, 1)[i % 4]
        sway = (1, 0, -1, 0)[i % 4]
        eyes = "right" if 10 <= i < 14 else "left" if 14 <= i < 18 else "open"
        frames = [(frame(bob, sway, eyes), FRAME_MS)]
        if i in (5, 20):
            frames.append((frame(bob, sway, "closed"), BLINK_MS))
        for f, ms in frames:
            out.append((dress_vasco(f, bob) if vasco else f, ms))
    return out


def emit(name: str, frames: list, palette: list[int]) -> list[str]:
    code = {".": 0, "#": 1, "o": 2, "s": 3, "w": 4, "x": 5}
    cells = [code[c] for f, _ in frames for row in f for c in row]
    stride = W * (H + BOB)
    lines = [
        f"static const uint16_t {name}_palette[{len(palette)}] = {{"
        + ", ".join(f"0x{c:04X}" for c in palette) + "};",
        f"static const uint16_t {name}_holds[{len(frames)}] = {{"
        + ", ".join(str(ms) for _, ms in frames) + "};",
        f"static const uint8_t {name}_frames[{len(cells)}] = {{",
    ]
    for i in range(0, len(cells), stride):
        lines.append("    " + ",".join(map(str, cells[i:i + stride])) + ",")
    lines += [
        "};",
        f"static const splash_anim_def_t {name}_anim = {{",
        f'    "{name}", "kiro", {W}, {H + BOB}, 0, 0, {len(frames)}, 0, {len(frames) - 1}, {len(palette)},',
        f"    {name}_palette, {name}_frames, {name}_holds,",
        "};",
        "",
    ]
    return lines


def main() -> None:
    lines = [
        "// Generated by tools/make_kiro_ghost.py — do not hand-edit.",
        "// Kiro ghost mascot; silhouette from the official Kiro logo.",
        "#pragma once",
        '#include "splash_animations.h"',
        "",
    ]
    lines += emit("kiro_ghost", sequence(), PALETTE)
    lines += emit("kiro_vasco", sequence(vasco=True), VASCO_PALETTE)
    out = Path(__file__).resolve().parent.parent / "firmware" / "src" / "kiro_ghost.h"
    out.write_text("\n".join(lines))
    print(f"wrote {out} (plain + Vasco, {W}x{H + BOB} cells)")


if __name__ == "__main__":
    main()
