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
