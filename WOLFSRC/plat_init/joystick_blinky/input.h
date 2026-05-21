#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

/* Fixed Point Math Helpers */
#define FX_SHIFT 16
#define INT2FX(i) ((i) << FX_SHIFT)

/* Joystick Tuning Parameters */
#define MAX_SPEED 0x6000 // Fixed-point proportional bounds
#define MAX_TURN  0x4000

typedef struct {
    int32_t move;   // Positive = Forward, Negative = Back
    int32_t turn;   // Positive = Right, Negative = Left
    uint8_t fire;   // 1 = Held down
    uint8_t door;   // 1 = Just pressed (rising edge)
} InputState;

void input_init(void);
void input_tick(void); // Call every 1ms
InputState input_read(void);

#endif
