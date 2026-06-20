"""
view_pipeline.py — Display every stage of the Canny pipeline side by side.

Usage:
    python3 Tool/view_pipeline.py <stages_dir> <width> <height>

Example:
    python3 Tool/view_pipeline.py Tool/stages 256 256

Expects the directory to contain the files produced by
Test/Test_pipeline_stages.cpp:
    01_input.raw, 02_blurred.raw, 03_gx.raw, 04_gy.raw,
    05_magnitude.raw, 06_direction.raw, 07_nms.raw, 08_hysteresis.raw

Any stage file that is missing is skipped (its subplot shows a notice)
so this also works if you've only generated some of the stages.
"""

import os
import sys
import numpy as np
import matplotlib.pyplot as plt

# (filename, display title, is_rgb) in pipeline order.
# 06_direction.raw is color-coded (3 bytes/pixel: R,G,B interleaved):
#   red = 0deg, green = 45deg, blue = 90deg, yellow = 135deg
STAGES = [
    ("01_input.raw",       "1. Input",                  False),
    ("02_blurred.raw",     "2. Gaussian Blur",          False),
    ("03_gx.raw",          "3. Sobel Gx",                False),
    ("04_gy.raw",          "4. Sobel Gy",                False),
    ("05_magnitude.raw",   "5. Magnitude",               False),
    ("06_direction.raw",   "6. Direction (color-coded)", True),
    ("07_nms.raw",         "7. Non-Max Suppression",     False),
    ("08_hysteresis.raw",  "8. Hysteresis (final edges)",False),
]

DIRECTION_LEGEND = [
    ("0 deg",   "red"),
    ("45 deg",  "green"),
    ("90 deg",  "blue"),
    ("135 deg", "yellow"),
]


def load_raw(filename, width, height):
    if not os.path.exists(filename):
        return None
    data = np.fromfile(filename, dtype=np.uint8)
    try:
        return data.reshape((height, width))
    except ValueError:
        print(f"Warning: '{filename}' size {data.size} doesn't match {width}x{height}.")
        return None


def load_raw_rgb(filename, width, height):
    """Loads an interleaved RGB raw file (3 bytes/pixel: R,G,B,R,G,B,...)."""
    if not os.path.exists(filename):
        return None
    data = np.fromfile(filename, dtype=np.uint8)
    expected = width * height * 3
    if data.size != expected:
        print(f"Warning: '{filename}' size {data.size} doesn't match "
              f"{width}x{height}x3={expected} (RGB).")
        return None
    return data.reshape((height, width, 3))


def main():
    if len(sys.argv) < 4:
        print("Usage: python3 view_pipeline.py <stages_dir> <width> <height>")
        print("Example: python3 view_pipeline.py Tool/stages 256 256")
        return

    stages_dir = sys.argv[1]
    width = int(sys.argv[2])
    height = int(sys.argv[3])

    cols = 4
    rows = 2
    fig, axes = plt.subplots(rows, cols, figsize=(5 * cols, 5 * rows))
    axes = axes.flatten()

    for ax, (fname, title, is_rgb) in zip(axes, STAGES):
        full_path = os.path.join(stages_dir, fname)
        if is_rgb:
            img = load_raw_rgb(full_path, width, height)
            if img is not None:
                ax.imshow(img)  # already 0-255 RGB, no cmap/vmin/vmax needed
                # Small color-key legend so red/green/blue/yellow map to
                # an actual angle instead of making the viewer guess.
                legend_text = "  ".join(f"{name}" for name, _ in DIRECTION_LEGEND)
                for j, (name, color) in enumerate(DIRECTION_LEGEND):
                    ax.text(0.02, -0.06 - 0.05 * j, f"\u25A0 {name}",
                            color=color, transform=ax.transAxes,
                            fontsize=9, fontweight='bold',
                            va='top', ha='left')
            else:
                ax.text(0.5, 0.5, "missing", ha='center', va='center')
        else:
            img = load_raw(full_path, width, height)
            if img is not None:
                im = ax.imshow(img, cmap='gray', vmin=0, vmax=255)
                plt.colorbar(im, ax=ax, fraction=0.046, pad=0.04)
            else:
                ax.text(0.5, 0.5, "missing", ha='center', va='center')
        ax.set_title(title)
        ax.axis('off')

    fig.suptitle(f"Canny Pipeline Stages — {stages_dir} ({width}x{height})", fontsize=14)
    plt.tight_layout(rect=[0, 0, 1, 0.96])
    fig.subplots_adjust(hspace=0.4)
    plt.show()


if __name__ == "__main__":
    main()