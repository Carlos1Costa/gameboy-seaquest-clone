#ifndef COLLISION_H
#define COLLISION_H

#include <stdint.h>

#include "game.h"

/*
 * Axis-aligned bounding box with precomputed edges (right/bottom exclusive).
 * x is in collision space: screen x + COLLISION_X_OFFSET, so anything at
 * least partly visible has a positive x that fits in a byte and every
 * comparison stays 8-bit, which is much cheaper on the Game Boy's CPU.
 */
typedef struct {
    uint8_t left;
    uint8_t right;
    uint8_t top;
    uint8_t bottom;
} Box;

enum CollisionSpace {
    COLLISION_X_OFFSET = ENTITY_WIDTH
};

/*
 * Checks every collision pair and applies its direct effect: destroys
 * torpedoed enemies, rescues touched divers and sets game->player_hit.
 * Consequences of a hit (death, respawn) are left to game.c.
 */
void collisions_update(GameState *game);

#endif
