/* Lado GDI: reproduce la forma exacta de Opus/dispspec.c:787,796
   dxwChar = GetTextExtent(hdc, &ch, 1) - tm.tmOverhang
   Bajo Winelib, GetTextExtentPoint32 es el equivalente Win32. */
#include <windows.h>
#include <stdio.h>

static void dump(const char *face, int points)
{
    HDC hdc = CreateDCA("DISPLAY", NULL, NULL, NULL);
    if (!hdc) { printf("ERR no DC\n"); return; }
    int dpiX = GetDeviceCaps(hdc, LOGPIXELSX);
    int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);

    LOGFONTA lf;
    ZeroMemory(&lf, sizeof lf);
    /* Misma convención que el código: altura negativa = alto de carácter. */
    lf.lfHeight = -MulDiv(points, dpiY, 72);
    lf.lfCharSet = ANSI_CHARSET;
    lf.lfOutPrecision = OUT_TT_PRECIS;
    lstrcpynA(lf.lfFaceName, face, LF_FACESIZE);

    HFONT hf = CreateFontIndirectA(&lf);
    HFONT old = SelectObject(hdc, hf);

    TEXTMETRICA tm;
    GetTextMetricsA(hdc, &tm);
    char actual[LF_FACESIZE];
    GetTextFaceA(hdc, LF_FACESIZE, actual);

    printf("GDI face=%s actual=%s pt=%d dpi=%dx%d\n", face, actual, points, dpiX, dpiY);
    printf("GDI tm height=%ld ascent=%ld descent=%ld overhang=%ld avgw=%ld maxw=%ld\n",
           tm.tmHeight, tm.tmAscent, tm.tmDescent, tm.tmOverhang,
           tm.tmAveCharWidth, tm.tmMaxCharWidth);

    for (int ch = 32; ch < 127; ch++) {
        char c = (char)ch;
        SIZE sz;
        GetTextExtentPoint32A(hdc, &c, 1, &sz);
        printf("GDI adv %d %ld\n", ch, sz.cx - tm.tmOverhang);
    }
    SelectObject(hdc, old);
    DeleteObject(hf);
    DeleteDC(hdc);
}

int main(int argc, char **argv)
{
    const char *face = argc > 1 ? argv[1] : "Liberation Serif";
    int pt = argc > 2 ? atoi(argv[2]) : 10;
    dump(face, pt);
    return 0;
}
