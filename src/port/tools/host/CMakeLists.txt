# Host-native build tools for the Winelib configuration.
#
# Built as a standalone CMake project by ExternalProject_Add() in the parent
# CMakeLists.txt (OPUS_WINELIB_BUILD branch). Compiles four generators with
# the native gcc/g++ instead of winegcc/wineg++: they are pure build-machine
# tools with no Win32 or engine-ABI dependency (see
# docs/port-linux/00-reconocimiento.md), so building them with winegcc
# gained nothing and exposed them to the Winelib layer's failure modes.
#
# opus_cabi_tool is intentionally NOT here: it measures the engine's own
# sizeof() through <windows.h> and must stay on the same compiler as the
# engine (built directly by the parent project).
cmake_minimum_required(VERSION 3.25)
project(opus_host_tools C CXX)

# Root of the msword/src tree, passed in by the parent's ExternalProject_Add
# so these sources can be referenced without duplicating them under host/.
if(NOT DEFINED OPUS_HOST_TOOLS_SRC_ROOT)
    message(FATAL_ERROR "OPUS_HOST_TOOLS_SRC_ROOT must be set by the caller.")
endif()

include(GNUInstallDirs)

add_executable(opus_mkcmd_tool
    "${OPUS_HOST_TOOLS_SRC_ROOT}/OpusEtAl/tools/src/mkcmd.c"
)
target_compile_definitions(opus_mkcmd_tool PRIVATE OPUS_X64_TOOL)
target_include_directories(opus_mkcmd_tool PRIVATE
    "${OPUS_HOST_TOOLS_SRC_ROOT}/OpusEtAl/tools/src"
    "${OPUS_HOST_TOOLS_SRC_ROOT}/port/tools"
)
target_compile_options(opus_mkcmd_tool PRIVATE -std=gnu89)

add_executable(opus_mkdlg_tool
    "${OPUS_HOST_TOOLS_SRC_ROOT}/OpusEtAl/tools/src/mkdlg.c"
)
target_include_directories(opus_mkdlg_tool PRIVATE
    "${OPUS_HOST_TOOLS_SRC_ROOT}/OpusEtAl/tools/src"
    "${OPUS_HOST_TOOLS_SRC_ROOT}/port/tools"
)
target_compile_options(opus_mkdlg_tool PRIVATE -std=gnu89)

add_executable(opus_bitapp_tool
    "${OPUS_HOST_TOOLS_SRC_ROOT}/OpusEtAl/tools/src/bitapp.c"
)
target_compile_definitions(opus_bitapp_tool PRIVATE OPUS_X64_TOOL)
target_include_directories(opus_bitapp_tool PRIVATE
    "${OPUS_HOST_TOOLS_SRC_ROOT}/OpusEtAl/tools/src"
    "${OPUS_HOST_TOOLS_SRC_ROOT}/port/tools"
)
target_compile_options(opus_bitapp_tool PRIVATE -std=gnu89)

add_executable(opus_dibapp_tool
    "${OPUS_HOST_TOOLS_SRC_ROOT}/port/tools/opus_dibapp_tool.cpp"
)
target_compile_features(opus_dibapp_tool PRIVATE cxx_std_20)

install(TARGETS
    opus_mkcmd_tool
    opus_mkdlg_tool
    opus_bitapp_tool
    opus_dibapp_tool
    RUNTIME DESTINATION bin
)
