---
name: newui-view-json5
description: Read, hand-write, or generate newui's JSON5 View/Frame persistence format (".newui" files written/read by Bundle + reflection::ObjectWriter/ObjectReader), including translating an HTML/CSS UI mockup into an equivalent .newui document. Use whenever asked to create, edit, inspect, or debug a saved View/Frame document, a "type": "..." reflection tree, delegate connections, the "animations" block in one of these files, or to turn an HTML mockup/wireframe into a newui View tree.
---

# newui View JSON5 persistence format

> Copied from the newui repo (`.claude/skills/newui-view-json5` in `D:\code\newui`) into jitnodes so the editor
> UI work has it at hand, then corrected against newui's current headers and reflectgen output (see "Tools" below
> and the corrections in `references/html-mockup-conversion.md`). When the two copies disagree, trust whatever
> `scripts/newuiSchema.py` and `scripts/validateNewui.py` report from the live newui checkout. jitnodes uses newui
> for the editor front end; `NEWUI_DIR` (default `D:\code\newui`) points the scripts at the checkout.

This document format is produced by `newui::reflection::ObjectWriter` and consumed by
`ObjectReader` (`include/newui/reflectionio.h`), driven entirely by the hand-rolled
reflection registry in `include/newui/reflection.h` (`Class`/`Property`/`Field`/
`Delegate`/`Enum`). `Bundle` (`include/newui/bundle.h`, `src/bundle.cpp`) is the
normal entry point apps use to load/save a `Frame`/`RootView`/`View` tree as a
`<name>.newui` file under the app's `Resources/` directory. The parser/writer
underneath is the vendored `3rdparty/json5` library, so files are genuine JSON5:
unquoted bare keys, double-quoted string values, `//` comments allowed, trailing
commas tolerated - but everything this skill describes is written in the plain
`key: value` style shown below; match that style when hand-editing or generating.

**Before trusting anything below against the live code**, remember reflectgen and
hand-written `ClassBuilder<T>` registration drift over time - grep the actual
`registerXReflection()` output or the class's header for the property names/types
you're about to rely on. See `[[project_reflectgen_generated_output_gotchas]]`-style
caution: verify, don't assume.

## Tools

Both scripts are stdlib-only Python and read newui's reflectgen output
(`<newui>/build/generated/newui_reflection_generated.cpp`, or `out/build/*/generated`; build the `newui` target
once to produce it). They answer "does this class have that property" from the code instead of from this
document, which drifts.

- `scripts/newuiSchema.py` - `--class Label` prints the constructibility, base chain and every registered
  property, field and collection; `--enum TextAlignment` prints an enum's values; no argument lists all classes.
  Class names drop the leading `newui::` (`gfx::Fill`); the `"type"` tag in a file uses the short name.
- `scripts/validateNewui.py FILE...` - checks a document: unknown `"type"`, classes reflection cannot construct,
  unknown property names (warning: the reader skips unknown keys), enum values, Color strings, missing
  `visible: true` on plain `SubView`/`RootView`, bounds shape, duplicate names, Frame without name/rootView.
  Exit status 1 on errors (`--strict`: also on warnings). Calibrated so newui's own `examples/*.newui` pass.
- `scripts/json5lite.py` - the small JSON5 reader/writer both scripts use.

Facts the scripts surfaced that this document used to get wrong or omit (verified 2026-10-16):

- `View` registers `bounds, visible, layoutIgnored, name, layout, layingOut, style, highlighted, focused,
  desiredSize, desiredSizeOverride, acceptsFocus, focusScope, wantsTabKey` plus the `childViews` collection;
  `clientBounds`, `origin` and `cursor` are no longer registered (older saved files still contain them; they are
  skipped on read). `SubView` adds `layoutParams`; `Control` adds `enabled` and `action`.
- `RootView` is not constructible by reflection; it only loads as the `rootView` of a `Frame`.
- `Slider` persists `value`, `integer`, `step`, `horizontal`, `showTicks` but **not** its range (`setRange` is a
  method; the range stays 0..100). `Progress.value` is 0..1. `Toggle` has no text property (pair it with a Label).
  `DropDownList`, `ListView` and `TreeView` take a `model` that a file cannot describe.
- `FlexLayoutParams` (a child's `layoutParams`) has `weight` (flex-grow) and `crossAxisAlignment`.

## Document shape

Every document written by `ObjectWriter` has exactly one `meta` object as a sibling
of the real payload, stamped once at the true root:

```json5
meta: {
  author: "",        // caller-settable (ObjectWriter::metadata.author), blank by default
  date: "2026-08-23", // ALWAYS overwritten by write() to today's date - never caller-set
  copyright: "",      // caller-settable
  version: "0.1.0.896" // ALWAYS overwritten by write() to newui::version() - never caller-set
}
```

Two possible root shapes follow `meta`:

1. **Single-instance root** (`ObjectWriter::write<T>()`, what `Bundle::writeFrame()`/
   `writeView()` use) - the payload's own properties sit directly at the document
   root, alongside `meta`, with a `"type"` key naming the root class:

   ```json5
   {
     meta: { ... },
     type: "Frame",
     title: "Overlay Example",
     name: "overl1ay1",
     bounds: { type: "Rect", ... },
     rootView: { type: "RootView", ... },   // see "Frame::rootView() is special" below
     animations: [ ... ]                     // see "animations block" below
   }
   ```

2. **Multi-object root** (`ObjectWriter::writeObjects()`/`ObjectReader::readObjects()`)
   - several independently-named top-level instances as siblings of `meta`, each its
     own tagged object, needed only when a `Delegate` connection crosses between two
     separately-named objects (resolving `"logger@DemoLogger.onPanelResized"` requires
     every named object in the file to already exist - a single-root document has
     nowhere to put a second top-level object):

   ```json5
   {
     meta: { ... },
     panel: { type: "DemoPanel", ... , delegates: { onResized: ["logger@DemoLogger.onPanelResized"] } },
     logger: { type: "DemoLogger" }
   }
   ```

Don't mix the two - a single document is either one tagged root or a flat set of
named siblings, never both.

## The `"type"` tag

Every nested reflected object gets a `"type"` key naming its class, written first
inside that object, from `Class::name()` (the exact string passed to
`ClassBuilder<T>` registration / what reflectgen emits - case-sensitive, must match
a class actually registered before `registerReflectionData()` runs). Two exceptions:

- The true document root and a collection element both use an **empty property
  name** internally, but still get a `"type"` tag once they're objects - the empty
  name only affects whether the object is written under a keyed slot vs. appended
  positionally to an array, not whether it's tagged.
- **Design-mode proxy substitution**: when the writer has `setDesignMode(true)` and
  the class has a registered `Class::proxyFor()` (e.g. a design-time `RootViewProxy`
  standing in for a real `RootView`), the written `"type"` is the *proxied* name
  (`"RootView"`), not the proxy's own class name - so a file saved from a
  design-time tree still loads as an ordinary tree in any app with no idea proxies
  exist.

A "grouping" object that isn't a reflected instance at all (the `"delegates"`
wrapper, the multi-object document root itself) gets **no** `"type"` tag.

## Scalar properties

- `std::string` -> JSON string.
- `bool` -> JSON `true`/`false`.
- Every integer width (int8/16/32/64, signed or not) and `float`/`double` -> a plain
  JSON number (json5's `value` is a double internally - don't expect integer vs.
  float formatting to mean anything on read).
- A **plain enum** -> its declared name as a string, e.g. `orientation: "Vertical"`,
  `kind: "Color"`. A value with no exact declared match falls back to a `"0x..."`
  hex token instead of failing.
- A **flags enum** (`EnumBuilder<T>::flags()` at registration time - a genuine,
  registration-time choice, not inferred from the C++ type) -> an array of the set
  bit names, e.g.:

  ```json5
  flags: ["Resizable", "AlwaysOnTop"]
  ```

  Unrecognized tokens on read are silently dropped (same "absent data reads back as
  default" contract as everything else here), not a load failure.

## Nested reflected objects (non-collection)

A property whose type is itself a registered `Class` (`Rect`, `Point`, `Size`,
`Color`, `Font`, `ViewStyle`/`Themed*ViewStyle`, `Layout` subclasses, `Fill`,
`Stroke`, `Gradient`, `ShapeStyle`, `Glow`, `DropShadow`, `Transform2D`,
`Cursor`, ...) is written as a nested keyed object with its own `"type"` tag,
recursively, e.g.:

```json5
bounds: {
  type: "Rect",
  pos: { type: "Point", x: 10, y: 10 },
  size: { type: "Size", width: 602, height: 144 }
}
```

A polymorphic slot (`Layout*`, `LayoutParams*`) always writes/reads the *real*
runtime subclass's own tag and property set (`"type": "FlexLayout"`, with
`FlexLayout`'s own properties), never the abstract base's.

A **get-only, non-addressable** nested value (e.g. `View::bounds()` returning
`const Rect&`) still round-trips fine - it's written from a throwaway copy, just
never edited in place on read the way an addressable one (`View::style()`,
backed by `unique_ptr`) is.

**Omitting a key is always safe now - it leaves whatever the target instance
already has untouched, for every property/field shape.** This is a real,
load-bearing guarantee (as of the 2026-09-13 read-path redesign - `HANDOFF.md`
Part 95), not an accident of any one property's own read() implementation:
`ObjectReader::readInto()` (`reflectionio.h`) drives the whole per-object walk
by iterating the *source document's own JSON5 keys* directly (via
`json5::object_view`), resolving each one against the target `Class` (`Class::
property()`/`field()`, both walk `parentClass()` so an inherited member
resolves too) and only ever calling that member's own `read()` for a key that
is genuinely present. A property/field never gets touched at all when its key
is absent - there is no "ask each registered member if it's there" step to
miss, no zero-value fallback path, nothing to skip. So: omitting
`crossAxisAlignment` leaves it at `Stretch`; omitting `opacity` leaves it at
`1.0`; omitting `style`/`layout` leaves the existing `ButtonStyle`/`FlexLayout`
alone, same as before. This closes the exact failure mode
`[[project_rect_redundant_property_bug]]` hit once for real (`Rect`'s flat
`left`/`top`/`width`/`height` clobbering `pos`/`size` when a document omitted
them) - generally now, not just via that one bug's own narrow `@reflect
ignore=true` fix.

**The guarantee is "whatever the target instance already has," not "whatever
value sounds like a sensible default" - those aren't always the same thing.**
Real, confirmed example (`HANDOFF.md` Part 96/97): `View::visible_` defaults to
`false` at the base class - omitting `visible` on a plain `SubView`/`RootView`
used as a layout container genuinely leaves it invisible (and its whole
subtree unpainted), because that container's own constructor never opts into
`true` the way `Label`/`Button`/`TextField`/every other `Control`-derived
widget's constructor does (each calls `setVisible(true)` itself). So: omitting
`visible` on a `Button`/`Label`/etc. is safe; omitting it on a bare
`SubView`/`RootView` is a real, easy-to-hit bug, not a safe default - always
write `visible: true` there explicitly. Check a class's own constructor (not
just its base class's field initializer) before assuming any property's
"default" is the value you actually want.

A resolved `"type"` tag that's unknown, or that doesn't derive from whatever's
statically expected at that slot, means that nested object simply can't be
processed - it's skipped (existing value, if any, untouched), never a fallback
to constructing the expected base type anyway. Same "can't be processed, don't
guess" contract at every level, top to bottom.

## Collections (`childViews`, `shapes`, `stops`, `points`, ...)

A `propertyCollection` (accessor-based, e.g. `View::childViews()`) is written as a
plain JSON5 array of tagged objects (or scalars, for a non-object element type),
positionally - array elements never get a key name of their own:

```json5
childViews: [
  { type: "Label", text: "Press H for help", ... },
  { type: "SubView", ... , childViews: [ { type: "Button", ... }, ... ] }
]
```

**Read is add-only, not replace.** `ObjectReader` calls the real `addChild()`
(or the collection's registered `add=`) for every element it reads, on top of
whatever the target already has - reading a `childViews` array into an
already-populated live tree duplicates children rather than replacing them. Only
safe to read into a freshly `createInstance()`'d (empty) tree, or one you know is
empty. This is the single most common footgun when writing code that reloads a
`.newui` file into a tree that's already been built once.

**A property that's also a collection element is written once, not twice.** If a
plain property's live value is the *same object* as one of the class's own
collection entries (e.g. `Slider::thumb()`, which is both a real property and
already present in `childViews` via `addChild()`), the writer detects the address
match and skips writing it as its own named key - it still round-trips correctly
via the collection element on read. Don't hand-add a duplicate `thumb: {...}` key
next to `childViews` - it isn't how the real writer represents this, and isn't
needed.

## Cycles / back-references

A getter that can walk back up the tree it's part of (`View::parent()`,
`View::rootView()`) must be registered `@reflect ignore=true` (or hand-annotated
equivalently) - it's simply never written/read as a property at all, not
represented in the file in any form. `ObjectWriter`/`ObjectReader` also carry a
runtime `enterInstance()`/`exitInstance()` guard keyed on `(Class*, instancePtr)`
that skips re-entering an object already open somewhere up the current
write()/read() chain, as defense in depth - but the real fix for a genuine
back-reference property is the `ignore=true` annotation, since the guard alone
doesn't cover every case (see reflectgen's own notes on `PtrGetter` self-refs).

## Delegates

A `Delegate<SenderT, Args...>` member with at least one *describable* connected
listener gets one `"delegates"` object, written as a sibling of the owning
object's own properties (present only when at least one delegate has something
to describe - entirely absent otherwise, not an empty `{}`):

```json5
delegates: {
  onResized: [
    "logger@DemoLogger.onPanelResized"
  ]
}
```

Each entry is a `"<targetObjectName>@<ClassName>.<methodName>"` string. Resolving
one back on read requires the target object to exist as a **named top-level
object in the same document** - this only works inside a multi-object document
(`writeObjects()`/`readObjects()`), which resolves it in two passes: construct
every named object first, then reconnect delegates. A free-function listener
(no `@`) is logged and skipped on read - reconnecting those isn't supported.
`<ClassName>` is informational only; a mismatch against the target's real class
just logs a warning and connects anyway using the target's actual class.

## `Frame::rootView()` is special

`Frame`'s `rootView` doesn't round-trip as an ordinary property (it would
recurse/needs different get-only-vs-owned handling) - it's written/read via
`ObjectWriter::writeNested()`/`ObjectReader::readNested()` as an explicit
`"rootView"` key at the document root, one level below the true root object. This
is why `Bundle::writeRootView()`/`loadRootView()` exist as a *separate*, narrower
path (touching only the `"rootView"` key, preserving every other top-level key an
existing file already has) distinct from `writeFrame()`/`loadFrame()` (which
always (re)writes the whole `Frame`, including `title`/`bounds`/`name`).

## The `animations` block

Unlike everything else in the file, this block is **hand-written by `Bundle`**
(`writeAnimations()`/`loadAnimations()` in `src/bundle.cpp`), not produced through
the ordinary `Class`/`Property` walk - `AnimationManager`'s animations aren't
reachable as a reflected property of `Frame`/`RootView` at all. Shape:

```json5
animations: [
  {
    name: "sliderPulse",
    startTime: 0,
    duration: 30,
    looping: false,
    keys: [
      {
        name: "start",
        keyFrame: 0,
        values: [
          { target: "childViews[2]/childViews[1]@value", value: 100, interpolation: "Linear" }
        ]
      },
      {
        name: "end",
        keyFrame: 30,
        values: [
          { target: "childViews[2]/childViews[1]@value", value: 100, interpolation: "EaseOut" }
        ]
      }
    ]
  }
]
```

`target` is `"<view-path>@<propertyName>"`, where `<view-path>` is
`computeViewPath()`'s own `childViews[i]/childViews[j]/...` index-chain syntax
resolved from the owning `Frame`'s `rootView` (empty view-path, i.e. just
`"@propertyName"`, means the target property lives directly on the root view
itself) - resolved back via `resolveViewPath()` (`include/newui/viewpath.h`)
against the *already-loaded* `rootView` tree, so `loadAnimations()` only works
after the view tree it references already exists. This block is optional; a file
with no `"animations"` key simply has none.

## Minimal hand-written examples

A standalone `SubView` (`Bundle::writeView()`/`loadView()` shape - `View::write()`
writes exactly the base `View` property set, no `Frame` wrapper):

```json5
{
  meta: { author: "", date: "2026-09-13", copyright: "", version: "0.1.0.900" },
  type: "SubView",
  bounds: { type: "Rect", pos: { type: "Point", x: 0, y: 0 }, size: { type: "Size", width: 100, height: 24 } },
  visible: true,
  name: "myPanel",
  style: { type: "ViewStyle", borderWidth: 0, strokeStyle: "StrokeColor", rectRadius: 0,
           opacity: 1, compositingOp: "CompSrcOver",
           font: { type: "Font", name: "", size: 12, bold: false, italic: false, strikeThrough: false, underlined: false } },
  childViews: []
}
```

`ViewStyle`'s real background/highlight properties are `backgroundFill`/`highlightFill` (a full
`Fill` object - `kind`/`color`/`gradient`/`imagePath`/`opacity`, or a plain `Color` for
`highlightFill`) - **not** `bkgFillStyle`/`hilightFillStyle` (an older schema; an example file
written before 2026-09-13 may still show those bare-enum names, now stale and silently ignored on
read). Usually best omitted entirely: `RootView` already initializes its own `backgroundFill` to a
sensible, theme-tracking default (`UIColorRole::WindowBackground`, `HANDOFF.md` Part 96) - a mid-
tree `SubView`/`Label` should almost always stay `PaintKind::None` (transparent) rather than
opaquely repainting over its parent.

`Color` is one of a small number of classes registered with `// @reflect stringvalue`
(`color.h`/`reflection.h` - see `tools/reflectgen/README.md`'s own section on that annotation),
so **as of 2026-09-15** a `Color`-typed property (`highlightFill`, `textColor`, `boxFill`, ...) can
be written directly as a plain string in a `.newui` file instead of the usual nested
`{type:"Color", r, g, b, a}` object - any of `Color::fromString()`'s own three forms work: a hex
value (`"#ff0000ff"`), a CSS color name (`"cornflowerblue"`), or a live `UIColorRole` member name
("WindowBackground", "ControlText", "LinkText", ...) resolved via `UIColorManager::colorFor()` at
read time. An old file written with the nested-object form still reads correctly either way - the
reader accepts both shapes, it isn't a schema migration. `backgroundFill`/`highlightFill` are
`gfx::Fill`, not `Color`, directly - `gfx::Fill` itself isn't `stringvalue`-registered, but its own
`color` sub-property is a plain `Color`, so *that* can use the compact string form the same way.

**Prefer a `UIColorRole` name over a hex/rgba literal for a standard control's colors.** A plain
hex/CSS value (`"#3c3c3c"`, `"gray"`) freezes to that exact color forever - it never adapts when
the OS Light/Dark mode setting changes, unlike a native `Themed*ViewStyle`-drawn control, which
already tracks that live via uxtheme. Writing `"#2b2b2b"` for, say, a `Label`'s `textColor` on an
otherwise ordinary panel looks fine in whichever mode the file was authored under, then reads as
low-contrast or plain wrong the next time the user's system is in the other mode. Use one of
`UIColorRole`'s own member names instead (`uicolormanager.h`) wherever the intent is "the ordinary
themed control color", not "this exact fixed color regardless of theme":

- `WindowBackground` / `WindowText` - a plain panel's fill/text.
- `ControlBackground` / `ControlText` / `ControlBorder` - an app-drawn control that's meant to look
  native (a custom button, a card, ...).
- `DisabledText` - a disabled control's label.
- `HighlightBackground` / `HighlightText` - a selection/active-state fill and its text, already
  tracking the user's real Windows accent color (`UIColorManager::colorFor()`'s own comment).
- `LinkText` / `LinkHoverText` - a hyperlink-style `Label` (`setHotLink(true)`).

e.g. `textColor: "WindowText"` rather than `textColor: "#000000ff"`. Reach for a literal hex/CSS
value only when the design genuinely calls for a fixed, theme-independent color - a brand color, a
status/severity color (an error-red toast, a success-green badge), or anything else that should
stay the same regardless of the user's Light/Dark setting. When unsure which a given property
wants, omitting the property entirely and letting the control's own constructor-set default apply
(same reasoning as `backgroundFill` above) is usually safer than guessing at either form.

A minimal `Frame` + `RootView` (`Bundle::writeFrame()`/`loadFrame()` shape):

```json5
{
  meta: { author: "", date: "2026-09-13", copyright: "", version: "0.1.0.900" },
  type: "Frame",
  title: "My Window",
  name: "mywindow",
  bounds: { type: "Rect", pos: { type: "Point", x: 10, y: 10 }, size: { type: "Size", width: 400, height: 300 } },
  rootView: {
    type: "RootView",
    bounds: { type: "Rect", pos: { type: "Point", x: 0, y: 0 }, size: { type: "Size", width: 400, height: 300 } },
    visible: true,
    name: "rootview",
    childViews: []
  }
}
```

Every `View`/`SubView`/`RootView` node's common property set (from `View`'s own
registration) is: `bounds`, `visible`, `name`, `layout` (omit to keep whatever the
live default/constructor already set), `style`, `highlighted`, `clientBounds`,
`desiredSize`, `desiredSizeOverride`, `origin`, `cursor`, `childViews`. A concrete
widget (`Label`, `Button`, `Slider`, ...) adds its own properties interleaved in
whatever order `ClassBuilder<T>` registered them - don't assume a fixed position,
look them up by key on read regardless of source-file ordering.

## Editing/generating checklist

- `"type"` must be a name actually registered with `newui::reflection` before
  `registerReflectionData()` (generated by `reflectgen`) runs in the consuming
  process - see `[[project_reflection_registration_trap]]`. An unknown `"type"`
  on a collection element or fresh-construct path just fails to construct that
  node (returns nullptr / element skipped), not a hard parse error.
- Property key names are exactly what `reflectgen`'s naming-convention stripping
  produces (`getTitle()`/`title()` -> `title`, `isActive()` -> `active`... but
  verify against the actual generated `.cpp` or `@reflect property=...` override
  rather than guessing from the getter name alone).
- You only need to write properties whose value actually differs from what a
  freshly-constructed instance already has - see "Nested reflected objects"
  above for why omission is now a general, guaranteed contract, not a
  per-property special case. Handy for hand-written/generated fixtures: a
  minimal node just needs `"type"` plus whatever you actually care about.
- Collections are the one exception - there's still no way to write "explicitly
  empty" as distinct from "not mentioned" (reading is purely additive; an empty
  array adds zero elements but doesn't clear anything already there either).
  Don't rely on an empty `childViews: []` to clear pre-existing children.
- A nested tagged object that *is* present must have a valid `"type"` (or that
  whole node is skipped, not partially applied) - but its own properties can be
  just as sparse as any other object's, same omission rule all the way down.
- Never write the same live object under two different keys (see "Collections"
  above) - the real writer never does, and a reader has no dedup logic for that
  on its side.
- Booleans are `true`/`false` (bare, unquoted); enums/flags are quoted strings;
  everything else follows ordinary JSON5.
- If you need to round-trip a document programmatically instead of hand-editing,
  use `newui::reflection::ObjectWriter`/`ObjectReader` directly (`write<T>()`/
  `read<T>()`/`readNew<BaseT>()`/`writeNested()`/`readNested()`/`writeObjects()`/
  `readObjects()`) or go through `Bundle` (`loadFrame`/`writeFrame`/`loadView`/
  `writeView`/`loadRootView`/`writeRootView`) rather than reimplementing this
  walk - `include/newui/reflectionio.h` is the source of truth for exact
  behavior, and it's short enough to read end-to-end when in doubt.

## Generating a `.newui` document from an HTML mockup

Translating an HTML/CSS mockup (a wireframe, a design tool's export, a static prototype page) into
an equivalent `.newui` View tree is a distinct enough sub-task - its own element/CSS mapping
tables, color/font conversion notes, a worked example, and an honest list of what doesn't
translate (CSS Grid persistence, per-corner radius, box-shadow, native button chrome, ...) - that
it lives in its own file: **read `references/html-mockup-conversion.md` before attempting this**.
Everything above (document shape, `"type"` tagging, scalar/nested-object encoding, the omission
rule, collections) still applies there unchanged; that reference only adds the HTML-specific
mapping on top.

## Where to look in the real code

- `include/newui/reflection.h` - `Class`/`Property`/`Field`/`Delegate`/`Enum`
  registry, `TypedClass<T>::write()` (the write-side per-object walk - the
  read side is driven by `ObjectReader::readInto()` instead, `reflectionio.h`,
  since `HANDOFF.md` Part 95's redesign; `Class` has no `read()` any more),
  `Property::writeValue()`/`readValue()` (scalar/enum dispatch, in
  `src/reflection.cpp`).
- `include/newui/reflectionio.h` - `ObjectWriter`/`ObjectReader` (the JSON5
  binding of the above onto `3rdparty/json5`) - `ObjectReader::readInto()` is
  the actual read-side driver as of Part 95.
- `include/newui/bundle.h` / `src/bundle.cpp` - the app-facing load/save API,
  including the hand-written `animations` block and view-path target resolution.
- `include/newui/layout.h` / `include/newui/controls.h` - `Layout` subclasses
  and concrete widgets, respectively (see `references/html-mockup-conversion.md`
  for the mapping tables built on these).
- `tools/reflectgen/README.md` + `cpp_naming_conventions.md` (repo root) - how a
  C++ class's getters/setters/collections turn into the properties this format
  actually serializes.
