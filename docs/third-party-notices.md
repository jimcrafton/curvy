# Third-party notices (Python scripting)

The installed folder bundles the following for the Python scripting feature. Their licence texts are in this `licenses` folder.

| Component | Version | Licence | File |
|---|---|---|---|
| CPython (embedded interpreter, standard library) | 3.13.18 | Python Software Foundation License | `python-LICENSE.txt` |
| NumPy (read-only views of geometry attributes) | 2.5.5 | BSD-3-Clause, plus the licences of the libraries it bundles (OpenBLAS and others, listed inside the file and in `python/Lib/site-packages/numpy-2.5.5.dist-info/licenses`) | `numpy-LICENSE.txt` |
| pybind11 (the C++/Python binding; compiled into `curvy.exe`) | 3.1.0 | BSD-3-Clause | `pybind11-LICENSE.txt` |

Not yet listed here: the licences of the other libraries linked into the application (the jitnodes and newui dependencies, zlib, Blend2D and so
on). Add them before the first public release.

When the bundled versions change (`3rdparty/python`, `3rdparty/pybind11`), update the table and the `numpy-<version>.dist-info` path in
`curvyInstallPython()` in `cmake/python.cmake`.
