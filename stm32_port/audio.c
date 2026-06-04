#include "include/audio.h"
#include <stddef.h> 

// --- CLOCK & GPIO REGISTERS ---
#define RCC_BASE        0x40023800
#define RCC_AHB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x30))
#define RCC_APB1ENR     (*(volatile uint32_t *)(RCC_BASE + 0x40))

#define GPIOB_BASE      0x40020400
#define GPIOB_MODER     (*(volatile uint32_t *)(GPIOB_BASE + 0x00))
#define GPIOB_AFRL      (*(volatile uint32_t *)(GPIOB_BASE + 0x20))

// --- TIM4 REGISTERS (Piezo PWM on PB6) ---
#define TIM4_BASE       0x40000800
#define TIM4_CR1        (*(volatile uint32_t *)(TIM4_BASE + 0x00))
#define TIM4_EGR        (*(volatile uint32_t *)(TIM4_BASE + 0x14))
#define TIM4_CCMR1      (*(volatile uint32_t *)(TIM4_BASE + 0x18)) 
#define TIM4_CCER       (*(volatile uint32_t *)(TIM4_BASE + 0x20))
#define TIM4_PSC        (*(volatile uint32_t *)(TIM4_BASE + 0x28))
#define TIM4_ARR        (*(volatile uint32_t *)(TIM4_BASE + 0x2C))
#define TIM4_CCR1       (*(volatile uint32_t *)(TIM4_BASE + 0x34))


// =========================================================
// PIEZO ARDUINO SEQUENCES 
// =========================================================
typedef struct { uint16_t freq; uint16_t duration; } Tone;

const Tone snd_piezo_bark[] = { {850, 30}, {0, 5}, {500, 70}, {0, 20}, {900, 25}, {0, 5}, {450, 80}, {0, 20}, {0, 0} };
const Tone snd_piezo_door_open[] = { {250, 15}, {290, 15}, {330, 15}, {370, 15}, {410, 15}, {450, 15}, {490, 15}, {530, 15}, {570, 15}, {610, 15}, {650, 15}, {690, 15}, {730, 15}, {770, 15}, {810, 15}, {850, 15}, {890, 15}, {0, 0} };
const Tone snd_piezo_door_close[] = { {900, 15}, {860, 15}, {820, 15}, {780, 15}, {740, 15}, {700, 15}, {660, 15}, {620, 15}, {580, 15}, {540, 15}, {500, 15}, {460, 15}, {420, 15}, {380, 15}, {340, 15}, {300, 15}, {260, 15}, {0, 0} };
const Tone snd_piezo_guard_alert[] = { {700, 60}, {0, 20}, {1100, 140}, {0, 10}, {0, 0} };
const Tone snd_piezo_guard_die[] = { {1200, 40}, {0, 5}, {1000, 40}, {0, 5}, {850, 40}, {0, 5}, {700, 40}, {0, 5}, {550, 40}, {0, 5}, {420, 40}, {0, 5}, {300, 40}, {0, 5}, {0, 0} };
const Tone snd_piezo_player_shoot[] = { {1800, 15}, {900, 20}, {500, 25}, {0, 0} };
const Tone snd_piezo_guard_shoot[] = { {1200, 15}, {650, 25}, {0, 0} };
const Tone snd_piezo_health_pickup[] = { {784, 60}, {0, 10}, {988, 60}, {0, 10}, {1319, 100}, {0, 20}, {0, 0} };
const Tone snd_piezo_ammo_pickup[]   = { {700, 40}, {0, 10}, {900, 40}, {0, 10}, {0, 0} };
const Tone snd_piezo_player_death[] = { {1400, 12}, {1365, 12}, {1330, 12}, {1295, 12}, {1260, 12}, {1225, 12}, {1190, 12}, {1155, 12}, {1120, 12}, {1085, 12}, {1050, 12}, {1015, 12}, {980, 12},  {945, 12},  {910, 12},  {875, 12},  {840, 12},  {805, 12},  {770, 12},  {735, 12},  {700, 12},  {665, 12},  {630, 12},  {595, 12},  {560, 12},  {525, 12},  {490, 12},  {455, 12},  {420, 12},  {385, 12},  {350, 12},  {315, 12},  {280, 12},  {245, 12},  {210, 12},  {175, 12},  {120, 300}, {0, 20},    {0, 0} };
const Tone snd_piezo_level_complete[] = { {523, 80}, {0, 20}, {659, 80}, {0, 20}, {784, 80}, {0, 20}, {1047, 120}, {0, 20}, {784, 80}, {0, 20}, {1047, 120}, {0, 20}, {1319, 200}, {0, 20}, {0, 0} };

// =========================================================
// BACKGROUND MUSIC SEQUENCES
// =========================================================

// --- 1. GAMEPLAY: Haunting & Eerie (Minor motifs + Heartbeat) ---
const Tone snd_bgm_eerie[] = {
    {329, 300}, {0, 50}, {349, 300}, {0, 50}, {329, 300}, {0, 50}, {233, 600}, {0, 1000}, // Creepy descent
    {164, 100}, {0, 100}, {164, 100}, {0, 800}, // Low Heartbeat
    {329, 300}, {0, 50}, {349, 300}, {0, 50}, {329, 300}, {0, 50}, {493, 600}, {0, 1000}, // Creepy ascent
    {164, 100}, {0, 100}, {164, 100}, {0, 1200}, // Low Heartbeat
    {0, 0} // Loop marker
};

// --- 2. SPLASH SCREEN: Charge -> Blast -> Upbeat Hero Anthem ---
const Tone snd_bgm_splash[] = {
    // Phase 1: The Charge (Rapid Frequency Sweep)
    {100, 20}, {150, 20}, {200, 20}, {250, 20}, {300, 20}, 
    {400, 20}, {500, 20}, {600, 20}, {700, 20}, {800, 20},
    {1000, 20}, {1200, 20}, {1500, 20}, {1800, 20}, {2000, 150},
    
    // Phase 2: The Blast (Sudden low crunch)
    {150, 30}, {100, 30}, {80, 30}, {50, 300}, {0, 400},
    
    // Phase 3: The Hero Anthem (150 BPM Anime style)
    {440, 150}, {0, 25}, {440, 150}, {0, 25}, {523, 150}, {0, 25}, {587, 300}, {0, 100},
    {440, 150}, {0, 25}, {523, 150}, {0, 25}, {659, 150}, {0, 25}, {587, 300}, {0, 100},
    {659, 150}, {0, 25}, {784, 150}, {0, 25}, {880, 300}, {0, 100},
    {784, 150}, {0, 25}, {659, 150}, {0, 25}, {587, 150}, {0, 25}, {440, 400}, {0, 500},
    {0, 0} // Loop marker
};

// --- BGM STATE VARIABLES ---
static const Tone* current_bgm_sequence = NULL;
static int bgm_step = 0;
static int bgm_timer_ms = 0;


// Mapped exactly to the 11 indices in audio.h
const Tone* const piezo_library[NUM_SOUNDS] = {
    snd_piezo_guard_alert,    // 0: SND_ACHTUNG
    snd_piezo_guard_die,      // 1: SND_DIE
    snd_piezo_bark,           // 2: SND_BARK
    snd_piezo_door_open,      // 3: SND_DOOR_OPEN1
    snd_piezo_door_close,     // 4: SND_DOOR_CLOSE1
    snd_piezo_player_shoot,   // 5: SND_GUN_FIRE1
    snd_piezo_guard_shoot,    // 6: SND_GUN_FIRE2
    snd_piezo_health_pickup,  // 7: SND_HEALTH
    snd_piezo_ammo_pickup,    // 8: SND_AMMO
    snd_piezo_player_death,   // 9: SND_PLAYER_DEATH
    snd_piezo_level_complete  // 10: SND_LEVEL_DONE
};

// =========================================================
// PLAYBACK STATE VARIABLES
// =========================================================
static const Tone* current_sequence = NULL;
static int current_step = 0;
static int step_timer_ms = 0;

static void set_piezo_frequency(uint16_t freq_hz) {
    if (freq_hz == 0) {
        TIM4_CCR1 = 0; // 0% Duty Cycle -> Silence
    } else {
        uint32_t arr_val = 1000000 / freq_hz;
        TIM4_ARR = arr_val - 1;
        TIM4_CCR1 = arr_val / 2; // 50% Duty Cycle for Piezo
    }
    TIM4_EGR = 1; // Force timer update
}

// =========================================================
// INITIALIZATION
// =========================================================
void audio_init(void) {
    // Enable Clocks for GPIOB and TIM4
    RCC_AHB1ENR |= (1 << 1); 
    RCC_APB1ENR |= (1 << 2); 

    // --- PIEZO MODE (TIM4_CH1 on PB6) ---
    GPIOB_MODER &= ~(3 << (6 * 2));
    GPIOB_MODER |=  (2 << (6 * 2));       // Alternate Function Mode
    GPIOB_AFRL  &= ~(0xF << (6 * 4));
    GPIOB_AFRL  |=  (2 << (6 * 4));       // AF2 = TIM4

    // Setup TIM4 for 1MHz Tick Base
    TIM4_CR1 = 0;
    TIM4_PSC = 83;             // 84MHz / 84 = 1MHz clock (Adjust to 95 if using 96MHz clock)
    TIM4_ARR = 1000;           // Default 1kHz tone
    TIM4_CCMR1 &= ~(0xFF);
    TIM4_CCMR1 |= (0x60);      // PWM Mode 1 on CH1
    TIM4_CCER |= (1 << 0);     // Enable CH1 output
    TIM4_CCR1 = 0;             // Start silent
    TIM4_CR1 |= 1;             // Start timer
}

// =========================================================
// AUDIO ROUTINES
// =========================================================
void play_sound(int sound_id) {
    if (sound_id < 0 || sound_id >= NUM_SOUNDS) return; 

    // Briefly disable interrupts to protect state variables
    __asm__ volatile("cpsid i"); 
    
    current_sequence = piezo_library[sound_id];
    current_step = 0;
    step_timer_ms = current_sequence[0].duration;
    
    set_piezo_frequency(current_sequence[0].freq);
    
    // Re-enable interrupts
    __asm__ volatile("cpsie i"); 
}

void play_bgm(int track_id) {
    __asm__ volatile("cpsid i"); 
    
    if (track_id == BGM_NONE) {
        current_bgm_sequence = NULL;
        if (current_sequence == NULL) set_piezo_frequency(0);
    } else {
        // Select the requested track
        if (track_id == BGM_SPLASH) current_bgm_sequence = snd_bgm_splash;
        else if (track_id == BGM_EERIE) current_bgm_sequence = snd_bgm_eerie;
        
        bgm_step = 0;
        bgm_timer_ms = current_bgm_sequence[0].duration;
        
        // Start immediately if no SFX is playing
        if (current_sequence == NULL) {
            set_piezo_frequency(current_bgm_sequence[0].freq);
        }
    }
    
    __asm__ volatile("cpsie i");
}

void audio_tick(void) {
    // LAYER 1: SOUND EFFECTS (Highest Priority)
    if (current_sequence) {
        if (step_timer_ms > 0) step_timer_ms--;

        if (step_timer_ms == 0) {
            current_step++;
            if (current_sequence[current_step].duration == 0 && current_sequence[current_step].freq == 0) {
                // SFX Finished! 
                current_sequence = NULL; 
                
                // If BGM is enabled, resume it immediately
                if (current_bgm_sequence != NULL) {
                    set_piezo_frequency(current_bgm_sequence[bgm_step].freq);
                } else {
                    set_piezo_frequency(0);
                }
            } else {
                set_piezo_frequency(current_sequence[current_step].freq);
                step_timer_ms = current_sequence[current_step].duration;
            }
        }
    } 
    // LAYER 2: BACKGROUND MUSIC (Plays only if no SFX is active)
    else if (current_bgm_sequence != NULL) {
        if (bgm_timer_ms > 0) bgm_timer_ms--;

        if (bgm_timer_ms == 0) {
            bgm_step++;
            
            // Loop back to the start if we hit the {0, 0} marker
            if (current_bgm_sequence[bgm_step].duration == 0 && current_bgm_sequence[bgm_step].freq == 0) {
                bgm_step = 0; 
            }
            
            set_piezo_frequency(current_bgm_sequence[bgm_step].freq);
            bgm_timer_ms = current_bgm_sequence[bgm_step].duration;
        }
    }
}
