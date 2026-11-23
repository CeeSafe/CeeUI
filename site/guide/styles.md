# Styles

This page covers how an element looks: the `Style` builder, colours, corners, borders, shadows and gradients,
the variants for hover and focus, responsive styles, dark and light themes, and transitions.

## The builder

A `Style` is a value built by chained calls, one for each Tailwind class you would write.

```ceesafe frame
// Tailwind: flex flex-row p-4 gap-2 bg-slate-900 rounded-lg border border-slate-700
ui.open(Style.row().p(space(4.0)).gap(space(2.0)).bg(SLATE_900).rounded(Radius.Lg).border(1.0, SLATE_700));
ui.label("A card", Text.of().color(SLATE_100));
ui.close();
```

Each call returns a new `Style`, so a style can be kept in a `let` and built on:

```ceesafe frame
let card = Style.column().p(space(4.0)).bg(SLATE_900).rounded(Radius.Lg);
ui.open(card.w(Size.fixed(240.0)));
ui.close();
ui.open(card.w(Size.grow()).border(1.0, SKY_500));
ui.close();
```

What is not set keeps its default: no background, no border, square corners, a size that fits the content.
[Layout](./layout) covers the calls that place an element. This page covers the rest.

A style is for an element. Text has its own builder, `Text` ([Text](./text)).

## The spacing scale

Lengths are `f32` points. `space(n)` is Tailwind's spacing scale: `n * 4` points.

| Tailwind | ClayWind | Points |
|---|---|---|
| `p-1` | `p(space(1.0))` | 4 |
| `p-2.5` | `p(space(2.5))` | 10 |
| `p-4` | `p(space(4.0))` | 16 |
| `gap-6` | `gap(space(6.0))` | 24 |
| `p-[10px]` | `p(10.0)` | 10 |

## Colours

A colour is a `u32`, `0xRRGGBBAA`. Tailwind's palette is there as constants:

```ceesafe frame
ui.open(Style.box().size(Size.fixed(32.0)).bg(SLATE_900));            // bg-slate-900
ui.close();
ui.open(Style.box().size(Size.fixed(32.0)).bg(0x1E90FFFF));            // a hex colour: #1E90FF, opaque
ui.close();
ui.open(Style.box().size(Size.fixed(32.0)).bg(withAlpha(BLUE_500, 128))); // bg-blue-500/50
ui.close();
```

| Constants | What they are |
|---|---|
| `SLATE_50` ... `SLATE_950` | A family's eleven shades: 50, 100, 200, 300, 400, 500, 600, 700, 800, 900, 950 |
| `SLATE`, `GRAY`, `ZINC`, `NEUTRAL`, `STONE` | The grey families |
| `RED`, `ORANGE`, `AMBER`, `YELLOW`, `LIME`, `GREEN`, `EMERALD`, `TEAL`, `CYAN`, `SKY`, `BLUE`, `INDIGO`, `VIOLET`, `PURPLE`, `FUCHSIA`, `PINK`, `ROSE` | The colour families |
| `WHITE`, `BLACK` | Opaque white and black |
| `TRANSPARENT`, `NONE` | No colour (alpha 0) |

The last two hex digits are the alpha: `FF` is opaque. `withAlpha(color, alpha)` replaces it, with `alpha` from 0
to 255.

## Backgrounds, corners and borders

```ceesafe frame
ui.open(Style.box().size(Size.fixed(64.0)).bg(SLATE_800).rounded(Radius.Xl).border(2.0, SLATE_600));
ui.close();
ui.open(Style.row().w(Size.grow()).h(Size.fixed(40.0)).bg(SLATE_900).borderB(1.0, SLATE_700).roundedT(Radius.Lg));
ui.close();
```

| Method | What it sets |
|---|---|
| `bg(color)` | The background colour |
| `rounded(r)` | All four corners |
| `roundedT(r)`, `roundedB(r)`, `roundedL(r)`, `roundedR(r)` | The two corners of one side |
| `corners(c)` | Each corner's radius in points, from a `Corners` |
| `border(width, color)` | A border on all sides |
| `borderT`, `borderB`, `borderL`, `borderR` `(width, color)` | A border on one side |
| `divide(width, color)` | Lines between the element's children; a colour of 0 keeps the border's colour |
| `ring(width, color, offset)` | A ring around the element, `offset` points out from it |

`Radius` is Tailwind's `rounded-*` scale:

| `Radius` | Points | Tailwind |
|---|---|---|
| `None` | 0 | `rounded-none` |
| `Xs` | 2 | `rounded-xs` |
| `Sm` | 4 | `rounded-sm` |
| `Md` | 6 | `rounded-md` |
| `Lg` | 8 | `rounded-lg` |
| `Xl` | 12 | `rounded-xl` |
| `Xl2` | 16 | `rounded-2xl` |
| `Xl3` | 24 | `rounded-3xl` |
| `Full` | 9999 | `rounded-full` |

A list with lines between its rows:

```ceesafe frame
ui.open(Style.column().w(Size.fixed(240.0)).bg(SLATE_900).rounded(Radius.Lg).divide(1.0, SLATE_700));
ui.label("One", Text.of().color(SLATE_100));
ui.label("Two", Text.of().color(SLATE_100));
ui.label("Three", Text.of().color(SLATE_100));
ui.close();
```

## Shadows and opacity

```ceesafe frame
ui.open(Style.box().size(Size.fixed(96.0)).bg(SLATE_800).rounded(Radius.Xl).shadow(ShadowSize.Lg));
ui.close();
ui.open(Style.box().size(Size.fixed(96.0)).bg(SKY_500).rounded(Radius.Xl).shadow(ShadowSize.Md).shadowColor(withAlpha(SKY_500, 120)));
ui.close();
ui.open(Style.box().size(Size.fixed(96.0)).bg(ROSE_500).opacity(0.5));
ui.close();
```

| `ShadowSize` | Offset down | Blur |
|---|---|---|
| `Sm` | 1 | 3 |
| `Md` | 4 | 8 |
| `Lg` | 10 | 16 |
| `Xl` | 20 | 28 |

A shadow is black at alpha 70 of 255 unless `shadowColor` gives another colour.

`opacity(o)` draws the element and everything in it at that opacity, between 0 and 1.

## Gradients

`gradient(dir, from, to)` fills the background with a gradient between two colours.

```ceesafe frame
ui.open(Style.box().w(Size.fixed(260.0)).h(Size.fixed(60.0)).gradient(GradientDir.ToRight, INDIGO_500, PINK_500));
ui.close();
// Three colours, the middle one 30% of the way along.
ui.open(Style.box().w(Size.fixed(260.0)).h(Size.fixed(60.0))
    .gradient(GradientDir.ToBottomRight, SKY_500, EMERALD_500).via(VIOLET_500).stops(0.0, 30.0, 0.0));
ui.close();
ui.open(Style.box().size(Size.fixed(120.0)).gradient(GradientDir.Radial, AMBER_300, ROSE_600).rounded(Radius.Full));
ui.close();
```

| Method | Tailwind | What it does |
|---|---|---|
| `gradient(dir, from, to)` | `bg-gradient-to-* from-* to-*` | A gradient from one colour to another |
| `via(color)` | `via-*` | A colour between the two. Call it after `gradient` |
| `stops(fromPct, viaPct, toPct)` | `from-10% via-30% to-90%` | Where each colour sits, in percent. 0 keeps the default: 0, the midpoint, 100 |
| `gradientAngle(degrees)` | | A CSS angle: 0 is to the top, 90 to the right |

`GradientDir` is `ToTop`, `ToTopRight`, `ToRight`, `ToBottomRight`, `ToBottom`, `ToBottomLeft`, `ToLeft`,
`ToTopLeft`, `Horizontal`, `Vertical` or `Radial`.

## Filters, blending and transforms

These apply to an element and everything in it.

| Method | What it does |
|---|---|
| `blur(radius)` | Blurs it |
| `grayscale()` | Draws it in greys |
| `brightness(factor)` | Brightens it (above 1) or darkens it (below 1) |
| `blend(mode)` | Blends it with what is under it: `BlendMode.Multiply`, `Screen`, `Overlay` and the other CSS blend modes |
| `translate(x, y)` | Moves it, in points, without moving its siblings |
| `scale(factor)` | Scales it |
| `rotate(degrees)` | Rotates it |
| `skew(xDegrees, yDegrees)` | Skews it |

```ceesafe frame
ui.open(Style.box().size(Size.fixed(80.0)).bg(EMERALD_500).rounded(Radius.Lg).rotate(8.0).scale(1.1));
ui.close();
ui.open(Style.box().size(Size.fixed(80.0)).bg(ROSE_500).blend(BlendMode.Multiply).grayscale());
ui.close();
```

## Variants

A variant is a second style that applies in a state. Make it with `Style.of()`; only what it sets is used, laid
over the base style.

```ceesafe frame
ui.open(
    Style.row().id("item").px(space(4.0)).py(space(2.0)).bg(SLATE_800).rounded(Radius.Md)
        .hover(Style.of().bg(SLATE_700))
        .active(Style.of().bg(SLATE_600).scale(0.98))
        .cursor(Cursor.Pointer),
);
ui.label("Press me", Text.of().color(SLATE_100));
ui.close();
```

| Method | Tailwind | Applies while |
|---|---|---|
| `hover(v)` | `hover:` | The pointer is over the element |
| `active(v)` | `active:` | The pointer is over it and pressed |
| `groupHover(name, v)` | `group-hover:` | The pointer is over the element whose id is `name` |
| `focus(v)` | `focus:` | `isFocused(true)` was given |
| `disabled(v)` | `disabled:` | `isDisabled(true)` was given |

They are applied in that order, a later one over an earlier. A disabled element takes only its `disabled`
variant: no hover, active, group or focus.

Hover and active are found by ClayWind. Focus and disabled are the app's to say, since a style does not know
them:

```ceesafe frame
let focused = ui.hasFocusNamed("name");
let locked = false;
ui.open(
    Style.row().id("name").w(Size.fixed(240.0)).h(Size.fixed(36.0)).bg(SLATE_900).rounded(Radius.Md)
        .border(1.0, SLATE_700)
        .isFocused(focused).focus(Style.of().border(1.0, SKY_500).ring(2.0, withAlpha(SKY_500, 90), 0.0))
        .isDisabled(locked).disabled(Style.of().opacity(0.5)),
);
ui.close();
```

The controls do this themselves ([Controls](./controls)).

`groupHover` styles a child by its parent's hover:

```ceesafe frame
ui.open(Style.row().id("row").w(Size.grow()).p(space(2.0)).gap(space(2.0)).hover(Style.of().bg(SLATE_800)));
ui.label("A row", Text.of().color(SLATE_100));
ui.open(Style.box().size(Size.fixed(16.0)).rounded(Radius.Full).bg(SLATE_700).groupHover("row", Style.of().bg(SKY_500)));
ui.close();
ui.close();
```

`cursor(Cursor.Pointer)` sets the pointer's shape while it is over the element. `Cursor` is `Default`, `Pointer`
or `Text`.

## Responsive styles

Responsive variants go by the window's width, mobile first as Tailwind's. The base style is a phone's. Each
variant applies from its width up, a wider one over a narrower.

| Method | Tailwind | From (points wide) | `Breakpoint` |
|---|---|---|---|
| | | 0 | `Base` |
| `sm(v)` | `sm:` | 640 | `Sm` |
| `md(v)` | `md:` | 768 | `Md` |
| `lg(v)` | `lg:` | 1024 | `Lg` |
| `xl(v)` | `xl:` | 1280 | `Xl` |
| `xxl(v)` | `2xl:` | 1536 | `Xxl` |

```ceesafe frame
// Tailwind: flex flex-col gap-2 p-3 md:flex-row md:gap-4 lg:p-6
ui.open(
    Style.column().w(Size.grow()).gap(space(2.0)).p(space(3.0))
        .md(Style.of().flexRow().gap(space(4.0)))
        .lg(Style.of().p(space(6.0))),
);
ui.label("First", Text.of().color(SLATE_100));
ui.label("Second", Text.of().color(SLATE_100));
ui.close();
```

`flexRow()` and `flexCol()` change a style's direction, so a phone's column becomes a tablet's row. Responsive
variants are applied before hover, active and focus.

What is built only from a width up (Tailwind's `hidden md:block`) is an `if`:

```ceesafe frame
ui.open(Style.row().w(Size.grow()).h(Size.grow()));
if (ui.atLeast(Breakpoint.Md)) {
    ui.open(Style.column().w(Size.fixed(220.0)).h(Size.grow()).bg(SLATE_900));
    ui.label("Sidebar", Text.of().color(SLATE_100));
    ui.close();
}
ui.open(Style.column().w(Size.grow()).h(Size.grow()));
ui.label("Content", Text.of().color(SLATE_100));
ui.close();
ui.close();
```

| Call | What it gives |
|---|---|
| `ui.atLeast(Breakpoint.Md)` | Whether the window is at least that breakpoint's width |
| `ui.breakpoint()` | The widest breakpoint the window reaches; `Breakpoint.Base` below `sm` |
| `ui.widthClass()` | `SizeClass.Compact` below 600 points wide (a phone held upright), else `SizeClass.Regular` |
| `ui.heightClass()` | `SizeClass.Compact` below 480 points tall (a phone on its side), else `SizeClass.Regular` |
| `ui.containerWidth(name)` | The width the element named `name` had in the last frame, for Tailwind's `@container` |
| `ui.screenSize()` | The window's size in points |

```ceesafe frame
let columns: usize = match (ui.breakpoint()) {
    Breakpoint.Base => 1,
    Breakpoint.Sm => 2,
    Breakpoint.Md => 2,
    _ => 4,
};
let compact = match (ui.widthClass()) {
    SizeClass.Compact => true,
    SizeClass.Regular => false,
};
ui.open(Style.row().w(Size.grow()).wrap().gap(space(if (compact) 2.0 else 4.0)));
for (i in 0..columns) {
    ui.open(Style.box().w(Size.fixed(120.0)).h(Size.fixed(80.0)).bg(SLATE_800).rounded(Radius.Lg));
    ui.close();
}
ui.close();
```

A rotated phone is a new screen size: the next frame's breakpoints and size classes follow it.

## Dark and light themes

The `Ui` holds one flag, dark mode. The host or the app sets it, and styles choose colours by it.

```ceesafe frame
if (Controls.button(mut ui, "theme", if (ui.isDarkMode()) "Light" else "Dark")) {
    ui.setDarkMode(!ui.isDarkMode());
}
ui.open(Style.column().w(Size.grow()).p(space(4.0)).bg(ui.theme(SLATE_900, WHITE)));
ui.label("Themed", Text.of().color(ui.theme(SLATE_100, SLATE_900)));
ui.close();
```

| Call | What it does |
|---|---|
| `ui.setDarkMode(on)` | Sets the flag. It stays until it is set again |
| `ui.isDarkMode()` | The flag |
| `ui.theme(dark, light)` | `dark` in dark mode, `light` otherwise |
| `ui.tone(dark)` | `dark` in dark mode. In light mode, a slate turned over; any other colour unchanged |

`ui.tone` is for styles written for the dark theme. In light mode it gives the slate at the other end of the
scale: `SLATE_50` for `SLATE_950`, `SLATE_100` for `SLATE_900`, and so on to `SLATE_400` for `SLATE_600`;
`SLATE_500` stays. A dark surface becomes a light one and its light ink becomes dark.

```ceesafe frame
ui.open(Style.column().p(space(4.0)).bg(ui.tone(SLATE_900)).border(1.0, ui.tone(SLATE_700)));
ui.label("Dark on light, or light on dark", Text.of().color(ui.tone(SLATE_100)));
ui.close();
```

The controls draw themselves in the current theme.

## Transitions

Without a transition, a change of style shows at once. `transition(spec)` eases the element from what it showed
to what the style now says.

```ceesafe frame
ui.open(
    Style.row().id("fade").px(space(4.0)).py(space(2.0)).bg(SLATE_800).rounded(Radius.Md)
        .hover(Style.of().bg(SKY_600).scale(1.05))
        .transition(TransitionSpec.all().durationMs(200.0).easing(Ease.Out)),
);
ui.label("Hover me", Text.of().color(SLATE_100));
ui.close();
```

A `TransitionSpec` says which groups of values are eased:

| Start | Tailwind | Eases |
|---|---|---|
| `TransitionSpec.colorsOnly()` | `transition-colors` | The background colour and the border colour |
| `TransitionSpec.opacityOnly()` | `transition-opacity` | The opacity |
| `TransitionSpec.transformOnly()` | `transition-transform` | Translate, scale and rotation |
| `TransitionSpec.all()` | `transition-all` | All three groups |

| Method | Tailwind | Default |
|---|---|---|
| `durationMs(ms)` | `duration-*` | 150 |
| `delayMs(ms)` | `delay-*` | 0 |
| `easing(e)` | `ease-*` | `Ease.InOut`; the others are `Ease.Linear`, `Ease.In` and `Ease.Out` |

A transition follows any change of the style, not only a variant's:

```ceesafe
import claywind;

public fn statusDot(ui: borrow mut Ui, online: bool) {
    ui.open(
        Style.box().id("status").size(Size.fixed(12.0)).rounded(Radius.Full)
            .bg(if (online) EMERALD_500 else SLATE_600)
            .transition(TransitionSpec.colorsOnly().durationMs(300.0)),
    );
    ui.close();
}
```

The shown values are kept by the element's id. The first frame an element is seen, it shows its style at once.
Give an element with a transition an id: an anonymous element's id depends on its place among its siblings
([A frame](./frame#ids)).

While a transition moves, `ui.needsFrame()` is true, so the host runs frames at the display's pace until it ends.
The number of elements that can have a transition is `UiCapacity.animations` ([A frame](./frame#capacity)).
