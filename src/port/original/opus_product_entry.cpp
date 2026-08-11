/* No compilado por ningún target al 2026-08-11 (verificado: cero referencias en src/CMakeLists.txt, src/core/CMakeLists.txt, src/port/tools/host/CMakeLists.txt y *.cmake). Ver docs/superpowers/specs/2026-08-11-opus-memory-passthrough-checklist-audit.md §3.1. */

#include "opus_x64_compat.h"
#include "inter.h"

extern "C" struct ITR vitr = {};

extern "C" int IdFromSzgSz(int string_group, CHAR* text);
extern "C" int CchSzFromSzgId(CHAR* destination, int string_group, int id,
                               int capacity);

/* Original res.c behavior. Keeping it here allows the executable link gate to
 * exercise strtbl.c before the complete resident module dependency set links. */
extern "C" int ChUpperLookup(const int character) {
    const CHAR french_before = vitr.fFrench;
    vitr.fFrench = 1;
    const int upper = static_cast<unsigned char>(ChUpper(character));
    vitr.fFrench = french_before;
    return upper;
}

namespace {

int VerifyOriginalPortSlice() {
    constexpr int kNormalFieldStringGroup = 1;
    constexpr int kAuthorField = 17;

    CHAR keyword[] = "author";
    if (CchSz(keyword) != 7 ||
        IdFromSzgSz(kNormalFieldStringGroup, keyword) != kAuthorField) {
        return 1;
    }

    CHAR output[32] = {};
    const int count = CchSzFromSzgId(output, kNormalFieldStringGroup,
                                     kAuthorField,
                                     static_cast<int>(sizeof(output)));
    if (count != 7 || lstrcmpA(output, "AUTHOR") != 0) {
        return 2;
    }
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR command_line, int) {
    const int result = VerifyOriginalPortSlice();
    if (command_line != nullptr &&
        lstrcmpiW(command_line, L"--self-test") == 0) {
        return result;
    }

    if (result != 0) {
        MessageBoxW(nullptr,
                    L"The original Opus engine startup check failed.",
                    L"WORD1 x64 port", MB_OK | MB_ICONERROR);
        return result;
    }

    MessageBoxW(nullptr,
                L"The original Opus string/STTB/PLC foundation and 38 "
                L"translated assembly entry points are active.\n\nThe "
                L"original fetch, edit, undo, footnote, style, formatting, "
                L"page-layout, display, table-display, core file, document "
                L"creation/open, save, fast-save, text/RTF/spreadsheet "
                L"converter C modules now compile in the engine archive. "
                L"Microsoft's original WinMain, window procedures, and both "
                L"startup phases also compile. MKCMD regenerates the command, "
                L"menu, and key tables. The original UI, document/file, and "
                L"converter dependency graphs are not linked into this "
                L"executable yet.",
                L"WORD1 x64 source port", MB_OK | MB_ICONINFORMATION);
    return 0;
}
