"""EPICs E and F1.2: points, extra lives, the on-screen count, difficulty."""
import pytest


def destroy_one_enemy(game):
    """Shoots a still target in lane 1; returns the points scored."""
    before = game.score
    y = game.lane(1).base_y
    game.set_player(20, y)
    game.tick(1)
    game.place_group(1, 70)
    game.tap("a")
    assert game.tick_until(lambda: game.lane(1).alive_mask == 0, 30)
    game.tick(2)
    return game.score - before


def surface_with_full_load(game):
    """Returns the oxygen left when the count starts; the count is finished on return."""
    game.divers = game.k.MAX_DIVERS_ON_BOARD
    game.rise()
    oxygen = game.oxygen
    assert game.tick_until(lambda: game.state != game.k.TALLYING, 800)
    return oxygen


def enemy_value(k, wave):
    return min(k.ENEMY_POINTS_BASE + wave * k.ENEMY_POINTS_STEP, k.ENEMY_POINTS_MAX)


def diver_value(k, wave):
    return min(k.DIVER_POINTS_BASE + wave * k.DIVER_POINTS_STEP, k.DIVER_POINTS_MAX)


@pytest.mark.parametrize("wave", [0, 1, 3, 7, 30, 200])
def test_enemy_value_grows_with_the_wave_up_to_the_cap(diving, wave):
    diving.force_wave = wave
    assert destroy_one_enemy(diving) == enemy_value(diving.k, wave)


def test_hud_shows_the_score(diving):
    destroy_one_enemy(diving)
    assert diving.hud_score() == diving.score


@pytest.mark.parametrize("wave", [0, 3, 19, 40])
def test_full_load_pays_every_diver_plus_oxygen_bonus(diving, wave):
    game, k = diving, diving.k
    game.wave = wave
    oxygen = surface_with_full_load(game)
    assert game.score == k.MAX_DIVERS_ON_BOARD * diver_value(k, wave) + oxygen * k.OXYGEN_BONUS_PER_UNIT


def test_partial_load_pays_nothing(diving):
    game = diving
    game.divers = 3
    game.rise()
    assert game.score == 0


def test_count_goes_diver_by_diver_then_oxygen(diving):
    game = diving
    game.oxygen = 50
    game.divers = game.k.MAX_DIVERS_ON_BOARD
    game.rise()
    oxygen = game.oxygen
    divers, tank, scores = [], [], []
    while game.state == game.k.TALLYING:
        game.tick()
        divers.append(game.divers)
        tank.append(game.oxygen)
        scores.append(game.score)
    assert sorted(set(divers), reverse=True) == [6, 5, 4, 3, 2, 1, 0]
    assert all(level == oxygen for level in tank[:divers.index(0)])
    assert tank[-1] == 0
    assert scores == sorted(scores)


def test_count_is_followed_by_the_next_wave_and_a_refill(diving):
    game = diving
    surface_with_full_load(game)
    assert game.state == game.k.REFILLING
    assert game.wave == 1
    game.wait_active()
    assert game.oxygen == game.k.OXYGEN_MAX


def test_crossing_10000_earns_a_reserve(diving):
    game = diving
    game.score = 9990
    game.reserves = 2
    points = destroy_one_enemy(game)
    assert game.score == 9990 + points >= 10000
    assert game.reserves == 3
    game.tick(2)
    assert game.hud_reserves() == 3


def test_reserves_stop_at_the_maximum(diving):
    game = diving
    game.score = 19990
    game.reserves = game.k.MAX_RESERVES
    destroy_one_enemy(game)
    assert game.reserves == game.k.MAX_RESERVES


def test_score_stops_at_999999(diving):
    game = diving
    game.score = 999990
    destroy_one_enemy(game)
    assert game.score == 999999


def test_score_carries_across_digits(diving):
    game = diving
    game.score = 99990
    points = destroy_one_enemy(game)
    assert game.score == 99990 + points >= 100000


@pytest.mark.parametrize("wave", [0, 4, 40])
def test_enemy_speed_grows_with_the_wave_up_to_the_cap(title, wave):
    game, k = title, title.k
    game.start()
    game.force_wave = wave
    expected = {
        k.SHARK: min(k.SHARK_SPEED + wave * k.ENEMY_SPEED_PER_WAVE, k.ENEMY_SPEED_MAX),
        k.SUB: min(k.ENEMY_SUB_SPEED + wave * k.ENEMY_SPEED_PER_WAVE, k.ENEMY_SPEED_MAX),
    }
    seen = {}

    def both_types_seen():
        for lane in game.lanes():
            if lane.member_xs:
                seen[lane.enemy_type] = lane.speed
        return len(seen) == 2

    assert game.tick_until(both_types_seen, 3000)
    assert seen == expected
