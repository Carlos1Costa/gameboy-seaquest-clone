# gameboy-seaquest-clone

A clone of *Seaquest* (Activision, Atari 2600, 1983) for the original Game Boy (DMG), written in C with GBDK-2020.

The design spec lives in [GUIA_SEAQUEST_GB.md](GUIA_SEAQUEST_GB.md).

## Requirements

- GBDK-2020 unpacked in `./gbdk/` (or set `GBDK_HOME` in the `Makefile`)
- `make`
- mgba-qt (or any Game Boy emulator)

## Build and run

    $ make          # builds Seaquest.gb
    $ make run      # builds and opens it in mgba-qt
    $ make clean

Art lives in `res/*.png` and is converted by `png2asset` (bundled with GBDK) into `build/assets/` during the build.

## Tests

The test suite runs the ROM headless in the PyBoy emulator. It needs `python3`.

    $ make test-env # once: creates the Python environment in .env/
    $ make test     # rebuilds and runs everything

See [tests/README.md](tests/README.md) for details and how to write tests.

## Controls (mGBA default keyboard mapping)

| Game Boy | Keyboard |
|---|---|
| D-Pad | Arrow keys |
| A (fire) | X |
| B | Z |
| Start | Enter |
| Select | Backspace |

## Layout

    src/    game code (main.c, game.c/.h, collision.c/.h, sound.c/.h)
    res/    source PNGs
    tests/  test suite (PyBoy + pytest)
