"""F1.4: title -> game -> game over -> title, all on START."""
from harness import Game, text_tiles



def banner_shows(game, text):
    row, tiles = game.banner_text_row(), text_tiles(text)
    start = (len(row) - len(tiles)) // 2
    return row[start:start + len(tiles)] == tiles


def lose_last_life(game):
    game.reserves = 0
    game.divers = 0
    game.dive()
    game.rise()
    assert game.tick_until(lambda: game.game_over, 200)
    game.tick(3)


def test_boots_into_the_title_screen(title):
    assert title.banner == title.k.TITLE_BANNER
    assert banner_shows(title, "SEAQUEST")


def test_nothing_runs_on_the_title_screen(title):
    frame = title.frame
    title.tick(60)
    assert title.frame == frame
    assert title.sprites() == []


def test_start_begins_the_game(title):
    title.start()
    title.tick(2)
    assert title.banner == title.k.NO_BANNER
    assert set(title.banner_text_row()) == {0}
    assert title.state == title.k.REFILLING


def test_last_life_lost_shows_game_over(game):
    lose_last_life(game)
    assert game.banner == game.k.GAME_OVER_BANNER
    assert banner_shows(game, "GAME OVER")


def test_world_is_frozen_and_sprites_hidden_on_game_over(game):
    game.place_group(3, 60, alive_mask=0b11, enemy_type=game.k.SHARK)
    lose_last_life(game)
    frame = game.frame
    game.tick(30)
    assert game.frame == frame
    assert game.sprites() == []


def test_start_on_game_over_goes_to_title(game):
    lose_last_life(game)
    game.tap("start")
    game.tick(2)
    assert banner_shows(game, "SEAQUEST")


def test_new_game_after_game_over_starts_from_zero(game):
    game.score = 5000
    game.wave = 3
    lose_last_life(game)
    game.tap("start")
    game.tap("start")
    game.tick(2)
    assert not game.game_over
    assert (game.score, game.wave, game.divers) == (0, 0, 0)
    assert game.reserves == game.k.STARTING_RESERVES
    assert game.hud_score() == 0
    assert game.state == game.k.REFILLING


def test_time_on_title_changes_the_game(rom):
    """The boot is deterministic: the seed must come from when START is pressed."""
    def opening(wait):
        game = Game(*rom)
        game.start(title_wait=wait)
        game.tick(400)
        lanes = [(lane.alive_mask, lane.enemy_type, lane.moving_left) for lane in game.lanes()]
        game.stop()
        return tuple(lanes)

    assert len({opening(wait) for wait in (5, 37, 90)}) > 1
