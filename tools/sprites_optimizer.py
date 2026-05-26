import struct

# The exact 28 sprites needed for E1M1 guards, dogs, pickups, and weapons
USED_SPRITES = [3, 4, 5, 10, 27, 28, 50, 58, 66, 74, 90, 91, 92, 93, 95, 97, 98, 99, 107, 115, 123, 131, 132, 133, 134, 135, 136, 137, 421, 422, 423, 424]

def extract_sprites():
    with open('original_assets/VSWAP.WL1', 'rb') as f:
        num_chunks = struct.unpack('<H', f.read(2))[0]
        sprite_start = struct.unpack('<H', f.read(2))[0]
        sound_start = struct.unpack('<H', f.read(2))[0]
        
        offsets = [struct.unpack('<I', f.read(4))[0] for _ in range(num_chunks)]
        lengths = [struct.unpack('<H', f.read(2))[0] for _ in range(num_chunks)]

        with open('stm32_sprites_8bit.h', 'w') as out:
            out.write('#ifndef STM32_SPRITES_8BIT_H\n#define STM32_SPRITES_8BIT_H\n\n#include <stdint.h>\n\n')
            
            max_id = max(USED_SPRITES) + 1
            out.write(f'const uint16_t sprite_lut[{max_id}] = {{\n    ')
            lut = [0] * max_id
            for packed_idx, original_id in enumerate(USED_SPRITES):
                if original_id < max_id:
                    lut[original_id] = packed_idx
            
            out.write(', '.join(map(str, lut)))
            out.write('\n};\n\n')

            out.write(f'#define NUM_SPRITES {len(USED_SPRITES)}\n')
            out.write('const uint8_t sprite_textures[NUM_SPRITES][4096] = {\n')
            
            for i, original_id in enumerate(USED_SPRITES):
                sprite_data = [255] * 4096 
                chunk_idx = sprite_start + original_id 
                
                if chunk_idx < num_chunks and lengths[chunk_idx] > 0 and chunk_idx < sound_start:
                    f.seek(offsets[chunk_idx])
                    chunk = f.read(lengths[chunk_idx])
                    
                    try:
                        left = struct.unpack_from('<H', chunk, 0)[0]
                        right = struct.unpack_from('<H', chunk, 2)[0]
                        
                        for c in range(right - left + 1):
                            ofs = struct.unpack_from('<H', chunk, 4 + c*2)[0]
                            if ofs == 0: continue
                            pos = ofs
                            
                            while pos + 6 <= len(chunk):
                                end_y = struct.unpack_from('<H', chunk, pos)[0]
                                if end_y == 0: break
                                end_y //= 2
                                
                                # --- THE MAGIC FIX ---
                                # This is the absolute offset in the chunk where the pixels live
                                data_ptr = struct.unpack_from('<H', chunk, pos + 2)[0]
                                
                                start_y = struct.unpack_from('<H', chunk, pos + 4)[0]
                                start_y //= 2
                                
                                # The compiler pre-calculated `data_ptr` so that adding `y` 
                                # perfectly aligns with the pixel color in the byte array!
                                for y in range(start_y, end_y):
                                    if 0 <= y < 64 and (data_ptr + y) < len(chunk):
                                        sprite_data[y * 64 + left + c] = chunk[data_ptr + y]
                                        
                                pos += 6
                    except Exception as e:
                        print(f"Error decoding sprite {original_id}: {e}")

                out.write('    { ')
                out.write(', '.join([f'0x{b:02X}' for b in sprite_data]))
                out.write(' }' + (',' if i < len(USED_SPRITES) - 1 else '') + '\n')

            out.write('};\n\n#endif\n')
            print("stm32_sprites_8bit.h generated successfully!.")

extract_sprites()
