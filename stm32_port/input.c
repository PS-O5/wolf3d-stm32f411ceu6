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
#define ADC1_SMPR2      (*(volatile uint32_t *)(ADC1_BASE + 0x10)) // Added SMPR2 register
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

    // Set ADC Sample Time to maximum (480 cycles) for CH1 and CH2
    // CH1 is bits [5:3], CH2 is bits [8:6]. '7' is 0b111.
    // Drops sample rate to ~24kHz, massively reducing analog noise.
    ADC1_SMPR2 |= (7 << 3) | (7 << 6);

    /* DMA2 Stream 0 (ADC1) Circular Continuous Config */
    DMA2_S0CR = 0; 
    while (DMA2_S0CR & 1);
    DMA2_S0PAR  = (uint32_t)&ADC1_DR;
    DMA2_S0M0AR = (uint32_t)adc_vals;
    DMA2_S0NDTR = 2; 
    
    // (0 << 25) explicitly selects DMA Channel 0 (ADC1)
    DMA2_S0CR = (0 << 25) | (1 << 13) | (1 << 11) | (1 << 10) | (1 << 8) | (1 << 0); // MINC, PSIZE/MSIZE=16b, CIRC, EN

    /* ADC1 Continuous Scan Config */
    ADC1_CR1  = (1 << 8); // SCAN mode
    ADC1_SQR1 = ((2 - 1) << 20); // 2 conversions
    ADC1_SQR3 = 1 | (2 << 5);    // SQ1=CH1=PA1=Y_AXIS, SQ2=CH2=PA2=X_AXIS
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
    
    // Persistent state trackers
    static uint8_t active_input_mode = 0; 
    static int idle_frame_count = 0; 
    
    // --- NEW: Calibration Variables ---
    static int boot_frames = 0;
    static int32_t center_y = 2048;
    static int32_t center_x = 2048;
    static int is_calibrated = 0;

    // 1. Read D-Pad (Active LOW)
    int32_t dpad_move = 0;
    int32_t dpad_turn = 0;

    if (!(GPIOB_IDR & (1 << 12))) dpad_move = MAX_SPEED;  // UP
    if (!(GPIOB_IDR & (1 << 13))) dpad_move = -MAX_SPEED; // DOWN
    if (!(GPIOB_IDR & (1 << 14))) dpad_turn = -MAX_TURN;  // LEFT
    if (!(GPIOB_IDR & (1 << 15))) dpad_turn = MAX_TURN;   // RIGHT

    if (dpad_move != 0 || dpad_turn != 0) {
        active_input_mode = 1; 
    }

    // --- NEW: Auto-Calibration Logic ---
    if (!is_calibrated) {
        boot_frames++;
        // Wait ~10 frames (160ms) for power to stabilize and DMA to update
        if (boot_frames > 10) { 
            // Guard against calibrating if the pins are dead shorted
            if (adc_vals[0] > 100 && adc_vals[0] < 4000) center_y = adc_vals[0];
            if (adc_vals[1] > 100 && adc_vals[1] < 4000) center_x = adc_vals[1];
            is_calibrated = 1;
        }
    }

    // 2. Process Analog Logic using the DYNAMIC center
    int32_t raw_y = (int32_t)adc_vals[0] - center_y;
    int32_t raw_x = (int32_t)adc_vals[1] - center_x;
    
    // Validate: Not completely grounded on both axes
    int analog_is_valid = !(adc_vals[0] < 10 && adc_vals[1] < 10); 
                           
    int analog_is_moving = (raw_y > 300 || raw_y < -300 || raw_x > 300 || raw_x < -300);

    if (active_input_mode == 0 && analog_is_valid && analog_is_moving) {
        active_input_mode = 2;
    }

    // 3. Route Output Based on Locked Mode
    if (active_input_mode == 1) {
        state.move = dpad_move;
        state.turn = dpad_turn;
    } 
    else {
        if (analog_is_valid) {
            // Apply Deadzone (we can safely use a smaller deadzone now that it's perfectly centered!)
            if (raw_y > -200 && raw_y < 200) raw_y = 0;
            if (raw_x > -200 && raw_x < 200) raw_x = 0;

            // Clamp max bounds so the math doesn't overflow if the center was skewed
            if (raw_y > 2047) raw_y = 2047; else if (raw_y < -2048) raw_y = -2048;
            if (raw_x > 2047) raw_x = 2047; else if (raw_x < -2048) raw_x = -2048;

            state.move = (raw_y * MAX_SPEED) / 2048;
            state.turn = (raw_x * MAX_TURN * 2) / 2048;
        } else {
            state.move = 0;
            state.turn = 0;
        }
    }

    // Button Logic
    // FIRE uses Level Detection: BJ fires continuously while the trigger is held
    state.fire = ((fire_hist & 0x0F) == 0x0F);
    
    // DOOR uses Edge Detection: Opens on a single press, prevents toggling every frame
    uint8_t door_current = ((door_hist & 0x0F) == 0x0F);
    state.door = (door_current && !door_prev); // Rising edge
    door_prev = door_current;

    // --- Idle Timeout Mode Reset ---
    // If absolutely no input is detected for 120 frames (~2 seconds), 
    // reset the active_input_mode so the system can re-detect a swapped controller.
    if (state.move == 0 && state.turn == 0 && !state.fire && !state.door) {
        idle_frame_count++;
        if (idle_frame_count > 120) {
            active_input_mode = 0; // Release the lock
        }
    } else {
        idle_frame_count = 0; // Reset counter on any valid input
    }

    return state;
}
