cmake_minimum_required(VERSION 3.24)
project(curvy VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

option(CURVY_GUI_AUTOMATION "Build curvy with newui's automation server" ON)

# Fetch the submodules on first configure (a fresh clone or worktree has them empty).
find_package(Git QUIET)
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/3rdparty/newui/CMakeLists.txt" OR NOT EXISTS "${CMAKE_SOURCE_DIR}/3rdparty/jitnodes/CMakeLists.txt")
    if(NOT GIT_FOUND)
        message(FATAL_ERROR "3rdparty submodules are missing and git was not found: run 'git submodule update --init --recursive'")
    endif()
    message(STATUS "Fetching submodules (git submodule update --init --recursive)")
    execute_process(COMMAND "${GIT_EXECUTABLE}" submodule update --init --recursive
                    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" RESULT_VARIABLE CURVY_SUBMODULE_RESULT)
    if(NOT CURVY_SUBMODULE_RESULT EQUAL 0)
        message(FATAL_ERROR "git submodule update failed (${CURVY_SUBMODULE_RESULT})")
    endif()
endif()

# newui is a full application framework; only the libraries curvy links are built from it.
# It also provides the one asmjit (embedded in Blend2D) and the json5 header that jitnodes uses.
add_subdirectory(3rdparty/newui EXCLUDE_FROM_ALL)

include(cmake/jitnodes.cmake)

# Everything but main() is one library, shared by the app and its unit tests.
add_library(curvy_core STATIC
    src/canvasview.cpp
    src/geometrytable.cpp
    src/colorpickercard.cpp
    src/colorwheel.cpp
    src/graphcrumbs.cpp
    src/swatchpalettes.cpp
    src/swatchstrip.cpp
    src/swatchpalettecontroller.cpp
    src/graphpanel.cpp
    src/graphoverviews.cpp
    src/numberscrubber.cpp
    src/graphview.cpp
    src/pointtable.cpp
    src/sketchnodes.cpp
    src/rendernode.cpp
    src/radialmenu.cpp
    src/workspacemodel.cpp
    src/newdocumentmodel.cpp
    src/newdocumentdialog.cpp
    src/pagepreview.cpp
    src/workspacecontroller.cpp
    src/dockcontroller.cpp
    src/settings.cpp
    src/settingsdialog.cpp
    src/graphdocument.cpp
    src/appearancenode.cpp
    src/objectnode.cpp
    src/sketchobjects.cpp
    src/sketchdocument.cpp
    src/backgroundtask.cpp
)

target_include_directories(curvy_core PUBLIC include)
target_include_directories(curvy_core PRIVATE src)
target_compile_definitions(curvy_core PUBLIC CURVY_VERSION="${PROJECT_VERSION}")
target_link_libraries(curvy_core PUBLIC newui jitnodes::jitnodes zlib_static windowscodecs ole32)

if(CURVY_GUI_AUTOMATION)
    target_link_libraries(curvy_core PUBLIC newui_automation)
    target_compile_definitions(curvy_core PUBLIC CURVY_GUI_AUTOMATION)
endif()

add_executable(curvy src/main.cpp)
target_link_libraries(curvy PRIVATE curvy_core)

# newui_add_reflectgen_output() reads these from the scope ReflectGen.cmake was included in (newui's).
set(NEWUI_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/newui)
set(NEWUI_REFLECTGEN_DIR ${NEWUI_ROOT}/tools/reflectgen)
set(NEWUI_REFLECTGEN_PYTHON ${NEWUI_REFLECTGEN_DIR}/.venv/Scripts/python.exe)
set(NEWUI_REFLECTGEN_SCAN_DIRS ${NEWUI_ROOT}/include/newui)
set(NEWUI_REFLECTGEN_BASE_INCLUDE_DIRS
    ${NEWUI_ROOT}/include
    ${CMAKE_BINARY_DIR}/3rdparty/newui/include
    ${NEWUI_ROOT}/3rdparty/json5/include
    ${NEWUI_ROOT}/3rdparty/blend2d)

# Generates registerCurvyReflection() for curvy's reflected classes (CanvasView, GradientBar, ...) so
# curvy.newui can name them as "type"; main() calls it after newui's own registerReflectionData().
# Only the headers directly in include/curvy are scanned (the io/ subfolder is left out); reflectgen takes files as well as directories.
file(GLOB CURVY_REFLECT_HEADERS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/include/curvy/*.h)
newui_add_reflectgen_output(curvy_core
    SCAN_DIRS ${CURVY_REFLECT_HEADERS}
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/jitnodes/include
    REGISTER_FUNCTION registerCurvyReflection)
# The function only tracks headers it finds by globbing a directory; the files above are not one, so say what the output depends on.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/curvy_core_reflection_generated.cpp APPEND DEPENDS ${CURVY_REFLECT_HEADERS})

newui_add_resources(curvy "${CMAKE_CURRENT_SOURCE_DIR}/Resources")

# Unit tests: GoogleTest only, run curvyTests directly (no ctest). The window layouts load from Resources next to the exe.
file(GLOB CURVY_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)
add_executable(curvyTests ${CURVY_TEST_SOURCES})
target_include_directories(curvyTests PRIVATE src)
target_link_libraries(curvyTests PRIVATE curvy_core GTest::gtest_main)
newui_add_resources(curvyTests "${CMAKE_CURRENT_SOURCE_DIR}/Resources")
