#!/usr/bin/env python3
"""
Usage:
  python3 Tool/save_pipeline_png.py file1.raw file2.raw file3.raw output.png \
      [title1] [title2] [title3]

If titles are omitted, defaults to: Original / Blurred (Gaussian) / Magnitude (Sobel)
Image size is always assumed 512x512.
"""
import sys
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def load(p):
    return np.fromfile(p, dtype=np.uint8).reshape((512, 512))

if len(sys.argv) < 5:
    print("Usage: save_pipeline_png.py file1 file2 file3 output.png [title1] [title2] [title3]")
    sys.exit(1)

input_files = sys.argv[1:4]
output_png  = sys.argv[4]

default_titles = ['Original', 'Blurred (Gaussian)', 'Magnitude (Sobel)']
titles = sys.argv[5:8] if len(sys.argv) >= 8 else default_titles

fig, axes = plt.subplots(1, 3, figsize=(15, 5))
for ax, path, title in zip(axes, input_files, titles):
    ax.imshow(load(path), cmap='gray', vmin=0, vmax=255)
    ax.set_title(title, fontsize=14)
    ax.axis('off')

plt.tight_layout()
plt.savefig(output_png, dpi=150)
print(f'Saved: {output_png}')