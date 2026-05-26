import struct
import sys

def decompress_carmack(compressed_bytes):
    out_words = []
    
    # FIX: The first 16-bit word is the uncompressed length in bytes!
    expected_bytes = struct.unpack_from('<H', compressed_bytes, 0)[0]
    expected_words = expected_bytes // 2
    
    # Start reading actual data AFTER the length word
    i = 2 
    
    while i < len(compressed_bytes) and len(out_words) < expected_words:
        word = struct.unpack_from('<H', compressed_bytes, i)[0]
        i += 2
        hi = word >> 8
        lo = word & 0xFF
        
        if hi == 0xA7: # Near Pointer
            if lo == 0: # Exception: it's a literal word, not a pointer
                out_words.append(struct.unpack_from('<H', compressed_bytes, i)[0])
                i += 2
            else:
                offset = compressed_bytes[i]
                i += 1
                start = len(out_words) - offset
                for j in range(lo):
                    out_words.append(out_words[start + j])
                    
        elif hi == 0xA8: # Far Pointer
            if lo == 0: # Exception: it's a literal word, not a pointer
                out_words.append(struct.unpack_from('<H', compressed_bytes, i)[0])
                i += 2
            else:
                offset = struct.unpack_from('<H', compressed_bytes, i)[0]
                i += 2
                for j in range(lo):
                    out_words.append(out_words[offset + j])
                    
        else: # Uncompressed word
            out_words.append(word)
            
    return out_words

def decompress_rlew(compressed_words, rlew_tag):
    out_words = []
    # The first word is the final expanded size in bytes
    expected_bytes = compressed_words[0]
    expected_words = expected_bytes // 2
    
    i = 1
    while i < len(compressed_words) and len(out_words) < expected_words:
        word = compressed_words[i]
        i += 1
        if word == rlew_tag:
            count = compressed_words[i]
            val = compressed_words[i+1]
            i += 2
            out_words.extend([val] * count)
        else:
            out_words.append(word)
            
    return out_words

def extract_level_1(maphead_path, gamemaps_path, output_path):
    print(f"Reading {maphead_path}...")
    with open(maphead_path, 'rb') as f:
        rlew_tag = struct.unpack('<H', f.read(2))[0]
        level_1_offset = struct.unpack('<I', f.read(4))[0]

    print(f"Reading {gamemaps_path} at offset {level_1_offset}...")
    with open(gamemaps_path, 'rb') as f:
        f.seek(level_1_offset)
        plane_offsets = struct.unpack('<III', f.read(12))
        plane_lengths = struct.unpack('<HHH', f.read(6))
        width, height = struct.unpack('<HH', f.read(4))
        name = f.read(16).decode('ascii').strip('\x00')
        
        print(f"Level Found: '{name}' ({width}x{height})")
        
        f.seek(plane_offsets[0])
        compressed_plane_0 = f.read(plane_lengths[0])

    print("Decompressing Carmack -> RLEW -> Raw...")
    carmack_words = decompress_carmack(compressed_plane_0)
    raw_map_words = decompress_rlew(carmack_words, rlew_tag)

    if len(raw_map_words) != 4096:
        print(f"Error: Expected 4096 tiles, got {len(raw_map_words)}")
        return
    else:
        print("Success: Exactly 4096 tiles recovered.")

    print(f"Writing 8-bit C array to {output_path}...")
    with open(output_path, 'w') as out:
        out.write(f"/* AUTO-GENERATED WOLF3D MAP: {name} */\n")
        out.write("/* Format: 64x64 8-bit Tile IDs (Row-Major) */\n\n")
        out.write("#include <stdint.h>\n\n")
        out.write("const uint8_t map_e1m1[4096] = {\n")
        
        for y in range(64):
            out.write("  ")
            for x in range(64):
                tile_16bit = raw_map_words[y * 64 + x]
                tile_8bit = tile_16bit & 0xFF 
                out.write(f"{tile_8bit:3},")
            out.write("\n")
        out.write("};\n")
    
    print("Success! Map extracted and downcasted to 4 KB.")

if __name__ == "__main__":
    extract_level_1("original_assets/MAPHEAD.WL1", "original_assets/GAMEMAPS.WL1", "map_e1m1.h")
