#ifndef COLLISION_H
#define COLLISION_H

#include <stdint.h>

#include "game.h"

/* Axis-aligned bounding box in screen pixels. */
typedef struct {
    int16_t x;
    int16_t y;
    uint8_t width;
    uint8_t height;
} Box;

uint8_t boxes_overlap(const Box *a, const Box *b);

/*
 * Checks every collision pair and applies its direct effect: destroys
 * torpedoed enemies, rescues touched divers and sets game->player_hit.
 * Consequences of a hit (death, respawn) are left to game.c.
 */
void collisions_update(GameState *game);

#endif
