"""
Tool/view_result.py

Reads one or more raw grayscale files (the C++ pipeline's output format)
and displays them as actual viewable images side-by-side using matplotlib.

This is the visual counterpart to host_tests.cpp's automated assertions --
where the GoogleTest suite checks numeric properties of the output
(e.g. "border pixels equal 0"), this script lets a human actually LOOK
at the blurred image, the gradient magnitude image, etc., to sanity-check
the pipeline produces something that visually makes sense.
"""

import numpy as np
import matplotlib.pyplot as plt
import os
import sys

def load_raw(filename, width, height):
    """Helper to read binary data and reshape it."""
    if not os.path.exists(filename):
        print(f"Error: File '{filename}' not found.")
        return None

    # Read the entire file as a flat 1D array of unsigned bytes -- this
    # matches exactly what image_write() in image_io.cpp wrote: no
    # header, just width*height raw bytes back to back.
    data = np.fromfile(filename, dtype=np.uint8)

    try:
        # Reshape the flat array into a 2D grid. This only works if the
        # given width/height EXACTLY match what the C++ side used when
        # writing the file -- there is no header to read the real
        # dimensions from, so a mismatch here is the most common way
        # this script fails (and is itself diagnostic: if reshape fails,
        # the size doesn't match what was actually written).
        return data.reshape((height, width))
    except ValueError:
        print(f"Error: File size {data.size} doesn't match {width}x{height}.")
        return None

def main():
    # Use command line args or default to 100x100 if not provided.
    # Usage: python3 view_result.py <file1> [file2] [width] [height]
    args = sys.argv[1:]

    if len(args) < 1:
        print("Usage: python3 view_result.py <filename> [optional_second_file] [width] [height]")
        return

    # Default settings.
    # 100x100 matches the default size used by Verify_io.cpp's test
    # pattern, so running this script with no size arguments "just works"
    # for that file specifically.
    width = 100
    height = 100
    files = []

    # Parse arguments.
    # Distinguishes numeric arguments (treated as width/height) from
    # non-numeric arguments (treated as filenames), so the script accepts
    # files and dimensions in any order on the command line.
    for arg in args:
        if arg.isdigit():
            # Assume first number is width, second is height.
            if width == 100 and arg != "100": width = int(arg)
            else: height = int(arg)
        else:
            files.append(arg)

    # Visualization.
    # Creates one subplot per file passed in, so you can compare e.g.
    # the blurred output and the Sobel magnitude output side by side
    # in a single window.
    num_files = len(files)
    fig, axes = plt.subplots(1, num_files, figsize=(5 * num_files, 5))

    # Handle single vs multiple plots.
    # matplotlib returns a single Axes object (not a list) when there's
    # only one subplot, so wrapping it in a list here keeps the loop
    # below working uniformly for both cases.
    if num_files == 1: axes = [axes]

    for i, filename in enumerate(files):
        img = load_raw(filename, width, height)
        if img is not None:
            # cmap='gray' forces true grayscale rendering instead of
            # matplotlib's default color map. vmin=0, vmax=255 pins the
            # color scale to the full byte range, so a dim image isn't
            # misleadingly auto-stretched to look brighter than it is.
            im = axes[i].imshow(img, cmap='gray', vmin=0, vmax=255)
            axes[i].set_title(f"{filename}\n({width}x{height})")
            plt.colorbar(im, ax=axes[i], fraction=0.046, pad=0.04)
        else:
            axes[i].text(0.5, 0.5, "File Load Error", ha='center')

    plt.tight_layout()
    plt.show()

if __name__ == "__main__":
    main()
