"""Shared fixtures. The ROM is rebuilt once per run, so tests never see a stale build."""
import subprocess

import pytest

from harness import Game, Layout, PERFORMANCE_REPORT, ROOT, SCANLINES_PER_FRAME, load_symbols


@pytest.fixture(scope="session")
def rom():
    """Rebuilds the ROM; returns what a Game needs to run it."""
    subprocess.run(["make"], cwd=ROOT, check=True, capture_output=True)
    return Layout(), load_symbols()


@pytest.fixture(scope="session")
def k(rom):
    """Struct offsets and tuning values from the game's own headers."""
    return rom[0]


@pytest.fixture
def title(rom):
    """A freshly booted game, on the title screen."""
    game = Game(*rom)
    yield game
    game.stop()


@pytest.fixture
def game(title):
    """A game in progress, player in control, nothing else in the ocean."""
    title.start()
    title.calm_ocean()
    title.wait_active()
    return title


@pytest.fixture
def diving(game):
    """Like game, with the sub under water."""
    game.dive()
    return game


def pytest_terminal_summary(terminalreporter):
    if PERFORMANCE_REPORT:
        terminalreporter.section("worst frame cost")
        for name, scanlines in PERFORMANCE_REPORT:
            percent = 100 * scanlines / SCANLINES_PER_FRAME
            terminalreporter.line(f"{scanlines:6.1f} / {SCANLINES_PER_FRAME} scanlines ({percent:3.0f}%)  {name}")
