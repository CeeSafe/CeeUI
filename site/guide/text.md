# Text

This page covers showing text: labels and their look, writing numbers into text, rich labels, fonts and fallbacks,
wrapping and truncation, and which scripts ClayWind shapes. Editing text is on [Editing text](./editing).

## Labels

`ui.label` shows a string. Its second argument, a `Text`, is the label's look.

```ceesafe frame
ui.label("Saved", Text.of());
ui.label("3 files changed", Text.of().size(14.0).color(SLATE_400));
```

A label is a leaf: it has no `ui.close()`. It is as wide as its text, or as its container when the text wraps.

`ui.labelBytes` takes UTF-8 bytes (`Slice<u8>`) where `ui.label` takes a `StringView`. Use it for text the app
builds or keeps in a buffer.

```ceesafe frame
let name = "report.pdf".bytes();
ui.labelBytes(name, Text.of().bold());
```

## The look of a label

`Text.of()` is the default look: the regular font at 18 points, in a light grey, wrapping at words. Each method
returns a changed copy, so a look can be kept and varied.

```ceesafe frame
let body = Text.of().size(15.0).color(SLATE_300);
ui.label("Total", body);
ui.label("42.00", body.bold().color(WHITE));
ui.label("id 7f3a", body.mono());
```

| Method | What it sets |
|---|---|
| `size(points)` | The font size. `0.0`, the default, is the font preset's size |
| `color(rgba)` | The colour, as `0xRRGGBBAA` or a palette constant such as `SLATE_100` |
| `bold()` | The bold face (`FONT_BOLD`) |
| `mono()` | The monospaced face (`FONT_MONO`) |
| `font(id)` | A font id (`FONT_18`, `FONT_MONO`, ...) or a size preset (`TEXT_LG`, ...) |
| `tracking(points)` | Space added between letters |
| `leading(points)` | The line height. `0.0`, the default, is the font's own |
| `align(TextAlign)` | Where the lines sit in the label's box |
| `wrap(TextWrap)` | How lines break |
| `clamp(lines)` | The most lines shown; the last ends in `...` when text is left out |

`mono()` wins over `bold()`, and both win over `font(id)`.

### Alignment

```ceesafe frame
ui.open(Style.column().w(Size.fixed(240.0)));
ui.label("Centered over the column", Text.of().align(TextAlign.Center));
ui.close();
```

| `TextAlign` | Where the lines sit |
|---|---|
| `Start` | The side the paragraph starts from: left, or right for a right-to-left paragraph. The default |
| `Left` | The left |
| `Center` | The middle |
| `Right` | The right |

### Tracking and line height

```ceesafe frame
ui.label("PRO", Text.of().size(12.0).tracking(2.0));
ui.label("Two lines of text set a little further apart than the font would.", Text.of().size(16.0).leading(26.0));
```

## Numbers in text

A frame allocates nothing. To show a number, write it into an array on the stack with a `TextWriter`, then pass
what was written to `ui.labelBytes`.

```ceesafe frame
let unread: u64 = 12;
var buffer = [0u8; 32];
var w = TextWriter.create(mut buffer[..]);
w.decimal(unread);
w.text(" unread");
ui.labelBytes(w.written(), Text.of());
```

| Call | What it writes |
|---|---|
| `w.text("...")` | A string |
| `w.byte('%')` | One byte |
| `w.decimal(n)` | A `u64` in decimal |
| `w.signed(n)` | An `i64` in decimal, with its sign |
| `Format.fixed(mut w, value, decimals)` | An `f32` with that many digits after the point (0 to 6) |
| `Format.padded(mut w, value, width, fill)` | An `i64` padded on the left to at least `width` characters |

```ceesafe frame
let seconds: i64 = 7;
let ratio: f32 = 0.8125;
var buffer = [0u8; 32];
var w = TextWriter.create(mut buffer[..]);
w.text("0:");
Format.padded(mut w, seconds, 2, '0');
w.text("  ");
Format.fixed(mut w, ratio * 100.0, 1);
w.byte('%');
ui.labelBytes(w.written(), Text.of().mono());
```

This label reads `0:07  81.3%`. Other integer types are converted first: `w.decimal(count as u64)`.

## Rich labels

`ui.richLabel` shows one paragraph in several looks. Each `TextStyle` is a run: a number of bytes and their look.
The runs follow one another from the start of the text.

```ceesafe frame
let text = "Press Ctrl-S to save.";
let plain = Text.of().color(SLATE_300);
let styles = [
    TextStyle.of(6, plain),
    TextStyle.of(6, plain.mono().color(AMBER_400)),
    TextStyle.of(9, plain),
];
ui.richLabel(text.bytes(), plain, styles[..]);
```

- A run gives its bytes their font, size, colour and letter spacing.
- Alignment, wrapping, line height and line clamp come from the second argument, the paragraph's look.
- Bytes past the last run are drawn in the paragraph's look. A rich label has at most 64 runs.
- The runs wrap together as one paragraph and share a baseline on each line.

A run's length is in bytes of UTF-8, not characters.

## Wrapping

A label wraps at words by default: when its container is narrower than the text, lines break to fit.

```ceesafe frame
ui.open(Style.column().w(Size.fixed(200.0)));
ui.label("A long sentence breaks into as many lines as the column's width needs.", Text.of());
ui.label("This one stays on one line and runs past the column.", Text.of().wrap(TextWrap.None));
ui.close();
```

| `TextWrap` | How lines break |
|---|---|
| `Words` | Where a line may break (UAX #14), to fit the label's width. The default |
| `Newlines` | Only at the text's own newlines |
| `None` | The text keeps its own width and is not broken to fit |

Lines break at the opportunities of Unicode's line breaking algorithm. CJK text breaks between ideographs, and a
line may break after a hyphen.

## Truncation

`clamp(n)` keeps at most `n` lines. When text is left out, the last line is cut to fit and ends in `...`.
`clamp(1)` truncates to one line.

```ceesafe frame
ui.open(Style.column().w(Size.fixed(200.0)));
ui.label("A file name that is far too long for its column.txt", Text.of().clamp(1));
ui.label("A description that may run on for several lines but shows two of them at most.", Text.of().clamp(2));
ui.close();
```

The cut falls between grapheme clusters, never inside a character. Clamping applies to text that wraps, so leave
`wrap` at `TextWrap.Words`.

## Fonts

ClayWind reads and draws TrueType and OpenType (CFF) fonts itself, from single fonts and collections (`.ttc`). The
host loads fonts into the `Raster` before the first frame, each under a font id. A label names a font by that id.

| Font id | What a host loads there | Size when `size` is not set |
|---|---|---|
| `FONT_18` | The regular face. Also `TEXT_SM` and `TEXT_BASE` | 18 |
| `FONT_20` | Also `TEXT_LG` | 20 |
| `FONT_22` | Also `TEXT_XL` | 22 |
| `FONT_24` | Also `TEXT_2XL` | 24 |
| `FONT_BOLD` | The bold face, which `bold()` uses | 18 |
| `FONT_MONO` | The monospaced face, which `mono()` uses | 18 |

A font id with nothing loaded uses the font under id 0, `FONT_18`. So a host that loads three faces (regular, bold
and monospaced) serves every id, and `Text.of().font(TEXT_XL)` is the regular face at 22 points.

```ceesafe frame
ui.label("Heading", Text.of().font(TEXT_2XL));
ui.label("let x = 1;", Text.of().font(FONT_MONO).size(14.0));
```

### Loading fonts

`Raster.loadFont` takes a font id and the bytes of a font file, and copies them. It returns false when the font
does not parse or does not fit the Raster's font memory.

```ceesafe
import claywind;

public fn loadFaces(raster: borrow mut Raster, regular: Slice<u8>, bold: Slice<u8>, mono: Slice<u8>) -> bool {
    return raster.loadFont(FONT_18, regular) && raster.loadFont(FONT_BOLD, bold) &&
        raster.loadFont(FONT_MONO, mono);
}
```

`raster.loadFontFace(id, bytes, face)` loads one face of a collection.

A desktop host made from the template does not call these itself. `claywind-desktop`'s `loadSystemFonts` finds a
regular, a bold and a monospaced face among the system's fonts and loads them:

```ceesafe skip
    if (!loadSystemFonts(mut raster)) {
        return 1; // no fonts to draw text with
    }
```

[A desktop host](./desktop) and [The web](./web) say how each host gets its fonts.

### Fallback fonts

A character the run's font lacks is taken from a fallback chain: a list of font ids, tried in order. With a CJK
font in the chain, Chinese, Japanese and Korean draw inside a Latin run.

```ceesafe
import claywind;

const FONT_CJK: u32 = 6;
const FONT_EMOJI: u32 = 7;

public fn loadFallbacks(raster: borrow mut Raster, cjk: Slice<u8>, emoji: Slice<u8>) -> bool {
    if (!raster.loadFont(FONT_CJK, cjk) || !raster.loadFont(FONT_EMOJI, emoji)) {
        return false;
    }
    let chain = [FONT_CJK, FONT_EMOJI];
    raster.setFallbacks(chain[..]);
    return true;
}
```

The chain holds up to 24 fonts. A character no font has is drawn as the missing-glyph box of the run's font.

### System fonts as fallbacks

The `claywind-fonts` package finds fallbacks among the operating system's own fonts, so a host need not ship
them. After a frame is laid out, `SystemFonts.supplyFrame` looks at the frame's text, loads a system face for each
character the loaded fonts lack, and adds it to the fallback chain. It returns how many faces it loaded; when that
is not zero, the host lays the frame out again.

```ceesafe skip
    var systemFonts = SystemFonts.create();
    loop {
        // ...events, the frame, layout...
        if (systemFonts.supplyFrame(mut raster, pane.ui) > 0) {
            // a font was loaded: lay the frame out again with it
        }
        // ...draw...
    }
```

The catalog of system fonts is built the first time a character is missing, not at startup.
`SystemFonts.supply(mut raster, utf8, fontId)` does the same for one text. `claywind-fonts` reads files, which
the `claywind` package itself does not, so it is for hosts with a file system.

## What text ClayWind handles

ClayWind shapes, breaks and draws text itself, on every platform alike. It does not call the system's text
engine.

| Area | What is handled |
|---|---|
| Fonts | TrueType and OpenType (CFF), single fonts and collections (`.ttc`), CID-keyed CJK fonts included |
| OpenType shaping | The font's GSUB and GPOS lookups for the run's script: ligatures, contextual alternates, kerning, combining marks placed by the font's anchors |
| Arabic | Joining forms (isolated, final, medial, initial), ZWJ and ZWNJ honoured, and cursive attachment as Nastaliq uses |
| Indic scripts | Devanagari, Bengali, Gurmukhi, Gujarati, Oriya, Tamil, Telugu, Kannada and Malayalam: syllables, reordering, half, below and post-base forms, conjuncts, two-part vowels |
| Bidirectional text | The Unicode Bidirectional Algorithm (UAX #9) on each line: right-to-left text inside a left-to-right line, mirrored brackets |
| Colour emoji | Bitmap emoji fonts (sbix, CBDT) and COLR fonts, versions 0 and 1; emoji sequences (ZWJ families, skin tones, flags, keycaps) |
| Line breaking | UAX #14 break opportunities |
| Grapheme clusters | UAX #29: the caret, Backspace and line clamping move a whole cluster (an accented letter, a flag, an emoji sequence) |
| Words | UAX #29 word boundaries, used by word selection and word movement |

The Indic shaper is ported from HarfBuzz's, and the tests compare its glyphs and positions with HarfBuzz's own.
Line breaking, grapheme clusters, words and bidirectional text pass Unicode 16's test files.

A paragraph whose first strong character is right to left starts at the right, with `TextAlign.Start`:

```ceesafe frame
ui.open(Style.column().w(Size.fixed(240.0)));
ui.label("שלום עולם", Text.of());
ui.label("Hello, world", Text.of());
ui.close();
```

The first label sits at the column's right edge and the second at its left. Drawing these scripts needs a font
that has them, loaded or in the fallback chain.
