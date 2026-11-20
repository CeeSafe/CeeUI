#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Writes fonts/colr.ttf: a small TrueType font whose letters A to L are COLR glyphs, one for each kind of paint
COLR version 1 has (and one of version 0's layers), for colr_test.cee. Only the standard library: the tables are
written by hand.

Units per em 1000, ascent 800, descent -200. Glyph 1 is the square (100, 0)-(900, 800); glyphs 2 and 3 its left
and right halves. Palette 0: red, blue, green, cyan, yellow.

    python3 make_colr.py fonts
"""
import os
import struct
import sys

RED, BLUE, GREEN, CYAN, YELLOW = 0, 1, 2, 3, 4
PALETTE = [(255, 0, 0, 255), (0, 0, 255, 255), (0, 255, 0, 255), (0, 255, 255, 255), (255, 255, 0, 255)]
FOREGROUND = 0xFFFF

SQUARE = [(100, 0), (900, 0), (900, 800), (100, 800)]
LEFT = [(100, 0), (500, 0), (500, 800), (100, 800)]
RIGHT = [(500, 0), (900, 0), (900, 800), (500, 800)]
OUTLINES = {1: SQUARE, 2: LEFT, 3: RIGHT}
FIRST_LETTER = 4  # 'A' is glyph 4, 'L' glyph 15
NUM_GLYPHS = 16


def f2(v):
    return struct.pack(">h", round(v * 16384))


def line(extend, stops, variable=False):
    out = struct.pack(">BH", extend, len(stops))
    for offset, index, alpha in stops:
        out += f2(offset) + struct.pack(">H", index) + f2(alpha)
        if variable:
            out += struct.pack(">I", 0xFFFFFFFF)
    return out


RED_TO_BLUE = [(0.0, RED, 1.0), (1.0, BLUE, 1.0)]

# The paint graphs: tuples (kind, ...); children are paints themselves.
PAINTS = {
    # A: a solid fill.
    "A": ("glyph", 1, ("solid", RED, 1.0)),
    # B: a linear gradient left to right (its lines of one colour vertical).
    "B": ("glyph", 1, ("linear", line(0, RED_TO_BLUE), 100, 400, 900, 400, 100, 800)),
    # C: a radial gradient out from the middle.
    "C": ("glyph", 1, ("radial", line(0, RED_TO_BLUE), 500, 400, 0, 500, 400, 400)),
    # D: a sweep, red at 0 degrees (right) to blue at 180 (left).
    "D": ("glyph", 1, ("sweep", line(0, RED_TO_BLUE), 500, 400, 0.0, 1.0)),
    # E: two layers, red left and blue right.
    "E": ("layers", [("glyph", 2, ("solid", RED, 1.0)), ("glyph", 3, ("solid", BLUE, 1.0))]),
    # F: the left half moved right by 400 units, in green.
    "F": ("translate", 400, 0, ("glyph", 2, ("solid", GREEN, 1.0))),
    # G: red, less the left half (Porter-Duff destination-out).
    "G": ("composite", ("glyph", 2, ("solid", BLUE, 1.0)), 8, ("glyph", 1, ("solid", RED, 1.0))),
    # H: B's gradient turned half round its middle: blue at the left, red at the right; a transform under a glyph.
    "H": ("glyph", 1, ("rotate", 1.0, 500, 400, ("linear", line(0, RED_TO_BLUE), 100, 400, 900, 400, 100, 800))),
    # I: a variable solid, at the default instance.
    "I": ("glyph", 1, ("varsolid", GREEN, 1.0)),
    # J: the text's own colour.
    "J": ("glyph", 1, ("solid", FOREGROUND, 1.0)),
    # K: cyan multiplied onto yellow: green.
    "K": ("composite", ("glyph", 1, ("solid", CYAN, 1.0)), 23, ("glyph", 1, ("solid", YELLOW, 1.0))),
}
# L: a version 0 glyph (layers: red left, blue right), in the same table.
V0 = {"L": [(2, RED), (3, BLUE)]}
CLIPS = {"A": (100, 0, 900, 800)}


class Paints:
    """Paint tables laid out one after another: a parent before its children, as Offset24s point forward."""

    def __init__(self, base):
        self.base = base  # where the area starts in the COLR table
        self.data = bytearray()
        self.layers = []  # LayerList entries: absolute positions of paints

    def emit(self, p):
        at = self.base + len(self.data)
        kind = p[0]
        if kind == "solid":
            self.data += struct.pack(">BH", 2, p[1]) + f2(p[2])
        elif kind == "varsolid":
            self.data += struct.pack(">BH", 3, p[1]) + f2(p[2]) + struct.pack(">I", 0xFFFFFFFF)
        elif kind in ("linear", "radial", "sweep"):
            fmt = {"linear": 4, "radial": 6, "sweep": 8}[kind]
            if kind == "linear":
                body = struct.pack(">6h", *p[2:8])
            elif kind == "radial":
                body = struct.pack(">hhHhhH", *p[2:8])
            else:
                body = struct.pack(">hh", p[2], p[3]) + f2(p[4]) + f2(p[5])
            head = len(self.data)
            self.data += bytes(4) + body
            self.data[head] = fmt
            self.patch24(head + 1, at, self.place(p[1]))
        elif kind == "glyph":
            head = len(self.data)
            self.data += struct.pack(">B3xH", 10, p[1])
            self.patch24(head + 1, at, self.emit(p[2]))
        elif kind == "translate":
            head = len(self.data)
            self.data += struct.pack(">B3xhh", 14, p[1], p[2])
            self.patch24(head + 1, at, self.emit(p[3]))
        elif kind == "rotate":
            head = len(self.data)
            self.data += struct.pack(">B3x", 26) + f2(p[1]) + struct.pack(">hh", p[2], p[3])
            self.patch24(head + 1, at, self.emit(p[4]))
        elif kind == "composite":
            head = len(self.data)
            self.data += struct.pack(">B3xB3x", 32, p[2])
            self.patch24(head + 1, at, self.emit(p[1]))
            self.patch24(head + 5, at, self.emit(p[3]))
        elif kind == "layers":
            first = len(self.layers)
            self.layers += [None] * len(p[1])
            self.data += struct.pack(">BBI", 1, len(p[1]), first)
            for k, child in enumerate(p[1]):
                self.layers[first + k] = self.emit(child)
        else:
            raise ValueError(kind)
        return at

    def place(self, raw):
        at = self.base + len(self.data)
        self.data += raw
        return at

    def patch24(self, where, at, target):
        offset = target - at
        assert 0 < offset < 1 << 24
        self.data[where:where + 3] = offset.to_bytes(3, "big")


def glyph_of(letter):
    return FIRST_LETTER + ord(letter) - ord("A")


def colr():
    v0 = sorted((glyph_of(k), v) for k, v in V0.items())
    v1 = sorted((glyph_of(k), v) for k, v in PAINTS.items())
    clips = sorted((glyph_of(k), v) for k, v in CLIPS.items())
    layer_records = [rec for _, layers in v0 for rec in layers]
    base_records_at = 34
    layer_records_at = base_records_at + 6 * len(v0)
    base_list_at = layer_records_at + 4 * len(layer_records)
    layer_count = sum(len(p[1]) for _, p in v1 if p[0] == "layers")
    layer_list_at = base_list_at + 4 + 6 * len(v1)
    clip_list_at = layer_list_at + 4 + 4 * layer_count
    paints_at = clip_list_at + 5 + 16 * len(clips)
    area = Paints(paints_at)
    roots = [(g, area.emit(p)) for g, p in v1]
    out = struct.pack(">HHIIHIIIII", 1, len(v0), base_records_at, layer_records_at, len(layer_records),
                      base_list_at, layer_list_at, clip_list_at, 0, 0)
    first = 0
    for g, layers in v0:
        out += struct.pack(">HHH", g, first, len(layers))
        first += len(layers)
    for g, index in layer_records:
        out += struct.pack(">HH", g, index)
    out += struct.pack(">I", len(roots))
    for g, at in roots:
        out += struct.pack(">HI", g, at - base_list_at)
    out += struct.pack(">I", len(area.layers))
    for at in area.layers:
        out += struct.pack(">I", at - layer_list_at)
    boxes = clip_list_at + 5 + 7 * len(clips)
    out += struct.pack(">BI", 1, len(clips))
    for k, (g, _) in enumerate(clips):
        out += struct.pack(">HH", g, g) + (boxes + 9 * k - clip_list_at).to_bytes(3, "big")
    for _, box in clips:
        out += struct.pack(">B4h", 1, *box)
    out += bytes(paints_at - (len(out)))
    assert len(out) == paints_at
    return out + bytes(area.data)


def cpal():
    out = struct.pack(">HHHHIH", 0, len(PALETTE), 1, len(PALETTE), 14, 0)
    for r, g, b, a in PALETTE:
        out += bytes([b, g, r, a])
    return out


def glyf_and_loca():
    glyf = bytearray()
    loca = []
    for g in range(NUM_GLYPHS):
        loca.append(len(glyf))
        pts = OUTLINES.get(g)
        if pts is None:
            continue
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        glyf += struct.pack(">hhhhh", 1, min(xs), min(ys), max(xs), max(ys))
        glyf += struct.pack(">HH", len(pts) - 1, 0) + bytes([1] * len(pts))
        px = py = 0
        for x, _ in pts:
            glyf += struct.pack(">h", x - px)
            px = x
        for _, y in pts:
            glyf += struct.pack(">h", y - py)
            py = y
        while len(glyf) % 4:
            glyf += b"\0"
    loca.append(len(glyf))
    return bytes(glyf), b"".join(struct.pack(">I", o) for o in loca)


def cmap():
    first, last = ord("A"), ord("L")
    delta = (FIRST_LETTER - first) & 0xFFFF
    seg = 2
    sub = struct.pack(">HHHHHHH", 4, 16 + 8 * seg, 0, seg * 2, 4, 1, 0)
    sub += struct.pack(">HH", last, 0xFFFF) + b"\0\0" + struct.pack(">HH", first, 0xFFFF)
    sub += struct.pack(">HH", delta, 1) + struct.pack(">HH", 0, 0)
    return struct.pack(">HHHHI", 0, 1, 3, 1, 12) + sub


def font():
    glyf, loca = glyf_and_loca()
    tables = {
        b"head": struct.pack(">IIIIHHQQhhhhHHhhh", 0x00010000, 0x00010000, 0, 0x5F0F3CF5, 0, 1000, 0, 0,
                             100, 0, 900, 800, 0, 8, 2, 1, 0),
        b"hhea": struct.pack(">IhhhHhhhhhh4hhH", 0x00010000, 800, -200, 0, 1000, 0, 0, 900, 1, 0, 0,
                             0, 0, 0, 0, 0, NUM_GLYPHS),
        b"maxp": struct.pack(">IH13H", 0x00010000, NUM_GLYPHS, 4, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0),
        b"hmtx": b"".join(struct.pack(">Hh", 1000, 0) for _ in range(NUM_GLYPHS)),
        b"cmap": cmap(),
        b"loca": loca,
        b"glyf": glyf,
        b"post": struct.pack(">IIhhIIIII", 0x00030000, 0, 0, 0, 0, 0, 0, 0, 0),
        b"COLR": colr(),
        b"CPAL": cpal(),
    }
    tags = sorted(tables)
    n = len(tags)
    shift = 1
    while shift * 2 <= n:
        shift *= 2
    out = struct.pack(">IHHHH", 0x00010000, n, shift * 16, shift.bit_length() - 1, n * 16 - shift * 16)
    at = 12 + 16 * n
    body = b""
    for tag in tags:
        data = tables[tag]
        out += struct.pack(">4sIII", tag, 0, at + len(body), len(data))
        body += data + b"\0" * (-len(data) % 4)
    return out + body


if __name__ == "__main__":
    folder = sys.argv[1] if len(sys.argv) > 1 else "fonts"
    os.makedirs(folder, exist_ok=True)
    with open(os.path.join(folder, "colr.ttf"), "wb") as f:
        f.write(font())
