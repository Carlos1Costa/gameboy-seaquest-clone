"""The PNGs in res/ and tools/make_art.py, which must never destroy edited art."""
import subprocess
import sys

import pytest
from PIL import Image

from harness import ROOT

SCRIPT = ROOT / "tools" / "make_art.py"
ENTITIES = ["player_sub", "shark", "enemy_sub", "diver", "explosion"]


def make_art(out, *options):
    return subprocess.run([sys.executable, SCRIPT, "--out", out, *options],
                          capture_output=True, text=True)


@pytest.mark.parametrize("name", ENTITIES)
def test_entity_sprites_are_16x8(name):
    assert Image.open(ROOT / "res" / f"{name}.png").size == (16, 8)


def test_torpedo_sprite_is_8x8():
    assert Image.open(ROOT / "res" / "torpedo.png").size == (8, 8)


def test_hud_tileset_is_a_strip_of_8x8_tiles():
    width, height = Image.open(ROOT / "res" / "hud_tiles.png").size
    assert height == 8 and width % 8 == 0


@pytest.mark.parametrize("path", sorted((ROOT / "res").glob("*.png")), ids=lambda p: p.name)
def test_pngs_are_indexed_with_four_colors(path):
    image = Image.open(path)
    assert image.mode == "P"
    assert max(image.tobytes()) <= 3


def test_generates_a_valid_set_from_nothing(tmp_path):
    assert make_art(tmp_path).returncode == 0
    assert {p.name for p in tmp_path.glob("*.png")} == {p.name for p in (ROOT / "res").glob("*.png")}
    for path in tmp_path.glob("*.png"):
        assert Image.open(path).mode == "P"


def test_recreates_only_what_is_missing(tmp_path):
    make_art(tmp_path)
    (tmp_path / "shark.png").unlink()
    result = make_art(tmp_path)
    assert result.returncode == 0
    assert (tmp_path / "shark.png").exists()
    assert "shark.png" in result.stdout and "diver.png" not in result.stdout


def edit_a_pixel(path):
    image = Image.open(path)
    image.putpixel((0, 0), 3 - image.getpixel((0, 0)))
    image.save(path)
    return path.read_bytes()


def test_refuses_to_overwrite_edited_art(tmp_path):
    make_art(tmp_path)
    edited = edit_a_pixel(tmp_path / "shark.png")
    (tmp_path / "diver.png").unlink()
    result = make_art(tmp_path)
    assert result.returncode != 0
    assert "shark.png" in result.stderr
    assert (tmp_path / "shark.png").read_bytes() == edited
    assert not (tmp_path / "diver.png").exists(), "must write nothing when it refuses"


def test_force_replaces_edited_art(tmp_path):
    make_art(tmp_path)
    edited = edit_a_pixel(tmp_path / "shark.png")
    assert make_art(tmp_path, "--force").returncode == 0
    assert (tmp_path / "shark.png").read_bytes() != edited
