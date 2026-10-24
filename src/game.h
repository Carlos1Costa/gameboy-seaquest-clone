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
    PLAYER_START_Y = PLAYER_MIN_Y,

    /*
     * The sub counts as submerged once its hull is fully under the waterline
     * (PLAYER_MIN_Y is the surface). Coming back to PLAYER_MIN_Y after that is
     * a surfacing; wiggling near the surface is not.
     */
    PLAYER_SUBMERGED_Y = PLAYER_MIN_Y + PLAYER_HEIGHT
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
    DIVER_CHANCE              = 128,   /* out of 256, per spawned group */

    /*
     * CPU budget (not the scanline one): each enemy costs ~5 of the 154
     * scanlines in a frame. These caps keep the worst case near 80% of the
     * frame, leaving room for the remaining epics. See GUIA section 2.1.
     */
    MAX_ACTIVE_ENEMIES        = 8,
    MAX_ENEMY_TORPEDOES       = 2
};

enum EnemyTuning {
    SHARK_SPEED                 = 12,              /* 0.75 px/frame */
    ENEMY_SUB_SPEED             = 10,              /* 0.625 px/frame */
    DIVER_SPEED                 = 8,               /* 0.5 px/frame */

    SHARK_WAVE_AMPLITUDE        = 4,               /* px, see shark_wave in game.c */
    SHARK_WAVE_STEPS            = 32,
    SHARK_WAVE_FRAMES_PER_STEP  = 2,               /* full wave every 64 frames */

    /* Lane members never reach further than this from the lane's base_y. */
    LANE_REACH                  = ENTITY_HEIGHT + SHARK_WAVE_AMPLITUDE,

    ENEMY_FIRE_DELAY_MIN        = 60,
    ENEMY_FIRE_DELAY_RANDOM_MASK = 63,
    ENEMY_FIRE_RETRY_DELAY      = 8                /* no shot lined up: look again later */
};

/* Game rules tuning (see GUIA_SEAQUEST_GB.md, sections 3.5-3.7). */
enum RulesTuning {
    MAX_DIVERS_ON_BOARD = 6,
    STARTING_RESERVES   = 3,
    MAX_RESERVES        = 6,

    /* One bar tile per 8 units so the bar can later get sub-tile precision. */
    OXYGEN_MAX                = OXYGEN_BAR_TILES * 8,
    OXYGEN_DRAIN_FRAMES       = 10,           /* frames per unit: full tank ~16 s */
    OXYGEN_REFILL_PER_FRAME   = 1,            /* empty to full in ~1.6 s */
    OXYGEN_LOW                = 2 * 8,        /* last 2 bar tiles: the bar blinks */
    OXYGEN_ALARM_BLINK_FRAMES = 8,            /* frames on, then frames off */

    DEATH_FRAMES              = 90,           /* explosion length, world frozen */
    EXPLOSION_BLINK_FRAMES    = 4
};

/*
 * Scoring (GUIA 3.7). Values grow with each full load surfaced (the wave).
 * Divers are paid when a full load is surfaced: 6 x the diver value.
 */
enum ScoringTuning {
    ENEMY_POINTS_BASE   = 20,
    ENEMY_POINTS_STEP   = 10,
    ENEMY_POINTS_MAX    = 90,
    DIVER_POINTS_BASE   = 50,
    DIVER_POINTS_STEP   = 50,
    DIVER_POINTS_MAX    = 1000,

    /* An extra reserve each time the ten-thousands change (every 10,000 pts). */
    SCORE_TEN_THOUSANDS_DIGIT = SCORE_DIGITS - 5
};

/*
 * Surfacing with a full load (F1.2): the score is counted on screen. Divers
 * leave the HUD one by one, then the oxygen bar drains into bonus points;
 * only then does the tank refill.
 */
enum TallyTuning {
    TALLY_DIVER_FRAMES     = 12,   /* frames per diver counted */
    OXYGEN_BONUS_PER_UNIT  = 10,   /* full tank = 960 points */
    TALLY_TICK_UNITS       = 4     /* a tick sound every N oxygen units */
};

/*
 * The patrol sub (F1.1) sails the surface line from PATROL_FIRST_WAVE on.
 * It can't be torpedoed and kills on contact, also while refilling.
 */
enum PatrolTuning {
    PATROL_FIRST_WAVE        = 2,
    PATROL_SPEED             = 12,        /* 0.75 px/frame */
    PATROL_DELAY_MIN         = 90,        /* frames between passes */
    PATROL_DELAY_RANDOM_MASK = 63,
    PATROL_Y                 = PLAYER_MIN_Y
};

/* Difficulty (GUIA 3.8): enemies get faster with each wave. */
enum DifficultyTuning {
    ENEMY_SPEED_PER_WAVE = 2,                 /* +1/8 px/frame per wave */
    ENEMY_SPEED_MAX      = 2 * FIXED_ONE      /* 2 px/frame */
};

/*
 * True when first <= value < first + count. The SM83 has no signed-compare
 * flags, so each signed 16-bit comparison costs ~20 instructions; shifting the
 * range to start at 0 turns both bounds into one unsigned comparison.
 */
static inline uint8_t is_in_range(int16_t value, int16_t first, uint16_t count) {
    return (uint16_t)(value - first) < count;
}

/* A 16-px-wide entity at screen x is at least partly visible. */
static inline uint8_t entity_is_visible(int16_t x) {
    return is_in_range(x, -ENTITY_WIDTH + 1, SCREEN_PX_WIDTH + ENTITY_WIDTH - 1);
}

typedef enum {
    ENEMY_SHARK,
    ENEMY_SUB
} EnemyType;

typedef struct {
    uint8_t held;      /* buttons down this frame (J_* bits) */
    uint8_t pressed;   /* buttons that went down this frame */
} InputState;

typedef enum {
    PLAYER_TALLYING,   /* surfaced with a full load: counting points, frozen */
    PLAYER_REFILLING,  /* at the surface, frozen until the tank is full */
    PLAYER_ACTIVE,
    PLAYER_DYING       /* exploding; the whole world waits */
} PlayerState;

typedef struct {
    Fixed x;
    Fixed y;
    uint8_t facing_left;
    PlayerState state;
    uint8_t state_timer;   /* frames left in PLAYER_DYING; pacing in PLAYER_TALLYING */
    uint8_t submerged;     /* has dived since the last surfacing */
} Player;

typedef struct {
    Fixed x;
    uint8_t y;         /* torpedoes never move vertically */
    uint8_t active;
    uint8_t moving_left;
} Torpedo;

/*
 * A lane holds one group: same type, same direction, entering together, plus
 * an optional diver. The group moves as a unit: members trail the leader by
 * member_step and share its y, so a member is just an "alive" bit. Visit the
 * members with:
 *
 *   int16_t x = lane->leader_x;
 *   for (uint8_t bit = 1; bit <= lane->alive_mask; bit <<= 1, x += lane->member_step)
 *       if (lane->alive_mask & bit) ...
 *
 * The loop stops right after the last living member, so empty lanes cost
 * almost nothing; on this CPU, looping over dead slots was a large share of
 * every frame.
 */
typedef struct {
    uint8_t alive_mask;       /* bit i set: member i is alive; 0 = no group */
    Fixed speed;              /* set at spawn from the enemy type and the wave */
    Fixed group_x;            /* leader's (member 0) x, even after it is destroyed */
    int16_t leader_x;         /* group_x in pixels, refreshed every frame */
    int8_t member_step;       /* px from one member to the next (behind the leader) */
    uint8_t enemy_y;          /* shared by all members; sharks bob around base_y */
    EnemyType enemy_type;
    uint8_t moving_left;      /* shared by the group and the diver */

    uint8_t diver_active;
    Fixed diver_fixed_x;      /* divers swim slower than the group */
    int16_t diver_x;          /* diver_fixed_x in pixels; the diver's y is base_y */

    Torpedo torpedo;          /* one enemy torpedo per lane keeps the scanline budget */
    uint8_t base_y;
    uint8_t spawn_timer;      /* frames left before the next group, while empty */
    uint8_t fire_timer;       /* frames left before an enemy sub may fire */
} Lane;

typedef struct {
    uint8_t active;
    uint8_t moving_left;
    Fixed fixed_x;
    int16_t x;                /* fixed_x in pixels; y is PATROL_Y */
    uint8_t timer;            /* frames until the next pass, while inactive */
} Patrol;

/* Text banner across the water; while one is up, sprites are hidden. */
typedef enum {
    BANNER_NONE,
    BANNER_TITLE,
    BANNER_GAME_OVER
} Banner;

/* What the HUD currently shows, so render only touches VRAM on changes. */
typedef struct {
    uint8_t oxygen_tiles;
    uint8_t divers;
    uint8_t reserves;
    Banner banner;
} HudCache;

typedef struct {
    Player player;
    Torpedo player_torpedo;   /* only one player torpedo on screen at a time */
    Lane lanes[LANE_COUNT];
    Patrol patrol;
    uint8_t player_hit;       /* set by collisions, handled at the end of the update */
    uint8_t enemies_destroyed; /* set by collisions this frame, scored by game.c */
    uint8_t game_over;        /* the last life is gone */
    Banner banner;
    uint8_t frame;            /* wraps; drives animations like the shark wave */
    /*
     * Decimal digits, most significant first. Extracting digits from a
     * uint32_t takes 32-bit divisions in software, which cost over a full
     * frame for 6 digits; digits make drawing the score a plain copy.
     */
    uint8_t score_digits[SCORE_DIGITS];
    uint8_t score_changed;    /* the HUD redraws the score when set */
    uint8_t oxygen;
    uint8_t oxygen_drain_timer;
    uint8_t divers;
    uint8_t reserves;
    uint8_t wave;             /* surfacings with a full load; drives difficulty (E1.4) */
    HudCache hud;
} GameState;

/* Loads tiles, draws the static background and resets the game. */
void game_init(GameState *game);
/* Shows the title banner over the (frozen) ocean. */
void game_show_title(GameState *game);
/* Starts a new game; the seed should come from player timing (e.g. DIV when START is pressed). */
void game_start(GameState *game, uint16_t seed);
void game_update(GameState *game, const InputState *input);
/*
 * Call right after vsync(): the HUD writes to VRAM first, while the VBlank
 * period lets them through without waiting; sprites go to shadow OAM, which
 * is copied to the hardware at the next VBlank.
 */
void game_render(GameState *game);

#endif
