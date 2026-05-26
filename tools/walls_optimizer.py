import struct

# The exact 28 wall textures needed for E1M1
USED_WALLS = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 40, 41, 98, 99]

def extract_walls():
    with open('original_assets/VSWAP.WL1', 'rb') as f:
        # Read ALL THREE 16-bit header values so the offsets array aligns perfectly!
        num_chunks = struct.unpack('<H', f.read(2))[0]
        sprite_start = struct.unpack('<H', f.read(2))[0]
        sound_start = struct.unpack('<H', f.read(2))[0] 
        
        offsets = [struct.unpack('<I', f.read(4))[0] for _ in range(num_chunks)]

        with open('stm32_walls_8bit.h', 'w') as out:
            out.write('#ifndef STM32_WALLS_8BIT_H\n#define STM32_WALLS_8BIT_H\n\n#include <stdint.h>\n\n')
            
            out.write('const uint8_t wall_lut[100] = {\n    ')
            lut = [0] * 100
            for packed_idx, original_id in enumerate(USED_WALLS):
                lut[original_id] = packed_idx
            
            out.write(', '.join(map(str, lut)))
            out.write('\n};\n\n')

            out.write(f'#define NUM_WALLS {len(USED_WALLS)}\n')
            out.write('const uint8_t wall_textures[NUM_WALLS][4096] = {\n')
            
            for i, chunk_idx in enumerate(USED_WALLS):
                f.seek(offsets[chunk_idx])
                data = f.read(4096)
                out.write('    { ')
                out.write(', '.join([f'0x{b:02X}' for b in data]))
                out.write(' }' + (',' if i < len(USED_WALLS) - 1 else '') + '\n')
                
            out.write('};\n\n#endif\n')
            print("stm32_walls_8bit.h generated successfully!")

extract_walls()
