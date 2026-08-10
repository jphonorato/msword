# toolchain-winelib.cmake
#
# Points CMake at winegcc/wineg++ to produce a Winelib ELF executable: not a
# PE .exe run under Wine, but the ELF that winegcc itself emits
# (<name>.exe.so, invoked through its <name>.exe stub), linked directly
# against Wine's own Win32 API implementation. See
# docs/port-linux/00-reconocimiento.md §6.1 for the reconnaissance that
# established this works with an unmodified CMake + Ninja setup.
#
# Selected by the linux-winelib-debug / linux-winelib-release presets in
# CMakePresets.json; not used by the Visual Studio presets, which this file
# does not touch.
#
# CMAKE_SYSTEM_NAME is deliberately left unset: this is not a cross build.
# winegcc produces an ordinary ELF binary for the running kernel, and CMake's
# normal native-compiler detection handles it once CMAKE_C_COMPILER /
# CMAKE_CXX_COMPILER point at it.

find_program(OPUS_WINEGCC_EXECUTABLE winegcc)
find_program(OPUS_WINEGXX_EXECUTABLE wineg++)

if(NOT OPUS_WINEGCC_EXECUTABLE)
    message(FATAL_ERROR
        "toolchain-winelib.cmake: winegcc was not found on PATH. Install "
        "wine-devel (Fedora) or the equivalent wine development package.")
endif()
if(NOT OPUS_WINEGXX_EXECUTABLE)
    message(FATAL_ERROR
        "toolchain-winelib.cmake: wineg++ was not found on PATH. Install "
        "wine-devel (Fedora) or the equivalent wine development package.")
endif()

set(CMAKE_C_COMPILER "${OPUS_WINEGCC_EXECUTABLE}" CACHE FILEPATH
    "C compiler (winegcc, produces a Winelib ELF)" FORCE)
set(CMAKE_CXX_COMPILER "${OPUS_WINEGXX_EXECUTABLE}" CACHE FILEPATH
    "C++ compiler (wineg++, produces a Winelib ELF)" FORCE)

# Read by src/CMakeLists.txt in place of WIN32 wherever the original source
# gate assumed a native Windows toolchain (the Windows Kits SDK path, the
# top-level FATAL_ERROR guard). Kept as a distinct marker rather than
# inferring from CMAKE_C_COMPILER's name so the rest of the project has one
# stable switch.
set(OPUS_WINELIB_BUILD TRUE CACHE BOOL
    "Building with the winegcc/wineg++ Winelib toolchain" FORCE)

# winegcc's own driver script does not behave like a conventional
# cross-compiler when CMake probes it for ABI info, which makes the
# "Detecting C/CXX compiler ABI info" configure step report failure. The
# compile+link itself does succeed (confirmed in reconnaissance, §6.1, and
# by the immediately following "Check for working C/CXX compiler" step,
# which passes) -- what fails is CMake's parsing of the *result*. winegcc's
# "<name>.exe" output is a POSIX shell-script stub, not an ELF; the actual
# ELF with the ABI markers CMake looks for is the sibling "<name>.exe.so".
# CMake's ABI-detection module inspects only the file it asked the linker to
# produce, so it reads the shell script and finds nothing, leaving
# CMAKE_<LANG>_SIZEOF_DATA_PTR empty and, downstream, CMAKE_SIZEOF_VOID_P
# unset -- which src/CMakeLists.txt's own x64 guard then rejects.
#
# winegcc only ever targets one architecture from this project's presets
# (x86-64; -m64 is baked into its own driver, confirmed in reconnaissance),
# so the pointer width is not actually in question here. Force it instead of
# trying to make CMake's ABI probe understand winegcc's two-file executable
# convention.
set(CMAKE_SIZEOF_VOID_P "8" CACHE STRING "" FORCE)

# winegcc names its output "<name>.exe" (the POSIX shell stub) alongside
# "<name>.exe.so" (the real ELF), the same two-file convention described
# above. CMake leaves CMAKE_EXECUTABLE_SUFFIX empty here because
# CMAKE_SYSTEM_NAME is not Windows -- this is treated as an ordinary native
# Linux build -- so "$<TARGET_FILE:...>" expands to a suffix-less path that
# does not exist on disk, and any custom command invoking a built tool fails
# with "No such file or directory" even though the link succeeded.
#
# Reproduced with a minimal CMake target unrelated to this project, so this is
# a property of the toolchain, not of any one target: set the suffix here
# rather than patching individual targets or custom commands.
set(CMAKE_EXECUTABLE_SUFFIX ".exe" CACHE STRING "" FORCE)
set(CMAKE_EXECUTABLE_SUFFIX_C ".exe" CACHE STRING "" FORCE)
set(CMAKE_EXECUTABLE_SUFFIX_CXX ".exe" CACHE STRING "" FORCE)
