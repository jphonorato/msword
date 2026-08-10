#pragma once

/*
 * Contrato de espina de mensajes y ventanas entre el núcleo Qt y el shell.
 * Diseño: docs/port-qt/01-frontera-nucleo-shell.md, §B4.
 *
 * La inversión de control (el bucle de mensajes pasa a pertenecer a
 * QCoreApplication) es el trabajo estructural de esta frontera y es el
 * último paso de la secuencia recomendada de Qt-2 -- no se hace en esta
 * fase. Este header no la implementa: declara únicamente los dos
 * fragmentos de §B4.3 con respaldo textual verificado por grep contra el
 * árbol, en el orden en que se atacan.
 *
 * - error.c: MessageBox real en la línea 1618 (mstApp, MB_OK|MB_SYSTEMMODAL),
 *   con el código de error y el texto ya resuelto por el núcleo antes de la
 *   llamada (ErrorEidStartup resuelve `eid` a `szMsg` vía IemdFromEid /
 *   CopyEmdSt antes de invocar MessageBox). El núcleo entrega ambos; el
 *   shell decide la presentación.
 * - editspec.c (líneas 1855, 2075) y undo.c (línea 97): `MessageBeep(MB_OK)`,
 *   no notificación de cambio de documento -- una lectura anterior de este
 *   documento lo afirmaba sin verificación independiente; corregido en
 *   §B4.3. Los tres sitios son el mismo patrón: operación de edición
 *   rechazada (pila de deshacer vacía, rango de bloque inválido), señal
 *   audible, sin texto.
 *
 * El resto de §B4.2 (SendMessage/PostMessage, ciclo de vida de ventana,
 * scroll.c/disp3.c/pagevw.c) sigue sin reducirse a firmas concretas: el
 * documento solo tiene una tabla de mapeo conceptual para esa parte, y
 * convertirla en API ahora sería inventar un contrato que el diseño no ha
 * cerrado. Deliberadamente fuera de este header.
 *
 * Solo declaraciones. Sin implementación en este header.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Reporta un error ya resuelto a texto por el núcleo (ErrorEidStartup y
 * equivalentes). eid es el código de error original; message es el texto
 * ya formateado que el núcleo resolvió de su tabla de recursos. El shell
 * decide cómo presentarlo; no devuelve valor porque el sitio que este
 * contrato cubre (error.c:1618) no consume una respuesta del usuario.
 */
void OpusShellReportError(int eid, const char *message);

/*
 * Señal audible de "operación rechazada", sin texto. Reemplaza
 * MessageBeep(MB_OK) en editspec.c y undo.c.
 */
void OpusShellAlert(void);

#ifdef __cplusplus
}
#endif
