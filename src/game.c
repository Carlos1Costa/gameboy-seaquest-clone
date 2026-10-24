#include <gb/gb.h>
#include <gb/metasprites.h>
#include <rand.h>

#include "game.h"
#include "collision.h"
#include "player_sub.h"
#include "torpedo.h"
#include "shark.h"
#include "enemy_sub.h"
#include "diver.h"
#include "hud_tiles.h"

/* First VRAM tile of each sprite sheet. */
enum SpriteTile {
    SPRITE_TILE_PLAYER_SUB = 0,
    SPRITE_TILE_TORPEDO    = SPRITE_TILE_PLAYER_SUB + player_sub_TILE_COUNT,
    SPRITE_TILE_SHARK      = SPRITE_TILE_TORPEDO + torpedo_TILE_COUNT,
    SPRITE_TILE_ENEMY_SUB  = SPRITE_TILE_SHARK + shark_TILE_COUNT,
    SPRITE_TILE_DIVER      = SPRITE_TILE_ENEMY_SUB + enemy_sub_TILE_COUNT
};

/*
 * Must match the left-to-right tile order of res/hud_tiles.png.
 * HUD tiles (digits, icons, bar) are drawn over the BG_TILE_HUD color, so the
 * top and bottom HUD bands share that tile as their background.
 */
enum BackgroundTile {
    BG_TILE_WATER     = 0,
    BG_TILE_HUD       = 1,
    BG_TILE_SURFACE   = 2,
    BG_TILE_SEABED    = 3,
    BG_TILE_O2_EMPTY  = 4,
    BG_TILE_O2_FULL   = 5,
    BG_TILE_DIVER     = 6,
    BG_TILE_LIFE      = 7,
    BG_TILE_DIGIT_0   = 8,
    BG_TILE_LETTER_O  = 18
};

/* One period of round(SHARK_WAVE_AMPLITUDE * sin(t)), in SHARK_WAVE_STEPS steps. */
static const int8_t shark_wave[SHARK_WAVE_STEPS] = {
     0,  1,  2,  2,  3,  3,  4,  4,  4,  4,  4,  3,  3,  2,  2,  1,
     0, -1, -2, -2, -3, -3, -4, -4, -4, -4, -4, -3, -3, -2, -2, -1
};

static void draw_static_background(void) {
    const uint8_t oxygen_label[2] = { BG_TILE_LETTER_O, BG_TILE_DIGIT_0 + 2 };
    const uint8_t water_rows = ROW_SEABED - ROW_WATER_FIRST;

    fill_bkg_rect(0, ROW_SCORE, SCREEN_TILES_WIDE, ROW_SURFACE - ROW_SCORE, BG_TILE_HUD);
    fill_bkg_rect(0, ROW_SURFACE, SCREEN_TILES_WIDE, 1, BG_TILE_SURFACE);
    fill_bkg_rect(0, ROW_WATER_FIRST, SCREEN_TILES_WIDE, water_rows, BG_TILE_WATER);
    fill_bkg_rect(0, ROW_SEABED, SCREEN_TILES_WIDE, 1, BG_TILE_SEABED);
    fill_bkg_rect(0, ROW_OXYGEN, SCREEN_TILES_WIDE, SCREEN_TILES_HIGH - ROW_OXYGEN, BG_TILE_HUD);
    set_bkg_tiles(OXYGEN_LABEL_COLUMN, ROW_OXYGEN, 2, 1, oxygen_label);
}

static void reset_hud_cache(HudCache *hud) {
    /* Values the game can never reach, so the first render draws everything. */
    hud->score = 0xFFFFFFFF;
    hud->oxygen_tiles = 0xFF;
    hud->divers = 0xFF;
    hud->reserves = 0xFF;
}

static uint8_t lane_y(uint8_t lane_index) {
    return LANE_FIRST_Y + lane_index * LANE_SPACING;
}

/* Empties every lane and staggers their first spawns. */
static void reset_lanes(GameState *game) {
    for (uint8_t i = 0; i < LANE_COUNT; i++) {
        Lane *lane = &game->lanes[i];
        for (uint8_t j = 0; j < LANE_MAX_ENTITIES; j++) {
            lane->enemies[j].active = 0;
        }
        lane->diver.active = 0;
        lane->torpedo.active = 0;
        lane->spawn_timer = LANE_FIRST_SPAWN_DELAY + i * LANE_FIRST_SPAWN_STAGGER;
    }
}

static void reset_player(GameState *game) {
    game->player.x = pixel_to_fixed(PLAYER_START_X);
    game->player.y = pixel_to_fixed(PLAYER_START_Y);
    game->player.facing_left = 0;
    game->player_torpedo.active = 0;
    game->player_hit = 0;
}

static void reset_game(GameState *game) {
    reset_player(game);
    reset_lanes(game);
    game->score = 0;
    game->oxygen = OXYGEN_MAX;
    game->oxygen_drain_timer = OXYGEN_DRAIN_FRAMES;
    game->divers = 0;
    game->reserves = STARTING_RESERVES;
}

void game_init(GameState *game) {
    set_sprite_data(SPRITE_TILE_PLAYER_SUB, player_sub_TILE_COUNT, player_sub_tiles);
    set_sprite_data(SPRITE_TILE_TORPEDO, torpedo_TILE_COUNT, torpedo_tiles);
    set_sprite_data(SPRITE_TILE_SHARK, shark_TILE_COUNT, shark_tiles);
    set_sprite_data(SPRITE_TILE_ENEMY_SUB, enemy_sub_TILE_COUNT, enemy_sub_tiles);
    set_sprite_data(SPRITE_TILE_DIVER, diver_TILE_COUNT, diver_tiles);
    set_bkg_data(0, hud_tiles_TILE_COUNT, hud_tiles_tiles);
    draw_static_background();

    /* DIV is deterministic at boot; the title screen (F1.4) will reseed on START. */
    initrand(DIV_REG);
    game->frame = 0;
    reset_game(game);
    reset_hud_cache(&game->hud);
}

static inline Fixed step_down(Fixed value, Fixed step, Fixed min) {
    value -= step;
    return (value < min) ? min : value;
}

static inline Fixed step_up(Fixed value, Fixed step, Fixed max) {
    value += step;
    return (value > max) ? max : value;
}

static void update_player(Player *player, const InputState *input) {
    if (input->held & J_LEFT) {
        player->facing_left = 1;
        player->x = step_down(player->x, PLAYER_SPEED, pixel_to_fixed(PLAYER_MIN_X));
    } else if (input->held & J_RIGHT) {
        player->facing_left = 0;
        player->x = step_up(player->x, PLAYER_SPEED, pixel_to_fixed(PLAYER_MAX_X));
    }

    if (input->held & J_UP) {
        player->y = step_down(player->y, PLAYER_SPEED, pixel_to_fixed(PLAYER_MIN_Y));
    } else if (input->held & J_DOWN) {
        player->y = step_up(player->y, PLAYER_SPEED, pixel_to_fixed(PLAYER_MAX_Y));
    }
}

/* Places a torpedo just outside the nose of a 16-px-wide sub. */
static void launch_torpedo(Torpedo *torpedo, int16_t sub_x, uint8_t sub_y, uint8_t moving_left) {
    torpedo->active = 1;
    torpedo->moving_left = moving_left;
    torpedo->y = sub_y;
    if (moving_left) {
        torpedo->x = pixel_to_fixed(sub_x - TORPEDO_WIDTH);
    } else {
        torpedo->x = pixel_to_fixed(sub_x + ENTITY_WIDTH);
    }
}

/* Holding A refires as soon as the previous torpedo is gone. */
static void fire_player_torpedo(Torpedo *torpedo, const Player *player, const InputState *input) {
    if (torpedo->active || !(input->held & J_A)) {
        return;
    }
    launch_torpedo(torpedo, fixed_to_pixel(player->x), (uint8_t)fixed_to_pixel(player->y),
                   player->facing_left);
}

static void update_torpedo(Torpedo *torpedo, Fixed speed) {
    int16_t x;

    if (!torpedo->active) {
        return;
    }
    torpedo->x += torpedo->moving_left ? -speed : speed;
    x = fixed_to_pixel(torpedo->x);
    if (x <= -TORPEDO_WIDTH || x >= SCREEN_PX_WIDTH) {
        torpedo->active = 0;
    }
}

/* Off-screen x where a group member enters: members queue up behind the leader. */
static Fixed group_entry_x(uint8_t moving_left, uint8_t member) {
    int16_t queue_offset = member * GROUP_SPACING;
    if (moving_left) {
        return pixel_to_fixed(SCREEN_PX_WIDTH + queue_offset);
    }
    return pixel_to_fixed(-ENTITY_WIDTH - queue_offset);
}

static void spawn_group(Lane *lane) {
    uint8_t roll = rand();
    uint8_t has_diver = rand() < DIVER_CHANCE;
    /* The diver takes one of the lane's entity slots. */
    uint8_t max_enemies = has_diver ? LANE_MAX_ENTITIES - 1 : LANE_MAX_ENTITIES;
    uint8_t enemy_count = 1 + rand() % max_enemies;

    lane->moving_left = roll & 1;
    lane->enemy_type = (roll & 2) ? ENEMY_SUB : ENEMY_SHARK;
    for (uint8_t i = 0; i < LANE_MAX_ENTITIES; i++) {
        lane->enemies[i].active = i < enemy_count;
        lane->enemies[i].x = group_entry_x(lane->moving_left, i);
    }
    lane->diver.active = has_diver;
    lane->diver.x = group_entry_x(lane->moving_left, enemy_count);
    lane->fire_timer = ENEMY_FIRE_DELAY_MIN + (rand() & ENEMY_FIRE_DELAY_RANDOM_MASK);
}

static uint8_t lane_is_empty(const Lane *lane) {
    for (uint8_t i = 0; i < LANE_MAX_ENTITIES; i++) {
        if (lane->enemies[i].active) {
            return 0;
        }
    }
    return !lane->diver.active;
}

/* Moves a lane entity and deactivates it once it has left the far side. */
static void move_lane_entity(LaneEntity *entity, Fixed speed, uint8_t moving_left) {
    if (moving_left) {
        entity->x -= speed;
        if (fixed_to_pixel(entity->x) <= -ENTITY_WIDTH) {
            entity->active = 0;
        }
    } else {
        entity->x += speed;
        if (fixed_to_pixel(entity->x) >= SCREEN_PX_WIDTH) {
            entity->active = 0;
        }
    }
}

static uint8_t is_fully_on_screen(int16_t x) {
    return x >= 0 && x <= SCREEN_PX_WIDTH - ENTITY_WIDTH;
}

/* An enemy sub fires along its heading, only when the player is ahead of it. */
static void update_enemy_fire(Lane *lane, int16_t player_x) {
    if (lane->enemy_type != ENEMY_SUB || lane->torpedo.active) {
        return;
    }
    if (lane->fire_timer != 0) {
        lane->fire_timer--;
        return;
    }
    for (uint8_t i = 0; i < LANE_MAX_ENTITIES; i++) {
        const LaneEntity *enemy = &lane->enemies[i];
        int16_t x = fixed_to_pixel(enemy->x);
        uint8_t player_ahead = lane->moving_left ? (player_x < x) : (player_x > x);

        if (enemy->active && is_fully_on_screen(x) && player_ahead) {
            launch_torpedo(&lane->torpedo, x, enemy->y, lane->moving_left);
            lane->fire_timer = ENEMY_FIRE_DELAY_MIN + (rand() & ENEMY_FIRE_DELAY_RANDOM_MASK);
            return;
        }
    }
}

static void update_lane(Lane *lane, uint8_t base_y, uint8_t frame, int16_t player_x) {
    Fixed enemy_speed = (lane->enemy_type == ENEMY_SHARK) ? SHARK_SPEED : ENEMY_SUB_SPEED;
    /* Unsigned divisor: enum constants are int, and a signed division can't become a plain shift. */
    uint8_t wave_step = frame / (uint8_t)SHARK_WAVE_FRAMES_PER_STEP;

    if (lane_is_empty(lane)) {
        if (lane->spawn_timer != 0) {
            lane->spawn_timer--;
        } else {
            spawn_group(lane);
            lane->spawn_timer = SPAWN_DELAY_MIN + (rand() & SPAWN_DELAY_RANDOM_MASK);
        }
    }

    for (uint8_t i = 0; i < LANE_MAX_ENTITIES; i++) {
        LaneEntity *enemy = &lane->enemies[i];
        if (!enemy->active) {
            continue;
        }
        move_lane_entity(enemy, enemy_speed, lane->moving_left);
        enemy->y = base_y;
        if (lane->enemy_type == ENEMY_SHARK) {
            /* Members are out of phase, so the group ripples instead of bobbing in sync. */
            uint8_t step = (wave_step + i * SHARK_WAVE_MEMBER_OFFSET) & (SHARK_WAVE_STEPS - 1);
            enemy->y += shark_wave[step];
        }
    }

    if (lane->diver.active) {
        move_lane_entity(&lane->diver, DIVER_SPEED, lane->moving_left);
        lane->diver.y = base_y;
    }

    update_torpedo(&lane->torpedo, ENEMY_TORPEDO_SPEED);
    update_enemy_fire(lane, player_x);
}

/* Placeholder drain that wraps around; real oxygen rules come in EPIC D. */
static void update_oxygen(GameState *game) {
    game->oxygen_drain_timer--;
    if (game->oxygen_drain_timer != 0) {
        return;
    }
    game->oxygen_drain_timer = OXYGEN_DRAIN_FRAMES;
    game->oxygen = (game->oxygen > 0) ? game->oxygen - 1 : OXYGEN_MAX;
}

/*
 * Placeholder until EPIC E1.3 (explosion, respawn flow, game over screen):
 * lose a reserve and restart the round; with no reserves left, restart the game.
 */
static void handle_player_death(GameState *game) {
    if (game->reserves == 0) {
        reset_game(game);
        return;
    }
    game->reserves--;
    reset_player(game);
    reset_lanes(game);
}

void game_update(GameState *game, const InputState *input) {
    int16_t player_x;

    game->frame++;
    update_player(&game->player, input);
    update_torpedo(&game->player_torpedo, PLAYER_TORPEDO_SPEED);
    fire_player_torpedo(&game->player_torpedo, &game->player, input);

    player_x = fixed_to_pixel(game->player.x);
    for (uint8_t i = 0; i < LANE_COUNT; i++) {
        update_lane(&game->lanes[i], lane_y(i), game->frame, player_x);
    }

    collisions_update(game);
    if (game->player_hit) {
        handle_player_death(game);
    }
    update_oxygen(game);
}

static void draw_score(uint32_t score) {
    uint8_t tiles[SCORE_DIGITS];
    for (int8_t i = SCORE_DIGITS - 1; i >= 0; i--) {
        tiles[i] = BG_TILE_DIGIT_0 + (uint8_t)(score % 10);
        score /= 10;
    }
    set_bkg_tiles(SCORE_COLUMN, ROW_SCORE, SCORE_DIGITS, 1, tiles);
}

static void draw_oxygen_bar(uint8_t full_tiles) {
    uint8_t tiles[OXYGEN_BAR_TILES];
    for (uint8_t i = 0; i < OXYGEN_BAR_TILES; i++) {
        tiles[i] = (i < full_tiles) ? BG_TILE_O2_FULL : BG_TILE_O2_EMPTY;
    }
    set_bkg_tiles(OXYGEN_BAR_COLUMN, ROW_OXYGEN, OXYGEN_BAR_TILES, 1, tiles);
}

/* Draws `count` icons followed by blanks, in a fixed-width slot of `slots` tiles. */
static void draw_icon_counter(uint8_t column, uint8_t row, uint8_t count, uint8_t slots,
                              uint8_t icon_tile, uint8_t blank_tile) {
    uint8_t tiles[MAX_RESERVES];
    for (uint8_t i = 0; i < slots; i++) {
        tiles[i] = (i < count) ? icon_tile : blank_tile;
    }
    set_bkg_tiles(column, row, slots, 1, tiles);
}

static void render_hud(GameState *game) {
    HudCache *hud = &game->hud;
    /* Round up so the last tile only empties when oxygen is truly zero. */
    uint8_t oxygen_tiles = (uint8_t)(game->oxygen + 7) >> 3;

    if (hud->score != game->score) {
        hud->score = game->score;
        draw_score(game->score);
    }
    if (hud->oxygen_tiles != oxygen_tiles) {
        hud->oxygen_tiles = oxygen_tiles;
        draw_oxygen_bar(oxygen_tiles);
    }
    if (hud->reserves != game->reserves) {
        hud->reserves = game->reserves;
        draw_icon_counter(LIVES_COLUMN, ROW_LIVES, game->reserves, MAX_RESERVES,
                          BG_TILE_LIFE, BG_TILE_HUD);
    }
    if (hud->divers != game->divers) {
        hud->divers = game->divers;
        draw_icon_counter(DIVERS_COLUMN, ROW_DIVERS, game->divers, MAX_DIVERS_ON_BOARD,
                          BG_TILE_DIVER, BG_TILE_HUD);
    }
}

/*
 * Draws a right-facing metasprite (flipped when facing left) whose top-left
 * corner is at screen (x, y). Its pivot is the sprite center, so the sprite
 * is 2 * pivot_x wide. Returns how many hardware sprites were used.
 */
static uint8_t render_metasprite(const metasprite_t *metasprite, uint8_t base_tile,
                                 uint8_t pivot_x, uint8_t pivot_y,
                                 int16_t x, uint8_t y, uint8_t facing_left,
                                 uint8_t first_sprite) {
    uint8_t oam_x;
    uint8_t oam_y;

    /* Fully off screen: skip, so queued group members don't use OAM slots. */
    if (x <= -(int16_t)(2 * pivot_x) || x >= SCREEN_PX_WIDTH) {
        return 0;
    }
    /*
     * A partly visible sprite on the left edge can give a hardware x below 0;
     * the uint8_t wrap puts that tile past the right edge, where it is hidden.
     */
    oam_x = (uint8_t)(x + pivot_x + DEVICE_SPRITE_PX_OFFSET_X);
    oam_y = y + pivot_y + DEVICE_SPRITE_PX_OFFSET_Y;

    if (facing_left) {
        return move_metasprite_flipx(metasprite, base_tile, 0, first_sprite, oam_x, oam_y);
    }
    return move_metasprite_ex(metasprite, base_tile, 0, first_sprite, oam_x, oam_y);
}

static uint8_t render_torpedo(const Torpedo *torpedo, uint8_t first_sprite) {
    if (!torpedo->active) {
        return 0;
    }
    return render_metasprite(torpedo_metasprites[0], SPRITE_TILE_TORPEDO,
                             torpedo_PIVOT_X, torpedo_PIVOT_Y,
                             fixed_to_pixel(torpedo->x), torpedo->y, torpedo->moving_left,
                             first_sprite);
}

static uint8_t render_lane(const Lane *lane, uint8_t first_sprite) {
    uint8_t used = 0;
    const metasprite_t *enemy_metasprite = shark_metasprites[0];
    uint8_t enemy_tile = SPRITE_TILE_SHARK;

    if (lane->enemy_type == ENEMY_SUB) {
        enemy_metasprite = enemy_sub_metasprites[0];
        enemy_tile = SPRITE_TILE_ENEMY_SUB;
    }

    for (uint8_t i = 0; i < LANE_MAX_ENTITIES; i++) {
        const LaneEntity *enemy = &lane->enemies[i];
        if (enemy->active) {
            used += render_metasprite(enemy_metasprite, enemy_tile,
                                      shark_PIVOT_X, shark_PIVOT_Y,
                                      fixed_to_pixel(enemy->x), enemy->y, lane->moving_left,
                                      first_sprite + used);
        }
    }
    if (lane->diver.active) {
        used += render_metasprite(diver_metasprites[0], SPRITE_TILE_DIVER,
                                  diver_PIVOT_X, diver_PIVOT_Y,
                                  fixed_to_pixel(lane->diver.x), lane->diver.y, lane->moving_left,
                                  first_sprite + used);
    }
    used += render_torpedo(&lane->torpedo, first_sprite + used);
    return used;
}

void game_render(GameState *game) {
    const Player *player = &game->player;
    uint8_t next_sprite = 0;

    /* Lowest OAM slots win when a scanline overflows, so the player goes first. */
    next_sprite += render_metasprite(player_sub_metasprites[0], SPRITE_TILE_PLAYER_SUB,
                                     player_sub_PIVOT_X, player_sub_PIVOT_Y,
                                     fixed_to_pixel(player->x), (uint8_t)fixed_to_pixel(player->y),
                                     player->facing_left, next_sprite);
    next_sprite += render_torpedo(&game->player_torpedo, next_sprite);
    for (uint8_t i = 0; i < LANE_COUNT; i++) {
        next_sprite += render_lane(&game->lanes[i], next_sprite);
    }
    hide_sprites_range(next_sprite, MAX_HARDWARE_SPRITES);

    render_hud(game);
}
