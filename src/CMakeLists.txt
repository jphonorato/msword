cmake_minimum_required(VERSION 3.25)

# RC is enabled below, only for a native Windows configure. On Linux no
# CMAKE_RC_COMPILER exists (confirmed in reconnaissance,
# docs/port-linux/00-reconocimiento.md §3.3); resources there are compiled
# by wrc through an add_custom_command instead (Fase 4 of that same
# document), not through CMake's RC language.
project(MicrosoftWordX64Port VERSION 0.2.0 LANGUAGES C CXX)
if(WIN32)
    enable_language(RC)
endif()

# OPUS_WINELIB_BUILD is set by cmake/toolchain-winelib.cmake (selected via
# the linux-winelib-* presets) when the compiler is winegcc/wineg++: a
# Winelib ELF build, not a native-Windows one, but still a build this
# project supports.
if(NOT WIN32 AND NOT OPUS_WINELIB_BUILD)
    message(FATAL_ERROR
        "This project targets either native Windows (the x64-debug / "
        "x64-release presets) or a Winelib build on Linux (the "
        "linux-winelib-debug / linux-winelib-release presets; see "
        "src/cmake/toolchain-winelib.cmake). Configure with one of the "
        "presets in CMakePresets.json.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This project must be configured for x64 (use: cmake --preset x64-debug).")
endif()

set(MSWORD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
cmake_path(NORMAL_PATH MSWORD_ROOT)
set(MSWORD_BUILD_ROOT "${MSWORD_ROOT}/build")
set(MSWORD_BIN_ROOT "${MSWORD_ROOT}/bin")
# Reserved for the executable that will be linked from the original Opus
# source graph. The migration bootstrap must not use this product name.
set(MSWORD_PRODUCT_OUTPUT_NAME "WORD1" CACHE STRING
    "Filename stem for the original-source Word x64 executable")

file(MAKE_DIRECTORY "${MSWORD_BUILD_ROOT}" "${MSWORD_BIN_ROOT}")

# Keep linked artifacts in the locations requested for this port. CMake's
# generated project and cache live under ../out (see CMakePresets.json).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${MSWORD_BIN_ROOT}")
set(CMAKE_PDB_OUTPUT_DIRECTORY "${MSWORD_BUILD_ROOT}/pdb")
set(CMAKE_COMPILE_PDB_OUTPUT_DIRECTORY "${MSWORD_BUILD_ROOT}/pdb")

foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${MSWORD_BIN_ROOT}")
    set(CMAKE_PDB_OUTPUT_DIRECTORY_${config_upper} "${MSWORD_BUILD_ROOT}/pdb/${config}")
    set(CMAKE_COMPILE_PDB_OUTPUT_DIRECTORY_${config_upper} "${MSWORD_BUILD_ROOT}/pdb/${config}")
endforeach()

file(GLOB_RECURSE LEGACY_ASSEMBLY CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.asm")
set(LEGACY_ASSEMBLY_RELATIVE)
foreach(assembly_file IN LISTS LEGACY_ASSEMBLY)
    file(RELATIVE_PATH relative_path "${CMAKE_CURRENT_SOURCE_DIR}" "${assembly_file}")
    cmake_path(CONVERT "${relative_path}" TO_CMAKE_PATH_LIST relative_path NORMALIZE)
    list(APPEND LEGACY_ASSEMBLY_RELATIVE "${relative_path}")
endforeach()
list(SORT LEGACY_ASSEMBLY_RELATIVE)

list(LENGTH LEGACY_ASSEMBLY_RELATIVE LEGACY_ASSEMBLY_COUNT)
if(NOT LEGACY_ASSEMBLY_COUNT EQUAL 59)
    message(FATAL_ERROR
        "The original assembly reference tree changed "
        "(${LEGACY_ASSEMBLY_COUNT} modules found; expected 59). Review the x64 translations."
    )
endif()

add_custom_target(legacy_sources SOURCES ${LEGACY_ASSEMBLY})
set_target_properties(legacy_sources PROPERTIES FOLDER "Legacy reference (not compiled)")

# Start of the real product engine. Every source in this target is from the
# original Opus tree; compatibility code supplies platform services only.
if(OPUS_WINELIB_BUILD)
    # Wine's own Win32 header tree stands in for the Windows Kits SDK.
    # winegcc already searches it by default; this find_path only locates it
    # so opus_windows_sdk.h.in can be filled in with the same @ONLY
    # substitution the native-Windows branch below uses.
    find_path(OPUS_WINE_INCLUDE_DIR
        NAMES windows/windows.h
        PATHS /usr/include/wine /usr/local/include/wine
        DOC "Wine's Win32 header tree (contains windows/windows.h)"
    )
    if(NOT OPUS_WINE_INCLUDE_DIR)
        message(FATAL_ERROR
            "Could not locate Wine's windows/windows.h under /usr/include/wine "
            "or /usr/local/include/wine. Install wine-devel.")
    endif()
    set(OPUS_WINDOWS_SDK_HEADER "${OPUS_WINE_INCLUDE_DIR}/windows/windows.h")
else()
    set(OPUS_SDK_ROOT "$ENV{ProgramFiles\(x86\)}/Windows Kits/10")
    set(OPUS_WINDOWS_SDK_HEADER
        "${OPUS_SDK_ROOT}/Include/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/um/Windows.h")
    if(NOT EXISTS "${OPUS_WINDOWS_SDK_HEADER}")
        message(FATAL_ERROR "Could not locate the Windows SDK header: ${OPUS_WINDOWS_SDK_HEADER}")
    endif()
endif()
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/original")
configure_file(
    port/original/opus_windows_sdk.h.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/original/opus_windows_sdk.h"
    @ONLY
)

# MERGEELX writes a fixed hidden-screen payload in addition to the dialog EL
# tables. Preserve that original payload directly from Microsoft's generator
# source. The archive lacks the Dialog Editor executable that converted its
# .des inputs to MERGEELX's intermediate .elx format, so an explicit inert EL
# table boundary remains until that missing compiler stage is replaced.
set(OPUS_GENERATED_ELXINFO
    "${CMAKE_CURRENT_BINARY_DIR}/generated/original/elxinfo.h")
add_custom_command(
    OUTPUT "${OPUS_GENERATED_ELXINFO}"
    COMMAND "${CMAKE_COMMAND}"
        -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/OpusEtAl/tools/src/mergeelx.c
        -DOUTPUT=${OPUS_GENERATED_ELXINFO}
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateElxInfoHeader.cmake"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateElxInfoHeader.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/OpusEtAl/tools/src/mergeelx.c"
    VERBATIM
)
add_custom_target(opus_generated_elxinfo DEPENDS "${OPUS_GENERATED_ELXINFO}")
set_target_properties(opus_generated_elxinfo PROPERTIES
    FOLDER "Original Opus x64/generated"
)

# Host build tools: opus_mkcmd_tool, opus_mkdlg_tool, opus_bitapp_tool and
# opus_dibapp_tool.
#
# Under the Winelib toolchain these four are built by a separate sub-project
# with the native gcc/g++ instead of winegcc.  They are pure build-machine
# generators with no Win32 or engine-ABI dependency (audited include by
# include; see docs/port-linux/00-reconocimiento.md), so compiling them with
# winegcc gained nothing and exposed them to the Winelib layer's failure
# modes.  opus_cabi_tool is excluded from this split on purpose: it measures
# the engine's own sizeof() through <windows.h> and must stay on the same
# compiler as the engine.
#
# The sub-project installs the binaries, and IMPORTED targets of the same
# names below make every "$<TARGET_FILE:opus_*_tool>" in this file resolve
# unchanged.  Only "DEPENDS" needs a different name -- ninja cannot resolve a
# DEPENDS on an IMPORTED target's location, so OPUS_HOST_TOOLS_DEP carries the
# right dependency for each branch.
if(OPUS_WINELIB_BUILD)
    include(ExternalProject)
    set(OPUS_HOST_TOOLS_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/host-tools")
    ExternalProject_Add(opus_host_tools_build
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/port/tools/host"
        CMAKE_ARGS
            -DCMAKE_C_COMPILER=gcc
            -DCMAKE_CXX_COMPILER=g++
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_INSTALL_PREFIX=${OPUS_HOST_TOOLS_PREFIX}
            -DOPUS_HOST_TOOLS_SRC_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        BUILD_ALWAYS 1
        USES_TERMINAL_CONFIGURE 1
        USES_TERMINAL_BUILD 1
    )
    set_target_properties(opus_host_tools_build PROPERTIES
        FOLDER "Original Opus x64/build tools")
    foreach(host_tool opus_mkcmd_tool opus_mkdlg_tool opus_bitapp_tool
                      opus_dibapp_tool)
        add_executable(${host_tool} IMPORTED GLOBAL)
        set_target_properties(${host_tool} PROPERTIES
            IMPORTED_LOCATION "${OPUS_HOST_TOOLS_PREFIX}/bin/${host_tool}")
        add_dependencies(${host_tool} opus_host_tools_build)
    endforeach()
    # Custom commands depend on the sub-build target, not on the IMPORTED
    # target: ninja cannot resolve a DEPENDS on an IMPORTED target's location.
    set(OPUS_MKCMD_TOOL_DEP opus_host_tools_build)
    set(OPUS_MKDLG_TOOL_DEP opus_host_tools_build)
    set(OPUS_BITAPP_TOOL_DEP opus_host_tools_build)
    set(OPUS_DIBAPP_TOOL_DEP opus_host_tools_build)
else()
    # Real targets in this project: depend on each tool exactly as before.
    set(OPUS_MKCMD_TOOL_DEP opus_mkcmd_tool)
    set(OPUS_MKDLG_TOOL_DEP opus_mkdlg_tool)
    set(OPUS_BITAPP_TOOL_DEP opus_bitapp_tool)
    set(OPUS_DIBAPP_TOOL_DEP opus_dibapp_tool)
endif()

# Núcleo Qt (rama Qt-2, docs/port-qt/01-frontera-nucleo-shell.md): código
# nuevo, sin windows.h, que nunca pasa por winegcc/wineg++. Bajo el
# toolchain Winelib se construye con el mismo esquema de sub-proyecto nativo
# que los host tools de arriba, porque necesita su propio compilador y su
# propio Qt6, no el de la capa de compatibilidad del motor. Solo se
# construye bajo OPUS_WINELIB_BUILD: es trabajo de la rama Qt en desarrollo
# activo sobre el preset Linux, no algo que el build de MSVC deba conocer.
if(OPUS_WINELIB_BUILD)
    set(OPUS_CORE_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/core")
    ExternalProject_Add(opus_core_build
        SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/core"
        CMAKE_ARGS
            -DCMAKE_C_COMPILER=gcc
            -DCMAKE_CXX_COMPILER=g++
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_INSTALL_PREFIX=${OPUS_CORE_PREFIX}
            -DOPUS_CORE_BUILD_TESTS=ON
        BUILD_ALWAYS 1
        USES_TERMINAL_CONFIGURE 1
        USES_TERMINAL_BUILD 1
    )
    set_target_properties(opus_core_build PROPERTIES
        FOLDER "Qt port/core")
    add_executable(opus_shell_config_test IMPORTED GLOBAL)
    set_target_properties(opus_shell_config_test PROPERTIES
        IMPORTED_LOCATION "${OPUS_CORE_PREFIX}/bin/opus_shell_config_test")
    add_dependencies(opus_shell_config_test opus_core_build)
    add_executable(opus_shell_font_substitution_test IMPORTED GLOBAL)
    set_target_properties(opus_shell_font_substitution_test PROPERTIES
        IMPORTED_LOCATION "${OPUS_CORE_PREFIX}/bin/opus_shell_font_substitution_test")
    add_dependencies(opus_shell_font_substitution_test opus_core_build)
endif()

# Rebuild the command constants from Microsoft's checked-in command compiler
# and resource sources. The tool runs natively on x64 but preserves MKCMD's
# original 16-bit serialized word layout.
if(NOT OPUS_WINELIB_BUILD)
add_executable(opus_mkcmd_tool EXCLUDE_FROM_ALL
    OpusEtAl/tools/src/mkcmd.c
)
target_compile_definitions(opus_mkcmd_tool PRIVATE OPUS_X64_TOOL)
target_include_directories(opus_mkcmd_tool PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/OpusEtAl/tools/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/port/tools"
)
if(MSVC)
    target_compile_options(opus_mkcmd_tool PRIVATE /J /W3 /wd4996)
endif()
if(NOT MSVC)
    target_compile_options(opus_mkcmd_tool PRIVATE -std=gnu89)
endif()
set_target_properties(opus_mkcmd_tool PROPERTIES
    FOLDER "Original Opus x64/build tools"
    RUNTIME_OUTPUT_DIRECTORY "${MSWORD_BUILD_ROOT}/tools"
    VS_GLOBAL_VcpkgApplocalDeps "false"
)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(opus_mkcmd_tool PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${MSWORD_BUILD_ROOT}/tools/${config}"
    )
endforeach()
endif()

# Stays on the project's main compiler (winegcc under the Winelib toolchain):
# it evaluates sizeof() over the engine's structures via <windows.h>, so its
# output *is* the engine ABI and must not be measured by a different compiler.
add_executable(opus_cabi_tool EXCLUDE_FROM_ALL
    port/tools/opus_cabi_tool.cpp
)
target_compile_features(opus_cabi_tool PRIVATE cxx_std_20)
target_compile_definitions(opus_cabi_tool PRIVATE
    WIN WIN23 CRLF NONATIVE OPUS_X64 NOMINMAX
)
target_include_directories(opus_cabi_tool PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated/original"
    "${CMAKE_CURRENT_SOURCE_DIR}/port/original"
    "${CMAKE_CURRENT_SOURCE_DIR}/Opus/lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/Opus"
)
if(MSVC)
    target_compile_options(opus_cabi_tool PRIVATE
        /J /W3 /wd4005 /permissive-)
endif()
set_target_properties(opus_cabi_tool PROPERTIES
    FOLDER "Original Opus x64/build tools"
    RUNTIME_OUTPUT_DIRECTORY "${MSWORD_BUILD_ROOT}/tools"
    VS_GLOBAL_VcpkgApplocalDeps "false"
)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(opus_cabi_tool PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${MSWORD_BUILD_ROOT}/tools/${config}"
    )
endforeach()

# Preserve Microsoft's original BITAPP resource conversion step.  These
# native x64 tool builds consume the checked-in Win16 .cur files and emit the
# same byte initializers that SCREEN2.C historically included.
if(NOT OPUS_WINELIB_BUILD)
add_executable(opus_bitapp_tool EXCLUDE_FROM_ALL
    OpusEtAl/tools/src/bitapp.c
)
target_compile_definitions(opus_bitapp_tool PRIVATE OPUS_X64_TOOL)
target_include_directories(opus_bitapp_tool PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/OpusEtAl/tools/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/port/tools"
)
if(MSVC)
    target_compile_options(opus_bitapp_tool PRIVATE /J /W3 /wd4996)
endif()
if(NOT MSVC)
    target_compile_options(opus_bitapp_tool PRIVATE -std=gnu89)
endif()
set_target_properties(opus_bitapp_tool PROPERTIES
    FOLDER "Original Opus x64/build tools"
    RUNTIME_OUTPUT_DIRECTORY "${MSWORD_BUILD_ROOT}/tools"
    VS_GLOBAL_VcpkgApplocalDeps "false"
)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(opus_bitapp_tool PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${MSWORD_BUILD_ROOT}/tools/${config}"
    )
endforeach()
endif()

set(OPUS_GENERATED_COMMAND_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated/original")

set(OPUS_CURSOR_RESOURCE_NAMES
    styname
    mic
    help
    cross
    vertout
    horzout
    prvwcrs
)
set(OPUS_GENERATED_CURSOR_FILES)
foreach(cursor_name IN LISTS OPUS_CURSOR_RESOURCE_NAMES)
    set(cursor_source "${CMAKE_CURRENT_SOURCE_DIR}/Opus/resource/${cursor_name}.cur")
    set(cursor_header "${OPUS_GENERATED_COMMAND_DIR}/${cursor_name}.hc")
    file(RELATIVE_PATH cursor_source_rel "${CMAKE_CURRENT_BINARY_DIR}" "${cursor_source}")
    file(RELATIVE_PATH cursor_header_rel "${CMAKE_CURRENT_BINARY_DIR}" "${cursor_header}")
    add_custom_command(
        OUTPUT "${cursor_header}"
        # Paths are passed relative to the build directory on purpose.  BITAPP
        # parses any argument starting with '/' as a switch (bitapp.c:59), a
        # Windows convention where absolute paths start with a drive letter.
        # On Linux an absolute path *is* a leading '/', so it would be read as
        # a bad switch.  Adapting the caller keeps OpusEtAl/ untouched.
        COMMAND "$<TARGET_FILE:opus_bitapp_tool>"
            "${cursor_source_rel}" "${cursor_header_rel}"
        DEPENDS ${OPUS_BITAPP_TOOL_DEP} "${cursor_source}"
        COMMENT "Converting original Opus cursor ${cursor_name}.cur with BITAPP"
        VERBATIM
    )
    list(APPEND OPUS_GENERATED_CURSOR_FILES "${cursor_header}")
endforeach()
add_custom_target(opus_generated_cursors
    DEPENDS ${OPUS_GENERATED_CURSOR_FILES}
)
set_target_properties(opus_generated_cursors PROPERTIES
    FOLDER "Original Opus x64/build tools"
)

# Recreate the checked-out build's embedded icon/cursor/bitmap C headers
# using Microsoft's original BITAPP source. These headers were intermediate
# build products and are not present in the source archive.
set(OPUS_BITAPP_BITMAP_HEADERS
    8hdr.hb 8otlpat.hb 8outline.hg 8pageico.hb 8paralig.hg
    8ribbon.hg 8rulmark.hg 8rultgls.hg 8scrptpo.hg
    chdr.hb cotlpat.hb coutline.hg cpageico.hb cparalig.hg
    cribbon.hg crulmark.hg crultgls.hg cscrptpo.hg
    ehdr.hb eotlpat.hb eoutline.hg epageico.hb eparalig.hg
    eribbon.hg erulmark.hg erultgls.hg escrptpo.hg
    shdr.hb showvis.hb sotlpat.hb soutline.hg spageico.hb
    sparalig.hg sribbon.hg srulmark.hg srultgls.hg sscrptpo.hg
    vhdr.hb votlpat.hb voutline.hg vpageico.hb vparalig.hg
    vribbon.hg vrulmark.hg vrultgls.hg vscrptpo.hg
)
set(OPUS_BITAPP_FIGURE_HEADERS
    mopus.hi mword.hi mwhires.hc split.hc column.hc
)
set(OPUS_GENERATED_EMBEDDED_RESOURCES)
foreach(resource_header IN LISTS OPUS_BITAPP_BITMAP_HEADERS OPUS_BITAPP_FIGURE_HEADERS)
    get_filename_component(resource_stem "${resource_header}" NAME_WE)
    get_filename_component(resource_extension "${resource_header}" EXT)
    if(resource_extension STREQUAL ".hi")
        set(resource_source_extension ".ico")
    elseif(resource_extension STREQUAL ".hc")
        set(resource_source_extension ".cur")
    else()
        set(resource_source_extension ".bmp")
    endif()
    set(resource_source
        "${CMAKE_CURRENT_SOURCE_DIR}/Opus/resource/${resource_stem}${resource_source_extension}")
    set(resource_output
        "${OPUS_GENERATED_COMMAND_DIR}/${resource_header}")
    # Relative for the same reason as the cursor rule above: BITAPP would read
    # a leading '/' as a switch.
    file(RELATIVE_PATH resource_source_rel
        "${CMAKE_CURRENT_BINARY_DIR}" "${resource_source}")
    file(RELATIVE_PATH resource_output_rel
        "${CMAKE_CURRENT_BINARY_DIR}" "${resource_output}")
    if(resource_extension STREQUAL ".hg")
        set(bitapp_gray_flag /g)
    else()
        set(bitapp_gray_flag)
    endif()
    add_custom_command(
        OUTPUT "${resource_output}"
        COMMAND "$<TARGET_FILE:opus_bitapp_tool>" ${bitapp_gray_flag}
            "${resource_source_rel}" "${resource_output_rel}"
        DEPENDS ${OPUS_BITAPP_TOOL_DEP} "${resource_source}"
        COMMENT "Regenerating original Opus resource header ${resource_header}"
        VERBATIM
    )
    list(APPEND OPUS_GENERATED_EMBEDDED_RESOURCES "${resource_output}")
endforeach()
add_custom_target(opus_generated_embedded_resources
    DEPENDS ${OPUS_GENERATED_EMBEDDED_RESOURCES}
)
set_target_properties(opus_generated_embedded_resources PROPERTIES
    FOLDER "Original Opus x64/generated"
)

# DIBAPP itself is not present in the archive. This native compatibility
# tool preserves its build boundary by extracting the original OS/2
# BITMAPCOREHEADER scan lines into the BMDS3 aggregate format consumed by
# Microsoft's rcbmp13/23/43 modules.
if(NOT OPUS_WINELIB_BUILD)
add_executable(opus_dibapp_tool EXCLUDE_FROM_ALL
    port/tools/opus_dibapp_tool.cpp
)
target_compile_features(opus_dibapp_tool PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(opus_dibapp_tool PRIVATE /W4 /permissive-)
endif()
set_target_properties(opus_dibapp_tool PROPERTIES
    FOLDER "Original Opus x64/build tools"
    RUNTIME_OUTPUT_DIRECTORY "${MSWORD_BUILD_ROOT}/tools"
    VS_GLOBAL_VcpkgApplocalDeps "false"
)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(opus_dibapp_tool PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${MSWORD_BUILD_ROOT}/tools/${config}"
    )
endforeach()
endif()

file(GLOB OPUS_DIB_RESOURCE_INPUTS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Opus/resource/*.dib")
set(OPUS_GENERATED_DIB_RESOURCE_FILES)
foreach(dib_input IN LISTS OPUS_DIB_RESOURCE_INPUTS)
    get_filename_component(dib_stem "${dib_input}" NAME_WE)
    set(dib_output "${OPUS_GENERATED_COMMAND_DIR}/${dib_stem}.hb")
    add_custom_command(
        OUTPUT "${dib_output}"
        COMMAND "$<TARGET_FILE:opus_dibapp_tool>" "${dib_input}" "${dib_output}"
        DEPENDS ${OPUS_DIBAPP_TOOL_DEP} "${dib_input}"
        COMMENT "Converting original ${dib_stem}.dib resource"
        VERBATIM
    )
    list(APPEND OPUS_GENERATED_DIB_RESOURCE_FILES "${dib_output}")
endforeach()
add_custom_target(opus_generated_dib_resources
    DEPENDS ${OPUS_GENERATED_DIB_RESOURCE_FILES}
)
set_target_properties(opus_generated_dib_resources PROPERTIES
    FOLDER "Original Opus x64/generated"
)

set(OPUS_GENERATED_COMMAND_FILES
    "${OPUS_GENERATED_COMMAND_DIR}/opuscmd.h"
    "${OPUS_GENERATED_COMMAND_DIR}/IBCM.H"
    "${OPUS_GENERATED_COMMAND_DIR}/RGBCM.H"
    "${OPUS_GENERATED_COMMAND_DIR}/OPUSCMD2.H"
    "${OPUS_GENERATED_COMMAND_DIR}/OPUSMENU.H"
    "${OPUS_GENERATED_COMMAND_DIR}/MENUHELP.TXT"
    "${OPUS_GENERATED_COMMAND_DIR}/menuhelp.h"
    "${OPUS_GENERATED_COMMAND_DIR}/opuscmd.asm"
    "${OPUS_GENERATED_COMMAND_DIR}/opuscmd_native.inc"
)
file(GLOB OPUS_RECONSTRUCTED_DIALOG_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/port/original/*.hs")
set(OPUS_GENERATED_CABI_DIR "${OPUS_GENERATED_COMMAND_DIR}/cabi")
set(OPUS_GENERATED_CABI_OPEN "${OPUS_GENERATED_CABI_DIR}/open.hs")
add_custom_command(
    OUTPUT "${OPUS_GENERATED_CABI_OPEN}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${OPUS_GENERATED_CABI_DIR}"
    COMMAND "$<TARGET_FILE:opus_cabi_tool>" "${OPUS_GENERATED_CABI_DIR}"
    DEPENDS opus_cabi_tool ${OPUS_RECONSTRUCTED_DIALOG_HEADERS}
    COMMENT "Computing native SDM CAB initializers"
    VERBATIM
)
add_custom_command(
    OUTPUT ${OPUS_GENERATED_COMMAND_FILES}
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Opus/resource/opuscmd.cmd"
        "${OPUS_GENERATED_COMMAND_DIR}/opuscmd.cmd"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Opus/resource/keys.cmd"
        "${OPUS_GENERATED_COMMAND_DIR}/keys.cmd"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/Opus/resource/menus.cmd"
        "${OPUS_GENERATED_COMMAND_DIR}/menus.cmd"
    COMMAND "${CMAKE_COMMAND}" -E copy
        "${CMAKE_CURRENT_SOURCE_DIR}/Opus/resource/ibcm.rel"
        "${OPUS_GENERATED_COMMAND_DIR}/IBCM.H"
    COMMAND "$<TARGET_FILE:opus_mkcmd_tool>"
        # Absolute rather than "../../../src/Opus/dlg/": the relative form
        # silently assumed the Visual Studio preset's binaryDir depth
        # (out/), and breaks under any preset that nests one level deeper
        # (out/<preset>/), where it resolves to a nonexistent out/src.
        -T@13 "-s${CMAKE_CURRENT_SOURCE_DIR}/Opus/dlg/" -bcabi/
        opuscmd keys menus
    COMMAND "${CMAKE_COMMAND}"
        -DINPUT=${OPUS_GENERATED_COMMAND_DIR}/MENUHELP.TXT
        -DOUTPUT=${OPUS_GENERATED_COMMAND_DIR}/menuhelp.h
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateMenuHelpHeader.cmake"
    DEPENDS
        ${OPUS_MKCMD_TOOL_DEP}
        Opus/resource/opuscmd.cmd
        Opus/resource/keys.cmd
        Opus/resource/menus.cmd
        Opus/resource/ibcm.rel
        Opus/dlg/elx.txt
        "${OPUS_GENERATED_CABI_OPEN}"
        ${OPUS_RECONSTRUCTED_DIALOG_HEADERS}
        cmake/GenerateMenuHelpHeader.cmake
    WORKING_DIRECTORY "${OPUS_GENERATED_COMMAND_DIR}"
    COMMENT "Regenerating original Opus command tables with MKCMD"
    VERBATIM
)
add_custom_target(opus_generated_commands
    DEPENDS ${OPUS_GENERATED_COMMAND_FILES}
)
set_target_properties(opus_generated_commands PROPERTIES
    FOLDER "Original Opus x64/build tools"
)

# Rebuild the EL dialog range/parse tables with Microsoft's original MKDLG
# source.  dlgcheck.h was a generated build artifact and is not checked into
# this archive.
if(NOT OPUS_WINELIB_BUILD)
add_executable(opus_mkdlg_tool EXCLUDE_FROM_ALL
    OpusEtAl/tools/src/mkdlg.c
)
target_include_directories(opus_mkdlg_tool PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/OpusEtAl/tools/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/port/tools"
)
if(MSVC)
    target_compile_options(opus_mkdlg_tool PRIVATE /J /W3 /wd4996)
endif()
if(NOT MSVC)
    target_compile_options(opus_mkdlg_tool PRIVATE -std=gnu89)
endif()
set_target_properties(opus_mkdlg_tool PROPERTIES
    FOLDER "Original Opus x64/build tools"
    RUNTIME_OUTPUT_DIRECTORY "${MSWORD_BUILD_ROOT}/tools"
    VS_GLOBAL_VcpkgApplocalDeps "false"
)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(opus_mkdlg_tool PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${MSWORD_BUILD_ROOT}/tools/${config}"
    )
endforeach()
endif()
file(GLOB OPUS_DIALOG_ELX CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Opus/dlg/*.elx")
set(OPUS_GENERATED_DLGCHECK
    "${OPUS_GENERATED_COMMAND_DIR}/dlgcheck.h")
add_custom_command(
    OUTPUT "${OPUS_GENERATED_DLGCHECK}"
    COMMAND "$<TARGET_FILE:opus_mkdlg_tool>"
        "@elx.txt" "${OPUS_GENERATED_DLGCHECK}"
    DEPENDS ${OPUS_MKDLG_TOOL_DEP} ${OPUS_DIALOG_ELX} Opus/dlg/elx.txt
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Opus/dlg"
    COMMENT "Regenerating original Opus dialog validation tables with MKDLG"
    VERBATIM
)
add_custom_target(opus_generated_dlgcheck
    DEPENDS "${OPUS_GENERATED_DLGCHECK}"
)
set_target_properties(opus_generated_dlgcheck PROPERTIES
    FOLDER "Original Opus x64/generated"
)

add_executable(opus_mergeelx_tool EXCLUDE_FROM_ALL
    OpusEtAl/tools/src/mergeelx.c
)
target_include_directories(opus_mergeelx_tool PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/OpusEtAl/tools/src"
)
if(MSVC)
    target_compile_options(opus_mergeelx_tool PRIVATE /J /W3 /wd4996)
endif()
set_target_properties(opus_mergeelx_tool PROPERTIES
    FOLDER "Original Opus x64/build tools"
    RUNTIME_OUTPUT_DIRECTORY "${MSWORD_BUILD_ROOT}/tools"
    VS_GLOBAL_VcpkgApplocalDeps "false"
)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(opus_mergeelx_tool PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${MSWORD_BUILD_ROOT}/tools/${config}"
    )
endforeach()

set(OPUS_ORIGINAL_ENGINE_SOURCES
    Opus/command.c
    Opus/command2.c
    Opus/annot.c
    Opus/cmd.c
    Opus/cmd2.c
    Opus/CMD3.C
    Opus/cmdcore.c
    Opus/cmdwnd.c
    Opus/curswin.c
    Opus/CLIPBORD.C
    Opus/CLIPBRD2.C
    Opus/compare.c
    Opus/create.c
    Opus/CREATE2.C
    Opus/EDIT.C
    Opus/disp1.c
    Opus/dispbrc.c
    Opus/dispspec.c
    Opus/dialog1.c
    Opus/dialog2.c
    Opus/dialog3.c
    Opus/dialog4.c
    Opus/dlglook1.c
    Opus/dlglook2.c
    Opus/dlgrec.c
    Opus/dlgmisc.c
    Opus/dlgopen.c
    Opus/dlgdoc.c
    Opus/edmacro.c
    Opus/search.c
    Opus/replace.c
    Opus/srchfmt.c
    Opus/tabs.c
    Opus/catalog.c
    Opus/customiz.c
    Opus/toc.c
    Opus/sort.c
    Opus/index1.c
    Opus/index2.c
    Opus/merge.c
    Opus/renum.c
    Opus/insfield.c
    Opus/dlgtable.c
    Opus/dlghyph.c
    Opus/etcmd.c
    Opus/docman1.c
    Opus/docman2.c
    Opus/print2.c
    Opus/ddeclnt.c
    Opus/ddesub.c
    Opus/DDESRVR.C
    Opus/DLBENUM.C
    Opus/eldde.c
    Opus/eldlg.c
    Opus/elfile.c
    Opus/elmisc.c
    Opus/elmisc2.c
    Opus/elsubs.c
    Opus/elsubs3.c
    Opus/elsubs2.c
    Opus/FILE2.C
    Opus/ffcrypt.c
    Opus/ffread.c
    Opus/fieldcr.c
    Opus/fieldclc.c
    Opus/FIELDCMD.C
    Opus/fieldfmt.c
    Opus/fieldprs.c
    Opus/fieldsc2.c
    Opus/fieldspc.c
    Opus/fltexp.c
    Opus/filecvt.c
    Opus/filewin.c
    Opus/formula.c
    Opus/formatsp.c
    Opus/fieldsp.c
    Opus/fieldpic.c
    Opus/glsy.c
    Opus/help.c
    Opus/hddwin.c
    Opus/GRSPEC.C
    Opus/grbit.c
    Opus/grswath.c
    Opus/grtiff.c
    Opus/initwin.c
    Opus/init2.c
    Opus/interp/corout.c
    Opus/interp/elcore.c
    Opus/interp/elinit.c
    Opus/interp/exp.c
    Opus/interp/main.c
    Opus/interp/sym.c
    Opus/interp/to.c
    Opus/idle.c
    Opus/LOADFONT.C
    Opus/iconbar1.c
    Opus/iconbar2.c
    Opus/iconbar3.c
    Opus/menu.c
    Opus/menuhelp.c
    Opus/mathapi.c
    Opus/wordtech/inssubs.c
    Opus/wordtech/insert.c
    Opus/open.c
    Opus/openrare.c
    Opus/outwin.c
    Opus/pic.c
    Opus/PIC2.C
    Opus/pictdrag.c
    Opus/prompt.c
    Opus/preview.c
    Opus/print.c
    Opus/print1.c
    Opus/prvw2.c
    Opus/raremsg.c
    Opus/recorder.c
    Opus/res.c
    Opus/rsb.c
    Opus/util2.c
    Opus/rulerdrw.c
    Opus/rulrib.c
    Opus/RTFIN.C
    Opus/RTFOUT.C
    Opus/RTFRARE.C
    Opus/rtfsubs.c
    Opus/rtftrans.c
    Opus/rcinit.c
    Opus/rcbmp1.c
    Opus/rcbmp2.c
    Opus/rcbmp3.c
    Opus/rcbmp4.c
    Opus/rcbmp13.c
    Opus/rcbmp23.c
    Opus/rcbmp43.c
    Opus/save.c
    Opus/savetext.c
    Opus/SCREEN2.C
    Opus/statline.c
    Opus/style.c
    Opus/stylesub.c
    Opus/strtbl.c
    Opus/splitter.c
    Opus/SYSCHG.C
    Opus/quit.c
    Opus/wproc.c
    Opus/wwact.c
    Opus/wwchange.c
    Opus/token.c
    Opus/vars.c
    Opus/wordtech/clsplc.c
    Opus/wordtech/banter.c
    Opus/wordtech/block.c
    Opus/wordtech/curskeys.c
    Opus/wordtech/disp2.c
    Opus/wordtech/disp3.c
    Opus/wordtech/disptbl.c
    Opus/wordtech/editsub.c
    Opus/wordtech/editspec.c
    Opus/wordtech/error.c
    Opus/wordtech/fetch.c
    Opus/wordtech/fetch1.c
    Opus/wordtech/fetch2.c
    Opus/wordtech/fetchtb.c
    Opus/wordtech/footnote.c
    Opus/wordtech/format.c
    Opus/wordtech/hdd.c
    Opus/wordtech/hyph.c
    Opus/wordtech/ihdd.c
    Opus/wordtech/layout.c
    Opus/wordtech/layout1.c
    Opus/wordtech/layout2.c
    Opus/wordtech/outline.c
    Opus/wordtech/layoutap.c
    Opus/wordtech/pagevw.c
    Opus/wordtech/printsub.c
    Opus/wordtech/prcsubs.c
    Opus/wordtech/savefast.c
    Opus/wordtech/scroll.c
    Opus/wordtech/select.c
    Opus/wordtech/selectsp.c
    Opus/wordtech/sttb.c
    Opus/wordtech/stysubs.c
    Opus/wordtech/selecttb.c
    Opus/wordtech/tablecmd.c
    Opus/wordtech/tableins.c
    Opus/wordtech/tablesub.c
    Opus/wordtech/undo.c
    Opus/SPELL.C
    Opus/spelcore.c
    port/original/opus_asm_layout2.cpp
    port/original/opus_asm_file2.cpp
    port/original/opus_asm_filewin.cpp
    port/original/opus_asm_math.cpp
    port/original/opus_asm_natinit_format.cpp
    port/original/opus_asm_graphics.cpp
    port/original/opus_asm_fetch3.cpp
    port/original/opus_asm_prompt.cpp
    port/original/opus_asm_misc.cpp
    port/original/opus_asm_movecmds.c
    port/original/opus_asm_fetch_adapters.cpp
    port/original/opus_asm_display_adapters.cpp
    port/original/opus_asm_plc_adapters.cpp
    port/original/opus_asm_resn2_adapters.cpp
    port/original/opus_asm_native_adapters.cpp
    port/original/opus_asm_wproc.cpp
    port/original/opus_win95_chrome.cpp
    port/original/opus_x64_segment_anchors.c
)

if(OPUS_WINELIB_BUILD)
    # winegcc's driver script does not forward -x to the underlying gcc
    # (confirmed in reconnaissance, §5.5), so CMake's usual
    # "set_source_files_properties(... LANGUAGE C)" override -- already
    # applied below for MSVC's benefit -- cannot force these 17 uppercase
    # .C sources to compile as C: winegcc still dispatches purely on the
    # file extension, and .C (like g++) means C++. Give it files whose
    # extension already says C, instead of fighting the driver script.
    set(OPUS_CASE_SENSITIVE_C_SOURCES
        CLIPBORD.C CLIPBRD2.C CMD3.C CREATE2.C DDESRVR.C DLBENUM.C EDIT.C
        FIELDCMD.C FILE2.C GRSPEC.C LOADFONT.C PIC2.C RTFIN.C RTFOUT.C
        RTFRARE.C SCREEN2.C SPELL.C SYSCHG.C
    )
    set(OPUS_LOWERCASE_C_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/lowercase-c")
    file(MAKE_DIRECTORY "${OPUS_LOWERCASE_C_DIR}")
    foreach(opus_upper_c IN LISTS OPUS_CASE_SENSITIVE_C_SOURCES)
        string(TOLOWER "${opus_upper_c}" opus_lower_c)
        string(REPLACE "." "\\." opus_upper_c_escaped "${opus_upper_c}")
        set(opus_lower_c_path "${OPUS_LOWERCASE_C_DIR}/${opus_lower_c}")
        if(NOT EXISTS "${opus_lower_c_path}")
            file(CREATE_LINK
                "${CMAKE_CURRENT_SOURCE_DIR}/Opus/${opus_upper_c}"
                "${opus_lower_c_path}"
                SYMBOLIC
            )
        endif()
        list(TRANSFORM OPUS_ORIGINAL_ENGINE_SOURCES
            REPLACE "^.*${opus_upper_c_escaped}$" "${opus_lower_c_path}")
    endforeach()
endif()

add_library(opus_original_engine STATIC ${OPUS_ORIGINAL_ENGINE_SOURCES})

add_dependencies(opus_original_engine
    opus_generated_commands
    opus_generated_cursors
    opus_generated_embedded_resources
    opus_generated_dib_resources
    opus_generated_dlgcheck
    opus_generated_elxinfo
)
set_source_files_properties(Opus/wordtech/fetch2.c PROPERTIES
    COMPILE_DEFINITIONS CC
)
set_source_files_properties(Opus/wordtech/fetch.c Opus/wordtech/fetch1.c Opus/wordtech/format.c Opus/wordtech/inssubs.c Opus/wordtech/fetchtb.c Opus/wordtech/editspec.c Opus/wordtech/layout.c PROPERTIES
    COMPILE_DEFINITIONS DEBUGORNOTWIN
)
set_source_files_properties(Opus/wordtech/clsplc.c PROPERTIES
    COMPILE_DEFINITIONS DEBUGORNOTWIN
)
set_source_files_properties(Opus/wordtech/layout1.c Opus/wordtech/layout2.c PROPERTIES
    COMPILE_DEFINITIONS DEBUGORNOTWIN
)
set_source_files_properties(Opus/wordtech/disp2.c PROPERTIES
    COMPILE_DEFINITIONS DEBUGORNOTWIN
)
set_source_files_properties(Opus/wordtech/disptbl.c PROPERTIES
    COMPILE_DEFINITIONS DEBUGORNOTWIN
)
set_source_files_properties(Opus/disp1.c PROPERTIES
    COMPILE_DEFINITIONS "NOASM;OPUS_X64_DISPLAY_C"
)
# Opus/CMD3.C joins this list because of a pre-existing typo elsewhere in
# this file, not a new platform quirk: the source list above used to spell
# it "Opus/cmd3.c" (lowercase), which NTFS's case-insensitivity silently
# resolved to this same file for MSVC, while also making MSVC's own
# extension-based C/C++ dispatch treat it as C without needing this LANGUAGE
# override -- the way every other correctly-lowercase .c file in that list
# still does. Linux's case-sensitive filesystem surfaced the typo (the
# source list now spells it correctly, "Opus/CMD3.C"), which means MSVC's
# dispatch needs the same explicit override its 17 uppercase siblings
# already had.
set_source_files_properties(Opus/CLIPBORD.C Opus/CLIPBRD2.C Opus/CMD3.C Opus/CREATE2.C Opus/DDESRVR.C Opus/DLBENUM.C Opus/EDIT.C Opus/FIELDCMD.C Opus/FILE2.C Opus/GRSPEC.C Opus/LOADFONT.C Opus/PIC2.C Opus/RTFIN.C Opus/RTFOUT.C Opus/RTFRARE.C Opus/SCREEN2.C Opus/SPELL.C Opus/SYSCHG.C PROPERTIES LANGUAGE C)

set(OPUS_CASE_SHIM_INCLUDE_DIRS)
if(OPUS_WINELIB_BUILD)
    # Linux's case-sensitive filesystem exposes 12 "#include" spellings in
    # src/Opus/ that do not match the case of the file actually on disk
    # (confirmed in reconnaissance, §7 -- e.g. "#include \"debug.h\"" against
    # Opus/DEBUG.H, 202 files affected by that one pair alone). Windows never
    # needed this: NTFS resolves both spellings to the same file. A directory
    # of symlinks prepended to the include path lets the original #include
    # lines stay exactly as Microsoft wrote them, in src/Opus/, untouched.
    set(OPUS_CASE_SHIM_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/case-shim")
    file(MAKE_DIRECTORY "${OPUS_CASE_SHIM_DIR}")
    set(OPUS_CASE_SHIM_PAIRS
        "debug.h=Opus/DEBUG.H"
        "screen.h=Opus/SCREEN.H"
        "rareflag.h=Opus/RAREFLAG.H"
        "resource.h=Opus/RESOURCE.H"
        "pic.h=Opus/PIC.H"
        "rtf.h=Opus/RTF.H"
        "rtftbl.h=Opus/RTFTBL.H"
        "spell.h=Opus/SPELL.H"
        "pic3.c=Opus/PIC3.C"
        "rtfin2.c=Opus/RTFIN2.C"
        "saveFast.h=Opus/wordtech/savefast.h"
        "Windows.h=port/original/windows.h"
        "DbgHelp.h=port/original/DbgHelp.h"
    )
    foreach(opus_case_pair IN LISTS OPUS_CASE_SHIM_PAIRS)
        string(REPLACE "=" ";" opus_case_pair "${opus_case_pair}")
        list(GET opus_case_pair 0 opus_case_shim_name)
        list(GET opus_case_pair 1 opus_case_shim_target)
        set(opus_case_shim_link "${OPUS_CASE_SHIM_DIR}/${opus_case_shim_name}")
        if(NOT EXISTS "${opus_case_shim_link}")
            file(CREATE_LINK
                "${CMAKE_CURRENT_SOURCE_DIR}/${opus_case_shim_target}"
                "${opus_case_shim_link}"
                SYMBOLIC
            )
        endif()
    endforeach()

    # The same mismatch exists for headers this build *generates*, not just for
    # the ones checked into src/Opus/. MKCMD writes IBCM.H / RGBCM.H /
    # OPUSCMD2.H / OPUSMENU.H in uppercase (and the ibcm.rel copy rule spells
    # IBCM.H uppercase too), while src/Opus/ includes all four in lowercase --
    # e.g. command2.c:1515 "#include \"rgbcm.h\"". On NTFS both spellings hit
    # the same file; here they do not.
    #
    # These four are linked separately from the pairs above because their
    # targets live in the build tree, not the source tree, and because they do
    # not exist yet when this runs: the symlinks are created dangling at
    # configure time and resolve once MKCMD has run. IS_SYMLINK, not EXISTS, is
    # what tests that correctly -- EXISTS follows the link and reports false
    # while the target is still missing, which would retry creation on every
    # reconfigure.
    set(OPUS_CASE_SHIM_GENERATED_PAIRS
        "ibcm.h=IBCM.H"
        "rgbcm.h=RGBCM.H"
        "opuscmd2.h=OPUSCMD2.H"
        "opusmenu.h=OPUSMENU.H"
    )
    foreach(opus_case_pair IN LISTS OPUS_CASE_SHIM_GENERATED_PAIRS)
        string(REPLACE "=" ";" opus_case_pair "${opus_case_pair}")
        list(GET opus_case_pair 0 opus_case_shim_name)
        list(GET opus_case_pair 1 opus_case_shim_target)
        set(opus_case_shim_link "${OPUS_CASE_SHIM_DIR}/${opus_case_shim_name}")
        if(NOT IS_SYMLINK "${opus_case_shim_link}")
            file(CREATE_LINK
                "${OPUS_GENERATED_COMMAND_DIR}/${opus_case_shim_target}"
                "${opus_case_shim_link}"
                SYMBOLIC
            )
        endif()
    endforeach()

    set(OPUS_CASE_SHIM_INCLUDE_DIRS "${OPUS_CASE_SHIM_DIR}")
endif()

set(OPUS_ORIGINAL_INCLUDE_DIRS
    ${OPUS_CASE_SHIM_INCLUDE_DIRS}
    "${CMAKE_CURRENT_BINARY_DIR}/generated/original"
    "${CMAKE_CURRENT_SOURCE_DIR}/port/original"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/Opus/wordtech"
    "${CMAKE_CURRENT_SOURCE_DIR}/Opus/lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/Opus/dlg"
    "${CMAKE_CURRENT_SOURCE_DIR}/Opus/resource"
    "${CMAKE_CURRENT_SOURCE_DIR}/Opus"
)
target_compile_definitions(opus_original_engine PRIVATE
    WIN
    WIN23
    CRLF
    NONATIVE
    OPUS_X64
    NOMINMAX
)
target_include_directories(opus_original_engine PRIVATE ${OPUS_ORIGINAL_INCLUDE_DIRS})
if(MSVC)
    # /J preserves the original compiler's unsigned-char default. Do not use
    # the C++ compiler for K&R-era original C translation units.
    target_compile_options(opus_original_engine PRIVATE /J /W3 /wd4005 /Gy)
elseif(OPUS_WINELIB_BUILD)
    # -std=gnu89: this is 1980s K&R-era C (old-style function definitions,
    # implicit int). -funsigned-char: /J's GCC equivalent -- Opus was built
    # with unsigned plain characters (see port/original/opus_x64_compat.h).
    # -fms-extensions: accepts MSVC's nameless-struct-member extension,
    # which struct PAP/PAPS (Opus/wordtech/props.h) depends on.
    # -fpermissive: downgrades a handful of stricter-in-GCC diagnostics
    # (old-style K&R prototype mismatches) to warnings. Applied only to C
    # sources: this target also holds ~20 .cpp translation units
    # (port/original/opus_asm_*.cpp) that -std=gnu89 would reject outright.
    target_compile_options(opus_original_engine PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-std=gnu89 -funsigned-char -fms-extensions -fpermissive>
    )
endif()
set_target_properties(opus_original_engine PROPERTIES
    FOLDER "Original Opus x64"
    ARCHIVE_OUTPUT_DIRECTORY "${MSWORD_BUILD_ROOT}/lib"
)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(opus_original_engine PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${MSWORD_BUILD_ROOT}/lib/${config}"
    )
endforeach()

add_library(opus_x64_runtime STATIC
    port/original/opus_original_chupper.c
    port/original/opus_elx_dispatch.cpp
    port/original/opus_startup_diagnostics.cpp
    port/original/opus_win16_platform.cpp
    port/original/opus_x64_heap.cpp
    port/original/opus_x64_layout.c
    port/original/opus_asm_fetchn2.cpp
    port/original/opus_asm_resn.cpp
    port/original/opus_asm_resn_core.cpp
    port/original/opus_asm_resn_plc.cpp
    port/original/opus_asm_resn2.cpp
    port/original/opus_asm_resn2_access.cpp
    port/original/opus_asm_resn2_pl.cpp
    port/original/opus_asm_resn2_sttb.cpp
    port/original/opus_asm_sttb.cpp
    port/original/opus_asm_search.cpp
    port/original/opus_sdm_cab.cpp
    port/original/opus_sdm_runtime.cpp
)
target_compile_features(opus_x64_runtime PRIVATE cxx_std_20)
target_compile_definitions(opus_x64_runtime PRIVATE
    WIN
    WIN23
    CRLF
    NONATIVE
    OPUS_X64
    NOMINMAX
)
target_include_directories(opus_x64_runtime PRIVATE ${OPUS_ORIGINAL_INCLUDE_DIRS})
if(MSVC)
    # Opus was built with unsigned plain characters; the byte-oriented string
    # and international-character assembly routines depend on that behavior.
    target_compile_options(opus_x64_runtime PRIVATE /J /W4 /permissive-)
elseif(OPUS_WINELIB_BUILD)
    # This target's two C sources (opus_original_chupper.c,
    # opus_x64_layout.c) are K&R-era like the engine; same rationale as
    # opus_original_engine's matching block above. Applied only to C
    # sources: the other 16 translation units here are C++.
    target_compile_options(opus_x64_runtime PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-std=gnu89 -funsigned-char -fms-extensions -fpermissive>
    )
endif()
set_target_properties(opus_x64_runtime PROPERTIES
    FOLDER "Original Opus x64/runtime"
    ARCHIVE_OUTPUT_DIRECTORY "${MSWORD_BUILD_ROOT}/lib"
    VS_GLOBAL_VcpkgApplocalDeps "false"
)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(opus_x64_runtime PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${MSWORD_BUILD_ROOT}/lib/${config}"
    )
endforeach()

add_executable(opus_original_sttb_test
    Opus/wordtech/sttb.c
    port/original/opus_original_sttb_test.c
)
target_compile_definitions(opus_original_sttb_test PRIVATE
    WIN WIN23 CRLF NONATIVE OPUS_X64 NOMINMAX
)
target_include_directories(opus_original_sttb_test PRIVATE ${OPUS_ORIGINAL_INCLUDE_DIRS})
target_link_libraries(opus_original_sttb_test PRIVATE opus_x64_runtime user32)
if(MSVC)
    target_compile_options(opus_original_sttb_test PRIVATE /J /W3 /wd4005)
endif()
set_target_properties(opus_original_sttb_test PROPERTIES
    FOLDER "Original Opus x64/tests"
    RUNTIME_OUTPUT_DIRECTORY "${MSWORD_BUILD_ROOT}/tests"
    VS_GLOBAL_VcpkgApplocalDeps "false"
)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(opus_original_sttb_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${MSWORD_BUILD_ROOT}/tests/${config}"
    )
endforeach()

add_executable(opus_original_plc_test
    Opus/wordtech/clsplc.c
    port/original/opus_asm_plc_adapters.cpp
    port/original/opus_original_plc_test.c
)
target_compile_definitions(opus_original_plc_test PRIVATE
    WIN WIN23 CRLF NONATIVE OPUS_X64 NOMINMAX
)
target_include_directories(opus_original_plc_test PRIVATE ${OPUS_ORIGINAL_INCLUDE_DIRS})
target_link_libraries(opus_original_plc_test PRIVATE opus_x64_runtime user32)
if(MSVC)
    target_compile_options(opus_original_plc_test PRIVATE /J /W3 /wd4005)
endif()
set_target_properties(opus_original_plc_test PROPERTIES
    FOLDER "Original Opus x64/tests"
    RUNTIME_OUTPUT_DIRECTORY "${MSWORD_BUILD_ROOT}/tests"
    VS_GLOBAL_VcpkgApplocalDeps "false"
)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(opus_original_plc_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${MSWORD_BUILD_ROOT}/tests/${config}"
    )
endforeach()

add_executable(opus_sdm_cab_test
    port/original/opus_sdm_cab_test.cpp
)
target_compile_features(opus_sdm_cab_test PRIVATE cxx_std_20)
target_compile_definitions(opus_sdm_cab_test PRIVATE
    WIN WIN23 CRLF NONATIVE OPUS_X64 NOMINMAX
)
target_include_directories(opus_sdm_cab_test PRIVATE ${OPUS_ORIGINAL_INCLUDE_DIRS})
target_link_libraries(opus_sdm_cab_test PRIVATE opus_x64_runtime user32)
if(MSVC)
    target_compile_options(opus_sdm_cab_test PRIVATE /J /W4 /permissive-)
endif()
set_target_properties(opus_sdm_cab_test PROPERTIES
    FOLDER "Original Opus x64/tests"
    RUNTIME_OUTPUT_DIRECTORY "${MSWORD_BUILD_ROOT}/tests"
    VS_GLOBAL_VcpkgApplocalDeps "false"
)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(opus_sdm_cab_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${MSWORD_BUILD_ROOT}/tests/${config}"
    )
endforeach()

add_executable(opus_original_command_test
    port/original/opus_original_command_test.c
)
add_dependencies(opus_original_command_test opus_generated_commands)
target_include_directories(opus_original_command_test PRIVATE
    "${OPUS_GENERATED_COMMAND_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/Opus"
)
if(MSVC)
    target_compile_options(opus_original_command_test PRIVATE /W4)
endif()
set_target_properties(opus_original_command_test PROPERTIES
    FOLDER "Original Opus x64/tests"
    RUNTIME_OUTPUT_DIRECTORY "${MSWORD_BUILD_ROOT}/tests"
    VS_GLOBAL_VcpkgApplocalDeps "false"
)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(opus_original_command_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${MSWORD_BUILD_ROOT}/tests/${config}"
    )
endforeach()

# Future product executables link this original-source archive and inherit the
# translated assembly/native-handle runtime at the library boundary.
target_link_libraries(opus_original_engine PUBLIC opus_x64_runtime user32)

# Product executable for Visual Studio's Startup Item list. The Unicode entry
# adapter converts the modern process command line, then transfers control to
# Microsoft's original Opus WinMain and its original window/message loop.
set(WORD1_SOURCES port/original/opus_original_startup_probe.cpp)
if(WIN32)
    list(APPEND WORD1_SOURCES port/word1.rc port/winword.manifest)
endif()
# Under OPUS_WINELIB_BUILD, port/word1.rc is instead compiled by wrc into a
# .res and linked in directly (Fase 4 of docs/port-linux/00-reconocimiento.md);
# RC is not an enabled CMake language there (see the project() call above),
# so these two sources cannot be listed on this add_executable yet.
add_executable(WORD1 WIN32 ${WORD1_SOURCES})
add_dependencies(WORD1 opus_original_engine)
target_compile_features(WORD1 PRIVATE cxx_std_20)
target_compile_definitions(WORD1 PRIVATE
    WIN WIN23 CRLF NONATIVE OPUS_X64 NOMINMAX UNICODE _UNICODE
)
target_link_libraries(WORD1 PRIVATE opus_original_engine opus_x64_runtime user32 dbghelp)
if(MSVC)
    target_compile_options(WORD1 PRIVATE /J /W3 /wd4005 /utf-8)
    target_link_options(WORD1 PRIVATE /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT /MANIFEST:NO)
endif()
if(OPUS_WINELIB_BUILD)
    # Only the headers WORD1's port entry needs.  Do NOT put Opus/ on the
    # include path: Wine's windows.h includes "dde.h", and Opus/dde.h would
    # shadow Wine's (case-insensitive NTFS never hit this).
    target_include_directories(WORD1 PRIVATE
        ${OPUS_CASE_SHIM_INCLUDE_DIRS}
        "${CMAKE_CURRENT_BINARY_DIR}/generated/original"
        "${CMAKE_CURRENT_SOURCE_DIR}/port/original"
    )

    find_program(OPUS_WRC_EXECUTABLE wrc REQUIRED
        DOC "Wine resource compiler (wrc) for port/word1.rc")
    find_program(OPUS_WINEBUILD_EXECUTABLE winebuild
        DOC "Wine module builder (optional; winegcc invokes it for .spec)")

    set(WORD1_SPEC "${CMAKE_CURRENT_BINARY_DIR}/generated/original/word1.spec")
    set(WORD1_RES  "${CMAKE_CURRENT_BINARY_DIR}/generated/original/word1.res")

    # Ruta B: transform MKCMD's opuscmd_native.inc into a winebuild .spec.
    # Same 427 /export: names; no second parse of the command tables and no
    # edit of restricted OpusEtAl/tools/src/mkcmd.c.
    add_custom_command(
        OUTPUT "${WORD1_SPEC}"
        COMMAND "${CMAKE_COMMAND}"
            -DINPUT=${OPUS_GENERATED_COMMAND_DIR}/opuscmd_native.inc
            -DOUTPUT=${WORD1_SPEC}
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateWord1Spec.cmake"
        DEPENDS
            "${OPUS_GENERATED_COMMAND_DIR}/opuscmd_native.inc"
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateWord1Spec.cmake"
        COMMENT "Generating word1.spec (${MSWORD_PRODUCT_OUTPUT_NAME} exports) from opuscmd_native.inc"
        VERBATIM
    )

    # wrc: compile product resources.  Absolute -I/-o paths (Wine 11 wrc failed
    # to open the .res when -fo was used with WORKING_DIRECTORY=port/).  The
    # relative asset paths in word1.rc resolve because wrc searches -I for them.
    add_custom_command(
        OUTPUT "${WORD1_RES}"
        COMMAND "${OPUS_WRC_EXECUTABLE}"
            -I "${CMAKE_CURRENT_SOURCE_DIR}/port"
            -o "${WORD1_RES}"
            "${CMAKE_CURRENT_SOURCE_DIR}/port/word1.rc"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/port/word1.rc"
            "${CMAKE_CURRENT_SOURCE_DIR}/port/winword.manifest"
            "${CMAKE_CURRENT_SOURCE_DIR}/port/assets/word95-toolbar.bmp"
            "${CMAKE_CURRENT_SOURCE_DIR}/port/icons/ICON8_1.ico"
            "${CMAKE_CURRENT_SOURCE_DIR}/port/icons/ICON2_1.ico"
        COMMENT "Compiling port/word1.rc with wrc → word1.res"
        VERBATIM
    )

    # winegcc/wineg++ treat .spec and .res on the link line as module-definition
    # and resource inputs (not as objects for ld).  EXTERNAL_OBJECT keeps CMake
    # from trying to compile them with the C++ compiler.
    set_source_files_properties("${WORD1_SPEC}" "${WORD1_RES}" PROPERTIES
        EXTERNAL_OBJECT TRUE
        GENERATED TRUE
    )
    target_sources(WORD1 PRIVATE "${WORD1_SPEC}" "${WORD1_RES}")
    add_dependencies(WORD1 opus_generated_commands)

    # GUI subsystem + Unicode entry (wWinMain).  winecrt0 looks for WinMain
    # unless -municode selects the wide entry point.
    target_link_options(WORD1 PRIVATE -mwindows -municode)
endif()
set_target_properties(WORD1 PROPERTIES
    FOLDER "Original Opus x64/product"
    OUTPUT_NAME "${MSWORD_PRODUCT_OUTPUT_NAME}"
    VS_DEBUGGER_WORKING_DIRECTORY "${MSWORD_BIN_ROOT}"
    VS_GLOBAL_VcpkgApplocalDeps "false"
)

# Diagnostic executable that force-links the same original startup graph as
# WORD1 without product resources. It remains useful as an all-symbol gate.
add_executable(opus_original_startup_probe EXCLUDE_FROM_ALL WIN32
    port/original/opus_original_startup_probe.cpp
)
add_dependencies(opus_original_startup_probe opus_original_engine)
target_compile_features(opus_original_startup_probe PRIVATE cxx_std_20)
target_compile_definitions(opus_original_startup_probe PRIVATE
    WIN WIN23 CRLF NONATIVE OPUS_X64 NOMINMAX UNICODE _UNICODE
)
target_link_libraries(opus_original_startup_probe PRIVATE opus_original_engine opus_x64_runtime user32 dbghelp)
set_target_properties(opus_original_startup_probe PROPERTIES
    FOLDER "Original Opus x64/port probes"
    RUNTIME_OUTPUT_DIRECTORY "${MSWORD_BUILD_ROOT}/probes"
    VS_GLOBAL_VcpkgApplocalDeps "false"
)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(opus_original_startup_probe PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${MSWORD_BUILD_ROOT}/probes/${config}"
    )
endforeach()

add_executable(opus_original_strtbl_test
    Opus/strtbl.c
    port/original/opus_original_strtbl_test.cpp
)
target_compile_definitions(opus_original_strtbl_test PRIVATE
    WIN WIN23 CRLF NONATIVE OPUS_X64 NOMINMAX
)
target_include_directories(opus_original_strtbl_test PRIVATE ${OPUS_ORIGINAL_INCLUDE_DIRS})
target_link_libraries(opus_original_strtbl_test PRIVATE user32)
if(MSVC)
    target_compile_options(opus_original_strtbl_test PRIVATE /J /W3 /wd4005)
endif()
set_target_properties(opus_original_strtbl_test PROPERTIES
    FOLDER "Original Opus x64/tests"
    RUNTIME_OUTPUT_DIRECTORY "${MSWORD_BUILD_ROOT}/tests"
    VS_GLOBAL_VcpkgApplocalDeps "false"
)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(opus_original_strtbl_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${MSWORD_BUILD_ROOT}/tests/${config}"
    )
endforeach()

add_executable(opus_x64_runtime_test
    port/original/opus_x64_runtime_test.cpp
    port/original/opus_x64_layout_test_fixture.c
)
target_compile_features(opus_x64_runtime_test PRIVATE cxx_std_20)
target_compile_definitions(opus_x64_runtime_test PRIVATE
    WIN WIN23 CRLF NONATIVE OPUS_X64 NOMINMAX
)
target_include_directories(opus_x64_runtime_test PRIVATE ${OPUS_ORIGINAL_INCLUDE_DIRS})
target_link_libraries(opus_x64_runtime_test PRIVATE opus_x64_runtime user32 gdi32)
set_target_properties(opus_x64_runtime_test PROPERTIES
    FOLDER "Original Opus x64/tests"
    RUNTIME_OUTPUT_DIRECTORY "${MSWORD_BUILD_ROOT}/tests"
    VS_GLOBAL_VcpkgApplocalDeps "false"
)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(opus_x64_runtime_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${MSWORD_BUILD_ROOT}/tests/${config}"
    )
endforeach()

add_executable(opus_word1_ui_test
    port/original/opus_word1_ui_test.cpp
)
add_dependencies(opus_word1_ui_test WORD1)
target_compile_features(opus_word1_ui_test PRIVATE cxx_std_20)
target_compile_definitions(opus_word1_ui_test PRIVATE NOMINMAX UNICODE _UNICODE)
target_link_libraries(opus_word1_ui_test PRIVATE user32 gdi32)
if(MSVC)
    target_compile_options(opus_word1_ui_test PRIVATE /W4 /utf-8)
endif()
set_target_properties(opus_word1_ui_test PROPERTIES
    FOLDER "Original Opus x64/tests"
    RUNTIME_OUTPUT_DIRECTORY "${MSWORD_BUILD_ROOT}/tests"
    VS_GLOBAL_VcpkgApplocalDeps "false"
)
foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(opus_word1_ui_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${MSWORD_BUILD_ROOT}/tests/${config}"
    )
endforeach()

enable_testing()
add_test(NAME opus_original_strtbl_test COMMAND $<TARGET_FILE:opus_original_strtbl_test>)
add_test(NAME opus_x64_runtime_test COMMAND $<TARGET_FILE:opus_x64_runtime_test>)
add_test(NAME opus_original_sttb_test COMMAND $<TARGET_FILE:opus_original_sttb_test>)
add_test(NAME opus_original_plc_test COMMAND $<TARGET_FILE:opus_original_plc_test>)
add_test(NAME opus_sdm_cab_test COMMAND $<TARGET_FILE:opus_sdm_cab_test>)
add_test(NAME opus_original_command_test COMMAND $<TARGET_FILE:opus_original_command_test>)
if(OPUS_WINELIB_BUILD)
    add_test(NAME opus_shell_config_test COMMAND $<TARGET_FILE:opus_shell_config_test>)
    add_test(NAME opus_shell_font_substitution_test COMMAND $<TARGET_FILE:opus_shell_font_substitution_test>)
endif()

# The tests below all launch the WORD1 binary itself (directly or via
# opus_word1_ui_test). WORD1 currently hits heap corruption during
# startup/constructors before it reaches a usable state (see README
# "Known blocker"), so none of these can be expected to pass yet. They are
# still registered and executed -- not skipped -- so CI has visibility into
# when the startup blocker is fixed, but they are labeled
# "word1_startup_blocked" so CI can run them non-gating (report, don't fail
# the build) until that happens. Once WORD1 starts cleanly, drop the LABELS
# line (and, for the 8 UI variants, confirm a graphical session/Xvfb is
# still provided) and let them gate CI like the tests above.
add_test(NAME word1_port_smoke_test COMMAND $<TARGET_FILE:WORD1> --self-test)
set_tests_properties(word1_port_smoke_test PROPERTIES LABELS "word1_startup_blocked")
add_test(NAME opus_word1_ui_test
    COMMAND $<TARGET_FILE:opus_word1_ui_test> $<TARGET_FILE:WORD1>
)
set_tests_properties(opus_word1_ui_test PROPERTIES TIMEOUT 20 LABELS "word1_startup_blocked")
add_test(NAME opus_word1_clipboard_shortcut_test
    COMMAND $<TARGET_FILE:opus_word1_ui_test> $<TARGET_FILE:WORD1> --clipboard
)
set_tests_properties(opus_word1_clipboard_shortcut_test PROPERTIES TIMEOUT 20 LABELS "word1_startup_blocked")
add_test(NAME opus_word1_typing_test
    COMMAND $<TARGET_FILE:opus_word1_ui_test> $<TARGET_FILE:WORD1> --typing
)
set_tests_properties(opus_word1_typing_test PROPERTIES TIMEOUT 20 LABELS "word1_startup_blocked")
add_test(NAME opus_word1_interaction_test
    COMMAND $<TARGET_FILE:opus_word1_ui_test> $<TARGET_FILE:WORD1> --interaction
)
set_tests_properties(opus_word1_interaction_test PROPERTIES TIMEOUT 25 LABELS "word1_startup_blocked")
add_test(NAME opus_word1_selection_test
    COMMAND $<TARGET_FILE:opus_word1_ui_test> $<TARGET_FILE:WORD1> --selection
)
set_tests_properties(opus_word1_selection_test PROPERTIES TIMEOUT 20 LABELS "word1_startup_blocked")
add_test(NAME opus_word1_font_typing_test
    COMMAND $<TARGET_FILE:opus_word1_ui_test> $<TARGET_FILE:WORD1> --font-typing
)
set_tests_properties(opus_word1_font_typing_test PROPERTIES TIMEOUT 20 LABELS "word1_startup_blocked")
add_test(NAME opus_word1_about_test
    COMMAND $<TARGET_FILE:opus_word1_ui_test> $<TARGET_FILE:WORD1> --about
)
set_tests_properties(opus_word1_about_test PROPERTIES TIMEOUT 20 LABELS "word1_startup_blocked")
add_test(NAME opus_word1_save_as_test
    COMMAND $<TARGET_FILE:opus_word1_ui_test> $<TARGET_FILE:WORD1> --save-as
)
set_tests_properties(opus_word1_save_as_test PROPERTIES TIMEOUT 20 LABELS "word1_startup_blocked")
