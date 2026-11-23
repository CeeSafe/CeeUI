# __NAME__

A [CeeUI](https://github.com/CeeSafe/CeeUI) app, made by `ceeui-pack new`: a list of tasks to add to, tick
off and delete, in a window of its own.

```sh
ceesafec fetch          # the CeeUI packages it depends on
ceesafec build          # out/__NAME__
out/__NAME__
```

- `src/app.cee` is the app: its state (`App`) and the frame that lays out the window from it (`appFrame`). CeeUI
  is immediate mode: the frame runs whenever something changes, and the controls say what the user did.
- `src/main.cee` opens the window, sets aside the UI's and the renderer's memory, and runs the frames.

Pack it as an app a system installs, from this directory, with CeeUI's `ceeui-pack`:
`ceeui-pack macos`, `ios-simulator`, `ios`, `android` or `web`.

While it runs, secondary-Alt-I (Command-Option-I on macOS) turns on CeeUI's layout inspector.
