#ifndef DISPLAY_H
#define DISPLAY_H

#include <stdint.h>

#define RENDER_WIDTH  160
#define RENDER_HEIGHT 120
#define TFT_WIDTH     320
#define TFT_HEIGHT    240

void display_init(void);
void display_push_frame(uint8_t *framebuffer, uint16_t *palette);

#endif // DISPLAY_H
