#include <gb/gb.h>
#include <gb/metasprites.h>

#include "game.h"
#include "player_sub.h"
#include "hud_tiles.h"

/* First VRAM tile of each sprite sheet. */
enum SpriteTile {
    SPRITE_TILE_PLAYER_SUB = 0
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

/* Placeholder until EPIC B1.3 turns A into the torpedo button. */
enum DebugTuning {
    DEBUG_SCORE_PER_PRESS = 20
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

void game_init(GameState *game) {
    set_sprite_data(SPRITE_TILE_PLAYER_SUB, player_sub_TILE_COUNT, player_sub_tiles);
    set_bkg_data(0, hud_tiles_TILE_COUNT, hud_tiles_tiles);
    draw_static_background();

    game->player.x = PLAYER_START_X;
    game->player.y = PLAYER_START_Y;
    game->player.facing_left = 0;

    game->score = 0;
    game->oxygen = OXYGEN_MAX;
    game->oxygen_drain_timer = OXYGEN_DRAIN_FRAMES;
    game->divers = 0;
    game->reserves = STARTING_RESERVES;
    reset_hud_cache(&game->hud);
}

/* Unsigned-safe moves: never wrap past the limit. */
static inline uint8_t step_down(uint8_t value, uint8_t step, uint8_t min) {
    return (value >= (uint8_t)(min + step)) ? (uint8_t)(value - step) : min;
}

static inline uint8_t step_up(uint8_t value, uint8_t step, uint8_t max) {
    return (value <= (uint8_t)(max - step)) ? (uint8_t)(value + step) : max;
}

static void update_player(Player *player, const InputState *input) {
    if (input->held & J_LEFT) {
        player->facing_left = 1;
        player->x = step_down(player->x, PLAYER_SPEED, PLAYER_MIN_X);
    } else if (input->held & J_RIGHT) {
        player->facing_left = 0;
        player->x = step_up(player->x, PLAYER_SPEED, PLAYER_MAX_X);
    }

    if (input->held & J_UP) {
        player->y = step_down(player->y, PLAYER_SPEED, PLAYER_MIN_Y);
    } else if (input->held & J_DOWN) {
        player->y = step_up(player->y, PLAYER_SPEED, PLAYER_MAX_Y);
    }
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

void game_update(GameState *game, const InputState *input) {
    update_player(&game->player, input);
    update_oxygen(game);

    if (input->pressed & J_A) {
        game->score += DEBUG_SCORE_PER_PRESS;
    }
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

/* Returns how many hardware sprites were used. */
static uint8_t render_player(const Player *player, uint8_t first_sprite) {
    /* Metasprites are positioned by their pivot (the sprite center), in OAM coordinates. */
    uint8_t oam_x = player->x + player_sub_PIVOT_X + DEVICE_SPRITE_PX_OFFSET_X;
    uint8_t oam_y = player->y + player_sub_PIVOT_Y + DEVICE_SPRITE_PX_OFFSET_Y;

    if (player->facing_left) {
        return move_metasprite_flipx(player_sub_metasprites[0], SPRITE_TILE_PLAYER_SUB, 0,
                                     first_sprite, oam_x, oam_y);
    }
    return move_metasprite_ex(player_sub_metasprites[0], SPRITE_TILE_PLAYER_SUB, 0,
                              first_sprite, oam_x, oam_y);
}

void game_render(GameState *game) {
    uint8_t next_sprite = 0;

    next_sprite += render_player(&game->player, next_sprite);
    hide_sprites_range(next_sprite, MAX_HARDWARE_SPRITES);

    render_hud(game);
}
