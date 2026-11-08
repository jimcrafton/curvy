---
name: newui-gui-driver
description: Create or extend GUI tests for a newui (Win32/Blend2D) app using newui's automation server and the Python driver in tools/guitest - real SendInput plus a named-pipe query protocol. Use when asked to add a GUI/live-input test, drive the app from outside, set up automation in a new newui app, customize the driver, or add an automation command.
---

# Driving a newui app with the automation code

**This works ONLY with apps built on the newui framework** (they must link `newui_automation` and start it with `--automation <pipe>`). It cannot test a Win32, Qt, WPF or browser app: there is no automation server in them to answer. If the app is not newui, say so and stop; It is not a general-purpose Win32 test tool. Do not try to adapt this driver to it. What it relies on in newui (named views, reflection, self-reporting controls, `popupMenu`, `idle`, `screenshot`, the server): "What newui provides that makes this possible" in `D:\code\newui\automation.md`.

Two halves talk over a named pipe, one JSON object per line:

- **App side (newui):** `newui_automation` lib. Answers questions (where is view X, what does it show). Never injects input.
- **Driver side (`newui_guitest`, Python):** `D:\code\newui\tools\guitest`. Launches the app, sends real input with `SendInput`, asks the
  app for geometry/state. Standard library only; the protocol is the only coupling to newui.

Real input is the point: focus, capture, scrollbars, DPI and menus go through the real message pump. Do NOT fake it by calling
`onMouseDown()`/`onKeyDown()` in a unit test.

This is the reference copy; Claude Code loads it from `.claude\skills\` (see `D:\code\newui\tools\guitest\skill\README.md` to install it).

Docs: `D:\code\newui\automation.md` (protocol, how to write or customize a driver) and `D:\code\newui\tools\guitest\README.md` (driver API).
Digest of both: [reference.md](reference.md). A worked example: `D:\code\newui\demos\sketch\tests\gui\test_sketch_gui.py`.

## Safety first

GUI tests move the person's real mouse and type on their keyboard. **Build freely, but never run a test that sends input without an explicit
go-ahead**, and say which window must stay uncovered. A new app's first run is read-only:
`python -m newui_guitest check <exe> --view <name>` (run from `D:\code\newui\tools\guitest`). Use absolute paths in every command. Put temporary output (screenshots, logs) in the
scratchpad, not the repo.

## Steps for a new app

1. **App CMake** (gate it so shipping builds have none of it):
   ```cmake
   option(MYAPP_GUI_AUTOMATION "Build with newui's automation server" ON)
   if(MYAPP_GUI_AUTOMATION)
       target_link_libraries(myapp PRIVATE newui_automation)
       target_compile_definitions(myapp PRIVATE MYAPP_GUI_AUTOMATION)
   endif()
   ```
2. **App code**: parse `--automation <pipe>`; after the root view exists, before `app.run()`:
   ```cpp
   #ifdef MYAPP_GUI_AUTOMATION
   if (!automationPipe.empty()) {
       newui::Automation::expose(root);                         // each window a test may see
       newui::Automation::start(automationPipe, app.runLoop());
   }
   #endif
   // after app.run():  newui::Automation::stop();
   ```
   Every window/RootView a driver must reach needs its own `expose()` (e.g. per-tab editor roots).
3. **Name every view a test touches** (`setName()` or the name in the `.newui` file). Unnamed views cannot be found.
4. **Tests**: copy `templates/example_tests.py.in` to `<app>\tests\gui\test_<app>_gui.py`, fill in the exe path and view names. No driver
   to copy: the test puts `D:\code\newui\tools\guitest` on `sys.path`.
5. **Build the app, then run the tests directly** (`python D:\code\<app>\tests\gui\test_<app>_gui.py --exe <app.exe>`, or
   `cd D:\code\newui\tools\guitest; python -m newui_guitest test <file> --exe <app.exe>`); they are not part of the unit tests because they
   need a desktop nobody is using. Options: `--list` (starts nothing), `-k TEXT`, `--arg`, `--cwd`, `--no-guard`, `--quiet-period`, `--connect-timeout` ...
   (`python -m newui_guitest test --help`). Never set the exe through an environment variable or by editing the test.

## Writing a test

```python
from newui_guitest import AppTestCase

class FilterTests(AppTestCase):                    # the exe comes from --exe on the command line

    def test_typing_fills_the_filter(self):
        self.app.click("filterInput")                 # find by name -> covered-point check -> SendInput -> wait for idle
        self.app.type("Box")
        self.assertEqual("Box", self.app.text_of("filterInput"))
```

Rules that keep tests reliable:
- Wait with `idle` (every input call does), never `time.sleep` after input.
- `idle` = UI loop empty, NOT background work done. Poll with `self.app.wait_until(lambda: ..., "what never happened")`.
- Prefer `prop(name, property)` / `describe` (reflection) for plain state. Add a bespoke server command only for geometry or state reflection can't give.
- Scroll a control into view before clicking it if its scrolling parent clips it (`reveal(name, scroller)`): a half-clipped control ignores clicks.
- Native menus run a modal loop: use `open_menu` / `choose_menu(bar, "A", "B", "C")` / `close_menu`. A native file dialog is a separate window:
  `wait_for_dialog`, `type_raw`, `key_raw`.
- Probe screenshots with the colour the app really draws (a "dark pixel" test misses a mid-blue stroke).
- Focus decides who gets keys: after typing in a text box Ctrl+Z belongs to the box. Click the target first or use a menu.
- The middle of a selected object may be a handle (sketch's transform origin): grab objects off-centre.

## Customizing

No config file; see "Launching the tests" and "Customizing the driver for an app" in `automation.md`. In short: `--exe`/`--arg`/timeouts are command-line options; helper methods
for the app's vocabulary on that subclass; override `launch()` to prepare state; a test file ends with `sys.exit(newui_guitest.main())` so it takes those options itself; `app.ask("cmd", ...)` for
a server command with no wrapper; subclass `App` for behaviour every test needs. A change every app would want goes into `tools/guitest`
itself (with a case in `tools/guitest/tests/test_pure.py` when it needs no desktop).

## User-input guard

The driver waits for 1.5 s of user idle before injecting (up to 30 s, then raises `UserBusyError`) and aborts if outside input arrives mid-test.
"another window covers", "could not bring the app to the foreground", "input from outside" mean the person was active, not a bug: report it,
rerun when they are idle. Turn the guard off (`--no-guard`) only on a machine the person says is unattended.

## Adding an automation command (app side)

Commands are a fixed `if (cmd == "...")` chain in `Automation::handle()` (`src/automation.cpp`); there is no plugin registration. Write a
`fooReply(const Request&)`, add the branch, document it in the header comment and `automation.md`, add a headless case in
`unittests/test_automation.cpp`, then call it from tests with `app.ask("foo", ...)` (or add a wrapper to `App`). The newui repo is committed
by the user: never commit.

## Pitfalls seen

- Main window is owned by a hidden helper: find it as "visible + titled", not "no owner" (the driver does). The pipe opens before the frame shows.
- Take screenshots only after verifying nothing else covers the window. `screenshot` renders the RootView itself, so it cannot be occluded.
- A submenu is not open the moment its parent is clicked: poll until its label appears (`choose_menu` does).
- Security: the pipe is current-user ACL, reject-remote, first-instance; still use a random name per run and never ship a build with automation on.
- Never leave a mouse button held if a test throws (`drag` releases it).
