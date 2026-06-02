import re
import os
import numpy as np
from PIL import Image

HEADER_FILE = "hud_faces.h"
OUTPUT_DIR = "faces"

FACE_W = 24
FACE_H = 32

os.makedirs(OUTPUT_DIR, exist_ok=True)

with open(HEADER_FILE, "r", encoding="utf-8", errors="ignore") as f:
    text = f.read()

# Find each face block
face_pattern = re.compile(
    r'/\*\s*\[\s*(\d+)\]\s*([A-Z0-9_]+).*?\*/(.*?)\n\s*\},',
    re.DOTALL
)

faces = face_pattern.findall(text)

if not faces:
    raise RuntimeError("No face blocks found")

print(f"Found {len(faces)} faces")

for idx, name, block in faces:

    # Extract every hex byte inside this face block
    bytes_found = re.findall(r'0x([0-9A-Fa-f]{2})', block)
    data = bytes(int(x, 16) for x in bytes_found)

    expected_size = FACE_H * FACE_W * 2

    if len(data) < expected_size:
        print(f"Skipping {name}: only {len(data)} bytes")
        continue

    data = data[:expected_size]

    img = np.zeros((FACE_H, FACE_W, 3), dtype=np.uint8)

    pos = 0
    for y in range(FACE_H):
        for x in range(FACE_W):

            rgb565 = (data[pos] << 8) | data[pos + 1]
            pos += 2

            r = ((rgb565 >> 11) & 0x1F) * 255 // 31
            g = ((rgb565 >> 5) & 0x3F) * 255 // 63
            b = (rgb565 & 0x1F) * 255 // 31

            img[y, x] = (r, g, b)

    Image.fromarray(img).save(
        os.path.join(OUTPUT_DIR, f"{name}.png")
    )

    print(f"Saved {name}.png")

print("Done.")
