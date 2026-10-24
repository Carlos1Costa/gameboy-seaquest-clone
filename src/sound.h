#ifndef SOUND_H
#define SOUND_H

/*
 * One-shot sound effects. Each one is a single write to the APU registers;
 * the hardware runs the envelope and pitch sweep on its own, so playing a
 * sound costs a few cycles and needs no per-frame update.
 */
typedef enum {
    SOUND_TORPEDO,          /* player fires */
    SOUND_ENEMY_EXPLOSION,
    SOUND_PLAYER_EXPLOSION,
    SOUND_RESCUE,           /* diver picked up */
    SOUND_OXYGEN_ALARM,
    SOUND_TALLY,            /* one step of the surfacing score count */
    SOUND_EXTRA_LIFE
} SoundEffect;

void sound_init(void);
void sound_play(SoundEffect effect);

#endif
