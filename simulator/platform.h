// platform.h
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>

// Internal Raycaster Resolution (8-bit)
#define RENDER_WIDTH  160
#define RENDER_HEIGHT 120

// Final Output Resolution (16-bit upscaled)
#define TFT_WIDTH     320
#define TFT_HEIGHT    240

// Global variables that both the engine and platform need
extern uint8_t  frame_buffer_8bit[RENDER_WIDTH * RENDER_HEIGHT];
extern uint16_t rgb565_palette[256];

// System timing
uint32_t plat_get_ticks_ms(void);

#endif
