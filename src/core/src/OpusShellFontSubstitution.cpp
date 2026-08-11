/* src/core/src/OpusShellFontSubstitution.cpp
 *
 * Implementación del contrato de sustitución de fuentes de época
 * (src/core/include/OpusShellFontSubstitution.h,
 * docs/port-qt/01-frontera-nucleo-shell.md §B2.6).
 *
 * Tabla estática, medida una vez contra el oráculo Winelib -- no vuelve a
 * medir en tiempo de ejecución. Ver §B2.6 para el método de medición
 * (GetTextFaceA para la familia, fc-match/fc-scan cruzados para el
 * archivo).
 */
#include "OpusShellFontSubstitution.h"

#include <cstring>

namespace {

struct SubstitutionEntry {
    const char *eraName;
    const char *filePath;
};

/* Los 4 nombres de época de la tabla maestra de arranque
   (Opus/initwin.c, vhsttbFont, ftc 0-3). No los cuatro resuelven a la
   misma familia -- ver §B2.6: Tms Rmn resuelve a Liberation Serif y
   Courier a Liberation Mono; solo Symbol y Helv coinciden en Liberation
   Sans (ambos piden una familia sans-serif vía ffid), y Symbol no recibe
   tratamiento de charset simbólico especial pese a lo que su nombre
   sugeriría. */
const SubstitutionEntry kSubstitutionTable[] = {
    { "Tms Rmn", "/usr/share/fonts/truetype/liberation/LiberationSerif-Regular.ttf" },
    { "Symbol",  "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf" },
    { "Helv",    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf" },
    { "Courier", "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf" },
};

}  // namespace

const char *OpusShellSubstituteFontFile(const char *eraName) {
    if (eraName == nullptr) {
        return nullptr;
    }
    for (const auto &entry : kSubstitutionTable) {
        if (std::strcmp(eraName, entry.eraName) == 0) {
            return entry.filePath;
        }
    }
    return nullptr;
}
