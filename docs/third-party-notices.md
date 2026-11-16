# Third-party notices (Python scripting)

The installed folder bundles the following for the Python scripting feature. Their licence texts are in this `licenses` folder.

| Component | Version | Licence | File |
|---|---|---|---|
| CPython (embedded interpreter, standard library) | 3.13.18 | Python Software Foundation License | `python-LICENSE.txt` |
| NumPy (read-only views of geometry attributes) | 2.5.5 | BSD-3-Clause, plus the licences of the libraries it bundles (OpenBLAS and others, listed inside the file and in `python/Lib/site-packages/numpy-2.5.5.dist-info/licenses`) | `numpy-LICENSE.txt` |
| pybind11 (the C++/Python binding; compiled into `curvy.exe`) | 3.1.0 | BSD-3-Clause | `pybind11-LICENSE.txt` |

Not yet listed here: the licences of the other libraries linked into the application (the jitnodes and newui dependencies, zlib, Blend2D and so
on). Add them before the first public release.

To address later (flagged 2026-11-16):
- Our own code (curvy, newui, jitnodes, lex) has no licence yet. Choose one before any public release.
- The Microsoft Visual C++ runtime DLLs next to `curvy.exe` (`concrt140`, `msvcp140*`, `vcruntime140*`) come from `VC\Redist\MSVC\<version>\x64\
  Microsoft.VC145.CRT`. The Visual Studio 2026 "Distributable Code" list allows redistributing any file under `VC\redist` except the
  `debug_nonredist` folders, unmodified, provided the person shipping holds a valid Visual Studio licence whose terms allow it (Community
  included, subject to its eligibility limits: check them against how curvy is distributed). Release builds only; never copy the debug runtime.
  They are shipped by the CMake install step (`InstallRequiredSystemLibraries`), so no extra file is needed; keep it that way.
- NumPy's `numpy.libs` holds OpenBLAS (BSD-3-Clause, LAPACK under BSD-3-Clause-Open-MPI, a statically linked GCC runtime under GPL-3.0 with the
  GCC Runtime Library Exception) and a renamed copy of Microsoft's `msvcp140.dll`. `numpy-LICENSE.txt` already carries all of those texts, which is
  what the licences require; keep that file with the install and mention OpenBLAS in the table above.
- The icons in `Resources/icons` and the other mockup-derived artwork were generated for this project (by Claude, in the mockup sessions), so they
  need no third-party notice.

When the bundled versions change (`3rdparty/python`, `3rdparty/pybind11`), update the table and the `numpy-<version>.dist-info` path in
`curvyInstallPython()` in `cmake/python.cmake`.
