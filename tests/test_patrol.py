"""F1.1: the patrol sub on the surface line."""
from harness import SPRITE_TILE_ENEMY_SUB


def wait_for_patrol(game):
    assert game.tick_until(lambda: game.patrol_active, 600)


def test_no_patrol_before_its_first_wave(game):
    game.hold_patrol = False
    game.force_wave = game.k.PATROL_FIRST_WAVE - 1
    for _ in range(600):
        game.tick()
        assert not game.patrol_active


def test_patrol_sails_the_surface_line(diving):
    game = diving
    game.hold_patrol = False
    game.force_wave = game.k.PATROL_FIRST_WAVE
    wait_for_patrol(game)
    assert game.tick_until(lambda: 20 < game.patrol_x < 120, 300)
    game.tick(2)
    on_surface = [s for s in game.sprites() if s.tile in (SPRITE_TILE_ENEMY_SUB, SPRITE_TILE_ENEMY_SUB + 1)]
    assert {s.y for s in on_surface} == {game.k.PATROL_Y}


def test_patrol_passes_alternate_sides(diving):
    game = diving
    game.hold_patrol = False
    game.force_wave = game.k.PATROL_FIRST_WAVE
    directions = []
    for _ in range(2):
        wait_for_patrol(game)
        x = game.patrol_x
        game.tick(10)
        directions.append(game.patrol_x > x)
        assert game.tick_until(lambda: not game.patrol_active, 600)
    assert directions[0] != directions[1]


def test_patrol_kills_the_sub_while_it_refills(game):
    game.hold_patrol = False
    game.force_wave = game.k.PATROL_FIRST_WAVE
    game.dive()
    game.divers = 1
    game.rise()
    assert game.state == game.k.REFILLING

    def refill_forever():
        if game.state == game.k.REFILLING:
            game.oxygen = 0
        return game.state == game.k.DYING

    assert game.tick_until(refill_forever, 1500)


def test_patrol_kills_the_sub_during_the_count(game):
    game.hold_patrol = False
    game.force_wave = game.k.PATROL_FIRST_WAVE
    game.dive()
    game.divers = game.k.MAX_DIVERS_ON_BOARD
    game.rise()
    assert game.state == game.k.TALLYING

    def count_forever():
        if game.state == game.k.TALLYING:
            game.divers = game.k.MAX_DIVERS_ON_BOARD
        return game.state == game.k.DYING

    assert game.tick_until(count_forever, 1500)


def test_patrol_cannot_reach_a_submerged_sub(diving):
    game = diving
    game.hold_patrol = False
    game.force_wave = game.k.PATROL_FIRST_WAVE
    game.oxygen = game.k.OXYGEN_MAX
    wait_for_patrol(game)
    assert game.tick_until(lambda: not game.patrol_active, 400)
    assert game.state == game.k.ACTIVE


def test_torpedo_passes_through_the_patrol(game):
    game.hold_patrol = False
    game.force_wave = game.k.PATROL_FIRST_WAVE
    game.set_player(0, game.k.PATROL_Y + 4)   # just under the patrol's hull line
    wait_for_patrol(game)
    assert game.tick_until(lambda: game.patrol_x > 60, 300)
    game.place_player_torpedo(game.patrol_x - 16, game.k.PATROL_Y)
    game.tick(10)
    assert game.patrol_active
    assert game.player_torpedo()[0]
    assert game.score == 0


def test_new_life_gets_a_pause_before_the_patrol(diving):
    game = diving
    game.hold_patrol = False
    game.force_wave = game.k.PATROL_FIRST_WAVE
    wait_for_patrol(game)
    game.oxygen = 1
    assert game.tick_until(lambda: game.state == game.k.DYING, 20)
    assert game.tick_until(lambda: game.state == game.k.REFILLING, game.k.DEATH_FRAMES + 2)
    assert not game.patrol_active
