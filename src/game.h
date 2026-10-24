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
 * Signed 12.4 fixed point: 1/16 pixel precision, range +-2048 px.
 * Signed because entities spawn off screen (negative x on the left edge).
 */
typedef int16_t Fixed;

enum FixedPoint {
    FIXED_SHIFT = 4,
    FIXED_ONE   = 1 << FIXED_SHIFT     /* 1 pixel */
};

static inline int16_t fixed_to_pixel(Fixed value) {
    return value >> FIXED_SHIFT;
}

static inline Fixed pixel_to_fixed(int16_t pixel) {
    return pixel * FIXED_ONE;
}

/*
 * Positions are the top-left corner of the sprite in screen pixels
 * (0,0 = top-left of the visible screen). Speeds are Fixed per frame.
 */
enum PlayerTuning {
    PLAYER_WIDTH   = 16,
    PLAYER_HEIGHT  = 8,
    PLAYER_SPEED   = FIXED_ONE,                /* 1 px/frame */

    PLAYER_MIN_X   = 0,
    PLAYER_MAX_X   = SCREEN_PX_WIDTH - PLAYER_WIDTH,
    PLAYER_MIN_Y   = ROW_SURFACE * 8 + 2,      /* hull just under the waterline */
    PLAYER_MAX_Y   = ROW_SEABED * 8 - PLAYER_HEIGHT,

    PLAYER_START_X = (SCREEN_PX_WIDTH - PLAYER_WIDTH) / 2,
    PLAYER_START_Y = PLAYER_MIN_Y
};

/*
 * Torpedo tuning. The sprite is 8x8 but the visible body is a 7x2 line on
 * rows 3-4, which lines up with a sub's hull when both share the same y.
 */
enum TorpedoTuning {
    TORPEDO_WIDTH        = 8,
    PLAYER_TORPEDO_SPEED = 4 * FIXED_ONE,
    ENEMY_TORPEDO_SPEED  = 2 * FIXED_ONE
};

/*
 * Enemies and divers travel in horizontal lanes. Lanes are LANE_SPACING apart
 * so a shark oscillating +-SHARK_WAVE_AMPLITUDE never enters the next lane.
 *
 * Sprite-per-scanline budget (hardware limit is 10): a lane holds at most
 * LANE_MAX_ENTITIES 16x8 entities (2 sprites each) plus its enemy torpedo,
 * and the player's sub and torpedo may share the same line:
 *   3 * 2 + 1 + 2 + 1 = 10.
 */
enum LaneTuning {
    LANE_COUNT                = 4,
    LANE_FIRST_Y              = 30,
    LANE_SPACING              = 22,
    LANE_MAX_ENTITIES         = 3,     /* enemies + diver */

    ENTITY_WIDTH              = 16,
    ENTITY_HEIGHT             = 8,
    GROUP_SPACING             = 24,    /* px between members of a group */

    SPAWN_DELAY_MIN           = 60,    /* frames a lane stays empty */
    SPAWN_DELAY_RANDOM_MASK   = 127,   /* plus 0..127 random frames */
    LANE_FIRST_SPAWN_DELAY    = 30,
    LANE_FIRST_SPAWN_STAGGER  = 45,    /* so lanes don't all fill at once */
    DIVER_CHANCE              = 128    /* out of 256, per spawned group */
};

enum EnemyTuning {
    SHARK_SPEED                 = 12,              /* 0.75 px/frame */
    ENEMY_SUB_SPEED             = 10,              /* 0.625 px/frame */
    DIVER_SPEED                 = 8,               /* 0.5 px/frame */

    SHARK_WAVE_AMPLITUDE        = 4,               /* px, see shark_wave in game.c */
    SHARK_WAVE_STEPS            = 32,
    SHARK_WAVE_FRAMES_PER_STEP  = 2,               /* full wave every 64 frames */
    SHARK_WAVE_MEMBER_OFFSET    = 8,               /* steps between group members */

    ENEMY_FIRE_DELAY_MIN        = 60,
    ENEMY_FIRE_DELAY_RANDOM_MASK = 63
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

typedef enum {
    ENEMY_SHARK,
    ENEMY_SUB
} EnemyType;

typedef struct {
    uint8_t held;      /* buttons down this frame (J_* bits) */
    uint8_t pressed;   /* buttons that went down this frame */
} InputState;

typedef struct {
    Fixed x;
    Fixed y;
    uint8_t facing_left;
} Player;

typedef struct {
    Fixed x;
    uint8_t y;         /* torpedoes never move vertically */
    uint8_t active;
    uint8_t moving_left;
} Torpedo;

/* An enemy or a diver. y is recomputed every frame from the lane. */
typedef struct {
    Fixed x;
    uint8_t y;
    uint8_t active;
} LaneEntity;

/* A lane holds one group: same type, same direction, entering together. */
typedef struct {
    LaneEntity enemies[LANE_MAX_ENTITIES];
    LaneEntity diver;
    Torpedo torpedo;          /* one enemy torpedo per lane keeps the scanline budget */
    EnemyType enemy_type;
    uint8_t moving_left;
    uint8_t spawn_timer;      /* frames left before the next group, while empty */
    uint8_t fire_timer;       /* frames left before an enemy sub may fire */
} Lane;

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
    Lane lanes[LANE_COUNT];
    uint8_t player_hit;       /* set by collisions, handled at the end of the update */
    uint8_t frame;            /* wraps; drives animations like the shark wave */
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
