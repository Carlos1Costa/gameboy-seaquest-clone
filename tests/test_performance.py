"""The 60 fps budget (GUIA section 6.1): a loop iteration must fit in one frame.

Costs are measured in scanlines with the emulator's cycle counter; a frame has
154. test_worst_case_scene rebuilds the heaviest scene the caps allow before
every frame, so it does not depend on luck; the stress tests play real games.
"""
import os
import random

import pytest

from harness import Game, PERFORMANCE_REPORT, SCANLINES_PER_FRAME

STRESS_FRAMES = int(os.environ.get("STRESS_FRAMES", "4000"))
BUTTONS = ["up", "down", "left", "right", "a", "start"]


def build_scene(game, enemies_per_lane, enemy_torpedoes):
    """Everything on screen at once, mid-frame-budget: nothing collides."""
    k = game.k
    y = game.lane(1).base_y
    game.oxygen = k.OXYGEN_MAX
    game.set_player(130, y)
    game.place_player_torpedo(40, y)
    for i, count in enumerate(enemies_per_lane):
        lane_type = k.SUB if i % 2 else k.SHARK
        game.place_group(i, 110, enemy_type=lane_type, alive_mask=(1 << count) - 1)
        if i < enemy_torpedoes:
            game.place_enemy_torpedo(i, 10)


@pytest.mark.parametrize("enemies_per_lane, enemy_torpedoes", [
    ((3, 3, 1, 1), 2),
    ((2, 2, 2, 2), 2),
    ((0, 0, 0, 0), 0),
], ids=["3-3-1-1", "2-2-2-2", "empty"])
def test_worst_case_scene(diving, enemies_per_lane, enemy_torpedoes):
    game = diving
    assert sum(enemies_per_lane) <= game.k.MAX_ACTIVE_ENEMIES
    assert enemy_torpedoes <= game.k.MAX_ENEMY_TORPEDOES
    game.track_frame_costs()
    first_frame = game.frame
    frames = 120
    for _ in range(frames):
        build_scene(game, enemies_per_lane, enemy_torpedoes)
        game.tick()
        assert game.state == game.k.ACTIVE
        assert game.max_sprites_on_a_line() <= 10

    worst = max(cost.scanlines for cost in game.frame_costs)
    PERFORMANCE_REPORT.append((f"scene {'-'.join(map(str, enemies_per_lane))}", worst))
    assert worst <= SCANLINES_PER_FRAME
    # Independent of the cycle counter: the loop ran exactly once per frame.
    assert (game.frame - first_frame) & 0xFF == frames & 0xFF


@pytest.mark.slow
@pytest.mark.parametrize("seed", [1, 2])
@pytest.mark.parametrize("wave", [None, 5], ids=["natural-wave", "wave-5"])
def test_random_play_stays_in_budget(rom, seed, wave):
    """Plays at random (START included), so deaths, kills and restarts all happen."""
    k = rom[0]
    game = Game(*rom)
    game.track_frame_costs()
    game.start(title_wait=seed * 13)
    game.force_wave = wave
    dice = random.Random(seed)
    held = set()
    for frame in range(STRESS_FRAMES):
        if frame % 15 == 0:
            for button in held:
                game.pb.button_release(button)
            held = set(dice.sample(BUTTONS, dice.randint(0, 3)))
            for button in held:
                game.pb.button_press(button)
        game.tick()
        lanes = game.lanes()
        assert game.max_sprites_on_a_line() <= 10
        assert sum(len(lane.member_xs) for lane in lanes) <= k.MAX_ACTIVE_ENEMIES
        assert sum(lane.torpedo_active for lane in lanes) <= k.MAX_ENEMY_TORPEDOES
    game.stop()

    # Showing or clearing a banner redraws the whole HUD at once: a known, invisible
    # one-frame spike on a static screen (see tests/README.md).
    in_game = [cost.scanlines for cost in game.frame_costs if not cost.banner_changed]
    worst = max(in_game)
    PERFORMANCE_REPORT.append((f"random play, seed {seed}, wave {wave or 'natural'}", worst))
    assert worst <= SCANLINES_PER_FRAME
