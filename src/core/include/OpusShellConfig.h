#pragma once

/*
 * Contrato de persistencia de configuración entre el núcleo Qt y el shell.
 * Diseño: docs/port-qt/01-core-shell-boundary.md, §B5.
 *
 * Reemplaza GetProfileString/GetProfileInt/WriteProfileString (43 sitios,
 * 12 TUs). La semántica de WIN.INI -- sección, clave, valor por omisión --
 * corresponde uno a uno con QSettings: el shell traduce cada función
 * directamente a QSettings::value/setValue con beginGroup(section). Sin
 * caché propia, sin capa de esquema, sin migración: QSettings ya resuelve
 * formato y ubicación por plataforma. Este es el contrato completo, sin
 * partes pendientes de diseño.
 *
 * Solo declaraciones. Sin implementación en este header.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Copia en out (buffer de cbOut bytes) el valor de section/key, o deflt si
 * no existe. Devuelve la cantidad de bytes escritos en out, sin contar el
 * terminador nulo.
 */
int OpusShellProfileString(const char *section, const char *key,
                            const char *deflt, char *out, int cbOut);

/* Devuelve el valor entero de section/key, o deflt si no existe o no es
   representable como entero. */
int OpusShellProfileInt(const char *section, const char *key, int deflt);

/* Escribe value bajo section/key. Devuelve 0 en éxito. */
int OpusShellProfileWrite(const char *section, const char *key,
                           const char *value);

#ifdef __cplusplus
}
#endif
