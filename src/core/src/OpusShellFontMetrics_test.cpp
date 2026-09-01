/*
 * Prueba propia de OpusShellFontMetrics.cpp, contra el punto de dato
 * medido y documentado en docs/port-qt/01-core-shell-boundary.md §B2.3:
 * Liberation Serif a 14 pt (19 px a 96 ppp), glifo '@', FullHinting ->
 * 18 -- coincide con GDI bajo el oráculo Winelib. No repite la medición
 * contra el oráculo (eso ya está hecho y documentado); verifica que esta
 * implementación reproduce el número ya verificado.
 *
 * Requiere que Liberation Serif/Sans/Mono estén instaladas -- igual
 * salvaguarda que OpusShellFontSubstitution_test.cpp (falla la prueba en
 * vez de medir contra una fuente sustituta silenciosa).
 */

#include "OpusShellFontMetrics.h"

#include <QGuiApplication>

#include <cstdio>

namespace {

int g_failures = 0;

void Check(bool condition, const char *what) {
    if (!condition) {
        std::fprintf(stderr, "FALLO: %s\n", what);
        ++g_failures;
    }
}

}  // namespace

int main(int argc, char **argv) {
    /* QRawFont/QFontDatabase necesitan una QGuiApplication -- sin ella,
       cualquier construcción de QRawFont segfaulta (medido). Fuerza
       offscreen si no hay ya una plataforma elegida, para que la prueba
       corra headless en CI sin exigir QT_QPA_PLATFORM en el entorno. */
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QGuiApplication app(argc, argv);

    /* Caso central: Tms Rmn (-> Liberation Serif) 14pt, '@' == 18.
       ps en medios puntos: 14pt == 28. */
    {
        OpusFontKey key{0, 28, 0};
        unsigned short w = 0xFFFF;
        int rc = OpusShellCharWidths(&key, '@', 1, &w);
        Check(rc == 0, "OpusShellCharWidths(Tms Rmn, 14pt, '@') fallo");
        Check(w == 18,
              "OpusShellCharWidths(Tms Rmn, 14pt, '@') != 18 (ver "
              "01-core-shell-boundary.md Sec.B2.3)");
    }

    /* Metrics agregadas: debe tener éxito y devolver valores no
       degenerados para los 4 ftc soportados. */
    for (int ftc = 0; ftc < 4; ++ftc) {
        OpusFontKey key{ftc, 24 /* 12pt */, 0};
        OpusFontMetrics m{};
        int rc = OpusShellFontMetrics(&key, &m);
        Check(rc == 0, "OpusShellFontMetrics fallo para ftc soportado");
        Check(m.dypAscent > 0, "dypAscent <= 0");
        Check(m.dypDescent > 0, "dypDescent <= 0");
        Check(m.dxpInch == 96 && m.dypInch == 96, "resolucion != 96 ppp");
    }
    /* Courier (ftc=3) es el unico de paso fijo. */
    {
        OpusFontKey key{3, 24, 0};
        OpusFontMetrics m{};
        Check(OpusShellFontMetrics(&key, &m) == 0, "Courier fallo");
        Check(m.dxuFixed != 0, "Courier deberia ser dxuFixed != 0");
    }
    {
        OpusFontKey key{0, 24, 0};
        OpusFontMetrics m{};
        Check(OpusShellFontMetrics(&key, &m) == 0, "Tms Rmn fallo");
        Check(m.dxuFixed == 0, "Tms Rmn no deberia ser dxuFixed");
    }

    /* hps==0 (ps==0): GDI usa altura por defecto; el contrato no debe
       fallar -- LOADFONT trata el -1 como matFont y bloquea la UI. */
    {
        OpusFontKey key{2, 0, 0};
        unsigned short w = 0xFFFF;
        Check(OpusShellCharWidths(&key, 'A', 1, &w) == 0,
              "OpusShellCharWidths(Helv, ps=0) deberia medir a 10pt");
        Check(w > 0 && w != 0xFFFF,
              "OpusShellCharWidths(Helv, ps=0) ancho degenerado");
    }

    /* szFace: ftc >= 4 (fuera de la tabla de 4 nombres de época) mide si
       trae szFace (camino QFont::fromFont, ver RawFontFor), y sigue
       fallando controlado si no lo trae. */
    {
        OpusFontKey key{4, 24, 0, "Liberation Sans"};
        unsigned short w = 0xFFFF;
        Check(OpusShellCharWidths(&key, 'A', 1, &w) == 0,
              "ftc=4 szFace=Liberation Sans deberia medir");
        Check(w > 0 && w != 0xFFFF, "Liberation Sans ancho degenerado");
    }
    {
        OpusFontKey key{4, 24, 0, nullptr};
        unsigned short w = 0;
        Check(OpusShellCharWidths(&key, 'A', 1, &w) != 0,
              "ftc=4 sin szFace debe seguir fallando controlado");
    }

    /* Falla controlada: ftc fuera de rango, catr no soportado, args
       invalidos -- ninguno debe abortar ni devolver 0 exito. */
    {
        OpusFontKey badFtc{99, 24, 0};
        OpusFontMetrics m{};
        Check(OpusShellFontMetrics(&badFtc, &m) != 0,
              "ftc fuera de rango deberia fallar controlado");

        OpusFontKey badCatr{0, 24, 1};
        Check(OpusShellFontMetrics(&badCatr, &m) != 0,
              "catr != 0 deberia fallar controlado (limitacion 2)");

        unsigned short w = 0;
        Check(OpusShellCharWidths(nullptr, 0, 1, &w) != 0,
              "key nulo deberia fallar controlado");
        Check(OpusShellCharWidths(&badFtc, 0, 1, &w) != 0,
              "ftc fuera de rango en CharWidths deberia fallar controlado");

        OpusFontKey ok{0, 24, 0};
        Check(OpusShellCharWidths(&ok, 0, 300, &w) != 0,
              "rango fuera de [0,256) deberia fallar controlado");
        Check(OpusShellCharWidths(&ok, -1, 1, &w) != 0,
              "chFirst negativo deberia fallar controlado");
    }

    if (g_failures > 0) {
        std::fprintf(stderr, "%d fallo(s)\n", g_failures);
        return 1;
    }
    std::fprintf(stderr, "OK\n");
    return 0;
}
