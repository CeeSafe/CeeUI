#!/usr/bin/env python3
"""The largest functions of a binary, by the distance from each code symbol to the next (nm -n), for finding where a
binary's size went. Works on Mach-O and ELF, whose nm (Apple's or GNU's) prints addresses but not always sizes.

    python3 tools/largest_functions.py ceeui-hosts/headless/out/headless [count]
"""

import subprocess
import sys


def main():
    binary = sys.argv[1]
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 25
    listing = subprocess.run(["nm", "-n", binary], capture_output=True, text=True, check=True).stdout
    symbols = []
    for line in listing.splitlines():
        parts = line.split()
        if len(parts) != 3:
            continue
        address, kind, name = parts
        try:
            symbols.append((int(address, 16), kind, name))
        except ValueError:
            continue
    sizes = []
    for k in range(len(symbols) - 1):
        address, kind, name = symbols[k]
        if kind.lower() == "t":
            sizes.append((symbols[k + 1][0] - address, name))
    sizes.sort(reverse=True)
    total = sum(size for size, _ in sizes)
    print(f"{len(sizes)} functions, {total} bytes of code; the largest:")
    for size, name in sizes[:count]:
        print(f"{size:>9}  {name}")


if __name__ == "__main__":
    main()
