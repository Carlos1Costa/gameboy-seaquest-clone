#!/usr/bin/env python3
"""Shows where the frame time goes, per C line and per function, during random play.

Not a test: run it (make profile) when test_performance gets tight. Each C line
of game.c and collision.c gets an emulator hook, and every line is charged the
cycles until the next hook, so a line that calls into the library is charged
for the call. Costs are per frame, averaged over the run.

    python tests/profile_lines.py [--frames N] [--top N] [--seed N] [--wave N]
"""
import argparse
import random
import re
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from harness import CYCLES_PER_SCANLINE, Game, Layout, ROOT, load_symbols  # noqa: E402

MODULES = {"game": "_game_init", "collision": "_collisions_update"}   # module -> a global in it
COMMENT = re.compile(r"^\s*(?:[0-9A-F]{8}\s+)?\d+\s+;(src/\S+:\d+): (.*)$")
INSTRUCTION = re.compile(r"^\s+([0-9A-F]{8})\s+[0-9A-Fa-frs ]+\[\s*\d+\]")
LABEL = re.compile(r"^\s+([0-9A-F]{8})\s+\d+\s+(_\w+)::?\s*$")
AREA = re.compile(r"\.area\s+(\w+)")


def source_lines(module, anchor, symbols):
    """(address, 'file:line: text', function) of the first instruction of each C line."""
    text = (ROOT / "build" / f"{module}.lst").read_text().splitlines()
    area, pending, function, found, labels = None, None, None, [], {}
    for line in text:
        match = AREA.search(line)
        if match:
            area = match.group(1)
            continue
        if area != "_CODE":
            continue
        match = LABEL.match(line)
        if match:
            labels[match.group(2)] = int(match.group(1), 16)
            function = match.group(2)
            continue
        match = COMMENT.match(line)
        if match:
            pending = f"{match.group(1)}: {match.group(2).strip()}"
            continue
        match = INSTRUCTION.match(line)
        if match and pending:
            found.append((int(match.group(1), 16), pending, function))
            pending = None
    base = symbols[anchor] - labels[anchor]
    return [(base + offset, name, fn) for offset, name, fn in found]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--frames", type=int, default=2000)
    parser.add_argument("--top", type=int, default=20)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--wave", type=int)
    args = parser.parse_args()

    symbols = load_symbols()
    game = Game(Layout(), symbols)
    game.start(title_wait=args.seed * 13)
    game.force_wave = args.wave
    pb = game.pb
    current, started = [None], [0]
    cycles = defaultdict(int)
    function_of = {}

    def on_line(name):
        now = pb._cycles()
        if current[0] is not None:
            cycles[current[0]] += now - started[0]
        current[0], started[0] = name, now

    seen = set()
    for module, anchor in MODULES.items():
        for address, name, function in source_lines(module, anchor, symbols):
            if address not in seen:
                seen.add(address)
                function_of[name] = function
                pb.hook_register(0, address, on_line, name)
    pb.hook_register(0, symbols["_vsync"], lambda _: on_line(None), None)

    dice = random.Random(args.seed)
    held = set()
    for frame in range(args.frames):
        if frame % 15 == 0:
            for button in held:
                pb.button_release(button)
            held = set(dice.sample(["up", "down", "left", "right", "a", "start"], dice.randint(0, 3)))
            for button in held:
                pb.button_press(button)
        game.tick()
    game.stop()

    per_frame = {name: c / args.frames / CYCLES_PER_SCANLINE for name, c in cycles.items()}
    functions = defaultdict(float)
    for name, cost in per_frame.items():
        functions[function_of[name]] += cost
    print(f"scanlines per frame, average over {args.frames} frames (budget 154)\n")
    print("By function:")
    for function, cost in sorted(functions.items(), key=lambda item: -item[1])[:args.top]:
        print(f"  {cost:6.2f}  {function}")
    print("\nBy line:")
    for name, cost in sorted(per_frame.items(), key=lambda item: -item[1])[:args.top]:
        print(f"  {cost:6.2f}  {name}")


if __name__ == "__main__":
    main()
