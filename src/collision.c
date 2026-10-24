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

static inline uint8_t boxes_overlap(const Box *a, const Box *b) {
    /* Vertical test first: within a lane it is the cheaper, more selective one. */
    return a->top < b->bottom && b->top < a->bottom
        && a->left < b->right && b->left < a->right;
}

/* x must be at least partly on screen (see entity_is_visible). */
static inline void entity_box(Box *box, int16_t x, uint8_t y) {
    box->left = (uint8_t)(x + COLLISION_X_OFFSET + ENTITY_HITBOX_INSET_X);
    box->right = box->left + ENTITY_HITBOX_WIDTH;
    box->top = y + ENTITY_HITBOX_INSET_Y;
    box->bottom = box->top + ENTITY_HITBOX_HEIGHT;
}

/* Active torpedoes are always partly on screen (update_torpedo retires them). */
static void torpedo_box(Box *box, const Torpedo *torpedo) {
    box->left = (uint8_t)(fixed_to_pixel(torpedo->x) + COLLISION_X_OFFSET);
    box->right = box->left + TORPEDO_HITBOX_WIDTH;
    box->top = torpedo->y + TORPEDO_HITBOX_INSET_Y;
    box->bottom = box->top + TORPEDO_HITBOX_HEIGHT;
}

/*
 * Broad phase: lane members stay within LANE_REACH of the lane's base_y, so
 * a single 8-bit compare rules out whole lanes before any box test.
 */
static uint8_t is_near_lane(uint8_t y, const Lane *lane) {
    return (uint8_t)(y + LANE_REACH) > lane->base_y && y < (uint8_t)(lane->base_y + LANE_REACH);
}

/*
 * Members queued off screen can't be hit, and their x doesn't fit collision
 * space. Returns the bit of the first living, visible member overlapping box.
 */
static uint8_t find_hit_member(const Lane *lane, const Box *box) {
    Box member_hitbox;
    /* Locals instead of lane->...: each pointer dereference costs extra on the SM83. */
    uint8_t alive = lane->alive_mask;
    int16_t step = lane->member_step;
    uint8_t y = lane->enemy_y;
    int16_t x = lane->leader_x;

    for (uint8_t bit = 1; bit <= alive; bit <<= 1, x += step) {
        if (!(alive & bit) || !entity_is_visible(x)) {
            continue;
        }
        entity_box(&member_hitbox, x, y);
        if (boxes_overlap(box, &member_hitbox)) {
            return bit;
        }
    }
    return 0;
}

static void check_player_torpedo(GameState *game, Lane *lane) {
    Torpedo *torpedo = &game->player_torpedo;
    Box torpedo_hitbox;
    uint8_t hit;

    torpedo_box(&torpedo_hitbox, torpedo);
    hit = find_hit_member(lane, &torpedo_hitbox);
    if (hit) {
        lane->alive_mask &= (uint8_t)~hit;
        torpedo->active = 0;
        game->enemies_destroyed++;
    }
}

static void check_player_against_lane(GameState *game, const Box *player_hitbox, Lane *lane) {
    Box other_hitbox;

    if (find_hit_member(lane, player_hitbox)) {
        game->player_hit = 1;
    }

    if (lane->torpedo.active) {
        torpedo_box(&other_hitbox, &lane->torpedo);
        if (boxes_overlap(player_hitbox, &other_hitbox)) {
            game->player_hit = 1;
        }
    }

    /* A full sub can't take more divers; they swim on. */
    if (lane->diver_active && entity_is_visible(lane->diver_x)
        && game->divers < MAX_DIVERS_ON_BOARD) {
        entity_box(&other_hitbox, lane->diver_x, lane->base_y);
        if (boxes_overlap(player_hitbox, &other_hitbox)) {
            lane->diver_active = 0;
            game->divers++;
        }
    }
}

void collisions_update(GameState *game) {
    Torpedo *torpedo = &game->player_torpedo;
    uint8_t player_y = (uint8_t)fixed_to_pixel(game->player.y);
    Box player_hitbox;

    entity_box(&player_hitbox, fixed_to_pixel(game->player.x), player_y);

    /* The patrol sails the surface: only a sub at (or just under) it can touch it. */
    if (game->patrol.active && player_y < PLAYER_SUBMERGED_Y && entity_is_visible(game->patrol.x)) {
        Box patrol_hitbox;
        entity_box(&patrol_hitbox, game->patrol.x, PATROL_Y);
        if (boxes_overlap(&player_hitbox, &patrol_hitbox)) {
            game->player_hit = 1;
        }
    }

    for (uint8_t i = 0; i < LANE_COUNT; i++) {
        Lane *lane = &game->lanes[i];
        if (torpedo->active && is_near_lane(torpedo->y, lane)) {
            check_player_torpedo(game, lane);
        }
        if (is_near_lane(player_y, lane)) {
            check_player_against_lane(game, &player_hitbox, lane);
        }
    }
}
