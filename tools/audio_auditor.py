import os
import re
import wave
import csv
import platform
import subprocess

# --- CONFIGURATION ---
HEADER_FILE = "sounds_pcm.h"
OUTPUT_DIR = "exported_wavs"
CSV_FILE = "sound_selection.csv"
SAMPLE_RATE = 7042

def parse_h_file(filepath):
    print(f"Parsing {filepath}...")
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    # Regex to find standard C byte arrays: const uint8_t name[] = { ... };
    # Handles 'uint8_t', 'unsigned char', with or without 'const'
    pattern = re.compile(
        r'(?:const\s+)?(?:unsigned\s+char|uint8_t)\s+([a-zA-Z0-9_]+)\[.*?\]\s*=\s*\{([^}]+)\}', 
        re.MULTILINE | re.DOTALL
    )
    
    sounds = {}
    for match in pattern.finditer(content):
        name = match.group(1)
        data_str = match.group(2)
        
        # Clean up the string and split by commas
        data_str = data_str.replace('\n', '').replace('\r', '').strip()
        bytes_list = []
        
        for val in data_str.split(','):
            val = val.strip()
            if not val: 
                continue
            try:
                if val.lower().startswith('0x'):
                    bytes_list.append(int(val, 16))
                else:
                    bytes_list.append(int(val))
            except ValueError:
                pass # Ignore non-numeric artifacts (like comments inside the array)
                
        sounds[name] = bytes(bytes_list)
        
    print(f"Found {len(sounds)} audio arrays.")
    return sounds

def save_wav(name, data):
    if not os.path.exists(OUTPUT_DIR):
        os.makedirs(OUTPUT_DIR)
        
    filepath = os.path.join(OUTPUT_DIR, f"{name}.wav")
    
    # Python's 'wave' library naturally treats 1-byte width as 8-bit unsigned PCM
    with wave.open(filepath, "wb") as wav:
        wav.setnchannels(1)       # Mono
        wav.setsampwidth(1)       # 8-bit
        wav.setframerate(SAMPLE_RATE)
        wav.writeframes(data)
        
    return filepath

def play_sound(filepath):
    system = platform.system()
    try:
        if system == "Windows":
            import winsound
            winsound.PlaySound(filepath, winsound.SND_FILENAME)
        elif system == "Darwin": # macOS
            subprocess.run(["afplay", filepath], check=True)
        else: # Linux
            subprocess.run(["aplay", "-q", filepath], check=True)
    except Exception as e:
        print(f"  [!] Audio playback failed: {e}. You can play the file manually from the '{OUTPUT_DIR}' folder.")

def main():
    if not os.path.exists(HEADER_FILE):
        print(f"Error: Could not find '{HEADER_FILE}' in the current directory.")
        return

    sounds = parse_h_file(HEADER_FILE)
    if not sounds:
        print("No valid C arrays found. Check your header file formatting.")
        return

    results = []

    print("\n--- Starting Audio Auditor ---")
    print("Press ENTER to play/replay. Type your answers when ready.\n")

    for i, (name, data) in enumerate(sounds.items(), 1):
        wav_path = save_wav(name, data)
        duration = len(data) / SAMPLE_RATE
        
        print("-" * 40)
        print(f"Sound {i}/{len(sounds)}: {name} ({duration:.2f} seconds)")
        
        while True:
            play_sound(wav_path)
            action = input("  Replay (ENTER) | Label/Name it: ").strip()
            if action != "":
                label = action
                break
                
        keep_input = input("  Keep this sound for the game? (y/n): ").strip().lower()
        keep = True if keep_input.startswith('y') else False
        
        results.append({
            "Original Array Name": name,
            "Your Label": label,
            "Keep": "Yes" if keep else "No",
            "Size (Bytes)": len(data)
        })

    # Save to CSV
    print(f"\nAudit complete! Saving results to {CSV_FILE}...")
    with open(CSV_FILE, 'w', newline='', encoding='utf-8') as csvfile:
        fieldnames = ["Original Array Name", "Your Label", "Keep", "Size (Bytes)"]
        writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
        writer.writeheader()
        for row in results:
            writer.writerow(row)
            
    print(f"Done. All playable .wav files are in the '{OUTPUT_DIR}' folder.")

if __name__ == "__main__":
    main()
