/*
 * Prueba propia de la implementación de OpusShellConfig.h
 * (docs/port-qt/01-core-shell-boundary.md §B5, Qt-2 paso 1).
 *
 * El árbol de pruebas existente (src/port/original/opus_*_test.c*) no
 * ejercita GetProfileString/GetProfileInt/WriteProfileString hoy -- se
 * verificó por grep antes de escribir esta prueba, ver el reporte de
 * cierre. No hay comportamiento previo que igualar caso por caso; esta
 * prueba verifica la implementación contra la semántica documentada del
 * Profile Win16 original que Opus/*.c sigue llamando (GetProfileInt usa el
 * valor por omisión solo cuando la clave no existe, no cuando el valor
 * presente no es numérico).
 *
 * QSettings::setPath() redirige QSettings::IniFormat/QSettings::UserScope a
 * un directorio temporal antes de la primera llamada al contrato, para no
 * tocar la configuración real del usuario que ejecuta la prueba.
 */

#include "OpusShellConfig.h"

#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>

#include <cstdio>
#include <cstring>

namespace {

int g_failures = 0;

void Check(bool condition, const char *what) {
    if (!condition) {
        std::fprintf(stderr, "FALLÓ: %s\n", what);
        ++g_failures;
    }
}

}  // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);

    QTemporaryDir tempDir;
    Check(tempDir.isValid(), "no se pudo crear directorio temporal de prueba");
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                        tempDir.path());

    /* Round trip de cadena: escribir y releer el mismo valor. */
    {
        Check(OpusShellProfileWrite("App", "RshPath", "C:\\DOCS") == 0,
              "OpusShellProfileWrite (RshPath) devolvió error");
        char out[64];
        int cch = OpusShellProfileString("App", "RshPath", "", out, sizeof(out));
        Check(cch == static_cast<int>(std::strlen("C:\\DOCS")),
              "OpusShellProfileString no devolvió la longitud esperada");
        Check(std::strcmp(out, "C:\\DOCS") == 0,
              "OpusShellProfileString no reprodujo el valor escrito");
    }

    /* Clave inexistente: usa el valor por omisión, tal como
       GetProfileString sobre una clave ausente. */
    {
        char out[64];
        int cch =
            OpusShellProfileString("App", "NoExiste", "porOmision", out,
                                    sizeof(out));
        Check(cch == static_cast<int>(std::strlen("porOmision")),
              "valor por omisión: longitud incorrecta");
        Check(std::strcmp(out, "porOmision") == 0,
              "valor por omisión: contenido incorrecto");
    }

    /* Truncamiento cuando el buffer de salida es más chico que el valor,
       igual que GetProfileString con cbMax insuficiente. */
    {
        Check(OpusShellProfileWrite("App", "Largo", "0123456789") == 0,
              "OpusShellProfileWrite (Largo) devolvió error");
        char out[5];
        int cch = OpusShellProfileString("App", "Largo", "", out, sizeof(out));
        Check(cch == 4, "truncamiento: longitud devuelta incorrecta");
        Check(std::strcmp(out, "0123") == 0,
              "truncamiento: contenido incorrecto");
    }

    /* Entero: round trip normal y negativo. */
    {
        Check(OpusShellProfileWrite("App", "RshNext", "42") == 0,
              "OpusShellProfileWrite (RshNext) devolvió error");
        Check(OpusShellProfileInt("App", "RshNext", -1) == 42,
              "OpusShellProfileInt no reprodujo el valor entero escrito");

        Check(OpusShellProfileWrite("App", "EmmLimit", "-1") == 0,
              "OpusShellProfileWrite (EmmLimit) devolvió error");
        Check(OpusShellProfileInt("App", "EmmLimit", 0) == -1,
              "OpusShellProfileInt no reprodujo el valor entero negativo");
    }

    /* Entero ausente: usa el valor por omisión -- igual que GetProfileInt
       cuando la clave no existe. */
    Check(OpusShellProfileInt("App", "NoExisteInt", 7) == 7,
          "entero ausente: no usó el valor por omisión");

    /* Entero presente pero no numérico: GetProfileInt real devuelve 0 en
       este caso, NO el valor por omisión -- es la distinción que
       QString::toInt() por sí solo no reproduce y que motivó AtoiLike(). */
    {
        Check(OpusShellProfileWrite("App", "Basura", "no-es-un-numero") == 0,
              "OpusShellProfileWrite (Basura) devolvió error");
        Check(OpusShellProfileInt("App", "Basura", 99) == 0,
              "entero no numérico presente: debía devolver 0, no el valor "
              "por omisión");
    }

    /* Sobrescritura: escribir dos veces la misma clave deja el segundo
       valor, no acumula. */
    {
        OpusShellProfileWrite("App", "RshNext", "1");
        OpusShellProfileWrite("App", "RshNext", "2");
        Check(OpusShellProfileInt("App", "RshNext", -1) == 2,
              "sobrescritura: quedó el primer valor en vez del segundo");
    }

    if (g_failures == 0) {
        std::printf("OpusShellConfig_test: %d verificaciones, todas bien.\n",
                    9);
        return 0;
    }
    std::fprintf(stderr, "OpusShellConfig_test: %d fallo(s).\n", g_failures);
    return 1;
}
