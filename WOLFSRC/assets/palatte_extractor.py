import os

def extract_gamepal():
    # Point this to where you archived the OBJ folder
    obj_path = "../dos_archive/OBJ/GAMEPAL.OBJ" 
    
    if not os.path.exists(obj_path):
        print(f"Error: Cannot find {obj_path}. Check capitalization.")
        return
        
    with open(obj_path, "rb") as f:
        data = f.read()

    # The Wolf3D palette starts with the 16 standard EGA colors in 6-bit VGA format.
    # We search for the first 4 colors (Black, Blue, Green, Cyan) to find the start.
    signature = bytes([0x00, 0x00, 0x00, 0x00, 0x00, 0x2A, 0x00, 0x2A, 0x00, 0x00, 0x2A, 0x2A])
    idx = data.find(signature)
    
    if idx == -1:
        print("Error: Signature not found in GAMEPAL.OBJ")
        return
        
    palette_data = data[idx : idx + 768]
    
    with open("wolf_palette.h", "w") as out:
        out.write("/* AUTO-GENERATED TRUE WOLF3D PALETTE */\n")
        out.write("#include <stdint.h>\n\n")
        out.write("const uint8_t wolf_rgb[256][3] = {\n")
        for i in range(256):
            # VGA hardware used 6-bit color (0-63). 
            # We scale this up to standard 8-bit (0-255) for modern rendering.
            r = (palette_data[i*3] * 255) // 63
            g = (palette_data[i*3+1] * 255) // 63
            b = (palette_data[i*3+2] * 255) // 63
            out.write(f"  {{{r:3}, {g:3}, {b:3}}},\n")
        out.write("};\n")
        
    print("Success! wolf_palette.h extracted.")

if __name__ == "__main__":
    extract_gamepal()
