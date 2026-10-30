#!/usr/bin/env python3
"""Regenerates the README images in docs/images from the real ROM.

A small bot plays the game headless, and the screenshots and the GIF come from
that run. Rare moments (the count, low oxygen, game over) are set up in the
game's memory, but every picture is drawn by the game itself. Run it after the
art or the HUD change:

    python tools/make_media.py [--out DIR] [--ticks N]
"""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tests"))
sys.path.insert(0, str(ROOT / "tools"))

from PIL import Image  # noqa: E402

from harness import Game, Layout, load_symbols  # noqa: E402
from make_art import GLYPHS  # noqa: E402

# The original Game Boy's green LCD, lightest to darkest.
DMG_GREEN = [(155, 188, 15), (139, 172, 15), (48, 98, 48), (15, 56, 15)]
SCALE = 3
GIF_TICKS = 420
GIF_FRAME_TICKS = 3


def to_green(image):
    """Maps the emulator's four grays to the DMG palette, as a paletted image."""
    gray = image.convert("L")
    levels = sorted(set(gray.tobytes()), reverse=True)
    index = {level: i for i, level in enumerate(levels)}
    result = Image.new("P", gray.size)
    result.putpalette([channel for color in DMG_GREEN for channel in color])
    result.putdata([index[value] for value in gray.tobytes()])
    return result


def enlarge(image, scale=SCALE):
    return image.resize((image.width * scale, image.height * scale), Image.NEAREST)


class Bot:
    """Plays well enough for a nice clip: hunts divers, shoots, comes up to breathe."""

    def __init__(self, game):
        self.game = game
        self.held = set()

    def press_only(self, wanted):
        for button in self.held - wanted:
            self.game.pb.button_release(button)
        for button in wanted - self.held:
            self.game.pb.button_press(button)
        self.held = wanted

    def threats(self):
        """(distance, signed dx) of the enemies and torpedoes near the sub's height."""
        game = self.game
        x, y = game.player_x, game.player_y
        found = []
        for lane in game.lanes():
            if abs(lane.y - y) < 12:
                found += [enemy_x - x for enemy_x in lane.member_xs if -16 < enemy_x < 160]
                if lane.torpedo_active:
                    found.append(lane.torpedo_x - x)
        return found

    def act(self):
        game, k = self.game, self.game.k
        if game.state != k.ACTIVE:
            self.press_only(set())
            return
        x, y = game.player_x, game.player_y
        wanted = set()

        needs_air = game.oxygen < 24 and game.divers > 0
        divers = [lane for lane in game.lanes() if lane.diver_active and 0 <= lane.diver_x <= 144]
        if game.divers == k.MAX_DIVERS_ON_BOARD or needs_air:
            target_y, target_x = k.PLAYER_MIN_Y, x
        elif divers:
            lane = min(divers, key=lambda candidate: abs(candidate.diver_x - x))
            target_y, target_x = lane.base_y, lane.diver_x
        else:
            target_y, target_x = game.lane(1).base_y + 11, 72     # between lanes

        threats = self.threats()
        close = [dx for dx in threats if abs(dx) < 30]
        if close:
            # Too close to shoot: leave the lane, away from the surface if it can.
            target_y = y + 20 if y < k.PLAYER_MAX_Y - 20 else y - 20
            target_x = x
        elif threats:
            # Turn to face the nearest one and fire.
            nearest = min(threats, key=abs)
            wanted.add("left" if nearest < 0 else "right")
            wanted.add("a")
            target_x = x

        if y < target_y:
            wanted.add("down")
        elif y > target_y:
            wanted.add("up")
        if not wanted & {"left", "right"}:
            if x < target_x - 2:
                wanted.add("right")
            elif x > target_x + 2:
                wanted.add("left")
        self.press_only(wanted)


def excitement(snapshot):
    return snapshot["enemies"] + 3 * snapshot["divers"]


def play(game, ticks):
    """Runs the bot and returns a snapshot of every frame."""
    bot = Bot(game)
    frames = []
    for _ in range(ticks):
        if game.game_over:
            game.tap("start")
            game.tap("start")
        bot.act()
        game.tick()
        lanes = game.lanes()
        frames.append({
            "image": game.pb.screen.image.copy(),
            "score": game.score,
            "enemies": sum(len(lane.member_xs) for lane in lanes),
            "divers": game.divers,
            "dying": game.state == game.k.DYING,
        })
    bot.press_only(set())
    return frames


def best_clip(frames, length):
    """The busiest stretch without a death, ending with more points than it began."""
    best, best_score = 0, -1
    for start in range(0, max(1, len(frames) - length), 30):
        clip = frames[start:start + length]
        if any(frame["dying"] for frame in clip):
            continue
        score = sum(excitement(frame) for frame in clip) + (clip[-1]["score"] - clip[0]["score"]) / 2
        if score > best_score:
            best, best_score = start, score
    return frames[best:best + length]


def save_gif(frames, path):
    images = [enlarge(to_green(frame["image"]), 2) for frame in frames[::GIF_FRAME_TICKS]]
    images[0].save(path, save_all=True, append_images=images[1:], loop=0,
                   duration=1000 * GIF_FRAME_TICKS // 60, optimize=True)


def staged_shots(layout, symbols, out):
    game = Game(layout, symbols)
    enlarge(to_green(game.pb.screen.image)).save(out / "title.png")
    game.start(title_wait=20)
    game.calm_ocean()
    game.wait_active()
    game.dive()
    k = game.k

    # Low oxygen, with the ocean busy: the bar blinks and the alarm beeps.
    game.set_player(64, game.lane(1).base_y - 11)
    game.place_group(0, 110, enemy_type=k.SHARK, alive_mask=0b11)
    game.place_group(2, 20, alive_mask=0b1)
    game.place_diver(3, 100, moving_left=True)
    game.divers = 4
    game.score = 4720
    game.oxygen = k.OXYGEN_LOW
    game.tick(4)    # sprites and the HUD reach the screen a frame or two later
    game.oxygen = k.OXYGEN_LOW
    game.tick_until(lambda: 0 < game.hud_oxygen_tiles() <= 2, 20)   # the blink's lit phase
    game.tick(1)    # VRAM reaches the screen on the next frame
    enlarge(to_green(game.pb.screen.image)).save(out / "low_oxygen.png")

    # The on-screen count of a full load.
    game.calm_ocean()
    game.oxygen = k.OXYGEN_MAX * 2 // 3
    game.divers = k.MAX_DIVERS_ON_BOARD
    game.rise()
    game.tick_until(lambda: game.divers == 3, 60)
    game.tick(2)
    enlarge(to_green(game.pb.screen.image)).save(out / "count.png")
    game.tick_until(lambda: game.state == k.ACTIVE, 400)

    # Game over.
    game.reserves = 0
    game.divers = 0
    game.score = 18340
    game.dive()
    game.rise()
    game.tick_until(lambda: game.game_over, 200)
    game.tick(3)
    enlarge(to_green(game.pb.screen.image)).save(out / "game_over.png")
    game.stop()


def sprite(name, frame=0, width=16):
    """One frame of a sprite sheet, on transparent: index 0 becomes alpha 0."""
    sheet = Image.open(ROOT / "res" / f"{name}.png")
    cell = sheet.crop((frame * width, 0, (frame + 1) * width, 8)).convert("P")
    pixels = cell.tobytes()
    result = Image.new("RGBA", cell.size)
    result.putdata([(0, 0, 0, 0) if p == 0 else DMG_GREEN[p] + (255,) for p in pixels])
    return result


def sprite_sheet(out):
    """Every animation frame, enlarged, on the LCD's lightest green."""
    names = [("player_sub", 2), ("enemy_sub", 2), ("shark", 2), ("diver", 2), ("explosion", 3)]
    cell, gap, scale = 16, 4, 8
    width = sum(frames * (cell + gap) for _, frames in names) + gap
    sheet = Image.new("RGBA", (width, 8 + 2 * gap), DMG_GREEN[0] + (255,))
    x = gap
    for name, frames in names:
        for frame in range(frames):
            layer = sprite(name, frame)
            sheet.alpha_composite(layer, (x, gap))
            x += cell + gap
    enlarge(sheet.convert("RGB"), scale).save(out / "sprites.png")


def banner(out):
    """The README header: the title in the game's font over its own sprites."""
    width, height = 240, 64
    scene = Image.new("RGBA", (width, height), DMG_GREEN[0] + (255,))
    water_top = 10
    for x in range(width):
        scene.putpixel((x, water_top + (1 if (x // 6) % 2 else 0)), DMG_GREEN[2] + (255,))
    for y in range(height - 5, height):
        for x in range(width):
            scene.putpixel((x, y), DMG_GREEN[1] + (255,))

    text = "SEAQUEST"
    scale, spacing = 3, 2
    glyph_width = 3 * scale + spacing
    x = (width - len(text) * glyph_width + spacing) // 2
    for letter in text:
        for row, line in enumerate(GLYPHS[letter]):
            for column, pixel in enumerate(line):
                if pixel != ".":
                    for dy in range(scale):
                        for dx in range(scale):
                            scene.putpixel((x + column * scale + dx, 16 + row * scale + dy),
                                           DMG_GREEN[3] + (255,))
        x += glyph_width

    placed = [("player_sub", 0, 112, 2), ("shark", 0, 20, 44), ("shark", 1, 44, 40),
              ("enemy_sub", 0, 196, 42), ("diver", 0, 150, 46)]
    for name, frame, x, y in placed:
        scene.alpha_composite(sprite(name, frame), (x, y))
    enlarge(scene.convert("RGB"), 4).save(out / "banner.png")


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, default=ROOT / "docs" / "images")
    parser.add_argument("--ticks", type=int, default=8000, help="frames the bot plays")
    parser.add_argument("--seed-wait", type=int, default=90, help="frames on the title screen")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    layout, symbols = Layout(), load_symbols()
    game = Game(layout, symbols)
    game.start(title_wait=args.seed_wait)
    frames = play(game, args.ticks)
    game.stop()

    clip = best_clip(frames, min(GIF_TICKS, len(frames)))
    save_gif(clip, args.out / "gameplay.gif")
    busiest = max(clip, key=excitement)
    enlarge(to_green(busiest["image"])).save(args.out / "gameplay.png")
    staged_shots(layout, symbols, args.out)
    sprite_sheet(args.out)
    banner(args.out)
    print(f"bot's best score: {max(frame['score'] for frame in frames)}")


if __name__ == "__main__":
    main()
