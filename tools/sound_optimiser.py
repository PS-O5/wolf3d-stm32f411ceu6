import re
import csv
import os

CSV_FILE = 'sound_selection.csv'
INPUT_HEADER = 'sounds_pcm.h'
OUTPUT_HEADER = 'optimized_sounds.h'

def main():
    sounds_to_keep = []
    
    # 1. Read the CSV for decisions
    with open(CSV_FILE, 'r', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        for row in reader:
            if row['Keep'].strip().lower() == 'yes':
                # Sanitize the label for C syntax (no spaces, all lowercase for arrays)
                clean_label = re.sub(r'[^a-zA-Z0-9_]', '_', row['Your Label']).lower()
                sounds_to_keep.append({
                    'old_array': row['Original Array Name'].strip(),
                    'new_array': f"snd_{clean_label}",
                    'macro': f"SND_{clean_label.upper()}",
                    'size': row['Size (Bytes)'].strip()
                })

    print(f"Keeping {len(sounds_to_keep)} sounds...")

    # 2. Extract raw data from original header
    with open(INPUT_HEADER, 'r', encoding='utf-8') as f:
        content = f.read()

    pattern = re.compile(
        r'(?:const\s+)?(?:unsigned\s+char|uint8_t)\s+([a-zA-Z0-9_]+)\[.*?\]\s*=\s*\{([^}]+)\};?', 
        re.MULTILINE | re.DOTALL
    )
    
    array_data = {}
    for match in pattern.finditer(content):
        array_data[match.group(1)] = match.group(2)

    # 3. Generate the optimized header
    with open(OUTPUT_HEADER, 'w', encoding='utf-8') as out:
        out.write("/* AUTO-GENERATED OPTIMIZED SOUND PACK */\n")
        out.write("#ifndef OPTIMIZED_SOUNDS_H\n")
        out.write("#define OPTIMIZED_SOUNDS_H\n\n")
        out.write("#include <stdint.h>\n\n")

        # Write Macros
        out.write("// --- SOUND ID MACROS ---\n")
        for i, s in enumerate(sounds_to_keep):
            out.write(f"#define {s['macro']:<16} {i}\n")
        out.write(f"\n#define NUM_SOUNDS       {len(sounds_to_keep)}\n\n")

        # Write Arrays
        out.write("// --- RAW PCM DATA (7042 Hz, 8-bit Unsigned) ---\n")
        for s in sounds_to_keep:
            old_name = s['old_array']
            new_name = s['new_array']
            data = array_data.get(old_name, "0x00 /* ERROR: DATA MISSING */")
            out.write(f"const uint8_t {new_name}[] = {{{data}}};\n\n")

        # Write Pointer Table (For DMA)
        out.write("// --- DMA PLAYBACK TABLES ---\n")
        out.write("const uint8_t* const sound_pointers[NUM_SOUNDS] = {\n")
        for s in sounds_to_keep:
            out.write(f"    {s['new_array']},\n")
        out.write("};\n\n")

        # Write Length Table (For DMA)
        out.write("const uint32_t sound_lengths[NUM_SOUNDS] = {\n")
        for s in sounds_to_keep:
            out.write(f"    {s['size']},\n")
        out.write("};\n\n")

        out.write("#endif // OPTIMIZED_SOUNDS_H\n")
        
    print(f"Success! Optimized file saved as: {OUTPUT_HEADER}")

if __name__ == "__main__":
    main()
