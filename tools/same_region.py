"""Exit 0 if two screenshots match in a region (mean abs difference below a
threshold), else 1. Used by scripted sessions to detect where a menu is.

usage: same_region.py A.png B.png x0 y0 x1 y1 [threshold]
Coordinates are fractions of the image (0..1).
"""
import sys

from PIL import Image, ImageChops, ImageStat

a, b = Image.open(sys.argv[1]).convert("L"), Image.open(sys.argv[2]).convert("L")
x0, y0, x1, y1 = (float(v) for v in sys.argv[3:7])
threshold = float(sys.argv[7]) if len(sys.argv) > 7 else 6.0
box = lambda im: (int(x0 * im.width), int(y0 * im.height), int(x1 * im.width), int(y1 * im.height))
ra, rb = a.crop(box(a)), b.crop(box(b)).resize(a.crop(box(a)).size)
diff = ImageStat.Stat(ImageChops.difference(ra, rb)).mean[0]
print(f"{diff:.2f}")
sys.exit(0 if diff < threshold else 1)
