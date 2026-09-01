/*
 * Prueba de fidelidad amplia de OpusShellFontMetrics -- extiende el punto
 * de dato único de OpusShellFontMetrics_test.cpp a las 2660 comparaciones
 * capturadas del oráculo Winelib real (4 nombres de época x 7 tamaños x
 * 95 caracteres ASCII imprimibles), vía
 * opus_shell_font_metrics_oracle_table.h -- ver el comentario de
 * cabecera de esa tabla para cómo y cuándo se capturó, y
 * docs/port-qt/01-core-shell-boundary.md §B2.3 para la estrategia que
 * valida.
 *
 * No re-mide contra Wine en cada corrida (eso exigiría wineg++ y
 * enlazar Qt contra un binario Winelib, no probado en este árbol) --
 * compara la implementación de hoy contra la tabla ya capturada. Si Wine
 * cambia de versión y el redondeo real se mueve, esta prueba seguirá en
 * verde contra un oráculo obsoleto -- por eso el comentario de la tabla
 * documenta cómo regenerarla, no se pretende que sea eterna.
 */

#include "OpusShellFontMetrics.h"

#include "opus_shell_font_metrics_oracle_table.h"

#include <QGuiApplication>

#include <cstdio>
#include <cstring>

namespace {

int g_failures = 0;
int g_checked = 0;

void Check(bool condition, const char *what) {
    if (!condition) {
        std::fprintf(stderr, "FALLO: %s\n", what);
        ++g_failures;
    }
}

int FtcFromEraName(const char *eraName) {
    if (std::strcmp(eraName, "Tms Rmn") == 0) return 0;
    if (std::strcmp(eraName, "Symbol") == 0) return 1;
    if (std::strcmp(eraName, "Helv") == 0) return 2;
    if (std::strcmp(eraName, "Courier") == 0) return 3;
    return -1;
}

}  // namespace

int main(int argc, char **argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QGuiApplication app(argc, argv);

    int mismatches = 0;
    for (int row = 0; row < kOracleTableCount; ++row) {
        const OracleRow &oracle = kOracleTable[row];
        int ftc = FtcFromEraName(oracle.eraName);
        Check(ftc >= 0, "nombre de epoca de la tabla oraculo no reconocido");
        if (ftc < 0) continue;

        OpusFontKey key{ftc, oracle.pt * 2 /* medios puntos */, 0};

        OpusFontMetrics m{};
        int rc = OpusShellFontMetrics(&key, &m);
        Check(rc == 0, "OpusShellFontMetrics fallo para fila de la tabla oraculo");
        if (rc == 0) {
            /* Ascenso/descenso: no forman parte de la garantia estricta
               de Sec.B2.3 (esa mide avances, no metricas de fuente), pero
               deben quedar razonablemente cerca -- +-1px de margen por
               redondeo de ascent()/descent() vs tmAscent/tmDescent
               enteros de GDI, distintos caminos de redondeo del mismo
               valor real. Diferencia mayor si indicaria una fuente
               distinta a la del oraculo, no solo redondeo. */
            int dAscent = m.dypAscent - oracle.ascent;
            int dDescent = m.dypDescent - oracle.descent;
            Check(dAscent >= -1 && dAscent <= 1,
                  "dypAscent fuera de +-1px del oraculo");
            Check(dDescent >= -1 && dDescent <= 1,
                  "dypDescent fuera de +-1px del oraculo");
        }

        /* Los 95 anchos: esta es la garantia estricta de Sec.B2.3 --
           coincidencia exacta, no aproximada. */
        unsigned short widths[95];
        int rcw = OpusShellCharWidths(&key, 32, 95, widths);
        Check(rcw == 0, "OpusShellCharWidths fallo para fila de la tabla oraculo");
        if (rcw == 0) {
            for (int i = 0; i < 95; ++i) {
                ++g_checked;
                if (widths[i] != oracle.widths[i]) {
                    ++mismatches;
                    if (mismatches <= 20) {
                        std::fprintf(
                            stderr,
                            "FALLO: %s %dpt char=%d ('%c'): shell=%u "
                            "oraculo=%u\n",
                            oracle.eraName, oracle.pt, 32 + i,
                            static_cast<char>(32 + i), widths[i],
                            oracle.widths[i]);
                    }
                }
            }
        }
    }
    Check(mismatches == 0,
          "hubo discrepancias de ancho contra la tabla oraculo (ver arriba)");

    std::fprintf(stderr, "%d puntos de dato comparados, %d discrepancias\n",
                 g_checked, mismatches);

    if (g_failures > 0) {
        std::fprintf(stderr, "%d fallo(s)\n", g_failures);
        return 1;
    }
    std::fprintf(stderr, "OK\n");
    return 0;
}
