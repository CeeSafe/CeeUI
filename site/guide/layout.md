# Layout

This page covers how elements are placed: nesting, rows and columns, sizes, padding and gaps, alignment, wrapping,
aspect ratios, floating elements, scrolling and clipping.

## Elements nest

`ui.open(style)` begins an element and `ui.close()` ends it. What is declared between them is inside it.

```ceesafe frame
ui.open(Style.column().p(space(4.0)).gap(space(2.0)).bg(SLATE_900));
ui.label("Title", Text.of().size(20.0).color(SLATE_100));
ui.open(Style.row().gap(space(2.0)));
ui.label("left", Text.of().color(SLATE_400));
ui.label("right", Text.of().color(SLATE_400));
ui.close();
ui.close();
```

Every `open` needs its `close`. An element with no children is an `open` followed at once by a `close`: a
coloured box, a spacer, a divider.

The frame's root is opened by `ui.begin` and is the size of the screen. The app's outermost element usually
fills it with `Size.grow()` on both axes.

## Rows and columns

A style begins as one of these:

| Start | Children are laid out | Size |
|---|---|---|
| `Style.row()` | Left to right | Fits its content |
| `Style.column()` | Top to bottom | Fits its content |
| `Style.box()` | Top to bottom (the same as `column()`) | Fits its content |
| `Style.separator()` | Left to right | Grows on both axes |
| `Style.of()` | Left to right (the same as `row()`) | For a variant's style: see [Styles](./styles#variants) |

```ceesafe frame
ui.open(Style.row().gap(space(2.0)));
ui.open(Style.box().w(Size.fixed(40.0)).h(Size.fixed(40.0)).bg(SKY_500));
ui.close();
ui.open(Style.box().w(Size.fixed(40.0)).h(Size.fixed(40.0)).bg(ROSE_500));
ui.close();
ui.close();
```

`flexRow()` and `flexCol()` change the direction of a style that already began. They are for responsive styles:
a column on a phone that is a row on a tablet ([Styles](./styles#responsive-styles)).

`reverse()` lays the children out in the opposite order.

## Sizes

`w` and `h` take a size for each axis; `size` sets both.

| Size | The element is |
|---|---|
| `Size.fit()` | As large as its content and padding. The default |
| `Size.grow()` | As large as the space its parent has left, shared with the other growing children |
| `Size.fixed(200.0)` | 200 points |
| `Size.percent(0.5)` | That fraction of its parent's inner size, from 0 to 1 |

```ceesafe frame
ui.open(Style.row().w(Size.grow()).h(Size.fixed(48.0)).gap(space(2.0)));
ui.open(Style.box().w(Size.fixed(200.0)).h(Size.grow()).bg(SLATE_800)); // a sidebar 200 points wide
ui.close();
ui.open(Style.box().w(Size.grow()).h(Size.grow()).bg(SLATE_700));       // the rest of the row
ui.close();
ui.open(Style.box().size(Size.fixed(48.0)).bg(SLATE_600));              // 48 by 48
ui.close();
ui.close();
```

Sizes are in points (layout units). A host draws several device pixels a point on a dense screen.

`minW`, `maxW`, `minH` and `maxH` bound a fit or growing size. They do not apply to a percent size.

```ceesafe frame
ui.open(Style.column().w(Size.grow()).maxW(640.0).minH(120.0).bg(SLATE_900));
ui.label("At most 640 points wide, at least 120 tall", Text.of().color(SLATE_100));
ui.close();
```

When the children of a row are wider than the row, they are made narrower. `shrink0()` keeps an element's size,
and `basis(px)` gives it a starting size along its parent's direction:

```ceesafe frame
ui.open(Style.row().w(Size.grow()).h(Size.fixed(40.0)).gapX(space(2.0)));
ui.open(Style.box().basis(160.0).shrink0().h(Size.fixed(32.0)).bg(SLATE_800));
ui.close();
ui.open(Style.box().w(Size.grow()).h(Size.fixed(32.0)).bg(SLATE_700));
ui.close();
ui.close();
```

## Padding, gaps and margins

All of them take points. `space(n)` is Tailwind's spacing scale, `n * 4` points, so `p(space(4.0))` is
Tailwind's `p-4`.

| Method | What it sets |
|---|---|
| `p(v)` | Padding on all four sides |
| `px(v)`, `py(v)` | Padding left and right; top and bottom |
| `pt(v)`, `pb(v)`, `pl(v)`, `pr(v)` | Padding on one side |
| `gap(v)` | The space between children |
| `gapX(v)`, `gapY(v)` | The horizontal and the vertical gap, whatever the direction |
| `m(v)` | Margin on all four sides |
| `mx(v)`, `my(v)` | Margin left and right; top and bottom |
| `mt(v)`, `mb(v)`, `ml(v)`, `mr(v)` | Margin on one side |

```ceesafe frame
ui.open(Style.column().px(space(6.0)).py(space(4.0)).gap(space(3.0)).bg(SLATE_900));
ui.label("First", Text.of().color(SLATE_100));
ui.label("Second", Text.of().color(SLATE_100));
ui.close();
```

A margin may be negative, which makes siblings overlap:

```ceesafe frame
ui.open(Style.row());
ui.open(Style.box().w(Size.fixed(60.0)).h(Size.fixed(26.0)).bg(GREEN_500).rounded(Radius.Lg));
ui.close();
ui.open(Style.box().w(Size.fixed(60.0)).h(Size.fixed(26.0)).ml(-24.0).bg(withAlpha(BLUE_500, 150)).rounded(Radius.Lg));
ui.close();
ui.close();
```

On a phone, `safe(ui.safeArea())` adds the screen's insets to an element's padding ([Phones](./phones)).

## Alignment

There are two ways to say where children go in the space their parent has left.

`alignXY(x, y)` names the axes outright:

```ceesafe frame
ui.open(Style.row().w(Size.grow()).h(Size.fixed(48.0)).alignXY(AxisAlign.Start, AxisAlign.Center));
ui.label("At the left, centred top to bottom", Text.of().color(SLATE_100));
ui.close();

ui.open(Style.column().w(Size.grow()).h(Size.fixed(120.0)).center());
ui.label("In the middle", Text.of().color(SLATE_100));
ui.close();
```

`center()` is `alignXY(AxisAlign.Center, AxisAlign.Center)`.

`justify` and `items` name the axes by the direction, as Tailwind's `justify-*` and `items-*` do. `justify` is
the main axis (a row's x, a column's y) and `items` the cross axis.

```ceesafe frame
ui.open(Style.row().w(Size.grow()).justify(Justify.Between).items(Items.Center));
ui.label("Name", Text.of().color(SLATE_100));
ui.label("12 items", Text.of().color(SLATE_400));
ui.close();
```

| `AxisAlign` | `Justify` | `Items` | Children are |
|---|---|---|---|
| `Start` | `Start` | `Start` | At the left or top. The default |
| `Center` | `Center` | `Center` | In the middle |
| `End` | `End` | `End` | At the right or bottom |
| `Between` | `Between` | | Spread out, the first and last at the edges (main axis only) |
| `Around` | `Around` | | Spread out, with half a space at each edge (main axis only) |
| `Evenly` | `Evenly` | | Spread out, with equal spaces (main axis only) |
| `Baseline` | | `Baseline` | On a shared text baseline (cross axis only) |
| | | `Stretch` | Fit children fill the cross axis |

```ceesafe frame
ui.open(Style.row().gap(space(1.0)).items(Items.Baseline));
ui.label("42", Text.of().size(32.0).color(SLATE_100));
ui.label("tasks", Text.of().size(14.0).color(SLATE_400));
ui.close();
```

A growing child takes the space that alignment would share out. A common spacer is an empty growing element:

```ceesafe frame
ui.open(Style.row().w(Size.grow()));
ui.label("Title", Text.of().color(SLATE_100));
ui.open(Style.box().w(Size.grow()));
ui.close();
ui.label("Right", Text.of().color(SLATE_400));
ui.close();
```

## Wrapping

`wrap()` makes a row's children go on to a new line when they do not fit. `gapX` is the space between children
on a line, and `gapY` the space between lines.

```ceesafe frame
ui.open(Style.row().id("tags").w(Size.grow()).wrap().gapX(space(2.0)).gapY(space(2.0)));
for (i in 0..12) {
    ui.open(Style.box().w(Size.fixed(72.0)).h(Size.fixed(24.0)).bg(SLATE_700).rounded(Radius.Full));
    ui.close();
}
ui.close();
```

`content(AlignContent.Center)` says how the lines share the cross axis, as Tailwind's `content-*` does. Its
values are `Start`, `Center`, `End`, `Between`, `Around`, `Evenly` and `Stretch`.

## Aspect ratio

`aspect(ratio)` keeps an element's width over its height at `ratio`. Give one of the two sizes; the other
follows.

```ceesafe frame
ui.open(Style.box().w(Size.fixed(160.0)).aspect(16.0 / 9.0).bg(SLATE_800)); // 160 by 90
ui.close();
ui.open(Style.box().w(Size.fixed(10.0)).aspect(1.0).bg(TEAL_400).rounded(Radius.Full)); // a dot
ui.close();
```

## Floating elements

An `absolute()` or `fixed()` element is taken out of its parent's flow: it takes no room among its siblings and
is drawn over them.

| Method | Placed against |
|---|---|
| `absolute()` | Its parent |
| `fixed()` | The window |

`top`, `left`, `right` and `bottom` are offsets inward from that edge. An element with a `right` offset is
anchored to the right edge, and one with a `bottom` offset to the bottom edge; otherwise it is anchored to the left
and the top.

```ceesafe frame
ui.open(Style.box().id("avatar").w(Size.fixed(48.0)).h(Size.fixed(48.0)).bg(SLATE_700).rounded(Radius.Full));
// A badge over the avatar's top right corner.
ui.open(Style.box().absolute().top(2.0).right(2.0).w(Size.fixed(12.0)).h(Size.fixed(12.0)).bg(RED_500).rounded(Radius.Full));
ui.close();
ui.close();

// A notice 16 points from the window's bottom right corner, over everything else.
ui.open(Style.box().fixed().right(16.0).bottom(16.0).z(10).p(space(3.0)).bg(SLATE_800).rounded(Radius.Lg));
ui.label("Saved", Text.of().color(SLATE_100));
ui.close();
```

An offset of 0 does not anchor. `inset(right, bottom)` anchors to the right edge, the bottom edge or both with
no offset:

```ceesafe frame
ui.open(Style.box().w(Size.fixed(200.0)).h(Size.fixed(100.0)).bg(SLATE_800));
ui.open(Style.box().absolute().inset(true, true).w(Size.fixed(20.0)).h(Size.fixed(20.0)).bg(AMBER_500));
ui.close();
ui.close();
```

### Z order

Elements are drawn in the order they are declared, children over their parent. `z(index)` orders floating
elements among themselves: a higher index is drawn later, on top. It applies only with `absolute()` or `fixed()`.

`passthrough()` makes a floating element one the pointer passes through, so what is under it is still hovered
and clicked. It is for a highlight, a caret, or a tooltip that follows the pointer:

```ceesafe frame
let at = ui.pointer().position;
ui.open(Style.box().fixed().passthrough().left(at.x + 8.0).top(at.y + 8.0).z(500).px(space(3.0)).py(space(1.0)).bg(SLATE_800));
ui.label("Under the pointer", Text.of().size(12.0).color(SLATE_100));
ui.close();
```

## Scrolling

`scroll(axis)` makes an element a scroll container: it keeps its own size, clips its children, and scrolls them.

| `ScrollAxis` | Scrolls |
|---|---|
| `Vertical` | Up and down |
| `Horizontal` | Left and right |
| `Both` | Both ways |

```ceesafe frame
ui.open(Style.column().id("log").w(Size.grow()).h(Size.fixed(200.0)).p(space(3.0)).gap(space(2.0)).scroll(ScrollAxis.Vertical));
for (i in 0..40) {
    ui.label("A line of the log", Text.of().color(SLATE_300));
}
ui.close();
```

The wheel, a drag with the mouse, a finger's pan and the container's scroll bar scroll it. A drag and a pan
carry on with momentum after the release. `noDragScroll()` turns the mouse's drag off, for a container whose
content is selected by dragging.

Give a scroll container an id. Its position is kept between frames by id, and the calls that read and move it
take the name:

| Call | What it does |
|---|---|
| `ui.scrollOffset(name)` | The container's scroll position, a `Vec2` |
| `ui.scrollBy(name, dx, dy)` | Scrolls by that many points |
| `ui.scrollToTop(name)` | Scrolls to the start |
| `ui.scrollToBottom(name)` | Scrolls to the end |
| `ui.scrollIntoView(name, top, bottom)` | Scrolls just enough to show the span from `top` to `bottom` of the content |

```ceesafe frame
if (Controls.button(mut ui, "toTop", "Back to top")) {
    ui.scrollToTop("log");
}
```

A list of thousands of rows should be a virtual list, which declares only the rows on screen
([Lists, grids and charts](./lists-grids-charts)).

## Clipping

`clip()` is Tailwind's `overflow-hidden`: children are cut off at the element's edges, rounded corners included.
Without it, a child larger than its parent is drawn outside it.

```ceesafe frame
ui.open(Style.box().id("clipOn").w(Size.fixed(220.0)).h(Size.fixed(120.0)).bg(SLATE_800).rounded(Radius.Xl2).clip());
ui.open(Style.box().w(Size.fixed(300.0)).h(Size.fixed(60.0)).bg(ROSE_500));
ui.close();
ui.close();
```

## Reading the layout back

A frame can read where an element was in the last frame, by its id:

```ceesafe frame
match (ui.boxOf("log")) {
    Some(box) => {
        if (box.h < 100.0) {
            ui.label("The log is short", Text.of().color(SLATE_400));
        }
    }
    null => {} // not laid out yet
}
```

`ui.containerWidth(name)` and `ui.containerHeight(name)` are the element's size alone, 0 before its first layout.
`ui.screenSize()` is the window's size. [A frame](./frame#what-is-kept-between-frames) says what else is kept.
