"""Drives the real ROM in PyBoy, headless, and exposes the game state to the tests.

Everything that depends on how the game is built is derived, not hard-coded:
struct offsets and tuning values come from layout_probe.c (compiled with the
game's headers), and the GameState address is read from the argument passed to
game_init. Changing a tuning value in game.h should not break a test unless the
behaviour it asserts really changed.
"""
import re
import subprocess
from dataclasses import dataclass
from pathlib import Path

from pyboy import PyBoy

ROOT = Path(__file__).resolve().parent.parent
ROM = ROOT / "Seaquest.gb"
PROBE_SOURCE = ROOT / "tests" / "layout_probe.c"
PROBE_OUTPUT = ROOT / "build" / "tests" / "layout_probe.asm"

CYCLES_PER_SCANLINE = 456
SCANLINES_PER_FRAME = 154

SCREEN_SPRITE_OFFSET_X = 8   # hardware sprite origin vs. the top-left of the screen
SCREEN_SPRITE_OFFSET_Y = 16
S_FLIPX = 0x20

# VRAM tile numbers, from SpriteTile and BackgroundTile in game.c.
SPRITE_TILE_PLAYER = 0
SPRITE_TILE_TORPEDO = 2
SPRITE_TILE_ENEMY_SUB = 5
SPRITE_TILE_EXPLOSION = 9
BG_TILE_WATER = 0
BG_TILE_HUD = 1
BG_TILE_O2_FULL = 5
BG_TILE_DIVER = 6
BG_TILE_LIFE = 7
BG_TILE_DIGIT_0 = 8
BG_TILE_LETTER_FIRST = 18

BG_MAP = 0x9800
OAM = 0xFE00
NR52 = 0xFF26
BANNER_TEXT_ROW = 8


# Worst frame cost of each performance test, printed at the end of the run (see conftest.py).
PERFORMANCE_REPORT = []


class Layout:
    """Struct offsets and constants from layout_probe.c, as attributes."""

    def __init__(self):
        PROBE_OUTPUT.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(
            [ROOT / "gbdk/bin/lcc", "-Isrc", "-S", "-o", PROBE_OUTPUT, PROBE_SOURCE],
            cwd=ROOT, check=True)
        source = PROBE_OUTPUT.read_text()
        for name, value in re.findall(r"^_(\w+):\n\s+\.dw #0x([0-9a-f]+)", source, re.M):
            number = int(value, 16)
            setattr(self, name.rstrip("_"), number - 65536 if number >= 32768 else number)


def load_symbols():
    """Global symbols of the linked ROM, from the .noi file."""
    symbols = {}
    for line in (ROOT / "Seaquest.noi").read_text().splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[0] == "DEF":
            symbols[parts[1]] = int(parts[2], 16)
    return symbols


def letter_glyphs():
    """The letters with a tile, in tile order, as declared in game.c."""
    return re.search(r'letter_glyphs\[\] = "(\w+)"', (ROOT / "src/game.c").read_text()).group(1)


def text_tiles(text):
    """The background tiles game.c draws for text."""
    letters = letter_glyphs()
    return [BG_TILE_HUD if c == " " else BG_TILE_LETTER_FIRST + letters.index(c) for c in text]


@dataclass
class Lane:
    alive_mask: int
    member_xs: list          # screen x of the living members
    y: int
    base_y: int
    enemy_type: int
    moving_left: int
    speed: int
    diver_active: int
    diver_x: int
    torpedo_active: int
    torpedo_x: int


@dataclass
class Sprite:
    x: int                   # screen coordinates
    y: int
    tile: int
    flags: int


@dataclass
class FrameCost:
    scanlines: float
    banner_changed: bool     # redraws the banner and the whole HUD at once (a known spike)


class Game:
    """One running ROM. Boots to the title screen."""

    def __init__(self, layout, symbols, sound=False):
        self.k = layout
        self.symbols = symbols
        self.pb = PyBoy(str(ROM), window="null", sound_emulated=sound)
        self.pb.set_emulation_speed(0)
        self.m = self.pb.memory
        self.base = None
        self.hold_spawns = False   # keep lanes from spawning
        self.hold_patrol = False   # keep the patrol away
        self.force_wave = None     # rewrite the wave before every frame
        self.frame_costs = []
        self._cost_start = None
        self._last_banner = None
        self.pb.hook_register(0, symbols["_game_init"], self._on_game_init, None)
        for _ in range(1000):
            if self.base is not None:
                break
            self.pb.tick(1)
        assert self.base is not None, "game_init never ran"
        self.pb.hook_deregister(0, symbols["_game_init"])
        self.pb.tick(10)   # past the first game_render, which draws the title banner

    def _on_game_init(self, _):
        # SDCC passes the first 16-bit argument in DE.
        self.base = (self.pb.register_file.D << 8) | self.pb.register_file.E

    def stop(self):
        self.pb.stop(save=False)

    # --- time and input

    def tick(self, frames=1):
        for _ in range(frames):
            if self.hold_spawns:
                for i in range(self.k.LANE_COUNT):
                    self.m[self._lane_base(i) + self.k.lane_spawn_timer] = 250
            if self.hold_patrol:
                self.m[self.base + self.k.patrol + self.k.patrol_active] = 0
                self.m[self.base + self.k.patrol + self.k.patrol_timer] = 250
            if self.force_wave is not None:
                self.m[self.base + self.k.wave] = self.force_wave
            self.pb.tick(1)

    def tick_until(self, condition, limit=2000):
        """Ticks until condition() holds; returns whether it did."""
        for _ in range(limit):
            if condition():
                return True
            self.tick()
        return condition()

    def hold(self, button, frames):
        self.pb.button_press(button)
        self.tick(frames)
        self.pb.button_release(button)
        self.tick(1)

    def tap(self, button):
        self.hold(button, 2)

    def start(self, title_wait=0):
        self.tick(title_wait)
        self.tap("start")

    def calm_ocean(self):
        """Empties the ocean and keeps it empty, to test one rule at a time."""
        self.hold_spawns = True
        self.hold_patrol = True
        for i in range(self.k.LANE_COUNT):
            lane = self._lane_base(i)
            self.m[lane + self.k.lane_alive_mask] = 0
            self.m[lane + self.k.lane_diver_active] = 0
            self.m[lane + self.k.lane_torpedo + self.k.torpedo_active] = 0

    def wait_active(self):
        assert self.tick_until(lambda: self.state == self.k.ACTIVE), f"state={self.state}"

    def dive(self):
        self.pb.button_press("down")
        assert self.tick_until(lambda: self.submerged, 200)
        self.pb.button_release("down")
        self.tick(1)

    def rise(self):
        """Holds UP until the sub surfaces (the player stops being ACTIVE)."""
        self.pb.button_press("up")
        assert self.tick_until(lambda: self.state != self.k.ACTIVE, 300)
        self.pb.button_release("up")

    # --- memory access

    def _u8(self, offset):
        return self.m[self.base + offset]

    def _set_u8(self, offset, value):
        self.m[self.base + offset] = value

    def _i16_at(self, address):
        value = self.m[address] | (self.m[address + 1] << 8)
        return value - 65536 if value >= 32768 else value

    def _set_i16_at(self, address, value):
        value &= 0xFFFF
        self.m[address] = value & 0xFF
        self.m[address + 1] = value >> 8

    def _pixels(self, address):
        return self._i16_at(address) // self.k.FIXED_ONE

    def _lane_base(self, index):
        return self.base + self.k.lanes + index * self.k.lane_size

    # --- game state

    state = property(lambda self: self._u8(self.k.player_state))
    submerged = property(lambda self: self._u8(self.k.player_submerged))
    player_x = property(lambda self: self._pixels(self.base + self.k.player_x))
    player_y = property(lambda self: self._pixels(self.base + self.k.player_y))
    facing_left = property(lambda self: self._u8(self.k.player_facing_left))
    game_over = property(lambda self: self._u8(self.k.game_over))
    banner = property(lambda self: self._u8(self.k.banner))
    frame = property(lambda self: self._u8(self.k.frame))
    drain_timer = property(lambda self: self._u8(self.k.oxygen_drain_timer))
    patrol_active = property(lambda self: self._u8(self.k.patrol + self.k.patrol_active))
    patrol_x = property(lambda self: self._i16_at(self.base + self.k.patrol + self.k.patrol_x))

    @property
    def oxygen(self):
        return self._u8(self.k.oxygen)

    @oxygen.setter
    def oxygen(self, value):
        self._set_u8(self.k.oxygen, value)

    @property
    def divers(self):
        return self._u8(self.k.divers)

    @divers.setter
    def divers(self, value):
        self._set_u8(self.k.divers, value)

    @property
    def reserves(self):
        return self._u8(self.k.reserves)

    @reserves.setter
    def reserves(self, value):
        self._set_u8(self.k.reserves, value)

    @property
    def wave(self):
        return self._u8(self.k.wave)

    @wave.setter
    def wave(self, value):
        self._set_u8(self.k.wave, value)

    @property
    def score(self):
        return int("".join(str(self._u8(self.k.score_digits + i))
                           for i in range(self.k.SCORE_DIGITS)))

    @score.setter
    def score(self, value):
        for i, digit in enumerate(f"{value:0{self.k.SCORE_DIGITS}d}"):
            self._set_u8(self.k.score_digits + i, int(digit))
        self._set_u8(self.k.score_changed, 1)

    def set_player(self, x, y):
        self._set_i16_at(self.base + self.k.player_x, x * self.k.FIXED_ONE)
        self._set_i16_at(self.base + self.k.player_y, y * self.k.FIXED_ONE)

    def player_torpedo(self):
        """(active, x, y) of the player's torpedo."""
        t = self.base + self.k.torpedo
        return (self.m[t + self.k.torpedo_active], self._pixels(t + self.k.torpedo_x),
                self.m[t + self.k.torpedo_y])

    def place_player_torpedo(self, x, y, moving_left=False):
        t = self.base + self.k.torpedo
        self._set_i16_at(t + self.k.torpedo_x, x * self.k.FIXED_ONE)
        self.m[t + self.k.torpedo_y] = y
        self.m[t + self.k.torpedo_active] = 1
        self.m[t + self.k.torpedo_moving_left] = int(moving_left)

    def lane(self, index):
        k, b = self.k, self._lane_base(index)
        alive = self.m[b + k.lane_alive_mask]
        step = self.m[b + k.lane_member_step]
        step = step - 256 if step >= 128 else step
        leader = self._i16_at(b + k.lane_leader_x)
        t = b + k.lane_torpedo
        return Lane(
            alive_mask=alive,
            member_xs=[leader + i * step for i in range(k.LANE_MAX_ENTITIES) if alive & (1 << i)],
            y=self.m[b + k.lane_enemy_y],
            base_y=self.m[b + k.lane_base_y],
            enemy_type=self.m[b + k.lane_enemy_type],
            moving_left=self.m[b + k.lane_moving_left],
            speed=self._i16_at(b + k.lane_speed),
            diver_active=self.m[b + k.lane_diver_active],
            diver_x=self._i16_at(b + k.lane_diver_x),
            torpedo_active=self.m[t + k.torpedo_active],
            torpedo_x=self._pixels(t + k.torpedo_x))

    def lanes(self):
        return [self.lane(i) for i in range(self.k.LANE_COUNT)]

    def place_group(self, index, x, enemy_type=None, moving_left=False, alive_mask=1,
                    speed=0, fire_timer=250):
        """Puts a group in a lane; speed 0 keeps it still, which most tests want."""
        k, b = self.k, self._lane_base(index)
        step = k.GROUP_SPACING if moving_left else -k.GROUP_SPACING
        self.m[b + k.lane_alive_mask] = alive_mask
        self.m[b + k.lane_enemy_type] = k.SUB if enemy_type is None else enemy_type
        self.m[b + k.lane_moving_left] = int(moving_left)
        self.m[b + k.lane_member_step] = step & 0xFF
        self.m[b + k.lane_fire_timer] = fire_timer
        self._set_i16_at(b + k.lane_speed, speed)
        self._set_i16_at(b + k.lane_group_x, x * k.FIXED_ONE)
        self._set_i16_at(b + k.lane_leader_x, x)
        self.m[b + k.lane_enemy_y] = self.m[b + k.lane_base_y]

    def place_diver(self, index, x, moving_left=False):
        k, b = self.k, self._lane_base(index)
        self.m[b + k.lane_moving_left] = int(moving_left)
        self.m[b + k.lane_diver_active] = 1
        self._set_i16_at(b + k.lane_diver_fixed_x, x * k.FIXED_ONE)
        self._set_i16_at(b + k.lane_diver_x, x)

    def clear_lane(self, index):
        k, b = self.k, self._lane_base(index)
        self.m[b + k.lane_alive_mask] = 0
        self.m[b + k.lane_diver_active] = 0

    def set_spawn_timer(self, index, frames):
        self.m[self._lane_base(index) + self.k.lane_spawn_timer] = frames

    def place_enemy_torpedo(self, index, x, moving_left=False):
        k, b = self.k, self._lane_base(index)
        t = b + k.lane_torpedo
        self._set_i16_at(t + k.torpedo_x, x * k.FIXED_ONE)
        self.m[t + k.torpedo_y] = self.m[b + k.lane_base_y]
        self.m[t + k.torpedo_active] = 1
        self.m[t + k.torpedo_moving_left] = int(moving_left)

    # --- what is on screen

    def sprites(self):
        """Hardware sprites that are on screen."""
        found = []
        for i in range(40):
            y, x, tile, flags = (self.m[OAM + 4 * i + j] for j in range(4))
            if 0 < y < 160 and 0 < x < 168:
                found.append(Sprite(x - SCREEN_SPRITE_OFFSET_X, y - SCREEN_SPRITE_OFFSET_Y, tile, flags))
        return found

    def max_sprites_on_a_line(self):
        lines = [0] * 144
        for sprite in self.sprites():
            for line in range(max(0, sprite.y), min(144, sprite.y + 8)):
                lines[line] += 1
        return max(lines)

    def bg_row(self, row):
        return [self.m[BG_MAP + row * 32 + column] for column in range(self.k.SCREEN_WIDTH // 8)]

    def hud_score(self):
        start = self.k.SCORE_COLUMN
        digits = self.bg_row(self.k.ROW_SCORE)[start:start + self.k.SCORE_DIGITS]
        return int("".join(str(tile - BG_TILE_DIGIT_0) for tile in digits))

    def hud_divers(self):
        return self.bg_row(self.k.ROW_DIVERS).count(BG_TILE_DIVER)

    def hud_reserves(self):
        return self.bg_row(self.k.ROW_LIVES).count(BG_TILE_LIFE)

    def hud_oxygen_tiles(self):
        return self.bg_row(self.k.ROW_OXYGEN).count(BG_TILE_O2_FULL)

    def banner_text_row(self):
        return self.bg_row(BANNER_TEXT_ROW)

    def channels_playing(self):
        """NR52 bits 0-3; needs Game(sound=True)."""
        return self.m[NR52] & 0x0F

    def track_sounds(self):
        """Records each effect passed to sound_play in self.sounds."""
        self.sounds = []
        self.pb.hook_register(0, self.symbols["_sound_play"], self._on_sound_play, None)

    def _on_sound_play(self, _):
        # SDCC passes the first 8-bit argument in A.
        self.sounds.append(self.pb.register_file.A)

    def screenshot(self, path):
        self.pb.screen.image.save(path)

    # --- performance

    def track_frame_costs(self):
        """Records the busy time of each loop iteration in self.frame_costs.

        The loop is input -> update -> vsync -> render, so one iteration of
        work runs from game_render's entry to the next call to vsync.
        """
        self.pb.hook_register(0, self.symbols["_game_render"], self._on_render, None)
        self.pb.hook_register(0, self.symbols["_vsync"], self._on_vsync, None)

    def _on_render(self, _):
        self._cost_start = self.pb._cycles()
        self._banner_at_render = self.banner
        self._banner_changed = self._banner_at_render != self._last_banner
        self._last_banner = self._banner_at_render

    def _on_vsync(self, _):
        if self._cost_start is not None:
            scanlines = (self.pb._cycles() - self._cost_start) / CYCLES_PER_SCANLINE
            self.frame_costs.append(FrameCost(scanlines, self._banner_changed))
            self._cost_start = None
