# curvy design notes

## Status
- `src/` and `include/curvy/` are the sketch demo (newui-demos/demos/sketch) ported as-is: namespace `curvy`, flat file names kept
  (`sketchdocument.h`, ...), `Resources/curvy.newui` is the old sketch layout.
- `3rdparty/newui` and `3rdparty/jitnodes` are git submodules.

## Build notes
- The project is C++17 (newui's `delegate.h` does not compile as C++20). jitnodes needs C++20 (`std::bit_cast`), so `cmake/jitnodes.cmake`
  builds it as its own C++20 targets; curvy's C++17 sources cannot include `<jitnodes/...>` directly. Graph integration code therefore
  goes in a separate C++20 library with a C++17-clean public header.
- jitnodes' CMakeLists is not used: Blend2D embeds asmjit under a private ABI namespace, so jitnodes gets its own asmjit static build.
- reflectgen needs `3rdparty/newui/tools/reflectgen/.venv` (`python -m venv .venv`, `pip install -r requirements.txt`).

## Phases
1. Port the sketch demo and build it (done).
2. Introduce a C++20 engine library wrapping a jitnodes `Network`; cook artwork objects from it.
3. Map the document model (paths, shapes, strokes) onto graph nodes; hide the graph in the default UI, expose it in advanced mode.
4. Skeletal strokes (see `Skel Strokes.docx`).
5. Replace the sketch UI with new mockups converted through the `html-mockup-to-newui` skill.
