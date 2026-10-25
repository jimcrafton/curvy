cmake_minimum_required(VERSION 3.24)
project(curvy VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

option(CURVY_GUI_AUTOMATION "Build curvy with newui's automation server" ON)

# newui is a full application framework; only the libraries curvy links are built from it.
# It also provides the one asmjit (embedded in Blend2D) and the json5 header that jitnodes uses.
add_subdirectory(3rdparty/newui EXCLUDE_FROM_ALL)

include(cmake/jitnodes.cmake)

add_executable(curvy
    src/main.cpp
    src/canvasview.cpp
    src/canvastools.cpp
    src/texttool.cpp
    src/cuttools.cpp
    src/artboardtool.cpp
    src/transformtools.cpp
    src/guides.cpp
    src/cursorset.cpp
    src/shapetools.cpp
    src/pathops.cpp
    src/textoutline.cpp
    src/gzip.cpp
    src/sketchbin.cpp
    src/gradientbar.cpp
    src/swatchgrid.cpp
    src/pagepreview.cpp
    src/welcomeoverlay.cpp
    src/stylepanel.cpp
    src/transformpanel.cpp
    src/layertree.cpp
    src/layerspanel.cpp
    src/artboardpanel.cpp
    src/librarypanel.cpp
    src/brushpanel.cpp
    src/brush.cpp
    src/pathflatten.cpp
    src/imagedata.cpp
    src/textwarp.cpp
    src/documenttextpath.cpp
    src/documentimages.cpp
    src/documentexpand.cpp
    src/pdfexport.cpp
    src/documentbrushes.cpp
    src/textpanel.cpp
    src/numberscrubber.cpp
    src/sketchcolor.cpp
    src/sketchobject.cpp
    src/graphdocument.cpp
    src/graphtree.cpp
    src/graphdock.cpp
    src/pathnode.cpp
    src/appearancenode.cpp
    src/radialmenu.cpp
    src/tooloptionscard.cpp
    src/colorwheel.cpp
    src/holdscrub.cpp
    src/workspacemodel.cpp
    src/workspacecontroller.cpp
    src/dockcontroller.cpp
    src/dockcontext.cpp
    src/workspacechecks.cpp
    src/sketchdocument.cpp
    src/documentlive.cpp
    src/documentcutting.cpp
    src/documentpaths.cpp
    src/documenttransform.cpp
    src/documentguides.cpp
    src/documentlayers.cpp
    src/documentswatches.cpp
    src/documentselect.cpp
    src/documentclip.cpp
    src/documentsymbols.cpp
    src/documentartboards.cpp
    src/documenttrace.cpp
    src/imagetrace.cpp
    src/sketchsvg.cpp
    src/sketchexport.cpp
    src/sketchcontroller.cpp
    src/sketchcommands.cpp
    src/sketchwindow.cpp
    src/sketchchecks.cpp
    src/newdocumentmodel.cpp
    src/newdocumentdialog.cpp
    src/settings.cpp
    src/settingsdialog.cpp
    src/tracedialog.cpp
)

target_include_directories(curvy PRIVATE include)
target_link_libraries(curvy PRIVATE newui jitnodes::jitnodes zlib_static windowscodecs ole32)

if(CURVY_GUI_AUTOMATION)
    target_link_libraries(curvy PRIVATE newui_automation)
    target_compile_definitions(curvy PRIVATE CURVY_GUI_AUTOMATION)
endif()

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
newui_add_reflectgen_output(curvy
    SCAN_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include/curvy
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/jitnodes/include
    REGISTER_FUNCTION registerCurvyReflection)

newui_add_resources(curvy "${CMAKE_CURRENT_SOURCE_DIR}/Resources")
# Headless tests of the model layer (color, objects, document, file formats).
set(CURVY_MODEL_SOURCES
    sketchcolor sketchobject sketchdocument documentcutting documentpaths documenttransform documentguides
    documentlayers documentswatches documentselect documentclip documentsymbols documentbrushes brush
    pathflatten textwarp imagedata documenttextpath documentimages documentexpand pdfexport documentartboards
    documenttrace imagetrace sketchsvg sketchexport shapetools pathops textoutline gzip numberscrubber
    sketchbin cursorset newdocumentmodel graphdocument graphtree pathnode appearancenode documentlive radialmenu holdscrub workspacemodel dockcontext settings)
list(TRANSFORM CURVY_MODEL_SOURCES PREPEND src/)
list(TRANSFORM CURVY_MODEL_SOURCES APPEND .cpp)

add_executable(curvyTests tests/test_sketch_model.cpp ${CURVY_MODEL_SOURCES})
target_include_directories(curvyTests PRIVATE include)
target_link_libraries(curvyTests PRIVATE newui jitnodes::jitnodes zlib_static windowscodecs ole32 GTest::gtest_main)
include(GoogleTest)
gtest_discover_tests(curvyTests)

# A throwaway app for trying the pen+keyboard interaction (labs/uilab) before it goes into the editor.
add_executable(curvyUiLab labs/uilab/main.cpp labs/uilab/labcanvas.cpp src/radialmenu.cpp)
target_include_directories(curvyUiLab PRIVATE include labs/uilab)
target_link_libraries(curvyUiLab PRIVATE newui)
newui_add_reflectgen_output(curvyUiLab
    SCAN_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/labs/uilab
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/labs/uilab
    REGISTER_FUNCTION registerLabReflection)
newui_add_resources(curvyUiLab "${CMAKE_CURRENT_SOURCE_DIR}/labs/uilab/Resources")
if(CURVY_GUI_AUTOMATION)
    target_link_libraries(curvyUiLab PRIVATE newui_automation)
    target_compile_definitions(curvyUiLab PRIVATE CURVY_GUI_AUTOMATION)
endif()
