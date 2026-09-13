#!/usr/bin/env python3
"""Generate firmware/src/kiro_ghost.h — the Kiro ghost mascot and its outfits.

Outfits: plain, Vasco shirt (match days), Santa (Christmas), passista
(Carnaval) and witch (Halloween). Every variant shares one canvas size, with
headroom above the head for hats and the feather headdress.

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
BOB = 2                      # cells of float travel
HEAD = 8                     # headroom above the head for hats; canvas is HEAD + H + BOB tall
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
    canvas = ["." * W] * (HEAD + BOB - bob) + ["".join(r) for r in rows] + ["." * W] * bob
    return canvas


def dress_vasco(canvas: list[str], bob: int) -> list[str]:
    """Overlay the shirt: 's' shirt, 'w' sash, 'x' cross, on body cells only."""
    rows = [list(r) for r in canvas]
    top = HEAD + BOB - bob                            # canvas row of BASE row 0
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


def _paint(rows, y, x, ch):
    if 0 <= y < len(rows) and 0 <= x < W:
        rows[y][x] = ch


def _body_rows(rows, top, first, last, fn):
    """fn(canvas_y, base_y, x) -> char or None, applied to body cells of BASE rows first..last."""
    for by in range(first, last + 1):
        y = top + by
        for x, c in enumerate(rows[y]):
            if c == "#":
                ch = fn(y, by, x)
                if ch:
                    rows[y][x] = ch


# Santa: red hat with white brim and pompom, red suit with white hem and black belt.
# Trim is light gray, not white: pure white vanishes against the white ghost.
SANTA_PALETTE = PALETTE + [0xD8A2, 0xBDF7, 0x2104, 0xFE60]   # r red, W trim, k belt, y buckle


def dress_santa(canvas, bob):
    rows = [list(r) for r in canvas]
    top = HEAD + BOB - bob
    for x in range(7, 12):
        _paint(rows, top, x, "r")
    for x in range(5, 14):
        _paint(rows, top + 1, x, "r")
    for x in range(3, 16):
        _paint(rows, top + 2, x, "W")                 # fur brim over the head
    for k, (x0, x1) in enumerate(((5, 13), (6, 12), (7, 12), (9, 12), (11, 13), (12, 14))):
        for x in range(x0, x1 + 1):
            _paint(rows, top - 1 - k, x, "r")        # cone leaning right
    for y, x in ((top - 7, 14), (top - 7, 15), (top - 6, 15)):
        _paint(rows, y, x, "W")                       # pompom
    belt = SHIRT_TOP + 4
    _body_rows(rows, top, SHIRT_TOP, SHIRT_BOTTOM + 1, lambda y, by, x:
               "W" if by == SHIRT_BOTTOM + 1 else
               ("y" if 8 <= x <= 9 else "k") if by == belt else "r")
    return ["".join(r) for r in rows]


# Passista: feather headdress (pink/yellow/green) on a gold band, sequined top,
# green waistband and a yellow/pink fringe skirt.
CARNAVAL_PALETTE = PALETTE + [0xFE60, 0xF8B2, 0x2FE4, 0xFFE0]   # g gold, p pink, e green, Y yellow


def dress_carnaval(canvas, bob):
    rows = [list(r) for r in canvas]
    top = HEAD + BOB - bob
    for x in range(4, 15):
        _paint(rows, top + 1, x, "g")                 # gold band
    for x in range(7, 12):
        _paint(rows, top, x, "g")
    plumes = ((5, -1, 6, "p"), (7, -1, 8, "Y"), (9, 0, 9, "e"), (11, 1, 8, "Y"), (13, 1, 6, "p"))
    for bx, lean, height, ch in plumes:
        for k in range(height):
            x = bx + (lean * k) // 3
            _paint(rows, top - 1 - k, x, ch)
            if k < height - 2:
                _paint(rows, top - 1 - k, x + 1, ch)
    waist = SHIRT_TOP + 4
    _body_rows(rows, top, SHIRT_TOP, SHIRT_BOTTOM + 1, lambda y, by, x:
               ("g" if (x + by) % 2 else "p") if by < waist else
               "e" if by == waist else ("Y" if x % 2 else "p"))
    return ["".join(r) for r in rows]


# Witch: pointed dark hat with an orange band, purple cape with an orange collar.
HALLOWEEN_PALETTE = PALETTE + [0x4010, 0xFC00, 0x923F]   # h hat, O orange, c cape


def dress_halloween(canvas, bob):
    rows = [list(r) for r in canvas]
    top = HEAD + BOB - bob
    for x in range(2, 17):
        _paint(rows, top + 1, x, "h")                 # wide brim
    for x in range(6, 13):
        _paint(rows, top, x, "O")                     # orange band
    for k, (x0, x1) in enumerate(((7, 11), (7, 11), (8, 11), (8, 10), (9, 11), (10, 12), (11, 12))):
        for x in range(x0, x1 + 1):
            _paint(rows, top - 1 - k, x, "h")
    _body_rows(rows, top, SHIRT_TOP, SHIRT_BOTTOM, lambda y, by, x:
               "O" if by == SHIRT_TOP else "c")
    return ["".join(r) for r in rows]


OUTFITS = {
    "plain": lambda f, bob: f,
    "vasco": dress_vasco,
    "santa": dress_santa,
    "carnaval": dress_carnaval,
    "halloween": dress_halloween,
}


def sequence(outfit: str = "plain") -> list[tuple[list[str], int]]:
    out = []
    for i in range(24):
        bob = (0, 1, 2, 1)[i % 4]
        sway = (1, 0, -1, 0)[i % 4]
        eyes = "right" if 10 <= i < 14 else "left" if 14 <= i < 18 else "open"
        frames = [(frame(bob, sway, eyes), FRAME_MS)]
        if i in (5, 20):
            frames.append((frame(bob, sway, "closed"), BLINK_MS))
        for f, ms in frames:
            out.append((OUTFITS[outfit](f, bob), ms))
    return out


def emit(name: str, frames: list, palette: list[int], extra: str = "") -> list[str]:
    code = {".": 0, "#": 1, "o": 2, **{ch: 3 + i for i, ch in enumerate(extra)}}
    cells = [code[c] for f, _ in frames for row in f for c in row]
    stride = W * (HEAD + H + BOB)
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
        f'    "{name}", "kiro", {W}, {HEAD + H + BOB}, 0, 0, {len(frames)}, 0, {len(frames) - 1}, {len(palette)},',
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
    lines += emit("kiro_vasco", sequence("vasco"), VASCO_PALETTE, "swx")
    lines += emit("kiro_santa", sequence("santa"), SANTA_PALETTE, "rWky")
    lines += emit("kiro_carnaval", sequence("carnaval"), CARNAVAL_PALETTE, "gpeY")
    lines += emit("kiro_halloween", sequence("halloween"), HALLOWEEN_PALETTE, "hOc")
    out = Path(__file__).resolve().parent.parent / "firmware" / "src" / "kiro_ghost.h"
    out.write_text("\n".join(lines))
    print(f"wrote {out} (plain, Vasco, Santa, passista, witch; {W}x{HEAD + H + BOB} cells)")


if __name__ == "__main__":
    main()
