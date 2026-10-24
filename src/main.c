#include <gb/gb.h>

#include "game.h"
#include "sound.h"

/* Screens: TITLE --START--> PLAYING --last life lost--> GAME_OVER --START--> TITLE */
typedef enum {
    APP_TITLE,
    APP_PLAYING,
    APP_GAME_OVER
} AppState;

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
    sound_init();
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
    AppState state = APP_TITLE;
    /*
     * The boot is deterministic, so the random seed comes from how long the
     * player waits on the title screen, plus the free-running DIV register.
     */
    uint8_t title_frames = 0;

    hardware_init();
    game_init(&game);
    game_show_title(&game);
    display_start();

    while (1) {
        input_read(&input);
        switch (state) {
        case APP_TITLE:
            title_frames++;
            if (input.pressed & J_START) {
                game_start(&game, ((uint16_t)title_frames << 8) | DIV_REG);
                state = APP_PLAYING;
            }
            break;
        case APP_PLAYING:
            game_update(&game, &input);
            if (game.game_over) {
                state = APP_GAME_OVER;
            }
            break;
        case APP_GAME_OVER:
            /* The world stays frozen behind the banner. */
            if (input.pressed & J_START) {
                game_show_title(&game);
                state = APP_TITLE;
            }
            break;
        }
        /* Render right after vsync so VRAM writes land in VBlank (see game_render). */
        vsync();
        game_render(&game);
    }
}
