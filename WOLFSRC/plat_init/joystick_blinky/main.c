#include <stdint.h>
#include "display.h"
#include "input.h"

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
    input_tick(); // Feed the input debounce routine
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
    input_init();

    game_palette[0] = 0x2104; // Dark Grey Background
    game_palette[1] = 0xF800; // Red Player
    game_palette[2] = 0x07E0; // Green Player (Firing)

    // Using Fixed-Point for smoother analog movement
    int32_t px = INT2FX(RENDER_WIDTH / 2);
    int32_t py = INT2FX(RENDER_HEIGHT / 2);

    while (1) {
        InputState input = input_read();

        // Integrate movement (Scale down max speed for this test context)
        py -= (input.move / 600); // Inverse Y-axis for standard joystick feel
        px += (input.turn / 600);

        // Map Boundaries
        if (px < 0) px = 0;
        if (px > INT2FX(RENDER_WIDTH - 15)) px = INT2FX(RENDER_WIDTH - 15);
        if (py < 0) py = 0;
        if (py > INT2FX(RENDER_HEIGHT - 15)) py = INT2FX(RENDER_HEIGHT - 15);

        // Clear Screen
        for (int i = 0; i < RENDER_WIDTH * RENDER_HEIGHT; i++) frame_buffer_8bit[i] = 0; 

        // Draw Player (Changes color if Fire button held)
        uint8_t color = input.fire ? 2 : 1;
        int screen_x = px >> FX_SHIFT;
        int screen_y = py >> FX_SHIFT;

        for (int y = 0; y < 15; y++) {
            for (int x = 0; x < 15; x++) {
                frame_buffer_8bit[(screen_y + y) * RENDER_WIDTH + (screen_x + x)] = color;
            }
        }

        display_push_frame(frame_buffer_8bit, game_palette);
        
        // Frame pacing (~30 FPS Target)
        delay_ms(8);
    }
    return 0;
}
