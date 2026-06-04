import re
import csv

CSV_FILE = 'sound_selection.csv'
INPUT_HEADER = 'sounds_pcm.h'
OUTPUT_HEADER = 'stm32_sounds.h'

def process_for_piezo(data_bytes):
    processed = []
    
    # Mathematical Model 1: First Derivative (High-Pass / Transient Emphasis)
    # Model 2: Infinite Hard Clipping (1-Bit Quantization)
    
    for i in range(len(data_bytes)):
        if i == 0:
            processed.append(128) # Center line
            continue
            
        # 1. Take the derivative (difference between current and previous sample)
        # Standard PCM center is 128.
        current_sample = data_bytes[i]
        prev_sample = data_bytes[i-1]
        delta = current_sample - prev_sample
        
        # 2. Apply massive gain to the delta
        amplified_delta = delta * 8 
        
        # 3. Re-center the audio
        new_val = 128 + amplified_delta
        
        # 4. 1-Bit Hard Clipping (Square Wave Generation)
        # If the wave is pushing up, slam it to 255. If pulling down, slam to 0.
        # We leave a small "deadzone" around 128 so absolute silence remains silent (no hissing).
        if new_val > 135:
            crushed_val = 255
        elif new_val < 120:
            crushed_val = 0
        else:
            crushed_val = 128 # Silence

        processed.append(crushed_val)
        
    return processed

def main():
    sounds_to_keep = []
    
    with open(CSV_FILE, 'r', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        for row in reader:
            if row['Keep'].strip().lower() == 'yes':
                clean_label = re.sub(r'[^a-zA-Z0-9_]', '_', row['Your Label']).lower()
                sounds_to_keep.append({
                    'old_array': row['Original Array Name'].strip(),
                    'new_array': f"snd_{clean_label}",
                    'macro': f"SND_{clean_label.upper()}",
                    'size': row['Size (Bytes)'].strip()
                })

    with open(INPUT_HEADER, 'r', encoding='utf-8') as f:
        content = f.read()

    pattern = re.compile(
        r'(?:const\s+)?(?:unsigned\s+char|uint8_t)\s+([a-zA-Z0-9_]+)\[.*?\]\s*=\s*\{([^}]+)\};?', 
        re.MULTILINE | re.DOTALL
    )
    
    array_data = {}
    for match in pattern.finditer(content):
        # Extract raw integers
        data_str = match.group(2).replace('\n', '').strip()
        raw_bytes = [int(x.strip(), 16) if x.strip().lower().startswith('0x') else int(x.strip()) 
                     for x in data_str.split(',') if x.strip()]
        
        # Apply the Piezo Math Model
        piezo_bytes = process_for_piezo(raw_bytes)
        
        # Convert back to C array string format
        hex_strings = [f"0x{b:02X}" for b in piezo_bytes]
        array_data[match.group(1)] = ", ".join(hex_strings)

    # Generate the header
    with open(OUTPUT_HEADER, 'w', encoding='utf-8') as out:
        out.write("/* AUTO-GENERATED PIEZO-OPTIMIZED SOUND PACK */\n")
        out.write("#ifndef STM32_SOUNDS_H\n")
        out.write("#define STM32_SOUNDS_H\n\n")
        out.write("#include <stdint.h>\n\n")
        out.write(f"#define NUM_SOUNDS       {len(sounds_to_keep)}\n\n")

        out.write("// --- RAW PCM DATA (7075 Hz, 1-Bit Piezo-Crushed) ---\n")
        for s in sounds_to_keep:
            old_name = s['old_array']
            new_name = s['new_array']
            data = array_data.get(old_name, "0x80")
            out.write(f"const uint8_t {new_name}[] = {{{data}}};\n\n")

        out.write("// --- DMA PLAYBACK TABLES ---\n")
        out.write("const uint8_t* const sound_pointers[NUM_SOUNDS] = {\n")
        for s in sounds_to_keep:
            out.write(f"    {s['new_array']},\n")
        out.write("};\n\n")

        out.write("const uint32_t sound_lengths[NUM_SOUNDS] = {\n")
        for s in sounds_to_keep:
            out.write(f"    {s['size']},\n")
        out.write("};\n\n")

        out.write("#endif // STM32_SOUNDS_H\n")
        
    print(f"Success! Piezo-Optimized file saved as: {OUTPUT_HEADER}")

if __name__ == "__main__":
    main()
