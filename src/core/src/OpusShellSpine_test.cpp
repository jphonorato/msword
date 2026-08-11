/*
 * Prueba propia de OpusShellSpine.cpp. QMessageBox exige QApplication
 * (no basta QGuiApplication, a diferencia de OpusShellFontMetrics_test.cpp)
 * -- confirmado necesario para este contrato, no copiado por hábito.
 *
 * OpusShellReportError es modal (Qt::ApplicationModal, ver comentario de
 * cabecera de la implementación) -- exec() no vuelve hasta que algo cierra
 * el diálogo. Se cierra desde un QTimer::singleShot(0, ...) que dispara
 * una vez que el bucle de eventos de exec() ya está corriendo y localiza
 * el widget modal activo -- patrón estándar para probar diálogos modales
 * de Qt sin interacción real, no una forma de evitar la prueba.
 */

#include "OpusShellSpine.h"

#include <QApplication>
#include <QTimer>
#include <QWidget>

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
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    QApplication app(argc, argv);

    /* OpusShellAlert: no bloquea, solo debe no crashear. */
    OpusShellAlert();

    /* OpusShellReportError: dispara con eid/mensaje, se auto-cierra. Si
       el diálogo modal no aparece dentro de este ciclo de eventos, el
       timer no encuentra nada que cerrar y exec() se queda colgado --
       eso ya sería una señal de fallo (la prueba no terminaría), no algo
       que este código tenga que detectar aparte. */
    bool foundModal = false;
    QTimer::singleShot(0, [&foundModal]() {
        QWidget *modal = QApplication::activeModalWidget();
        if (modal != nullptr) {
            foundModal = true;
            modal->close();
        }
    });
    OpusShellReportError(42, "mensaje de prueba, no un error real");
    Check(foundModal, "OpusShellReportError no produjo un widget modal activo");

    if (g_failures > 0) {
        std::fprintf(stderr, "%d fallo(s)\n", g_failures);
        return 1;
    }
    std::fprintf(stderr, "OK\n");
    return 0;
}
