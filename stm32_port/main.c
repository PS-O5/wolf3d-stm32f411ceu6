#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#include "include/display.h"
#include "include/input.h"

// --- ASSETS ---
#include "include/stm32_walls_8bit.h"
#include "include/map_e1m1.h"
#include "include/wolf_palette.h"
#include "include/stm32_sprites_8bit.h"
#include "include/hud_statusbar.h"
#include "include/hud_digits.h"
#include "include/hud_faces.h"

// Dedicated 25.6 KB SRAM buffer for the native 16-bit HUD
uint16_t hud_buffer[STATUSBAR_H][STATUSBAR_W];
//#define VIEW_HEIGHT (RENDER_HEIGHT - 20) // 120 - 20 = 100 rows (200 TFT pixels)
                                         
                                         
// --- BARE METAL SYSTEM REGISTERS ---
#define SYSTICK_BASE  0xE000E010
#define SCB_BASE      0xE000ED00
#define STK_CTRL      (*(volatile uint32_t *)(SYSTICK_BASE + 0x00))
#define STK_LOAD      (*(volatile uint32_t *)(SYSTICK_BASE + 0x04))
#define STK_VAL       (*(volatile uint32_t *)(SYSTICK_BASE + 0x08))
#define SCB_VTOR      (*(volatile uint32_t *)(SCB_BASE + 0x08))
#define SCB_CPACR     (*(volatile uint32_t *)(SCB_BASE + 0x88)) // <---FPU 

// --- CLOCK INIT ---
#define FLASH_BASE      0x40023C00
#define FLASH_ACR       (*(volatile uint32_t *)(FLASH_BASE + 0x00))

#define RCC_BASE        0x40023800
#define RCC_CR          (*(volatile uint32_t *)(RCC_BASE + 0x00))
#define RCC_PLLCFGR     (*(volatile uint32_t *)(RCC_BASE + 0x04))
#define RCC_CFGR        (*(volatile uint32_t *)(RCC_BASE + 0x08))

static volatile uint32_t ms_ticks = 0;

void SysTick_Handler(void) {
    ms_ticks++;
    input_tick(); // Feed the debounce shift registers
}

void delay_ms(uint32_t ms) {
    uint32_t start_tick = ms_ticks;
    while ((ms_ticks - start_tick) < ms) {
        //__asm__("nop");
        __asm__ volatile ("wfi"); // Sleeps CPU until SysTick (or any interrupt) wakes it
    }
}

// --- ENGINE CONSTANTS & MACROS ---
#define FX_SHIFT 16
#define FX_ONE   (1 << FX_SHIFT)
#define INT2FX(i)   ((i) << FX_SHIFT)
#define FX2INT(x)   ((x) >> FX_SHIFT)
#define ABS(x)      ((x) < 0 ? -(x) : (x))
#define FLT2FX(f)   ((int32_t)((f) * (1 << FX_SHIFT)))
#define FX_MUL(a,b) (int32_t)(((int64_t)(a) * (b)) >> FX_SHIFT)
#define FX_DIV(a,b) (int32_t)(((int64_t)(a) << FX_SHIFT) / (b))
#define TRANSPARENCY_COLOR 255

// --- GLOBAL BUFFERS ---
uint8_t  frame_buffer_8bit[RENDER_WIDTH * RENDER_HEIGHT];
uint16_t base_palette[256];
uint16_t active_palette[256]; // Used for damage/pickup flashing

// --- GAME STATE ---
int32_t pos_x, pos_y, dir_x, dir_y, plane_x, plane_y;
//float   bob_time = 0.0f;
int bob_counter = 0;
int     weapon_frame = 0; 
int     player_ammo = 8;
int     player_health = 100;
int     game_state = 3; // 3 = Boot Splash, 0 = Play, 1 = Dead, 2 = Win
int     death_fade = 0;
int     flash_timer = 0;
uint16_t flash_color_16 = 0;
int player_angle = 0; // New LUT range from 0 to 511

// --- DOOR STATE MACHINE ---

#define MAX_ACTIVE_DOORS 16
uint16_t active_door_list[MAX_ACTIVE_DOORS];
int num_active_doors = 0;
uint8_t door_state[64 * 64]  = {0}; 
int32_t door_timer[64 * 64]  = {0};
int32_t door_offset[64 * 64] = {0}; 

// --- FONTS ---
const uint8_t font_num_3x5[10][5] = {
    {0x7,0x5,0x5,0x5,0x7}, {0x2,0x6,0x2,0x2,0x7}, {0x7,0x1,0x7,0x4,0x7}, {0x7,0x1,0x7,0x1,0x7},
    {0x5,0x5,0x7,0x1,0x1}, {0x7,0x4,0x7,0x1,0x7}, {0x7,0x4,0x7,0x5,0x7}, {0x7,0x1,0x2,0x2,0x2},
    {0x7,0x5,0x7,0x5,0x7}, {0x7,0x5,0x7,0x1,0x7}
};
const uint8_t font_alpha_3x5[26][5] = {
    {0x2,0x5,0x7,0x5,0x5}, {0x6,0x5,0x6,0x5,0x6}, {0x3,0x4,0x4,0x4,0x3}, {0x6,0x5,0x5,0x5,0x6},
    {0x7,0x4,0x6,0x4,0x7}, {0x7,0x4,0x6,0x4,0x4}, {0x3,0x4,0x5,0x5,0x3}, {0x5,0x5,0x7,0x5,0x5},
    {0x7,0x2,0x2,0x2,0x7}, {0x1,0x1,0x1,0x5,0x2}, {0x5,0x5,0x6,0x5,0x5}, {0x4,0x4,0x4,0x4,0x7},
    {0x5,0x7,0x5,0x5,0x5}, {0x6,0x5,0x5,0x5,0x5}, {0x2,0x5,0x5,0x5,0x2}, {0x6,0x5,0x6,0x4,0x4},
    {0x2,0x5,0x5,0x6,0x3}, {0x6,0x5,0x6,0x5,0x5}, {0x3,0x4,0x2,0x1,0x6}, {0x7,0x2,0x2,0x2,0x2},
    {0x5,0x5,0x5,0x5,0x7}, {0x5,0x5,0x5,0x2,0x2}, {0x5,0x5,0x5,0x7,0x5}, {0x5,0x5,0x2,0x5,0x5},
    {0x5,0x5,0x2,0x2,0x2}, {0x7,0x1,0x2,0x4,0x7}
};

// Adjust width/height to match digit header and hardware
#define DIGIT_WIDTH  8
#define DIGIT_HEIGHT 12


// --- SPRITES ---
int32_t z_buffer[RENDER_WIDTH];

typedef struct {
    int32_t x, y;
    int texture_id;
    int type;   
    int active; 
    int state;  
    int health;
    int tick;   
} Sprite;

#define NUM_WORLD_SPRITES 52 

const Sprite initial_sprites[NUM_WORLD_SPRITES] = {
    // --- ORIGINAL 32 SPRITES (Guards, Dogs, Pickups) ---
    { FLT2FX(59.5f), FLT2FX(38.5f), 50, 2, 1, 0, 25, 0 }, { FLT2FX(34.5f), FLT2FX(36.5f), 50, 2, 1, 0, 25, 0 },
    { FLT2FX(28.5f), FLT2FX(33.5f), 50, 2, 1, 0, 25, 0 }, { FLT2FX(34.5f), FLT2FX(8.5f),  50, 2, 1, 0, 25, 0 },
    { FLT2FX(22.5f), FLT2FX(18.5f), 50, 2, 1, 0, 25, 0 }, { FLT2FX(14.5f), FLT2FX(20.5f), 50, 2, 1, 0, 25, 0 },
    { FLT2FX(6.5f),  FLT2FX(17.5f), 50, 2, 1, 0, 25, 0 }, { FLT2FX(7.5f),  FLT2FX(30.5f), 50, 2, 1, 0, 25, 0 },
    { FLT2FX(14.5f), FLT2FX(33.5f), 50, 2, 1, 0, 25, 0 }, { FLT2FX(2.5f),  FLT2FX(28.5f), 50, 2, 1, 0, 25, 0 },
    { FLT2FX(20.5f), FLT2FX(47.5f), 50, 2, 1, 0, 25, 0 }, { FLT2FX(40.5f), FLT2FX(60.5f), 50, 2, 1, 0, 25, 0 },
    { FLT2FX(54.5f), FLT2FX(29.5f), 99, 3, 1, 0, 15, 0 }, { FLT2FX(36.5f), FLT2FX(31.5f), 99, 3, 1, 0, 15, 0 },
    { FLT2FX(28.5f), FLT2FX(60.5f), 99, 3, 1, 0, 15, 0 }, { FLT2FX(38.5f), FLT2FX(13.5f), 99, 3, 1, 0, 15, 0 },
    { FLT2FX(14.5f), FLT2FX(14.5f), 99, 3, 1, 0, 15, 0 }, { FLT2FX(10.5f), FLT2FX(37.5f), 99, 3, 1, 0, 15, 0 },
    { FLT2FX(1.5f),  FLT2FX(39.5f), 99, 3, 1, 0, 15, 0 }, { FLT2FX(36.5f), FLT2FX(22.5f), 99, 3, 1, 0, 15, 0 },
    { FLT2FX(59.5f), FLT2FX(33.5f), 27, 1, 1, 0, 0, 0 },  { FLT2FX(32.5f), FLT2FX(61.5f), 27, 1, 1, 0, 0, 0 },
    { FLT2FX(34.5f), FLT2FX(61.5f), 27, 1, 1, 0, 0, 0 },  { FLT2FX(36.5f), FLT2FX(61.5f), 27, 1, 1, 0, 0, 0 },
    { FLT2FX(8.5f),  FLT2FX(15.5f), 27, 1, 1, 0, 0, 0 },  { FLT2FX(57.5f), FLT2FX(33.5f), 28, 1, 1, 0, 0, 0 },
    { FLT2FX(29.5f), FLT2FX(53.5f), 28, 1, 1, 0, 0, 0 },  { FLT2FX(29.5f), FLT2FX(57.5f), 28, 1, 1, 0, 0, 0 },
    { FLT2FX(38.5f), FLT2FX(57.5f), 28, 1, 1, 0, 0, 0 },  { FLT2FX(38.5f), FLT2FX(53.5f), 28, 1, 1, 0, 0, 0 },
    { FLT2FX(42.5f), FLT2FX(19.5f), 28, 1, 1, 0, 0, 0 },  { FLT2FX(8.5f),  FLT2FX(19.5f), 28, 1, 1, 0, 0, 0 },

    // --- NEW STATIC SCENERY (Type 0) & DEAD GUARD ---
    
    // Spawned Dead Guard (Type 2, State 2, Tick 32 forces final dead texture 95)
    { FLT2FX(51.5f), FLT2FX(44.5f), 95, 2, 1, 2, 0, 32 },

    // Tables (Texture 4, Type 0)
    { FLT2FX(59.5f), FLT2FX(31.5f), 4, 0, 1, 0, 0, 0 },
    { FLT2FX(7.5f),  FLT2FX(15.5f), 4, 0, 1, 0, 0, 0 },
    { FLT2FX(8.5f),  FLT2FX(35.5f), 4, 0, 1, 0, 0, 0 },

    // Barrels (Texture 3, Type 0)
    { FLT2FX(61.5f), FLT2FX(37.5f), 3, 0, 1, 0, 0, 0 },
    { FLT2FX(33.5f), FLT2FX(16.5f), 3, 0, 1, 0, 0, 0 },
    { FLT2FX(35.5f), FLT2FX(16.5f), 3, 0, 1, 0, 0, 0 },
    { FLT2FX(6.5f),  FLT2FX(32.5f), 3, 0, 1, 0, 0, 0 },
    { FLT2FX(6.5f),  FLT2FX(34.5f), 3, 0, 1, 0, 0, 0 },

    // Pots (Texture 10, Type 0)
    { FLT2FX(32.5f), FLT2FX(29.5f), 10, 0, 1, 0, 0, 0 },
    { FLT2FX(36.5f), FLT2FX(29.5f), 10, 0, 1, 0, 0, 0 },
    { FLT2FX(39.5f), FLT2FX(10.5f), 10, 0, 1, 0, 0, 0 },
    { FLT2FX(39.5f), FLT2FX(12.5f), 10, 0, 1, 0, 0, 0 },
    { FLT2FX(9.5f),  FLT2FX(37.5f), 10, 0, 1, 0, 0, 0 },
    { FLT2FX(11.5f), FLT2FX(37.5f), 10, 0, 1, 0, 0, 0 },

    // Lamps (Texture 5, Type 0)
    { FLT2FX(28.5f), FLT2FX(31.5f), 5, 0, 1, 0, 0, 0 },
    { FLT2FX(29.5f), FLT2FX(10.5f), 5, 0, 1, 0, 0, 0 },
    { FLT2FX(29.5f), FLT2FX(12.5f), 5, 0, 1, 0, 0, 0 },
    { FLT2FX(1.5f),  FLT2FX(48.5f), 5, 0, 1, 0, 0, 0 },
    { FLT2FX(14.5f), FLT2FX(29.5f), 5, 0, 1, 0, 0, 0 }
};

// --- MATH LUT ---

// 512-entry sine table for fixed-point math (Angle 0 to 511 represents 0 to 2*PI)
const int32_t fx_sin_lut[512] = {
    0, 804, 1608, 2412, 3215, 4018, 4821, 5622, 6423, 7223, 8022, 8819, 9615, 10410, 11204, 11995,
    12785, 13573, 14359, 15142, 15923, 16702, 17479, 18253, 19024, 19792, 20557, 21319, 22078, 22833, 23586, 24334,
    25079, 25820, 26557, 27291, 28020, 28745, 29465, 30181, 30893, 31600, 32302, 32999, 33692, 34379, 35061, 35738,
    36409, 37075, 37736, 38390, 39039, 39682, 40319, 40950, 41575, 42194, 42806, 43412, 44011, 44603, 45189, 45768,
    46340, 46906, 47464, 48015, 48558, 49095, 49624, 50146, 50660, 51166, 51665, 52155, 52639, 53115, 53583, 54043,
    54495, 54939, 55375, 55803, 56223, 56634, 57037, 57431, 57817, 58195, 58564, 58925, 59277, 59621, 59956, 60282,
    60599, 60908, 61208, 61499, 61781, 62054, 62319, 62574, 62821, 63059, 63288, 63508, 63719, 63921, 64114, 64298,
    64472, 64638, 64794, 64941, 65079, 65208, 65328, 65438, 65539, 65631, 65713, 65786, 65850, 65905, 65950, 65986,
    66013, 66031, 66039, 66039, 66030, 66011, 65983, 65946, 65900, 65844, 65779, 65705, 65622, 65530, 65428, 65318,
    65198, 65069, 64931, 64784, 64628, 64463, 64289, 64106, 63914, 63713, 63503, 63284, 63056, 62820, 62574, 62320,
    62057, 61785, 61504, 61215, 60917, 60610, 60295, 59971, 59639, 59298, 58948, 58590, 58223, 57848, 57465, 57073,
    56673, 56265, 55848, 55423, 54989, 54548, 54098, 53641, 53175, 52701, 52219, 51730, 51232, 50727, 50214, 49693,
    49164, 48628, 48084, 47533, 46974, 46408, 45834, 45253, 44665, 44069, 43466, 42856, 42239, 41615, 40984, 40346,
    39701, 39049, 38390, 37725, 37053, 36374, 35689, 34997, 34299, 33594, 32883, 32166, 31442, 30713, 29977, 29235,
    28488, 27735, 26976, 26211, 25441, 24665, 23884, 23098, 22306, 21509, 20707, 19900, 19088, 18271, 17449, 16623,
    15792, 14956, 14116, 13271, 12423, 11570, 10713, 9852, 8987, 8118, 7246, 6370, 5491, 4608, 3723, 2834,
    1942, 1048, 151, -747, -1645, -2543, -3439, -4335, -5229, -6121, -7011, -7899, -8785, -9668, -10549, -11426,
    -12301, -13173, -14041, -14906, -15768, -16626, -17480, -18331, -19177, -20020, -20858, -21692, -22522, -23347, -24168, -24984,
    -25795, -26601, -27402, -28198, -28989, -29775, -30555, -31330, -32099, -32863, -33621, -34373, -35119, -35859, -36594, -37322,
    -38044, -38759, -39469, -40172, -40868, -41558, -42241, -42917, -43586, -44249, -44904, -45552, -46193, -46826, -47452, -48071,
    -48682, -49285, -49880, -50467, -51046, -51617, -52180, -52735, -53281, -53818, -54347, -54867, -55379, -55881, -56375, -56860,
    -57335, -57801, -58258, -58706, -59144, -59572, -59991, -60400, -60799, -61188, -61567, -61936, -62295, -62644, -62983, -63312,
    -63630, -63938, -64235, -64522, -64799, -65065, -65320, -65565, -65799, -66023, -66236, -66438, -66629, -66810, -66980, -67139,
    -67287, -67424, -67550, -67666, -67770, -67863, -67945, -68016, -68076, -68124, -68162, -68188, -68203, -68207, -68200, -68181,
    -68151, -68110, -68058, -67995, -67920, -67834, -67737, -67629, -67509, -67379, -67237, -67084, -66920, -66744, -66558, -66360,
    -66151, -65931, -65700, -65457, -65203, -64938, -64662, -64375, -64077, -63768, -63448, -63116, -62774, -62421, -62057, -61682,
    -61296, -60899, -60492, -60073, -59644, -59204, -58753, -58292, -57820, -57338, -56845, -56342, -55828, -55304, -54770, -54226,
    -53671, -53107, -52533, -51949, -51355, -50751, -50138, -49515, -48883, -48241, -47590, -46930, -46260, -45582, -44894, -44198,
    -43493, -42779, -42057, -41326, -40587, -39840, -39084, -38321, -37549, -36770, -35983, -35188, -34386, -33576, -32759, -31934,
    -31102, -30263, -29417, -28564, -27704, -26837, -25964, -25084, -24198, -23305, -22406, -21501, -20589, -19672, -18749, -17820,
    -16886, -15946, -15000, -14049, -13093, -12132, -11166, -10195, -9220, -8240, -7256, -6267, -5274, -4277, -3277, -2273
};

// Precomputed weapon bobbing offsets (32-frame cycle)
const int8_t bob_y_lut[32] = {
    0, 1, 2, 3, 4, 5, 5, 5, 5, 5, 5, 4, 4, 3, 1, 0, 
    0, 1, 2, 3, 4, 5, 5, 5, 5, 5, 5, 4, 4, 3, 1, 0
};
const int8_t bob_x_lut[32] = {
    3, 2, 2, 2, 2, 1, 1, 0, 0, 0, -1, -1, -2, -2, -2, -3, 
   -3, -2, -2, -2, -2, -1, -1, 0, 0, 0, 1, 1, 2, 2, 2, 3
};

Sprite sprites[NUM_WORLD_SPRITES];

// --- PALETTE SYSTEM ---
void init_vga_palette(void) {
    for (int i = 0; i < 256; i++) {
        uint8_t r = wolf_rgb[i][0];
        uint8_t g = wolf_rgb[i][1];
        uint8_t b = wolf_rgb[i][2];
        // STM32 hardware native RGB565 (MSB first via SPI)
        base_palette[i] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        active_palette[i] = base_palette[i];
    }
}

void trigger_flash(uint16_t color16, int duration) {
    flash_timer = duration;
    // Calculate the blended palette ONCE
    for (int i = 0; i < 256; i++) {
        active_palette[i] = ((base_palette[i] & 0xF7DE) >> 1) + ((color16 & 0xF7DE) >> 1);
    }
}

/*
void apply_translucent_flash(void) {
    if (flash_timer > 0) {
        for (int i = 0; i < 256; i++) {
            // Fast 50% Alpha Blend directly on the palette
            active_palette[i] = ((base_palette[i] & 0xF7DE) >> 1) + ((flash_color_16 & 0xF7DE) >> 1);
        }
        flash_timer--;
    } else {
        memcpy(active_palette, base_palette, sizeof(base_palette));
    }
}
*/

// --- ENGINE LOGIC ---
int get_map_tile(int x, int y) {
    if (x < 0 || x >= 64 || y < 0 || y >= 64) return 1;
    return map_e1m1[y * 64 + x];
}

int check_sprite_collision(int32_t target_x, int32_t target_y) {
    int32_t radius = FLT2FX(0.3f);
    for (int i = 0; i < NUM_WORLD_SPRITES; i++) {
        if (!sprites[i].active) continue;
        int blocking = (sprites[i].type == 0) || (sprites[i].type == 2 && sprites[i].state != 2);
        if (blocking) {
            if (target_x > sprites[i].x - radius && target_x < sprites[i].x + radius &&
                target_y > sprites[i].y - radius && target_y < sprites[i].y + radius)
                return 1;
        }
    }
    return 0;
}

int is_passable(int32_t fx, int32_t fy) {
    int32_t radius = FLT2FX(0.35f); 
    int corners[4][2] = {
        { FX2INT(fx - radius), FX2INT(fy - radius) },
        { FX2INT(fx + radius), FX2INT(fy - radius) },
        { FX2INT(fx - radius), FX2INT(fy + radius) },
        { FX2INT(fx + radius), FX2INT(fy + radius) }
    };
    for (int i = 0; i < 4; i++) {
        int tile = get_map_tile(corners[i][0], corners[i][1]);
        if (tile > 0 && tile < 90) return 0; 
        if (tile >= 90 && tile <= 101) {
            if (door_state[corners[i][1] * 64 + corners[i][0]] != 2) return 0; 
        }
    }
    if (check_sprite_collision(fx, fy)) return 0;
    return 1;
}

int is_passable_for_enemy(int32_t fx, int32_t fy, int self_index) {
    int map_x = FX2INT(fx);
    int map_y = FX2INT(fy);
    int tile  = get_map_tile(map_x, map_y);
    if (tile > 0 && tile < 90) return 0; 
    if (tile >= 90 && tile <= 101) {     
        if (door_state[map_y * 64 + map_x] != 2) return 0;
    }
    int32_t radius = FLT2FX(0.4f);
    for (int i = 0; i < NUM_WORLD_SPRITES; i++) {
        if (i == self_index || !sprites[i].active) continue;
        int blocking = (sprites[i].type == 0) || (sprites[i].type == 2 && sprites[i].state != 2);
        if (blocking) {
            if (fx > sprites[i].x - radius && fx < sprites[i].x + radius &&
                fy > sprites[i].y - radius && fy < sprites[i].y + radius) return 0;
        }
    }
    return 1;
}

int check_line_of_sight(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    int x0 = FX2INT(x1), y0 = FX2INT(y1);
    int x1_grid = FX2INT(x2), y1_grid = FX2INT(y2);
    int dx = abs(x1_grid - x0), sx = x0 < x1_grid ? 1 : -1;
    int dy = -abs(y1_grid - y0), sy = y0 < y1_grid ? 1 : -1;
    int err = dx + dy; 
    
    while (1) {
        if (x0 != FX2INT(x1) || y0 != FX2INT(y1)) { 
            int tile = get_map_tile(x0, y0);
            if (tile > 0 && tile < 90) return 0; 
            if (tile >= 90 && tile <= 101 && door_state[y0 * 64 + x0] != 2) return 0; 
        }
        if (x0 == x1_grid && y0 == y1_grid) break; 
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
    return 1; 
}

void try_open_door(void) {
    int tx = FX2INT(pos_x + dir_x);
    int ty = FX2INT(pos_y + dir_y);
    int tile = get_map_tile(tx, ty);
    
    if (tile >= 90 && tile <= 101) {
        int idx = ty * 64 + tx;
        if (door_state[idx] == 0) {
            door_state[idx] = 1;
            // Add to active updating list
            if (num_active_doors < MAX_ACTIVE_DOORS) {
                active_door_list[num_active_doors++] = idx;
            }
        }
    }
    
    if (tile == 21) { 
        game_state = 2;
        death_fade = 0;
    }
}

void update_doors(void) {
    for (int i = 0; i < num_active_doors; ) {
        int idx = active_door_list[i];
        int keep_active = 1;

        if (door_state[idx] == 1) { // Opening
            door_offset[idx] += 2;
            if (door_offset[idx] >= 64) {
                door_offset[idx] = 64;
                door_state[idx] = 2; 
                door_timer[idx] = 150; 
            }
        } else if (door_state[idx] == 2) { // Open Wait
            if (door_timer[idx] > 0) door_timer[idx]--;
            if (door_timer[idx] <= 0) {
                int x = idx % 64;
                int y = idx / 64;
                int32_t r = FLT2FX(0.4f); 
                int block = (pos_x + r > INT2FX(x) && pos_x - r < INT2FX(x + 1) &&
                             pos_y + r > INT2FX(y) && pos_y - r < INT2FX(y + 1));

                for (int s = 0; s < NUM_WORLD_SPRITES && !block; s++) {
                    if (sprites[s].active && sprites[s].state != 2) { 
                        if (FX2INT(sprites[s].x) == x && FX2INT(sprites[s].y) == y) block = 1;
                    }
                }
                if (block) door_timer[idx] = 30; 
                else door_state[idx] = 3; 
            }
        } else if (door_state[idx] == 3) { // Closing
            door_offset[idx] -= 2;
            if (door_offset[idx] <= 0) {
                door_offset[idx] = 0;
                door_state[idx] = 0; 
                keep_active = 0; // Door fully closed, flag for removal
            }
        }

        // List management: remove inactive doors by swapping with the last element
        if (!keep_active) {
            num_active_doors--;
            active_door_list[i] = active_door_list[num_active_doors];
        } else {
            i++;
        }
    }
}

// --- RENDER PIPELINE ---
void render_frame(void) {
        for (int y = 0; y < RENDER_HEIGHT; y++) {
        uint8_t color = (y < RENDER_HEIGHT / 2) ? 29 : 27; // Ceiling / Floor
        memset(&frame_buffer_8bit[y * RENDER_WIDTH], color, RENDER_WIDTH);
        }
    for (int x = 0; x < RENDER_WIDTH; x++) {
        int32_t camera_x  = FX_DIV(INT2FX(2 * x), INT2FX(RENDER_WIDTH)) - INT2FX(1);
        int32_t ray_dir_x = dir_x + FX_MUL(plane_x, camera_x);
        int32_t ray_dir_y = dir_y + FX_MUL(plane_y, camera_x);

        int map_x = FX2INT(pos_x);
        int map_y = FX2INT(pos_y);

        //float f_ray_x = (float)ray_dir_x / 65536.0f;
        //float f_ray_y = (float)ray_dir_y / 65536.0f;
        /*
        int32_t delta_dist_x = (ray_dir_x == 0) ? INT2FX(1000) : FLT2FX(__builtin_fabsf(1.0f / f_ray_x));
        int32_t delta_dist_y = (ray_dir_y == 0) ? INT2FX(1000) : FLT2FX(__builtin_fabsf(1.0f / f_ray_y));
        */

        
        // FX_ONE is 65536. Dividing FX_ONE by the ray direction gives us the exact fixed-point ratio.
        int32_t delta_dist_x = (ray_dir_x == 0) ? INT2FX(1000) : ABS(FX_DIV(FX_ONE, ray_dir_x));
        int32_t delta_dist_y = (ray_dir_y == 0) ? INT2FX(1000) : ABS(FX_DIV(FX_ONE, ray_dir_y));


        int32_t side_dist_x, side_dist_y;
        int step_x, step_y;
        int hit = 0, side = 0, tile_hit = 0, is_door_face = 0;

        if (ray_dir_x < 0) { step_x = -1; side_dist_x = FX_MUL(pos_x - INT2FX(map_x), delta_dist_x); }
        else               { step_x =  1; side_dist_x = FX_MUL(INT2FX(map_x + 1) - pos_x, delta_dist_x); }
        if (ray_dir_y < 0) { step_y = -1; side_dist_y = FX_MUL(pos_y - INT2FX(map_y), delta_dist_y); }
        else               { step_y =  1; side_dist_y = FX_MUL(INT2FX(map_y + 1) - pos_y, delta_dist_y); }

        while (!hit) {
            if (side_dist_x < side_dist_y) { side_dist_x += delta_dist_x; map_x += step_x; side = 0; }
            else                           { side_dist_y += delta_dist_y; map_y += step_y; side = 1; }

            if (map_x < 0 || map_x >= 64 || map_y < 0 || map_y >= 64) {
                hit = 1; tile_hit = 1; break;
            }

            tile_hit = get_map_tile(map_x, map_y);

            if (tile_hit > 0 && tile_hit < 90) {
                hit = 1;
            } else if (tile_hit >= 90 && tile_hit <= 101) {
                is_door_face = ((tile_hit == 90 && side == 0) || (tile_hit == 91 && side == 1));
                if (is_door_face) {
                    int d_state = door_state[map_y * 64 + map_x];
                    if (d_state == 2) continue;
                    if (d_state == 0) { hit = 1; continue; }

                    int32_t perp = (side == 0) ? (side_dist_x - delta_dist_x) : (side_dist_y - delta_dist_y);
                    int32_t wx = (side == 0) ? (pos_y + FX_MUL(perp, ray_dir_y)) : (pos_x + FX_MUL(perp, ray_dir_x));
                    wx &= (FX_ONE - 1);

                    int tx = FX_MUL(wx, INT2FX(64)) >> FX_SHIFT;
                    if (side == 0 && ray_dir_x < 0) tx = 64 - tx - 1;
                    if (side == 1 && ray_dir_y > 0) tx = 64 - tx - 1;

                    if (tx < door_offset[map_y * 64 + map_x]) continue;
                    hit = 1;
                } else hit = 1;
            }
        }

        int32_t perp_wall_dist = (side == 0) ? (side_dist_x - delta_dist_x) : (side_dist_y - delta_dist_y);
        if (perp_wall_dist < INT2FX(1) / 4) perp_wall_dist = INT2FX(1) / 4;

        int line_height = FX2INT(FX_DIV(INT2FX(RENDER_HEIGHT), perp_wall_dist));
        if (line_height <= 0) line_height = 1;

        int draw_start = -line_height / 2 + RENDER_HEIGHT / 2;
        if (draw_start < 0) draw_start = 0;
        int draw_end = line_height / 2 + RENDER_HEIGHT / 2;
        if (draw_end >= RENDER_HEIGHT) draw_end = RENDER_HEIGHT - 1;

        /* Uncompressed wall sprite logic
        int tex_num;
        if (tile_hit >= 90) tex_num = is_door_face ? 98 : 4;
        else tex_num = (tile_hit - 1) * 2;
        if (side == 1) tex_num += 1;
        tex_num = tex_num % NUM_WALLS;
        */

        int tex_num;
        if (tile_hit >= 90) {
            tex_num = is_door_face ? 98 : 4;
            if (side == 1) tex_num += 1;
        } else {
            tex_num = (tile_hit - 1) * 2;
            if (side == 1) tex_num += 1;
            
            // Safety clamp so we never exceed our wall_lut[100] array
            if (tex_num > 99) tex_num = 0; 
        }
        // Removed the modulo math! The LUT handles the compression now.
        
        

        int32_t wall_x = (side == 0) ? (pos_y + FX_MUL(perp_wall_dist, ray_dir_y)) : (pos_x + FX_MUL(perp_wall_dist, ray_dir_x));
        wall_x &= (FX_ONE - 1);

        int tex_x = FX_MUL(wall_x, INT2FX(64)) >> FX_SHIFT;
        if (side == 0 && ray_dir_x < 0) tex_x = 64 - tex_x - 1;
        if (side == 1 && ray_dir_y > 0) tex_x = 64 - tex_x - 1;

        if (tile_hit >= 90 && is_door_face) {
            tex_x -= door_offset[map_y * 64 + map_x];
            if (tex_x < 0) tex_x = 0;
        }

        int32_t step = FX_DIV(INT2FX(64), INT2FX(line_height));
        int32_t tex_pos = FX_MUL(INT2FX(draw_start - RENDER_HEIGHT / 2 + line_height / 2), step);
        if (tex_x < 0) tex_x = 0;
        if (tex_x > 63) tex_x = 63;

        z_buffer[x] = perp_wall_dist;
        for (int y = draw_start; y < draw_end; y++) {
            int tex_y = FX2INT(tex_pos) & 63;
            tex_pos += step;
            //frame_buffer_8bit[y * RENDER_WIDTH + x] = wall_textures[tex_num][tex_x * 64 + tex_y];
            frame_buffer_8bit[y * RENDER_WIDTH + x] = wall_textures[wall_lut[tex_num]][tex_x * 64 + tex_y];
        }
    }
}

void draw_sprites(void) {
    int visible_sprites[NUM_WORLD_SPRITES];
    int32_t sprite_depth[NUM_WORLD_SPRITES]; 
    int num_visible = 0;

    int32_t det = FX_MUL(plane_x, dir_y) - FX_MUL(dir_x, plane_y);
    int32_t inv_det = (det == 0) ? 0 : FX_DIV(INT2FX(1), det);

    if (inv_det == 0) return; // Safety check

    // 1. Depth Calculation & Visibility Culling
    for (int i = 0; i < NUM_WORLD_SPRITES; i++) {
        if (sprites[i].active) {
            int32_t sx = sprites[i].x - pos_x;
            int32_t sy = sprites[i].y - pos_y;
            int32_t transform_y = FX_MUL(inv_det, -FX_MUL(plane_y, sx) + FX_MUL(plane_x, sy));

            // Cull anything behind the camera plane immediately
            if (transform_y > 0) {
                sprite_depth[i] = transform_y;
                visible_sprites[num_visible] = i;
                num_visible++;
            }
        }
    }

    // 2. Insertion Sort (Only on visible sprites, Farthest to Nearest)
    for (int i = 1; i < num_visible; i++) {
        int key_index = visible_sprites[i];
        int32_t key_depth = sprite_depth[key_index];
        int j = i - 1;

        while (j >= 0 && sprite_depth[visible_sprites[j]] < key_depth) {
            visible_sprites[j + 1] = visible_sprites[j];
            j = j - 1;
        }
        visible_sprites[j + 1] = key_index;
    }

    // 3. Render Loop
    for (int k = 0; k < num_visible; k++) {
        int i = visible_sprites[k]; 
        int32_t transform_y = sprite_depth[i];
        
        // Near-clip plane cutoff
        if (transform_y < FLT2FX(0.1f)) continue;

        int32_t sx = sprites[i].x - pos_x;
        int32_t sy = sprites[i].y - pos_y;
        int32_t transform_x = FX_MUL(inv_det,  FX_MUL(dir_y, sx) - FX_MUL(dir_x, sy));

        int32_t screen_ratio = FX_DIV(transform_x, transform_y);
        int sprite_screen_x  = (RENDER_WIDTH / 2) + FX2INT(FX_MUL(INT2FX(RENDER_WIDTH / 2), screen_ratio));

        int sprite_height = ABS(FX2INT(FX_DIV(INT2FX(RENDER_HEIGHT), transform_y)));
        
        // Prevent division by zero in step calculation
        if (sprite_height == 0) continue; 

        int draw_start_y  = -sprite_height / 2 + RENDER_HEIGHT / 2;
        if (draw_start_y < 0) draw_start_y = 0;
        int draw_end_y    =  sprite_height / 2 + RENDER_HEIGHT / 2;
        if (draw_end_y >= RENDER_HEIGHT) draw_end_y = RENDER_HEIGHT - 1;

        int sprite_width = sprite_height;
        int draw_start_x = -sprite_width / 2 + sprite_screen_x;
        if (draw_start_x < 0) draw_start_x = 0;
        int draw_end_x   =  sprite_width / 2 + sprite_screen_x;
        if (draw_end_x >= RENDER_WIDTH) draw_end_x = RENDER_WIDTH - 1;

        // --- NEW: Fixed-point texture stepping (8-bit fractional) ---
        int32_t tex_step = (64 << 8) / sprite_height; 
        int32_t tex_pos_start = (draw_start_y - RENDER_HEIGHT / 2 + sprite_height / 2) * tex_step;

        for (int stripe = draw_start_x; stripe < draw_end_x; stripe++) {
            if (transform_y >= z_buffer[stripe]) continue; 
            
            int tex_x = ((stripe - (-sprite_width / 2 + sprite_screen_x)) * 64) / sprite_width;
            if (tex_x < 0) tex_x = 0;
            if (tex_x > 63) tex_x = 63;

            int32_t tex_pos = tex_pos_start; // Reset Y step for this column

            for (int y = draw_start_y; y < draw_end_y; y++) {
                int tex_y = (tex_pos >> 8) & 63;
                tex_pos += tex_step; // Accumulate instead of divide

                uint8_t color = sprite_textures[sprite_lut[sprites[i].texture_id]][tex_y * 64 + tex_x];
                if (color != TRANSPARENCY_COLOR) {
                    frame_buffer_8bit[y * RENDER_WIDTH + stripe] = color;
                }
            }
        }
    }
}


void draw_weapon(void) {
    //int bob_y = (int)(fabsf(sinf(bob_time)) * 6.0f);
    //int bob_x = (int)(cosf(bob_time) * 3.0f);

    int bob_y = bob_y_lut[bob_counter];
    int bob_x = bob_x_lut[bob_counter];

    int scale   = 1;
    int start_x = (RENDER_WIDTH / 2) - (32 * scale) + bob_x;
    int start_y = RENDER_HEIGHT - (64 * scale) + bob_y;

    int weapon_tex_id = 421; 
    if (weapon_frame > 10) weapon_tex_id = 421; 
    else if (weapon_frame > 5) weapon_tex_id = 422;
    else if (weapon_frame > 0) weapon_tex_id = 423;

    if (weapon_frame > 0) {
        start_y += 4;
        weapon_tex_id += 1;
        weapon_frame--;
    }

    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            //uint8_t color = sprite_textures[weapon_tex_id][y * 64 + x];
            uint8_t color = sprite_textures[sprite_lut[weapon_tex_id]][y * 64 + x];
            if (color == TRANSPARENCY_COLOR) continue;
            int screen_x = start_x + x;
            int screen_y = start_y + y;
            if (screen_x >= 0 && screen_x < RENDER_WIDTH && screen_y >= 0 && screen_y < RENDER_HEIGHT) {
                frame_buffer_8bit[screen_y * RENDER_WIDTH + screen_x] = color;
            }
        }
    }
}

// --- HUD RENDERING ---
void draw_mini_string(int x, int y, const char* str, uint8_t color_idx) {
    for (int i = 0; str[i] != '\0'; i++) {
        char c = str[i];
        if (c == ' ') continue; 
        const uint8_t* bitmap = NULL;
        if (c >= '0' && c <= '9')      bitmap = font_num_3x5[c - '0'];
        else if (c >= 'A' && c <= 'Z') bitmap = font_alpha_3x5[c - 'A'];
        
        if (bitmap) {
            for (int row = 0; row < 5; row++) {
                uint8_t bits = bitmap[row];
                for (int col = 0; col < 3; col++) {
                    if (bits & (1 << (2 - col))) {
                        int px = x + (i * 4) + col; 
                        int py = y + row;
                        if (px >= 0 && px < RENDER_WIDTH && py >= 0 && py < RENDER_HEIGHT) {
                            frame_buffer_8bit[py * RENDER_WIDTH + px] = color_idx;
                        }
                    }
                }
            }
        }
    }
}

void draw_number(int x, int y, int number, uint8_t color_idx) {
    char str[16];
    // Custom lightweight itoa alternative for bare metal
    int i = 0, temp = number;
    if (temp == 0) { str[i++] = '0'; }
    while (temp > 0) { str[i++] = (temp % 10) + '0'; temp /= 10; }
    str[i] = '\0';
    // Reverse string
    for (int j = 0, k = i - 1; j < k; j++, k--) { char t = str[j]; str[j] = str[k]; str[k] = t; }
    draw_mini_string(x, y, str, color_idx);
}

void draw_hud_number(int start_x, int start_y, int number, int max_digits) {
    char str[10];
    int i = 0, temp = number;
    if (temp == 0) { str[i++] = '0'; }
    while (temp > 0) { str[i++] = (temp % 10) + '0'; temp /= 10; }

    // Target dimensions for the UI
    int target_w = 8;
    int target_h = 12;
    
    // Add a 1-pixel gap so digits don't touch
    int stride_x = target_w + 1; 

    // Right-align the digits inside the box
    int cursor_x = start_x + ((max_digits - i) * stride_x);

    for (int j = i - 1; j >= 0; j--) {
        int digit_idx = str[j] - '0';
       
        for (int dy = 0; dy < target_h; dy++) {
            // Map target Y block to the 16x16 source using the header macros
            int sy1 = (dy * DIGIT_H) / target_h;
            int sy2 = ((dy + 1) * DIGIT_H - 1) / target_h;
            
            for (int dx = 0; dx < target_w; dx++) {
                // Map target X block to the 16x16 source using the header macros
                int sx1 = (dx * DIGIT_W) / target_w;
                int sx2 = ((dx + 1) * DIGIT_W - 1) / target_w;

                //uint16_t final_color = DIGIT_BG_R565_BE; 
                
                // Scan the calculated block of pixels for any foreground color
                uint16_t final_color = 0x0208; // Default to the true background
                
                // Scan the block for any foreground color
                for (int y = sy1; y <= sy2; y++) {
                    for (int x = sx1; x <= sx2; x++) {
                        uint8_t msb = hud_digits[digit_idx][y][x * 2]; 
                        uint8_t lsb = hud_digits[digit_idx][y][(x * 2) + 1];
                        uint16_t color = (msb << 8) | lsb;
                        
                        // Ignore both the documented BG and the TRUE hex BG
                        if (color != DIGIT_BG_R565_BE && color != 0x0208) {
                            // Paint the actual font pixels pure white!
                            final_color = 0xFFFF; 
                        }
                    }
                }
            
                // Only draw if we found a font pixel
                if (final_color == 0xFFFF) {
                    hud_buffer[start_y + dy][cursor_x + dx] = final_color;
                }
            }
        }
        cursor_x += stride_x;
    }
}

void draw_hud_face(void) {
    int face_idx;
    int face_row = 0;
    
    // Idle Animation State Machine (Looks left, center, right)
    static int face_timer = 0;
    static int face_state = 1; // 0=Left, 1=Center, 2=Right
    
    // Only look around if he is still alive
    if (player_health > 0) {
        face_timer++;
        if (face_timer > 45) { // Change glance every ~0.75 seconds
            face_timer = 0;
            int r = rand() % 100;
            if (r < 25) face_state = 0;
            else if (r < 50) face_state = 2;
            else face_state = 1;
        }
    } else {
        face_state = 1; // Lock to center frame if dead
    }

    // Map current health to the exact 0-7 sprite row array
    if      (player_health >= 100) face_row = 0; // Face 1
    else if (player_health >= 90)  face_row = 1; // Face 2
    else if (player_health >= 75)  face_row = 2; // Face 3
    else if (player_health >= 55)  face_row = 3; // Face 4
    else if (player_health >= 35)  face_row = 4; // Face 5
    else if (player_health >= 25)  face_row = 5; // Face 6 (Ripper mislabeled as Gatling/Ouch)
    else if (player_health >= 10)  face_row = 6; // Face 7 (Ripper mislabeled as Dead1/2/3)
    else                           face_row = 7; // Death Face (Ripper mislabeled as Dead4)
    
    // Calculate the exact 1D index from the 24-face array
    face_idx = (face_row * 3) + face_state; 

    // Centered in the proper Face box
    int face_x = 136; 

    // Clear the background
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 24; x++) {
            hud_buffer[4 + y][face_x + x] = 0; 
        }
    }

    // Draw the face
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 24; x++) {
            uint8_t msb = hud_faces[face_idx][y][x * 2];
            uint8_t lsb = hud_faces[face_idx][y][x * 2 + 1];
            uint16_t color = (msb << 8) | lsb;
            
            if (color != DIGIT_BG_R565_BE) { 
                hud_buffer[4 + y][face_x + x] = color;
            }
        }
    }
}

void draw_hud(void) {
    // 1. Parse the background directly
    for (int y = 0; y < STATUSBAR_H; y++) {
        for (int x = 0; x < STATUSBAR_W; x++) {
            uint8_t msb = hud_statusbar[y][x * 2];
            uint8_t lsb = hud_statusbar[y][x * 2 + 1];
            hud_buffer[y][x] = (msb << 8) | lsb;
        }
    }

    // 2. Overlay dynamic elements
    draw_hud_face();
    
    // 3. Exact Wolf3D Coordinates (Y=16 drops them into the black boxes)
    // format: draw_hud_number(X, Y, value, max_digits);
    draw_hud_number(16,  19, 1, 2);             // Level
    draw_hud_number(35,  19, 0, 6);             // Score
    draw_hud_number(45, 19, 0, 2);              //Lives           
    draw_hud_number(167, 19, player_health, 3); // Health
    draw_hud_number(201, 19, player_ammo, 3);   // Ammo
}

// --- GAME LOGIC UPDATE ---
void update_world(void) {
    //int32_t pickup_radius = FLT2FX(0.8f);
    int32_t enemy_speed   = FLT2FX(0.015f);
    int32_t aggro_range   = FLT2FX(6.0f);

    for (int i = 0; i < NUM_WORLD_SPRITES; i++) {
        if (!sprites[i].active) continue;

        if (sprites[i].type == 1) { 
            int32_t dist_x = ABS(pos_x - sprites[i].x);
            int32_t dist_y = ABS(pos_y - sprites[i].y);
            if (dist_x < FLT2FX(0.4f) && dist_y < FLT2FX(0.4f)) {
                if (sprites[i].texture_id == 27) {      //Medkit 
                    if (player_health < 100) {     
                        player_health += 25;
                        if (player_health > 100) player_health = 100;
                        sprites[i].active = 0;     
                        //flash_timer = 10; flash_color_16 = 0x001F; // Blue
                        trigger_flash(0x001F, 10);
                    }
                } else if (sprites[i].texture_id == 28) { //Ammo
                    player_ammo += 8;
                    sprites[i].active = 0;         
                    //flash_timer = 10; flash_color_16 = 0xFFE0; // Yellow
                    trigger_flash(0xFFE0, 10);
                }
            }
        }
        else if (sprites[i].type == 2 && sprites[i].state != 2) {
            sprites[i].tick++; 
            int32_t dx = pos_x - sprites[i].x;
            int32_t dy = pos_y - sprites[i].y;
            if (sprites[i].state == 0) { 
                if (ABS(dx) < aggro_range && ABS(dy) < aggro_range) {
                    sprites[i].state = 1; sprites[i].tick = 0;
                } else {
                    int patrol_cycle = sprites[i].tick % 240;
                    int32_t step_x = 0, step_y = 0;
                    if      (patrol_cycle < 60)  step_x =  (enemy_speed / 2); 
                    else if (patrol_cycle < 120) step_y =  (enemy_speed / 2); 
                    else if (patrol_cycle < 180) step_x = -(enemy_speed / 2); 
                    else                         step_y = -(enemy_speed / 2); 

                    if (patrol_cycle % 60 >= 10) {
                        int walk_frames[4] = {50, 58, 66, 74};
                        sprites[i].texture_id = walk_frames[(sprites[i].tick / 15) % 4];
                        if (is_passable_for_enemy(sprites[i].x + step_x, sprites[i].y, i)) sprites[i].x += step_x;
                        if (is_passable_for_enemy(sprites[i].x, sprites[i].y + step_y, i)) sprites[i].y += step_y;
                    } else sprites[i].texture_id = 50; 
                }
            }
            else if (sprites[i].state == 1) { 
                int walk_frames[4] = {50, 58, 66, 74};
                sprites[i].texture_id = walk_frames[(sprites[i].tick / 10) % 4];
                if (ABS(dx) > FLT2FX(4.0f) || ABS(dy) > FLT2FX(4.0f) || !check_line_of_sight(sprites[i].x, sprites[i].y, pos_x, pos_y)) { 
                    int32_t step_x = (dx > 0) ? enemy_speed : -enemy_speed;
                    int32_t step_y = (dy > 0) ? enemy_speed : -enemy_speed;
                    if (is_passable_for_enemy(sprites[i].x + step_x, sprites[i].y, i)) sprites[i].x += step_x;
                    if (is_passable_for_enemy(sprites[i].x, sprites[i].y + step_y, i)) sprites[i].y += step_y;
                } else {
                    sprites[i].state = 3; sprites[i].tick = 0;
                }
            } 
            else if (sprites[i].state == 3) { 
                sprites[i].texture_id = (sprites[i].tick < 15) ? 97 : 98; 
                if (sprites[i].tick == 15) { player_health -= 10; trigger_flash(0xF800, 10);}//flash_timer = 10; flash_color_16 = 0xF800
                if (sprites[i].tick > 30)  { sprites[i].state = 4; sprites[i].tick = 0; }
            }
            else if (sprites[i].state == 4) {
                int walk_frames[4] = {50, 58, 66, 74}; 
                sprites[i].texture_id = walk_frames[(sprites[i].tick / 15) % 4];
                int32_t step_x = 0, step_y = 0;
                int dir = (sprites[i].tick / 30 + i) % 4; 
                if (dir == 0) step_x = enemy_speed; else if (dir == 1) step_x = -enemy_speed;
                else if (dir == 2) step_y = enemy_speed; else step_y = -enemy_speed;
                if (is_passable_for_enemy(sprites[i].x + step_x, sprites[i].y, i)) sprites[i].x += step_x;
                if (is_passable_for_enemy(sprites[i].x, sprites[i].y + step_y, i)) sprites[i].y += step_y;
                if (sprites[i].tick > 60) { sprites[i].state = 1; sprites[i].tick = 0; }
            }
        } 
        else if (sprites[i].type == 2 && sprites[i].state == 2) {
            sprites[i].tick++;
            if (sprites[i].tick < 8)       sprites[i].texture_id = 90; 
            else if (sprites[i].tick < 16) sprites[i].texture_id = 91;
            else if (sprites[i].tick < 24) sprites[i].texture_id = 92;
            else if (sprites[i].tick < 32) sprites[i].texture_id = 93;
            else                           sprites[i].texture_id = 95; 
        }
        // ==========================================
        // --- DOG AI LOGIC (Type 3) ---
        // ==========================================
        else if (sprites[i].type == 3 && sprites[i].state != 2) {
            sprites[i].tick++; // Dog timer ticks up!
            
            int32_t dx = pos_x - sprites[i].x;
            int32_t dy = pos_y - sprites[i].y;
            int32_t dog_speed = enemy_speed + (enemy_speed / 2); 

            // State 0: Patrol / Idle
            if (sprites[i].state == 0) { 
                if (ABS(dx) < aggro_range && ABS(dy) < aggro_range) {
                    sprites[i].state = 1; 
                    sprites[i].tick = 0;
                } else {
                    int patrol_cycle = sprites[i].tick % 240;
                    int32_t step_x = 0, step_y = 0;
                    int is_moving = 1;

                    if      (patrol_cycle < 60)  step_x =  (dog_speed / 2); 
                    else if (patrol_cycle < 120) step_y =  (dog_speed / 2); 
                    else if (patrol_cycle < 180) step_x = -(dog_speed / 2); 
                    else                         step_y = -(dog_speed / 2); 

                    if (patrol_cycle % 60 < 10) is_moving = 0;

                    if (is_moving) {
                        int walk_frames[4] = {99, 107, 115, 123};
                        sprites[i].texture_id = walk_frames[(sprites[i].tick / 8) % 4];
                        if (is_passable_for_enemy(sprites[i].x + step_x, sprites[i].y, i)) sprites[i].x += step_x;
                        if (is_passable_for_enemy(sprites[i].x, sprites[i].y + step_y, i)) sprites[i].y += step_y;
                    } else {
                        sprites[i].texture_id = 99; 
                    }
                }
            }
            // State 1: Chase
            else if (sprites[i].state == 1) { 
                int walk_frames[4] = {99, 107, 115, 123};
                sprites[i].texture_id = walk_frames[(sprites[i].tick / 8) % 4];

                // Check Line of Sight!
                int has_los = check_line_of_sight(sprites[i].x, sprites[i].y, pos_x, pos_y);

                if (ABS(dx) > FLT2FX(0.8f) || ABS(dy) > FLT2FX(0.8f) || !has_los) { // Dog melee range
                    int32_t step_x = (dx > 0) ? dog_speed : -dog_speed;
                    int32_t step_y = (dy > 0) ? dog_speed : -dog_speed;

                    if (is_passable_for_enemy(sprites[i].x + step_x, sprites[i].y, i)) sprites[i].x += step_x;
                    if (is_passable_for_enemy(sprites[i].x, sprites[i].y + step_y, i)) sprites[i].y += step_y;
                } else {
                    sprites[i].state = 3; // Bite
                    sprites[i].tick = 0;
                }
            } 
            // State 3: Bite
            else if (sprites[i].state == 3) { 
                if (sprites[i].tick < 6)       sprites[i].texture_id = 135; 
                else if (sprites[i].tick < 12) sprites[i].texture_id = 136; 
                else                           sprites[i].texture_id = 137; 

                if (sprites[i].tick == 12) {
                    player_health -= 5; 
                    //flash_timer = 5;
                    //flash_color_16 = 0xF800;    // RED
                    trigger_flash(0xF800, 5);
                }
                
                if (sprites[i].tick > 18) {
                    sprites[i].state = 4; // Switch to Retreat
                    sprites[i].tick = 0;
                }
            }
            // State 4: Retreat (Hit and Run)
            else if (sprites[i].state == 4) {
                int walk_frames[4] = {99, 107, 115, 123}; // GUARANTEED DOG FRAMES
                sprites[i].texture_id = walk_frames[(sprites[i].tick / 8) % 4];

                int32_t step_x = 0, step_y = 0;
                int dir = (sprites[i].tick / 15 + i) % 4; 

                if (dir == 0) step_x = dog_speed;
                else if (dir == 1) step_x = -dog_speed;
                else if (dir == 2) step_y = dog_speed;
                else step_y = -dog_speed;

                if (is_passable_for_enemy(sprites[i].x + step_x, sprites[i].y, i)) sprites[i].x += step_x;
                if (is_passable_for_enemy(sprites[i].x, sprites[i].y + step_y, i)) sprites[i].y += step_y;

                if (sprites[i].tick > 90) { // 1.5 second of dodging
                    sprites[i].state = 1; 
                    sprites[i].tick = 0;
                }
            }
        } 
        // ==========================================
        // --- DOG DEATH LOGIC ---
        // ==========================================
        else if (sprites[i].type == 3 && sprites[i].state == 2) { 
            sprites[i].tick++;
            if (sprites[i].tick < 8)       sprites[i].texture_id = 131; 
            else if (sprites[i].tick < 16) sprites[i].texture_id = 132;
            else if (sprites[i].tick < 24) sprites[i].texture_id = 133;
            else                           sprites[i].texture_id = 134; // Dead
            }       
        }
    }

void process_player_input(InputState input) {
    // Tuning parameters mapped from fixed-point boundaries
    int32_t move_speed = (input.move / 4); // Max 0x6000 -> 0x1800 (~0.09)
    //float rot_speed = (float)input.turn / 200000.0f; // Max 0x4000 -> ~0.08 rad/frame

    //if (input.move != 0) bob_time += 0.2f;
    //else bob_time = 0.0f;

    // Advance the bobbing animation cycle if moving
    if (input.move != 0) {
        bob_counter = (bob_counter + 1) & 31; // Wraps cleanly from 31 back to 0
    } else {
        bob_counter = 0; // Reset to center when standing still
    }

    // Movement
    if (input.move != 0) {
        if (is_passable(pos_x + FX_MUL(dir_x, move_speed), pos_y)) pos_x += FX_MUL(dir_x, move_speed);
        if (is_passable(pos_x, pos_y + FX_MUL(dir_y, move_speed))) pos_y += FX_MUL(dir_y, move_speed);
    }

    /*
    // Rotation
    if (input.turn != 0) {
        float fdx = (float)dir_x / FX_ONE, fdy = (float)dir_y / FX_ONE;
        float fpx = (float)plane_x / FX_ONE, fpy = (float)plane_y / FX_ONE;
        dir_x   = FLT2FX(fdx * cosf(-rot_speed) - fdy * sinf(-rot_speed));
        dir_y   = FLT2FX(fdx * sinf(-rot_speed) + fdy * cosf(-rot_speed));
        plane_x = FLT2FX(fpx * cosf(-rot_speed) - fpy * sinf(-rot_speed));
        plane_y = FLT2FX(fpx * sinf(-rot_speed) + fpy * cosf(-rot_speed));
    }
    */

    // Rotation via LUT
    if (input.turn != 0) {
        // Map joystick turn value to angle steps (adjust the divisor for turn speed)
        int angle_step = input.turn / 8000; 
        
        // Wolf3D turns opposite to standard cartesian, invert the step
        player_angle = (player_angle - angle_step) & 511; 

        // Get sin/cos (cos is just sin shifted by 128 degrees/quarter circle)
        int32_t sin_val = fx_sin_lut[player_angle];
        int32_t cos_val = fx_sin_lut[(player_angle + 128) & 511];

        // Recalculate pure direction vectors based on global angle
        // Standard projection plane ratio in Wolf3D is 0.66
        dir_x   = cos_val;
        dir_y   = sin_val;
        plane_x = -FX_MUL(INT2FX(0) + 43253, sin_val); // 43253 is 0.66 in fixed point
        plane_y =  FX_MUL(INT2FX(0) + 43253, cos_val);
    }

    if (weapon_frame > 0) weapon_frame--;
    else if (weapon_frame < 0) weapon_frame++;

    if (input.fire) {
        if (weapon_frame == 0) {
            if (player_ammo > 0) {
                player_ammo--; weapon_frame = 30;
                int32_t det = FX_MUL(plane_x, dir_y) - FX_MUL(dir_x, plane_y);
                int32_t local_inv_det = (det == 0) ? 0 : FX_DIV(INT2FX(1), det);
                int closest_target = -1; int32_t closest_dist = 0x7FFFFFFF;

                for (int i = 0; i < NUM_WORLD_SPRITES; i++) {
                    if ((sprites[i].type == 2 || sprites[i].type == 3) && sprites[i].state != 2 && sprites[i].active) {
                        int32_t dx = sprites[i].x - pos_x;
                        int32_t dy = sprites[i].y - pos_y;
                        int32_t transform_y = FX_MUL(local_inv_det, -FX_MUL(plane_y, dx) + FX_MUL(plane_x, dy));
                        if (transform_y > 0) {
                            int32_t transform_x = FX_MUL(local_inv_det, FX_MUL(dir_y, dx) - FX_MUL(dir_x, dy));
                            int32_t screen_ratio = FX_DIV(transform_x, transform_y);
                            int ssx = (RENDER_WIDTH / 2) + FX2INT(FX_MUL(INT2FX(RENDER_WIDTH / 2), screen_ratio));
                            if (ssx > RENDER_WIDTH / 2 - 30 && ssx < RENDER_WIDTH / 2 + 30) {
                                if (ssx >= 0 && ssx < RENDER_WIDTH && transform_y < z_buffer[ssx]) {
                                    if (transform_y < closest_dist) { closest_dist = transform_y; closest_target = i; }
                                }
                            }
                        }
                    }
                }
                if (closest_target != -1) {
                    sprites[closest_target].health -= 25;
                    if (sprites[closest_target].health <= 0) { sprites[closest_target].state = 2; sprites[closest_target].tick = 0; }
                }
            } else weapon_frame = -15; 
        }
    }
    if (input.door) try_open_door();
}

void reset_game(void) {
    pos_x = FLT2FX(53.5f); pos_y = FLT2FX(44.5f);
    dir_x = FLT2FX(-1.0f); dir_y = FLT2FX(0.0f);
    plane_x = FLT2FX(0.0f); plane_y = FLT2FX(-0.66f);
    player_health = 100; player_ammo = 8; weapon_frame = -30;
    player_angle = 256;
    
    // Clear damage flash state and reset palette
    flash_timer = 0;
    flash_color_16 = 0;
    memcpy(active_palette, base_palette, sizeof(base_palette));

    memset(door_state, 0, sizeof(door_state));
    memset(door_timer, 0, sizeof(door_timer));
    memset(door_offset, 0, sizeof(door_offset));
    memcpy(sprites, initial_sprites, sizeof(initial_sprites));
    memset(hud_buffer, 0, sizeof(hud_buffer));
}

void system_clock_96mhz(void) {
    /* 1. Enable Flash Caches and set Wait States (Latency = 3 for 96MHz)
     * PRFTEN (Prefetch), ICEN (I-Cache), DCEN (D-Cache) */
    FLASH_ACR = (1 << 8) | (1 << 9) | (1 << 10) | 3;

    /* 2. Configure the Main PLL
     * HSI is 16 MHz. We want 96 MHz.
     * M = 8  (VCO In  = 16/8 = 2 MHz)
     * N = 96 (VCO Out = 2 * 96 = 192 MHz)
     * P = 2  (SYSCLK  = 192/2 = 96 MHz)
     * SRC = 0 (HSI) */
    RCC_PLLCFGR = (8 << 0) | (96 << 6) | (0 << 16) | (0 << 22);

    /* 3. Turn on the PLL */
    RCC_CR |= (1 << 24); // PLLON
    while (!(RCC_CR & (1 << 25))); // Wait for PLL to lock and stabilize

    /* 4. Configure APB/AHB Prescalers 
     * APB1 (Low Speed) = /2 (48 MHz max)
     * APB2 (High Speed) = /1 (96 MHz max) */
    RCC_CFGR = (4 << 10); // PPRE1 = 100 (/2)

    /* 5. Switch System Clock to the PLL */
    RCC_CFGR |= 2; // SW = 10 (Select PLL)
    while ((RCC_CFGR & (3 << 2)) != (2 << 2)); // Wait for switch to complete
}


// --- MAIN LOOP ---
int main(void) {
    SCB_VTOR = 0x08000000;
    
    // Enable the FPU Coprocessors (CP10 and CP11)
    SCB_CPACR |= ((3UL << 10*2) | (3UL << 11*2));

    //BOOT TO 96MHz
    system_clock_96mhz();

    STK_VAL  = 0;
    STK_LOAD = 96000 - 1;
    STK_CTRL = 7;
    __asm__ volatile ("cpsie i" : : : "memory");

    //input_init comes first otherwise the renderer will miss inputs
    input_init();
    display_init();
    init_vga_palette();
    reset_game();

    while (1) {
        InputState input = input_read();

        // Boot Splash Screen
        if (game_state == 3) {
            memset(frame_buffer_8bit, 0, RENDER_WIDTH * RENDER_HEIGHT); 
            draw_mini_string(52, 30, "WOLFENSTEIN 3D", 15);
            draw_mini_string(26, 45, "STM32F411CEU6 PORT BY PSxO5", 15);
            if ((ms_ticks / 500) % 2) draw_mini_string(42, 75, "PRESS FIRE TO START", 14); 
            draw_mini_string(21, 105, "ALL RIGHTS RESERVED ID SOFTWARE", 7); 
            
            display_push_frame(frame_buffer_8bit, active_palette, (uint16_t *)hud_buffer);
            if (input.fire) { reset_game(); game_state = 0; }
            delay_ms(16); continue;
        }
       
        // Level Complete
        if (game_state == 2) {
            if (death_fade < 255) {
                // Dissolve effect (Using color 2 for a different visual than death)
                for (int i = 0; i < RENDER_WIDTH * RENDER_HEIGHT; i++) {
                    if (rand() % 100 < 15) frame_buffer_8bit[i] = 2; 
                }
                death_fade += 10; 
            } else {
                // Solid screen and text
                memset(frame_buffer_8bit, 2, RENDER_WIDTH * RENDER_HEIGHT); 
                draw_mini_string(52, 50, "LEVEL COMPLETE", 15);
                draw_mini_string(38, 70, "PRESS OPEN TO RESTART", 15);
            }
            
            // Push to the STM32 SPI/DMA display handler
            display_push_frame(frame_buffer_8bit, active_palette, (uint16_t *)hud_buffer);
            
            // Wait for door open key to reset
            if (input.door && death_fade >= 255) { 
                reset_game();   
                game_state = 3; // Go back to splash screen
            }
            
            delay_ms(16); // Maintain ~60fps pacing for the fade
            continue; // Skip the rest of the engine rendering!
        }

        // Death Screen
        if (game_state == 1) {
            if (death_fade < 255) {
                for (int i = 0; i < RENDER_WIDTH * RENDER_HEIGHT; i++) if (rand() % 100 < 15) frame_buffer_8bit[i] = 40; 
                death_fade += 10; 
            } else {
                memset(frame_buffer_8bit, 40, RENDER_WIDTH * RENDER_HEIGHT); 
                draw_mini_string(70, 50, "DEATH", 15); draw_mini_string(38, 70, "PRESS OPEN TO RESTART", 15);
            }
            display_push_frame(frame_buffer_8bit, active_palette, (uint16_t *)hud_buffer);
            if (input.door && death_fade >= 255) { reset_game(); game_state = 3; }
            delay_ms(16); continue;
        }

        // Gameplay
        if (player_health <= 0) { death_fade = 0; game_state = 1; continue; }

        process_player_input(input);
        update_doors();
        update_world();
        
        render_frame();
        draw_sprites();
        draw_weapon();
        draw_hud();

        // Palette pointer swap logic for translucent flash
        uint16_t* render_palette = base_palette;
        if (flash_timer > 0) {
            render_palette = active_palette;
            flash_timer--;
        }

        display_push_frame(frame_buffer_8bit, render_palette, (uint16_t *)hud_buffer);
    }
    return 0;
}
