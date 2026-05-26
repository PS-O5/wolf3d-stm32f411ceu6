"""
Wolf3D Sprite Extractor
=======================
Reads VSWAP.WL1 (or .WL6) and produces:
  1. sprites_8bit.h   -- flat 64x64 palette-index arrays, one per sprite,
                         matching the style/format of walls_8bit.h exactly.
                         Transparent pixels are stored as 0xFF (255) so the
                         renderer can skip them.  Include TRANSPARENCY_COLOR
                         from the header to check.
  2. sprites_png/     -- one PNG per sprite for visual verification (uses the
                         wolf_palette.h colours; transparent pixels are clear).

Usage:
    python sprite_extractor.py [VSWAP.WL1] [wolf_palette.h]

Defaults: VSWAP.WL1 and wolf_palette.h in the current directory.

Sprite chunk layout recap
--------------------------
Walls  :  chunks  0 .. sprite_start-1   raw 64x64 column-major bytes
Sprites:  chunks  sprite_start .. sound_start-1   compressed (see below)
Sounds :  chunks  sound_start ..

Each sprite chunk:
  Offset  Size        Field
  0       uint16      FirstColumn  (leftmost non-transparent column, 0-based)
  2       uint16      LastColumn   (rightmost non-transparent column, inclusive)
  4       uint16*W    ColumnOffsets  (W = LastColumn-FirstColumn+1 words,
                                     each is a byte offset into the chunk)
  ...     posts       each column is a list of vertical pixel-runs (posts)

Each post group for one column (at ColumnOffsets[col]):
  Repeating:
    uint16  end_pixel_offset   -- divide by 2 to get last row (exclusive);
                                  0x0000 means no more posts
    uint16  pixel_data_offset  -- byte offset into chunk where palette bytes live
    uint16  start_pixel_offset -- divide by 2 to get first row (inclusive)
  Then at pixel_data_offset: (end_row - start_row) raw palette bytes

The transparency sentinel value written to the flat array is 0xFF (255).
Index 255 in the Wolf palette is {153,0,137} -- never used for real sprite
pixels, so it is safe as a magic "transparent" marker.
"""

import struct
import sys
import os

# ── sentinel written for transparent pixels ──────────────────────────────────
TRANSPARENT = 0xFF


# ─────────────────────────────────────────────────────────────────────────────
# 1.  Parse wolf_palette.h  →  list of 256 (R,G,B) tuples
# ─────────────────────────────────────────────────────────────────────────────
def load_palette(palette_h_path):
    palette = []
    with open(palette_h_path) as f:
        for line in f:
            line = line.strip()
            if line.startswith('{') and line.endswith('},'):
                # e.g.  {  0,   0,   0},
                inner = line[1:-2]           # "  0,   0,   0"
                r, g, b = [int(v) for v in inner.split(',')]
                palette.append((r, g, b))
    if len(palette) != 256:
        raise ValueError(f"Expected 256 palette entries, got {len(palette)}")
    return palette


# ─────────────────────────────────────────────────────────────────────────────
# 2.  Read VSWAP header  →  (chunks_total, sprite_start, sound_start,
#                             offsets[], lengths[])
# ─────────────────────────────────────────────────────────────────────────────
def read_vswap_header(path):
    with open(path, 'rb') as f:
        chunks_total, sprite_start, sound_start = struct.unpack('<HHH', f.read(6))

        offsets = []
        for _ in range(chunks_total):
            offsets.append(struct.unpack('<I', f.read(4))[0])

        lengths = []
        for _ in range(chunks_total):
            lengths.append(struct.unpack('<H', f.read(2))[0])

    return chunks_total, sprite_start, sound_start, offsets, lengths


# ─────────────────────────────────────────────────────────────────────────────
# 3.  Read one raw chunk from VSWAP
# ─────────────────────────────────────────────────────────────────────────────
def read_chunk(f, offset, length):
    f.seek(offset)
    return f.read(length)


# ─────────────────────────────────────────────────────────────────────────────
# 4.  Decode one sprite chunk  →  bytearray of 64*64 palette indices
#     Transparent pixels  → TRANSPARENT (0xFF)
#     Opaque pixels       → palette index 0x00-0xFE
# ─────────────────────────────────────────────────────────────────────────────
def decode_sprite(chunk):
    # Start fully transparent
    pixels = bytearray([TRANSPARENT] * (64 * 64))

    if len(chunk) < 4:
        return pixels  # degenerate / empty sprite

    first_col = struct.unpack_from('<H', chunk, 0)[0]
    last_col  = struct.unpack_from('<H', chunk, 2)[0]

    # Sanity-check: columns must be 0-63
    if first_col > 63 or last_col > 63 or first_col > last_col:
        return pixels

    num_cols = last_col - first_col + 1

    # Column-offset table starts at byte 4
    col_offsets = []
    for i in range(num_cols):
        off = struct.unpack_from('<H', chunk, 4 + i * 2)[0]
        col_offsets.append(off)

    for col_idx in range(num_cols):
        col = first_col + col_idx
        ptr = col_offsets[col_idx]  # byte pointer into chunk

        while True:
            if ptr + 2 > len(chunk):
                break

            end_pixel_ofs = struct.unpack_from('<H', chunk, ptr)[0]
            ptr += 2

            if end_pixel_ofs == 0:
                break  # sentinel: no more posts in this column

            if ptr + 4 > len(chunk):
                break

            pixel_data_ofs = struct.unpack_from('<H', chunk, ptr)[0]
            ptr += 2
            start_pixel_ofs = struct.unpack_from('<H', chunk, ptr)[0]
            ptr += 2

            # pixel offsets are in units of 2 (they're word offsets into a
            # hypothetical 64-word column buffer)
            start_row = start_pixel_ofs // 2
            end_row   = end_pixel_ofs   // 2

            if start_row >= 64 or end_row > 64 or start_row >= end_row:
                continue

            count = end_row - start_row
            if pixel_data_ofs + count > len(chunk):
                count = len(chunk) - pixel_data_ofs
                if count <= 0:
                    continue

            for row in range(start_row, start_row + count):
                # Check bounds just in case the VSWAP is corrupted
                if pixel_data_ofs + row >= len(chunk):
                    continue
                    
                pal_idx = chunk[pixel_data_ofs + row] # <--- FIXED
                # Never write our sentinel value as an opaque pixel;
                # palette index 255 is extremely rare in Wolf sprites but
                # remap it to 254 (visually indistinguishable dark purple)
                if pal_idx == TRANSPARENT:
                    pal_idx = 0xFE
                pixels[row * 64 + col] = pal_idx

    return pixels


# ─────────────────────────────────────────────────────────────────────────────
# 5.  Save one PNG  (64x64 RGBA, transparent where pixel == TRANSPARENT)
# ─────────────────────────────────────────────────────────────────────────────
def save_png(pixels, palette, out_path):
    try:
        from PIL import Image
    except ImportError:
        return  # Pillow not available, skip PNG output silently

    img = Image.new('RGBA', (64, 64), (0, 0, 0, 0))
    raw = img.load()
    for i, idx in enumerate(pixels):
        x = i % 64
        y = i // 64
        if idx == TRANSPARENT:
            raw[x, y] = (0, 0, 0, 0)
        else:
            r, g, b = palette[idx]
            raw[x, y] = (r, g, b, 255)
    img.save(out_path)


# ─────────────────────────────────────────────────────────────────────────────
# 6.  Write sprites_8bit.h  (matches walls_8bit.h style exactly)
#     Layout: one row per pixel-row (64 values per line) to match weapon_8bit.h
# ─────────────────────────────────────────────────────────────────────────────
def write_sprite_header(sprites, sprite_start, sound_start, out_path):
    num_sprites = sound_start - sprite_start  # total sprite chunks

    with open(out_path, 'w') as out:
        out.write("/* AUTO-GENERATED WOLF3D 8-BIT SPRITE TEXTURES */\n")
        out.write("/* Format: 64x64 Row-Major 8-bit palette indices  */\n")
        out.write("/* Transparent pixels are marked as TRANSPARENCY_COLOR (0xFF) */\n")
        out.write("/* Chunk indices are relative to VSWAP sprite_start           */\n\n")
        out.write("#include <stdint.h>\n\n")
        out.write("#define ALLOC_FLASH __attribute__((section(\".rodata\")))\n\n")
        out.write(f"#define NUM_SPRITES {num_sprites}\n")
        out.write(f"#define SPRITE_START_CHUNK {sprite_start}  "
                  "/* absolute chunk index in VSWAP */\n")
        out.write("#define TRANSPARENCY_COLOR 0xFF  "
                  "/* palette index used as alpha-zero sentinel */\n\n")

        # ── Known sprite index table for WL1 shareware ────────────────────
        out.write("/*\n")
        out.write(" * Selected sprite indices (relative offset from sprite_start):\n")
        out.write(" *\n")
        out.write(" *  Offset  Description\n")
        out.write(" *  ------  -----------\n")
        out.write(" *    0     Static: clip (ammo)\n")
        out.write(" *    1     Static: machine gun pickup\n")
        out.write(" *    2     Static: chain gun pickup\n")
        out.write(" *    3     Static: food\n")
        out.write(" *    4     Static: medkit\n")
        out.write(" *    5     Static: gold key\n")
        out.write(" *    6     Static: silver key\n")
        out.write(" *    9     Static: cross (treasure)\n")
        out.write(" *   10     Static: chalice (treasure)\n")
        out.write(" *   11     Static: chest (treasure)\n")
        out.write(" *   12     Static: crown (treasure)\n")
        out.write(" *   13     Static: life (extra man)\n")
        out.write(" *   50..55 Guard walk S/W/N/E (4 frames each)\n")
        out.write(" *   56..59 Guard pain / die frames\n")
        out.write(" *   90..95 SS Guard walk / attack frames\n")
        out.write(" *  125     Hans Grosse (boss) idle\n")
        out.write(" *  180..182 Rocket launcher frames (WL6 only)\n")
        out.write(" *  415..418 Weapon: Knife frames\n")
        out.write(" *  419..422 Weapon: Pistol frames\n")
        out.write(" *  423..428 Weapon: Machine gun frames\n")
        out.write(" *  429..434 Weapon: Chain gun frames\n")
        out.write(" *\n")
        out.write(" * These offsets may shift by ±1 between shareware/full.\n")
        out.write(" * Cross-reference WL_DEF.H SPR_* enum from the Wolf4SDL source.\n")
        out.write(" */\n\n")

        out.write(f"ALLOC_FLASH const uint8_t sprite_textures[NUM_SPRITES][4096] = {{\n")

        for rel_idx, pixels in enumerate(sprites):
            abs_chunk = sprite_start + rel_idx
            out.write(f"  {{ // Sprite {rel_idx}  (VSWAP chunk {abs_chunk})\n")
            # 64 values per line  (one pixel-row), matching weapon_8bit.h style
            for row in range(64):
                row_bytes = pixels[row * 64 : row * 64 + 64]
                vals = ", ".join(f"{b:3}" for b in row_bytes)
                out.write(f"    {vals},\n")
            out.write("  },\n")

        out.write("};\n")


# ─────────────────────────────────────────────────────────────────────────────
# 7.  Main
# ─────────────────────────────────────────────────────────────────────────────
def main():
    vswap_path   = sys.argv[1] if len(sys.argv) > 1 else "original_assets/VSWAP.WL1"
    palette_path = sys.argv[2] if len(sys.argv) > 2 else "original_assets/wolf_palette.h"
    header_out   = "sprites_8bit.h"
    png_dir      = "sprites_png"

    # ── Palette ──────────────────────────────────────────────────────────────
    print(f"Loading palette from {palette_path}...")
    try:
        palette = load_palette(palette_path)
        print(f"  OK: {len(palette)} colours loaded.")
    except FileNotFoundError:
        print(f"  WARNING: {palette_path} not found — PNG output disabled.")
        palette = None

    # ── VSWAP header ─────────────────────────────────────────────────────────
    print(f"Reading VSWAP header from {vswap_path}...")
    chunks_total, sprite_start, sound_start, offsets, lengths = \
        read_vswap_header(vswap_path)

    num_walls   = sprite_start
    num_sprites = sound_start - sprite_start
    num_sounds  = chunks_total - sound_start

    print(f"  Chunks total : {chunks_total}")
    print(f"  Walls        : 0 .. {sprite_start - 1}  ({num_walls} chunks)")
    print(f"  Sprites      : {sprite_start} .. {sound_start - 1}  ({num_sprites} chunks)")
    print(f"  Sounds       : {sound_start} .. {chunks_total - 1}  ({num_sounds} chunks)")

    # ── PNG directory ─────────────────────────────────────────────────────────
    if palette:
        os.makedirs(png_dir, exist_ok=True)

    # ── Decode every sprite ───────────────────────────────────────────────────
    print(f"\nDecoding {num_sprites} sprites...")
    sprites = []

    with open(vswap_path, 'rb') as f:
        for rel_idx in range(num_sprites):
            abs_chunk = sprite_start + rel_idx
            offset = offsets[abs_chunk]
            length = lengths[abs_chunk]

            if offset == 0 or length == 0:
                # Null / empty chunk — blank transparent sprite
                pixels = bytearray([TRANSPARENT] * 4096)
            else:
                chunk = read_chunk(f, offset, length)
                pixels = decode_sprite(chunk)

            sprites.append(pixels)

            # Save PNG for visual verification
            if palette:
                png_path = os.path.join(png_dir, f"sprite_{rel_idx:04d}.png")
                save_png(pixels, palette, png_path)

            if (rel_idx + 1) % 50 == 0 or rel_idx == num_sprites - 1:
                print(f"  {rel_idx + 1}/{num_sprites} done...")

    # ── Write C header ────────────────────────────────────────────────────────
    print(f"\nWriting {header_out}...")
    write_sprite_header(sprites, sprite_start, sound_start, header_out)
    size_kb = os.path.getsize(header_out) / 1024
    print(f"  OK: {header_out}  ({size_kb:.1f} KB text)")

    if palette:
        png_count = len([f for f in os.listdir(png_dir) if f.endswith('.png')])
        print(f"  OK: {png_dir}/  ({png_count} PNGs for visual verification)")

    print("\nDone.")
    print(f"\nMemory budget (Flash):")
    raw_bytes = num_sprites * 4096
    print(f"  {num_sprites} sprites × 4096 B = {raw_bytes // 1024} KB")
    print(f"  Compare: {num_walls} walls × 4096 B = {num_walls * 4096 // 1024} KB")
    print(f"  Tip: if Flash is tight, store only the sprite indices you actually")
    print(f"  reference and exclude null/empty sprites (all-0xFF rows).")


if __name__ == "__main__":
    main()
