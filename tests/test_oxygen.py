"""EPIC D: oxygen, refilling, surfacing rules, deaths."""
import pytest


def test_life_starts_frozen_at_the_surface_refilling(title):
    title.start()
    title.tick(3)
    k = title.k
    assert title.state == k.REFILLING
    assert title.player_y == k.PLAYER_MIN_Y
    assert 0 < title.oxygen < k.OXYGEN_MAX


def test_input_is_ignored_while_refilling(title):
    title.start()
    title.tick(3)
    x, y = title.player_x, title.player_y
    title.hold("down", 15)
    title.hold("left", 15)
    title.tap("a")
    assert title.state == title.k.REFILLING
    assert (title.player_x, title.player_y) == (x, y)
    assert not title.player_torpedo()[0]


def test_refill_ends_with_a_full_tank_and_returns_control(game):
    assert game.state == game.k.ACTIVE
    assert game.oxygen == game.k.OXYGEN_MAX
    game.tick(2)
    assert game.hud_oxygen_tiles() == game.k.OXYGEN_BAR_TILES


def test_no_drain_at_the_surface(game):
    oxygen = game.oxygen
    game.tick(60)
    assert game.oxygen == oxygen


def test_wiggling_at_the_surface_is_not_a_dive(game):
    game.hold("down", game.k.PLAYER_SUBMERGED_Y - game.k.PLAYER_MIN_Y - 2)
    assert not game.submerged
    game.hold("up", 10)
    assert game.state == game.k.ACTIVE


def test_drains_at_a_constant_rate_under_water(diving):
    game = diving
    game.tick(1)
    oxygen = game.oxygen
    frames = 20 * game.k.OXYGEN_DRAIN_FRAMES
    game.tick(frames)
    assert oxygen - game.oxygen == 20


def test_hud_bar_follows_the_oxygen(diving):
    game = diving
    game.oxygen = 41                     # 5 tiles and a bit: rounds up to 6
    game.tick(2)
    assert game.hud_oxygen_tiles() == 6


def test_low_oxygen_bar_blinks(diving):
    game = diving
    game.oxygen = game.k.OXYGEN_LOW
    seen = set()
    for _ in range(40):
        game.tick()
        seen.add(game.hud_oxygen_tiles())
    assert 0 in seen and max(seen) > 0


def test_surfacing_with_some_divers_costs_one_and_refills(diving):
    game = diving
    game.divers = 3
    game.rise()
    assert game.divers == 2
    assert game.state == game.k.REFILLING
    game.tick(3)
    assert game.hud_divers() == 2


def test_surfacing_empty_destroys_the_sub(diving):
    game = diving
    game.rise()
    assert game.state == game.k.DYING


def test_surfacing_with_a_full_load_clears_the_ocean(diving):
    game = diving
    game.divers = game.k.MAX_DIVERS_ON_BOARD
    game.place_group(3, 60, alive_mask=0b11)
    game.rise()
    assert game.state == game.k.TALLYING
    assert game.lane(3).alive_mask == 0


def test_running_out_of_oxygen_destroys_the_sub(diving):
    game = diving
    game.oxygen = 1
    assert game.tick_until(lambda: game.state == game.k.DYING, game.k.OXYGEN_DRAIN_FRAMES + 2)


@pytest.mark.parametrize("cause", ["oxygen", "enemy", "enemy_torpedo", "surfacing_empty"])
def test_every_death_costs_a_reserve_and_all_divers(diving, cause):
    game = diving
    reserves = game.reserves
    y = game.lane(1).base_y
    if cause == "surfacing_empty":
        game.divers = 0
        game.rise()
    else:
        game.divers = 5
        if cause == "oxygen":
            game.oxygen = 1
        elif cause == "enemy":
            game.set_player(80, y)
            game.place_group(1, 84)
        else:
            game.set_player(80, y)
            game.place_enemy_torpedo(1, 60)
    assert game.tick_until(lambda: game.state == game.k.DYING, 60)
    assert game.divers == 0
    assert game.tick_until(lambda: game.state == game.k.REFILLING, game.k.DEATH_FRAMES + 2)
    assert game.reserves == reserves - 1
    assert game.player_y == game.k.PLAYER_MIN_Y


def test_world_freezes_during_the_explosion(diving):
    game = diving
    game.place_group(0, 40, speed=game.k.FIXED_ONE)
    game.tick(2)
    game.oxygen = 1
    assert game.tick_until(lambda: game.state == game.k.DYING, 20)
    x = game.lane(0).member_xs
    game.tick(30)
    assert game.lane(0).member_xs == x
