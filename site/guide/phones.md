# Phones

This page says what changes when an app runs on iOS or Android: fingers in place of a pointer, the parts of the
screen the system covers, a small screen that turns, the soft keyboard, files, and an app the system may end.
The frame is the same one that runs on a desktop.

## Touch and gestures

A platform with a touch screen gives the `Ui` every finger (`Platform.nextTouch`). The `Ui` recognizes taps, double
taps, long presses, pans and pinches.

Controls made for a pointer keep working:

- The first finger presses, and its lift clicks.
- A pan or a pinch cancels the press.
- A long press is a secondary press.
- A pan scrolls the scroll container under it along its axis. Its release flings the container, which then slows.

An element of your own asks for a gesture by its id. `tapOn`, `longPressOn`, `panOn` and `pinchOn` give the gesture
that began on the element; `tap`, `longPress`, `pan` and `pinch` give it wherever it began.

```ceesafe frame
ui.open(Style.box().id("photo").w(Size.fixed(240.0)).h(Size.fixed(240.0)).bg(SLATE_800));
ui.close();
match (ui.tapOn("photo")) {
    Some(tap) => {
        if (tap.count == 2) {
            ui.label("Double tap", Text.of().color(SLATE_100));
        }
    }
    null => {}
}
```

[Input](./input) describes each gesture and its values.

## The safe area

The system covers parts of a phone's screen: the status bar, a sensor housing, the home indicator, rounded corners.
A platform says how far it covers the screen from each edge (`Platform.safeArea`). On a desktop the insets are zero.

The frame is still the whole screen. `ui.safeArea()` gives the insets as an `Insets` (`left`, `top`, `right`,
`bottom`, in points), and `Style.safe(insets)` adds them to an element's padding. The element's background then
reaches the screen's edges and its content stays clear.

```ceesafe frame
ui.open(Style.column().w(Size.grow()).h(Size.grow()).p(space(4.0)).bg(SLATE_950).safe(ui.safeArea()));
ui.label("Inbox", Text.of().size(24.0).color(SLATE_100));
ui.close();
```

Call `safe` after the padding it adds to.

| Call | What it gives |
|---|---|
| `ui.safeArea()` | The insets of all four edges |
| `insets.sides()` | The left and right insets alone: for a bar that reaches the screen's top or bottom edge itself |
| `insets.ends()` | The top and bottom insets alone |
| `Insets.zero()` | No insets |

## Size classes and responsive styles

`ui.widthClass()` and `ui.heightClass()` are `SizeClass.Compact` or `SizeClass.Regular`. A width below 600 points is
compact (a phone held upright). A height below 480 points is compact (a phone on its side). A rotation is a new
screen size, so the next frame sees the new classes.

```ceesafe frame
ui.open(Style.column().w(Size.grow()).h(Size.grow()));
if (ui.widthClass() == SizeClass.Compact) {
    ui.label("One column", Text.of());
} else {
    ui.label("Two columns", Text.of());
}
ui.close();
```

Styles are mobile first, as Tailwind's are. The base style is a phone's, and `sm`, `md`, `lg`, `xl` and `xxl` each
hold from their width up. This column of a phone becomes a row at 768 points:

```ceesafe frame
ui.open(Style.column().gap(space(2.0)).md(Style.of().flexRow().gap(space(4.0))));
ui.label("First", Text.of());
ui.label("Second", Text.of());
ui.close();
```

What is built only from a width up is an `if`:

```ceesafe frame
ui.open(Style.row().w(Size.grow()).h(Size.grow()));
if (ui.atLeast(Breakpoint.Md)) {
    ui.label("Sidebar", Text.of());
}
ui.label("Content", Text.of());
ui.close();
```

[Styles](./styles) has the breakpoints and the other variants. The pixel ratio is the window's scale, as on a
desktop.

## The soft keyboard

A text field's focus is what shows the soft keyboard. Typed text, an input method's composition, Backspace and
Enter reach the field as they do on a desktop.

`Platform.keyboardInset` says how much of the screen's bottom the keyboard covers, and `ui.keyboardInset()` gives it
to the frame. `ui.safeAreaWithKeyboard()` is the safe area with its bottom raised to the keyboard. A page padded by
it ends above the keyboard while one shows.

A focused field may say what kind of text it takes, which chooses the keyboard, and what the Return key says. Both
reports last one frame, so make them every frame the field has the focus.

```ceesafe
import ceeui;

public struct Login {
    public email: [u8; 64];
    public emailLength: usize;
}

public fn loginFrame(ui: borrow mut Ui, app: borrow mut Login) {
    ui.open(Style.column().w(Size.grow()).h(Size.grow()).p(space(4.0)).safe(ui.safeAreaWithKeyboard()));
    Controls.textInput(mut ui, "email", mut app.email[..], mut app.emailLength);
    if (ui.hasFocusNamed("email")) {
        ui.reportTextKind(TextKind.Email);
        ui.reportReturnKey(ReturnKey.Next);
    }
    ui.close();
}
```

| `TextKind` | The text a field takes |
|---|---|
| `Text` | Any text |
| `Number` | Whole numbers |
| `Decimal` | Numbers with a decimal separator |
| `Email` | An email address |
| `Url` | A URL |
| `Password` | The keyboard neither learns nor suggests what is typed |

| `ReturnKey` | What the key says |
|---|---|
| `Done` | Done. This is what a field says unless told otherwise |
| `Go` | Goes to what was typed: an address |
| `Next` | Moves to the next field |
| `Search` | Search |
| `Send` | Sends what was typed: a message |
| `System` | The system's own key ("return") |

The Return key's face changes nothing else: the key is Enter whichever it shows.

### Leaving a field

A click or a tap anywhere else leaves a text field, which on a phone puts the keyboard away.

- The field is left when the tap ends. What was tapped is still where it was, though the keyboard's going moves
  the page.
- The field is left at the frame's end (`Ui.leaveFocus`). A button that focuses the field again in the same frame,
  such as a chat's Send, keeps the keyboard without a blink.

## File pickers

A phone gives an app no path to write to. The demo's host handles its File menu in this way, with CeeSafe's
`FileDialog`:

- Open shows the system's document picker and gives a copy of the picked file.
- Save writes the file into a folder of the app's own, then exports it through the picker
  (`FileDialog.exportFile`).

## The app's life

The system may end an app that is off the screen. The demo's hosts save the app's state when it leaves the screen,
and start from that state:

- At `WindowAppEvent.Background` the host saves a snapshot into the app's own folder (`Directory.appData`). It is
  the same snapshot that [hot reload](./tools#hot-reload) uses, written with `SnapshotWriter`.
- At start the host reads the snapshot, so an app the system ended comes back on its page with its text.
- Back on the screen, the app lays out and draws its next frame whole.
- At a memory warning the desktop host lets go of the inspector's cached frame.

`ceeui-hosts/desktop/src/main.cee` shows all four.

## The GPU on a phone

The GPU host (`ceeui-hosts/gpu`) draws with Metal on an iPhone and with Vulkan on Android.

- Android gives the app another window each time it is back on the screen. The host attaches again when the
  window's handle changes, and presents nothing while there is none.
- The swapchain is the window's own size, unrotated, so a phone held sideways is drawn upright.
- It saves and restores its state as the desktop host does.
- The iOS Simulator's Metal cannot run it; an iPhone can. On an iPhone it needs ceesafec 0.62.1 or later.

## Packing

`ceeui-pack` makes an iOS `.app`, for the simulator or signed for a device, and an Android `.apk`, from an app's
package directory. [Packing](./tools#packing) has the commands and their options.
