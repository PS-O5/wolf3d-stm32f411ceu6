#include "include/input.h"

#define RCC_BASE        0x40023800
#define RCC_AHB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_APB2ENR     (*(volatile uint32_t *)(RCC_BASE + 0x44))

#define GPIOA_BASE      0x40020000
#define GPIOA_MODER     (*(volatile uint32_t *)(GPIOA_BASE + 0x00))
#define GPIOA_PUPDR     (*(volatile uint32_t *)(GPIOA_BASE + 0x0C))
#define GPIOA_IDR       (*(volatile uint32_t *)(GPIOA_BASE + 0x10))

#define GPIOB_BASE      0x40020400
#define GPIOB_MODER     (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_PUPDR     (*(volatile uint32_t *)(GPIOB_BASE + 0x0C))
#define GPIOB_IDR       (*(volatile uint32_t *)(GPIOB_BASE + 0x10))

#define ADC1_BASE       0x40012000
#define ADC1_CR1        (*(volatile uint32_t *)(ADC1_BASE + 0x04))
#define ADC1_CR2        (*(volatile uint32_t *)(ADC1_BASE + 0x08))
#define ADC1_SQR1       (*(volatile uint32_t *)(ADC1_BASE + 0x2C))
#define ADC1_SQR3       (*(volatile uint32_t *)(ADC1_BASE + 0x34))
#define ADC1_DR         (*(volatile uint32_t *)(ADC1_BASE + 0x4C))

#define ADC_CCR         (*(volatile uint32_t *)(0x40012304)) // ADC Common Control

#define DMA2_BASE       0x40026400
#define DMA2_S0CR       (*(volatile uint32_t *)(DMA2_BASE + 0x10))
#define DMA2_S0NDTR     (*(volatile uint32_t *)(DMA2_BASE + 0x14))
#define DMA2_S0PAR      (*(volatile uint32_t *)(DMA2_BASE + 0x18))
#define DMA2_S0M0AR     (*(volatile uint32_t *)(DMA2_BASE + 0x1C))

volatile uint16_t adc_vals[2] = {2048, 2048}; // [0]=Y, [1]=X
static uint8_t fire_hist = 0;
static uint8_t door_hist = 0;
static uint8_t door_prev = 0;

void input_init(void) {
    /* Clocks: GPIOA, GPIOB, DMA2, ADC1 */
    RCC_AHB1ENR |= (1 << 0) | (1 << 1) | (1 << 22); 
    RCC_APB2ENR |= (1 << 8); 

    /* Analog Pins: PA1 (Y), PA2 (X) */
    GPIOA_MODER |= (3 << (1 * 2)) | (3 << (2 * 2)); 

    /* Digital Pins: PA0 (KEY), PB10 (Joy Click) -> Pull-up Inputs */
    GPIOA_MODER &= ~(3 << (0 * 2));
    GPIOA_PUPDR &= ~(3 << (0 * 2)); // MUST CLEAR FIRST for Soft Reset at high speeds
    GPIOA_PUPDR |=  (1 << (0 * 2));
    
    GPIOB_MODER &= ~(3 << (10 * 2));
    GPIOB_PUPDR &= ~(3 << (10 * 2)); // MUST CLEAR FIRST for Soft Reset at high speeds
    GPIOB_PUPDR |=  (1 << (10 * 2));

    // NEW: D-Pad Pins: PB12, PB13, PB14, PB15 -> Pull-up Inputs as Joystick is broken :(
    GPIOB_MODER &= ~((3 << (12 * 2)) | (3 << (13 * 2)) | (3 << (14 * 2)) | (3 << (15 * 2)));
    GPIOB_PUPDR &= ~((3 << (12 * 2)) | (3 << (13 * 2)) | (3 << (14 * 2)) | (3 << (15 * 2)));
    GPIOB_PUPDR |=  ((1 << (12 * 2)) | (1 << (13 * 2)) | (1 << (14 * 2)) | (1 << (15 * 2)));


    // APB2 is 96MHz. ADC Max is 36MHz. Set prescaler to /4 (24MHz)
    ADC_CCR = (1 << 16);

    /* DMA2 Stream 0 (ADC1) Circular Continuous Config */
    DMA2_S0CR = 0; 
    while (DMA2_S0CR & 1);
    DMA2_S0PAR  = (uint32_t)&ADC1_DR;
    DMA2_S0M0AR = (uint32_t)adc_vals;
    DMA2_S0NDTR = 2; 
    DMA2_S0CR = (1 << 13) | (1 << 11) | (1 << 10) | (1 << 8) | (1 << 0); // MINC, PSIZE/MSIZE=16b, CIRC, EN

    /* ADC1 Continuous Scan Config */
    ADC1_CR1  = (1 << 8); // SCAN mode
    ADC1_SQR1 = ((2 - 1) << 20); // 2 conversions
    ADC1_SQR3 = 1 | (2 << 5);    // SQ1=CH1(PA1), SQ2=CH2(PA2)
    ADC1_CR2  = (1 << 0) | (1 << 1) | (1 << 8) | (1 << 9); // ADON, CONT, DMA, DDS
    ADC1_CR2 |= (1 << 30); // SWSTART
}

void input_tick(void) {
    // Shift register debounce (Active LOW)
    door_hist = (door_hist << 1) | !(GPIOA_IDR & (1 << 0));
    fire_hist = (fire_hist << 1) | !(GPIOB_IDR & (1 << 10));
}

InputState input_read(void) {
    InputState state = {0};
    
    // Persistent state tracker: 0 = Unknown, 1 = D-Pad Locked, 2 = Analog Locked
    static uint8_t active_input_mode = 0; 

    // 1. Read D-Pad (Active LOW)
    int32_t dpad_move = 0;
    int32_t dpad_turn = 0;

    if (!(GPIOB_IDR & (1 << 12))) dpad_move = MAX_SPEED;  // UP
    if (!(GPIOB_IDR & (1 << 13))) dpad_move = -MAX_SPEED; // DOWN
    if (!(GPIOB_IDR & (1 << 14))) dpad_turn = -MAX_TURN;  // LEFT
    if (!(GPIOB_IDR & (1 << 15))) dpad_turn = MAX_TURN;   // RIGHT

    // Lock into D-Pad mode the moment a button is pressed
    if (dpad_move != 0 || dpad_turn != 0) {
        active_input_mode = 1; 
    }

    // 2. Process Analog Logic
    int32_t raw_y = (int32_t)adc_vals[0] - 2048;
    int32_t raw_x = (int32_t)adc_vals[1] - 2048;
    
    // Strict valid check: Not grounded/shorted to VCC, AND outside the deadzone
    int analog_is_valid = (adc_vals[0] > 100 && adc_vals[0] < 3995 && 
                           adc_vals[1] > 100 && adc_vals[1] < 3995);
                           
    int analog_is_moving = (raw_y > 300 || raw_y < -300 || raw_x > 300 || raw_x < -300);

    // Lock into Analog mode if we see valid, intentional analog movement before any D-pad use
    if (active_input_mode == 0 && analog_is_valid && analog_is_moving) {
        active_input_mode = 2;
    }

    // 3. Route Output Based on Locked Mode
    if (active_input_mode == 1) {
        // D-Pad locked: Completely ignore the ADC variables
        state.move = dpad_move;
        state.turn = dpad_turn;
    } 
    else {
        // Analog locked (or still waiting for input)
        if (analog_is_valid) {
            // Apply Deadzone
            if (raw_y > -300 && raw_y < 300) raw_y = 0;
            if (raw_x > -300 && raw_x < 300) raw_x = 0;

            // Scale proportionally
            state.move = (raw_y * MAX_SPEED) / 2048;
            state.turn = (raw_x * MAX_TURN) / 2048;
        } else {
            // Hardware check failed (e.g., pins are tied to ground)
            state.move = 0;
            state.turn = 0;
        }
    }

    // Button Logic
    state.fire = ((fire_hist & 0x0F) == 0x0F);
    
    uint8_t door_current = ((door_hist & 0x0F) == 0x0F);
    state.door = (door_current && !door_prev); // Rising edge
    door_prev = door_current;

    return state;
}
