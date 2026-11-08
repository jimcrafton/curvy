# jitnodes

C++17 node based vector graphics editor

## Conventions
- camelCase for functions, variables, enumerators, and file names; PascalCase for type names.
- Member variables of classes have a `_` postfix (`pointCount_`). Plain aggregate structs keep bare field names.
- Keep code comments to a minimum so the code isn't overwhelmed; only comment non-obvious "why". Put documentation in `.md` files (`docs/`), not in source.
- Do not commit or push; the user makes all commits.
- After each implemented task, update `docs/project-tracker.xlsx` (Tracker sheet: status, notes, Verified; add a row for new work). Keep the Summary formulas and the Notes snapshot figures in step.
- always use the native file-editing tools instead of falling back to shell scripts or Python one-liners


## Build
`cmake --preset x64-release && cmake --build --preset x64-release && ctest --preset x64-release`
(on Windows run from a VS developer prompt so Ninja finds cl.exe).

## Layout
- `include/curvy/` public headers, `src/` implementation (core, geo, graph, jit).
- `docs/design.md` research notes and phased plan.
- `.claude/skills/` project skills: `html-mockup-to-newui` (HTML mockup to newui `.newui` UI document) and
  `newui-view-json5` (the `.newui` format plus schema lookup and validator scripts).

## Editor UI
The editor front end uses newui (`D:\code\newui`, `NEWUI_DIR` for the skill scripts); see "Editor UI" in
`docs/design.md`. UI screens are HTML mockups converted to `.newui` with the `html-mockup-to-newui` skill, never
hand-written first; validate generated files with `validateNewui.py`.
