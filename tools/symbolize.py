#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Décode un rapport de plantage de rhythm-ps5.

Les lignes "!!! ... ancre+1234" du log sont des décalages par rapport à la
fonction rhythm_crash_anchor. Avec l'ELF de symboles de LA MÊME build
(build/ps5/dist/rhythm-ps5-symbols.elf, ou build/pc/rhythm-ps5 pour le PC),
ce script les traduit en fonction + fichier:ligne via llvm-symbolizer.

    python3 tools/symbolize.py build/ps5/dist/rhythm-ps5-symbols.elf logs/2026-10-13_20-00-00.log
"""
import re
import shutil
import subprocess
import sys


def tool(*names):
    for name in names:
        path = shutil.which(name)
        if path:
            return path
    sys.exit(f"outil introuvable : {' ou '.join(names)}")


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    elf, log = sys.argv[1], sys.argv[2]
    nm = tool("llvm-nm-18", "llvm-nm", "nm")
    symbolizer = tool("llvm-symbolizer-18", "llvm-symbolizer")
    anchor = None
    for line in subprocess.run([nm, elf], capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == "rhythm_crash_anchor":
            anchor = int(parts[0], 16)
    if anchor is None:
        sys.exit("rhythm_crash_anchor absent de l'ELF : mauvais fichier ?")
    pattern = re.compile(r"ancre([+-]\d+)")
    with open(log, encoding="utf-8", errors="replace") as stream:
        for line in stream:
            if not line.startswith("!!!"):
                continue
            match = pattern.search(line)
            if not match:
                print(line.rstrip())
                continue
            address = anchor + int(match.group(1))
            # Pour une adresse de retour, l'instruction fautive est juste avant.
            query = address if "rip=" in line else address - 1
            out = subprocess.run([symbolizer, "--obj", elf, "--functions=linkage", "--demangle", hex(query)],
                                 capture_output=True, text=True).stdout.split("\n")
            where = " | ".join(x for x in out if x.strip())
            print(f"{line.rstrip()}   ->  {where}")


if __name__ == "__main__":
    main()
