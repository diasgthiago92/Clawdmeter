#!/usr/bin/env python3
"""Desenha os logos do Instagram e do TikTok e gera firmware/src/social_icons.h.

    python3 tools/make_social_icons.py

Precisa do ImageMagick (`magick`). Cada logo é desenhado em 256 px e reduzido para
28 px (tela Cronograma de Posts); o resultado sai em RGB565A8 (RGB565 little-endian
de w*h pixels seguido de w*h bytes de alfa), como os outros ícones.
São desenhos simplificados das marcas, não os arquivos oficiais.
"""
import subprocess
import tempfile
from pathlib import Path

SIZE = 28
OUT = Path(__file__).resolve().parent.parent / "firmware" / "src" / "social_icons.h"


def magick(*args: str) -> None:
    subprocess.run(["magick", *args], check=True)


def instagram(tmp: Path) -> Path:
    bg, out = tmp / "ig_bg.png", tmp / "ig.png"
    magick("-size", "256x256", "gradient:#feda75-#7b3fd0",
           "(", "-size", "256x256", "xc:none", "-fill", "white",
           "-draw", "roundrectangle 8,8 247,247 64,64", ")",
           "-alpha", "off", "-compose", "CopyOpacity", "-composite", str(bg))
    magick(str(bg), "-fill", "none", "-stroke", "white", "-strokewidth", "18",
           "-draw", "roundrectangle 52,52 203,203 50,50", "-draw", "circle 128,128 128,88",
           "-stroke", "none", "-fill", "white", "-draw", "circle 172,84 172,72", str(out))
    return out


def tiktok(tmp: Path) -> Path:
    layers = []
    for name, color, dx, dy in (("c", "#25f4ee", -9, -6), ("r", "#fe2c55", 9, 6), ("w", "white", 0, 0)):
        layer = tmp / f"tt_{name}.png"
        move = f"translate {dx},{dy}"
        magick("-size", "256x256", "xc:none", "-fill", "none", "-stroke", color, "-strokewidth", "26",
               "-draw", f"{move} path 'M128 40 L128 172'",
               "-draw", f"{move} circle 100,182 100,152",
               "-draw", f"{move} path 'M128 40 C132 84 160 104 204 108'", str(layer))
        layers.append(layer)
    out = tmp / "tt.png"
    magick(str(layers[0]), str(layers[1]), "-composite", str(layers[2]), "-composite", str(out))
    return out


def rgb565a8(png: Path) -> bytes:
    raw = subprocess.run(["magick", str(png), "-resize", f"{SIZE}x{SIZE}", "-depth", "8", "rgba:-"],
                         check=True, capture_output=True).stdout
    assert len(raw) == SIZE * SIZE * 4
    color, alpha = bytearray(), bytearray()
    for i in range(0, len(raw), 4):
        r, g, b, a = raw[i:i + 4]
        c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        color += bytes((c & 0xFF, c >> 8))
        alpha.append(a)
    return bytes(color + alpha)


def c_array(name: str, data: bytes) -> str:
    rows = [", ".join(f"0x{b:02x}" for b in data[i:i + 16]) for i in range(0, len(data), 16)]
    return (f"#define ICON_{name.upper()}_W {SIZE}\n#define ICON_{name.upper()}_H {SIZE}\n"
            f"static const uint8_t icon_{name}_data[] = {{\n    " + ",\n    ".join(rows) + ",\n};\n")


def main() -> None:
    with tempfile.TemporaryDirectory() as t:
        tmp = Path(t)
        body = c_array("instagram", rgb565a8(instagram(tmp))) + "\n" + c_array("tiktok", rgb565a8(tiktok(tmp)))
    OUT.write_text("// Gerado por tools/make_social_icons.py: logos simplificados do Instagram e do TikTok,\n"
                   f"// {SIZE}x{SIZE} RGB565A8. Não edite à mão.\n#pragma once\n#include <stdint.h>\n\n" + body)
    print(f"{OUT} ({OUT.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
