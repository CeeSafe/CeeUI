#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Writes shaping cases for claywind-tests' shapingMatchesHarfBuzz: text in a font, and HarfBuzz's glyphs for it
(hb-shape), for the test to shape the same text and compare glyph for glyph.

    python3 tools/shaping/hb_cases.py OUT_FILE [COUNT_PER_FONT] [SEED]

Each line: font path, face index, units per em, the text's code points (hex, space-separated), and HarfBuzz's
glyphs as id:x:y;... (x the pen plus x offset, y the y offset, in font units; joiners HarfBuzz hides as a
zero-width space are left out). Fonts missing on this machine are skipped. HB_CASES_FONTS adds fonts, as
"path,face,first-code-point" entries separated by semicolons (say, a Noto font of a script no system font has).
"""

import json
import os
import random
import unicodedata
import struct
import subprocess
import sys

# (font, face, script's first code point): fonts with GSUB and no morx, which hb-shape shapes with OpenType.
FONTS = [
    ("/System/Library/Fonts/Kohinoor.ttc", 0, 0x900),
    ("/System/Library/Fonts/Supplemental/ITFDevanagari.ttc", 0, 0x900),
    ("/System/Library/Fonts/KohinoorBangla.ttc", 0, 0x980),
    ("/System/Library/Fonts/MuktaMahee.ttc", 0, 0xA00),
    ("/System/Library/Fonts/KohinoorGujarati.ttc", 0, 0xA80),
    ("/System/Library/Fonts/NotoSansOriya.ttc", 0, 0xB00),
    ("/System/Library/Fonts/Supplemental/Tamil Sangam MN.ttc", 0, 0xB80),
    ("/System/Library/Fonts/KohinoorTelugu.ttc", 0, 0xC00),
    ("/System/Library/Fonts/NotoSansKannada.ttc", 0, 0xC80),
]

# A few real words in each script, as a base of ordinary text.
WORDS = {
    0x900: ["हिन्दी", "क्षत्रिय", "र्म", "कर्म", "द्ध", "श्री", "प्रेम", "विद्यालय", "अर्थ", "क्‍ष", "क्‌ष", "त्र", "ज्ञान", "स्त्री", "हृदय"],
    0x980: ["বাংলা", "ক্ষমা", "র্ক", "কর্ম", "স্ত্রী", "জ্ঞান", "কৌশল", "বোন", "ধর্ম"],
    0xA00: ["ਪੰਜਾਬੀ", "ਗੁਰਮੁਖੀ", "ਪ੍ਰੇਮ", "ਸ੍ਵ", "ਕਿਤਾਬ"],
    0xA80: ["ગુજરાતી", "ક્ષમા", "ધર્મ", "પ્રેમ", "શ્રી"],
    0xB00: ["ଓଡ଼ିଆ", "କ୍ଷମା", "ଧର୍ମ", "ପ୍ରେମ", "କୋଣ"],
    0xB80: ["தமிழ்", "கொண்டு", "க்ஷ", "ஸ்ரீ", "கோவில்", "பௌத்தம்"],
    0xC00: ["తెలుగు", "క్ష", "ధర్మం", "ప్రేమ", "కొండ", "ర్క"],
    0xC80: ["ಕನ್ನಡ", "ಕ್ಷಮೆ", "ಧರ್ಮ", "ಪ್ರೀತಿ", "ಕೊಂಡ", "ರ್ಕ"],
    0xD00: ["മലയാളം", "ക്ഷമ", "ധർമ്മം", "പ്രേമം", "കൊണ്ട്", "ര്ക", "ന്റെ", "ക്ക"],
}

# Offsets within a script's block: consonants, dependent vowels, the virama, the nukta, signs, independent vowels.
CONSONANTS = list(range(0x15, 0x39))
MATRAS = list(range(0x3E, 0x4D)) + [0x62, 0x63]
SIGNS = [0x01, 0x02, 0x03]
VOWELS = list(range(0x05, 0x14))


def syllable(rng, base):
    # A consonant cluster (C (H C)*), an optional nukta, a matra or a final virama, a sign; sometimes a joiner.
    parts = []
    if rng.random() < 0.15:
        parts += [base + 0x30, base + 0x4D]  # Ra + virama: a reph
    parts.append(base + rng.choice(CONSONANTS))
    for _ in range(rng.choice([0, 0, 1, 1, 2])):
        if rng.random() < 0.1:
            parts.append(rng.choice([0x200C, 0x200D]))
        parts += [base + 0x4D, base + rng.choice(CONSONANTS)]
    if rng.random() < 0.15:
        parts.append(base + 0x3C)
    r = rng.random()
    if r < 0.6:
        parts.append(base + rng.choice(MATRAS))
    elif r < 0.7:
        parts.append(base + 0x4D)
    if rng.random() < 0.2:
        parts.append(base + rng.choice(SIGNS))
    return parts


def assigned(cps):
    # Real text has no unassigned characters (the generator's ranges have a few holes).
    return all(c in (0x200C, 0x200D) or unicodedata.category(chr(c)) != "Cn" for c in cps)


def text_for(rng, base):
    if rng.random() < 0.1:
        return [base + rng.choice(VOWELS)] + ([base + 0x02] if rng.random() < 0.5 else [])
    out = []
    for _ in range(rng.choice([1, 1, 2, 3])):
        out += syllable(rng, base)
    return out


def units_per_em(path, face):
    d = open(path, "rb").read(1 << 16)
    off = 0
    if d[:4] == b"ttcf":
        off = struct.unpack_from(">I", d, 12 + face * 4)[0]
    n = struct.unpack_from(">H", d, off + 4)[0]
    for i in range(n):
        tag, _, o, _ = struct.unpack_from(">4sIII", d, off + 12 + i * 16)
        if tag == b"head":
            f = open(path, "rb")
            f.seek(o + 18)
            return struct.unpack(">H", f.read(2))[0]
    return 1000


def hb(path, face, text):
    out = subprocess.run(["hb-shape", "--no-glyph-names", "--output-format=json", f"--face-index={face}",
                          "--font-funcs=ot", path, text], capture_output=True, text=True).stdout
    return json.loads(out)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 200
    rng = random.Random(int(sys.argv[3]) if len(sys.argv) > 3 else 1)
    fonts = list(FONTS)
    for entry in filter(None, os.environ.get("HB_CASES_FONTS", "").split(";")):
        path, face, base = entry.split(",")
        fonts.append((path, int(face), int(base, 16)))
    lines = []
    for path, face, base in fonts:
        try:
            upem = units_per_em(path, face)
        except OSError:
            continue
        space = hb(path, face, " ")[0]["g"]
        texts = [[ord(c) for c in w] for w in WORDS.get(base, [])]
        while len(texts) < count + len(WORDS.get(base, [])):
            cps = text_for(rng, base)
            if assigned(cps):
                texts.append(cps)
        for cps in texts:
            text = "".join(chr(c) for c in cps)
            glyphs = []
            pen = 0
            for g in hb(path, face, text):
                if not (g["g"] == space and g["ax"] == 0 and g["dx"] == 0):
                    glyphs.append(f"{g['g']}:{pen + g['dx']}:{g['dy']}")
                pen += g["ax"]
            lines.append("\t".join([path, str(face), str(upem), " ".join(f"{c:X}" for c in cps), ";".join(glyphs)]))
    open(sys.argv[1], "w", encoding="utf-8").write("\n".join(lines) + "\n")
    print(f"hb_cases.py: {len(lines)} cases -> {sys.argv[1]}")


if __name__ == "__main__":
    main()
