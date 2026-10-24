#ifndef GAME_H
#define GAME_H

#include <stdint.h>

/*
 * Screen layout, in 8x8 background tiles (the screen is 20x18 tiles).
 * The screen never scrolls, so the HUD lives directly in the background map.
 */
enum ScreenLayout {
    SCREEN_TILES_WIDE   = 20,
    SCREEN_TILES_HIGH   = 18,
    SCREEN_PX_WIDTH     = SCREEN_TILES_WIDE * 8,

    ROW_SCORE           = 0,
    ROW_LIVES           = 1,
    ROW_SURFACE         = 2,
    ROW_WATER_FIRST     = 3,
    ROW_SEABED          = 15,
    ROW_OXYGEN          = 16,
    ROW_DIVERS          = 17,

    SCORE_DIGITS        = 6,
    SCORE_COLUMN        = 7,   /* centered: (20 - 6) / 2 */
    LIVES_COLUMN        = 1,
    OXYGEN_LABEL_COLUMN = 2,
    OXYGEN_BAR_COLUMN   = 5,
    OXYGEN_BAR_TILES    = 12,
    DIVERS_COLUMN       = 7
};

/*
 * Player sub tuning. Positions are the top-left corner of the sprite in
 * screen pixels (0,0 = top-left of the visible screen).
 */
enum PlayerTuning {
    PLAYER_WIDTH  = 16,
    PLAYER_HEIGHT = 8,
    PLAYER_SPEED  = 1,                        /* pixels per frame */

    PLAYER_MIN_X  = 0,
    PLAYER_MAX_X  = SCREEN_PX_WIDTH - PLAYER_WIDTH,
    PLAYER_MIN_Y  = ROW_SURFACE * 8 + 2,      /* hull just under the waterline */
    PLAYER_MAX_Y  = ROW_SEABED * 8 - PLAYER_HEIGHT,

    PLAYER_START_X = (SCREEN_PX_WIDTH - PLAYER_WIDTH) / 2,
    PLAYER_START_Y = PLAYER_MIN_Y
};

/*
 * Torpedo tuning. The sprite is 8x8 but the visible body is a 7x2 line on
 * rows 3-4, which lines up with the sub's hull when both share the same y.
 */
enum TorpedoTuning {
    TORPEDO_WIDTH        = 8,
    PLAYER_TORPEDO_SPEED = 4                  /* pixels per frame */
};

/* Game rules tuning (see GUIA_SEAQUEST_GB.md, sections 3.5-3.7). */
enum RulesTuning {
    MAX_DIVERS_ON_BOARD = 6,
    STARTING_RESERVES   = 3,
    MAX_RESERVES        = 6,

    /* One bar tile per 8 units so the bar can later get sub-tile precision. */
    OXYGEN_MAX          = OXYGEN_BAR_TILES * 8,
    OXYGEN_DRAIN_FRAMES = 10                  /* frames per unit: full tank ~16 s */
};

typedef struct {
    uint8_t held;      /* buttons down this frame (J_* bits) */
    uint8_t pressed;   /* buttons that went down this frame */
} InputState;

typedef struct {
    uint8_t x;
    uint8_t y;
    uint8_t facing_left;
} Player;

typedef struct {
    int16_t x;         /* signed: a torpedo can be partly off the left edge */
    uint8_t y;
    uint8_t active;
    uint8_t moving_left;
} Torpedo;

/* What the HUD currently shows, so render only touches VRAM on changes. */
typedef struct {
    uint32_t score;
    uint8_t oxygen_tiles;
    uint8_t divers;
    uint8_t reserves;
} HudCache;

typedef struct {
    Player player;
    Torpedo player_torpedo;   /* only one player torpedo on screen at a time */
    uint32_t score;
    uint8_t oxygen;
    uint8_t oxygen_drain_timer;
    uint8_t divers;
    uint8_t reserves;
    HudCache hud;
} GameState;

/* Loads tiles, draws the static background and resets the game. */
void game_init(GameState *game);
void game_update(GameState *game, const InputState *input);
void game_render(GameState *game);

#endif
