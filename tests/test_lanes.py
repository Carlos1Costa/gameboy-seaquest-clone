"""EPIC C: spawner, sharks, enemy subs, divers and collisions."""
import pytest

from harness import Game


@pytest.fixture(scope="module")
def ocean(rom):
    """Watches 3000 frames of the natural spawner, with the sub safe at the surface."""
    game = Game(*rom)
    game.start()
    seen = {"sides": set(), "types": set(), "spacing": set(), "enemies": 0, "lane_entities": 0,
            "torpedoes": 0, "shark_offsets": set(), "divers": 0, "line": 0}
    for _ in range(3000):
        game.tick()
        lanes = game.lanes()
        seen["enemies"] = max(seen["enemies"], sum(len(l.member_xs) for l in lanes))
        seen["torpedoes"] = max(seen["torpedoes"], sum(l.torpedo_active for l in lanes))
        seen["line"] = max(seen["line"], game.max_sprites_on_a_line())
        for lane in lanes:
            xs = lane.member_xs
            seen["lane_entities"] = max(seen["lane_entities"], len(xs) + lane.diver_active)
            seen["divers"] += lane.diver_active
            if xs:
                seen["sides"].add(lane.moving_left)
                seen["types"].add(lane.enemy_type)
                if lane.enemy_type == game.k.SHARK:
                    seen["shark_offsets"].add(lane.y - lane.base_y)
                else:
                    assert lane.y == lane.base_y
            if lane.alive_mask == 0b111:
                seen["spacing"].add(abs(xs[0] - xs[1]))
    game.stop()
    return seen


def test_groups_enter_from_both_sides(ocean):
    assert ocean["sides"] == {0, 1}


def test_both_enemy_types_spawn(ocean, k):
    assert ocean["types"] == {k.SHARK, k.SUB}


def test_divers_spawn(ocean):
    assert ocean["divers"] > 0


def test_members_keep_the_group_spacing(ocean, k):
    assert ocean["spacing"] <= {k.GROUP_SPACING}


def test_lane_entity_cap(ocean, k):
    assert ocean["lane_entities"] <= k.LANE_MAX_ENTITIES


def test_global_enemy_cap(ocean, k):
    assert ocean["enemies"] <= k.MAX_ACTIVE_ENEMIES


def test_global_enemy_torpedo_cap(ocean, k):
    assert ocean["torpedoes"] <= k.MAX_ENEMY_TORPEDOES


def test_sharks_bob_around_their_lane(ocean, k):
    amplitude = k.SHARK_WAVE_AMPLITUDE
    assert min(ocean["shark_offsets"]) == -amplitude
    assert max(ocean["shark_offsets"]) == amplitude


def test_lanes_never_overflow_a_scanline(ocean):
    assert ocean["line"] <= 10


def fill_lanes(game, masks):
    """Still sharks in the first lanes, leaving the last lane free to spawn."""
    for index, mask in enumerate(masks):
        game.place_group(index, 100, enemy_type=game.k.SHARK, alive_mask=mask)
    game.hold_spawns = False


def test_spawner_never_exceeds_the_enemy_cap(game):
    k = game.k
    assert k.MAX_ACTIVE_ENEMIES == 8, "adapt the groups below to the new cap"
    fill_lanes(game, [0b111, 0b111, 0b001])        # 7 alive: one place left
    free = k.LANE_COUNT - 1
    for _ in range(40):
        game.clear_lane(free)
        game.set_spawn_timer(free, 0)
        game.tick(2)
        assert len(game.lane(free).member_xs) == 1


def test_no_group_spawns_at_the_enemy_cap(game):
    fill_lanes(game, [0b111, 0b111, 0b011])        # 8 alive
    free = game.k.LANE_COUNT - 1
    game.set_spawn_timer(free, 0)
    for _ in range(300):
        game.tick()
        assert game.lane(free).alive_mask == 0


def test_subs_hold_fire_at_the_enemy_torpedo_cap(diving):
    game = diving
    assert game.k.MAX_ENEMY_TORPEDOES == 2, "adapt the torpedoes below to the new cap"
    game.set_player(140, game.lane(3).base_y)
    game.place_group(2, 20, fire_timer=0)
    for _ in range(20):
        game.place_enemy_torpedo(0, 10)
        game.place_enemy_torpedo(1, 10)
        game.tick()
        assert not game.lane(2).torpedo_active


def test_group_moves_and_leaves_at_the_far_edge(game):
    k = game.k
    game.place_group(0, 100, speed=k.FIXED_ONE, alive_mask=1)
    game.tick(10)
    assert abs(game.lane(0).member_xs[0] - 110) <= 1
    assert game.tick_until(lambda: game.lane(0).alive_mask == 0, 100)


def test_queued_members_are_not_retired_before_entering(game):
    k = game.k
    game.place_group(0, -16, speed=k.FIXED_ONE, alive_mask=0b111)
    game.tick(30)
    assert game.lane(0).alive_mask == 0b111


def test_enemy_sub_fires_when_the_player_is_ahead(diving):
    game = diving
    y = game.lane(1).base_y
    game.set_player(120, y)
    game.place_group(1, 20, fire_timer=0)
    game.tick(3)
    assert game.lane(1).torpedo_active
    assert game.lane(1).torpedo_x >= 36


def test_enemy_sub_holds_fire_when_the_player_is_behind(diving):
    game = diving
    game.set_player(10, 100)
    game.place_group(1, 80, fire_timer=0)
    game.tick(30)
    assert not game.lane(1).torpedo_active


def test_sharks_never_fire(diving):
    game = diving
    game.set_player(120, 100)
    game.place_group(1, 20, enemy_type=game.k.SHARK, fire_timer=0)
    game.tick(30)
    assert not game.lane(1).torpedo_active


def test_enemy_torpedo_speed(diving):
    game = diving
    game.place_enemy_torpedo(0, 10)
    game.tick(20)
    assert game.lane(0).torpedo_x - 10 in range(38, 43)   # 2 px/frame, give or take a frame


def test_enemy_torpedo_kills_the_player(diving):
    game = diving
    y = game.lane(2).base_y
    game.set_player(80, y)
    game.place_enemy_torpedo(2, 40)
    assert game.tick_until(lambda: game.state == game.k.DYING, 40)


def test_touching_an_enemy_kills_the_player(diving):
    game = diving
    y = game.lane(1).base_y
    game.set_player(80, y)
    game.place_group(1, 84)
    game.tick(2)
    assert game.state == game.k.DYING


def test_hitboxes_forgive_grazing_contact(diving):
    """Hitboxes are smaller than sprites: 4 px of vertical overlap is safe."""
    game = diving
    y = game.lane(1).base_y
    game.set_player(80, y + 4)
    game.place_group(1, 80)
    game.tick(3)
    assert game.state == game.k.ACTIVE


def test_torpedo_destroys_only_the_member_it_hits(diving):
    game = diving
    y = game.lane(1).base_y
    game.place_group(1, 110, alive_mask=0b111)      # members at 110, 86, 62
    game.set_player(20, y)
    game.tap("a")
    assert game.tick_until(lambda: game.lane(1).alive_mask != 0b111, 30)
    assert game.lane(1).alive_mask == 0b011
    assert not game.player_torpedo()[0]


def test_torpedo_cannot_hit_members_off_screen(diving):
    game = diving
    y = game.lane(1).base_y
    game.place_group(1, -40, alive_mask=1)
    game.place_player_torpedo(0, y, moving_left=True)
    game.tick(6)
    assert game.lane(1).alive_mask == 1


def test_diver_is_rescued_on_contact(diving):
    game = diving
    y = game.lane(0).base_y
    game.set_player(80, y)
    game.place_diver(0, 84)
    game.tick(2)
    assert game.divers == 1
    assert not game.lane(0).diver_active
    game.tick(2)
    assert game.hud_divers() == 1


def test_full_sub_leaves_divers_in_the_water(diving):
    game = diving
    y = game.lane(0).base_y
    game.divers = game.k.MAX_DIVERS_ON_BOARD
    game.set_player(80, y)
    game.place_diver(0, 84)
    game.tick(3)
    assert game.divers == game.k.MAX_DIVERS_ON_BOARD
    assert game.lane(0).diver_active
