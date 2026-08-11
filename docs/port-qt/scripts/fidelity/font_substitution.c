/* docs/port-qt/scripts/fidelity/font_substitution.c
 *
 * Sonda de un solo uso para §B2.6: mide a qué familia resuelve el oráculo
 * Winelib cada uno de los 4 nombres de época que Word 1.1a carga por
 * defecto (Opus/initwin.c, vhsttbFont). No forma parte del build.
 */
#include <windows.h>
#include <stdio.h>

int main(void) {
    HDC hdc = CreateDCA("DISPLAY", NULL, NULL, NULL);
    const char *names[] = { "Tms Rmn", "Symbol", "Helv", "Courier" };
    /* lfPitchAndFamily per name, matching Opus/initwin.c's boot table
       (ffid/prq pairs) exactly as Opus/LOADFONT.C:864 combines them:
       plf->lfPitchAndFamily = (pffn->ffid & maskFfFfid) | fcid.prq; */
    const BYTE pitchAndFamily[] = {
        FF_ROMAN | VARIABLE_PITCH,        /* Tms Rmn */
        FF_DECORATIVE | DEFAULT_PITCH,    /* Symbol */
        FF_SWISS | VARIABLE_PITCH,        /* Helv */
        FF_MODERN | FIXED_PITCH,          /* Courier */
    };
    for (unsigned k = 0; k < sizeof(names) / sizeof(*names); k++) {
        LOGFONTA lf;
        ZeroMemory(&lf, sizeof lf);
        lf.lfHeight = -MulDiv(14, GetDeviceCaps(hdc, LOGPIXELSY), 72);
        lf.lfWeight = FW_NORMAL;
        lf.lfItalic = 0;
        lf.lfCharSet = ANSI_CHARSET;
        lf.lfPitchAndFamily = pitchAndFamily[k];
        lstrcpynA(lf.lfFaceName, names[k], LF_FACESIZE);

        HFONT hf = CreateFontIndirectA(&lf);
        HFONT old = SelectObject(hdc, hf);

        TEXTMETRICA tm;
        GetTextMetricsA(hdc, &tm);
        char actual[LF_FACESIZE];
        GetTextFaceA(hdc, LF_FACESIZE, actual);

        printf("%-10s -> %-24s charset=%u overhang=%ld\n",
               names[k], actual, (unsigned)tm.tmCharSet, tm.tmOverhang);

        SelectObject(hdc, old);
        DeleteObject(hf);
    }
    DeleteDC(hdc);
    return 0;
}
