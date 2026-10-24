#include <gb/gb.h>

#include "game.h"

/* DMG palette: color 0 = white ... color 3 = black (sprite color 0 is transparent). */
enum PaletteTuning {
    PALETTE_IDENTITY = 0xE4
};

static void input_read(InputState *input) {
    uint8_t previous = input->held;
    input->held = joypad();
    input->pressed = input->held & (uint8_t)~previous;
}

static void hardware_init(void) {
    DISPLAY_OFF;
    BGP_REG = PALETTE_IDENTITY;
    OBP0_REG = PALETTE_IDENTITY;
    SPRITES_8x8;
}

static void display_start(void) {
    SHOW_BKG;
    SHOW_SPRITES;
    DISPLAY_ON;
}

void main(void) {
    /* Static so the large state lives in WRAM, not on the small stack. */
    static GameState game;
    InputState input = { 0, 0 };

    hardware_init();
    game_init(&game);
    display_start();

    while (1) {
        input_read(&input);
        if (game.game_over) {
            /* The world stays frozen behind the GAME OVER banner until START. */
            if (input.pressed & J_START) {
                game_restart(&game);
            }
        } else {
            game_update(&game, &input);
        }
        /* Render right after vsync so VRAM writes land in VBlank (see game_render). */
        vsync();
        game_render(&game);
    }
}
