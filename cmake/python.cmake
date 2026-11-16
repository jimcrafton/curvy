# Embedded Python scripting: jitnodes' Python bindings (jitnodesPython) built against the bundled
# Python in 3rdparty/python, from jitnodes' own python/CMakeLists.txt on top of the jitnodes target
# that cmake/jitnodes.cmake already made.
set(JITNODES_NEWUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/newui)
set(JITNODES_JSON5_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/newui/3rdparty/json5/include)
set(JITNODES_PYTHON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/python)
set(JITNODES_PYBIND11_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/pybind11)
set(JITNODES_BUILD_TESTS OFF)

# EXCLUDE_FROM_ALL: only jitnodesPython (linked by curvy_core) is built, not the standalone jitnodes.pyd.
add_subdirectory(${JITNODES_DIR}/python ${CMAKE_CURRENT_BINARY_DIR}/jitnodesPython EXCLUDE_FROM_ALL)

# The runtime starts from this folder; its DLLs are copied next to each executable (see curvyCopyPython).
set(CURVY_PYTHON_HOME ${JITNODES_PYTHON_DIR})

# Install (cmake --install <build dir> --prefix <folder>): the bundled Python goes to <prefix>/python, where findPythonHome() looks first,
# without what an embedded interpreter never uses (tests, IDLE, tkinter and Tcl/Tk, pip, headers, import libraries, launchers).
# The DLL goes next to the executable.
function(curvyInstallPython)
    install(DIRECTORY ${CURVY_PYTHON_HOME}/ DESTINATION python
        PATTERN "test" EXCLUDE
        PATTERN "tests" EXCLUDE
        PATTERN "idlelib" EXCLUDE
        PATTERN "tkinter" EXCLUDE
        PATTERN "turtledemo" EXCLUDE
        PATTERN "ensurepip" EXCLUDE
        PATTERN "venv" EXCLUDE
        PATTERN "lib2to3" EXCLUDE
        PATTERN "pydoc_data" EXCLUDE
        PATTERN "pip" EXCLUDE
        PATTERN "pip-*" EXCLUDE
        PATTERN "bin" EXCLUDE
        PATTERN "Scripts" EXCLUDE
        PATTERN "include" EXCLUDE
        PATTERN "libs" EXCLUDE
        PATTERN "tcl" EXCLUDE
        PATTERN "python.exe" EXCLUDE
        PATTERN "pythonw.exe" EXCLUDE
        PATTERN "python3*.dll" EXCLUDE
        PATTERN "vcruntime*.dll" EXCLUDE
        PATTERN "_test*.pyd" EXCLUDE
        PATTERN "_tkinter.pyd" EXCLUDE
        PATTERN "tcl*.dll" EXCLUDE
        PATTERN "tk*.dll" EXCLUDE)
    install(FILES ${CURVY_PYTHON_HOME}/python313.dll ${CURVY_PYTHON_HOME}/python3.dll DESTINATION .)
    # Licence texts of what the bundle carries, in one visible folder (NumPy's also stays in its dist-info, with its bundled libraries').
    install(FILES ${CURVY_PYTHON_HOME}/LICENSE.txt DESTINATION licenses RENAME python-LICENSE.txt)
    install(FILES ${CURVY_PYTHON_HOME}/Lib/site-packages/numpy-2.5.5.dist-info/licenses/LICENSE.txt DESTINATION licenses RENAME numpy-LICENSE.txt)
    install(FILES ${CMAKE_SOURCE_DIR}/3rdparty/pybind11/LICENSE DESTINATION licenses RENAME pybind11-LICENSE.txt)
    install(FILES ${CMAKE_SOURCE_DIR}/docs/third-party-notices.md DESTINATION licenses)
endfunction()

function(curvyCopyPython target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CURVY_PYTHON_HOME}/python313.dll ${CURVY_PYTHON_HOME}/python3.dll $<TARGET_FILE_DIR:${target}>
        COMMENT "Copying the Python runtime DLLs")
endfunction()
