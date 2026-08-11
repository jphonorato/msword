/*
 * Primer binario Qt real de la rama Qt-2 -- ver docs/port-qt/
 * 01-frontera-nucleo-shell.md, "Secuencia recomendada para Qt-2".
 *
 * Este NO es Word bajo Qt. No hay motor de documento, no hay wordtech/,
 * no hay inversión de bucle de mensajes (paso 7, deliberadamente el
 * último). Es el andamiaje mínimo que demuestra que un ejecutable Qt6
 * real -- no un test de biblioteca -- enlaza y arranca contra los tres
 * contratos ya cerrados (OpusShellConfig, OpusShellMemory,
 * OpusShellFontSubstitution), como paso previo a colgarles encima
 * medición de texto (B2) y el resto del núcleo.
 *
 * Filosofía de esta sesión: primero que compile y corra, después se
 * depura y se rellena -- no diseñar la ventana completa antes de saber
 * si el grafo de enlace funciona de punta a punta con QApplication de
 * por medio (los tests existentes usan QCoreApplication, no
 * QApplication/QMainWindow -- ninguno prueba el camino de Widgets).
 *
 * Smoke check en el arranque, no solo un enlace silencioso: ejercita
 * los tres contratos con una llamada real cada uno y lo muestra en
 * pantalla, así un fallo de enlace o de ABI entre bibliotecas se ve
 * inmediatamente en vez de quedar enmascarado por un "compila" vacío.
 */

#include "OpusShellConfig.h"
#include "OpusShellFontMetrics.h"
#include "OpusShellFontSubstitution.h"
#include "OpusShellMemory.h"

#include <QApplication>
#include <QLabel>
#include <QMainWindow>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdio>
#include <cstring>

namespace {

QString RunSmokeChecks() {
    QStringList lines;

    /* OpusShellMemory: alocar, escribir, releer, liberar. Round-trip
       mínimo del camino propio (no passthrough -- eso necesita una
       tabla ops que este binario todavía no instala, ver
       OpusMemSetPassthrough). */
    {
        OpusHandle h = OpusMemAlloc(32, OPUS_MEM_ZEROINIT);
        bool memOk = false;
        if (h != nullptr) {
            void *p = OpusMemLock(h);
            if (p != nullptr) {
                std::memcpy(p, "opus-qt-shell", 13);
                memOk = (OpusMemSize(h) == 32) &&
                        (std::memcmp(p, "opus-qt-shell", 13) == 0);
            }
            OpusMemUnlock(h);
            OpusMemFree(h);
        }
        lines << QStringLiteral("OpusShellMemory: %1")
                     .arg(memOk ? "OK (alloc/lock/write/read/free)"
                                : "FALLO");
    }

    /* OpusShellFontSubstitution: resolver los 4 nombres de época. */
    {
        static const char *kNames[] = {"Tms Rmn", "Symbol", "Helv",
                                        "Courier"};
        bool fontOk = true;
        for (const char *name : kNames) {
            if (OpusShellSubstituteFontFile(name) == nullptr) {
                fontOk = false;
            }
        }
        lines << QStringLiteral("OpusShellFontSubstitution: %1")
                     .arg(fontOk ? "OK (4/4 nombres de epoca resueltos)"
                                 : "FALLO");
    }

    /* OpusShellConfig: escribir y releer un valor bajo una sección propia
       de este smoke check ("OpusQtShellSmoke") para no pisar
       configuración real de otra sección -- usa la ruta por omisión de
       QSettings (comportamiento real de una app, no el QTemporaryDir que
       usa OpusShellConfig_test.cpp). */
    {
        int wrote = OpusShellProfileWrite("OpusQtShellSmoke", "Check",
                                           "hello");
        char out[64] = {0};
        int cch = OpusShellProfileString("OpusQtShellSmoke", "Check", "",
                                          out, sizeof(out));
        bool cfgOk = (wrote == 0) && (cch > 0) &&
                     (std::strcmp(out, "hello") == 0);
        lines << QStringLiteral("OpusShellConfig: %1")
                     .arg(cfgOk ? "OK (write/read round-trip)" : "FALLO");
    }

    /* OpusShellFontMetrics: reproduce el punto de dato medido en §B2.3
       (Liberation Serif 14pt, '@' == 18 px bajo el oráculo Winelib). */
    {
        OpusFontKey key{0, 28, 0};
        unsigned short w = 0;
        int rc = OpusShellCharWidths(&key, '@', 1, &w);
        bool fontMetricsOk = (rc == 0) && (w == 18);
        lines << QStringLiteral("OpusShellFontMetrics: %1")
                     .arg(fontMetricsOk
                              ? "OK ('@' Tms Rmn 14pt == 18px, B2.3)"
                              : QStringLiteral("FALLO (w=%1)").arg(w));
    }

    return lines.join('\n');
}

}  // namespace

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName("opus_qt_shell");

    QMainWindow window;
    window.setWindowTitle("Opus/Word -- andamiaje Qt (paso 0, sin motor de documento)");

    const QString smokeResults = RunSmokeChecks();
    /* También a stderr: verificable en CI/headless sin captura de
       pantalla, y sin depender de que la ventana llegue a pintar. */
    std::fputs(qPrintable(smokeResults), stderr);
    std::fputc('\n', stderr);

    auto *central = new QWidget(&window);
    auto *layout = new QVBoxLayout(central);
    auto *label = new QLabel(smokeResults, central);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(label);
    window.setCentralWidget(central);
    window.resize(480, 200);
    window.show();

    return app.exec();
}
