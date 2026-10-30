"""EPICs A and B: movement at 60 fps, screen limits, orientation, player torpedo."""
import pytest

from harness import S_FLIPX, SPRITE_TILES


def test_moves_one_pixel_per_frame_in_every_direction(diving):
    game = diving
    game.set_player(80, 60)
    for button, dx, dy in (("right", 1, 0), ("left", -1, 0), ("down", 0, 1), ("up", 0, -1)):
        x, y = game.player_x, game.player_y
        game.hold(button, 20)
        assert (game.player_x - x, game.player_y - y) == (20 * dx, 20 * dy)


def test_moves_diagonally(diving):
    game = diving
    game.set_player(80, 60)
    game.pb.button_press("right")
    game.hold("down", 10)
    game.pb.button_release("right")
    game.tick(1)
    assert game.player_y == 70
    assert game.player_x > 80


@pytest.mark.parametrize("button, axis, limit", [
    ("left", "player_x", "PLAYER_MIN_X"),
    ("right", "player_x", "PLAYER_MAX_X"),
    ("down", "player_y", "PLAYER_MAX_Y"),
])
def test_stops_at_the_screen_limits(diving, button, axis, limit):
    diving.hold(button, 200)
    assert getattr(diving, axis) == getattr(diving.k, limit)


def test_sprite_mirrors_when_facing_left(game):
    game.tick(2)
    left, right = game.sprites_of("player_sub")
    assert left.tile % 2 == 0 and right.tile == left.tile + 1    # left tile of a frame, then right
    assert not left.flags & S_FLIPX

    game.hold("left", 3)
    game.tick(2)
    left, right = game.sprites_of("player_sub")
    assert game.facing_left
    assert left.tile % 2 == 1 and left.tile == right.tile + 1    # mirrored: swapped and flipped
    assert left.flags & right.flags & S_FLIPX


def test_torpedo_leaves_the_nose_in_the_facing_direction(diving):
    game = diving
    game.set_player(60, 60)
    game.tap("a")
    active, x, y = game.player_torpedo()
    assert active and y == 60 and x > game.player_x + 15

    game.tick(40)
    game.hold("left", 2)
    game.tick(40)
    start = game.player_x
    game.tap("a")
    active, x, _ = game.player_torpedo()
    assert active and x < start


def test_torpedo_speed(diving):
    game = diving
    game.set_player(0, 60)
    game.tap("a")
    x = game.player_torpedo()[1]
    game.tick(10)
    assert game.player_torpedo()[1] - x == 10 * game.k.PLAYER_TORPEDO_SPEED // game.k.FIXED_ONE


def test_torpedo_keeps_its_heading_when_the_sub_moves(diving):
    game = diving
    game.set_player(20, 60)
    game.tap("a")
    game.hold("down", 8)
    assert game.player_torpedo()[2] == 60


def test_torpedo_vanishes_off_screen(diving):
    game = diving
    game.tap("a")
    assert game.tick_until(lambda: not game.player_torpedo()[0], 60)


def test_only_one_torpedo_at_a_time_and_holding_a_refires(diving):
    game = diving
    game.set_player(0, 60)
    launches, previous_x = 0, game.k.SCREEN_WIDTH
    game.pb.button_press("a")
    for _ in range(120):
        game.tick()
        active, x, _ = game.player_torpedo()
        assert len(game.sprites_of("torpedo")) <= 1
        if active and x < previous_x:
            launches += 1
        previous_x = x
    game.pb.button_release("a")
    assert launches >= 3
