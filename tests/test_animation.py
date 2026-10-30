"""EPIC G: sprite flipbook animation. It is cosmetic: only the tiles shown change."""
import pytest

from harness import S_FLIPX, SPRITE_TILES


def poses(game, sheet, frames):
    """The set of first tiles a sheet shows over some frames."""
    seen = set()
    for _ in range(frames):
        game.tick()
        sprites = game.sprites_of(sheet)
        if sprites:
            seen.add(min(s.tile for s in sprites))
    return seen


def first_tile(sheet, frame=0):
    return SPRITE_TILES[sheet].start + frame * 2


def test_hero_propeller_turns_even_when_idle(diving):
    assert poses(diving, "player_sub", 16) == {first_tile("player_sub", 0), first_tile("player_sub", 1)}


def test_hero_pose_lasts_its_period(diving):
    game = diving
    changes, previous = [], None
    for frame in range(32):
        game.tick()
        pose = game.sprites_of("player_sub")[0].tile
        if previous is not None and pose != previous:
            changes.append(frame)
        previous = pose
    gaps = {b - a for a, b in zip(changes, changes[1:])}
    assert gaps == {game.k.ANIM_PERIOD_PROPELLER}


def test_enemy_sub_propeller_turns(diving):
    diving.place_group(1, 60)
    assert poses(diving, "enemy_sub", 16) == {first_tile("enemy_sub", 0), first_tile("enemy_sub", 1)}


def test_patrol_inherits_the_enemy_propeller(diving):
    game = diving
    game.hold_patrol = False
    game.force_wave = game.k.PATROL_FIRST_WAVE
    assert game.tick_until(lambda: 20 < game.patrol_x < 100 and game.patrol_active, 600)
    assert poses(game, "enemy_sub", 16) == {first_tile("enemy_sub", 0), first_tile("enemy_sub", 1)}


def test_shark_bites_as_it_swims(diving):
    diving.place_group(1, 60, enemy_type=diving.k.SHARK)
    assert poses(diving, "shark", 32) == {first_tile("shark", 0), first_tile("shark", 1)}


def test_diver_kicks(diving):
    diving.place_diver(1, 60)
    assert poses(diving, "diver", 32) == {first_tile("diver", 0), first_tile("diver", 1)}


@pytest.mark.parametrize("sheet, place", [
    ("enemy_sub", lambda game: game.place_group(1, 60, moving_left=True)),
    ("diver", lambda game: game.place_diver(1, 60, moving_left=True)),
])
def test_mirroring_keeps_working_on_every_frame(diving, sheet, place):
    game = diving
    place(game)
    for _ in range(32):
        game.tick()
        sprites = game.sprites_of(sheet)
        if len(sprites) == 2:
            left, right = sprites
            assert left.tile == right.tile + 1
            assert left.flags & right.flags & S_FLIPX


def test_a_frame_never_splits_across_two_poses(diving):
    game = diving
    game.place_group(1, 60, alive_mask=0b111, enemy_type=game.k.SHARK)
    for _ in range(32):
        game.tick()
        sprites = game.sprites_of("shark")
        start = SPRITE_TILES["shark"].start
        assert len({(s.tile - start) // 2 for s in sprites}) == 1     # every member in the same pose


def test_the_world_stops_animating_during_the_explosion(diving):
    game = diving
    game.place_group(1, 60)
    game.tick(2)
    game.oxygen = 1
    assert game.tick_until(lambda: game.state == game.k.DYING, 20)
    game.tick(2)
    assert len(poses(game, "enemy_sub", 40)) == 1


def test_explosion_goes_through_its_frames(diving):
    game = diving
    game.oxygen = 1
    assert game.tick_until(lambda: game.state == game.k.DYING, 20)
    seen = []
    for _ in range(game.k.DEATH_FRAMES):
        game.tick()
        tiles = game.sprites_of("explosion")
        if tiles:
            first = min(s.tile for s in tiles)
            if not seen or seen[-1] != first:
                seen.append(first)
    assert seen == [first_tile("explosion", i) for i in range(3)]


def test_torpedo_has_a_single_pose(diving):
    game = diving
    game.set_player(0, 60)
    game.tap("a")
    assert len(poses(game, "torpedo", 16)) == 1
