# Comportamiento de WORD1: lista de arranque bloqueado

Fecha original: 2026-08-15 · Rama `fix/winelib-startup-blocked`. Esta
serie parte de la tabla de §26 de
[`01-diagnostico-heap-corruption-arranque.md`](01-diagnostico-heap-corruption-arranque.md)
(los 7 fallos de comportamiento real que quedaron cuando el arnés dejó
de crashear). Cada sección es un ítem de esa lista.

Todas las afirmaciones están respaldadas por comandos ejecutados en
debian13 contra `/home/pablo/build-debian13-verify`, `DISPLAY=:59`.

---

## 1. `--about`: "Help About dialog did not appear"

**Test:** `opus_word1_about_test` · `stage=0 responsive=1` (y, si se
fuerza el cierre del MessageBox, `exit=0x0 mainWindow=0`).

**Estado:** causa raíz confirmada. El diálogo About **sí se materializa**
cuando `WM_COMMAND` 182 llega a `CmdAbout`. El test no lo ve porque ese
comando nunca se ejecuta: un MessageBox modal de arranque deja
`vcInMessageBox=1` y `AppWndProc` descarta todo `WM_COMMAND`. Fix de
producto diferido (3+ intentos de desbloqueo no dejaron el test en
Passed; ver más abajo).

### Qué significa `stage=0` / `responsive=1`

En `opus_word1_ui_test.cpp` (`about_mode`):

1. Espera la ventana principal `"Microsoft Word - Document1"`.
2. `Sleep(1000)`.
3. `PostMessageW(main, WM_COMMAND, kHelpAbout=182, 0)`.
4. Espera una top-level `OpusSdmDialog` 5 s.
5. Si no aparece, imprime
   `exit=… mainWindow=… responsive=… stage=…`.

- `responsive=1` es `window_is_responsive`: el proceso sigue vivo, la
  ventana principal responde a `WM_NULL`.
- `stage=` es `GetPropA(main, "OpusX64AboutStage")`. **Nadie en el
  árbol pone esa propiedad.** `stage=0` solo dice “nunca se instrumentó
  el comando”, no en qué línea falló SDM.

`182` es el `bcm` correcto (`opuscmd.h`: `bcmAbout = 182`).

### El diálogo no se crea (never-reached), no es invisible

Instrumentación temporal en `NatAppWndProc` y
`HdlgStartDlg`/`TmcDoDlgDli`/`IdDoMsgBox` (revertida; no quedó en el
árbol):

```
create_dialog_host hid=32773 / 32774   ← ribbon / ruler (hijos, no About)
IdDoMsgBox parent=(nil) flags=0x30
  caption='Microsoft Word'
  text='Low memory: cannot display requested font'
NatAppWndProc WM_COMMAND LOWORD=2799 (ViewPage)  vcInMessageBox=1
NatAppWndProc WM_COMMAND LOWORD=182  (HelpAbout) vcInMessageBox=1
  sy[182] mct=3 name=HelpAbout pfn≠0
— no hay TmcDoDlgDli hid=44 —
```

La tabla de comandos está bien (`mctSdm`, `CmdAbout` resuelto).
`AppWndProc` (`wproc.c:854`) hace `if (vcInMessageBox) return 0`. El
About **nunca llega** a `HdlgStartDlg` ni al bloque compartido
`create_dialog_host` líneas 338-397.

El MessageBox es real: `xwininfo` muestra
`"Microsoft Word" 295x82+364+356` junto a
`"Microsoft Word - Document1"`. En el screenshot de Xvfb se ve un
rectángulo negro (el `#32770` no pinta el texto en este Wine). El
origen del texto es `eidCantRealizeFont` (`error.c:933-934`), disparado
desde `ReportPendingAlerts` cuando `vmerr.mat == matFont`.
`LOADFONT.C` pone `matFont` si `CreateFontIndirect` / `OurSelectObject`
falla y cae a `SYSTEM_FONT`.

### El camino About funciona si el comando se ejecuta

Con WORD1 lanzado a mano en `:59`, `xdotool` Return sobre el
MessageBox y luego Alt+H, A:

```
IdDoMsgBox returned 1
NatAppWndProc WM_COMMAND LOWORD=182  vcInMessageBox=0
HcabAlloc cabi=1824 ok=1
TmcDoDlgDli enter hid=44 flags=0x41
create_dialog_host popup caption=About Microsoft Word
materialize_about_template hid=44
TmcDoDlgDli branch hid=44 modal=1 native_modal=1
```

`xwininfo`: `"About Microsoft Word" 410x230`. El host SDM, el
`materialize_about_template` y el loop modal de `TmcDoDlgDli` están
sanos. El fallo del test no está en las líneas 338-397.

### Intentos de desbloqueo (ninguno dejó Passed)

| # | Cambio | Resultado |
|---|---|---|
| 1 | `IdDoMsgBox` con owner `vhwndApp` | El `#32770` se puede encontrar; al cerrarlo WORD1 acaba en `exit=0` |
| 2 | El test hace `PostMessage(IDOK)` / `WM_CLOSE` / `BM_CLICK` al `#32770` y luego About | Tras dismiss `mainWindow=1`; el About deja `exit=0x0 mainWindow=0` |
| 3 | `IdDoMsgBox` devuelve `IDOK` al ver `"cannot display requested font"` (sin `MessageBoxA`) | WORD1 **sale solo a los ~3 s** (el loop modal del MB era el que mantenía vivo el arranque: bombea `ViewPage` y el resto del idle) |
| 4 | Test envía `VK_RETURN` + `TmcDoDlgDli` drena `WM_QUIT` | Sigue `exit=0x0 mainWindow=0` |

Cerrar el alerta desde otro proceso Wine, o saltárselo, no reproduce el
camino interactivo (Return en el X window, luego el menú). El MessageBox
de arranque es a la vez **el bloqueo de `WM_COMMAND`** y **parte del
bombeo de mensajes que termina el init**. Eso es pregunta de
arquitectura (¿arreglar `CreateFontIndirect` para que no haya
`matFont`? ¿un `IdDoMsgBox` que no sea modal y no corte el idle? ¿el
arnés usa `SendInput` en el hilo de WORD1?), no un parche de una línea
en `create_dialog_host`.

### Shared vs independent (Tasks 4–5)

**El mismo punto de fallo cubre `kIddNewDoc` y `kIddSaveAs`.** File New
(1813) y File Save As (1897) también van por `AppWndProc` → `FExecCmd`.
Con `vcInMessageBox=1` esos `WM_COMMAND` se tragan igual. El bloque
338-397 de `create_dialog_host` **no** es el fallo compartido: About,
New y Save As divergen *antes*, en el gate de `vcInMessageBox`.

Cuando el alerta deje de bloquear el pump, Tasks 4–5 deben
**verificar primero** el mismo camino (`TmcDoDlgDli` ya creó About con
`hid=44`). Si New/Save As siguen fallando *después* de un About verde,
entonces sí investigación propia (Save As además entra en
`run_word95_common_file_dialog`).

Pendiente de producto, fuera de este ítem: por qué
`CreateFontIndirect` falla (Arial 10 en la cinta se ve; el documento
cae a `SYSTEM_FONT`); el `#32770` y el About se pintan negros en este
Xvfb/Wine.

### Fix round (2026-08-15): sitio LOADFONT + `OpusShellCharWidths`

De los tres sitios que ponen `matFont` en `LOADFONT.C`, el que disparó
antes del MessageBox es el **3** (camino Linux, líneas 428-459):
`OpusShellCharWidths(...) != 0` → `LSystemFontErr`.

Instrumentación temporal de `OpusShellCharWidths` (luego retirada):

```
OpusShellCharWidths ftc=2 ps=0 catr=0 chFirst=0 cch=256 rc=-1 why=bad-px
```

No es `CreateFontIndirect` NULL (sitio 1) ni `OurSelectObject`/`FSelectFont`
(sitio 2): se llegó a pedir anchos. La petición de arranque es Helv
(`ftc=2`) con `hps==0`. `RawFontFor` hacía `px = MulDiv(ps/2, 96, 72)`
y rechazaba `px<=0`. Ese `-1` es el que pone `matFont` y deja
`vcInMessageBox=1`.

Cambio en `src/core/src/OpusShellFontMetrics.cpp` (sin tocar `src/Opus/`):

- `hps==0` se mide como 10 pt (`hpsDefault`), igual que
  `CreateFontIndirect(lfHeight==0)` usa altura por defecto.
- WORD1 no tiene `QGuiApplication`. Crear una en el hilo Wine cuelga el
  pump o mata el proceso. Sin app, `QRawFont` no se construye: se
  rellenan los anchos desde la tabla oráculo ya medida
  (`opus_shell_font_metrics_oracle_table.h`, Helv 10 pt).

Tras ese éxito, el MessageBox de fuente **no aparece**, pero WORD1
sigue sin dejar About verde: el init continúa y cae en
`FInsertInPl` (`clsplc.c:829`) desde `C_PushLbs` (`layout2.c:1430`),
write AV `0xC0000005` vía `HpInPl` (`opus_asm_resn2_pl.cpp`).
`ctest -R opus_word1_about_test` queda:

```
Help About process exit=0x0 mainWindow=0 responsive=0 stage=0
Help About dialog did not appear
```

(debian13, `/home/pablo/build-debian13-verify`, `DISPLAY=:59`)

Una vez con `QGuiApplication` en el hilo Wine el diálogo About
*sí* se creó (`OpusSdmDialog`) y el fallo pasó a “did not finish
initializing” (el host no respondía `WM_NULL`). Ese camino no se dejó:
Qt en el hilo Wine no es viable. El crash de `FInsertInPl` es el
siguiente bloqueo; no es un quinto skip del MessageBox.

### Estado al cortar (2026-08-15, HEAD `134cddc`)

`opus_word1_about_test` **sigue en Failed** (~7.8 s,
`exit=0x0 mainWindow=0`). El MessageBox de fuente ya no aparece. El
proceso muere en layout:

```
Exception 0xC0000005 write at 0xFFFFFFFD3726D202
OpusMoveBytesEnd+0x2B
FInsertInPl+0x1B6
C_PushLbs+0x296
```

(`build/WORD1-crash.txt`; call site C `layout2.c:1430`
`FInsertInPl(vhpllbs, ilbs, plbsTo)`.)

Instrumentación temporal de `HpInPl`/`OpusPlData` (revertida, no quedó
en el árbol) dejó `build/t3-h2-pl.log`. Las primeras llamadas son
PLs sanos:

```
iMac=1 iMax=1 cb=2  brgfoo=20  fExternal=0
iMac=1 iMax=1 cb=136 brgfoo=256 fExternal=0   ← dest in-heap, insane=0
```

La última, ya con el header destrozado, es otro `hpl`:

```
HpInPl hpl=0x7ffffe811970 *hpl=0x7ffffe811bc0
  iMac=-1072622911 iMax=32726 cb=6 brgfoo=0 fExternal=-31497312
  i=-1072622912 base=(nil) dest=0xfffffffe80667080 insane=1
```

`HpInPl` no inventa ese puntero: le pasan un bloque que ya no es un
`struct PL`. `iMac==iMax==1` en las llamadas sanas implica que el
siguiente `FInsertInPl` entra en el grow (`clsplc.c:847-874`).
Siguiente Phase 1 (no hecha): discriminar *grow que corrompe el
header* (`FChngSizeHCw` / tamaño `brgfoo + cb*iMax`) vs *handle
equivocado* (no es `vhpllbs`). No editar `src/Opus/`.

Un intento a medias de anchos GDI (`opus_gdi_char_widths.cpp` +
`OpusShellSetCharWidthsFallback`) se descartó al cortar: no llegó a
ctest verde. No reintroducirlo hasta tener el log de `HpInPl` sobre
el grow.

**Shared vs independent (Tasks 4–5), actualizado:** About / New /
Save As ya no están tapados por `vcInMessageBox`. Siguen
bloqueados porque el proceso no sobrevive al primer layout. Verificar
primero cuando About esté verde.

### Fix round 2 (2026-08-15): H1 vs H2 del AV en `FInsertInPl`

Phase 1, un cambio cada vez. El MessageBox de fuente sigue cerrado.

**H1 (métricas):** `OpusShellCharWidths` devolvió 0 (sin `matFont`) y
rellenó la tabla con un dummy constante 8. El AV **no desapareció**.
Luego se midió con GDI (`CreateCompatibleDC` + `CreateFontIndirectA`
mismo `LOGFONT` que `C_FGraphicsFcidToPlf`, `hps==0` → `lfHeight==0`,
`GetCharWidthA`; confirmado `GDI ftc=2 ps=0 w32=3 wA=7`). El AV
**sigue** en `FInsertInPl`. Los valores de la tabla no son la causa:
oráculo Helv-10, dummy 8 y GDI Helv `lfHeight=0` mueren igual.

El camino interactivo que sí abre About usa `fFallback` /
`fFixedPitch=true` (`LOADFONT.C:397` no llena `hqrgdxp`). Eso no se
puede forzar desde el puerto sin devolver -1 (`matFont` / MessageBox).

**H2 (header PL / HpInPl):** confirmado. El `hpl` del crash **es**
`vhpllbs` (`lbs=1`, `hsz=1428` = `cbPLBase + 8*sizeof(LBS)` con
`sizeof(LBS)==176`). En la primera `FInsertInPl` el header ya es
basura:

```
iMac=-1072622911 iMax=32726 cb=6 brgfoo=0 fExternal=-31497312
i=-1072622912 base=(nil) dest=0xfffffffe80667080
```

`PL`: `cbPLBase=20`, `fExternal` @ 16. `PLLBS` no tiene `fExternal`
(`rglbs` @ 16). `OpusPlData` trataba cualquier `fExternal!=0` como HQ
→ dest salvaje. Las primeras `HpInPl` son **otros** PLs sanos
(`cb=2`/`cb=136`); `vhpllbs` no se ve sano ni una vez.

El smash **no** es un `bltbh` sobre los 20 bytes del header (un
`memmove` vigilado no disparó). Tampoco un `FChngSizeHCb` de
`vhpllbs`: al `HAllocateCw(1428)` `vhpllbs` aún es nil; no hay
`chng` posterior sobre ese handle antes del AV.

Clamp de `dest` en `HpInPl` evita el write salvaje y mueve el crash a
`UnstackLbs` (camina `ilbsMac` basura) o `IpgdPldrFromWwDocCpIpgd`.
No repara el header.

**Ambas:** H1 no es “tabla Helv-10 ≠ GDI”. H2 es el mecanismo del AV
(`HpInPl` sobre `vhpllbs` ya destrozado). El header se corrompe en
el camino variable-pitch (`fFixedPitch=false`) antes de
`C_PushLbs`; el puerto no ve el store. Sin editar Opus no hay sitio
para restaurar `iMac` antes de `layout2.c:1384`.

**ctest** (debian13, `DISPLAY=:59`, GDI + `fExternal==1` en
`OpusPlData`):

```
opus_word1_about_test ***Failed  7.8 sec
Help About process exit=0x0 mainWindow=0 responsive=0 stage=0
Help About dialog did not appear
CTEST_EXIT=8
Exception 0xC0000005 FInsertInPl+0x1B6 C_PushLbs+0x296
```

**BLOCKED** en líneas Opus (no tocadas):
`src/Opus/wordtech/layout2.c:1384` (`ilbs = (*vhpllbs)->ilbsMac`) y
`1430` (`FInsertInPl`); o `LOADFONT.C:397` (único camino que no
entra en layout variable-pitch).

### Fix round 3 (2026-08-15): `_setjmp` con ABI equivocada — RESUELTO

`opus_word1_about_test` **pasa**. El AV no estaba en `FInsertInPl` ni en
`HpInPl`: los dos eran víctimas. Quien destroza el header de `vhpllbs`
es `setjmp`.

**Evidencia (watchpoint de hardware, no printf).** En el contenedor
debian13, `gdb -batch` sobre `/usr/lib/wine/wine64` (no sobre
`/usr/bin/wine`, que es un script), con `set follow-fork-mode parent`
para no seguir al `wineserver`, y `handle SIGSEGV nostop noprint pass`
para que Wine convierta la falla en excepción Win32:

1. Breakpoint en `layout.c:286` (justo después de
   `vhpllbs = HplInit(sizeof(struct LBS), 8)`). El header está **sano**:
   `iMac=0 iMax=8 cb=176 brgfoo=20 fExternal=0`, `data=0x7ffffe8117a0`.
   Es decir, la hipótesis "ya nace destrozado" de la ronda 2 era falsa:
   nace bien y lo rompen cuatro líneas más abajo.
2. `watch -l *(int *)(data + 16)` (o sea `PL.fExternal`) y `continue`.
   Dispara enseguida: `Old value = 0`, `New value = -31497312`, con
   `#1 LbcFormatPage ... layout.c:290` — o sea `SetLayoutAbort()`.
3. En el punto del disparo, `x/10i $pc-32` muestra el cuerpo del
   callee, que es exactamente el `_setjmp` de MSVCRT x86-64:

   ```
   0x6fffffc2f8e8:  mov    %rdx,(%rcx)      ; buf->Frame
   0x6fffffc2f8eb:  mov    %rbx,0x8(%rcx)   ; buf->Rbx
   0x6fffffc2f8ef:  lea    0x8(%rsp),%rax
   0x6fffffc2f8f4:  mov    %rax,0x10(%rcx)  ; buf->Rsp
=> 0x6fffffc2f8f8:  mov    %rbp,0x18(%rcx)  ; buf->Rbp
   ```

   y los registros: `rdi=0x7ffff79122d8` (que **sí** es
   `&venvLayout.nativeEnv`, el buffer correcto) frente a
   `rcx=0x7ffffe8117a0` (que es `*vhpllbs`).

**Causa raíz, en una frase:** `_setjmp` se resolvía contra el `msvcrt`
PE de Wine, que es Microsoft-x64 y toma el `jmp_buf` en **RCX**,
mientras que todo el código de Opus es System V y lo pasa en **RDI**;
el import escribía su `_JUMP_BUFFER` de 256 bytes sobre el puntero
rancio que quedara en RCX.

En `LbcFormatPage` ese RCX rancio es `*vhpllbs`, el bloque que
`HplInit` acababa de devolver nueve líneas antes, así que cada pasada
de layout escribía estado de registros encima del `struct PL`:

- `fExternal` (offset 16) recibía la mitad baja de RSP →
  `0xfe1f63a0` = `-31497312`, que es justo el valor que la ronda 2
  había registrado.
- offsets 20..27 recibían `0x00007fff` + ceros → de ahí sale el
  `hqple = 0x7fff00000000` del AV.

El síntoma final (tras el clamp de `OpusPlData` de `5bebdd9`) ya no era
`FInsertInPl` sino `FreeHpl` (`clsplc.c:465`): con `fExternal` distinto
de cero toma el PL no-externo por externo, lee ese `hqple` basura de
`rglbs[0]` y llama a `FreeHq` → AV de lectura en `OpusFreeH`
(`opus_x64_heap.cpp:411`, `mov (%rax),%rax` con `rax=0x7fff00000000`).
La cadena `LbcFormatPage → FreePhpl(&vhpllbs) → FreeHpl → OpusFreeH`
quedó confirmada casando los retornos con el desensamblado
(`FreeHpl+84` es exactamente el retorno del `call OpusFreeH` de la rama
`FreeHq`, y el slot `rbp-8` de `FreePhpl` contiene `&vhpllbs`).

Confirmación estática: `nm bin/WORD1.exe.so` mostraba
`__imp__setjmp` y un thunk `t _setjmp` en la misma tabla de imports que
`ShellExecuteA` / `AdjustWindowRect`. `_setjmp` era el **único** símbolo
de CRT importado por error (los otros cuatro con pinta de CRT —
`_lclose`, `_llseek`, `_lread`, `_lwrite` — son APIs Win16 legítimas de
kernel32). `longjmp`, en cambio, sí resolvía a glibc
(`longjmp@GLIBC_2.2.5`): el par estaba roto por la mitad.

Llega ahí porque `Opus/lib/qsetjmp.h` (rama `OPUS_X64`) usa el
`<setjmp.h>` del host y glibc expande `setjmp(env)` a `_setjmp(env)`.

**Fix (todo fuera de árbol restringido):**
`src/port/original/opus_x64_setjmp.cpp` define `_setjmp` en ABI System V
como salto de cola a `__sigsetjmp(env, 0)` — que es literalmente lo que
hace el `_setjmp` de glibc. Tiene que ser salto de cola: un wrapper en C
dejaría un marco de pila que ya no existe cuando `longjmp` vuelve a él.
Al quedar el símbolo definido, `winebuild` deja de generar el import de
`msvcrt` (verificado: `nm` ahora da `T _setjmp` + `U
__sigsetjmp@GLIBC_2.2.5`, sin `__imp__setjmp`). Se añade a
`WORD1_SOURCES` dentro del bloque `if(OPUS_WINELIB_BUILD)` que ya
existía, así que MSVC no lo ve.

**Segundo cambio, necesario para que ctest lo *vea*.** Con el AV
arreglado, `opus_word1_ui_test --about` termina en 2 s con código 0
(diálogo `OpusSdmDialog` creado, botón en el id 1, responde, cierra
limpio), pero ctest seguía dando `Timeout 20 s` sin imprimir nada. La
causa es el §25 ya documentado: `CreateProcessW` devuelve un
`PROCESS_INFORMATION` a cero para `WORD1.exe.so`, así que
`hProcess == nullptr` y el `TerminateProcess` de cada salida es un
no-op; WORD1 sobrevivía al harness reteniendo el pipe de stdout
heredado y ctest esperaba. Mientras WORD1 se moría solo esto no se
notaba. `opus_word1_ui_test.cpp` recupera ahora el PID real desde la
ventana principal (`GetWindowThreadProcessId` + `OpenProcess`) cuando
`hProcess` viene nulo; con eso todo el teardown y las esperas ya
existentes funcionan sin tocarlas, y las búsquedas de ventana
recuperan el filtro por PID exacto que el §26 prefiere.

**ctest** (debian13, `/home/pablo/build-debian13-verify`, `DISPLAY=:59`):

```
1/1 Test #17: opus_word1_about_test ............   Passed    2.88 sec
100% tests passed, 0 tests failed out of 1
```

Estable en 3 ejecuciones consecutivas (2.84 / 2.97 / 2.83 s).

La etiqueta `word1_startup_blocked` pasa de **0/9** a **4/8** (sin contar
`word1_port_smoke_test`, ver más abajo):

```
1/8 Test #11: opus_word1_ui_test ...................   Passed    0.37 sec
2/8 Test #12: opus_word1_clipboard_shortcut_test ...   Passed    0.56 sec
3/8 Test #13: opus_word1_typing_test ...............   Passed   11.24 sec
4/8 Test #14: opus_word1_interaction_test ..........***Failed    0.91 sec
5/8 Test #15: opus_word1_selection_test ............***Failed    1.96 sec
6/8 Test #16: opus_word1_font_typing_test ..........***Failed    0.25 sec
7/8 Test #17: opus_word1_about_test ................   Passed    1.35 sec
8/8 Test #18: opus_word1_save_as_test ..............***Failed    6.39 sec
50% tests passed, 4 tests failed out of 8
```

Los 4 que siguen fallando ya no mueren por el AV: fallan rápido y con
mensaje propio. Son el material de las Tasks 4–5.

Gating: 7/7 verdes (`opus_original_sttb_test`, `opus_original_plc_test`,
`opus_sdm_cab_test`, `opus_original_command_test`,
`opus_shell_memory_foreign_test`, `opus_shell_config_test`,
`opus_shell_font_substitution_test`) más `opus_original_strtbl_test`.

**Dos cosas pendientes, ninguna causada por este fix:**

1. `opus_x64_runtime_test` (gating) **se cuelga** en el tip de la rama:
   ejecutado directamente termina en `timeout 40` sin imprimir una sola
   línea (`rc=124`). No lo provoca esta ronda — el binario de ayer
   (05:46, anterior a `5bebdd9` y a este fix) se colgaba igual, y ni
   `opus_x64_setjmp.cpp` ni el cambio del harness entran en ese target.
   Reconstruirlo no lo arregla. Hay que investigarlo aparte.
2. `word1_port_smoke_test` (`WORD1 --self-test`) no tiene propiedad
   `TIMEOUT` en `CMakeLists.txt:1580`, a diferencia de sus 8 hermanos.
   Mientras WORD1 se moría solo eso no se notaba; ahora que sobrevive,
   un `ctest` completo se queda ahí hasta el default de 1500 s.
   Recomendado (no hecho aquí, queda fuera del alcance de esta ronda):
   añadirle `TIMEOUT 20` igual que a los demás.

**Alcance real del bug.** `SetJmp` no se usa solo en layout: también en
`GRSPEC.C`, `eldde.c`, `fieldpic.c`, `fltexp.c`, `ffread.c`,
`mathapi.c`, `token.c`, `sort.c` e `interp/elinit.c`. Todos esos sitios
llevaban escribiendo 256 bytes de estado de registros sobre punteros
ajenos. Conviene revisar si otros fallos "inexplicables" del port
desaparecen con esto antes de investigarlos por separado.

**Nota para quien siga:** `opus_original_startup_probe` (target
`EXCLUDE_FROM_ALL`, no entra en ctest) enlaza el mismo grafo y sigue
sin el shim. Si se revive, hay que añadirle
`port/original/opus_x64_setjmp.cpp` igual que a WORD1.

---

## Cómo retomar

Rama `fix/winelib-startup-blocked` (no está en `main`). Plan:
`docs/superpowers/plans/2026-08-15-terminar-winelib.md`. Ledger SDD:
`.superpowers/sdd/2026-08-15-terminar-winelib/progress.md`.

Build/test solo en debian13 contra `/home/pablo/build-debian13-verify`,
`DISPLAY=:59`. No usar el `--preset` del host.

Task 3 **cerrada** en Fix round 3: el AV era `_setjmp` con ABI
Microsoft-x64 pisando `*vhpllbs`. `opus_word1_about_test` pasa y la
etiqueta va 4/8. Tasks 1–2 hechas.

Siguiente:

- Tasks 4–5 sobre los 4 que aún fallan (`interaction`, `selection`,
  `font-typing`, `save-as`). Ya fallan rápido y con mensaje propio;
  empezar por leer ese mensaje, no por asumir el AV viejo.
- Aparte y sin relación con Task 3: `opus_x64_runtime_test` (gating)
  se cuelga sin imprimir nada, y `word1_port_smoke_test` no tiene
  `TIMEOUT` (`CMakeLists.txt:1580`). Detalle en Fix round 3.
