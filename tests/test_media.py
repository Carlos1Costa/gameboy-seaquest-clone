"""tools/make_media.py, which draws the README images from the real ROM."""
import subprocess
import sys

from PIL import Image

from harness import ROOT

SCREENS = ["title", "gameplay", "low_oxygen", "count", "game_over"]


def test_generates_every_image(rom, tmp_path):
    subprocess.run([sys.executable, ROOT / "tools/make_media.py", "--out", tmp_path, "--ticks", "300"],
                   check=True, capture_output=True)
    for name in SCREENS:
        assert Image.open(tmp_path / f"{name}.png").size == (480, 432)
    assert Image.open(tmp_path / "gameplay.gif").n_frames > 1
    assert (tmp_path / "banner.png").exists()
    assert (tmp_path / "sprites.png").exists()
