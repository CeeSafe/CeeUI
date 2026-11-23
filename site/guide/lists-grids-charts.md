# Lists, grids and charts

This page covers the controls that show data: the virtual list for any number of rows, the data grid for rows
cut into sortable, resizable columns, and line, area and bar charts.

## Virtual list

A virtual list shows `count` rows of one height in a scrolling box. Only the rows in view, and a few either side,
are built each frame. A list of a million rows costs a frame what a screenful does.

The list is three calls. `Controls.virtualList` opens it and says which rows to draw. `Controls.virtualRow`
opens one row. `Controls.endVirtualList` closes the list.

```ceesafe
import claywind;

public struct Inbox {
    public count: usize;
    public selected: usize?;
}

public fn inboxFrame(ui: borrow mut Ui, app: borrow mut Inbox) {
    ui.open(Style.column().w(Size.fixed(320.0)).h(Size.fixed(400.0)));
    let list = Controls.virtualList(mut ui, "inbox", app.count, 32.0, Size.grow(), mut app.selected);
    for (i in list.first..list.last) {
        var text = [0u8; 32];
        var w = TextWriter.create(mut text[..]);
        w.text("Message ");
        w.decimal(i as u64 + 1);
        let on = Controls.virtualRow(mut ui, list, i, mut app.selected, "Message");
        ui.labelBytes(w.written(), Text.of().size(14.0).color(if (on) WHITE else SLATE_200));
        ui.close();
    }
    Controls.endVirtualList(mut ui, list);
    ui.close();
}
```

`Controls.virtualList(mut ui, id, count, rowHeight, height, mut selected)` takes:

| Argument | What it is |
|---|---|
| `id` | The list's id |
| `count` | How many rows the list has |
| `rowHeight` | The height of every row, in points |
| `height` | The list's own height: `Size.grow()`, `Size.fixed(240.0)` |
| `selected` | The selected row, a `usize?` the app keeps: `null` for none |

It returns a `VirtualList`. Its `first` and `last` are the rows to draw: `first` up to, not including, `last`.
The app loops over them and reads only those rows from its data.

### Rows

`Controls.virtualRow(mut ui, list, i, mut selected, name)` opens row `i` as a row element. What the app declares
before the matching `ui.close()` is the row's content, laid out left to right. A row can hold anything: labels,
an icon, a checkbox.

`name` is what a screen reader calls the row. `virtualRow` returns `true` when row `i` is the selected one, so
that the content can change its colours on the selection's background.

```ceesafe
import claywind;

const TRACKS: usize = 4;
static TITLES: [StringView; 4] = ["Opening", "Low Tide", "Signal", "Closing"];
static MINUTES: [u64; 4] = [3, 5, 4, 7];

public struct Album {
    public selected: usize?;
}

public fn albumFrame(ui: borrow mut Ui, app: borrow mut Album) {
    let list = Controls.virtualList(mut ui, "tracks", TRACKS, 36.0, Size.grow(), mut app.selected);
    for (i in list.first..list.last) {
        let on = Controls.virtualRow(mut ui, list, i, mut app.selected, TITLES[i]);
        ui.label(TITLES[i], Text.of().color(if (on) WHITE else SLATE_200));
        ui.open(Style.box().w(Size.grow()));
        ui.close();
        var text = [0u8; 16];
        var w = TextWriter.create(mut text[..]);
        w.decimal(MINUTES[i]);
        w.text(" min");
        ui.labelBytes(w.written(), Text.of().size(13.0).color(if (on) WHITE else SLATE_400));
        ui.close();
    }
    Controls.endVirtualList(mut ui, list);
}
```

The empty growing box between the two labels pushes the second to the row's right edge.

### Selection and keys

A press on a row selects it and gives the list the focus. The list is a Tab stop. While it has the focus:

| Key | What it does |
|---|---|
| Up, Down | Select the row above or below |
| Page Up, Page Down | Move the selection a screenful |
| Home, End | Select the first or the last row |

The list scrolls enough to show the selected row. With nothing selected, a key starts at the first or the last
row. When `count` falls below the selected row, the selection moves to the last row; an empty list has none.

The list does not act on a row; the app does. Test the list's focus and the keys after the list:

```ceesafe
import claywind;

public struct Queue {
    /// Each job's number.
    public jobs: Vector<u64>;
    public selected: usize?;
}

public fn queueFrame(ui: borrow mut Ui, app: borrow mut Queue) {
    let list = Controls.virtualList(mut ui, "queue", app.jobs.length, 32.0, Size.grow(), mut app.selected);
    for (i in list.first..list.last) {
        var text = [0u8; 32];
        var w = TextWriter.create(mut text[..]);
        w.text("Job ");
        w.decimal(app.jobs[i]);
        Controls.virtualRow(mut ui, list, i, mut app.selected, "Job");
        ui.labelBytes(w.written(), Text.of());
        ui.close();
    }
    Controls.endVirtualList(mut ui, list);
    if (ui.hasFocusNamed("queue")) {
        match (app.selected) {
            Some(i) => {
                if (ui.keyPressed(Key.Delete)) {
                    app.jobs.remove(i);
                }
            }
            null => {}
        }
    }
}
```

The next frame's `virtualList` brings the selection back in range after a row is removed.

## Data grid

A data grid is a virtual list whose rows are cut into columns under a header. Pressing a column's title sorts by
it. Dragging the edge at a title's right resizes the column. The header stays at the top, and follows the rows
when they scroll sideways.

The grid is four calls: `Controls.dataGrid` opens it, `Controls.gridRow` opens a row, `Controls.gridCell` opens a
cell in that row, and `Controls.endDataGrid` closes the grid.

```ceesafe
import claywind;

const PARTS: usize = 5;
static PART_NAMES: [StringView; 5] = ["Axle", "Bearing", "Chain", "Gear", "Spring"];

public struct Stock {
    /// How many of each part there are.
    public counts: [u32; 5];
    /// The parts in the order the grid shows them.
    public rows: [usize; 5];
    public widths: [f32; 2];
    public sort: GridSort;
    public selected: usize?;

    public fn create() -> Stock => Stock {
        counts: [12, 240, 7, 31, 96],
        rows: [0, 1, 2, 3, 4],
        widths: [160.0, 90.0],
        sort: GridSort.none(),
        selected: null,
    };
}

public fn stockFrame(ui: borrow mut Ui, app: borrow mut Stock) {
    let titles = ["Part", "In stock"];
    let grid = Controls.dataGrid(
        mut ui,
        "stock",
        titles[..],
        mut app.widths[..],
        PARTS,
        28.0,
        Size.grow(),
        mut app.sort,
        mut app.selected,
    );
    for (i in grid.first..grid.last) {
        let part = app.rows[i];
        let on = Controls.gridRow(mut ui, grid, i, mut app.selected, PART_NAMES[part]);
        let look = Text.of().size(14.0).wrap(TextWrap.None).color(if (on) WHITE else SLATE_200);
        Controls.gridCell(mut ui, grid, 0);
        ui.label(PART_NAMES[part], look);
        ui.close();
        Controls.gridCell(mut ui, grid, 1);
        var text = [0u8; 16];
        var w = TextWriter.create(mut text[..]);
        w.decimal(app.counts[part] as u64);
        ui.labelBytes(w.written(), look);
        ui.close();
        ui.close();
    }
    Controls.endDataGrid(mut ui, grid);
}
```

Each cell is opened and closed, and then the row is closed: two `ui.close()` calls end a row's last cell.

`Controls.dataGrid(mut ui, id, titles, widths, count, rowHeight, height, mut sort, mut selected)` takes:

| Argument | What it is |
|---|---|
| `titles` | The columns' titles. A grid shows up to 32 columns |
| `widths` | Each column's width in points, in an array the app keeps |
| `count` | How many rows the grid has |
| `rowHeight` | The height of every row, and of the header |
| `height` | The grid's own height |
| `sort` | How the rows are sorted, a `GridSort` the app keeps |
| `selected` | The selected row, a `usize?` the app keeps |

It returns a `DataGrid` with `first` and `last`, the rows to draw, and `sortChanged`.

### Columns and widths

The grid has as many columns as it has both titles and widths. The app owns the widths: it sets them at the
start, and the grid changes them while the user drags a title's edge. A column is never narrower than 32 points.

Because the widths are the app's, they can be saved with its state and restored.

`Controls.gridCell(mut ui, grid, column)` opens a cell as wide as its column. Content that does not fit is
clipped, so give a cell's text `wrap(TextWrap.None)` to keep it on one line.

### Sorting

The grid does not sort. It keeps `sort` up to date and sets `sortChanged` in the frame a title was pressed. The
app then sorts its rows.

| `GridSort` | What it is |
|---|---|
| `column` | The column sorted by, a `usize?`: `null` when the rows are as the app has them |
| `descending` | Whether the largest comes first |
| `GridSort.none()` | No sort |
| `GridSort.by(column, descending)` | A sort by a column |

A press on a title sorts by its column, ascending. A second press on the same title turns it to descending, and
a third back to ascending. The sorted column's title shows an arrow.

This function sorts the rows of the grid above. Call it where the frame has `grid.sortChanged`:

```ceesafe
import claywind;

const PARTS: usize = 5;

public struct Stock {
    public counts: [u32; 5];
    public rows: [usize; 5];
    public sort: GridSort;
}

/// Whether part `a` comes before part `b`.
fn before(app: borrow Stock, a: usize, b: usize) -> bool {
    let byCount = app.sort.column == Some(1);
    let ka = if (byCount) app.counts[a] as u64 else a as u64;
    let kb = if (byCount) app.counts[b] as u64 else b as u64;
    return if (app.sort.descending) ka > kb else ka < kb;
}

/// An insertion sort of the rows by the grid's sort.
public fn sortStock(app: borrow mut Stock) {
    var i: usize = 1;
    while (i < PARTS) {
        var k = i;
        while (k > 0 && before(app, app.rows[k], app.rows[k - 1])) {
            let moved = app.rows[k];
            app.rows[k] = app.rows[k - 1];
            app.rows[k - 1] = moved;
            k -= 1;
        }
        i += 1;
    }
}

/// After Controls.dataGrid, before the rows are drawn.
public fn sortIfAsked(grid: borrow DataGrid, app: borrow mut Stock) {
    if (grid.sortChanged) {
        sortStock(mut app);
    }
}
```

The example sorts an array of row numbers, not the data. The grid's row `i` then shows part `app.rows[i]`.

### Selection

Selection works as in a virtual list: a press selects a row, and the same keys move it while the grid has the
focus. `Controls.gridRow` returns `true` for the selected row.

`selected` is a row's position, not the row's data. After a sort, the same position holds another row. To keep
the same item selected, note which item was selected before sorting and find its new position after.

## Charts

`Controls.chart` draws a line, area or bar chart of one or more series. It is one call and returns nothing.

```ceesafe frame
let months = ["Jan", "Feb", "Mar", "Apr"];
let orders = [310.0f32, 342.0, 335.0, 380.0];
let colors = [EMERALD_400];
ui.open(Style.column().w(Size.fixed(420.0)));
Controls.chart(mut ui, "orders", ChartKind.Bar, orders[..], months[..], colors[..], 200.0);
ui.close();
```

`Controls.chart(mut ui, id, kind, values, labels, colors, height)` takes:

| Argument | What it is |
|---|---|
| `kind` | A `ChartKind` |
| `values` | The values, as `f32`: one for each label, series after series |
| `labels` | The labels along the bottom |
| `colors` | One colour for each series; their number is the number of series |
| `height` | The chart's height in points |

| `ChartKind` | What it draws |
|---|---|
| `Line` | A line through each series' points |
| `Area` | Lines with the area under them filled, translucent |
| `Bar` | Bars side by side at each label, one for each series |

The chart is as wide as its container gives it. Its value axis runs over round values and always takes in 0.
With the pointer over the plot, the chart shows the values at the nearest label.

### Several series

All series are in one slice of values: the first series' values, then the second's. Each series has one value
for each label.

```ceesafe frame
let quarters = ["Q1", "Q2", "Q3", "Q4"];
let revenue = [
    42.0f32, 48.5, 53.8, 61.0, // online
    30.1, 33.4, 34.2, 39.8, // in store
];
let colors = [SKY_400, ROSE_400];
ui.open(Style.column().w(Size.grow()).gap(space(1.0)));
ui.label("Revenue, thousands (online, in store)", Text.of().size(13.0).color(SLATE_300));
Controls.chart(mut ui, "revenue", ChartKind.Area, revenue[..], quarters[..], colors[..], 170.0);
ui.close();
```

The chart draws no title and no legend. Say what the series are in a label of your own, as above.

Two charts side by side each take half of a row:

```ceesafe frame
let days = ["Mon", "Tue", "Wed", "Thu", "Fri"];
let visits = [120.0f32, 180.0, 150.0, 210.0, 190.0];
let errors = [3.0f32, 1.0, 4.0, 0.0, 2.0];
ui.open(Style.row().w(Size.grow()).gap(space(4.0)));
ui.open(Style.column().w(Size.grow()));
Controls.chart(mut ui, "visits", ChartKind.Line, visits[..], days[..], [SKY_400][..], 160.0);
ui.close();
ui.open(Style.column().w(Size.grow()));
Controls.chart(mut ui, "errors", ChartKind.Bar, errors[..], days[..], [ROSE_400][..], 160.0);
ui.close();
ui.close();
```

A chart is drawn as vector paths; [Drawing](./drawing) describes paths.
