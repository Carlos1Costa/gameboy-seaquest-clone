/*
 * Compiled by the test harness with the game's own compiler and headers, so
 * the tests read struct offsets and tuning values from the code instead of
 * hard-coding them. Every "const int16_t name" below becomes a value in
 * harness.Layout under that name; add a line here to expose a new one.
 */
#include <stddef.h>

#include "game.h"
#include "sound.h"

/* Struct layout */
const int16_t lane_size = sizeof(Lane);
const int16_t player_x = offsetof(GameState, player) + offsetof(Player, x);
const int16_t player_y = offsetof(GameState, player) + offsetof(Player, y);
const int16_t player_facing_left = offsetof(GameState, player) + offsetof(Player, facing_left);
const int16_t player_state = offsetof(GameState, player) + offsetof(Player, state);
const int16_t player_submerged = offsetof(GameState, player) + offsetof(Player, submerged);
const int16_t torpedo = offsetof(GameState, player_torpedo);
const int16_t lanes = offsetof(GameState, lanes);
const int16_t patrol = offsetof(GameState, patrol);
const int16_t game_over = offsetof(GameState, game_over);
const int16_t banner = offsetof(GameState, banner);
const int16_t frame = offsetof(GameState, frame);
const int16_t score_digits = offsetof(GameState, score_digits);
const int16_t score_changed = offsetof(GameState, score_changed);
const int16_t oxygen = offsetof(GameState, oxygen);
const int16_t oxygen_drain_timer = offsetof(GameState, oxygen_drain_timer);
const int16_t divers = offsetof(GameState, divers);
const int16_t reserves = offsetof(GameState, reserves);
const int16_t wave = offsetof(GameState, wave);

const int16_t torpedo_x = offsetof(Torpedo, x);
const int16_t torpedo_y = offsetof(Torpedo, y);
const int16_t torpedo_active = offsetof(Torpedo, active);
const int16_t torpedo_moving_left = offsetof(Torpedo, moving_left);

const int16_t lane_alive_mask = offsetof(Lane, alive_mask);
const int16_t lane_speed = offsetof(Lane, speed);
const int16_t lane_group_x = offsetof(Lane, group_x);
const int16_t lane_leader_x = offsetof(Lane, leader_x);
const int16_t lane_member_step = offsetof(Lane, member_step);
const int16_t lane_enemy_y = offsetof(Lane, enemy_y);
const int16_t lane_enemy_type = offsetof(Lane, enemy_type);
const int16_t lane_moving_left = offsetof(Lane, moving_left);
const int16_t lane_diver_active = offsetof(Lane, diver_active);
const int16_t lane_diver_fixed_x = offsetof(Lane, diver_fixed_x);
const int16_t lane_diver_x = offsetof(Lane, diver_x);
const int16_t lane_torpedo = offsetof(Lane, torpedo);
const int16_t lane_base_y = offsetof(Lane, base_y);
const int16_t lane_spawn_timer = offsetof(Lane, spawn_timer);
const int16_t lane_fire_timer = offsetof(Lane, fire_timer);

const int16_t patrol_active = offsetof(Patrol, active);
const int16_t patrol_moving_left = offsetof(Patrol, moving_left);
const int16_t patrol_fixed_x = offsetof(Patrol, fixed_x);
const int16_t patrol_x = offsetof(Patrol, x);
const int16_t patrol_timer = offsetof(Patrol, timer);

/* Enum values */
const int16_t TALLYING = PLAYER_TALLYING;
const int16_t REFILLING = PLAYER_REFILLING;
const int16_t ACTIVE = PLAYER_ACTIVE;
const int16_t DYING = PLAYER_DYING;
const int16_t SHARK = ENEMY_SHARK;
const int16_t SUB = ENEMY_SUB;
const int16_t NO_BANNER = BANNER_NONE;
const int16_t TITLE_BANNER = BANNER_TITLE;
const int16_t GAME_OVER_BANNER = BANNER_GAME_OVER;

const int16_t TORPEDO_SOUND = SOUND_TORPEDO;
const int16_t ENEMY_EXPLOSION_SOUND = SOUND_ENEMY_EXPLOSION;
const int16_t PLAYER_EXPLOSION_SOUND = SOUND_PLAYER_EXPLOSION;
const int16_t RESCUE_SOUND = SOUND_RESCUE;
const int16_t OXYGEN_ALARM_SOUND = SOUND_OXYGEN_ALARM;
const int16_t TALLY_SOUND = SOUND_TALLY;
const int16_t EXTRA_LIFE_SOUND = SOUND_EXTRA_LIFE;

/* Tuning */
const int16_t SCREEN_WIDTH = SCREEN_PX_WIDTH;
const int16_t ROW_SCORE_ = ROW_SCORE;
const int16_t ROW_LIVES_ = ROW_LIVES;
const int16_t ROW_OXYGEN_ = ROW_OXYGEN;
const int16_t ROW_DIVERS_ = ROW_DIVERS;
const int16_t SCORE_COLUMN_ = SCORE_COLUMN;
const int16_t LIVES_COLUMN_ = LIVES_COLUMN;
const int16_t DIVERS_COLUMN_ = DIVERS_COLUMN;
const int16_t OXYGEN_BAR_COLUMN_ = OXYGEN_BAR_COLUMN;
const int16_t OXYGEN_BAR_TILES_ = OXYGEN_BAR_TILES;
const int16_t SCORE_DIGITS_ = SCORE_DIGITS;
const int16_t FIXED_ONE_ = FIXED_ONE;
const int16_t PLAYER_SPEED_ = PLAYER_SPEED;
const int16_t PLAYER_MIN_X_ = PLAYER_MIN_X;
const int16_t PLAYER_MAX_X_ = PLAYER_MAX_X;
const int16_t PLAYER_MIN_Y_ = PLAYER_MIN_Y;
const int16_t PLAYER_MAX_Y_ = PLAYER_MAX_Y;
const int16_t PLAYER_START_X_ = PLAYER_START_X;
const int16_t PLAYER_SUBMERGED_Y_ = PLAYER_SUBMERGED_Y;
const int16_t PLAYER_TORPEDO_SPEED_ = PLAYER_TORPEDO_SPEED;
const int16_t ENEMY_TORPEDO_SPEED_ = ENEMY_TORPEDO_SPEED;
const int16_t LANE_COUNT_ = LANE_COUNT;
const int16_t LANE_FIRST_Y_ = LANE_FIRST_Y;
const int16_t LANE_SPACING_ = LANE_SPACING;
const int16_t LANE_MAX_ENTITIES_ = LANE_MAX_ENTITIES;
const int16_t GROUP_SPACING_ = GROUP_SPACING;
const int16_t MAX_ACTIVE_ENEMIES_ = MAX_ACTIVE_ENEMIES;
const int16_t MAX_ENEMY_TORPEDOES_ = MAX_ENEMY_TORPEDOES;
const int16_t SHARK_SPEED_ = SHARK_SPEED;
const int16_t ENEMY_SUB_SPEED_ = ENEMY_SUB_SPEED;
const int16_t SHARK_WAVE_AMPLITUDE_ = SHARK_WAVE_AMPLITUDE;
const int16_t MAX_DIVERS_ON_BOARD_ = MAX_DIVERS_ON_BOARD;
const int16_t STARTING_RESERVES_ = STARTING_RESERVES;
const int16_t MAX_RESERVES_ = MAX_RESERVES;
const int16_t OXYGEN_MAX_ = OXYGEN_MAX;
const int16_t OXYGEN_DRAIN_FRAMES_ = OXYGEN_DRAIN_FRAMES;
const int16_t OXYGEN_LOW_ = OXYGEN_LOW;
const int16_t DEATH_FRAMES_ = DEATH_FRAMES;
const int16_t ENEMY_POINTS_BASE_ = ENEMY_POINTS_BASE;
const int16_t ENEMY_POINTS_STEP_ = ENEMY_POINTS_STEP;
const int16_t ENEMY_POINTS_MAX_ = ENEMY_POINTS_MAX;
const int16_t DIVER_POINTS_BASE_ = DIVER_POINTS_BASE;
const int16_t DIVER_POINTS_STEP_ = DIVER_POINTS_STEP;
const int16_t DIVER_POINTS_MAX_ = DIVER_POINTS_MAX;
const int16_t OXYGEN_BONUS_PER_UNIT_ = OXYGEN_BONUS_PER_UNIT;
const int16_t PATROL_FIRST_WAVE_ = PATROL_FIRST_WAVE;
const int16_t PATROL_Y_ = PATROL_Y;
const int16_t ENEMY_SPEED_PER_WAVE_ = ENEMY_SPEED_PER_WAVE;
const int16_t ENEMY_SPEED_MAX_ = ENEMY_SPEED_MAX;
const int16_t OXYGEN_LABEL_COLUMN_ = OXYGEN_LABEL_COLUMN;
const int16_t OXYGEN_REFILL_TICK_UNITS_ = OXYGEN_REFILL_TICK_UNITS;
const int16_t ANIM_PERIOD_PROPELLER_ = ANIM_PERIOD_PROPELLER;
const int16_t ANIM_PERIOD_SHARK_ = ANIM_PERIOD_SHARK;
const int16_t ANIM_PERIOD_DIVER_ = ANIM_PERIOD_DIVER;
const int16_t EXPLOSION_FRAME_LENGTH_ = EXPLOSION_FRAME_LENGTH;
