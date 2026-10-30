#include <gb/gb.h>
#include <gb/hardware.h>

#include "sound.h"

/*
 * Channel use: pulse 1 (with pitch sweep) for the torpedo and extra life,
 * pulse 2 for short beeps, noise for explosions. A new sound on a busy
 * channel simply restarts it, which is what arcade-style SFX want.
 *
 * Register cheat sheet (Pan Docs):
 *   NRx0 sweep: time<<4 | decrease<<3 | shift
 *   NRx1 duty<<6 (pulse) / length
 *   NRx2 envelope: volume<<4 | increase<<3 | step (0 = hold)
 *   NRx3/NRx4 period: 11 bits, frequency = 131072 / (2048 - period) Hz;
 *   NRx4 bit 7 restarts the channel.
 */
enum SoundRegisters {
    APU_ON           = 0x80,
    MASTER_VOLUME    = 0x77,   /* max on both outputs */
    ALL_CHANNELS_LR  = 0xFF,
    TRIGGER          = 0x80,
    DUTY_12_5        = 0x00,   /* thin, reedy tone: reads as a bubble */
    DUTY_50          = 0x80,

    REFILL_ENVELOPE    = 0x71,
    REFILL_BASE_PERIOD = 0x600,   /* ~260 Hz with an empty tank... */
    REFILL_PITCH_SHIFT = 2        /* ...up to ~1 kHz with a full one */
};

/* Pulse period (11 bits) split into the NRx3 low byte and NRx4 high bits. */
static void trigger_pulse1(uint8_t sweep, uint8_t envelope, uint16_t period) {
    NR10_REG = sweep;
    NR11_REG = DUTY_50;
    NR12_REG = envelope;
    NR13_REG = (uint8_t)period;
    NR14_REG = TRIGGER | (uint8_t)(period >> 8);
}

static void trigger_pulse2(uint8_t duty, uint8_t envelope, uint16_t period) {
    NR21_REG = duty;
    NR22_REG = envelope;
    NR23_REG = (uint8_t)period;
    NR24_REG = TRIGGER | (uint8_t)(period >> 8);
}

static void trigger_noise(uint8_t envelope, uint8_t polynomial) {
    NR41_REG = 0;
    NR42_REG = envelope;
    NR43_REG = polynomial;
    NR44_REG = TRIGGER;
}

void sound_play_oxygen_refill(uint8_t oxygen) {
    trigger_pulse2(DUTY_12_5, REFILL_ENVELOPE, REFILL_BASE_PERIOD + ((uint16_t)oxygen << REFILL_PITCH_SHIFT));
}

void sound_init(void) {
    NR52_REG = APU_ON;          /* must come first: the other registers ignore writes while off */
    NR50_REG = MASTER_VOLUME;
    NR51_REG = ALL_CHANNELS_LR;
}

void sound_play(SoundEffect effect) {
    switch (effect) {
    case SOUND_TORPEDO:
        /* ~1 kHz falling quickly: a short "pew". */
        trigger_pulse1(0x1C, 0xA1, 0x780);
        break;
    case SOUND_ENEMY_EXPLOSION:
        trigger_noise(0xA2, 0x55);
        break;
    case SOUND_PLAYER_EXPLOSION:
        /* Louder, lower and longer than an enemy's. */
        trigger_noise(0xF5, 0x71);
        break;
    case SOUND_RESCUE:
        trigger_pulse2(DUTY_50, 0x92, 0x7C1);   /* ~2 kHz blip */
        break;
    case SOUND_OXYGEN_ALARM:
        trigger_pulse2(DUTY_50, 0x71, 0x6D6);   /* ~440 Hz beep */
        break;
    case SOUND_TALLY:
        trigger_pulse2(DUTY_50, 0x51, 0x783);   /* soft, high tick */
        break;
    case SOUND_EXTRA_LIFE:
        /*
         * Rising sweep from ~500 Hz. The shift sets how fast the period grows:
         * past 2047 the hardware cuts the channel, so a low shift ends the
         * sound within milliseconds. This one lasts ~150 ms.
         */
        trigger_pulse1(0x26, 0xC3, 0x700);
        break;
    }
}
