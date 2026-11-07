# jitnodes' own CMakeLists would add a second asmjit next to the one newui's Blend2D embeds (under its
# private abi_bl namespace, so it can't be shared). Both are built here from the submodule's sources,
# the asmjit the same way jitnodes does: static, host backend only.
set(JITNODES_DIR ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/jitnodes)
set(JITNODES_ASMJIT_DIR ${JITNODES_DIR}/3rdparty/asmjit/asmjit)

file(GLOB JITNODES_ASMJIT_SOURCES CONFIGURE_DEPENDS
    ${JITNODES_ASMJIT_DIR}/core/*.cpp
    ${JITNODES_ASMJIT_DIR}/support/*.cpp
    ${JITNODES_ASMJIT_DIR}/x86/*.cpp
    ${JITNODES_ASMJIT_DIR}/ujit/*.cpp)

add_library(jitnodesAsmjit STATIC ${JITNODES_ASMJIT_SOURCES})
target_include_directories(jitnodesAsmjit SYSTEM PUBLIC ${JITNODES_DIR}/3rdparty/asmjit)
target_compile_definitions(jitnodesAsmjit PUBLIC ASMJIT_STATIC ASMJIT_NO_FOREIGN)

file(GLOB JITNODES_SOURCES CONFIGURE_DEPENDS ${JITNODES_DIR}/src/*/*.cpp)

add_library(jitnodes STATIC ${JITNODES_SOURCES})
add_library(jitnodes::jitnodes ALIAS jitnodes)
target_include_directories(jitnodes PUBLIC ${JITNODES_DIR}/include)
target_include_directories(jitnodes SYSTEM PRIVATE ${JITNODES_DIR}/3rdparty/json5/include)
target_link_libraries(jitnodes PUBLIC jitnodesAsmjit)
target_compile_definitions(jitnodes PRIVATE _CRT_SECURE_NO_WARNINGS)
target_compile_options(jitnodes PRIVATE /utf-8)

# jitnodes' own test suite, built here at C++17 (jitnodes itself builds as C++20).
add_executable(jitnodesTests ${JITNODES_DIR}/tests/testMain.cpp)
target_link_libraries(jitnodesTests PRIVATE jitnodes::jitnodes)
target_compile_definitions(jitnodesTests PRIVATE JITNODES_SOURCE_DIR="${JITNODES_DIR}")
add_test(NAME jitnodesTests COMMAND jitnodesTests)
