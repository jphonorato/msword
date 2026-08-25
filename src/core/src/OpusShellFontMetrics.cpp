/*
 * Implementación del contrato de medición de texto
 * (src/core/include/OpusShellFontMetrics.h, docs/port-qt/
 * 01-frontera-nucleo-shell.md §B2). Estrategia validada empíricamente en
 * §B2.3: ppem entero + QFont::PreferFullHinting sobre QRawFont construido
 * directamente contra el archivo físico -- no QFontMetricsF, no QFont
 * (ver §B2.4 para por qué esas dos quedan descartadas). Reproduce, no
 * aproxima, los enteros que produce Opus/LOADFONT.C:187 C_LoadFcid() bajo
 * el oráculo Winelib (mismo rasterizador FreeType a ambos lados).
 *
 * Limitaciones de este primer corte, explícitas, no silenciosas:
 *
 * 1. **`ftc` -> nombre de época: tabla fija de 4 entradas, hardcodeada
 *    aquí, usada solo cuando `OpusFontKey.szFace` es NULL.** El contrato
 *    declara que "el núcleo traduce" (§B2.2); la tabla de arranque
 *    verificada (Opus/initwin.c:1541-1583, vhsttbFont, orden Tms
 *    Rmn/Symbol/Helv/Courier = ftc 0-3) sigue siendo la única fuente para
 *    esos 4 `ftc` y es la misma que ya cubre OpusShellFontSubstitution.
 *    Para fuentes en tiempo de ejecución fuera de esas 4 (ftc >= 4), el
 *    llamador pasa el nombre real en `szFace` -- un `ftc` fuera de [0,3]
 *    sin `szFace` sigue fallando controlado. Ver 01-frontera-nucleo-shell.md,
 *    pregunta abierta #3, último párrafo, para la decisión pendiente
 *    sobre dónde debe vivir esta tabla a largo plazo.
 * 2. **`catr` (negrita/cursiva) no soportado -- falla controlado, no se
 *    ignora en silencio.** QRawFont rasteriza un archivo físico tal cual;
 *    no sintetiza negrita/cursiva como hace GDI sobre estas fuentes
 *    (§B2.5: "negrita y cursiva se sintetizan sobre el mismo archivo").
 *    Reproducir esa síntesis (transformación oblicua para cursiva,
 *    engrosado de trazo para negrita) es trabajo de fidelidad aparte, no
 *    hecho aquí. `catr != 0` devuelve error en vez de devolver métricas
 *    de peso regular calladamente.
 * 3. **Resolución de pantalla fija a 96 ppp.** Es la resolución con la
 *    que se hizo toda la medición de §B2.3/§B2.6. El contrato declara
 *    dxpInch/dypInch como salida (el shell decide, el núcleo no pide un
 *    DPI concreto) -- soporte de impresora, con su propio DPI real, es
 *    trabajo futuro, no de este corte.
 * 4. **`dxpOverhang` fijo a 0.** Medido en §B2.5 para los 8 casos de
 *    estilo probados (los 4 nombres × negrita/cursiva sintetizada):
 *    tmOverhang = 0 en todos. Coherente con la limitación 2 -- sin
 *    síntesis de estilo, el caso que podría producir overhang != 0 no se
 *    alcanza de todas formas.
 */

#include "OpusShellFontMetrics.h"

#include "OpusShellFontSubstitution.h"
#include "opus_shell_font_metrics_oracle_table.h"

#include <QCoreApplication>
#include <QFont>
#include <QGuiApplication>
#include <QRawFont>
#include <QString>
#include <QThread>

#include <cstring>

namespace {

constexpr int kScreenDpi = 96;

const char *EraNameFromFtc(int ftc) {
    switch (ftc) {
        case 0: return "Tms Rmn";
        case 1: return "Symbol";
        case 2: return "Helv";
        case 3: return "Courier";
        default: return nullptr;
    }
}

/* MulDiv de Win32: round(a*b/c), redondeo al entero más cercano. Mismo
   cálculo que Opus/LOADFONT.C usa para pasar de puntos a píxeles
   (lfHeight = -MulDiv(pt, dpiY, 72), §B2.3). Sin la protección de
   overflow de 64 bits del MulDiv real: los rangos de este contrato (ps
   en medios puntos, dpi de pantalla) nunca se acercan a desbordar un
   long, no hace falta reproducirla. */
int MulDivRound(int a, int b, int c) {
    return static_cast<int>(
        (static_cast<long long>(a) * b + c / 2) / c);
}

/* Nombre de fuente efectivo para esta clave: `szFace` cuando el llamador
   lo da (fuentes en tiempo de ejecución, ftc >= 4 incluido), si no la
   tabla fija de 4 nombres de época (limitación 1). */
const char *FaceNameFor(const OpusFontKey *key) {
    if (key == nullptr) {
        return nullptr;
    }
    if (key->szFace != nullptr && key->szFace[0] != '\0') {
        return key->szFace;
    }
    return EraNameFromFtc(key->ftc);
}

bool CanUseRawFont() {
    /* QRawFont needs a QGuiApplication. A lone QCoreApplication (or
       instance() on the wrong thread) is how WORD1 used to hang. */
    auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    return app != nullptr && QThread::currentThread() == app->thread();
}

int PixelSizeFor(const OpusFontKey *key) {
    int px = MulDivRound(key->ps / 2, kScreenDpi, 72);
    /* CreateFontIndirect(lfHeight==0) uses a default face height. Startup
       asks for Helv at hps==0; returning -1 here sets matFont and bricks
       every later WM_COMMAND. Measure at 10pt (hpsDefault) instead. */
    if (px <= 0) {
        px = MulDivRound(10, kScreenDpi, 72);
    }
    return px;
}

/* Construye el QRawFont validado en §B2.3/§B2.4 para (ftc, ps). Devuelve
   un QRawFont inválido (isValid() == false) si ftc/catr no están
   soportados o el archivo de sustitución no existe -- el llamador
   traduce eso a "falla controlado", no lo desreferencia. */
QRawFont RawFontFor(const OpusFontKey *key, int *pxOut, const char **whyOut) {
    QRawFont invalid;
    if (key == nullptr) {
        if (whyOut) *whyOut = "null-key";
        return invalid;
    }
    if (key->catr != 0 && CanUseRawFont()) {
        if (whyOut) *whyOut = "catr";
        return invalid;  /* limitación 2: solo con QGuiApplication */
    }
    const char *name = FaceNameFor(key);
    if (name == nullptr) {
        if (whyOut) *whyOut = "ftc";
        return invalid;  /* limitación 1 */
    }
    int px = PixelSizeFor(key);
    if (px <= 0) {
        if (whyOut) *whyOut = "bad-px";
        return invalid;
    }
    if (pxOut != nullptr) {
        *pxOut = px;
    }
    /* Los 4 nombres de época siguen midiéndose contra el archivo físico
       de sustitución (§B2.3/§B2.4, el oráculo medido) -- nunca por
       QFont::fromFont, ni siquiera cuando llegan aquí vía szFace en vez
       de la tabla ftc. Un szFace de tiempo de ejecución que no sea uno
       de los 4 nombres no tiene entrada de sustitución y cae al camino
       QFont::fromFont de abajo. */
    const char *file = OpusShellSubstituteFontFile(name);
    if (file != nullptr) {
        if (!CanUseRawFont()) {
            if (whyOut) *whyOut = "no-gui-app";
            return invalid;
        }
        QRawFont rf(QString::fromUtf8(file), static_cast<qreal>(px),
                    QFont::PreferFullHinting);
        if (!rf.isValid()) {
            if (whyOut) *whyOut = "qrawfont-invalid";
            return invalid;
        }
        if (whyOut) *whyOut = nullptr;
        return rf;
    }
    if (!CanUseRawFont()) {
        if (whyOut) *whyOut = "no-gui-app";
        return invalid;  /* WORD1 debe llegar al fallback GDI */
    }
    /* Fuente en tiempo de ejecución sin archivo de sustitución conocido:
       deja que Qt la resuelva contra las fuentes instaladas del sistema.
       Este camino es para las pruebas del núcleo / opus_qt_shell, no
       para WORD1 (que nunca tiene QGuiApplication, ver CanUseRawFont). */
    QFont qf(QString::fromLatin1(name));
    qf.setPixelSize(px);
    qf.setHintingPreference(QFont::PreferFullHinting);
    /* QRawFont::fromFont's 2nd param is QFontDatabase::WritingSystem, not
       a hinting preference (that's already on `qf` above) -- the brief's
       snippet had QFont::PreferFullHinting there, which doesn't compile
       against this overload; default WritingSystem (Any) is correct. */
    QRawFont rf = QRawFont::fromFont(qf);
    if (!rf.isValid()) {
        if (whyOut) *whyOut = "qrawfont-invalid";
        return invalid;
    }
    if (whyOut) *whyOut = nullptr;
    return rf;
}

int PointSizeFor(const OpusFontKey *key) {
    int pt = key->ps / 2;
    return pt > 0 ? pt : 10;
}

const OracleRow *FindOracleRow(const char *eraName, int pt) {
    const OracleRow *best = nullptr;
    int bestDelta = 1000;
    for (int i = 0; i < kOracleTableCount; ++i) {
        if (std::strcmp(kOracleTable[i].eraName, eraName) != 0) {
            continue;
        }
        const int delta = kOracleTable[i].pt > pt
                              ? kOracleTable[i].pt - pt
                              : pt - kOracleTable[i].pt;
        if (delta < bestDelta) {
            bestDelta = delta;
            best = &kOracleTable[i];
        }
    }
    return best;
}

void FillOracleWidths(const OracleRow *row, int chFirst, int cch,
                      unsigned short *rgdxu) {
    const unsigned short space = row->widths[0];
    for (int i = 0; i < cch; ++i) {
        const int ch = chFirst + i;
        rgdxu[i] = (ch >= 32 && ch <= 126) ? row->widths[ch - 32] : space;
    }
}

OpusShellCharWidthsFn g_char_widths_fallback;

}  // namespace

extern "C" void OpusShellSetCharWidthsFallback(OpusShellCharWidthsFn fn) {
    g_char_widths_fallback = fn;
}

extern "C" int OpusShellFontMetrics(const OpusFontKey *key,
                                     OpusFontMetrics *out) {
    if (out == nullptr) {
        return -1;
    }
    const char *why = nullptr;
    int px = 0;
    QRawFont rf = RawFontFor(key, &px, &why);
    if (!rf.isValid()) {
        /* WORD1 has no QGuiApplication; do not construct one (it hangs
           or kills the Wine pump). A non-zero box still avoids matFont. */
        /* key->catr == 0: the oracle table only has plain-weight rows
           (opus_shell_font_metrics_oracle_table.h's capture never
           measured bold/italic) -- a bold/italic request must fail
           controlled here (Opus/LOADFONT.C:442-448's documented
           contract for catr != 0), not silently answer with the wrong
           weight's ascent/descent. */
        if (why != nullptr && std::strcmp(why, "no-gui-app") == 0 &&
            key != nullptr && key->catr == 0) {
            const char *era = EraNameFromFtc(key->ftc);
            const OracleRow *row =
                era != nullptr ? FindOracleRow(era, PointSizeFor(key))
                               : nullptr;
            if (row != nullptr) {
                out->dypAscent = row->ascent;
                out->dypDescent = row->descent;
                out->dxpOverhang = row->overhang;
                out->dxpInch = kScreenDpi;
                out->dypInch = kScreenDpi;
                out->dxuFixed = (key->ftc == 3) ? 1 : 0;
                return 0;
            }
        }
        return -1;
    }

    out->dypAscent = qRound(rf.ascent());
    out->dypDescent = qRound(rf.descent());
    out->dxpOverhang = 0;             /* limitación 4 */
    out->dxpInch = kScreenDpi;
    out->dypInch = kScreenDpi;
    /* Único de los 4 nombres de época con paso fijo, verificado contra
       Opus/initwin.c:1578 (FF_MODERN|FIXED_PITCH, único prq=FIXED_PITCH
       de la tabla) -- no se deriva de QRawFont (no expone paso fijo
       directamente sin medir dos anchos y compararlos, y para estos 4
       nombres ya se sabe la respuesta sin medir). */
    out->dxuFixed = (key->ftc == 3) ? 1 : 0;
    return 0;
}

extern "C" int OpusShellCharWidths(const OpusFontKey *key, int chFirst,
                                    int cch, unsigned short *rgdxu) {
    if (rgdxu == nullptr || chFirst < 0 || cch <= 0 ||
        chFirst + cch > 256) {
        return -1;
    }
    const char *why = nullptr;
    int px = 0;
    QRawFont rf = RawFontFor(key, &px, &why);
    if (!rf.isValid()) {
        /* Same as FontMetrics: never return -1 when the only problem is
           that WORD1 has no QGuiApplication. LOADFONT treats -1 as
           matFont and the font MessageBox swallows every later
           WM_COMMAND, including Help About.

           Gated on !CanUseRawFont(), not on a specific `why` code: a
           runtime font (szFace, ftc >= 4) with no era substitution file
           fails RawFontFor with why=="ftc" or why=="no-gui-app"
           depending on where it fell over, but either way WORD1 (no
           QGuiApplication ever) must still reach the real GDI fallback
           below instead of stopping at -1. */
        if (!CanUseRawFont() && key != nullptr &&
            g_char_widths_fallback != nullptr) {
            /* g_char_widths_fallback (OpusPortGdiCharWidths) builds a
               real LOGFONT with lfWeight/lfItalic from key->catr, so it
               stays correct for bold/italic and is tried unconditionally,
               regardless of why. */
            if (g_char_widths_fallback(key, chFirst, cch, rgdxu) == 0) {
                return 0;
            }
        }
        /* Oracle table fallback: only for the 4 era names (EraNameFromFtc,
           not FaceNameFor -- the table has no rows for runtime szFace
           fonts) and only plain weight (limitación 2). why=="no-gui-app"
           specifically means RawFontFor got as far as resolving a real
           era substitution file and only stopped for lack of a
           QGuiApplication -- the case this table was captured for. */
        if (why != nullptr && std::strcmp(why, "no-gui-app") == 0 &&
            key != nullptr && key->catr == 0) {
            const char *era = EraNameFromFtc(key->ftc);
            const OracleRow *row =
                era != nullptr ? FindOracleRow(era, PointSizeFor(key))
                               : nullptr;
            if (row != nullptr) {
                FillOracleWidths(row, chFirst, cch, rgdxu);
                return 0;
            }
        }
        return -1;
    }

    for (int i = 0; i < cch; ++i) {
        /* QChar::fromLatin1: los 256 códigos de FCE.hqrgdxp son valores
           de byte crudos (ANSI_CHARSET para los 4 nombres cubiertos,
           Opus/initwin.c:1547 etc.) -- Latin-1 es el mapeo correcto para
           0-255, no UTF-8 multibyte. */
        QChar ch = QChar::fromLatin1(static_cast<char>(chFirst + i));
        QVector<quint32> glyphs = rf.glyphIndexesForString(QString(ch));
        if (glyphs.isEmpty() || glyphs.first() == 0) {
            /* Sin glifo para este byte en esta fuente: ancho 0, igual
               que OurGetCharWidth devuelve 0 cuando GetTextExtentPoint32A
               falla (LOADFONT.C:990). No es un error del contrato. */
            rgdxu[i] = 0;
            continue;
        }
        QVector<QPointF> advances = rf.advancesForGlyphIndexes(glyphs);
        qreal advance = advances.isEmpty() ? 0.0 : advances.first().x();
        rgdxu[i] = static_cast<unsigned short>(qRound(advance));
    }
    return 0;
}
