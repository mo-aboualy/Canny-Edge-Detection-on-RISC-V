import numpy as np
from PIL import Image
import sys
import os

def convert_to_raw(input_path, output_path, size=(512, 512)):
    # Debug: Check if file actually exists and its size
    if not os.path.exists(input_path):
        print(f"Error: Path '{input_path}' does not exist.")
        return

    print(f"Opening: {input_path} (File size: {os.path.getsize(input_path)} bytes)")

    try:
        # 1. Open and convert to grayscale
        img = Image.open(input_path).convert('L')
        
        # 2. Resize
        img = img.resize(size)
        
        # 3. Convert to NumPy array
        raw_data = np.array(img, dtype=np.uint8)
        
        # 4. Save
        raw_data.tofile(output_path)
        
        print(f"Success! Created {output_path} ({size[0]}x{size[1]})")
    except Exception as e:
        print(f"Detailed Error: {e}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python3 prepare_image.py <input_image>")
    else:
        # We output to the root folder for easier access by your C++ code
        convert_to_raw(sys.argv[1], "input_512.raw")