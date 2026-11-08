# Reference

Sources of truth: `D:\code\newui\automation.md` (protocol v1, writing/customizing a driver) and `D:\code\newui\tools\guitest\README.md`
(driver API). This is a digest.

## Protocol commands (one JSON line each way; reply has `"ok"`, request `"id"` is echoed)

| Command | Use |
|---|---|
| `hello` | `protocol` version; send first, refuse unknown versions |
| `findView name` | screen rect `x,y,w,h` + `visible` |
| `focused` | `name`, `type` of focused view |
| `getText name` | text of a `TextControl` |
| `describe name` / `get name property` | reflected properties (bool/number/string) |
| `scrollInfo name` | ScrollView bars: `vValue/vMax/vPage/vVisible`, thumb rect `vx vy vw vh` (same with `h`) |
| `segment name index` | SegmentedControl: `count`, `selected`, `label`, rect |
| `tree name limit` | TreeView rows: `depth,text,expanded,hasChildren,x,y,w,h` |
| `caret name` | `line`, `column` (1-based), `offset` |
| `menuBar name` | top-level menu button rects |
| `popupMenu` | open native popup's items (answered off the UI thread, works inside a modal menu) |
| `screenshot name path` | RootView to `.bmp` |
| `idle` | replies when the run loop is empty (cannot answer while a native menu is open) |

## Driver API (`newui_guitest.App`, Python)

- Lifetime: `App(exe, args=(), cwd=None)`, `close()`, `running`, context manager. Adds `--automation <random pipe>`; kills the process on close.
- Raw: `ask(cmd, check=True, timeout=None, **fields) -> dict` (raises `AutomationError` if `ok` is false).
- Queries: `wait_idle`, `find -> Rect`, `text_of`, `focused_name`, `prop`, `describe`, `scroll_info`, `segment`, `caret`, `tree`, `find_row`,
  `screenshot(root) -> ScreenShot`, `wait_until(condition, what, timeout)`.
- Input (each ends in `wait_idle`): `click`, `double_click`, `click_at(point, to_front, settle, button)`, `type`, `key("ctrl+shift+z")`,
  `wheel(target, notches)` (+ = up), `drag(start, end, steps)`, `drag_by(name, dx, dy)`.
- Controls: `click_segment`, `click_row`, `double_click_row`, `toggle_row`, `reveal`.
- Menus: `open_menu`, `choose_menu(bar, *path)`, `close_menu`. Dialogs: `wait_for_dialog`, `type_raw`, `key_raw`.
- `AppTestCase`: `self.app` per test; the exe comes from `--exe` (a class may name a default `exe`/`args`); override `launch()` to customize startup.
- Command line: `python -m newui_guitest check <exe> [--view N]` (read-only); `python -m newui_guitest test <files|folders|names> --exe PATH [--arg A] [--cwd D] [-k T] [--list] [-f] [-q] [--no-guard] [--quiet-period S] [--quiet-timeout S] [--connect-timeout S] [--reply-timeout S]`; a test file ends with `sys.exit(newui_guitest.main())`.
- `ScreenShot`: `pixel` (screen coords), `count`, `changed`; `differs`, `dark`.
- Tunables (class attributes, set by the options above): `CONNECT_TIMEOUT` 20 s, `REPLY_TIMEOUT` 30 s, `QUIET_PERIOD` 1.5 s, `QUIET_TIMEOUT` 30 s, `user_input_guard`.
- Errors: `GuiTestError` > `AutomationError`, `UserBusyError`; messages say why (no such view, covered, no foreground...).

## Not yet covered (ideas)

DPI round trip, IME/key repeat, capture/drag beyond scrollbars, focus after popup close, VS-hosted editors (needs UI Automation or the vs-debug tools).
