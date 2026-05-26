#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>

#include "include/display.h"
#include "include/input.h"

// --- ASSETS ---
#include "include/stm32_walls_8bit.h"
#include "include/map_e1m1.h"
#include "include/wolf_palette.h"
#include "include/stm32_sprites_8bit.h"

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
float   bob_time = 0.0f;
int     weapon_frame = 0; 
int     player_ammo = 8;
int     player_health = 100;
int     game_state = 3; // 3 = Boot Splash, 0 = Play, 1 = Dead, 2 = Win
int     death_fade = 0;
int     flash_timer = 0;
uint16_t flash_color_16 = 0;

// --- DOOR STATE MACHINE ---
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
        if (door_state[idx] == 0) door_state[idx] = 1;
    }
    
    if (tile == 21) { 
        game_state = 2;
        death_fade = 0;
    }
}

void update_doors(void) {
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            int idx = y * 64 + x;
            if (door_state[idx] == 1) { 
                door_offset[idx] += 2;
                if (door_offset[idx] >= 64) {
                    door_offset[idx] = 64;
                    door_state[idx] = 2; 
                    door_timer[idx] = 150; 
                }
            } else if (door_state[idx] == 2) { 
                if (door_timer[idx] > 0) door_timer[idx]--;
                if (door_timer[idx] <= 0) {
                    //int block = (FX2INT(pos_x) == x && FX2INT(pos_y) == y);
                    
                    // Replace with bounding box overlap check(robust than older check):
                    int32_t r = FLT2FX(0.4f); // Radius wider than player collision
                    int block = (pos_x + r > INT2FX(x) && pos_x - r < INT2FX(x + 1) &&
                                pos_y + r > INT2FX(y) && pos_y - r < INT2FX(y + 1));

                    for (int i = 0; i < NUM_WORLD_SPRITES && !block; i++) {
                        if (sprites[i].active && sprites[i].state != 2) { 
                            if (FX2INT(sprites[i].x) == x && FX2INT(sprites[i].y) == y) block = 1;
                        }
                    }
                    if (block) door_timer[idx] = 30; 
                    else door_state[idx] = 3; 
                }
            } else if (door_state[idx] == 3) { 
                door_offset[idx] -= 2;
                if (door_offset[idx] <= 0) {
                    door_offset[idx] = 0;
                    door_state[idx] = 0; 
                }
            }
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

        float f_ray_x = (float)ray_dir_x / 65536.0f;
        float f_ray_y = (float)ray_dir_y / 65536.0f;
        
        int32_t delta_dist_x = (ray_dir_x == 0) ? INT2FX(1000) : FLT2FX(fabsf(1.0f / f_ray_x));
        int32_t delta_dist_y = (ray_dir_y == 0) ? INT2FX(1000) : FLT2FX(fabsf(1.0f / f_ray_y));

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
    int sprite_order[NUM_WORLD_SPRITES];
    int32_t sprite_depth[NUM_WORLD_SPRITES]; 

    int32_t det = FX_MUL(plane_x, dir_y) - FX_MUL(dir_x, plane_y);
    int32_t inv_det = (det == 0) ? 0 : FX_DIV(INT2FX(1), det);

    for (int i = 0; i < NUM_WORLD_SPRITES; i++) {
        sprite_order[i] = i;
        if (sprites[i].active && inv_det != 0) {
            int32_t sx = sprites[i].x - pos_x;
            int32_t sy = sprites[i].y - pos_y;
            sprite_depth[i] = FX_MUL(inv_det, -FX_MUL(plane_y, sx) + FX_MUL(plane_x, sy));
        } else {
            sprite_depth[i] = -INT_MAX; 
        }
    }

    for (int i = 0; i < NUM_WORLD_SPRITES - 1; i++) {
        for (int j = 0; j < NUM_WORLD_SPRITES - i - 1; j++) {
            if (sprite_depth[sprite_order[j]] < sprite_depth[sprite_order[j + 1]]) {
                int temp = sprite_order[j];
                sprite_order[j] = sprite_order[j + 1];
                sprite_order[j + 1] = temp;
            }
        }
    }

    for (int k = 0; k < NUM_WORLD_SPRITES; k++) {
        int i = sprite_order[k]; 
        if (!sprites[i].active || sprite_depth[i] <= 0) continue;
        int32_t transform_y = sprite_depth[i];
        
        int32_t sx = sprites[i].x - pos_x;
        int32_t sy = sprites[i].y - pos_y;
        int32_t transform_x = FX_MUL(inv_det,  FX_MUL(dir_y, sx) - FX_MUL(dir_x, sy));

        if (transform_y < FLT2FX(0.1f)) continue;

        int32_t screen_ratio = FX_DIV(transform_x, transform_y);
        int sprite_screen_x  = (RENDER_WIDTH / 2) + FX2INT(FX_MUL(INT2FX(RENDER_WIDTH / 2), screen_ratio));

        int sprite_height = ABS(FX2INT(FX_DIV(INT2FX(RENDER_HEIGHT), transform_y)));
        int draw_start_y  = -sprite_height / 2 + RENDER_HEIGHT / 2;
        if (draw_start_y < 0) draw_start_y = 0;
        int draw_end_y    =  sprite_height / 2 + RENDER_HEIGHT / 2;
        if (draw_end_y >= RENDER_HEIGHT) draw_end_y = RENDER_HEIGHT - 1;

        int sprite_width = sprite_height;
        int draw_start_x = -sprite_width / 2 + sprite_screen_x;
        if (draw_start_x < 0) draw_start_x = 0;
        int draw_end_x   =  sprite_width / 2 + sprite_screen_x;
        if (draw_end_x >= RENDER_WIDTH) draw_end_x = RENDER_WIDTH - 1;

        for (int stripe = draw_start_x; stripe < draw_end_x; stripe++) {
            if (transform_y >= z_buffer[stripe]) continue; 
            int tex_x = ((stripe - (-sprite_width / 2 + sprite_screen_x)) * 64) / sprite_width;
            if (tex_x < 0) tex_x = 0;
            if (tex_x > 63) tex_x = 63;

            for (int y = draw_start_y; y < draw_end_y; y++) {
                int d = (y * 256) - (RENDER_HEIGHT * 128) + (sprite_height * 128);
                int tex_y = ((d * 64) / sprite_height) / 256;
                if (tex_y < 0) tex_y = 0;
                if (tex_y > 63) tex_y = 63;

                //uint8_t color = sprite_textures[sprites[i].texture_id][tex_y * 64 + tex_x];
                uint8_t color = sprite_textures[sprite_lut[sprites[i].texture_id]][tex_y * 64 + tex_x];
                if (color != TRANSPARENCY_COLOR) {
                    frame_buffer_8bit[y * RENDER_WIDTH + stripe] = color;
                }
            }
        }
    }
}

void draw_weapon(void) {
    int bob_y = (int)(fabsf(sinf(bob_time)) * 6.0f);
    int bob_x = (int)(cosf(bob_time) * 3.0f);

    int scale   = 1;
    int start_x = (RENDER_WIDTH / 2) - (32 * scale) + bob_x;
    int start_y = RENDER_HEIGHT - (64 * scale) + bob_y - 15;

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

void draw_hud(void) {
    int hud_height = 15;
    int hud_start_y = RENDER_HEIGHT - hud_height;
    
    for (int y = hud_start_y; y < RENDER_HEIGHT; y++) {
        memset(&frame_buffer_8bit[y * RENDER_WIDTH], 3, RENDER_WIDTH);
    }
    for (int x = 0; x < RENDER_WIDTH; x++) {
        frame_buffer_8bit[hud_start_y * RENDER_WIDTH + x] = 0; 
        frame_buffer_8bit[(hud_start_y + 1) * RENDER_WIDTH + x] = 2;
    }
    draw_mini_string(10, hud_start_y + 2, "LVL", 15); draw_number(15, hud_start_y + 10, 1, 15);
    draw_mini_string(45, hud_start_y + 2, "SCORE", 15); draw_number(50, hud_start_y + 10, 0, 15); 
    draw_mini_string(90, hud_start_y + 2, "HEALTH", 15); draw_number(95, hud_start_y + 10, player_health, 15);
    draw_mini_string(130, hud_start_y + 2, "AMMO", 15); draw_number(135, hud_start_y + 10, player_ammo, 15);
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
                        flash_timer = 10; flash_color_16 = 0x001F; // Blue
                    }
                } else if (sprites[i].texture_id == 28) { //Ammo
                    player_ammo += 8;
                    sprites[i].active = 0;         
                    flash_timer = 10; flash_color_16 = 0xFFE0; // Yellow
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
                if (sprites[i].tick == 15) { player_health -= 10; flash_timer = 10; flash_color_16 = 0xF800; }
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
                    flash_timer = 5;
                    flash_color_16 = 0xF800;    // RED
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
    float rot_speed = (float)input.turn / 200000.0f; // Max 0x4000 -> ~0.08 rad/frame

    if (input.move != 0) bob_time += 0.2f;
    else bob_time = 0.0f;

    // Movement
    if (input.move != 0) {
        if (is_passable(pos_x + FX_MUL(dir_x, move_speed), pos_y)) pos_x += FX_MUL(dir_x, move_speed);
        if (is_passable(pos_x, pos_y + FX_MUL(dir_y, move_speed))) pos_y += FX_MUL(dir_y, move_speed);
    }

    // Rotation
    if (input.turn != 0) {
        float fdx = (float)dir_x / FX_ONE, fdy = (float)dir_y / FX_ONE;
        float fpx = (float)plane_x / FX_ONE, fpy = (float)plane_y / FX_ONE;
        dir_x   = FLT2FX(fdx * cosf(-rot_speed) - fdy * sinf(-rot_speed));
        dir_y   = FLT2FX(fdx * sinf(-rot_speed) + fdy * cosf(-rot_speed));
        plane_x = FLT2FX(fpx * cosf(-rot_speed) - fpy * sinf(-rot_speed));
        plane_y = FLT2FX(fpx * sinf(-rot_speed) + fpy * cosf(-rot_speed));
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
    
    // Clear damage flash state and reset palette
    flash_timer = 0;
    flash_color_16 = 0;
    memcpy(active_palette, base_palette, sizeof(base_palette));

    memset(door_state, 0, sizeof(door_state));
    memset(door_timer, 0, sizeof(door_timer));
    memset(door_offset, 0, sizeof(door_offset));
    memcpy(sprites, initial_sprites, sizeof(initial_sprites));
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
            
            display_push_frame(frame_buffer_8bit, active_palette);
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
            display_push_frame(frame_buffer_8bit, active_palette);
            
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
            display_push_frame(frame_buffer_8bit, active_palette);
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

        apply_translucent_flash();
        display_push_frame(frame_buffer_8bit, active_palette);
    }
    return 0;
}
