# Drawing: paths, images, effects

This page covers what an element can draw besides a background and text: vector paths, icons, images, gradients,
shadows, filters, opacity, blend modes and transforms. It ends with how frames reach the screen: the pixel ratio,
incremental rendering, and the GPU backends.

Everything here is an option of `Style`, or one call on the `Ui`. [Styles](./styles) covers colors, borders, corners
and the state variants.

## Vector paths

`PathBuilder` writes a path into a byte buffer that you own. `ui.path` draws those bytes in an element's box,
clipped to it.

```ceesafe frame
var bytes = [0u8; 128];
var p = PathBuilder.over(mut bytes[..]);
p.moveTo(24.0, 4.0);
p.lineTo(44.0, 44.0);
p.lineTo(4.0, 44.0);
p.close();
ui.path(
    Style.box().w(Size.fixed(48.0)).h(Size.fixed(48.0)),
    p.bytes(),
    PathLook.fill(Color.hex(AMBER_500)),
);
```

Coordinates are in points from the top-left corner of the element. The element is laid out like any other, so the
style gives it its size.

| `PathBuilder` call | What it adds |
|---|---|
| `moveTo(x, y)` | Starts a subpath |
| `lineTo(x, y)` | A straight line |
| `quadTo(cx, cy, x, y)` | A quadratic Bézier curve |
| `cubicTo(c1x, c1y, c2x, c2y, x, y)` | A cubic Bézier curve |
| `close()` | A line back to the start of the subpath |
| `rect(x, y, w, h)` | A rectangle, as a closed subpath |
| `circle(cx, cy, radius)` | A circle, as a closed subpath |
| `clear()` | Empties the path, to build another in the same buffer |

A verb takes 1 byte and each coordinate 4. When the buffer is too small the path is cut short at the last whole
verb, and `p.overflowed` is true.

### Fill and stroke

A `PathLook` says how the path is drawn, in one solid color.

| `PathLook` | What it draws |
|---|---|
| `PathLook.fill(color)` | The inside, by the nonzero rule |
| `PathLook.fill(color).withEvenOdd()` | The inside, by the even-odd rule |
| `PathLook.stroke(color, width)` | The outline, `width` wide, with round joins and caps |

The two fill rules differ where subpaths overlap. Under even-odd, a shape inside another is a hole:

```ceesafe frame
var bytes = [0u8; 256];
var p = PathBuilder.over(mut bytes[..]);
p.circle(32.0, 32.0, 28.0);
p.circle(32.0, 32.0, 12.0);
ui.path(
    Style.box().w(Size.fixed(64.0)).h(Size.fixed(64.0)),
    p.bytes(),
    PathLook.fill(Color.hex(SKY_500)).withEvenOdd(),
);
```

A stroke's width is in the path's units:

```ceesafe frame
var bytes = [0u8; 128];
var p = PathBuilder.over(mut bytes[..]);
p.moveTo(4.0, 28.0);
p.cubicTo(20.0, 0.0, 44.0, 56.0, 60.0, 28.0);
ui.path(
    Style.box().w(Size.fixed(64.0)).h(Size.fixed(56.0)),
    p.bytes(),
    PathLook.stroke(Color.hex(EMERALD_500), 3.0),
);
```

### The view box

`viewBox(w, h)` stretches a `w` by `h` area, from 0, 0, over the element's box, as SVG's `viewBox` does. The path
is then written once in its own units and drawn at any size.

```ceesafe frame
var bytes = [0u8; 128];
var p = PathBuilder.over(mut bytes[..]);
p.moveTo(5.0, 12.0);
p.lineTo(10.0, 17.0);
p.lineTo(19.0, 7.0);
let look = PathLook.stroke(Color.hex(SLATE_100), 2.0).viewBox(24.0, 24.0);
ui.path(Style.box().w(Size.fixed(16.0)).h(Size.fixed(16.0)), p.bytes(), look);
ui.path(Style.box().w(Size.fixed(48.0)).h(Size.fixed(48.0)), p.bytes(), look);
```

The stroke scales with the view box, since its width is in the path's units.

## Icons

`Style.icon(id, color, stroke)` draws one of the built-in vector icons, centered in the element. A color of 0 is
the default ink, and a stroke of 0 is the default width of 2 points.

```ceesafe frame
ui.open(Style.box().size(Size.fixed(16.0)).icon(ICON_CHECK, EMERALD_500, 0.0));
ui.close();
ui.open(Style.box().size(Size.fixed(24.0)).icon(ICON_CLOSE, 0, 3.0));
ui.close();
```

| Constant | Icon |
|---|---|
| `ICON_CHECK` | A check mark |
| `ICON_CHEVRON_DOWN` | A chevron pointing down |
| `ICON_CHEVRON_RIGHT` | A chevron pointing right |
| `ICON_CLOSE` | A cross |
| `ICON_PLUS` | A plus sign |
| `ICON_MINUS` | A minus sign |
| `ICON_DOT` | A dot |

These are the icons the built-in controls use. For any other icon, draw a path with a view box.

## Images

An image lives in the `Raster`, under a handle: a `u32` that you choose, not 0. `Style.image(handle)` draws it
stretched over the element's box.

```ceesafe frame
ui.open(Style.box().w(Size.fixed(160.0)).h(Size.fixed(90.0)).rounded(Radius.Lg).clip().image(7));
ui.close();
```

`loadImage` decodes a PNG or a BMP into the raster. It runs once, in the host or at startup, not in the frame.
ClayWind reads no files itself: the caller gives it the file's bytes.

```ceesafe
import claywind;

const PHOTO: u32 = 7;

public fn loadPhoto(raster: borrow mut Raster, file: Slice<u8>, scratch: SliceMut<u8>) -> bool {
    return match (loadImage(mut raster, PHOTO, file, mut scratch)) {
        Ok(info) => info.w > 0 && info.h > 0,
        Err(_) => false,
    };
}
```

Decoding allocates nothing. A PNG needs scratch bytes for its inflated rows, and `imageInfo` says how many before
you decode:

```ceesafe
import claywind;

public fn scratchFor(file: Slice<u8>) -> usize {
    return match (imageInfo(file)) {
        Ok(info) => info.scratchBytes,
        Err(_) => 0,
    };
}
```

`ImageInfo` holds the `kind` (`ImageKind.Png` or `Bmp`), the size `w` and `h`, and `scratchBytes` (0 for a BMP).
A failure is an `ImageError`:

| `ImageError` | Meaning |
|---|---|
| `NotAnImage` | Neither a BMP nor a PNG |
| `Truncated` | The file ends early |
| `Unsupported` | A valid file using something not supported, such as BMP RLE |
| `Corrupt` | Malformed data |
| `TooSmall` | The scratch is too small, or the raster has no room for the image |

The raster's room for images is set when it is created: `RasterCapacity.images` (32 by default) and
`RasterCapacity.imagePixels` (262,144 by default, a 512 by 512 image).

An app that makes its own pixels registers an image and fills it:

```ceesafe
import claywind;

/// A 16 by 16 checkerboard under handle 3.
public fn makeChecker(raster: borrow mut Raster) -> bool {
    if (!raster.addImage(3, 16, 16)) {
        return false;
    }
    for (y in 0usize..16) {
        for (x in 0usize..16) {
            let light = (x / 4 + y / 4) % 2 == 0;
            raster.setImagePixel(3, x, y, if (light) 0xFFE2E8F0 else 0xFF475569);
        }
    }
    return true;
}
```

A pixel is `0xAABBGGRR` with straight alpha. `raster.removeImage(handle)` frees a handle, so a picture that changes
can be replaced. An image whose pixels changed is redrawn on the next frame.

## Gradients

`Style.gradient(dir, from, to)` fills the element with a gradient between two colors.

```ceesafe frame
ui.open(Style.box().w(Size.fixed(160.0)).h(Size.fixed(48.0)).rounded(Radius.Lg)
    .gradient(GradientDir.ToRight, SKY_500, AMBER_500));
ui.close();
```

| `GradientDir` | Direction |
|---|---|
| `ToTop`, `ToRight`, `ToBottom`, `ToLeft` | Toward a side |
| `ToTopRight`, `ToBottomRight`, `ToBottomLeft`, `ToTopLeft` | Toward a corner |
| `Horizontal`, `Vertical` | Left to right, top to bottom |
| `Radial` | From the center outward |

Three more calls refine a gradient. Each needs `gradient` before it.

| Call | What it does |
|---|---|
| `via(color)` | A third color between the two |
| `stops(fromPct, viaPct, toPct)` | Where each color sits, in percent. 0 keeps the default: 0, the midpoint, 100 |
| `gradientAngle(degrees)` | A CSS angle instead of a direction: 0 is to the top, 90 to the right |

```ceesafe frame
ui.open(Style.box().w(Size.fixed(160.0)).h(Size.fixed(48.0)).rounded(Radius.Lg)
    .gradient(GradientDir.ToRight, EMERALD_500, EMERALD_500).via(SLATE_950).stops(20.0, 50.0, 80.0));
ui.close();
ui.open(Style.box().w(Size.fixed(96.0)).h(Size.fixed(96.0)).rounded(Radius.Full)
    .gradient(GradientDir.Radial, AMBER_500, ROSE_500));
ui.close();
ui.open(Style.box().w(Size.fixed(160.0)).h(Size.fixed(48.0))
    .gradient(GradientDir.ToRight, VIOLET_500, CYAN_500).gradientAngle(135.0));
ui.close();
```

## Shadows and rings

`Style.shadow(size)` draws a drop shadow under the element.

```ceesafe frame
ui.open(Style.box().p(space(4.0)).bg(SLATE_800).rounded(Radius.Lg).shadow(ShadowSize.Lg));
ui.label("A raised card", Text.of().color(SLATE_100));
ui.close();
```

| `ShadowSize` | Offset down | Blur |
|---|---|---|
| `Sm` | 1 | 3 |
| `Md` | 4 | 8 |
| `Lg` | 10 | 16 |
| `Xl` | 20 | 28 |

The shadow is black at about 27% opacity. `shadowColor(color)` changes it:

```ceesafe frame
ui.open(Style.box().p(space(4.0)).bg(BLUE_600).rounded(Radius.Lg).shadow(ShadowSize.Xl)
    .shadowColor(withAlpha(BLUE_500, 120)));
ui.label("A glow", Text.of().color(WHITE));
ui.close();
```

`Style.ring(width, color, offset)` draws an outline outside the element, `offset` points away from its edge. It
takes no room in the layout. The built-in button shows its focus with one.

```ceesafe frame
ui.open(Style.box().p(space(3.0)).bg(SLATE_700).rounded(Radius.Md).ring(2.0, BLUE_400, 2.0));
ui.label("Focused", Text.of().color(SLATE_100));
ui.close();
```

## Blur and filters

A filter applies to an element and everything inside it. An element has one filter: the last one set.

| Call | What it does |
|---|---|
| `blur(radius)` | Blurs by `radius` |
| `grayscale()` | Removes the color |
| `brightness(factor)` | Multiplies the brightness: below 1 darkens, above 1 brightens |

```ceesafe frame
ui.open(Style.row().gap(space(3.0)));
ui.open(Style.box().p(space(3.0)).bg(ROSE_500).rounded(Radius.Md).blur(4.0));
ui.label("Blurred", Text.of().color(WHITE));
ui.close();
ui.open(Style.box().p(space(3.0)).bg(ROSE_500).rounded(Radius.Md).grayscale());
ui.label("Gray", Text.of().color(WHITE));
ui.close();
ui.open(Style.box().p(space(3.0)).bg(ROSE_500).rounded(Radius.Md).brightness(0.6));
ui.label("Dimmed", Text.of().color(WHITE));
ui.close();
ui.close();
```

To combine two filters, nest two elements and give each one.

## Opacity

`Style.opacity(o)` draws the element and its children as one layer at opacity `o`, between 0 and 1.

```ceesafe frame
ui.open(Style.box().p(space(3.0)).bg(BLUE_600).rounded(Radius.Md).opacity(0.5));
ui.label("Half there", Text.of().color(WHITE));
ui.close();
```

A value of 0, or of 1 and above, leaves the element fully opaque: 0 means "not set". To hide an element, do not
declare it.

For one translucent color, give the color an alpha instead: `withAlpha(AMBER_500, 128)`. That costs no layer.

## Blend modes

`Style.blend(mode)` blends the element with what is drawn under it, as CSS `mix-blend-mode` does.

```ceesafe frame
ui.open(Style.box().w(Size.fixed(88.0)).h(Size.fixed(56.0)).rounded(Radius.Md)
    .gradient(GradientDir.ToBottomRight, SKY_500, AMBER_500).clip());
ui.open(Style.box().blend(BlendMode.Multiply).w(Size.fixed(40.0)).h(Size.fixed(40.0)).mt(8.0).ml(24.0)
    .bg(ROSE_500).rounded(Radius.Full));
ui.close();
ui.close();
```

| Group | `BlendMode` |
|---|---|
| Darkening | `Multiply`, `Darken`, `ColorBurn` |
| Lightening | `Screen`, `Lighten`, `ColorDodge`, `Add` |
| Contrast | `Overlay`, `HardLight`, `SoftLight` |
| Difference | `Difference`, `Exclusion`, `Subtract` |
| Color components | `Hue`, `Saturation`, `Color`, `Luminosity` |

`BlendMode.None` is the normal drawing.

## Transforms

A transform changes how an element and its children are drawn. It does not change the layout: the element keeps its
place and size among its siblings.

| Call | What it does |
|---|---|
| `translate(x, y)` | Moves by `x` and `y` points |
| `scale(factor)` | Scales, about the element's center |
| `rotate(degrees)` | Rotates, about the element's center |
| `skew(xDegrees, yDegrees)` | Skews, about the element's center |

```ceesafe frame
ui.open(Style.box().px(space(3.0)).py(space(1.0)).bg(AMBER_500).rounded(Radius.Md).rotate(-6.0));
ui.label("NEW", Text.of().color(SLATE_950));
ui.close();
```

A transform set from the app's state animates when the state changes each frame. Ask for the next frame with
`ui.requestFrame()` while it moves:

```ceesafe
import claywind;

public struct Spinner {
    public angle: f32;
}

public fn spinnerFrame(ui: borrow mut Ui, app: borrow mut Spinner) {
    app.angle += ui.frameTime() * 180.0;
    ui.requestFrame();
    ui.open(Style.box().size(Size.fixed(24.0)).border(3.0, BLUE_500).rounded(Radius.Md).rotate(app.angle));
    ui.close();
}
```

A transform inside a `hover` or `active` variant, with a `transition`, eases by itself. See [Styles](./styles).

```ceesafe frame
ui.open(Style.box().id("lift").p(space(3.0)).bg(SLATE_800).rounded(Radius.Md)
    .transition(TransitionSpec.transformOnly().durationMs(150.0))
    .hover(Style.of().scale(1.05).translate(0.0, -2.0)));
ui.label("Hover me", Text.of().color(SLATE_100));
ui.close();
```

## The pixel ratio

Layout is in points. `Raster.setPixelRatio(ratio)` makes the raster draw `ratio` device pixels for each point, so a
Retina display gets one pixel per pixel. Paths, clips, images, blurs and text are all scaled.

The frame's size is in pixels, so a host sets it to the layout size times the ratio:

```ceesafe
import claywind;

/// Prepares `raster` for a window of `w` by `h` points on a display of `ratio` pixels per point.
public fn sizeFrame(raster: borrow mut Raster, w: f32, h: f32, ratio: f32) -> bool {
    raster.setPixelRatio(ratio);
    return raster.resize(saturatingTruncate<usize>(w * ratio), saturatingTruncate<usize>(h * ratio));
}
```

`resize` is false when the frame is larger than the capacity the raster was created with. `claywind-desktop`'s
`Pane` does both for you on every frame; see [A desktop host](./desktop).

## Incremental rendering

A `Damage` compares each frame's render commands with those of the frame before, and the raster redraws only the
boxes that changed. The result is byte-identical to a full redraw.

```ceesafe
import claywind;

/// Draws `ui`'s frame. False when nothing changed, and the raster still holds the frame.
public fn drawChanged(damage: borrow mut Damage, raster: borrow mut Raster, ui: borrow Ui) -> bool {
    return damage.render(mut raster, ui, Color.hex(SLATE_950));
}
```

| What changed | What is redrawn |
|---|---|
| Nothing | Nothing |
| Some commands | Their old and new areas, as up to four boxes |
| The number or kind of commands, the clear color, the size or the ratio | The whole frame |

`damage.invalidate()` forgets the previous frame, so the next one is drawn whole. Call it when the pixels were
lost, or when fonts were loaded and the same commands now draw other glyphs. An app does nothing else for this: it
declares the whole window every frame, and the damage finds what differs.

## The GPU backends

The `Raster` is a software renderer. `claywind-hosts/gpu` draws the same render commands with the GPU instead:

| System | Backend |
|---|---|
| macOS, iOS | Metal |
| Linux, Android | Vulkan |
| Windows | Direct3D 12, or Vulkan with `CLAYWIND_GPU=vulkan` or when Direct3D 12 cannot start |

A GPU frame also redraws only what `Damage` finds changed; `CLAYWIND_WHOLE=1` redraws it whole. The frame function
is the same for both renderers, so an app does not choose one in its frame. The GPU host is a demo host, with a
side-by-side check against `Raster`. [Status](./status) says what has run where.
