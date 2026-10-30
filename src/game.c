#include <gb/gb.h>
#include <rand.h>

#include "game.h"
#include "collision.h"
#include "sound.h"
#include "player_sub.h"
#include "torpedo.h"
#include "shark.h"
#include "enemy_sub.h"
#include "diver.h"
#include "explosion.h"
#include "hud_tiles.h"

/* First VRAM tile of each sprite sheet. */
enum SpriteTile {
    SPRITE_TILE_PLAYER_SUB = 0,
    SPRITE_TILE_TORPEDO    = SPRITE_TILE_PLAYER_SUB + player_sub_TILE_COUNT,
    SPRITE_TILE_SHARK      = SPRITE_TILE_TORPEDO + torpedo_TILE_COUNT,
    SPRITE_TILE_ENEMY_SUB  = SPRITE_TILE_SHARK + shark_TILE_COUNT,
    SPRITE_TILE_DIVER      = SPRITE_TILE_ENEMY_SUB + enemy_sub_TILE_COUNT,
    SPRITE_TILE_EXPLOSION  = SPRITE_TILE_DIVER + diver_TILE_COUNT
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
    BG_TILE_LETTER_FIRST = 18   /* the letters in letter_glyphs, in that order */
};

/* Letters with a glyph in res/hud_tiles.png, in tile order. */
static const char letter_glyphs[] = "AEGMNOPQRSTUVXY";

/* Title / GAME OVER banner across the water, as a light band like the HUD. */
enum BannerLayout {
    BANNER_FIRST_ROW = 7,
    BANNER_ROWS      = 5,
    BANNER_TITLE_ROW = 8,
    BANNER_HINT_ROW  = 10
};

/* One period of round(SHARK_WAVE_AMPLITUDE * sin(t)), in SHARK_WAVE_STEPS steps. */
static const int8_t shark_wave[SHARK_WAVE_STEPS] = {
     0,  1,  2,  2,  3,  3,  4,  4,  4,  4,  4,  3,  3,  2,  2,  1,
     0, -1, -2, -2, -3, -3, -4, -4, -4, -4, -4, -3, -3, -2, -2, -1
};

/* Characters without a glyph (including space) draw as the plain HUD tile. */
static uint8_t tile_for_char(char c) {
    if (c >= '0' && c <= '9') {
        return BG_TILE_DIGIT_0 + (c - '0');
    }
    for (uint8_t i = 0; letter_glyphs[i] != '\0'; i++) {
        if (letter_glyphs[i] == c) {
            return BG_TILE_LETTER_FIRST + i;
        }
    }
    return BG_TILE_HUD;
}

static void draw_text(uint8_t column, uint8_t row, const char *text) {
    uint8_t tiles[SCREEN_TILES_WIDE];
    uint8_t length = 0;
    while (text[length] != '\0' && length < SCREEN_TILES_WIDE) {
        tiles[length] = tile_for_char(text[length]);
        length++;
    }
    set_bkg_tiles(column, row, length, 1, tiles);
}

/* Centers text on the 20-tile-wide screen. */
static void draw_centered_text(uint8_t row, const char *text, uint8_t length) {
    draw_text((SCREEN_TILES_WIDE - length) / 2, row, text);
}

static void draw_static_background(void) {
    const uint8_t water_rows = ROW_SEABED - ROW_WATER_FIRST;

    fill_bkg_rect(0, ROW_SCORE, SCREEN_TILES_WIDE, ROW_SURFACE - ROW_SCORE, BG_TILE_HUD);
    fill_bkg_rect(0, ROW_SURFACE, SCREEN_TILES_WIDE, 1, BG_TILE_SURFACE);
    fill_bkg_rect(0, ROW_WATER_FIRST, SCREEN_TILES_WIDE, water_rows, BG_TILE_WATER);
    fill_bkg_rect(0, ROW_SEABED, SCREEN_TILES_WIDE, 1, BG_TILE_SEABED);
    fill_bkg_rect(0, ROW_OXYGEN, SCREEN_TILES_WIDE, SCREEN_TILES_HIGH - ROW_OXYGEN, BG_TILE_HUD);
    draw_text(OXYGEN_LABEL_COLUMN, ROW_OXYGEN, "OXYGEN");
}

static void reset_hud_cache(HudCache *hud) {
    /* Values the game can never reach, so the first render draws everything. */
    hud->oxygen_tiles = 0xFF;
    hud->divers = 0xFF;
    hud->reserves = 0xFF;
    hud->banner = BANNER_NONE;
}

/* Empties every lane and staggers their first spawns. */
static void reset_lanes(GameState *game) {
    for (uint8_t i = 0; i < LANE_COUNT; i++) {
        Lane *lane = &game->lanes[i];
        lane->alive_mask = 0;
        lane->diver_active = 0;
        lane->torpedo.active = 0;
        lane->base_y = LANE_FIRST_Y + i * LANE_SPACING;
        lane->spawn_timer = LANE_FIRST_SPAWN_DELAY + i * LANE_FIRST_SPAWN_STAGGER;
    }
}

/* Every life starts at the surface with an empty tank, frozen until it fills (as in the original). */
static void reset_patrol(Patrol *patrol) {
    patrol->active = 0;
    patrol->moving_left = 1;      /* toggled at each pass: the first one enters from the left */
    patrol->timer = PATROL_DELAY_MIN;
}

static void respawn_player(GameState *game) {
    Player *player = &game->player;
    player->x = pixel_to_fixed(PLAYER_START_X);
    player->y = pixel_to_fixed(PLAYER_START_Y);
    player->facing_left = 0;
    player->state = PLAYER_REFILLING;
    player->submerged = 0;
    game->player_torpedo.active = 0;
    game->player_hit = 0;
    game->enemies_destroyed = 0;
    game->oxygen = 0;
    /* Give the new life a moment at the surface before the patrol comes by. */
    reset_patrol(&game->patrol);
}

static void reset_game(GameState *game) {
    respawn_player(game);
    reset_lanes(game);
    for (uint8_t i = 0; i < SCORE_DIGITS; i++) {
        game->score_digits[i] = 0;
    }
    game->score_changed = 1;
    game->divers = 0;
    game->reserves = STARTING_RESERVES;
    game->wave = 0;
    game->game_over = 0;
    game->banner = BANNER_NONE;
}

void game_init(GameState *game) {
    set_sprite_data(SPRITE_TILE_PLAYER_SUB, player_sub_TILE_COUNT, player_sub_tiles);
    set_sprite_data(SPRITE_TILE_TORPEDO, torpedo_TILE_COUNT, torpedo_tiles);
    set_sprite_data(SPRITE_TILE_SHARK, shark_TILE_COUNT, shark_tiles);
    set_sprite_data(SPRITE_TILE_ENEMY_SUB, enemy_sub_TILE_COUNT, enemy_sub_tiles);
    set_sprite_data(SPRITE_TILE_DIVER, diver_TILE_COUNT, diver_tiles);
    set_sprite_data(SPRITE_TILE_EXPLOSION, explosion_TILE_COUNT, explosion_tiles);
    set_bkg_data(0, hud_tiles_TILE_COUNT, hud_tiles_tiles);
    draw_static_background();

    game->frame = 0;
    reset_game(game);
    reset_hud_cache(&game->hud);
}

void game_show_title(GameState *game) {
    game->banner = BANNER_TITLE;
}

void game_start(GameState *game, uint16_t seed) {
    initrand(seed);
    reset_game(game);
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
    sound_play(SOUND_TORPEDO);
}

static void update_torpedo(Torpedo *torpedo, Fixed speed) {
    int16_t x;

    if (!torpedo->active) {
        return;
    }
    torpedo->x += torpedo->moving_left ? -speed : speed;
    x = fixed_to_pixel(torpedo->x);
    if (!is_in_range(x, -TORPEDO_WIDTH + 1, SCREEN_PX_WIDTH + TORPEDO_WIDTH - 1)) {
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

/* What the lanes may still add this frame under the global caps. */
typedef struct {
    uint8_t enemies_left;
    uint8_t torpedoes_left;
} SpawnBudget;

static uint8_t count_members(uint8_t alive_mask) {
    return (alive_mask & 1) + ((alive_mask >> 1) & 1) + ((alive_mask >> 2) & 1);
}

static Fixed enemy_speed(EnemyType type, uint8_t wave) {
    Fixed speed = (type == ENEMY_SHARK) ? SHARK_SPEED : ENEMY_SUB_SPEED;
    speed += wave * ENEMY_SPEED_PER_WAVE;
    return (speed > ENEMY_SPEED_MAX) ? ENEMY_SPEED_MAX : speed;
}

static void spawn_group(Lane *lane, SpawnBudget *budget, uint8_t wave) {
    uint8_t roll = rand();
    uint8_t has_diver = rand() < DIVER_CHANCE;
    /* The diver takes one of the lane's entity slots. */
    uint8_t max_enemies = has_diver ? LANE_MAX_ENTITIES - 1 : LANE_MAX_ENTITIES;
    uint8_t enemy_count = 1 + rand() % max_enemies;
    uint8_t moving_left = roll & 1;

    if (enemy_count > budget->enemies_left) {
        enemy_count = budget->enemies_left;
    }
    budget->enemies_left -= enemy_count;

    lane->moving_left = moving_left;
    lane->enemy_type = (roll & 2) ? ENEMY_SUB : ENEMY_SHARK;
    lane->speed = enemy_speed(lane->enemy_type, wave);
    lane->alive_mask = (uint8_t)((1 << enemy_count) - 1);
    lane->group_x = group_entry_x(moving_left, 0);
    lane->leader_x = fixed_to_pixel(lane->group_x);
    /* Members queue up behind the leader, opposite to the direction of travel. */
    lane->member_step = moving_left ? GROUP_SPACING : -GROUP_SPACING;
    lane->enemy_y = lane->base_y;

    lane->diver_active = has_diver;
    lane->diver_fixed_x = group_entry_x(moving_left, enemy_count);
    lane->diver_x = fixed_to_pixel(lane->diver_fixed_x);

    lane->fire_timer = ENEMY_FIRE_DELAY_MIN + (rand() & ENEMY_FIRE_DELAY_RANDOM_MASK);
}

/*
 * For an entity already known to be off screen: true when it is on the side
 * it is heading to, i.e. it has crossed (otherwise it is still queued to enter).
 * A sign test is cheap on the SM83.
 */
static inline uint8_t is_past_far_edge(int16_t x, uint8_t moving_left) {
    return moving_left ? x < 0 : x >= 0;
}

static inline uint8_t has_crossed_screen(int16_t x, uint8_t moving_left) {
    return !entity_is_visible(x) && is_past_far_edge(x, moving_left);
}

static uint8_t is_fully_on_screen(int16_t x) {
    return is_in_range(x, 0, SCREEN_PX_WIDTH - ENTITY_WIDTH + 1);
}

static void update_group(Lane *lane, uint8_t frame) {
    Fixed speed = lane->speed;
    uint8_t moving_left = lane->moving_left;
    int16_t x;

    lane->group_x += moving_left ? -speed : speed;
    x = fixed_to_pixel(lane->group_x);
    lane->leader_x = x;

    /* A shark group bobs in sync, as in the original. */
    lane->enemy_y = lane->base_y;
    if (lane->enemy_type == ENEMY_SHARK) {
        /* Unsigned divisor: enum constants are int, and a signed division can't become a plain shift. */
        uint8_t wave_step = frame / (uint8_t)SHARK_WAVE_FRAMES_PER_STEP;
        lane->enemy_y += shark_wave[wave_step & (SHARK_WAVE_STEPS - 1)];
    }

    /* Members cross the far edge in order, leader first: stop at the first still on its way. */
    for (uint8_t bit = 1; bit <= lane->alive_mask; bit <<= 1, x += lane->member_step) {
        if (!has_crossed_screen(x, moving_left)) {
            break;
        }
        lane->alive_mask &= (uint8_t)~bit;
    }
}

static void update_diver(Lane *lane) {
    lane->diver_fixed_x += lane->moving_left ? -DIVER_SPEED : DIVER_SPEED;
    lane->diver_x = fixed_to_pixel(lane->diver_fixed_x);
    if (has_crossed_screen(lane->diver_x, lane->moving_left)) {
        lane->diver_active = 0;
    }
}

/* An enemy sub fires along its heading, only when the player is ahead of it. */
static void update_enemy_fire(Lane *lane, int16_t player_x, SpawnBudget *budget) {
    if (lane->enemy_type != ENEMY_SUB || lane->alive_mask == 0 || lane->torpedo.active
        || budget->torpedoes_left == 0) {
        return;
    }
    if (lane->fire_timer != 0) {
        lane->fire_timer--;
        return;
    }
    int16_t x = lane->leader_x;
    for (uint8_t bit = 1; bit <= lane->alive_mask; bit <<= 1, x += lane->member_step) {
        uint8_t player_ahead = lane->moving_left ? (player_x < x) : (player_x > x);

        if ((lane->alive_mask & bit) && is_fully_on_screen(x) && player_ahead) {
            launch_torpedo(&lane->torpedo, x, lane->enemy_y, lane->moving_left);
            budget->torpedoes_left--;
            lane->fire_timer = ENEMY_FIRE_DELAY_MIN + (rand() & ENEMY_FIRE_DELAY_RANDOM_MASK);
            return;
        }
    }
    /* Scanning is costly; skip a few frames instead of retrying every frame. */
    lane->fire_timer = ENEMY_FIRE_RETRY_DELAY;
}

static void update_lane(Lane *lane, uint8_t frame, uint8_t wave, int16_t player_x,
                        SpawnBudget *budget) {
    if (lane->alive_mask == 0 && !lane->diver_active) {
        if (lane->spawn_timer != 0) {
            lane->spawn_timer--;
        } else {
            /* At the enemy cap, wait a full delay before trying again. */
            if (budget->enemies_left != 0) {
                spawn_group(lane, budget, wave);
            }
            lane->spawn_timer = SPAWN_DELAY_MIN + (rand() & SPAWN_DELAY_RANDOM_MASK);
        }
    }
    if (lane->alive_mask != 0) {
        update_group(lane, frame);
    }
    if (lane->diver_active) {
        update_diver(lane);
    }
    update_torpedo(&lane->torpedo, ENEMY_TORPEDO_SPEED);
    update_enemy_fire(lane, player_x, budget);
}

static void compute_spawn_budget(const GameState *game, SpawnBudget *budget) {
    budget->enemies_left = MAX_ACTIVE_ENEMIES;
    budget->torpedoes_left = MAX_ENEMY_TORPEDOES;
    for (uint8_t i = 0; i < LANE_COUNT; i++) {
        const Lane *lane = &game->lanes[i];
        budget->enemies_left -= count_members(lane->alive_mask);
        budget->torpedoes_left -= lane->torpedo.active;
    }
}

static uint16_t enemy_points(uint8_t wave) {
    uint16_t points = ENEMY_POINTS_BASE + wave * ENEMY_POINTS_STEP;
    return (points > ENEMY_POINTS_MAX) ? ENEMY_POINTS_MAX : points;
}

static uint16_t diver_points(uint8_t wave) {
    uint16_t points = DIVER_POINTS_BASE + wave * DIVER_POINTS_STEP;
    return (points > DIVER_POINTS_MAX) ? DIVER_POINTS_MAX : points;
}

/* The score in units of 10,000 (its two leading digits). */
static uint8_t score_ten_thousands(const uint8_t *digits) {
    return digits[SCORE_TEN_THOUSANDS_DIGIT - 1] * 10 + digits[SCORE_TEN_THOUSANDS_DIGIT];
}

/*
 * Place values of score_digits[1..5]. A single award never reaches 100,000
 * (the largest is a 1,000-point diver), so the leading digit only changes
 * through carries.
 */
static const uint16_t score_place_values[SCORE_DIGITS - 1] = { 10000, 1000, 100, 10, 1 };

/*
 * Decimal add. The SM83 has no divide instruction, so % 10 and / 10 are
 * slow software routines (they cost ~15 scanlines per kill); the points are
 * split into digits by repeated subtraction instead, at most 9 per place.
 * Every 10,000 points earn a reserve, up to MAX_RESERVES.
 */
static void add_score(GameState *game, uint16_t points) {
    uint8_t *digits = game->score_digits;
    uint8_t ten_thousands_before = score_ten_thousands(digits);
    uint8_t points_digits[SCORE_DIGITS] = { 0 };
    uint8_t carry = 0;

    for (uint8_t i = 1; i < SCORE_DIGITS; i++) {
        uint16_t place = score_place_values[i - 1];
        while (points >= place) {
            points -= place;
            points_digits[i]++;
        }
    }
    for (int8_t i = SCORE_DIGITS - 1; i >= 0; i--) {
        uint8_t sum = digits[i] + points_digits[i] + carry;
        carry = sum >= 10;
        digits[i] = carry ? sum - 10 : sum;
    }
    if (carry) {
        /* Past 999999: stay at the maximum instead of wrapping to zero. */
        for (uint8_t i = 0; i < SCORE_DIGITS; i++) {
            digits[i] = 9;
        }
    }
    game->score_changed = 1;

    for (uint8_t earned = score_ten_thousands(digits) - ten_thousands_before; earned != 0; earned--) {
        if (game->reserves < MAX_RESERVES) {
            game->reserves++;
            sound_play(SOUND_EXTRA_LIFE);
        }
    }
}

/* Sails the surface from PATROL_FIRST_WAVE on, alternating sides between passes. */
static void update_patrol(Patrol *patrol, uint8_t wave) {
    if (!patrol->active) {
        if (wave < PATROL_FIRST_WAVE || --patrol->timer != 0) {
            return;
        }
        patrol->active = 1;
        patrol->moving_left = !patrol->moving_left;
        patrol->fixed_x = pixel_to_fixed(patrol->moving_left ? SCREEN_PX_WIDTH : -ENTITY_WIDTH);
    }
    patrol->fixed_x += patrol->moving_left ? -PATROL_SPEED : PATROL_SPEED;
    patrol->x = fixed_to_pixel(patrol->fixed_x);
    if (has_crossed_screen(patrol->x, patrol->moving_left)) {
        patrol->active = 0;
        patrol->timer = PATROL_DELAY_MIN + (rand() & PATROL_DELAY_RANDOM_MASK);
    }
}

/* Losing the sub loses every diver on board, as in the original. */
static void start_dying(GameState *game) {
    game->player.state = PLAYER_DYING;
    game->player.state_timer = DEATH_FRAMES;
    game->player_torpedo.active = 0;
    game->divers = 0;
    sound_play(SOUND_PLAYER_EXPLOSION);
}

/* Once the explosion ends: lose a reserve and restart the round, or end the game. */
static void update_dying(GameState *game) {
    if (--game->player.state_timer != 0) {
        return;
    }
    if (game->reserves == 0) {
        game->game_over = 1;
        game->banner = BANNER_GAME_OVER;
        return;
    }
    game->reserves--;
    reset_lanes(game);
    respawn_player(game);
}

static void update_refill(GameState *game) {
    game->oxygen += OXYGEN_REFILL_PER_FRAME;
    if ((game->oxygen & (OXYGEN_REFILL_TICK_UNITS - 1)) == 0) {
        sound_play_oxygen_refill(game->oxygen);
    }
    if (game->oxygen >= OXYGEN_MAX) {
        game->oxygen = OXYGEN_MAX;
        game->oxygen_drain_timer = OXYGEN_DRAIN_FRAMES;
        game->player.state = PLAYER_ACTIVE;
    }
}

/* Surfacing rules (GUIA 3.5): the payload decides the outcome. */
static void surface(GameState *game) {
    game->player.submerged = 0;
    if (game->divers == 0) {
        start_dying(game);
        return;
    }
    if (game->divers == MAX_DIVERS_ON_BOARD) {
        /* Full load: the ocean clears and the points are counted (update_tally). */
        reset_lanes(game);
        game->player.state = PLAYER_TALLYING;
        game->player.state_timer = TALLY_DIVER_FRAMES;
        return;
    }
    /* Coming up early costs one rescued diver. */
    game->divers--;
    game->player.state = PLAYER_REFILLING;
}

/*
 * Counts a full load on screen: each diver leaves the HUD for its points,
 * then the remaining oxygen drains into bonus points. Diver values use the
 * wave being finished; the next wave starts as the tank refills.
 */
static void update_tally(GameState *game) {
    Player *player = &game->player;

    if (game->divers != 0) {
        if (--player->state_timer == 0) {
            player->state_timer = TALLY_DIVER_FRAMES;
            game->divers--;
            add_score(game, diver_points(game->wave));
            sound_play(SOUND_TALLY);
        }
        return;
    }
    if (game->oxygen != 0) {
        game->oxygen--;
        add_score(game, OXYGEN_BONUS_PER_UNIT);
        if (game->oxygen % (uint8_t)TALLY_TICK_UNITS == 0) {
            sound_play(SOUND_TALLY);
        }
        return;
    }
    game->wave++;
    player->state = PLAYER_REFILLING;
}

static void update_surfacing(GameState *game) {
    Player *player = &game->player;
    int16_t y = fixed_to_pixel(player->y);

    if (y >= PLAYER_SUBMERGED_Y) {
        player->submerged = 1;
    } else if (player->submerged && y == PLAYER_MIN_Y) {
        surface(game);
    }
}

/* Drains only while submerged; the tank runs dry -> the sub is lost. */
static void update_oxygen(GameState *game) {
    if (!game->player.submerged) {
        return;
    }
    /* Beep each time the low-oxygen bar blinks back on. */
    if (game->oxygen <= OXYGEN_LOW && game->frame % (uint8_t)(2 * OXYGEN_ALARM_BLINK_FRAMES) == 0) {
        sound_play(SOUND_OXYGEN_ALARM);
    }
    if (--game->oxygen_drain_timer != 0) {
        return;
    }
    game->oxygen_drain_timer = OXYGEN_DRAIN_FRAMES;
    if (--game->oxygen == 0) {
        start_dying(game);
    }
}

void game_update(GameState *game, const InputState *input) {
    SpawnBudget budget;
    Player *player = &game->player;
    int16_t player_x;
    uint8_t divers_before;

    /* The clock stops with the world: nothing animates during the explosion. */
    if (player->state == PLAYER_DYING) {
        update_dying(game);
        return;
    }
    game->frame++;
    if (player->state == PLAYER_REFILLING) {
        update_refill(game);
    } else if (player->state == PLAYER_TALLYING) {
        update_tally(game);
    } else {
        update_player(player, input);
        fire_player_torpedo(&game->player_torpedo, player, input);
    }
    update_torpedo(&game->player_torpedo, PLAYER_TORPEDO_SPEED);

    player_x = fixed_to_pixel(player->x);
    compute_spawn_budget(game, &budget);
    for (uint8_t i = 0; i < LANE_COUNT; i++) {
        update_lane(&game->lanes[i], game->frame, game->wave, player_x, &budget);
    }
    update_patrol(&game->patrol, game->wave);

    divers_before = game->divers;
    collisions_update(game);
    if (game->divers > divers_before) {
        sound_play(SOUND_RESCUE);
    }
    if (game->enemies_destroyed != 0) {
        add_score(game, game->enemies_destroyed * enemy_points(game->wave));
        game->enemies_destroyed = 0;
        sound_play(SOUND_ENEMY_EXPLOSION);
    }
    if (game->player_hit) {
        start_dying(game);
        return;
    }
    if (player->state == PLAYER_ACTIVE) {
        update_surfacing(game);
        update_oxygen(game);
    }
}

static void draw_score(const uint8_t *digits) {
    uint8_t tiles[SCORE_DIGITS];
    for (uint8_t i = 0; i < SCORE_DIGITS; i++) {
        tiles[i] = BG_TILE_DIGIT_0 + digits[i];
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

/* Low-oxygen alarm: while draining, the last tiles blink. */
static uint8_t is_oxygen_alarm_off_phase(const GameState *game) {
    return game->player.submerged && game->oxygen <= OXYGEN_LOW
        && (game->frame / (uint8_t)OXYGEN_ALARM_BLINK_FRAMES) & 1;
}

static void draw_banner(Banner banner) {
    if (banner == BANNER_NONE) {
        fill_bkg_rect(0, BANNER_FIRST_ROW, SCREEN_TILES_WIDE, BANNER_ROWS, BG_TILE_WATER);
        return;
    }
    fill_bkg_rect(0, BANNER_FIRST_ROW, SCREEN_TILES_WIDE, BANNER_ROWS, BG_TILE_HUD);
    if (banner == BANNER_TITLE) {
        draw_centered_text(BANNER_TITLE_ROW, "SEAQUEST", 8);
    } else {
        draw_centered_text(BANNER_TITLE_ROW, "GAME OVER", 9);
    }
    draw_centered_text(BANNER_HINT_ROW, "PRESS START", 11);
}

static void render_hud(GameState *game) {
    HudCache *hud = &game->hud;
    /* Round up so the last tile only empties when oxygen is truly zero. */
    uint8_t oxygen_tiles = (uint8_t)(game->oxygen + 7) >> 3;

    if (is_oxygen_alarm_off_phase(game)) {
        oxygen_tiles = 0;
    }

    if (game->score_changed) {
        game->score_changed = 0;
        draw_score(game->score_digits);
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
    if (hud->banner != game->banner) {
        hud->banner = game->banner;
        draw_banner(game->banner);
    }
}

/*
 * Every sprite asset is a horizontal strip of 8x8 tiles: entities are 2 tiles
 * (left, right) per animation frame, torpedoes are 1. Writing those straight
 * into the shadow OAM is much cheaper than GBDK's generic metasprite routine.
 */
_Static_assert(player_sub_TILE_COUNT == 2 * ENTITY_TILES_PER_FRAME, "player_sub.png must be 2 frames");
_Static_assert(shark_TILE_COUNT == 2 * ENTITY_TILES_PER_FRAME, "shark.png must be 2 frames");
_Static_assert(enemy_sub_TILE_COUNT == 2 * ENTITY_TILES_PER_FRAME, "enemy_sub.png must be 2 frames");
_Static_assert(diver_TILE_COUNT == 2 * ENTITY_TILES_PER_FRAME, "diver.png must be 2 frames");
_Static_assert(explosion_TILE_COUNT == EXPLOSION_FRAMES * ENTITY_TILES_PER_FRAME,
               "explosion.png must have EXPLOSION_FRAMES frames");
_Static_assert(torpedo_TILE_COUNT == 1, "torpedo.png must be 8x8");
_Static_assert((ANIM_PERIOD_PROPELLER & (ANIM_PERIOD_PROPELLER - 1)) == 0, "must be a power of 2");
_Static_assert((ANIM_PERIOD_SHARK & (ANIM_PERIOD_SHARK - 1)) == 0, "must be a power of 2");
_Static_assert((ANIM_PERIOD_DIVER & (ANIM_PERIOD_DIVER - 1)) == 0, "must be a power of 2");

/* Tile offset of the current pose of a 2-frame animation. */
static inline uint8_t anim_tile_offset(uint8_t clock, uint8_t period) {
    return (clock & period) ? ENTITY_TILES_PER_FRAME : 0;
}

/* First tile of the explosion frame: spark, then burst, then cloud. */
static uint8_t explosion_tile(uint8_t timer) {
    if (timer > 2 * EXPLOSION_FRAME_LENGTH) {
        return SPRITE_TILE_EXPLOSION;
    }
    if (timer > EXPLOSION_FRAME_LENGTH) {
        return SPRITE_TILE_EXPLOSION + ENTITY_TILES_PER_FRAME;
    }
    return SPRITE_TILE_EXPLOSION + 2 * ENTITY_TILES_PER_FRAME;
}

static inline void set_oam(volatile OAM_item_t *oam, uint8_t x, uint8_t y, uint8_t tile, uint8_t prop) {
    oam->y = y;
    oam->x = x;
    oam->tile = tile;
    oam->prop = prop;
}

/*
 * Draws a right-facing 16x8 entity (mirrored when facing left) with its
 * top-left corner at screen (x, y). Returns how many hardware sprites were used.
 */
static uint8_t render_entity(uint8_t first_tile, int16_t x, uint8_t y, uint8_t facing_left,
                             uint8_t sprite) {
    volatile OAM_item_t *oam = &shadow_OAM[sprite];
    uint8_t left_tile = first_tile;
    uint8_t right_tile = first_tile + 1;
    uint8_t prop = 0;

    /* Fully off screen: skip, so queued group members don't use OAM slots. */
    if (!entity_is_visible(x)) {
        return 0;
    }
    /* Mirroring a 2-tile sprite = flip each tile and swap their order. */
    if (facing_left) {
        left_tile = first_tile + 1;
        right_tile = first_tile;
        prop = S_FLIPX;
    }
    /*
     * A partly visible entity on the left edge gives a hardware x below 0 for
     * its left tile; the uint8_t wrap puts that tile past the right edge, where
     * the hardware hides it.
     */
    uint8_t oam_x = (uint8_t)x + DEVICE_SPRITE_PX_OFFSET_X;
    uint8_t oam_y = y + DEVICE_SPRITE_PX_OFFSET_Y;
    set_oam(oam, oam_x, oam_y, left_tile, prop);
    set_oam(oam + 1, oam_x + 8, oam_y, right_tile, prop);
    return 2;
}

static uint8_t render_torpedo(const Torpedo *torpedo, uint8_t sprite) {
    if (!torpedo->active) {
        return 0;
    }
    /* Active torpedoes are never fully off screen (update_torpedo retires them). */
    set_oam(&shadow_OAM[sprite],
            (uint8_t)fixed_to_pixel(torpedo->x) + DEVICE_SPRITE_PX_OFFSET_X,
            torpedo->y + DEVICE_SPRITE_PX_OFFSET_Y,
            SPRITE_TILE_TORPEDO,
            torpedo->moving_left ? S_FLIPX : 0);
    return 1;
}

static uint8_t render_lane(const Lane *lane, uint8_t clock, uint8_t first_sprite) {
    uint8_t used = 0;

    if (lane->alive_mask != 0) {
        /* Locals instead of lane->...: each pointer dereference costs extra on the SM83. */
        uint8_t alive = lane->alive_mask;
        int16_t step = lane->member_step;
        uint8_t y = lane->enemy_y;
        uint8_t facing_left = lane->moving_left;
        uint8_t tile = (lane->enemy_type == ENEMY_SUB)
            ? SPRITE_TILE_ENEMY_SUB + anim_tile_offset(clock, ANIM_PERIOD_PROPELLER)
            : SPRITE_TILE_SHARK + anim_tile_offset(clock, ANIM_PERIOD_SHARK);
        int16_t x = lane->leader_x;
        for (uint8_t bit = 1; bit <= alive; bit <<= 1, x += step) {
            if (alive & bit) {
                used += render_entity(tile, x, y, facing_left, first_sprite + used);
            }
        }
    }
    if (lane->diver_active) {
        used += render_entity(SPRITE_TILE_DIVER + anim_tile_offset(clock, ANIM_PERIOD_DIVER),
                              lane->diver_x, lane->base_y, lane->moving_left,
                              first_sprite + used);
    }
    used += render_torpedo(&lane->torpedo, first_sprite + used);
    return used;
}

void game_render(GameState *game) {
    const Player *player = &game->player;
    uint8_t clock = game->frame;
    uint8_t next_sprite = 0;

    /* First, while still in VBlank (see game.h). */
    render_hud(game);

    /* Sprites would draw over the banner text. */
    if (game->banner != BANNER_NONE) {
        hide_sprites_range(0, MAX_HARDWARE_SPRITES);
        return;
    }

    /* Lowest OAM slots win when a scanline overflows, so the player goes first. */
    if (player->state != PLAYER_DYING) {
        next_sprite += render_entity(SPRITE_TILE_PLAYER_SUB + anim_tile_offset(clock, ANIM_PERIOD_PROPELLER),
                                     fixed_to_pixel(player->x),
                                     (uint8_t)fixed_to_pixel(player->y), player->facing_left,
                                     next_sprite);
    } else if ((player->state_timer / (uint8_t)EXPLOSION_BLINK_FRAMES) & 1) {
        next_sprite += render_entity(explosion_tile(player->state_timer), fixed_to_pixel(player->x),
                                     (uint8_t)fixed_to_pixel(player->y), player->facing_left,
                                     next_sprite);
    }
    next_sprite += render_torpedo(&game->player_torpedo, next_sprite);
    if (game->patrol.active) {
        next_sprite += render_entity(SPRITE_TILE_ENEMY_SUB + anim_tile_offset(clock, ANIM_PERIOD_PROPELLER),
                                     game->patrol.x, PATROL_Y,
                                     game->patrol.moving_left, next_sprite);
    }
    for (uint8_t i = 0; i < LANE_COUNT; i++) {
        next_sprite += render_lane(&game->lanes[i], clock, next_sprite);
    }
    hide_sprites_range(next_sprite, MAX_HARDWARE_SPRITES);
}
