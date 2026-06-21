"""
Tool/prepare_image.py

Converts any standard image file (JPEG, PNG, etc.) into the raw grayscale
format the C++ pipeline expects: a flat binary file of exactly
width*height bytes, one byte per pixel, no header.

This script exists because the project deliberately avoids image-decoding
libraries in C++ (per the spec: "raw grayscale format... eliminates
library dependencies"). Instead, all decoding (JPEG/PNG -> grayscale ->
fixed size) happens once, offline, in Python -- and the C++ side only
ever has to deal with the simplest possible format.
"""

import numpy as np
from PIL import Image
import sys
import os

def convert_to_raw(input_path, output_path, size=(512, 512)):
    # Debug: Check if file actually exists and its size.
    # Fails fast with a clear message instead of letting PIL throw a
    # less obvious error further down.
    if not os.path.exists(input_path):
        print(f"Error: Path '{input_path}' does not exist.")
        return

    print(f"Opening: {input_path} (File size: {os.path.getsize(input_path)} bytes)")

    try:
        # 1. Open and convert to grayscale.
        # 'L' is Pillow's mode code for 8-bit grayscale (Luminance) --
        # this matches the C++ side's uint8_t pixel type exactly: one
        # byte per pixel, values 0 (black) to 255 (white).
        img = Image.open(input_path).convert('L')

        # 2. Resize.
        # The C++ pipeline has no way to read width/height from the raw
        # file itself (no header), so the dimensions must be fixed and
        # known in advance on both sides. Forcing every input to the same
        # size keeps the C++ command-line arguments simple and consistent.
        img = img.resize(size)

        # 3. Convert to NumPy array.
        # dtype=np.uint8 ensures each pixel is stored as exactly one byte,
        # matching the C++ struct's uint8_t* pixels layout.
        raw_data = np.array(img, dtype=np.uint8)

        # 4. Save.
        # .tofile() writes the raw bytes directly to disk with no header,
        # no metadata, and no compression -- this is the exact "raw
        # grayscale" format required by the project spec, and what
        # image_read() in image_io.cpp expects to find.
        raw_data.tofile(output_path)

        print(f"Success! Created {output_path} ({size[0]}x{size[1]})")
    except Exception as e:
        print(f"Detailed Error: {e}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 prepare_image.py <input_image>")
    else:
        # We output to the root folder for easier access by your C++ code.
        # Hardcoding "input_512.raw" keeps it consistent with the default
        # filename Test_blur.cpp expects when run with no arguments.
        convert_to_raw(sys.argv[1], "input_512.raw")
