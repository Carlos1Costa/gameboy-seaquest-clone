"""F1.3: each game event requests the right sound effect.

The tests check what the game asks for (the argument of sound_play), not the
APU state: a channel can switch itself off within frames (a sweep that
overflows, for one), so reading NR52 afterwards depends on timing. Whether the
effects sound good is for ears, not tests.
"""
import pytest

from harness import Game


@pytest.fixture
def game(rom):
    game = Game(*rom, sound=True)
    game.start()
    game.calm_ocean()
    game.wait_active()
    game.dive()
    game.track_sounds()
    yield game
    game.stop()


def shoot_target(game):
    y = game.lane(1).base_y
    game.place_group(1, 60)
    game.place_player_torpedo(44, y)
    assert game.tick_until(lambda: game.lane(1).alive_mask == 0, 10)
    game.tick(1)


def test_apu_is_on_and_a_torpedo_reaches_pulse_1(game):
    assert not game.channels_playing() & 0x01
    game.tap("a")
    assert game.channels_playing() & 0x01


def test_no_sound_while_nothing_happens(game):
    game.tick(30)
    assert game.sounds == []


def test_torpedo(game):
    game.tap("a")
    assert game.sounds == [game.k.TORPEDO_SOUND]


def test_enemy_explosion(game):
    shoot_target(game)
    assert game.sounds == [game.k.ENEMY_EXPLOSION_SOUND]


def test_player_explosion(game):
    game.oxygen = 1
    assert game.tick_until(lambda: game.state == game.k.DYING, 20)
    assert game.k.PLAYER_EXPLOSION_SOUND in game.sounds


def test_rescue(game):
    game.set_player(80, game.lane(0).base_y)
    game.place_diver(0, 84)
    game.tick(3)
    assert game.sounds == [game.k.RESCUE_SOUND]


def test_low_oxygen_alarm_repeats(game):
    game.oxygen = game.k.OXYGEN_LOW
    game.tick(40)
    assert game.sounds.count(game.k.OXYGEN_ALARM_SOUND) >= 2


def test_no_alarm_with_a_healthy_tank(game):
    game.tick(40)
    assert game.k.OXYGEN_ALARM_SOUND not in game.sounds


def test_count_ticks(game):
    game.divers = game.k.MAX_DIVERS_ON_BOARD
    game.rise()
    assert game.tick_until(lambda: game.state != game.k.TALLYING, 800)
    assert game.sounds.count(game.k.TALLY_SOUND) >= game.k.MAX_DIVERS_ON_BOARD


def test_extra_life(game):
    game.score = 9990
    shoot_target(game)
    assert game.k.EXTRA_LIFE_SOUND in game.sounds
    assert game.reserves == game.k.STARTING_RESERVES + 1


def test_extra_life_sound_is_long_enough_to_hear(game):
    """A fast upward sweep overflows and the hardware cuts the channel within ms."""
    game.score = 9990
    y = game.lane(1).base_y
    game.place_group(1, 60)
    game.place_player_torpedo(44, y)
    assert game.tick_until(lambda: game.k.EXTRA_LIFE_SOUND in game.sounds, 10)
    game.tick(6)                    # 100 ms
    assert game.channels_playing() & 0x01


def refill_after_surfacing(game):
    game.divers = 1
    game.oxygen = 8
    game.rise()
    assert game.state == game.k.REFILLING
    game.tick_until(lambda: game.state != game.k.REFILLING, 200)


def test_refill_ticks_once_per_bar_tile_with_rising_pitch(game):
    k = game.k
    refill_after_surfacing(game)
    levels = game.refill_levels
    assert levels == sorted(levels) and len(set(levels)) == len(levels)
    assert all(level % k.OXYGEN_REFILL_TICK_UNITS == 0 for level in levels)
    assert levels[-1] == k.OXYGEN_MAX
    assert len(levels) >= k.OXYGEN_BAR_TILES - 2


def test_refill_sound_stops_when_the_tank_is_full(game):
    refill_after_surfacing(game)
    ticks = len(game.refill_levels)
    game.tick(60)
    assert len(game.refill_levels) == ticks


def test_no_refill_sound_under_water(game):
    game.tick(60)
    assert game.refill_levels == []


def test_refill_sound_is_not_the_tally_sound(game):
    refill_after_surfacing(game)
    assert game.k.TALLY_SOUND not in game.sounds
