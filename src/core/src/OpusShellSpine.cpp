/*
 * Implementación del contrato de espina de mensajes/ventanas -- solo los
 * dos fragmentos con firma concreta (src/core/include/OpusShellSpine.h,
 * docs/port-qt/01-frontera-nucleo-shell.md §B4.3). La inversión de
 * control completa (paso 7 de la secuencia Qt-2) sigue sin tocar aquí --
 * ver el comentario de cabecera del header.
 */

#include "OpusShellSpine.h"

#include <QApplication>
#include <QMessageBox>
#include <QString>

#include <cstdio>

extern "C" void OpusShellReportError(int eid, const char *message) {
    /* MB_SYSTEMMODAL (error.c:1618) -> Qt::ApplicationModal: bloquea toda
       la aplicación hasta que se descarta, mismo efecto práctico que el
       modal de sistema Win16 tenía sobre un proceso de una sola
       ventana.

       WORD1 real corre hoy con el loop de mensajes Win32 (GetMessage/
       DispatchMessage), no con QApplication -- la inversión de control
       (paso 7 de la secuencia Qt-2) sigue sin hacerse. Si este contrato se
       invoca antes de que exista una QApplication (p.ej. un error durante
       el arranque temprano), construir un QMessageBox aborta el proceso
       ("QWidget: Must construct a QApplication before a QWidget"). Sin
       QApplication no hay forma de mostrar UI Qt: se degrada a stderr. */
    if (qApp == nullptr) {
        std::fprintf(stderr, "Word (%d) %s\n", eid, message != nullptr ? message : "");
        std::fflush(stderr);
        return;
    }

    QMessageBox box(QMessageBox::Critical, QApplication::translate("OpusShellSpine", "Word"),
                     QString("(%1) %2")
                         .arg(eid)
                         .arg(message != nullptr ? QString::fromUtf8(message)
                                                  : QString()));
    box.setWindowModality(Qt::ApplicationModal);
    box.setStandardButtons(QMessageBox::Ok);
    box.exec();
}

extern "C" void OpusShellAlert(void) {
    /* MessageBeep(MB_OK) en editspec.c/undo.c -- sin texto, sin diálogo.
       Mismo guard que OpusShellReportError: sin QApplication, no-op en vez
       de crashear. */
    if (qApp == nullptr) {
        return;
    }
    QApplication::beep();
}
