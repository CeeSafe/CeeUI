# Editing text

This page covers the controls that edit and select text: the one-line text field, the views of long text, the
multi-line editor and the rich editor. It also says what a host passes in and out for the clipboard and for input
methods. Showing text is on [Text](./text).

## The text field

`Controls.textField` is a one-line field with a caret and a selection. The app owns three things: the buffer the
text is in, how many of its bytes are text, and a `TextEdit`.

```ceesafe
import ceeui;

public struct Login {
    public name: [u8; 64];
    public nameLength: usize;
    public nameEdit: TextEdit;
}

public fn loginFrame<M: TextMetrics>(ui: borrow mut Ui, app: borrow mut Login, metrics: borrow M) {
    ui.open(Style.column().w(Size.fixed(280.0)));
    if (Controls.textField(mut ui, metrics, "name", mut app.name[..], mut app.nameLength, mut app.nameEdit)) {
        // the text changed this frame
    }
    ui.accessName("name", "Name");
    ui.close();
}
```

The field returns `true` in the frame its text changed. It measures text to place the caret, so it takes the text
metrics, which a frame gets from its host. The text is UTF-8; text that does not fit the buffer is not taken.
The field is as wide as its container.

### TextEdit

A `TextEdit` is the caret and the other end of the selection, both byte offsets into the text.

| Member | What it is |
|---|---|
| `caret` | Where the caret is |
| `anchor` | The other end of the selection; equal to `caret` when nothing is selected |
| `TextEdit.at(offset)` | A caret at `offset`, nothing selected |
| `from()`, `to()` | Where the selection starts and ends |
| `selects()` | Whether anything is selected |

When the app changes the text itself, it sets the `TextEdit` too:

```ceesafe
import ceeui;

public struct Search {
    public query: [u8; 64];
    public length: usize;
    public edit: TextEdit;
}

public fn clear(app: borrow mut Search) {
    app.length = 0;
    app.edit = TextEdit.at(0);
}

public fn selectAll(app: borrow mut Search) {
    app.edit = TextEdit { caret: app.length, anchor: 0 };
}
```

### The pointer and the keys

| Input | What it does |
|---|---|
| A press | Places the caret and gives the field the focus |
| A drag | Selects |
| A double click | Selects the word |
| A triple click | Selects everything |
| Shift and a press | Extends the selection to the press |
| Left, Right | Move the caret a grapheme cluster |
| Home, End | Move the caret to the text's ends |
| Shift with these | Extends the selection |
| Option (macOS) or Control with Left, Right | Move by words |
| Backspace, Delete | Remove the selection, or a grapheme cluster |
| Typing, Paste | Replace the selection |
| Copy, Cut, Select All | Act on the selection |
| Enter | Leaves the field |
| A press anywhere else | Leaves the field |

The arrows move the caret as the text is drawn: in right-to-left text, Right goes towards the text's start. A
grapheme cluster is what a reader sees as one character: an accented letter, a flag, an emoji sequence.

The field gives up the focus on Enter. To act on Enter, read it before the field:

```ceesafe
import ceeui;

public struct Chat {
    public draft: [u8; 96];
    public length: usize;
    public edit: TextEdit;
    public sent: u32;
}

public fn chatFrame<M: TextMetrics>(ui: borrow mut Ui, app: borrow mut Chat, metrics: borrow M) {
    let enter = ui.hasFocusNamed("draft") && ui.keyPressed(Key.Enter);
    Controls.textField(mut ui, metrics, "draft", mut app.draft[..], mut app.length, mut app.edit);
    if (enter && app.length > 0) {
        app.sent += 1;
        app.length = 0;
        app.edit = TextEdit.at(0);
        ui.setFocus(hashId("draft")); // ready for the next one
    }
}
```

### Password field

`Controls.passwordField` takes the same arguments. It shows a dot for each character and gives nothing to Copy
or Cut. A double click selects everything, a screen reader hears only the dots, and an input method does not
compose in it.

```ceesafe
import ceeui;

public struct SignIn {
    public secret: [u8; 64];
    public length: usize;
    public edit: TextEdit;
}

public fn signInFrame<M: TextMetrics>(ui: borrow mut Ui, app: borrow mut SignIn, metrics: borrow M) {
    Controls.passwordField(mut ui, metrics, "secret", mut app.secret[..], mut app.length, mut app.edit);
    ui.accessName("secret", "Password");
}
```

### The soft keyboard

On a phone, a field can say what kind of text it takes and what the keyboard's Return key shows. Report it after
the field, while the field has the focus. A report lasts the frame.

```ceesafe
import ceeui;

public struct Form {
    public age: [u8; 8];
    public length: usize;
    public edit: TextEdit;
}

public fn formFrame<M: TextMetrics>(ui: borrow mut Ui, app: borrow mut Form, metrics: borrow M) {
    Controls.textField(mut ui, metrics, "age", mut app.age[..], mut app.length, mut app.edit);
    if (ui.hasFocusNamed("age")) {
        ui.reportTextKind(TextKind.Number);
        ui.reportReturnKey(ReturnKey.Next);
    }
}
```

| `TextKind` | The text | `ReturnKey` | The key's face |
|---|---|---|---|
| `Text` | Any text; the default | `Done` | The default |
| `Number` | Whole numbers | `Go` | Goes to what was typed |
| `Decimal` | Numbers with a decimal separator | `Next` | Moves to the next field |
| `Email` | An email address | `Search` | Searches |
| `Url` | An address | `Send` | Sends what was typed |
| `Password` | The keyboard neither learns nor suggests | `System` | The system's own key |

The Return key's face changes only what it shows. The key is Enter whichever face it has. [Phones](./phones)
describes the rest of a phone host.

## Shortcuts and the clipboard

CeeUI does not reach the system clipboard itself; the host does. The `Ui` carries editing commands and
clipboard text between the host and the focused control.

Copy, Cut, Paste, Select All, Undo and Redo are bound to the platform's shortcuts by default. "Secondary" is
Command on macOS and Control elsewhere.

| Action | Default keystroke | `EditCommand` |
|---|---|---|
| `"copy"` | secondary-C | `Copy` |
| `"cut"` | secondary-X | `Cut` |
| `"paste"` | secondary-V | `Paste` |
| `"select-all"` | secondary-A | `SelectAll` |
| `"undo"` | secondary-Z | `Undo` |
| `"redo"` | secondary-Shift-Z, Control-Y | `Redo` |
| `"delete"` | none | `Delete`: removes the selection without the clipboard |

A keystroke bound to one of these actions becomes the frame's edit command, and the focused field or editor acts
on it. An app can bind other keystrokes to the same actions with `ui.bind`, or trigger one with `ui.dispatch`:

```ceesafe frame
let bound = ui.bind("ctrl-insert", "copy", "");
if (ui.typed('!')) {
    ui.dispatch("select-all"); // acts on the next frame
}
```

[Input](./input) describes actions and key bindings.

### What a host does

A host made with `ceeui-desktop`'s `Pane` does all of this already. A host of your own does two things around
the frame.

Before the frame's controls run, it gives the `Ui` the clipboard's text when a Paste was commanded, and the input
method's composition:

```ceesafe
import ceeui;

/// After ui.begin, before the app's frame. `clipboard` is the system clipboard's text, `composing` the input
/// method's composition (empty when none) and `composingCaret` the caret's byte offset in it.
public fn editInto(ui: borrow mut Ui, clipboard: Slice<u8>, composing: Slice<u8>, composingCaret: usize) {
    if (ui.commanded(EditCommand.Paste)) {
        ui.paste(clipboard);
    }
    ui.setComposition(composing, composingCaret);
}
```

A host whose shortcuts come from the system, such as a menu item, gives the command itself with
`ui.editCommand(EditCommand.Copy)`.

After the frame, it takes what a control copied or cut, and learns whether a text control has the focus:

```ceesafe
import ceeui;

/// After the frame. True when a control copied or cut text: it is added to `copied`, which the host then puts on
/// the system clipboard.
public fn editFrom(ui: borrow Ui, copied: borrow mut Vector<u8>) -> bool {
    match (ui.copiedText()) {
        Some(text) => {
            for (b in text) {
                copied.push(b)!;
            }
            return true;
        }
        null => { return false; }
    }
}

/// After the frame: whether the input method should be on, and where its candidate window goes.
public fn inputMethodArea(ui: borrow Ui) -> Rect? => if (ui.wantsTextInput()) Some(ui.textInputArea()) else null;
```

| Call | When | What it is |
|---|---|---|
| `ui.editCommand(c)` | Before the frame | The user gave editing command `c` |
| `ui.commanded(c)` | Any time in the frame | Whether command `c` was given this frame |
| `ui.paste(utf8)` | Before the frame | A Paste of this text |
| `ui.setComposition(utf8, caret)` | Before the frame | The input method's composition |
| `ui.setClipboardHasText(has)` | When a menu with Paste may open | Whether the clipboard has text |
| `ui.copiedText()` | After the frame | The text a control copied or cut, or `null` |
| `ui.wantsTextInput()` | After the frame | Whether a text control has the focus |
| `ui.textInputArea()` | After the frame | The focused control's caret, as a `Rect` |
| `ui.textInputKind()`, `ui.returnKey()` | After the frame | What the focused field reported for a soft keyboard |

A control of your own that copies calls `ui.copyText(utf8)`, and reads a Paste with `ui.pastedText()`.

## Input methods

An input method composes text before it commits it: Japanese kana into kanji, a dead key and a letter into an
accented letter. While a composition is in progress, the focused field or editor shows it at the caret,
underlined. It is not part of the text yet. When the input method commits it, it arrives as typed text.

The host passes the composition in with `ui.setComposition` each frame, empty when there is none. It turns the
input method on while `ui.wantsTextInput()` is true, and places its candidate window by `ui.textInputArea()`.
The app does nothing.

A password field does not show a composition.

## Long text

A label lays out all of its text. For a long document, index its lines once and show it with a text view, which
lays out only the lines on screen. A 100,000-line document costs a frame what a page of it does.

`indexLines(text, starts)` writes where each line begins into `starts` and returns the number of lines. It
returns `null` when `starts` has no room for them all. An empty text is one empty line.

```ceesafe
import ceeui;

public struct Log {
    public text: [u8; 65536];
    public length: usize;
    public starts: [u32; 2048];
    public count: usize;
}

/// After the text changed: index its lines again.
public fn indexLog(app: borrow mut Log) -> bool {
    match (indexLines(app.text[0..app.length], mut app.starts[..])) {
        Some(count) => {
            app.count = count;
            return true;
        }
        null => { return false; } // more lines than `starts` holds
    }
}

public fn logFrame(ui: borrow mut Ui, app: borrow Log) {
    ui.open(Style.column().w(Size.grow()).h(Size.grow()));
    let look = Text.of().mono().size(13.0).leading(18.0);
    Controls.textView(mut ui, "log", app.text[0..app.length], app.starts[..], app.count, look, true);
    ui.close();
}
```

`Controls.textView(mut ui, id, text, starts, count, look, gutter)` is a scrolling view. Lines do not wrap. Each
line is as tall as `look`'s line height, or 1.4 times its font size when it has none. With `gutter` true, the
view numbers the lines.

Two more functions read an indexed text:

| Function | What it returns |
|---|---|
| `lineOf(text, starts, count, k)` | Line `k`, without its newline |
| `lineAt(starts, count, offset)` | The line that holds byte `offset` |
| `reindexLines(text, starts, first)` | The line count, after indexing again from line `first` on |

### A view with a selection

`Controls.selectableTextView` is the same view with a caret and a selection, kept in a `TextEdit` whose offsets
are bytes of the whole text. The text can be selected and copied, not changed.

```ceesafe
import ceeui;

public struct Viewer {
    public text: [u8; 65536];
    public length: usize;
    public starts: [u32; 2048];
    public count: usize;
    public edit: TextEdit;
}

public fn viewerFrame<M: TextMetrics>(ui: borrow mut Ui, app: borrow mut Viewer, metrics: borrow M) {
    ui.open(Style.column().w(Size.grow()).h(Size.grow()));
    Controls.selectableTextView(
        mut ui,
        metrics,
        "viewer",
        app.text[0..app.length],
        app.starts[..],
        app.count,
        Text.of().size(14.0).leading(20.0),
        false,
        mut app.edit,
    );
    ui.close();
}
```

| Input | What it does |
|---|---|
| A press, a drag | Places the caret; selects |
| A double click, a triple click | Selects a word; a line |
| Left, Right, Up, Down | Move the caret, across lines too |
| Home, End | Go to the line's ends |
| Page Up, Page Down | Move a view's height |
| Option (macOS) or Control with the arrows | Move by words |
| Shift with these | Extends the selection |
| Copy, Select All | Act on the selection |

Up and Down keep the caret's place across through shorter lines. The view scrolls to keep the caret in sight.

## The text editor

`Controls.textEditor` edits a multi-line text. It is a selectable text view that changes its text: typing and
Paste replace the selection, Enter starts a line, Backspace and Delete at a line's edge join lines, and Cut takes
the selection. It returns `true` when the text changed.

The editor works on a `TextDocument`, which gathers the memory the editor uses. All of it is the app's:

| Part | What it is |
|---|---|
| `buf`, `len` | The text's buffer, and how many of its bytes are text |
| `starts`, `count` | The index of its lines, and how many there are |
| `rows` | How many rows each line wraps into; needed for wrapping |
| `history`, `undo` | The undo history's buffer, and a `TextHistory` that says what is in it |
| `edit` | The caret and the selection |
| `wrapWidth`, `goalX` | What the editor keeps between frames for wrapping and for Up and Down |

A frame makes the document over the app's arrays with `TextDocument.over`, which indexes the lines. It gives the
document what the editor kept from the last frame, runs the editor, and takes those values back.

```ceesafe
import ceeui;

public struct Notes {
    public text: [u8; 16384];
    public length: usize;
    public starts: [u32; 1024];
    public rows: [u16; 1024];
    public history: [u8; 4096];
    public edit: TextEdit;
    public undo: TextHistory;
    public wrapWidth: f32;
    public goalX: f32;

    public fn create() -> Notes => Notes {
        text: [0u8; 16384],
        length: 0,
        starts: [0u32; 1024],
        rows: [0u16; 1024],
        history: [0u8; 4096],
        edit: TextEdit.at(0),
        undo: TextHistory.create(),
        wrapWidth: 0.0,
        goalX: -1.0,
    };
}

public fn notesFrame<M: TextMetrics>(ui: borrow mut Ui, app: borrow mut Notes, metrics: borrow M) {
    var doc = TextDocument.over(
        mut app.text[..],
        app.length,
        mut app.starts[..],
        mut app.rows[..],
        mut app.history[..],
    );
    doc.edit = app.edit;
    doc.undo = app.undo;
    doc.wrapWidth = app.wrapWidth;
    doc.goalX = app.goalX;
    ui.open(Style.column().w(Size.grow()).h(Size.grow()));
    let changed = Controls.textEditor(mut ui, metrics, "notes", mut doc, Text.of().size(15.0).leading(22.0), false);
    ui.close();
    app.length = doc.len;
    app.edit = doc.edit;
    app.undo = doc.undo;
    app.wrapWidth = doc.wrapWidth;
    app.goalX = doc.goalX;
    if (changed) {
        // the text is app.text[0..app.length]: mark the document unsaved, for instance
    }
}
```

An edit with no room in the text's buffer or in the line index is not made. The sizes of the arrays are the
document's limits: here 16,384 bytes and 1,024 lines.

`Controls.textEditor(mut ui, metrics, id, mut doc, look, gutter)` takes the look of the text and whether to
number the lines, as a text view does. An input method's composition shows underlined at the caret.

### Undo and redo

With a history buffer, the editor records each edit. Undo and Redo arrive as edit commands: secondary-Z,
secondary-Shift-Z and Control-Y by default.

- A run of typing is one record, ended by a space, a line break or a move of the caret. Undo takes back a word.
- When the buffer is full, the oldest records go.
- A document made with an empty history slice has no undo.

`TextHistory` says whether there is anything to undo or redo, which a toolbar or a menu can show. This function
draws Undo and Redo buttons for a document; call it before the editor, with the editor's id.

```ceesafe
import ceeui;

public fn undoButtons(ui: borrow mut Ui, undo: borrow TextHistory, editor: StringView) {
    ui.open(Style.row().gap(space(2.0)));
    if (Controls.button(mut ui, "undo", "Undo") && undo.canUndo()) {
        ui.dispatch("undo");
        ui.setFocus(hashId(editor)); // the editor keeps the focus, and acts on it next frame
    }
    if (Controls.button(mut ui, "redo", "Redo") && undo.canRedo()) {
        ui.dispatch("redo");
        ui.setFocus(hashId(editor));
    }
    ui.close();
}
```

A press outside an editor takes its focus away when the frame ends. `ui.setFocus` gives it back, so the command
reaches the editor.

### Wrapping

The editor wraps lines at the view's width when two things hold: `look` wraps at words, which is `Text`'s
default, and the document has `rows` for its lines. Up, Down and the page keys then move by rows on the screen.

For lines that do not wrap, give the look `TextWrap.None`:

```ceesafe frame
let wrapping = Text.of().size(15.0).leading(22.0);
let code = Text.of().mono().size(13.0).leading(18.0).wrap(TextWrap.None);
```

Lines break at UAX #14's opportunities, as a label's do. Only the lines in view are laid out and measured; the
rest count as one row until they are.

## The rich editor

`Controls.richEditor` edits text in several looks: bold, a colour, a monospaced face. A rich document has a mark
for each byte of its text. A mark is a number, an index into an array of looks that the frame gives the editor.

Make the document with `TextDocument.styled`, which takes the marks' buffer after the text's. The marks' buffer
is as long as the text's.

```ceesafe
import ceeui;

const MARK_PLAIN: u8 = 0;
const MARK_BOLD: u8 = 1;
const MARK_CODE: u8 = 2;

public struct Page {
    public text: [u8; 8192];
    public length: usize;
    public marks: [u8; 8192];
    public typing: u8;
    public starts: [u32; 512];
    public rows: [u16; 512];
    public history: [u8; 4096];
    public edit: TextEdit;
    public undo: TextHistory;
    public wrapWidth: f32;
    public goalX: f32;
}

public fn pageFrame<M: TextMetrics>(ui: borrow mut Ui, app: borrow mut Page, metrics: borrow M) {
    var doc = TextDocument.styled(
        mut app.text[..],
        app.length,
        mut app.marks[..],
        mut app.starts[..],
        mut app.rows[..],
        mut app.history[..],
    );
    doc.typing = app.typing;
    doc.edit = app.edit;
    doc.undo = app.undo;
    doc.wrapWidth = app.wrapWidth;
    doc.goalX = app.goalX;
    ui.open(Style.column().w(Size.grow()).h(Size.grow()).gap(space(2.0)));
    // A button for each mark: it marks the selection, or what is typed next.
    ui.open(Style.row().gap(space(2.0)));
    var wanted: u8? = null;
    if (Controls.button(mut ui, "bold", "Bold")) {
        wanted = MARK_BOLD;
    }
    if (Controls.button(mut ui, "code", "Code")) {
        wanted = MARK_CODE;
    }
    ui.close();
    match (wanted) {
        Some(mark) => {
            // The mark the selection already has takes it back to plain.
            let same = doc.selectionMark() == Some(mark);
            let _ = doc.setMark(if (same) MARK_PLAIN else mark);
            ui.setFocus(hashId("page")); // typing goes on where it was
        }
        null => {}
    }
    let plain = Text.of().size(15.0).color(SLATE_100);
    let looks = [plain, plain.bold().color(WHITE), plain.mono().color(AMBER_400)];
    let _ = Controls.richEditor(mut ui, metrics, "page", mut doc, plain, looks[..], false);
    ui.close();
    app.typing = doc.typing;
    app.length = doc.len;
    app.edit = doc.edit;
    app.undo = doc.undo;
    app.wrapWidth = doc.wrapWidth;
    app.goalX = doc.goalX;
}
```

`Controls.richEditor(mut ui, metrics, id, mut doc, look, looks, gutter)` draws each byte in `looks[mark]`. Mark 0
is drawn in `look`, as are marks past the end of `looks` or past 16. It returns `true` when the text or its marks
changed.

Every row is as tall as `look`'s line height, so keep the looks to `look`'s size: vary the weight, the colour
and the face.

### Marks

| Call | What it does |
|---|---|
| `doc.setMark(mark)` | Gives the selection `mark`. With nothing selected, typing takes `mark` from here |
| `doc.selectionMark()` | The mark all of the selection has, or `null` when it is mixed |
| `doc.markAt(offset)` | The mark of one byte |
| `doc.typing` | The mark typed and pasted text takes |

The typing mark follows the caret: when the caret moves, typing takes the mark of the byte before it. Edits keep
the marks in step with the text, and Undo and Redo bring marks back with the text. `setMark` on a selection can
be undone.

To save a rich document, save the text and the marks: `app.text[0..app.length]` and `app.marks[0..app.length]`.

A shortcut for a mark is an action of the app's, bound once and asked for in the frame:

```ceesafe frame
let bound = ui.bind("secondary-b", "mark-bold", ""); // once, on the first frame
if (ui.action("mark-bold")) {
    // doc.setMark(MARK_BOLD), as the button does
}
```

## The edit menu

`Controls.editMenu` is a context menu for text: Cut, Copy, Paste, Delete and Select All. A choice runs the
action its shortcut does, and the focused field or editor acts on it the next frame. It returns `true` when an
item was chosen.

The app opens it on a secondary press over the editor, gives the editor the focus, and keeps where it was
opened.

```ceesafe
import ceeui;

public struct EditMenu {
    public open: bool;
    public x: f32;
    public y: f32;
    /// Whether the editor's text had a selection: doc.edit.selects(), kept after the editor ran.
    public selects: bool;
}

/// Call last in the frame: the menu floats above what is declared before it.
public fn editMenuFrame(ui: borrow mut Ui, menu: borrow mut EditMenu, editor: StringView) {
    let p = ui.pointer();
    if (p.secondaryPressed && ui.hovered(editor)) {
        menu.open = true;
        menu.x = p.position.x;
        menu.y = p.position.y;
        ui.setFocus(hashId(editor)); // the choice acts on the editor
    }
    let _ = Controls.editMenu(mut ui, "editMenu", mut menu.open, menu.x, menu.y, menu.selects);
}
```

Cut, Copy and Delete are enabled when the last argument is true. Paste is enabled when the clipboard had text
when the host last looked (`ui.setClipboardHasText`). A host reads the clipboard for this on a secondary press,
not each frame.

[Controls](./controls#context-menu) describes context menus of your own.
