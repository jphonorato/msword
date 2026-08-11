/* src/core/src/OpusShellFontSubstitution_test.cpp
 *
 * Prueba propia de OpusShellFontSubstitution.h
 * (docs/port-qt/01-frontera-nucleo-shell.md §B2.6). Sin Qt: es una tabla
 * de datos estática, mismo patrón de Check() que OpusShellConfig_test.cpp.
 */
#include "OpusShellFontSubstitution.h"

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

int main() {
    const char *kExpected =
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf";

    const char *names[] = { "Tms Rmn", "Symbol", "Helv", "Courier" };
    for (const char *name : names) {
        const char *path = OpusShellSubstituteFontFile(name);
        Check(path != nullptr, name);
        if (path != nullptr) {
            Check(std::strcmp(path, kExpected) == 0, name);
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
                    8);
        return 0;
    }
    std::fprintf(stderr, "OpusShellFontSubstitution_test: %d fallo(s).\n",
                 g_failures);
    return 1;
}
