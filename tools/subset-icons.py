#!/usr/bin/env python3
# Régénère assets/fonts/FontAwesome-Solid.ttf (sous-ensemble) et src/ui/icons.h.
# usage : python3 tools/subset-icons.py fa-solid-900.ttf   (Font Awesome Free 6.5.2)
# Prérequis : pip install fonttools
import sys
from fontTools.ttLib import TTFont
from fontTools import subset

NAMES = """play gear circle-play download door-open power-off music clock star magnifying-glass arrow-left shuffle
wand-magic-sparkles circle chevron-left chevron-right chevron-down chevron-up house bars xmark check pause rotate-right
rotate-left bolt eye eye-slash shield-halved robot heart trophy user bell circle-notch list sliders volume-high display
gamepad keyboard compass image lightbulb circle-xmark circle-check compact-disc layer-group forward backward floppy-disk
trash info wifi hourglass-half up-right-from-square gauge-high skull feather crosshairs caret-left caret-right
circle-info computer-mouse hand-pointer bullseye fire chart-simple headphones plus minus globe folder arrow-down arrow-up
pen spinner filter sort angles-right circle-question circle-down square stopwatch face-smile""".split()

f = TTFont(sys.argv[1])
rev = {}
for cp, g in f.getBestCmap().items():
    rev.setdefault(g, cp)
found = {n: rev[n] for n in NAMES if n in rev}
opts = subset.Options()
opts.notdef_outline = True
sub = subset.Subsetter(opts)
sub.populate(unicodes=sorted(set(found.values())))
sub.subset(f)
f.save('ps5/assets/fonts/FontAwesome-Solid.ttf')
with open('src/ui/icons.h', 'w') as o:
    o.write('// SPDX-License-Identifier: GPL-3.0-or-later\n// Points de code des icônes Font Awesome Free (police OFL, icônes CC BY 4.0),\n'
            '// sous-ensemble embarqué dans assets/fonts/FontAwesome-Solid.ttf.\n// Généré par tools/subset-icons.py.\n'
            '#pragma once\n\nnamespace fa {\n')
    for n in NAMES:
        if n in found:
            esc = ''.join('\\x%02x' % b for b in chr(found[n]).encode('utf-8'))
            o.write('constexpr const char *%s = "%s";\n' % (n.replace('-', '_').replace('power_off', 'powerOff'), esc))
    o.write('}  // namespace fa\n')
