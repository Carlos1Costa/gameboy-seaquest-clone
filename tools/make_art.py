#!/usr/bin/env python3
"""Generates the placeholder pixel art in res/.

The art is written here as text, one character per pixel: '.' is palette index 0
(transparent in sprites), and '1', '2', '3' are light gray, dark gray and black.
It only exists until the final art is drawn, so it never runs during a build and
refuses to overwrite a PNG that differs from what it would generate.

    python tools/make_art.py [--out DIR] [--force]
"""
import argparse
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
PALETTE = [255, 255, 255, 170, 170, 170, 85, 85, 85, 0, 0, 0]

# Each sprite is a list of frames, facing right, drawn left to right in its PNG.
PLAYER_SUB = [
    [
        "........2222....",
        ".......22.222...",
        "...33333333333..",
        "3..1111111111133",
        ".3.1111111111113",
        "3..222222222223.",
        "...3333333333...",
        "................",
    ],
    [
        "........2222....",
        ".......22.222...",
        "...33333333333..",
        ".3.1111111111113",
        "3..1111111111113",
        ".3.222222222223.",
        "...3333333333...",
        "................",
    ],
]

TORPEDO = [[
    "........",
    "........",
    "........",
    "2223333.",
    "2223333.",
    "........",
    "........",
    "........",
]]

SHARK = [
    [
        "................",
        "........22......",
        "2......2222....2",
        "22...2222222222.",
        ".222222222232...",
        "22...111111111.2",
        "......11111.....",
        "................",
    ],
    [
        "................",
        "........22......",
        ".......2222.....",
        "2....2222222222.",
        ".222222222222322",
        "2....1111111111.",
        "22....11111.....",
        "................",
    ],
]

ENEMY_SUB = [
    [
        "................",
        "........333.....",
        "...222222222222.",
        "3.32222222222222",
        ".332222222222222",
        "...222222222222.",
        "................",
        "................",
    ],
    [
        "................",
        "........333.....",
        "...222222222222.",
        ".332222222222222",
        "3.32222222222222",
        "...222222222222.",
        "................",
        "................",
    ],
]

DIVER = [
    [
        ".........1......",
        "...........33...",
        "...........333..",
        ".........21122..",
        ".......21112....",
        "....2211122.....",
        ".2222...........",
        "22..............",
    ],
    [
        "...........1....",
        ".........1.33...",
        "...........333..",
        ".........21122..",
        ".......21112....",
        "....2211122.....",
        "..222...........",
        ".2.2............",
    ],
]

EXPLOSION = [
    [
        "................",
        "................",
        ".......33.......",
        "......3223......",
        "......3223......",
        ".......33.......",
        "................",
        "................",
    ],
    [
        ".......3........",
        "...3...3...3....",
        "....3.232.3.....",
        ".....32123......",
        "..3332111233333.",
        ".....32123......",
        "....3.232.3.....",
        "...3...3...3....",
    ],
    [
        "..3....3...3..3.",
        "...3..232.3..3..",
        ".3..32123223.3..",
        "..3221111112233.",
        "3322111111122...",
        "..3.2211211233..",
        ".3..3.323..3..3.",
        "...3...3....3...",
    ],
]

# 3x5 font, doubled horizontally by glyph_tile.
GLYPHS = {
    "0": ["333", "3.3", "3.3", "3.3", "333"], "1": [".3.", "33.", ".3.", ".3.", "333"],
    "2": ["333", "..3", "333", "3..", "333"], "3": ["333", "..3", ".33", "..3", "333"],
    "4": ["3.3", "3.3", "333", "..3", "..3"], "5": ["333", "3..", "333", "..3", "333"],
    "6": ["333", "3..", "333", "3.3", "333"], "7": ["333", "..3", "..3", ".3.", ".3."],
    "8": ["333", "3.3", "333", "3.3", "333"], "9": ["333", "3.3", "333", "..3", "333"],
    "A": [".3.", "3.3", "333", "3.3", "3.3"], "E": ["333", "3..", "33.", "3..", "333"],
    "G": ["333", "3..", "3.3", "3.3", "333"], "M": ["3.3", "333", "333", "3.3", "3.3"],
    "O": ["333", "3.3", "3.3", "3.3", "333"], "P": ["333", "3.3", "333", "3..", "3.."],
    "Q": ["333", "3.3", "3.3", "333", "..3"], "R": ["33.", "3.3", "33.", "3.3", "3.3"],
    "S": ["333", "3..", "333", "..3", "333"], "T": ["333", ".3.", ".3.", ".3.", ".3."],
    "U": ["3.3", "3.3", "3.3", "3.3", "333"], "V": ["3.3", "3.3", "3.3", "3.3", ".3."],
    "N": ["333", "3.3", "3.3", "3.3", "3.3"], "X": ["3.3", "3.3", ".3.", "3.3", "3.3"],
    "Y": ["3.3", "3.3", ".3.", ".3.", ".3."],
}

# The order must match BackgroundTile and letter_glyphs in src/game.c. Adding a frame
# to a sprite also means updating the _Static_asserts there.
BACKGROUND_TILES = [
    ["........"] * 8,                                                                 # water
    ["11111111"] * 8,                                                                 # HUD band
    ["11111111", "11111111", "11111111", "11111111",
     "22111122", "..2222..", "........", "........"],                                 # surface
    ["........", "........", "........", "...2....",
     "..222..2", "22222222", "22222222", "33333333"],                                 # seabed
    ["33333333", "31111113", "31111113", "31111113",
     "31111113", "31111113", "31111113", "33333333"],                                 # O2 empty
    ["33333333", "32222223", "32222223", "32222223",
     "32222223", "32222223", "32222223", "33333333"],                                 # O2 full
    ["11133111", "11133111", "11333311", "13133131",
     "11133111", "11311311", "11311311", "11111111"],                                 # diver icon
    ["11133111", "11322311", "33333333", "32222223",
     "33333333", "11111111", "11111111", "11111111"],                                 # life icon
]
GLYPH_ORDER = "0123456789AEGMNOPQRSTUVXY"


def glyph_tile(glyph):
    """An 8x8 HUD tile: the glyph, doubled in width, over the HUD band color."""
    tile = [list("11111111") for _ in range(8)]
    for y, row in enumerate(glyph):
        for x, pixel in enumerate(row):
            if pixel != ".":
                tile[y + 1][x * 2 + 1] = pixel
                tile[y + 1][x * 2 + 2] = pixel
    return ["".join(row) for row in tile]


def hud_strip():
    tiles = BACKGROUND_TILES + [glyph_tile(GLYPHS[c]) for c in GLYPH_ORDER]
    return ["".join(tile[y] for tile in tiles) for y in range(8)]


def frame_strip(frames):
    return ["".join(frame[y] for frame in frames) for y in range(len(frames[0]))]


ART = {
    "player_sub.png": frame_strip(PLAYER_SUB),
    "torpedo.png": frame_strip(TORPEDO),
    "shark.png": frame_strip(SHARK),
    "enemy_sub.png": frame_strip(ENEMY_SUB),
    "diver.png": frame_strip(DIVER),
    "explosion.png": frame_strip(EXPLOSION),
    "hud_tiles.png": hud_strip(),
}


def to_image(rows):
    assert len({len(row) for row in rows}) == 1, "rows of different widths"
    image = Image.new("P", (len(rows[0]), len(rows)))
    image.putpalette(PALETTE)
    image.putdata([0 if pixel == "." else int(pixel) for row in rows for pixel in row])
    return image


def same_art(path, image):
    existing = Image.open(path)
    return (existing.mode == "P" and existing.size == image.size
            and existing.tobytes() == image.tobytes()
            and existing.getpalette()[:12] == PALETTE)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, default=ROOT / "res")
    parser.add_argument("--force", action="store_true", help="overwrite PNGs that differ")
    args = parser.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    images = {name: to_image(rows) for name, rows in ART.items()}
    different = [name for name, image in images.items()
                 if (args.out / name).exists() and not same_art(args.out / name, image)]
    if different and not args.force:
        print("Not overwriting art that differs from the placeholders:", file=sys.stderr)
        for name in different:
            print(f"  {args.out / name}", file=sys.stderr)
        print("Nothing was written. Use --force (make art FORCE=1) to replace them.", file=sys.stderr)
        return 1
    for name, image in images.items():
        if not (args.out / name).exists() or name in different:
            image.save(args.out / name)
            print(f"wrote {args.out / name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
