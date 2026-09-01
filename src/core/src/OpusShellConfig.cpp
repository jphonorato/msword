/*
 * Implementación del contrato de persistencia de configuración
 * (src/core/include/OpusShellConfig.h, docs/port-qt/01-core-shell-boundary.md
 * §B5).
 *
 * Reemplaza GetProfileString/GetProfileInt/WriteProfileString con QSettings.
 * Cada llamada abre su propio QSettings::IniFormat/QSettings::UserScope,
 * organización "OpusShell", aplicación "Word1" -- sin caché propia, tal
 * como especifica §B5. QSettings::setPath() permite redirigir esa ruta en
 * pruebas sin tocar la configuración real del usuario; ver
 * OpusShellConfig_test.cpp.
 *
 * Semántica que este archivo replica del Win16 original, verificada contra
 * el comportamiento documentado de GetProfileInt/GetProfileString (no hay
 * sitio en el árbol de pruebas existente que ejercite estas tres funciones
 * hoy -- ver el reporte de cierre):
 *
 * - GetProfileInt usa el valor por omisión solo cuando la clave no existe.
 *   Si existe pero no es numérica, el valor real devuelto es 0 (conversión
 *   estilo atoi: espacio inicial, signo opcional, dígitos hasta el primer
 *   carácter no numérico; ninguno de los dos.deflt). Esto NO es lo mismo
 *   que QString::toInt(), que exige la cadena completa como número válido.
 * - GetProfileString sí usa el valor por omisión cuando la clave no existe,
 *   sin distinción adicional.
 *
 * Fuera de alcance de este archivo (call sites no migrados en esta fase,
 * documentado en el reporte de cierre): print2.c pasa key=NULL a
 * GetProfileString para enumerar todas las claves de una sección
 * ("devices"); este contrato de tres funciones no cubre esa forma de
 * enumeración. Migrar ese sitio exige extender el contrato, no solo
 * traducir la llamada.
 */

#include "OpusShellConfig.h"

#include <QByteArray>
#include <QSettings>
#include <QString>

#include <algorithm>
#include <cstring>

namespace {

QSettings OpenSettings() {
    return QSettings(QSettings::IniFormat, QSettings::UserScope, "OpusShell",
                      "Word1");
}

/* Conversión estilo atoi: espacio inicial, signo opcional, dígitos hasta el
   primer carácter no numérico. Devuelve 0 si no hay ningún dígito, igual
   que atoi/GetProfileInt sobre una entrada no numérica presente. */
int AtoiLike(const QString &s) {
    int i = 0;
    const int n = s.length();
    while (i < n && s.at(i).isSpace()) {
        ++i;
    }
    bool neg = false;
    if (i < n && (s.at(i) == QLatin1Char('+') || s.at(i) == QLatin1Char('-'))) {
        neg = (s.at(i) == QLatin1Char('-'));
        ++i;
    }
    long value = 0;
    bool any = false;
    while (i < n && s.at(i).isDigit()) {
        any = true;
        value = value * 10 + s.at(i).digitValue();
        ++i;
    }
    if (!any) {
        return 0;
    }
    return static_cast<int>(neg ? -value : value);
}

}  // namespace

extern "C" int OpusShellProfileString(const char *section, const char *key,
                                       const char *deflt, char *out,
                                       int cbOut) {
    if (cbOut <= 0) {
        return 0;
    }

    QSettings settings = OpenSettings();
    settings.beginGroup(QString::fromUtf8(section));
    const QString value =
        settings.contains(QString::fromUtf8(key))
            ? settings.value(QString::fromUtf8(key)).toString()
            : QString::fromUtf8(deflt ? deflt : "");
    settings.endGroup();

    const QByteArray utf8 = value.toUtf8();
    const int toCopy = std::min<int>(utf8.size(), cbOut - 1);
    if (toCopy > 0) {
        std::memcpy(out, utf8.constData(), static_cast<size_t>(toCopy));
    }
    out[toCopy] = '\0';
    return toCopy;
}

extern "C" int OpusShellProfileInt(const char *section, const char *key,
                                    int deflt) {
    QSettings settings = OpenSettings();
    settings.beginGroup(QString::fromUtf8(section));
    const bool present = settings.contains(QString::fromUtf8(key));
    const QString value =
        present ? settings.value(QString::fromUtf8(key)).toString() : QString();
    settings.endGroup();

    if (!present) {
        return deflt;
    }
    return AtoiLike(value);
}

extern "C" int OpusShellProfileWrite(const char *section, const char *key,
                                      const char *value) {
    QSettings settings = OpenSettings();
    settings.beginGroup(QString::fromUtf8(section));
    settings.setValue(QString::fromUtf8(key), QString::fromUtf8(value));
    settings.endGroup();
    settings.sync();
    return settings.status() == QSettings::NoError ? 0 : 1;
}
