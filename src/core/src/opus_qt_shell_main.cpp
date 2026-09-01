/*
 * Primer binario Qt real de la rama Qt-2 -- ver docs/port-qt/
 * 01-core-shell-boundary.md, "Secuencia recomendada para Qt-2".
 *
 * Este NO es Word bajo Qt. No hay motor de documento, no hay wordtech/
 * conectado. Es el andamiaje que demuestra que un ejecutable Qt6 real --
 * no un test de biblioteca -- enlaza y arranca contra los contratos ya
 * cerrados (OpusShellConfig, OpusShellMemory, OpusShellFontSubstitution,
 * OpusShellFontMetrics, OpusShellSpine), y que el bucle de eventos de
 * QApplication puede llamar hacia dentro con los dos patrones de
 * despacho de §B4.2 (SendMessage -> llamada directa, PostMessage ->
 * QMetaObject::invokeMethod con Qt::QueuedConnection).
 *
 * Alcance real de "inversión de bucle de mensajes" en esta sesión --
 * léase con cuidado, no es la inversión completa de §B4.1: eso exige
 * mover Opus/wproc.c de conductor a biblioteca, y Opus/ es árbol
 * restringido, no tocado aquí. Lo que sí es real y verificable: el bucle
 * que corre es el de QApplication (no hay GetMessage/DispatchMessage en
 * ningún punto de este binario), y ya llama hacia dentro con ambos
 * patrones de la tabla de §B4.2 contra los contratos que existen hoy.
 * Cuando wordtech/ se conecte, este es el molde a reutilizar, no algo
 * que rehacer.
 *
 * Filosofía de esta sesión: primero que compile y corra, después se
 * depura y se rellena.
 *
 * Smoke check en el arranque (no bloqueante -- OpusShellReportError,
 * modal, queda fuera, solo se dispara desde el menú a demanda): ejercita
 * los contratos con una llamada real cada uno y lo muestra en pantalla,
 * así un fallo de enlace o de ABI entre bibliotecas se ve inmediatamente
 * en vez de quedar enmascarado por un "compila" vacío.
 */

#include "OpusShellConfig.h"
#include "OpusShellFontMetrics.h"
#include "OpusShellFontSubstitution.h"
#include "OpusShellMemory.h"
#include "OpusShellSpine.h"

#include <QAction>
#include <QApplication>
#include <QDateTime>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMetaObject>
#include <QPlainTextEdit>
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
    window.setWindowTitle(
        "Opus/Word -- andamiaje Qt (sin motor de documento; "
        "bucle de eventos = QApplication, no GetMessage/DispatchMessage)");

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

    /* Bitácora de despacho -- hace observable la diferencia entre los
       dos patrones de §B4.2, no solo la afirma. static int en vez de
       captura de referencia porque las lambdas se reusan como slots
       conectados a QAction::triggered, que puede disparar más de una
       vez por sesión. */
    auto *log = new QPlainTextEdit(central);
    log->setReadOnly(true);
    log->setMaximumBlockCount(200);
    static int seq = 0;
    auto logLine = [log](const QString &s) {
        log->appendPlainText(
            QStringLiteral("[%1] #%2 %3")
                .arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"))
                .arg(++seq)
                .arg(s));
    };
    layout->addWidget(log);

    window.setCentralWidget(central);

    auto *dispatchMenu = window.menuBar()->addMenu("&Despacho (B4.2)");

    /* SendMessage -> llamada directa: sincrónica, se ejecuta dentro del
       mismo ciclo del bucle de eventos que procesó el clic, antes de que
       el slot retorne. */
    auto *directAction =
        dispatchMenu->addAction("Alerta &directa (SendMessage)");
    QObject::connect(directAction, &QAction::triggered, &window,
                      [logLine]() {
                          logLine("directo: entrando a OpusShellAlert()");
                          OpusShellAlert();
                          logLine("directo: OpusShellAlert() ya volvió");
                      });

    /* PostMessage -> QMetaObject::invokeMethod con Qt::QueuedConnection:
       se encola y corre en una vuelta posterior del bucle de eventos,
       después de que este slot ya haya retornado -- por eso el mensaje
       "encolado" y el mensaje "ejecutado" no son consecutivos en la
       bitácora cuando hay más eventos de por medio, a diferencia del
       directo, que sí lo son siempre. */
    auto *queuedAction =
        dispatchMenu->addAction("Alerta &diferida (PostMessage)");
    QObject::connect(
        queuedAction, &QAction::triggered, &window, [logLine]() {
            logLine("diferido: encolado (todavía no corrió)");
            QMetaObject::invokeMethod(
                qApp,
                [logLine]() {
                    logLine("diferido: corriendo ahora, ciclo posterior");
                    OpusShellAlert();
                },
                Qt::QueuedConnection);
        });

    /* OpusShellReportError es modal (Qt::ApplicationModal) -- aquí sí es
       correcto dispararlo, a diferencia del arranque: es una acción de
       usuario real, no algo automático que bloquee cada inicio. */
    auto *errorAction = dispatchMenu->addAction("&Reportar error de prueba (modal)");
    QObject::connect(errorAction, &QAction::triggered, &window, [logLine]() {
        logLine("modal: abriendo OpusShellReportError (bloquea hasta "
                "cerrar)");
        OpusShellReportError(
            42, "Mensaje de prueba -- disparado desde el menu, no un "
                "error real.");
        logLine("modal: cerrado");
    });

    window.resize(560, 360);
    window.show();

    return app.exec();
}
