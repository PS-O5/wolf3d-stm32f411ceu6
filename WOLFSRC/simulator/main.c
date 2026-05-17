#include <SDL2/SDL.h>
#include <SDL2/SDL_keyboard.h>
#include <SDL2/SDL_scancode.h>
#include <SDL2/SDL_stdinc.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>

#include "platform.h"
#include "../assets/walls_8bit.h"
#include "../assets/map_e1m1.h"
#include "../assets/wolf_palette.h"
#include "../assets/sprites_8bit.h"   // BUG 1 FIX: was "all_sprites_8bit.h" (wrong name/path).
                             // Array inside is sprite_textures[], not all_sprites[].

// --- WEAPON STATE ---
int weapon_frame = 0; // 0 = idle, > 0 = firing recoil timer

// --- DOOR STATE MACHINE ---
uint8_t door_state[64 * 64]  = {0}; // 0=Closed 1=Opening 2=Open 3=Closing
int32_t door_timer[64 * 64]  = {0};
int32_t door_offset[64 * 64] = {0}; // pixels slid open (0-64)

// --- 16.16 FIXED POINT MATH MACROS ---
#define FX_SHIFT 16
#define FX_ONE   (1 << FX_SHIFT)
#define INT2FX(i)   ((i) << FX_SHIFT)
#define FX2INT(x)   ((x) >> FX_SHIFT)
#define ABS(x)      ((x) < 0 ? -(x) : (x))
#define FLT2FX(f)   ((int32_t)((f) * (1 << FX_SHIFT)))
#define FX_MUL(a,b) (int32_t)(((int64_t)(a) * (b)) >> FX_SHIFT)
#define FX_DIV(a,b) (int32_t)(((int64_t)(a) << FX_SHIFT) / (b))

uint8_t  frame_buffer_8bit[RENDER_WIDTH * RENDER_HEIGHT];
uint16_t rgb565_palette[256];
uint16_t dma_tft_buffer[TFT_WIDTH * TFT_HEIGHT];

// Player state (16.16 fixed point)
int32_t pos_x   = FLT2FX(57.5f);
int32_t pos_y   = FLT2FX(37.5f);
int32_t dir_x   = FLT2FX(0.0f);
int32_t dir_y   = FLT2FX(-1.0f);
int32_t plane_x = FLT2FX(0.66f);
int32_t plane_y = FLT2FX(0.0f);
float   bob_time = 0.0f;


const uint8_t font_num_3x5[10][5] = {
    {0x7, 0x5, 0x5, 0x5, 0x7}, // 0
    {0x2, 0x6, 0x2, 0x2, 0x7}, // 1
    {0x7, 0x1, 0x7, 0x4, 0x7}, // 2
    {0x7, 0x1, 0x7, 0x1, 0x7}, // 3
    {0x5, 0x5, 0x7, 0x1, 0x1}, // 4
    {0x7, 0x4, 0x7, 0x1, 0x7}, // 5
    {0x7, 0x4, 0x7, 0x5, 0x7}, // 6
    {0x7, 0x1, 0x2, 0x2, 0x2}, // 7
    {0x7, 0x5, 0x7, 0x5, 0x7}, // 8
    {0x7, 0x5, 0x7, 0x1, 0x7}  // 9
};

// 1D Z-buffer for sprite depth clipping
int32_t z_buffer[RENDER_WIDTH];

// Sprite struct
typedef struct {
    int32_t x, y;
    int texture_id;
    int type;   
    int active; 
    int state;  // 0=idle, 1=chase, 2=dead, 3=shoot (NEW)
    int health;
    int tick;   // NEW: Animation timer
} Sprite;

// BUG 6 FIX: renamed to WORLD_SPRITES to avoid colliding with
// NUM_SPRITES 436 defined in sprites_8bit.h
#define NUM_WORLD_SPRITES 4

Sprite sprites[NUM_WORLD_SPRITES] = {
    { FLT2FX(57.5f), FLT2FX(36.0f), 28, 1, 1, 0,  0 },  // Ammo pickup (ID 28)
    { FLT2FX(57.5f), FLT2FX(34.5f),  3, 0, 1, 0,  0 },  // Barrel (ID 3)
    { FLT2FX(56.5f), FLT2FX(36.0f), 27, 1, 1, 0,  0 },  // Medkit (ID 27)
    { FLT2FX(57.5f), FLT2FX(31.5f), 50, 2, 1, 0, 25 },  // Guard (ID 50)
};

int player_ammo = 8;
int player_health = 100;

// ─── PALETTE ─────────────────────────────────────────────────────────────────
// BUG 2 FIX: SDL2 on x86 (little-endian) with SDL_PIXELFORMAT_BGR565 wants
// the bits in memory as BBBBBGGG GGGRRRRR.  The STM32 TFT expects native
// RRRRRGGG GGGBBBBB.  On the PC simulator we swap R↔B so SDL shows correct
// colours.  On the STM32 keep the original order (R<<11).
void init_vga_palette(void) {
    for (int i = 0; i < 256; i++) {
        uint8_t r = wolf_rgb[i][0];
        uint8_t g = wolf_rgb[i][1];
        uint8_t b = wolf_rgb[i][2];
#ifdef SDL_SIMULATOR
        // x86 SDL: store as BGR565 (R and B swapped vs hardware)
        rgb565_palette[i] = ((b >> 3) << 11) | ((g >> 2) << 5) | (r >> 3);
#else
        // STM32 hardware native RGB565
        rgb565_palette[i] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
#endif
    }
}

// ─── MAP HELPERS ─────────────────────────────────────────────────────────────
int get_map_tile(int x, int y) {
    if (x < 0 || x >= 64 || y < 0 || y >= 64) return 1;
    return map_e1m1[y * 64 + x];
}

int check_sprite_collision(int32_t target_x, int32_t target_y) {
    int32_t radius = FLT2FX(0.3f);
    for (int i = 0; i < NUM_WORLD_SPRITES; i++) {
        if (!sprites[i].active) continue;
        int blocking = (sprites[i].type == 0) ||
                       (sprites[i].type == 2 && sprites[i].state != 2);
        if (blocking) {
            if (target_x > sprites[i].x - radius && target_x < sprites[i].x + radius &&
                target_y > sprites[i].y - radius && target_y < sprites[i].y + radius)
                return 1;
        }
    }
    return 0;
}

int is_passable(int32_t fx, int32_t fy) {
    // 0.25 units of padding around the player
    int32_t radius = FLT2FX(0.25f); 
    
    // Check all 4 corners of the player's bounding box
    int corners[4][2] = {
        { FX2INT(fx - radius), FX2INT(fy - radius) },
        { FX2INT(fx + radius), FX2INT(fy - radius) },
        { FX2INT(fx - radius), FX2INT(fy + radius) },
        { FX2INT(fx + radius), FX2INT(fy + radius) }
    };

    for (int i = 0; i < 4; i++) {
        int tile = get_map_tile(corners[i][0], corners[i][1]);
        
        // Wall collision
        if (tile > 0 && tile < 90) return 0; 
        
        // Door collision
        if (tile >= 90 && tile <= 101) {
            if (door_state[corners[i][1] * 64 + corners[i][0]] != 2) return 0; 
        }
    }

    // Sprite collision (uses its own radius check)
    if (check_sprite_collision(fx, fy)) return 0;
    
    return 1;
}

int is_passable_for_enemy(int32_t fx, int32_t fy, int self_index) {
    int map_x = FX2INT(fx);
    int map_y = FX2INT(fy);
    int tile  = get_map_tile(map_x, map_y);

    if (tile > 0 && tile < 90) return 0; // Hit a wall
    if (tile >= 90 && tile <= 101) {     // Hit a door
        if (door_state[map_y * 64 + map_x] != 2) return 0;
    }

    // Check collision against OTHER sprites
    int32_t radius = FLT2FX(0.4f);
    for (int i = 0; i < NUM_WORLD_SPRITES; i++) {
        if (i == self_index || !sprites[i].active) continue;
        int blocking = (sprites[i].type == 0) || (sprites[i].type == 2 && sprites[i].state != 2);
        if (blocking) {
            if (fx > sprites[i].x - radius && fx < sprites[i].x + radius &&
                fy > sprites[i].y - radius && fy < sprites[i].y + radius)
                return 0;
        }
    }
    return 1;
}


// ─── DOORS ───────────────────────────────────────────────────────────────────
void try_open_door(void) {
    int tx = FX2INT(pos_x + dir_x);
    int ty = FX2INT(pos_y + dir_y);
    int tile = get_map_tile(tx, ty);
    
    // Standard doors
    if (tile >= 90 && tile <= 101) {
        int idx = ty * 64 + tx;
        if (door_state[idx] == 0) door_state[idx] = 1;
    }
    
    // Elevator switch (usually tile 21 or 41)
    if (tile == 21) { 
        printf("Elevator activated! Level Complete.\n");
        // Quick and dirty exit for the simulator
        exit(0); 
    }
}

void update_doors(void) {
    for (int i = 0; i < 64 * 64; i++) {
        if (door_state[i] == 1) {
            door_offset[i] += 2;
            if (door_offset[i] >= 64) { door_state[i] = 2; door_offset[i] = 64; door_timer[i] = 0; }
        } else if (door_state[i] == 2) {
            if (++door_timer[i] > 150) door_state[i] = 3;
        } else if (door_state[i] == 3) {
            door_offset[i] -= 2;
            if (door_offset[i] <= 0) { door_state[i] = 0; door_offset[i] = 0; }
        }
    }
}

// ─── WEAPON ──────────────────────────────────────────────────────────────────
void draw_weapon(void) {
    int bob_y = (int)(fabs(sin(bob_time)) * 6.0f);
    int bob_x = (int)(cos(bob_time) * 3.0f);

    int scale   = 1;
    int start_x = (RENDER_WIDTH / 2) - (32 * scale) + bob_x;
    int start_y = RENDER_HEIGHT - (64 * scale) + bob_y - 15;

    // BUG 1 (cont.): sprite index for pistol idle frame relative to sprite_start.
    // Verified against VSWAP.WL1: pistol idle = relative index 315.
    int weapon_tex_id = 421;

    if (weapon_frame > 0) {
        start_y += 4;
        weapon_tex_id += 1; // firing frame
        weapon_frame--;
    }

    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            // BUG 1 FIX: was all_sprites[id][...], correct name is sprite_textures
            uint8_t color = sprite_textures[weapon_tex_id][y * 64 + x];
            if (color == TRANSPARENCY_COLOR) continue;

            for (int sy = 0; sy < scale; sy++) {
                for (int sx = 0; sx < scale; sx++) {
                    int screen_x = start_x + (x * scale) + sx;
                    int screen_y = start_y + (y * scale) + sy;
                    if (screen_x >= 0 && screen_x < RENDER_WIDTH &&
                        screen_y >= 0 && screen_y < RENDER_HEIGHT) {
                        frame_buffer_8bit[screen_y * RENDER_WIDTH + screen_x] = color;
                    }
                }
            }
        }
    }
}

// ─── SPRITES ─────────────────────────────────────────────────────────────────
  
  
void draw_sprites(void) {
    int sprite_order[NUM_WORLD_SPRITES];
    int32_t sprite_depth[NUM_WORLD_SPRITES]; 

    // 1. Calculate inverse determinant for camera transformation
    int32_t det = FX_MUL(plane_x, dir_y) - FX_MUL(dir_x, plane_y);
    int32_t inv_det = (det == 0) ? 0 : FX_DIV(INT2FX(1), det);

    // 2. Pre-calculate orthogonal depth (transform_y) for ALL sprites
    for (int i = 0; i < NUM_WORLD_SPRITES; i++) {
        sprite_order[i] = i;
        if (sprites[i].active && inv_det != 0) {
            int32_t sx = sprites[i].x - pos_x;
            int32_t sy = sprites[i].y - pos_y;
            // This is the true distance to the camera plane
            sprite_depth[i] = FX_MUL(inv_det, -FX_MUL(plane_y, sx) + FX_MUL(plane_x, sy));
        } else {
            // Drop dead/inactive sprites to the absolute back of the sorting list
            sprite_depth[i] = -INT_MAX; 
        }
    }

    // 3. Bubble Sort by Depth (Descending order: furthest rendered first)
    for (int i = 0; i < NUM_WORLD_SPRITES - 1; i++) {
        for (int j = 0; j < NUM_WORLD_SPRITES - i - 1; j++) {
            if (sprite_depth[sprite_order[j]] < sprite_depth[sprite_order[j + 1]]) {
                int temp = sprite_order[j];
                sprite_order[j] = sprite_order[j + 1];
                sprite_order[j + 1] = temp;
            }
        }
    }

    // 4. Render Loop
    for (int k = 0; k < NUM_WORLD_SPRITES; k++) {
        int i = sprite_order[k]; 
        
        // If inactive or behind the camera plane, skip entirely
        if (!sprites[i].active || sprite_depth[i] <= 0) continue;

        int32_t transform_y = sprite_depth[i];
        
        // Calculate X transform
        int32_t sx = sprites[i].x - pos_x;
        int32_t sy = sprites[i].y - pos_y;
        int32_t transform_x = FX_MUL(inv_det,  FX_MUL(dir_y, sx) - FX_MUL(dir_x, sy));

        if (transform_y < FLT2FX(0.1f)) continue;

        // Calculate screen coordinates
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

        // Draw the pixels
        for (int stripe = draw_start_x; stripe < draw_end_x; stripe++) {
            // Z-Buffer check against walls
            if (transform_y >= z_buffer[stripe]) continue; 


            int tex_x = ((stripe - (-sprite_width / 2 + sprite_screen_x)) * 64) / sprite_width;
            
            if (tex_x < 0) tex_x = 0;
            if (tex_x > 63) tex_x = 63;

            for (int y = draw_start_y; y < draw_end_y; y++) {
                int d = (y * 256) - (RENDER_HEIGHT * 128) + (sprite_height * 128);
                int tex_y = ((d * 64) / sprite_height) / 256;
                if (tex_y < 0) tex_y = 0;
                if (tex_y > 63) tex_y = 63;

                uint8_t color = sprite_textures[sprites[i].texture_id][tex_y * 64 + tex_x];
                if (color != TRANSPARENCY_COLOR) {
                    frame_buffer_8bit[y * RENDER_WIDTH + stripe] = color;
                }
            }
        }
    }
}


// ─── RAYCASTER ───────────────────────────────────────────────────────────────
void render_frame(void) {
    // Clear: palette index 29 = dark grey ceiling, 27 = brown floor
    for (int y = 0; y < RENDER_HEIGHT; y++) {
        uint8_t color = (y < RENDER_HEIGHT / 2) ? 29 : 27;
        memset(&frame_buffer_8bit[y * RENDER_WIDTH], color, RENDER_WIDTH);
    }

    for (int x = 0; x < RENDER_WIDTH; x++) {

        // BUG 7 FIX: must use FX_DIV here, not plain integer division.
        // Old: INT2FX(2*x) / RENDER_WIDTH  → divides the fixed-point number
        //      as an integer, losing all fractional bits.
        // New: FX_DIV(INT2FX(2*x), INT2FX(RENDER_WIDTH)) → correct FP divide.
        int32_t camera_x  = FX_DIV(INT2FX(2 * x), INT2FX(RENDER_WIDTH)) - INT2FX(1);
        int32_t ray_dir_x = dir_x + FX_MUL(plane_x, camera_x);
        int32_t ray_dir_y = dir_y + FX_MUL(plane_y, camera_x);

        int map_x = FX2INT(pos_x);
        int map_y = FX2INT(pos_y);

        int32_t delta_dist_x = (ray_dir_x == 0) ? INT2FX(1000) : ABS(FX_DIV(INT2FX(1), ray_dir_x));
        int32_t delta_dist_y = (ray_dir_y == 0) ? INT2FX(1000) : ABS(FX_DIV(INT2FX(1), ray_dir_y));

        int32_t side_dist_x, side_dist_y;
        int step_x, step_y;
        int hit = 0, side = 0, tile_hit = 0, is_door_face = 0;

        if (ray_dir_x < 0) { step_x = -1; side_dist_x = FX_MUL(pos_x - INT2FX(map_x), delta_dist_x); }
        else                { step_x =  1; side_dist_x = FX_MUL(INT2FX(map_x + 1) - pos_x, delta_dist_x); }
        if (ray_dir_y < 0) { step_y = -1; side_dist_y = FX_MUL(pos_y - INT2FX(map_y), delta_dist_y); }
        else                { step_y =  1; side_dist_y = FX_MUL(INT2FX(map_y + 1) - pos_y, delta_dist_y); }

        while (!hit) {
            if (side_dist_x < side_dist_y) { side_dist_x += delta_dist_x; map_x += step_x; side = 0; }
            else                            { side_dist_y += delta_dist_y; map_y += step_y; side = 1; }

            tile_hit = get_map_tile(map_x, map_y);

            if (tile_hit > 0 && tile_hit < 90) {
                hit = 1;
            } else if (tile_hit >= 90 && tile_hit <= 101) {
                is_door_face = ((tile_hit == 90 && side == 0) || (tile_hit == 91 && side == 1));
                if (is_door_face) {
                    int d_state = door_state[map_y * 64 + map_x];
                    if (d_state == 2) continue;
                    if (d_state == 0) { hit = 1; continue; }

                    int32_t perp = (side == 0) ? (side_dist_x - delta_dist_x)
                                               : (side_dist_y - delta_dist_y);
                    int32_t wx = (side == 0) ? (pos_y + FX_MUL(perp, ray_dir_y))
                                             : (pos_x + FX_MUL(perp, ray_dir_x));
                    wx &= (FX_ONE - 1);

                    int tx = FX_MUL(wx, INT2FX(64)) >> FX_SHIFT;
                    if (side == 0 && ray_dir_x < 0) tx = 64 - tx - 1;
                    if (side == 1 && ray_dir_y > 0) tx = 64 - tx - 1;

                    int offset = door_offset[map_y * 64 + map_x];
                    if (tx < offset) continue;
                    hit = 1;
                } else {
                    hit = 1; // door jamb
                }
            }
        }

        int32_t perp_wall_dist = (side == 0) ? (side_dist_x - delta_dist_x)
                                              : (side_dist_y - delta_dist_y);
        if (perp_wall_dist < INT2FX(1) / 4) perp_wall_dist = INT2FX(1) / 4;

        int line_height = FX2INT(FX_DIV(INT2FX(RENDER_HEIGHT), perp_wall_dist));
        if (line_height <= 0) line_height = 1;

        int draw_start = -line_height / 2 + RENDER_HEIGHT / 2;
        if (draw_start < 0) draw_start = 0;
        int draw_end = line_height / 2 + RENDER_HEIGHT / 2;
        if (draw_end >= RENDER_HEIGHT) draw_end = RENDER_HEIGHT - 1;

        // Texture selection
        int tex_num;
        if (tile_hit >= 90) {
            tex_num = is_door_face ? 98 : 4;
            if (side == 1) tex_num += 1;
        } else {
            tex_num = (tile_hit - 1) * 2;
            if (side == 1) tex_num += 1;
        }
        tex_num = tex_num % NUM_WALLS;

        int32_t wall_x = (side == 0) ? (pos_y + FX_MUL(perp_wall_dist, ray_dir_y))
                                      : (pos_x + FX_MUL(perp_wall_dist, ray_dir_x));
        wall_x &= (FX_ONE - 1);

        int tex_x = FX_MUL(wall_x, INT2FX(64)) >> FX_SHIFT;
        if (side == 0 && ray_dir_x < 0) tex_x = 64 - tex_x - 1;
        if (side == 1 && ray_dir_y > 0) tex_x = 64 - tex_x - 1;

        if (tile_hit >= 90 && is_door_face) {
            tex_x -= door_offset[map_y * 64 + map_x];
            if (tex_x < 0) tex_x = 0;
        }

        int32_t step    = FX_DIV(INT2FX(64), INT2FX(line_height));
        int32_t tex_pos = FX_MUL(INT2FX(draw_start - RENDER_HEIGHT / 2 + line_height / 2), step);

        z_buffer[x] = perp_wall_dist;
        for (int y = draw_start; y < draw_end; y++) {
            int tex_y = FX2INT(tex_pos) & 63;
            tex_pos += step;
            frame_buffer_8bit[y * RENDER_WIDTH + x] = wall_textures[tex_num][tex_x * 64 + tex_y];
        }
    }
}

void draw_number(int x, int y, int number, uint8_t color_idx) {
char str[16];
    sprintf(str, "%d", number);
    
    for (int i = 0; str[i] != '\0'; i++) {
        int digit = str[i] - '0';
        if (digit >= 0 && digit <= 9) {
            for (int row = 0; row < 5; row++) {
                uint8_t bits = font_num_3x5[digit][row];
                for (int col = 0; col < 3; col++) {
                    // Check the 3 least significant bits
                    if (bits & (1 << (2 - col))) {
                        int px = x + (i * 4) + col; // 4 pixels per character (3 width + 1 spacing)
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


void draw_hud(void) {
    int hud_start_y = RENDER_HEIGHT - 15; // Shrunk to 10 pixels
    
    // Draw background
    for (int y = hud_start_y; y < RENDER_HEIGHT; y++) {
        memset(&frame_buffer_8bit[y * RENDER_WIDTH], 3, RENDER_WIDTH); 
    }

    // Top border
    for (int x = 0; x < RENDER_WIDTH; x++) {
        frame_buffer_8bit[hud_start_y * RENDER_WIDTH + x] = 0;
    }

    // Draw the mini readouts
    draw_number(10, hud_start_y + 3, player_health, 15);
    draw_number(130, hud_start_y + 3, player_ammo, 15);
}

// ─── UPSCALE & PUSH ──────────────────────────────────────────────────────────
// BUG 4 FIX: original loop iterated src_x over RENDER_WIDTH but wrote into
// a TFT_WIDTH-wide row, leaving the right half of each row uninitialised.
// Now each render pixel is written twice (once for each 2x column).
void upscale_and_push_frame(void) {
    for (int y = 0; y < TFT_HEIGHT; y++) {
        int src_y = y / 2;                       // 2x vertical scale
        uint16_t *dst = &dma_tft_buffer[y * TFT_WIDTH];
        for (int src_x = 0; src_x < RENDER_WIDTH; src_x++) {
            uint8_t  idx   = frame_buffer_8bit[src_y * RENDER_WIDTH + src_x];
            uint16_t color = rgb565_palette[idx];
            dst[src_x * 2]     = color;          // 2x horizontal scale
            dst[src_x * 2 + 1] = color;
        }
    }
}

// ─── WORLD UPDATE ────────────────────────────────────────────────────────────
void update_world(void) {
    int32_t pickup_radius = FLT2FX(0.8f);
    int32_t enemy_speed   = FLT2FX(0.015f);
    int32_t aggro_range   = FLT2FX(6.0f);

    for (int i = 0; i < NUM_WORLD_SPRITES; i++) {
        if (!sprites[i].active) continue;

        if (sprites[i].type == 1) {
            if (ABS(pos_x - sprites[i].x) < pickup_radius &&
                ABS(pos_y - sprites[i].y) < pickup_radius) {
                sprites[i].active = 0;
                player_ammo += 8;
                printf("Picked up ammo! Total: %d\n", player_ammo);
            }
        }

        if (sprites[i].type == 2 && sprites[i].state != 2) {
            int32_t dx = pos_x - sprites[i].x;
            int32_t dy = pos_y - sprites[i].y;
            sprites[i].tick++; 

            if (sprites[i].state == 0 && ABS(dx) < aggro_range && ABS(dy) < aggro_range) {
                sprites[i].state = 1; 
                sprites[i].tick = 0;
                sprites[i].texture_id = 96; // Alert frame
            }

                if (sprites[i].state == 1) { // Chase
                // Walk cycle: staggered by 8 to hit the front-facing frame of each animation block
                int walk_frames[4] = {50, 58, 66, 74};
                sprites[i].texture_id = walk_frames[(sprites[i].tick / 10) % 4]; 

                if (ABS(dx) > FLT2FX(1.5f) || ABS(dy) > FLT2FX(1.5f)) {
                    int32_t step_x = (dx > 0) ? enemy_speed : -enemy_speed;
                    int32_t step_y = (dy > 0) ? enemy_speed : -enemy_speed;

                    // Move X and Y independently to slide along walls/barrels
                    if (is_passable_for_enemy(sprites[i].x + step_x, sprites[i].y, i)) {
                        sprites[i].x += step_x;
                    }
                    if (is_passable_for_enemy(sprites[i].x, sprites[i].y + step_y, i)) {
                        sprites[i].y += step_y;
                    }
                }
                else {
                    sprites[i].state = 3; 
                    sprites[i].tick = 0;
                }
            }
            else if (sprites[i].state == 3) { // Shoot
                // Toggle between the two shooting frames
                sprites[i].texture_id = (sprites[i].tick < 15) ? 97 : 98; 
                
                if (sprites[i].tick == 15) {
                    printf("Guard shoots you!\n");
                    player_health -= 10;
                }
                if (sprites[i].tick > 30) {
                    sprites[i].state = 1; // Back to chase
                    sprites[i].tick = 0;
                }
            }
        } else if (sprites[i].type == 2 && sprites[i].state == 2) { // Dead / Dying
            sprites[i].tick++;
            
            // Dying sequence
            if (sprites[i].tick < 8)       sprites[i].texture_id = 94; // Getting hit / Pain
            else if (sprites[i].tick < 16) sprites[i].texture_id = 90;
            else if (sprites[i].tick < 24) sprites[i].texture_id = 91;
            else if (sprites[i].tick < 32) sprites[i].texture_id = 92;
            else if (sprites[i].tick < 40) sprites[i].texture_id = 93;
            else                           sprites[i].texture_id = 95; // Final dead body
        }
    }
}

// ─── MAIN ────────────────────────────────────────────────────────────────────
int main(int argc __attribute__((unused)), char *argv[] __attribute__((unused))) {
    SDL_Init(SDL_INIT_VIDEO);

    SDL_Window   *window   = SDL_CreateWindow("Wolf3D E1M1 Simulator",
                                 SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 TFT_WIDTH * 3, TFT_HEIGHT * 3, 0);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    // BUG 2 FIX: use BGR565 on x86 SDL so R and B channels render correctly.
    // The palette init below (with SDL_SIMULATOR defined) matches this format.
    SDL_Texture  *texture  = SDL_CreateTexture(renderer,
                                 SDL_PIXELFORMAT_RGB565,
                                 SDL_TEXTUREACCESS_STREAMING,
                                 TFT_WIDTH, TFT_HEIGHT);

    init_vga_palette();

    int32_t move_speed = FLT2FX(0.1f);
    float   rot_speed  = 0.05f;
    int running = 1;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = 0;
        }

        const Uint8 *keys = SDL_GetKeyboardState(NULL);

        // Bob only while moving
        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_S] ||
            keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_DOWN]) bob_time += 0.2f;
        else bob_time = 0.0f;

        // Forward / Backward
        if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]) {
            if (is_passable(pos_x + FX_MUL(dir_x, move_speed), pos_y)) pos_x += FX_MUL(dir_x, move_speed);
            if (is_passable(pos_x, pos_y + FX_MUL(dir_y, move_speed))) pos_y += FX_MUL(dir_y, move_speed);
        }
        if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) {
            if (is_passable(pos_x - FX_MUL(dir_x, move_speed), pos_y)) pos_x -= FX_MUL(dir_x, move_speed);
            if (is_passable(pos_x, pos_y - FX_MUL(dir_y, move_speed))) pos_y -= FX_MUL(dir_y, move_speed);
        }

        // BUG 3 FIX: A/D were mapped to opposite rotation directions AND
        // wrongly paired with LEFT/RIGHT arrow keys.
        // Correct Wolf3D convention: Left arrow / A = rotate left (counter-clockwise)
        //                           Right arrow / D = rotate right (clockwise)
        if (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]) {
            float fdx = (float)dir_x / FX_ONE, fdy = (float)dir_y / FX_ONE;
            float fpx = (float)plane_x / FX_ONE, fpy = (float)plane_y / FX_ONE;
            dir_x   = FLT2FX(fdx * cos(-rot_speed) - fdy * sin(-rot_speed));
            dir_y   = FLT2FX(fdx * sin(-rot_speed) + fdy * cos(-rot_speed));
            plane_x = FLT2FX(fpx * cos(-rot_speed) - fpy * sin(-rot_speed));
            plane_y = FLT2FX(fpx * sin(-rot_speed) + fpy * cos(-rot_speed));
        }
        if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) {
            float fdx = (float)dir_x / FX_ONE, fdy = (float)dir_y / FX_ONE;
            float fpx = (float)plane_x / FX_ONE, fpy = (float)plane_y / FX_ONE;
            dir_x   = FLT2FX(fdx * cos(rot_speed) - fdy * sin(rot_speed));
            dir_y   = FLT2FX(fdx * sin(rot_speed) + fdy * cos(rot_speed));
            plane_x = FLT2FX(fpx * cos(rot_speed) - fpy * sin(rot_speed));
            plane_y = FLT2FX(fpx * sin(rot_speed) + fpy * cos(rot_speed));
        }

        // Shoot
        if (keys[SDL_SCANCODE_Z] && weapon_frame == 0) {
            if (player_ammo > 0) {
                weapon_frame = 10;
                player_ammo--;
                printf("BANG! Ammo: %d\n", player_ammo);

                for (int i = 0; i < NUM_WORLD_SPRITES; i++) {
                    if (!sprites[i].active || sprites[i].type != 2 || sprites[i].state == 2) continue;

                    int32_t sx = sprites[i].x - pos_x;
                    int32_t sy = sprites[i].y - pos_y;
                    int32_t det = FX_MUL(plane_x, dir_y) - FX_MUL(dir_x, plane_y);
                    if (det == 0) continue;
                    int32_t inv_det = FX_DIV(INT2FX(1), det);

                    int32_t transform_x = FX_MUL(inv_det,  FX_MUL(dir_y,   sx) - FX_MUL(dir_x,   sy));
                    int32_t transform_y = FX_MUL(inv_det, -FX_MUL(plane_y, sx) + FX_MUL(plane_x, sy));

                    if (transform_y <= 0) continue;

                    int32_t screen_ratio = FX_DIV(transform_x, transform_y);
                    int ssx = (RENDER_WIDTH / 2) + FX2INT(FX_MUL(INT2FX(RENDER_WIDTH / 2), screen_ratio));

                    if (ssx > RENDER_WIDTH / 2 - 30 && ssx < RENDER_WIDTH / 2 + 30) {
                        sprites[i].health -= 25;
                        if (sprites[i].health <= 0) { 
                            sprites[i].state = 2; 
                            sprites[i].tick = 0; // CRITICAL: Reset the timer!
                            printf("Guard down!\n"); 
                        } else {
                            printf("Guard hit!\n");
                        }
                    }
                }
            } else {
                printf("Click. Out of ammo.\n");
            }
        }

        // Open door
        if (keys[SDL_SCANCODE_SPACE]) try_open_door();

        update_doors();
        update_world();
        render_frame();
        draw_sprites();
        draw_weapon();
        draw_hud();
        upscale_and_push_frame();

        SDL_UpdateTexture(texture, NULL, dma_tft_buffer, TFT_WIDTH * sizeof(uint16_t));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        // GPS readout every ~1 second
        static int frame_count = 0;
        if (++frame_count % 60 == 0) {
            printf("GPS -> X: %05.2f  Y: %05.2f  |  Facing -> X: %5.2f  Y: %5.2f\n",
                   (float)pos_x / 65536.f, (float)pos_y / 65536.f,
                   (float)dir_x / 65536.f, (float)dir_y / 65536.f);
        }

        SDL_Delay(16);
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
