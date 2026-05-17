import struct
import os
import sys

def extract_vswap_8bit(vswap_path, output_path):
    print(f"Opening {vswap_path}...")
    
    try:
        with open(vswap_path, 'rb') as f:
            # 1. Read the Header (3 x 16-bit little-endian integers)
            # ChunksInFile, SpriteStartOffset, SoundStartOffset
            header_data = f.read(6)
            chunks_in_file, sprite_start, sound_start = struct.unpack('<HHH', header_data)
            
            print(f"Total Chunks: {chunks_in_file}")
            print(f"Wall Textures: 0 to {sprite_start - 1} (Total: {sprite_start})")
            
            # 2. Read Offsets (32-bit little-endian integers)
            offsets = []
            for _ in range(chunks_in_file):
                offset = struct.unpack('<I', f.read(4))[0]
                offsets.append(offset)
                
            # 3. Read Lengths (16-bit little-endian integers)
            lengths = []
            for _ in range(chunks_in_file):
                length = struct.unpack('<H', f.read(2))[0]
                lengths.append(length)

            # 4. Extract Wall Textures
            # Walls are guaranteed to be 64x64 pixels = 4096 bytes.
            walls = []
            for i in range(sprite_start):
                f.seek(offsets[i])
                wall_data = f.read(4096)
                
                # Sanity check: Ensure we actually read 4096 bytes
                if len(wall_data) != 4096:
                    print(f"Warning: Wall {i} is not 4096 bytes!")
                
                walls.append(wall_data)

    except FileNotFoundError:
        print(f"Error: Could not find {vswap_path}. Ensure the file exists.")
        sys.exit(1)

    # 5. Output to C Header
    print(f"Writing C header to {output_path}...")
    with open(output_path, 'w') as out:
        out.write("/* AUTO-GENERATED WOLF3D 8-BIT WALL TEXTURES */\n")
        out.write("/* Format: 64x64 Column-Major 8-bit indices */\n\n")
        out.write("#include <stdint.h>\n\n")
        
        # Add the linker attribute to force this into Flash (.rodata)
        out.write("#define ALLOC_FLASH __attribute__((section(\".rodata\")))\n\n")
        
        out.write(f"#define NUM_WALLS {sprite_start}\n\n")
        
        out.write("ALLOC_FLASH const uint8_t wall_textures[NUM_WALLS][4096] = {\n")
        
        for i, wall in enumerate(walls):
            out.write(f"  {{ // Wall {i}\n")
            
            # Write 16 bytes per line for readable formatting
            for j in range(0, 4096, 16):
                chunk = wall[j:j+16]
                hex_chunk = ", ".join([f"0x{b:02X}" for b in chunk])
                out.write(f"    {hex_chunk},\n")
                
            out.write("  },\n")
            
        out.write("};\n")
        
    print(f"Success! {sprite_start} walls extracted to {output_path}.")
    # 48 walls * 4096 bytes = ~196 KB in Flash.

if __name__ == "__main__":
    # Ensure you have VSWAP.WL1 (Shareware) or VSWAP.WL6 (Full) in the directory
    input_file = "VSWAP.WL1" 
    output_file = "walls_8bit.h"
    extract_vswap_8bit(input_file, output_file)
