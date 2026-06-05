import numpy as np
import matplotlib.pyplot as plt
import os
import sys

def load_raw(filename, width, height):
    """Helper to read binary data and reshape it."""
    if not os.path.exists(filename):
        print(f"Error: File '{filename}' not found.")
        return None
    
    data = np.fromfile(filename, dtype=np.uint8)
    
    try:
        return data.reshape((height, width))
    except ValueError:
        print(f"Error: File size {data.size} doesn't match {width}x{height}.")
        return None

def main():
    # Use command line args or default to 100x100 if not provided
    # Usage: python3 view_result.py <file1> [file2] [width] [height]
    args = sys.argv[1:]
    
    if len(args) < 1:
        print("Usage: python3 view_result.py <filename> [optional_second_file] [width] [height]")
        return

    # Default settings
    width = 100
    height = 100
    files = []

    # Parse arguments
    for arg in args:
        if arg.isdigit():
            # Assume first number is width, second is height
            if width == 100 and arg != "100": width = int(arg)
            else: height = int(arg)
        else:
            files.append(arg)

    # Visualization
    num_files = len(files)
    fig, axes = plt.subplots(1, num_files, figsize=(5 * num_files, 5))
    
    # Handle single vs multiple plots
    if num_files == 1: axes = [axes]

    for i, filename in enumerate(files):
        img = load_raw(filename, width, height)
        if img is not None:
            im = axes[i].imshow(img, cmap='gray', vmin=0, vmax=255)
            axes[i].set_title(f"{filename}\n({width}x{height})")
            plt.colorbar(im, ax=axes[i], fraction=0.046, pad=0.04)
        else:
            axes[i].text(0.5, 0.5, "File Load Error", ha='center')

    plt.tight_layout()
    plt.show()

if __name__ == "__main__":
    main()