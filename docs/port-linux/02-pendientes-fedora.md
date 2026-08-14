# Pendientes que requieren Fedora — diagnóstico de heap corruption en el arranque de WORD1

**ACTUALIZADO 2026-08-14 — la premisa del título quedó parcialmente
superada.** La sesión hp-15 (`01-...md`, sección "Sesión hp-15") reprodujo
el crash en **EndeavourOS/Arch** (GCC 16.2.1, wine-staging 11.15), no solo
en Fedora — confirma que la variable relevante es GCC ≥15, no la distro
específica. Los ítems 1 y 2 de este documento, que estaban listados como
bloqueados a la espera de Fedora, ya se ejecutaron ahí y quedan marcados
**hecho** abajo, con el resultado real en vez de la instrucción pendiente.
Fedora en sí sigue sin probarse — el documento no queda invalidado, solo
deja de ser la única vía. El resto de los ítems (3-7) sigue abierto tal
cual, independiente de en qué entorno se ejecuten.

Checklist de continuación de `01-diagnostico-heap-corruption-arranque.md`.
Todo lo que sigue quedó bloqueado, sin cerrar, o sin poder generalizarse
desde el sandbox usado en las sesiones más recientes (Debian 13 "trixie",
GCC 14.2.0, wine-10.0 Debian repack, `gdb` 16.3) — que **no reproduce el
crash** (§11.2, §13). El crash sí reproduce, de forma consistente, en:

- **Fedora 44**
- **GCC 16.1.1**
- **wine-staging 11.0**
- `gdb` 17.2
- `valgrind` 3.27.1
- **EndeavourOS (Arch), GCC 16.2.1, wine-staging 11.15** (hp-15, ver arriba)

No se sabe todavía cuál de estas variables (versión de Wine, de GCC, o
config de `wine.conf`/DPI/tema) es la que hace la diferencia frente a
Debian — no investigado (§11.2). Igualar el entorno completo es la única
vía confirmada para reproducir, no solo la versión de Wine. Lo que sí se
puede afirmar ahora (hp-15): **no hace falta Fedora específicamente**,
GCC ≥15 alcanza.

Referencias entre paréntesis (`§N`) son secciones de
`01-diagnostico-heap-corruption-arranque.md`.

---

## 0. Prerrequisitos — ya resueltos en `main`, no repetir el trabajo

Los tres bloqueadores de build que aparecieron al reconstruir en un
entorno distinto a Fedora (§11.1) ya están commiteados en `main`, no hace
falta re-descubrirlos:

- `wrc: codepage 1252 not supported` → fix con `--nls-dir` (`a0191b0`).
  **Verificar en Fedora**: el path hardcodeado es
  `/usr/share/wine/nls` — confirmar que existe ahí en el paquete
  `wine-staging` de Fedora antes de asumir que aplica sin cambios.
- `GetCurrentThreadStackLimits was not declared` → declaración local
  guardada (`a0191b0`). En Wine 11.0/Fedora la función **sí** está
  declarada nativamente (a diferencia de Wine 10.0/Debian, que fue el
  motivo del fix) — confirmar que la declaración local no choca
  (redefinición) contra la real de ese `windows.h`.
- `opus_word1_ui_test.cpp:379` cast `LONG`/`20L`, más `std::min`,
  `_wcsicmp` sin declarar y `wmain` sin `extern "C"` → reparado y
  **linkeando** (`6ff2b53`). Esto desbloquea disparar `C_FormatLineDxa`
  vía `--typing`/`--font-typing` en vez de lanzar `WORD1` en vacío (ver
  §5, punto abajo).

Además, desde la última sesión en Fedora (§1-§9) se sumaron **7 commits**
de migración `Global*` → `OpusMem*` (`bf7a5e1`..`aab06e5`, más
`opus_shell_spine` ya enlazado en `WORD1` desde `7966f3b`) — el binario a
diagnosticar en Fedora ya no es el mismo que en §1-§9. Reconstruir desde
`HEAD` de `main`, no reusar un binario viejo.

```bash
cd src
cmake --preset linux-winelib-debug
cmake --build --preset linux-winelib-debug --target opus_original_engine
cmake --build --preset linux-winelib-debug --target WORD1
```

---

## 1. Confirmar que el crash sigue reproduciendo (baseline) — HECHO (hp-15)

Ejecutado en EndeavourOS/Arch desde `HEAD` (`5fed452`), 4 corridas con
`gdb -q --batch -ex run -ex "bt full" --args wine WORD1.exe.so`: reproduce
4/4, con **dos firmas nuevas** además de las ya conocidas (`free(): invalid
pointer`, `double free or corruption (!prev)`) — ver `01-...md`, "Sesión
hp-15" §1. Confirma que la migración `Global*`→`OpusMem*` de las 7
commits previas no cambió el comportamiento de fondo: sigue siendo
corrupción de heap real, timing-dependiente. No hace falta repetir esto en
Fedora salvo para comparar firmas específicas.

---

## 2. Hito 2 del plan v2 — `WINEDEBUG=+heap` — HECHO (hp-15), resultado negativo pero informativo

Ejecutado por primera vez en cualquier entorno (hp-15, `01-...md` §2):
**888.073 líneas**, crash real en la línea 24273. **Hallazgo:** no hay
ningún `RtlFreeHeap` logueado inmediatamente antes del crash — el `free()`
que aborta es el destructor de un `std::wstring` de C++ (`operator
delete`/glibc directo), no pasa por el heap propio de Wine que `+heap`
instrumenta. **Consecuencia para el resto del plan:** `+heap` queda sin
valor diagnóstico adicional para esta familia de bug (cualquier corrupción
originada en un `std::wstring`/`std::vector` de C++ es invisible a esta
herramienta) — no reintentar sin una hipótesis nueva que involucre memoria
Win32 (`GlobalAlloc`/`HeapAlloc`) directamente. La vía que sí dio una pista
concreta fue el escaneo manual de stack (`01-...md` §3), no esto.

---

## 3. Symbolizar frame #0 — sigue sin nombre

`addr2line` (§5) solo resolvió frame #1 (`N_FormatLineDxa`, dentro de
`WORD1`). Frame #0 — el punto exacto del `write` a `0x0` — es una
dirección cruda que no cae dentro del rango de `WORD1`, así que es una
DLL de Wine (`user32`/`gdi32`/`ntdll`, sin confirmar cuál). Nunca
resuelto porque `winedbg`/`dbghelp` está roto en este binario (§3: no
puede parsear el DWARF de GCC 16) y `addr2line` offline solo tiene el
DWARF de `WORD1.exe.so`.

**Vía sugerida, no intentada:** con el proceso corriendo (o en el core
si `ulimit -c unlimited` deja uno), leer `/proc/PID/maps` para identificar
qué `.so` de Wine cubre el rango de la dirección de frame #0 fresca (paso
2 de la sección anterior), y correr `addr2line -e <esa.so>` (o `nm`, si
no tiene DWARF) contra esa biblioteca específica, no contra `WORD1`.
Necesita repetirse con la dirección fresca del build actual, la de §5 ya
no sirve.

---

## 4. Breakpoints por archivo:línea — usar el atajo de §12.1, no el plan viejo de §11.4

**No hace falta** el plan de 3 pasos que §11.4 dejó recomendado
(breakpoint por dirección absoluta, o instrumentar `LOADFONT.C` con
`fprintf` bajo autorización) — quedó **refutado como innecesario** en
§12.1, aunque esa refutación se hizo en Debian (sin el crash real). El
mecanismo (símlinks de 18 fuentes case-shimmed a
`generated/lowercase-c/*.c`, DWARF registra el path en minúsculas) es
una propiedad del build, no del entorno, así que debería aplicar igual en
Fedora:

```bash
gdb -q --batch -ex "set breakpoint pending on" \
    -ex "break loadfont.c:349" -ex "break loadfont.c:709" \
    -ex "run" -ex "bt 6" --args wine WORD1.exe.so
```

**En minúsculas.** `break LOADFONT.C:349` (mayúsculas, como venía
haciéndose en Fedora en §11.3 antes de aislar la causa) nunca resuelve —
queda `<PENDING>` para siempre. Confirmar esto primero en Fedora antes de
asumir que el problema de §2/§11.3 (`info sharedlibrary` vacío por
`wine-preloader`) sigue aplicando de la misma forma — puede que Wine
11.0/Fedora se comporte distinto de Wine 10.0/Debian acá, no verificado.

Con esto, cerrar la hipótesis de `vsci.hdcScratch` (§10) **con el crash
real reproduciendo**, no como en §13 (que la cerró solo para una corrida
sin crash en Debian — no generalizable). Repetir ahí mismo lo de §13:

```bash
DISPLAY=:99 WINEDEBUG=+gdi wine WORD1.exe.so >gdi.trace 2>&1
```

y `grep` el valor de handle de `vsci.hdcScratch` (obtenido del propio
`gdb` en el breakpoint) buscando si aparece junto a
`NtGdiDeleteObjectApp`/`free_gdi_handle` **antes** de que el crash real
ocurra — en Debian nunca hubo crash con el que cruzar esto, en Fedora sí
lo hay.

---

## 5. Alternativa/complemento: disparar `C_FormatLineDxa` con `opus_word1_ui_test`

Ahora que `opus_word1_ui_test` compila y linkea (`6ff2b53`, ver §0), está
disponible el camino que §11.1 dejó bloqueado: usar `--typing`/
`--font-typing` para forzar una ruta de formateo de línea real en vez de
depender de que el arranque en vacío la ejercite por su cuenta. Puede dar
una reproducción más determinista o más rápida del crash que lanzar
`WORD1` sin interacción.

---

## 6. Ítem secundario, no confirmado para Fedora: `opus_shell_memory_foreign_test`

Sin relación directa con el heap corruption, pero es otro ítem que quedó
condicionado a un entorno con soporte multiarch (P6 del checklist-audit
de memoria, `docs/superpowers/specs/2026-08-11-opus-memory-passthrough-checklist-audit.md`):
el test compila y linkea, pero no se pudo ejecutar en el sandbox Debian
de esta sesión porque falta `wine32`/multiarch
(`it looks like wine32 is missing... apt-get install wine32:i386`,
reproducido de nuevo al reconstruir `WORD1` en esta sesión). Si Fedora
tiene el soporte de 32 bits ya instalado (no confirmado, no asumido),
correr:

```bash
ctest --test-dir out/linux-winelib-debug -R opus_shell_memory_foreign_test
```

y confirmar en verde — sería la primera ejecución real de ese test desde
que se implementó.

---

## 7. Explícitamente descartado — no reintentar sin evidencia nueva

- **ASan.** Probado en Fedora (binario Winelib trivial, aislado, §8):
  falla en la inicialización propia de ASan (`AddressSanitizer failed to
  deallocate`) por choque con la reserva de espacio de direcciones de
  `wine-preloader` — probadas 5 combinaciones de `ASAN_OPTIONS`, mismo
  resultado en las 5. No es un problema de este build, es incompatibilidad
  Winelib x86-64/ASan. No hay reconfiguración de flags pendiente de
  probar; se necesitaría un enfoque distinto (no identificado) para
  reabrir esta vía.
- **`valgrind --trace-children`.** Choca con la misma reserva de
  `wine-preloader` (§4) — pero esto era específico del empaquetado de
  Fedora, no universal (ver siguiente punto).
- **`valgrind` sin `wine-preloader`, sesión hp-15 cont. (`01-...md` §8).**
  Ni el VPS (Debian 13) ni Arch/hp-15 tienen `wine-preloader` — ahí
  `valgrind` sí arranca. Probado en ambos: 240s limpio en el VPS (no
  reproduce), y **directo contra el crash real en Arch** — el crash
  ocurre igual bajo valgrind (mismo `free(): invalid pointer` de glibc),
  pero el log de valgrind no registra ningún error, con la intercepción
  de `malloc`/`free` confirmadamente activa (`-v` mostró los `REDIR`
  antes de cargar `ntdll.so`). **Descartado por un motivo más amplio que
  `wine-preloader`**, causa exacta no investigada — no vale reintentar
  sin entender primero por qué memcheck no ve esta corrupción con la
  intercepción activa.
- **`glibc.malloc.check` (`LD_PRELOAD=libc_malloc_debug.so`).** Corre sin
  chocar con nada (§12.4), pero con valor diagnóstico limitado: casi toda
  la memoria de Word 1.1a pasa por el heap propio de Wine
  (`Rtl*Heap`/`ntdll`), no por `malloc` de glibc — un `write` fuera de
  rango en *ese* heap no lo va a detectar este checker sin importar
  cuánto tiempo corra limpio. Usar `WINEDEBUG=+heap` (§2 de este
  documento) en su lugar, no esto.
