# Test suite

The tests run the real ROM headless in [PyBoy](https://github.com/Baekalfen/PyBoy), a
scriptable Game Boy emulator. They press buttons, read the game's memory and the
screen, and measure CPU time. No test needs a window or a graphics stack.

## Running

    make test-env    # once: creates the Python environment in .env/
    make test        # rebuilds the ROM and runs everything (about 15 s)
    make test-fast   # skips the long random-play runs
    make profile     # not a test: shows where the frame time goes

`make test-env` needs `python3` with the `venv` module (on Ubuntu:
`sudo apt install python3-venv`). `.env/` is not versioned; `requirements.txt` is.

To run part of the suite, or see why a test failed:

    .env/bin/python -m pytest tests/test_oxygen.py
    .env/bin/python -m pytest -k patrol
    .env/bin/python -m pytest -x --tb=long

`STRESS_FRAMES=20000 make test` makes the random-play runs longer.

## What is covered

| File | Covers |
|---|---|
| `test_screens.py` | Title, game over, START flow, random seed (F1.4) |
| `test_player.py` | Movement, screen limits, mirroring, player torpedo (EPICs A, B) |
| `test_lanes.py` | Spawner, caps, sharks, enemy subs, divers, collisions (EPIC C) |
| `test_oxygen.py` | Drain, refill, alarm, surfacing rules, deaths (EPIC D) |
| `test_scoring.py` | Points, extra lives, on-screen count, difficulty (EPICs E, F1.2) |
| `test_patrol.py` | Patrol sub (F1.1) |
| `test_sound.py` | Which effect each event requests (F1.3) |
| `test_performance.py` | The 60 fps budget and the sprite limit (GUIA section 6.1) |
| `test_art.py` | PNG format in `res/`, and the safety of `tools/make_art.py` |

The other files are infrastructure:

| File | Role |
|---|---|
| `harness.py` | `Game`: runs the ROM, reads and writes game state, measures frames |
| `conftest.py` | pytest fixtures; rebuilds the ROM once per run |
| `layout_probe.c` | Exposes struct offsets and tuning values to the tests |
| `profile_lines.py` | Per-function and per-C-line profiler (`make profile`) |

## Writing a test

Pick the fixture for the starting point you need:

| Fixture | Starts with |
|---|---|
| `title` | Booted, on the title screen |
| `game` | Game running, player in control at the surface, ocean empty and quiet |
| `diving` | Same, with the sub under water |
| `k` | No game: just the tuning values |

`game` and `diving` hold the spawner and the patrol, so a test sees one rule at a
time. Set up the scene with the `Game` helpers, then let time pass:

```python
def test_touching_an_enemy_kills_the_player(diving):
    game = diving
    y = game.lane(1).base_y
    game.set_player(80, y)
    game.place_group(1, 84)
    game.tick(2)
    assert game.state == game.k.DYING
```

Rules that keep the suite useful:

- **Don't hard-code tuning values.** Read them from `game.k` (`k.OXYGEN_MAX`, not `96`).
  Changing a value in `game.h` should only fail a test if the behaviour it describes
  really changed. Expose a new value by adding a line to `layout_probe.c`.
- **Force the case you want to check; don't wait for it to happen.** A cap checked only
  by watching random play passes whenever the game never reaches the cap.
- **Avoid depending on the exact frame** when reading something that changes by itself.
  Prefer `game.tick_until(condition, limit)` over a fixed number of ticks.
- **Sprites appear one frame after they are computed** (the loop renders after `vsync()`,
  and OAM is copied at the next VBlank). Tick a frame or two before reading `sprites()`.
- **Check that a new test can fail.** Break the code on purpose once and watch it go red.

## How it works

**Finding the game state.** `GameState` is a `static` in `main.c`, so it has no symbol.
The harness hooks `game_init` and reads its argument from register DE, where SDCC passes
the first 16-bit parameter.

**Struct offsets and constants.** `layout_probe.c` is compiled with the game's own
compiler and headers, and the harness parses the values from the generated assembly.
Reordering a struct or changing a constant needs no change in the tests.

**Measuring time.** One iteration of the loop runs from the entry of `game_render` to
the next call to `vsync`. `Game.track_frame_costs()` hooks both and reads PyBoy's cycle
counter. A frame has 154 scanlines of 456 cycles each. The counter is an internal
PyBoy API (`_cycles`), which is why `requirements.txt` pins the exact version.

## Reading the performance report

Every run ends with the worst frame of each performance test:

     127.1 / 154 scanlines ( 83%)  scene 3-3-1-1
     143.6 / 154 scanlines ( 93%)  random play, seed 2, wave natural

Above 154 the game drops to 30 fps and the test fails. The goal is to stay at or below
90% or so, leaving room for new features. When it gets tight, run `make profile`, and see
the rules in section 6.1 of the guide.

Frames that show or clear a banner are left out of the budget check. They redraw the
whole HUD at once and can pass 154 scanlines, but that happens once, on a still screen.
