#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Génère ps5/sce_sys/icon0.png (512x512) : visuel original, sans logo officiel."""
import sys
from PIL import Image, ImageDraw

S = 4  # sur-échantillonnage pour l'anticrénelage
img = Image.new("RGB", (512 * S, 512 * S), (22, 20, 34))
d = ImageDraw.Draw(img)
c = 256 * S
for r, w, col in ((200, 14, (255, 102, 170)), (120, 0, (255, 102, 170)), (120, 12, (255, 255, 255))):
    box = (c - r * S, c - r * S, c + r * S, c + r * S)
    if w == 0:
        d.ellipse(box, fill=col)
    else:
        d.ellipse(box, outline=col, width=w * S)
d.ellipse((c - 26 * S, c - 26 * S, c + 26 * S, c + 26 * S), fill=(255, 255, 255))
img.resize((512, 512), Image.LANCZOS).save(sys.argv[1] if len(sys.argv) > 1 else "ps5/sce_sys/icon0.png")
