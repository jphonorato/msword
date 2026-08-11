# Rama Qt — tabla de sustitución de fuentes (§B2.5)

**Fecha:** 2026-08-11
**Estado:** aprobado para implementación
**Alcance:** solo el paso 2 de la secuencia Qt-2 (`01-frontera-nucleo-shell.md`,
"Secuencia recomendada para Qt-2"). No implementa B2 (contrato de medición de
texto); es su prerrequisito.

## Contexto

`01-frontera-nucleo-shell.md` §B2.5 dejó medido, con `gdi_synth.c`, que la
sustitución de nombres de fuente de época del oráculo Winelib no es intuitiva:
`Helv`, `Tms Rmn`, `Script` y `Modern` resuelven los cuatro a **Liberation
Sans**, incluido `Tms Rmn`, que es un nombre serif. Como los avances de B2
dependen del archivo físico (§B2.3: `QRawFont` sobre el mismo archivo,
ppem entero, `PreferFullHinting`), el shell tiene que reproducir la misma
tabla de sustitución que aplica Wine, no simplemente tener fuentes
disponibles. Sin esa tabla, B2 no tiene con qué construir la fuente Qt del
lado shell.

Esta rama solo cubre las 4 fuentes que Word 1.1a carga por defecto en su
tabla maestra de fuentes al arrancar (`initwin.c`, bloque `InitWinWord` /
`vhsttbFont`, `ftc` 0-3): **Tms Rmn, Symbol, Helv, Courier**. `Script` y
`Modern` aparecen en la sonda `gdi_synth.c` pero no en esa tabla de arranque;
quedan fuera de alcance aquí (no se sabe todavía si son nombres vivos en
algún otro punto del motor o resabios del experimento de medición).

## No-objetivos

- No implementa el contrato B2 (`OpusShellFontMetrics.h` ya existe declarado;
  esta rama no lo toca ni añade su implementación).
- No cubre `Script` ni `Modern`, ni ningún otro nombre de fuente fuera de la
  tabla maestra de arranque.
- No resuelve variantes negrita/cursiva por archivo separado: §B2.5 ya midió
  `tmOverhang = 0` en los ocho casos con TrueType bajo Wine, así que negrita
  y cursiva son sintetizadas por el rasterizador sobre el mismo archivo
  regular, no archivos distintos. Esta rama solo resuelve el archivo del
  peso regular.
- No decide todavía qué hace el shell si `Symbol` resulta no tener una ruta
  de sustitución directa comparable a las otras tres (ver riesgo abajo);
  eso se documenta como pregunta abierta, no se fuerza una respuesta aquí.

## Método

Extiende el patrón ya usado en `docs/port-qt/scripts/fidelity/gdi_synth.c`
(mismo directorio, mismo estilo de sonda de un solo uso, no integrada al
build), en dos pasos:

1. **Resolución de familia (lado oráculo).** Para cada uno de los 4 nombres,
   `CreateFontIndirectA` con ese `lfFaceName` a 14 pt / 96 ppp (misma
   configuración que §B2.3) y `GetTextFaceA` para leer la familia realmente
   resuelta — el mismo mecanismo que `gdi_synth.c` ya usa para los ocho casos
   de negrita/cursiva, aplicado ahora a los 4 nombres base.
2. **Resolución de archivo físico.** El nombre de familia que devuelve
   `GetTextFaceA` no es un path. Se resuelve con `fc-match -f '%{file}'
   "<familia>"` (fontconfig, que es lo que memoria/GDI de Wine usa por debajo
   en Linux para localizar el archivo TrueType real), y se verifica cruzado
   contra la tabla `name` del propio archivo (`fc-scan --format '%{family}'
   <path>` o equivalente) para no dar la ruta por buena solo porque
   fontconfig la ofreció — mismo estándar de verificación que B2.3 usó para
   confirmar el redondeo (no se aceptó la primera hipótesis sin medir).

Riesgo conocido antes de medir: `Symbol` es un nombre de fuente de codificación
simbólica (no Latin-1 estándar), y no hay garantía de que fontconfig lo
resuelva a algo comparable a como resuelve `Tms Rmn`/`Helv`/`Courier`. Si
`fc-match` devuelve una fuente simbólica real (p. ej. una fuente de símbolos
instalada) eso se documenta tal cual; si degrada a una fuente Latin genérica
(mismo tipo de sorpresa que ya se vio con `Tms Rmn` → Liberation Sans), eso
también se documenta y queda como advertencia expresa para cuando B2 tenga
que decidir cómo tratar texto en fuente Symbol.

## Entregables

1. **Sonda:** `docs/port-qt/scripts/fidelity/font_substitution.c` — programa
   Winelib de un solo uso, junto a los demás de `fidelity/`. Imprime, por
   cada uno de los 4 nombres: nombre pedido, familia resuelta
   (`GetTextFaceA`), y se deja como entrada manual al paso de `fc-match` (no
   se linkea fontconfig dentro del binario Winelib; el segundo paso corre
   por separado, en Linux nativo, tomando como entrada la familia que imprimió
   la sonda). `fidelity/README.md` gana una fila nueva en su tabla.
2. **Doc:** nueva subsección `§B2.6 — Tabla de sustitución de fuentes` en
   `01-frontera-nucleo-shell.md` (después de §B2.5), con la tabla medida
   (nombre de época → familia resuelta → archivo físico), el comando exacto
   usado en cada paso, y el resultado de la verificación cruzada. Se marca
   ahí mismo si `Symbol` requirió tratamiento especial.
3. **Código:** `src/core/include/OpusShellFontSubstitution.h` +
   `src/core/src/OpusShellFontSubstitution.cpp`, agregados a
   `add_library(opus_core ...)` en `src/core/CMakeLists.txt` siguiendo el
   patrón de `opus_shell_memory` (biblioteca estática, sin dependencia de
   Qt ya que esto es solo una tabla de datos, `install(... ARCHIVE DESTINATION
   lib)`). Interfaz mínima, en C, siguiendo el estilo de
   `OpusShellFontMetrics.h`:

   ```c
   /* Devuelve la ruta absoluta al archivo de fuente física que sustituye
      al nombre de época dado, o NULL si eraName no es uno de los 4
      nombres cubiertos (Tms Rmn, Symbol, Helv, Courier). La ruta viene
      fija en la tabla medida en docs/port-qt/01-frontera-nucleo-shell.md
      §B2.6 -- no se recalcula en tiempo de ejecución. */
   const char *OpusShellSubstituteFontFile(const char *eraName);
   ```

   Sin target de test en CMake todavía: no tiene consumidor hasta que B2 se
   implemente (mismo estado que `OpusShellFontMetrics.h` hoy, declarado pero
   sin implementación consumida). Se agrega igual al build de `opus_core`
   para que compile desde ya y no quede código sin verificar hasta B2.

## Verificación

- La sonda compila con `winegcc` y corre contra el oráculo, igual que
  `gdi_synth.c` (`WINEDEBUG=-all ./font_substitution.exe`).
- La tabla en el doc se llena con datos medidos, no supuestos — si alguno de
  los 4 nombres no resuelve a algo esperable (como ya pasó con `Tms Rmn`),
  se documenta el resultado real, no lo intuitivo.
- `OpusShellSubstitutionFontFile` (biblioteca `opus_core`) compila limpio
  bajo `OPUS_CORE_BUILD_TESTS=OFF` y `ON`.

## Preguntas abiertas

- Si `Symbol` no tiene sustituto directo razonable, ¿el shell sintetiza un
  mapeo de glifos manual, usa una fuente Symbol instalada aparte, o Symbol
  queda fuera del alcance de fidelidad hasta una decisión posterior? No se
  responde en esta rama; se documenta el hallazgo y se deja abierta para
  cuando B2 la necesite.
- `Script` y `Modern`: si en una revisión futura aparecen como nombres vivos
  fuera de la tabla de arranque, esta tabla se extiende con el mismo método;
  no hace falta rediseñar nada.
