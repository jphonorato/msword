/* src/core/src/OpusShellFontSubstitution_test.cpp
 *
 * Prueba propia de OpusShellFontSubstitution.h
 * (docs/port-qt/01-core-shell-boundary.md §B2.6). Sin Qt: es una tabla
 * de datos estática, mismo patrón de Check() que OpusShellConfig_test.cpp.
 */
#include "OpusShellFontSubstitution.h"

#include <cstdio>
#include <cstring>

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool condition, const char *what) {
    ++g_checks;
    if (!condition) {
        std::fprintf(stderr, "FALLÓ: %s\n", what);
        ++g_failures;
    }
}

/* Por-nombre: no los 4 nombres de época resuelven a la misma familia
   (§B2.6) -- Tms Rmn y Courier tienen su propio archivo, solo Symbol y
   Helv coinciden en Liberation Sans. Una constante única compartida no
   detectaría una tabla con el mapeo cruzado equivocado entre nombres. */
struct ExpectedEntry {
    const char *eraName;
    const char *expectedPath;
};

const ExpectedEntry kExpected[] = {
    { "Tms Rmn", "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf" },
    { "Symbol",  "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf" },
    { "Helv",    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf" },
    { "Courier", "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf" },
};

}  // namespace

int main() {
    for (const auto &expected : kExpected) {
        const char *path = OpusShellSubstituteFontFile(expected.eraName);
        Check(path != nullptr, expected.eraName);
        if (path == nullptr) {
            continue;
        }
        Check(std::strcmp(path, expected.expectedPath) == 0, expected.eraName);

        /* Red de seguridad contra tabla obsoleta: la ruta debe ser
           realmente abrible en esta máquina, no solo coincidir como
           cadena. Una máquina sin las fuentes Liberation instaladas en
           esa ruta exacta debe fallar la prueba de forma ruidosa, no
           producir anchos de carácter incorrectos en silencio más
           adelante. */
        std::FILE *fp = std::fopen(path, "rb");
        char msg[256];
        std::snprintf(msg, sizeof msg,
                      "%s: archivo de fuente '%s' debe poder abrirse -- "
                      "¿faltan las fuentes Liberation en esta máquina?",
                      expected.eraName, path);
        Check(fp != nullptr, msg);
        if (fp != nullptr) {
            std::fclose(fp);
        }
    }

    Check(OpusShellSubstituteFontFile("Script") == nullptr,
          "Script no debe resolver -- fuera de alcance de esta tabla");
    Check(OpusShellSubstituteFontFile("Modern") == nullptr,
          "Modern no debe resolver -- fuera de alcance de esta tabla");
    Check(OpusShellSubstituteFontFile("nombre-inexistente") == nullptr,
          "nombre desconocido debe devolver NULL");
    Check(OpusShellSubstituteFontFile(nullptr) == nullptr,
          "NULL de entrada debe devolver NULL, no crashear");

    if (g_failures == 0) {
        std::printf("OpusShellFontSubstitution_test: %d verificaciones, todas bien.\n",
                    g_checks);
        return 0;
    }
    std::fprintf(stderr, "OpusShellFontSubstitution_test: %d fallo(s) de %d verificaciones.\n",
                 g_failures, g_checks);
    return 1;
}
