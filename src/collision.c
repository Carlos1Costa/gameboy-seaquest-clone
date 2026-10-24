#include "collision.h"

/*
 * Hitboxes are smaller than the 16x8 sprites: the art has empty corners
 * (fins, towers, tails), and a forgiving box feels fairer than a pixel-exact one.
 */
enum Hitbox {
    ENTITY_HITBOX_INSET_X = 2,
    ENTITY_HITBOX_INSET_Y = 2,
    ENTITY_HITBOX_WIDTH   = ENTITY_WIDTH - 2 * ENTITY_HITBOX_INSET_X,
    ENTITY_HITBOX_HEIGHT  = ENTITY_HEIGHT - 2 * ENTITY_HITBOX_INSET_Y,

    /* The torpedo body is a 7x2 line on rows 3-4 of its 8x8 tile. */
    TORPEDO_HITBOX_INSET_Y = 3,
    TORPEDO_HITBOX_WIDTH   = 7,
    TORPEDO_HITBOX_HEIGHT  = 2
};

uint8_t boxes_overlap(const Box *a, const Box *b) {
    return a->x < b->x + b->width
        && b->x < a->x + a->width
        && a->y < b->y + b->height
        && b->y < a->y + a->height;
}

static void entity_box(Box *box, int16_t x, int16_t y) {
    box->x = x + ENTITY_HITBOX_INSET_X;
    box->y = y + ENTITY_HITBOX_INSET_Y;
    box->width = ENTITY_HITBOX_WIDTH;
    box->height = ENTITY_HITBOX_HEIGHT;
}

static void torpedo_box(Box *box, const Torpedo *torpedo) {
    box->x = fixed_to_pixel(torpedo->x);
    box->y = torpedo->y + TORPEDO_HITBOX_INSET_Y;
    box->width = TORPEDO_HITBOX_WIDTH;
    box->height = TORPEDO_HITBOX_HEIGHT;
}

static void lane_entity_box(Box *box, const LaneEntity *entity) {
    entity_box(box, fixed_to_pixel(entity->x), entity->y);
}

static void check_player_torpedo(Torpedo *torpedo, Lane *lane) {
    Box torpedo_hitbox;
    Box enemy_hitbox;

    if (!torpedo->active) {
        return;
    }
    torpedo_box(&torpedo_hitbox, torpedo);
    for (uint8_t i = 0; i < LANE_MAX_ENTITIES; i++) {
        LaneEntity *enemy = &lane->enemies[i];
        if (!enemy->active) {
            continue;
        }
        lane_entity_box(&enemy_hitbox, enemy);
        if (boxes_overlap(&torpedo_hitbox, &enemy_hitbox)) {
            enemy->active = 0;
            torpedo->active = 0;
            return;
        }
    }
}

static void check_player_against_lane(GameState *game, const Box *player_hitbox, Lane *lane) {
    Box other_hitbox;

    for (uint8_t i = 0; i < LANE_MAX_ENTITIES; i++) {
        if (!lane->enemies[i].active) {
            continue;
        }
        lane_entity_box(&other_hitbox, &lane->enemies[i]);
        if (boxes_overlap(player_hitbox, &other_hitbox)) {
            game->player_hit = 1;
        }
    }

    if (lane->torpedo.active) {
        torpedo_box(&other_hitbox, &lane->torpedo);
        if (boxes_overlap(player_hitbox, &other_hitbox)) {
            game->player_hit = 1;
        }
    }

    /* A full sub can't take more divers; they swim on. */
    if (lane->diver.active && game->divers < MAX_DIVERS_ON_BOARD) {
        lane_entity_box(&other_hitbox, &lane->diver);
        if (boxes_overlap(player_hitbox, &other_hitbox)) {
            lane->diver.active = 0;
            game->divers++;
        }
    }
}

void collisions_update(GameState *game) {
    Box player_hitbox;

    entity_box(&player_hitbox, fixed_to_pixel(game->player.x), fixed_to_pixel(game->player.y));

    for (uint8_t i = 0; i < LANE_COUNT; i++) {
        Lane *lane = &game->lanes[i];
        check_player_torpedo(&game->player_torpedo, lane);
        check_player_against_lane(game, &player_hitbox, lane);
    }
}
