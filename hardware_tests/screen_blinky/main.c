#include <stdint.h>
#include "display.h"

#define SYSTICK_BASE  0xE000E010
#define SCB_BASE      0xE000ED00
#define STK_CTRL      (*(volatile uint32_t *)(SYSTICK_BASE + 0x00))
#define STK_LOAD      (*(volatile uint32_t *)(SYSTICK_BASE + 0x04))
#define STK_VAL       (*(volatile uint32_t *)(SYSTICK_BASE + 0x08))
#define SCB_VTOR      (*(volatile uint32_t *)(SCB_BASE + 0x08))

static volatile uint32_t ms_ticks = 0;

uint8_t  frame_buffer_8bit[RENDER_WIDTH * RENDER_HEIGHT];
uint16_t game_palette[256];

void SysTick_Handler(void) {
    ms_ticks++;
}

void delay_ms(uint32_t ms) {
    uint32_t start = ms_ticks;
    while ((ms_ticks - start) < ms) {
        __asm__("nop");
    }
}

int main(void) {
    SCB_VTOR = 0x08000000;

    STK_VAL  = 0;
    STK_LOAD = 16000 - 1;
    STK_CTRL = 7;
    __asm__ volatile ("cpsie i" : : : "memory");

    display_init();

    /* Basic Palette */
    game_palette[0] = 0x2104; // Dark Grey
    game_palette[1] = 0xF800; // Red
    game_palette[2] = 0x07E0; // Green
    game_palette[3] = 0x001F; // Blue


    /* Faster Box Physics */
    int px = 10, py = 10;
    int dx = 5, dy = 4; // Increased velocity
    uint8_t color_idx = 1;

    while (1) {
        /* 1. Automated Animation Logic */
        px += dx;
        py += dy;

        // Bounce off walls and change color
        if (px <= 0 || px >= RENDER_WIDTH - 15) {
            dx = -dx;
            color_idx = (color_idx % 3) + 1;
        }
        if (py <= 0 || py >= RENDER_HEIGHT - 15) {
            dy = -dy;
            color_idx = (color_idx % 3) + 1;
        }

        /* 2. Clear 8-bit Framebuffer */
        for (int i = 0; i < RENDER_WIDTH * RENDER_HEIGHT; i++) {
            frame_buffer_8bit[i] = 0; 
        }

        /* 3. Draw 15x15 Block */
        for (int y = 0; y < 15; y++) {
            for (int x = 0; x < 15; x++) {
                frame_buffer_8bit[(py + y) * RENDER_WIDTH + (px + x)] = color_idx;
            }
        }

        /* 4. Push 160x120 buffer through the 320x240 Upscale Pipeline */
        display_push_frame(frame_buffer_8bit, game_palette);

        /* Remove throttle for running at absolute maximum FPS */
        //delay_ms(8);   //30FPS Game target
    }
    return 0;
}
