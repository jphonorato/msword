cmake_minimum_required(VERSION 3.25)

# Post-link guard for the Winelib build.
#
# Opus reaches setjmp/longjmp through Opus/lib/qsetjmp.h, which on OPUS_X64
# uses the host <setjmp.h>; glibc expands setjmp(env) to _setjmp(env). Those
# names are also exported by a long list of Wine PE import archives (msvcrt,
# msvcr70..120, ucrtbase, vcruntime140, ntdll, ntoskrnl -- 14 of them on
# Debian 13), and Wine's versions are Microsoft-x64: they take the jmp_buf in
# RCX, not RDI. Binding to those from System V code makes setjmp write its
# 256-byte _JUMP_BUFFER over whatever stale pointer RCX holds -- the bug that
# smashed *vhpllbs during layout and cost three debugging rounds (see
# docs/port-linux/03-comportamiento-word1-startup-blocked.md, "Fix round 3").
#
# port/original/opus_x64_setjmp.cpp pins both halves by defining them in
# System V ABI, which stops winebuild from generating the import thunks. This
# script asserts that it worked, so any future toolchain, link-order or
# winegcc change fails the build loudly instead of resurfacing as a wild write
# during layout.
#
# Invoked as: cmake -DWORD1_BINARY=<path to the linked ELF> -P <this file>

if(NOT DEFINED WORD1_BINARY)
    message(FATAL_ERROR
        "AssertNoWineCrtSetjmp.cmake requires -DWORD1_BINARY=<path>")
endif()

if(NOT EXISTS "${WORD1_BINARY}")
    message(FATAL_ERROR
        "AssertNoWineCrtSetjmp.cmake: '${WORD1_BINARY}' does not exist")
endif()

find_program(OPUS_ASSERT_NM_EXECUTABLE NAMES nm llvm-nm)
if(NOT OPUS_ASSERT_NM_EXECUTABLE)
    # Not fatal: the guard is a safety net, not a build requirement.
    message(WARNING
        "AssertNoWineCrtSetjmp.cmake: no nm found, skipping the setjmp/longjmp "
        "ABI check on '${WORD1_BINARY}'")
    return()
endif()

execute_process(
    COMMAND "${OPUS_ASSERT_NM_EXECUTABLE}" "${WORD1_BINARY}"
    OUTPUT_VARIABLE opus_nm_output
    ERROR_VARIABLE opus_nm_error
    RESULT_VARIABLE opus_nm_result
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT opus_nm_result EQUAL 0)
    message(WARNING
        "AssertNoWineCrtSetjmp.cmake: nm failed on '${WORD1_BINARY}' "
        "(${opus_nm_result}): ${opus_nm_error}")
    return()
endif()

# Every Microsoft-x64 CRT entry point of this family. winebuild names its
# generated import thunks __imp_<symbol>, so their presence is exactly the
# condition we must never ship.
set(opus_forbidden_imports
    __imp__setjmp
    __imp__setjmpex
    __imp_longjmp
    __imp__longjmp
    __imp_siglongjmp
)

string(REGEX REPLACE ";" "\\\\;" opus_nm_output "${opus_nm_output}")
string(REPLACE "\n" ";" opus_nm_lines "${opus_nm_output}")

set(opus_found_imports "")
foreach(opus_nm_line IN LISTS opus_nm_lines)
    # nm lines end with the symbol name: "<addr> <type> <symbol>".
    string(REGEX REPLACE "^.* " "" opus_symbol "${opus_nm_line}")
    if(opus_symbol IN_LIST opus_forbidden_imports)
        list(APPEND opus_found_imports "${opus_symbol}")
    endif()
endforeach()

if(opus_found_imports)
    list(REMOVE_DUPLICATES opus_found_imports)
    string(REPLACE ";" ", " opus_found_text "${opus_found_imports}")
    message(FATAL_ERROR
        "'${WORD1_BINARY}' imports the Microsoft-x64 CRT setjmp/longjmp "
        "family from Wine: ${opus_found_text}.\n"
        "Those take the jmp_buf in RCX while all Opus code is System V and "
        "passes it in RDI, so setjmp would write its _JUMP_BUFFER over an "
        "unrelated pointer (this previously corrupted *vhpllbs during "
        "layout). Make sure port/original/opus_x64_setjmp.cpp is linked into "
        "this target and defines every symbol listed above.")
endif()
