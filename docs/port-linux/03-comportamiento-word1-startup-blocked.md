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

La etiqueta `word1_startup_blocked` pasa de **0/9** a **5/9** (cifras
tras la revisión, ver "Cierre de revisión" abajo):

```
1/9 Test #10: word1_port_smoke_test ................   Passed    1.56 sec
2/9 Test #11: opus_word1_ui_test ...................   Passed    1.82 sec
3/9 Test #12: opus_word1_clipboard_shortcut_test ...   Passed    2.02 sec
4/9 Test #13: opus_word1_typing_test ...............   Passed   12.70 sec
5/9 Test #14: opus_word1_interaction_test ..........***Failed    2.52 sec
6/9 Test #15: opus_word1_selection_test ............***Failed    3.39 sec
7/9 Test #16: opus_word1_font_typing_test ..........***Failed    1.69 sec
8/9 Test #17: opus_word1_about_test ................   Passed    2.78 sec
9/9 Test #18: opus_word1_save_as_test ..............***Failed    7.83 sec
56% tests passed, 4 tests failed out of 9
```

Los 4 que siguen fallando ya no mueren por el AV: fallan rápido y con
mensaje propio. Son el material de las Tasks 4–5.

Gating: 8/8 verdes (`opus_original_strtbl_test`,
`opus_original_sttb_test`, `opus_original_plc_test`, `opus_sdm_cab_test`,
`opus_original_command_test`, `opus_shell_memory_foreign_test`,
`opus_shell_config_test`, `opus_shell_font_substitution_test`).

### Cierre de revisión (2026-08-15): las dos mitades del par, y el guard

La revisión levantó dos cosas importantes; ambas corregidas.

**1. `longjmp` seguía atado a glibc solo por suerte de link order.**
El fix original fijaba `_setjmp` por definición pero dejaba `longjmp`
a lo que decidiera el enlazador. No es una preocupación teórica: en
este contenedor hay **14 archivos de import de Wine** que definen
`longjmp`, `_setjmp` y `_setjmpex` — `libmsvcrt.a`, `libmsvcr70..120`,
`libucrtbase.a`, `libvcruntime140.a`, `libntdll.a`, `libntoskrnl.a`,
tanto en `x86_64-unix` como en `x86_64-windows`. `libntdll.a` lo enlaza
cualquier target winelib. Pasarle un `jmp_buf` de glibc al `longjmp`
Microsoft-x64 de Wine es la misma catástrofe silenciosa en la otra
dirección.

`opus_x64_setjmp.cpp` fija ahora también `longjmp`, como salto de cola
a `_longjmp@PLT`. `_longjmp` es un nombre que **ninguno** de esos 14
archivos define (verificado), así que es un destino seguro; y en glibc
`longjmp`, `_longjmp` y `siglongjmp` son alias débiles de un mismo
`__libc_siglongjmp` (misma dirección `0x3fab0` en `libc.so.6`), o sea
que el reenvío es exacto.

Comprobación en el binario:

```
                 U __sigsetjmp@GLIBC_2.2.5
                 U _longjmp@GLIBC_2.2.5
000000000008bd34 T _setjmp
000000000008bd3b T longjmp
```

**2. Guard en tiempo de build.** `src/cmake/AssertNoWineCrtSetjmp.cmake`
corre como `POST_BUILD` de WORD1 y falla el build si reaparece
cualquier thunk `__imp_` de la familia (`__imp__setjmp`,
`__imp__setjmpex`, `__imp_longjmp`, `__imp__longjmp`,
`__imp_siglongjmp`). Así, un cambio futuro de toolchain o de orden de
enlace sale como error de build y no como escritura salvaje durante
layout.

El guard está probado en los dos sentidos, no solo escrito:

```
NEGATIVO: binario fabricado con `void *__imp__setjmp = 0;`
  -> CMake Error ... imports the Microsoft-x64 CRT setjmp/longjmp
     family from Wine: __imp__setjmp        (rc=1)
POSITIVO: bin/WORD1.exe.so                  (rc=0)
CABLEADO: ninja -t commands WORD1 | grep AssertNoWineCrtSetjmp.cmake  -> presente
```

**3. `word1_port_smoke_test` ya tiene `TIMEOUT 20`**
(`CMakeLists.txt:1580`), igual que sus 8 hermanos. Le faltaba, y
mientras WORD1 moría solo eso no se notaba. Con el par setjmp/longjmp
completo el test además **pasa** (1.56 s), no solo deja de colgarse.

**Sigue pendiente, y no lo causa este fix:**
`opus_x64_runtime_test` (gating) **se cuelga** en el tip de la rama:
ejecutado directamente termina en `timeout 40` sin imprimir una sola
línea (`rc=124`). El binario de ayer (05:46, anterior a `5bebdd9` y a
este fix) se colgaba igual, no contiene ningún símbolo `setjmp`, y ni
`opus_x64_setjmp.cpp` ni el cambio del harness entran en ese target.
Reconstruirlo no lo arregla. Hay que investigarlo aparte.

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

## 4. File > New: verificación bloqueada por entorno (no por código)

**Estado:** sin determinar. Task 4 se lanzó para verificar si File > New
comparte la causa raíz de Task 3 (hipótesis fuerte: sí — `opus_word1_ui_test`
en modo base, el mismo test, ya había dado `Passed 1.82 s` dentro del run
completo de Fix round 3). El intento de verificación de esta sesión no
llegó a confirmarlo ni a refutarlo:

```
1/9 Test #10: word1_port_smoke_test .................   Passed    1.69 sec
2/9 Test #11: opus_word1_ui_test ....................***Failed    9.57 sec

WORD1 main window did not appear
(Wine CreateWindow error 1400: Invalid window handle)
```

El fallo ocurre **antes** de llegar al comando File > New (línea ~1965 del
arnés) — la segunda instancia de WORD1 nunca crea su ventana principal.
Diagnóstico del implementador: síntoma de `explorer.exe`/wineserver en
mal estado tras el primer proceso, no relacionado con el binario (símbolos
`_setjmp`/`longjmp` verificados correctos).

**Causa más probable, descubierta después de que Task 4 se cerrara BLOCKED:**
durante esta misma sesión había **otra sesión** trabajando en paralelo
sobre el mismo checkout y el mismo `~/build-debian13-verify` dentro de
debian13 (confirmado con procesos en vivo: un `cmake --build --target
WORD1` + `ctest -R opus_word1_ui_test` detached, `PPID 1`, que ninguno de
los agentes de esta sesión lanzó). Dos wineserver/Wine-prefix compartidos
recibiendo lanzamientos de WORD1 al mismo tiempo explica el síntoma
("Wine state corruption between test runs") mejor que una regresión de
código — y es coherente con que `opus_word1_ui_test` ya había pasado horas
antes, en la misma rama, sin cambios de código de por medio. **No
confirmado**, es la hipótesis más probable a re-verificar primero, en una
ventana donde el build dir no esté en uso por nadie más.

No hubo cambios de código ni commit para Task 4. Detalle completo:
`.superpowers/sdd/2026-08-15-terminar-winelib/task-4-report.md`.

---

## 5. Revisión independiente de Task 3 -- 4 hallazgos de fidelidad, corregidos y verificados en exia

**Contexto:** antes de confiar en Task 3, se corrió `/code-review` (nivel
`high`) contra los 9 commits de `fix/winelib-startup-blocked` sobre
`main`. Encontró 4 problemas, los 4 en el mismo tema: la rama arregla el
AV real de `_setjmp`, pero dos de sus workarounds de la era de
investigación (antes de encontrar la causa raíz) y dos gaps del camino
sin-`QGuiApplication` de `OpusShellFontMetrics` quedaban silenciando
datos incorrectos en vez de fallar visible -- exactamente lo que el
proyecto no se puede permitir dado el requisito duro de paginación
byte-idéntica.

**Los 4, con su causa raíz confirmada contra el código real (no solo el
diff):**

1. **`opus_original_startup_probe.cpp` -- `OpusPortGdiCharWidths`**
   dejaba `lfHeight=0` (tamaño por defecto de Wine, indefinido) cuando
   `hps<=0`, mientras `OpusShellFontMetrics` ya usaba 10pt
   (`PixelSizeFor`/`PointSizeFor`, hpsDefault) para la *misma* petición
   de fuente -- anchos y ascent/descent medidos a dos tamaños distintos.
   El comentario original decía que esto "recreaba" el `LOGFONT` que
   `C_FGraphicsFcidToPlf` real construye para `hps==0`; falso:
   `Opus/LOADFONT.C:880` tiene `Assert( fcid.hps > 0 )` -- el Word real
   nunca llega a esa función con `hps==0`. Se corrigió para usar el
   mismo default de 10pt.

2. **`opus_x64_layout.c` -- `OpusPlData`** clampeaba cualquier
   `brgfoo` fuera de `[cbPLBase, 4096]` a `cbPLBase` en silencio. Es un
   workaround de la sesión de investigación de Task 3 (mismo commit
   `5bebdd9` que originó el AV), de cuando la causa raíz todavía no se
   conocía. Con `_setjmp`/`longjmp` ya arreglados no debería dispararse
   más -- se dejó el clamp pero se le añadió un `fprintf(stderr, ...)`
   para que una recurrencia real sea visible.

3. **`opus_asm_resn2_pl.cpp` -- `HpInPl`** devolvía el puntero al
   elemento 0 (no `nullptr`) cuando `cb<=0` o `index<0` -- también del
   mismo commit `5bebdd9`. El original (`Opus/asm/resn2.asm:1340`) no
   tiene ningún fallback de release, solo un `Assert` de DEBUG; dar el
   elemento 0 como si fuera válido hace que cualquier llamador (todos
   tratan un retorno no-nulo como "índice válido") lea o escriba el
   slot equivocado sin poder distinguirlo de un acceso real. Se
   corrigió a `nullptr`.

4. **`OpusShellFontMetrics.cpp`** -- el fallback de tabla oráculo
   (usado cuando no hay `QGuiApplication`, el caso de arranque) ignoraba
   `key->catr` (negrita/cursiva): la tabla
   (`opus_shell_font_metrics_oracle_table.h`) solo tiene filas de peso
   regular (28 filas = 4 nombres de época × 7 tamaños, nunca se capturó
   negrita/cursiva), así que una petición en negrita/cursiva recibía en
   silencio las métricas de peso regular. El llamador real
   (`Opus/LOADFONT.C:442-448`) documenta explícitamente que `catr != 0`
   debe fallar controlado -- ese contrato ya se cumplía en el camino
   con `QGuiApplication`, pero no en este fallback. Se corrigió para
   que el fallback de oráculo (ascenso/descenso y, si el GDI de
   `OpusPortGdiCharWidths` también falla, anchos) se salte cuando
   `catr != 0`, en vez de responder con datos de peso equivocado.

**Verificado en exia (VPS, no el contenedor `debian13` de hp-15--
hp-15 no estaba disponible esta sesión; exia es Debian 13 trixie con
`wine`/`winegcc` igual de válido per `CLAUDE.md`):**

- `ninja`/`cmake --build` de `opus_original_engine` y `WORD1`: 0
  errores, mismos warnings preexistentes de siempre.
- `ctest` completo (sin `opus_x64_runtime_test`, ver nota abajo): igual
  que antes de estos 4 fixes -- sin regresión.
- **Gotcha real encontrado en esta verificación, no relacionado con el
  código:** la primera corrida de la etiqueta `word1_startup_blocked`
  dio 0/9 con `free(): corrupted unsorted chunks` y `unknown test mode`
  en todos -- el patrón de bug *original*, de antes de Tasks 1-2. Causa:
  `build/tests/Debug/opus_word1_ui_test.exe.so` tenía fecha del 12 de
  agosto -- de **antes** de que existieran los fixes de Tasks 1-3 en el
  árbol. `cmake --build --target WORD1` no reconstruye el arnés de
  test; hace falta `--target opus_word1_ui_test` aparte. Con el arnés
  reconstruido: **4/9** (`word1_port_smoke_test`, `opus_word1_ui_test`
  base, `opus_word1_clipboard_shortcut_test`, `opus_word1_about_test`),
  coincide con los mismos 4 wins ya documentados en §1-4 arriba (la
  quinta de "5/9" en `Cómo retomar` corresponde al mismo `ui_test` base
  contado una sola vez ahí). Los 5 fallos restantes
  (`--typing`, `--interaction`, `--selection`, `--font-typing`,
  Save As) son exactamente el alcance sin empezar de Tasks 5-10 --
  no regresiones de estos 4 fixes.
- **`Xvfb` real necesario:** sin `DISPLAY`, Wine cae a `nodrv` y todo
  falla con "Invalid window handle" antes de llegar a la lógica real.
  Este VPS ya tenía un `Xvfb :99` corriendo desde el 12 de agosto
  (otra sesión); se reusó en vez de levantar uno nuevo.
- `opus_x64_runtime_test` (gating) sigue colgándose sin imprimir nada
  -- confirmado también aquí, mismo síntoma que documentó la sesión de
  Task 3 en debian13. Sigue sin investigar, sigue sin relación con
  Task 3 ni con estos 4 fixes.

4 commits nuevos sobre `16145b6`: uno por hallazgo, mismo formato de
mensaje que el resto de la rama.

## 6. File > New: confirmado -- verify-only, cierra §4

**Task 4 retomada 2026-08-19 en exia.** El plan (Task 4, Step 1) pedía
correr `opus_word1_ui_test` (modo base) aislado: si pasa sin ningún
cambio de código, Task 4 es verify-only.

```
ctest -R "^opus_word1_ui_test$" --output-on-failure
    Start 11: opus_word1_ui_test
1/1 Test #11: opus_word1_ui_test ...............   Passed    3.63 sec
100% tests passed, 0 tests failed out of 1
```

El modo base del arnés (`opus_word1_ui_test.cpp:1964-1995`) manda
`WM_COMMAND`/`kFileNew` (id 1813), confirma que el diálogo File New
aparece, que sus controles coinciden con el contrato SDM, lo acepta,
espera a que cierre y confirma que `Document2` se creó -- exactamente
el flujo que §4 dejó sin determinar. Pasa limpio, sin ningún cambio de
código en esta sesión.

**Confirma la hipótesis de §4** ("la causa más probable... contención
del build dir compartido, no una regresión de código", no confirmada
en su momento): mismo root cause que About (Task 3, `_setjmp`/`longjmp`
ABI), sin bug independiente de File > New. La corrida de §4 falló por
el entorno compartido de debian13/hp-15 en esa sesión, no por el
código -- aquí, en exia, sin contención, pasa a la primera.

Sin cambios de código para esta sección -- commit de documentación
solamente.

## 7. Save As: "File Save As dialog did not cancel cleanly" -- causa raíz independiente, arreglada

**Task 5, 2026-08-19 en exia.** A diferencia de Task 4, esta **no**
comparte causa raíz con Task 3 -- verify-only dio un fallo real, con
mensaje propio:

```
ctest -R "^opus_word1_save_as_test$" --output-on-failure
    File Save As dialog did not cancel cleanly
```

**Causa raíz, confirmada leyendo el código real (no solo el síntoma):**
`create_dialog_host` (`opus_sdm_runtime.cpp:336`) crea, para
`kIddOpen`/`kIddSaveAs`, una ventana `WS_POPUP` de clase
`"OpusSdmDialog"` **sin `WS_VISIBLE`** -- un señuelo que
`materialize_save_as_template` puebla con controles reales (incluido
el botón Cancel, id 2). Pero `TmcDoDlgDli` (`opus_sdm_runtime.cpp:2522`)
nunca usa esa ventana para Open/Save As: para esos dos `hid` llama
directo a `run_word95_common_file_dialog`, que bloquea el hilo dentro
de `GetSaveFileNameA`/`GetOpenFileNameA` -- el diálogo real y visible
es una ventana completamente distinta (`#32770`, el común de Windows/
Wine), no el señuelo.

El arnés de test (razonablemente, seguiendo la misma convención de
clase que usan *todos los demás* diálogos SDM) busca `"OpusSdmDialog"`
+ `"Save As"` -- encuentra el señuelo (existe, aunque oculto:
`EnumWindows` no filtra por visibilidad y nada en `find_window_callback`
lo hace tampoco), le manda `WM_COMMAND` id=2 al botón Cancel del
señuelo. `handle_dialog_command` lo recibe (la bomba de mensajes de
`GetSaveFileNameA` despacha *todos* los mensajes del hilo, no solo los
de su propia ventana) y llama `finish_native_dialog`, que marca
`dialog.dying=true` y hace `ShowWindow(SW_HIDE)` -- pero nada de eso
llega al diálogo real, que sigue bloqueado esperando su propio Cancel.
`wait_for_window_to_close` espera a que la ventana señuelo *desaparezca*
(`find_process_window` devuelve null), pero `DestroyWindow` del señuelo
solo ocurre en `TmcDoDlgDli` **después** de que
`run_word95_common_file_dialog` retorne -- que nunca pasa. Timeout a
los 5000 ms.

No es un bug compartido con Task 3, y no es un bug del arnés tampoco
(apuntar al señuelo es lo correcto dado que ningún otro diálogo de este
código tiene esta arquitectura de dos ventanas) -- es un hueco real:
nada conecta el señuelo con el diálogo real que reemplaza.

**Fix:** un hook `OFN_ENABLEHOOK`/`lpfnHook` en el `OPENFILENAMEA` de
`run_word95_common_file_dialog` captura el HWND real del diálogo
(`GetParent()` del hook, el patrón documentado para diálogos
`OFN_EXPLORER`) en `WM_INITDIALOG`, guardado en un global
(`g_active_win95_file_dialog`, limpiado apenas retorna la llamada
bloqueante). `handle_dialog_command`, para `kIddOpen`/`kIddSaveAs` con
`tmc == kTmcOk || tmc == kTmcCancel`, ahora reenvía el clic del señuelo
al diálogo real (`PostMessageA(..., WM_COMMAND, MAKEWPARAM(tmc,
BN_CLICKED), 0)` -- `IDOK`/`IDCANCEL` coinciden con `kTmcOk`/`kTmcCancel`
por la misma convención de Windows que ya usa este archivo) en vez de
llamar `finish_native_dialog` directo -- así es
`run_word95_common_file_dialog` el que termina el diálogo exactamente
una vez, igual que si un usuario real hubiera hecho clic en la ventana
visible.

**Verificado:**
```
ctest -R "^opus_word1_save_as_test$" --output-on-failure
    Passed    4.79 sec

ctest -L word1_startup_blocked --output-on-failure
    5/9 (antes 4/9) -- Save As nuevo, sin regresión en los demás
```

Archivos: `src/port/original/opus_sdm_runtime.cpp` (global +
hook + wiring `lpfnHook`/`OFN_ENABLEHOOK` + reenvío en
`handle_dialog_command`).

## 8. `--font-typing`: dos bugs reales encontrados y arreglados, uno localizado y sin cerrar

**Task 6, 2026-08-19 en exia.** El plan (Step 1) anticipaba que este test
necesitaba un display real, no Xvfb, para la parte visual ("black popup").
Esta sesión avanzó sin eso -- el fallo real resultó ser de datos y de
lógica de foco, no de render -- pero **no cierra** Task 6 del todo: un
tercer problema queda localizado sin arreglar.

**Bug 1 -- nombres de fuente Windows nunca enumerables (arreglado):**
`installed_windows_fonts()` (opus_sdm_runtime.cpp) enumera con
`EnumFontFamiliesExA` -- nombres de familia reales, no alias de Windows.
El test buscaba literales `"Courier New"`/`"Arial"` con
`CB_FINDSTRINGEXACT`, que nunca aparecen en una pila de fuentes Linux.
Confirmado con una sonda standalone (`EnumFontFamiliesExA` vía winegcc)
en dos entornos independientes: exia enumera FreeMono, FreeSans,
FreeSerif, la familia Liberation, Noto, Unifont, WenQuanYi e IPA; una
sesión anterior en debian13 vio la familia Liberation, DejaVu, Tahoma,
MS Sans Serif, Symbol y Wingdings -- cero nombres de alias Windows en
ninguno de los dos. `installed_windows_fonts()` está bien: es fiel al
Word real, que tampoco hardcodeaba nombres, enumeraba lo instalado.
Arreglo: los literales del test pasan a `"Liberation Sans"`/
`"Liberation Mono"` (los únicos dos nombres que ambos entornos
comparten; `fonts-liberation` es paquete base común en Debian). Sin
dependencia nueva -- se consideró instalar fuentes MS reales via
`winetricks corefonts` y se descartó (licenciamiento, no está en
ningún otro sitio de este port).

**Bug 2 -- `union FCID` de 8 bytes en Linux, no 4 (arreglado, LP64 real):**
Con el Bug 1 arreglado el test llegó a un segundo fallo, uno que ya
existía pero nunca se había alcanzado: `"the packed font identifier is
not 32 bits"`. Causa: `Opus/fontwin.h`, `union FCID` tiene
`long lFcid;` -- en Win16/Win32/Win64 `long` siempre fue 4 bytes (Win64
mantiene el modelo LLP64), pero en Linux x86-64 (LP64) `long` es 8
bytes. Los otros dos miembros del union (WORD+WORD, y la estructura de
bitfields `unsigned int`) ya eran de 4 bytes en todas las plataformas
-- solo `long lFcid` duplicaba el tamaño del union completo aquí. Fix
guardado en `Opus/fontwin.h` (`#if defined(__GNUC__) && !defined(_MSC_VER)`,
mismo patrón que el guard de `wordwin.h`/`splshare.h`): `int lFcid`
bajo GCC/Linux, `long lFcid` sin cambios bajo MSVC. Verificado que
`.lFcid` solo se usa como asignación de patrón de bits completo
(`= fcidNil`, `= 0L`, `= pchr->l`) en los 3 sitios de uso reales -- nunca
se compara como valor ancho, así que el cambio de tipo es seguro.

**Bug 3 -- el foco no vuelve al panel del documento tras elegir fuente
del ribbon (localizado, NO arreglado -- toca `Opus/` restringido):**
Con los Bugs 1-2 arreglados, el test abre el combo (el clic simulado
por sí solo no lo hacía -- se añadió un `CB_SHOWDROPDOWN` explícito
tras el clic, que sí lo abre) y elige el ítem, pero
`wait_for_focus(pane, 1500ms)` nunca ve el foco Win32 real
(`GetGUIThreadInfo().hwndFocus`) volver al panel `OpusWwd`.

El trace ya existente `OpusX64TraceRibbon` (activo siempre, escribe a
`build/WORD1-ribbon.txt` -- no necesita instrumentación nueva) muestra
la cadena completa corriendo sin errores lógicos: `CBN_SELENDOK` →
`commit_ribbon_list_selection` → `kDlmKillItemFocus` (ok) →
`kDlmKillDialogFocus` (ok, aplica la fuente: `original-applied ...
tmc=5`) → `kDlmDialogClick`. Esta última invoca `FDlgIb` (el handler
original, `Opus/iconbar1.c:412`, caso `dlmDlgClick`) con el comentario
explícito "SDM gets the focus, send it back to the pane" y una llamada
real `SetFocus(hwwdCur == hNil ? NULL : (*hwwdCur)->hwnd)` -- pero el
trace confirma que devuelve `fFalse` (normal para este caso, no es un
error: `dlmDlgDblClick` arriba también retorna `fFalse` siempre). El
`bool focus_result` de `commit_ribbon_list_selection` solo alimenta el
trace, no cambia ningún flujo -- así que el `SetFocus` real sí debería
ejecutarse (`vidf.fIBDlgMode` ya está en `fFalse` para ese punto, puesto
por `dlmKillDlgFocus` un paso antes, así que la rama `if
(!vidf.fIBDlgMode)` sí entra). Por qué el foco Win32 real no se queda
en `pane` después de esa llamada -- no investigado más allá de esto:
candidatos sin descartar son que `hwwdCur` no apunte al mismo HWND que
`pane`, o que algo dentro del cierre del combo de Wine (limpieza
interna tras `CB_SHOWDROPDOWN`+clic simulado) le devuelva el foco a sí
mismo justo después.

Esto toca `Opus/iconbar1.c` (árbol restringido) -- necesita autorización
explícita antes de tocar código ahí, per `CLAUDE.md`. Diagnóstico
completo, sin cambio de código en `Opus/` esta sesión.

**Actualización 2026-08-20 (exia): hipótesis `CBRollUp` descartada, foco
nunca sale del botón "OK".** Se añadió `wait_for_focus_traced` (variante
de `wait_for_focus` que loguea cada `hwndFocus` distinto visto, con clase
y caption) para probar la hipótesis de arriba -- que Wine's `CBRollUp()`
(dlls/user32/combo.c) corre la cadena SDM completa (incluido el
`SetFocus(pane)` real de `FDlgIb`) y solo *después* oculta el popup listbox,
robando el foco como efecto secundario. La traza la contradice: el foco no
llega a `pane` ni transitoriamente -- se queda fijo en una ventana
`class=Button caption='OK'` (hwnd distinto cada corrida, ~0x1011x) desde
el primer poll (`t+0`/`t+1ms`) hasta el timeout de 1500ms, sin un solo
cambio intermedio. Eso descarta la hipótesis original: no es que el foco
llegue y se vaya, es que nunca se mueve de ese botón "OK" en absoluto --
el `SetFocus(hwwdCur->hwnd)` de `FDlgIb` o no se ejecuta, o se ejecuta y
es inmediatamente revertido a ese botón por algo que corre después (o el
propio `hwwdCur` no apunta al pane esperado). Candidato más probable:
ese "OK" es el botón por defecto de algún diálogo/dialog-manager SDM que
sigue vivo (o se recrea) en ese hilo de mensajes; no identificado más
allá de clase+caption esta sesión. Sigue sin arreglar, sigue tocando
`Opus/iconbar1.c` (restringido) -- este hallazgo solo corrige el
diagnóstico previo, no lo cierra.

**Segunda actualización 2026-08-20 (exia): la hipótesis `CBRollUp` tenía
razón después de todo -- cerrado, arreglado en `opus_sdm_runtime.cpp`, no
en `Opus/iconbar1.c`.** El párrafo anterior se equivocó de granularidad de
traza. `wait_for_focus_traced` mide desde un *proceso externo* con poll de
10ms -- demasiado grueso para ver una ventana de foco que dura microsegundos
dentro del mismo hilo. Se instrumentó en cambio `FDlgIb` mismo
(`Opus/iconbar1.c`, autorizado para esta sesión): dos llamadas a
`OpusX64TraceRibbon` alrededor del `SetFocus(hwwdCur->hwnd)` real,
`dlgclick-before`/`dlgclick-after`, leyendo `GetFocus()` de forma síncrona
en el mismo hilo. Resultado, en `build/WORD1-ribbon.txt`:

```
commit-end   msg=14 ...
dlgclick-before msg=18 tmc=0 a=<hwwdCur> b=65770 sel=65750,0 ins=0
dlgclick-after  msg=18 tmc=0 a=65770 b=0 sel=0,0 ins=0
```

`b=65770` = `0x100EA` = `pane`. **`dlgclick-after` confirma que
`SetFocus(hwwdCur->hwnd)` sí funciona** -- `GetFocus()` es `pane` en el
instante en que `dlmDlgClick` retorna. `Opus/iconbar1.c` está limpio, no
tiene ningún bug: hace exactamente lo que el Word 1.1a original hacía.
Algo *después* de que toda la cadena SDM termina (`commit-end` es el
último evento de esta traza) deshace ese foco antes de que
`wait_for_focus_traced` (10ms más tarde) llegue a verlo -- exactamente la
hipótesis original de `CBRollUp()`: Wine oculta el popup del listbox
*después* de que `CBN_SELENDOK` retorna, y ocultar una ventana con foco
reasigna el foco como efecto secundario.

Arreglo real, en la capa de puerto sin restringir
(`src/port/original/opus_sdm_runtime.cpp`, no `Opus/`): `commit_ribbon_
list_selection` ya reenviaba `CBN_SELENDOK` a través de un mensaje
pospuesto propio (`kWmCommitRibbonSelection`) para esquivar el cierre no
confiable del combo nativo. Se agregó un segundo mensaje pospuesto,
`kWmReassertPaneFocus`, que repite exactamente la misma llamada que ya
funciona (`invoke_dialog_proc(dialog, kDlmDialogClick, tmc)`, el mismo
`FDlgIb` de siempre) -- primero vía `PostMessageW` inmediato (insuficiente:
la traza mostró que ese reintento también aterriza en `pane` y también se
deshace después), luego vía `SetTimer(..., 50)` de un solo disparo
(`WM_TIMER` → mata el timer → repite la llamada). El segundo intento, con
50ms reales de por medio para que la cola de mensajes drene lo que sea que
esté robando el foco, **se queda**. Verificado con la misma traza síncrona:
`reassert-focus ... b=65770` (pane), y esta vez `wait_for_focus_traced`
(el poll externo de 10ms) también lo confirma:

```
[focus-trace] t+1ms hwndFocus=0x100ea class=OpusWwd caption='' (== pane)
```

Task 6 Bug 3 **cerrado**. El test avanza más allá del chequeo de foco, a
un fallo distinto y nuevo: `"newly typed text did not retain the ribbon
font"` -- el texto recién tecleado no conserva la fuente elegida en el
ribbon. No es una regresión (antes el test nunca llegaba tan lejos); es
un cuarto bug real, sin investigar todavía. Etiqueta
`word1_startup_blocked` sigue en 7/9 (mismos dos conocidos: este nuevo
fallo de fuente en `--font-typing`, y `--interaction` sin cambios), pero
el bug real detrás de uno de los dos cambió.

**Verificado:**
```
DISPLAY=:99 ctest -R "^opus_word1_font_typing_test$" --output-on-failure
    [focus-trace] t+1ms hwndFocus=0x100ea class=OpusWwd caption='' (== pane)
    font properties=20,0 applied=3,48 inserted=20,0 lineHeight=16
        formatted=16 formatter=0,16
    newly typed text did not retain the ribbon font

DISPLAY=:99 ctest -L word1_startup_blocked --output-on-failure
    7/9 -- sin regresión en los demás
```

Archivos: `Opus/iconbar1.c` (traza `dlgclick-before`/`dlgclick-after`,
autorizado), `src/port/original/opus_sdm_runtime.cpp` (el arreglo real:
`kWmReassertPaneFocus`, `kReassertPaneFocusTimerId`,
`DialogState::pending_reassert_tmc`).

**Verificado:**
```
DISPLAY=:99 ctest -R "^opus_word1_font_typing_test$" --output-on-failure
    font combo select stages: foreground=1 chose_item=1 regained_focus=0
    font typing test could not mouse-select the font
    (avanzó de fail(47) "could not find controls" -> fail(49) "could
    not mouse-select the font", con fallo 47 intermedio de "not 32
    bits" ya cerrado en el camino)

DISPLAY=:99 ctest -L word1_startup_blocked --output-on-failure
    5/9 -- sin regresión en los demás
```

Archivos: `src/port/original/opus_word1_ui_test.cpp` (literales de
fuente, `CB_SHOWDROPDOWN`, diagnóstico de las 3 etapas del clic de
combo), `Opus/fontwin.h` (guard LP64 de `union FCID`).

**Tercera actualización 2026-08-20 (exia): "no conserva la fuente" era
diagnóstico engañoso -- el texto nunca se tecleó. Causa raíz real
encontrada: `idle.c:503` precarga la fuente con `selCur.chp.hps == 0`,
`CreateFontIndirect` falla, y el diálogo real de error resultante se
traga el teclado. Sesión cerrada sin arreglo -- se retoma en Debian 13
(LXQt).**

El "cuarto bug" del párrafo anterior resultó ser una lectura
equivocada. Con foco confirmado correcto en `pane`
(`[pre-type] hwndFocus=... (== pane)=1`), se trazó `FIsKeyMessage`
(`Opus/wproc.c:2450`) y el `PeekMessage` del loop principal
(`OpusOriginalWinMain`, `Opus/wproc.c:~556`): **cero** mensajes
`WM_CHAR`/`WM_KEYDOWN` llegan a ninguno de los dos durante todo
`--font-typing`, pese a que `send_physical_text` (SendInput real, no
`PostMessageW`) reporta éxito. Prueba diferencial decisiva: el mismo
trace, corrido contra `opus_word1_typing_test` (tecleo simple, sin
ribbon), sí ve cada tecla (`iskeymsg`/`mainloop-msg` para cada
`WM_KEYDOWN`/`WM_KEYUP`/`WM_CHAR`, valores `tmc` deletreando
"ORIGINAL"). La app permanece "responsive"
(`window_is_responsive`/`WM_NULL` responde) durante toda la falla --
no es un cuelgue real.

**Teorías de foco descartadas, todas verificadas empíricamente, ninguna
cambió el síntoma un solo bit (`applied=3,48 inserted=20,0` idéntico
en cada intento):**

- `SetTimer`-reassert de Task 6 Bug 3 (arriba) desactivado
  temporalmente -- el test simplemente vuelve a fallar el chequeo de
  foco original (`regained_focus=0`), sin siquiera llegar al tecleo:
  confirma que ese fix sigue siendo necesario, pero no es la causa de
  esto.
- `SetForegroundWindow(GetAncestor(pane, GA_ROOT))` agregado junto al
  reassert de foco (`opus_sdm_runtime.cpp`, diagnóstico, sigue en el
  árbol sin commitear) -- `foreground_result=1`, `root` resuelve
  correctamente a `main_window`. Sin cambio.
- Cursor real (`SetCursorPos`) movido al centro de `pane` justo antes
  de teclear -- por si este Xvfb (sin gestor de ventanas) enrutara
  input real por posición del puntero (`XGetInputFocus` devuelve
  `PointerRoot` fijo durante todo el test, confirmado con una sonda
  standalone en C/Xlib). Sin cambio.
- `make_foreground_and_focus(main_window, pane, thread_id)` (el mismo
  helper que sí usa con éxito el bloque `caret_mode`, con su
  `AttachThreadInput` cruzado entre el proceso de test y el hilo de
  WORD1) llamado explícitamente antes de `send_physical_text`. Sin
  cambio -- y de paso se confirmó que `caret_mode`/`--caret` no está
  registrado como ctest (`src/CMakeLists.txt` solo registra 8 modos),
  así que ese patrón nunca estuvo realmente probado en este entorno,
  no era la referencia sólida que parecía.
- `IsWindowEnabled`, `GetActiveWindow`, captura de mouse
  (`GUITHREADINFO.hwndCapture`) -- todos correctos (`paneEnabled=1
  mainEnabled=1 active=main_window capture=0`).

**Causa real: `EnumThreadWindows` sobre el hilo de WORD1 en el momento
`[pre-type]` muestra una ventana visible extra, clase `#32770`
(diálogo estándar de Windows), título `"Microsoft Word"`, con hijos
`Static id=65535 text='Low memory: cannot display requested font'` +
`Button id=1 text='OK'`.** Ese diálogo real -- no ficticio, no un
efecto de foco -- corre su propio loop de mensajes modal desde que se
crea; por eso ni `FIsKeyMessage` ni el `PeekMessage` del loop principal
ven un solo mensaje después: el hilo está parado dentro del loop del
diálogo, no en el de Opus. Explica también por qué la app sigue
"responsive" (el loop del diálogo también atiende `WM_NULL`) y por qué
ningún arreglo de foco cambió nada -- el foco nunca fue el problema.

Rastreado hasta el origen exacto (2 puntos de traza nuevos en
`Opus/LOADFONT.C`, autorizados como continuación de esta misma
investigación):

```
idle.c:503   LoadFont(&selCur.chp, fFalse)   /* "preload new font in
                                                 Idle, avoid delay
                                                 when typing commences" */
  -> C_LoadFcid -> FGraphicsFcidToPlf:
       lf.lfHeight = NMultDiv(fcid.hps * (czaPoint/2), vfli.dysInch, czaInch)
       trace: fcid.hps=0  vfli.dysInch=96 (DPI normal, no es la causa)
       lf.lfHeight=0
  -> CreateFontIndirect(&lf) devuelve NULL (GetLastError=5,
     ERROR_ACCESS_DENIED)
  -> SetErrorMat(matFont)  (LOADFONT.C:330, camino LSystemFontErr)
  -> idle.c / ReportPendingAlerts(): case matFont ->
     ErrorEid(eidCantRealizeFont, ...) -> el MessageBox real de arriba
```

`selCur.chp.hps` **debería** ser 48 (24pt, la talla recién elegida en
el ribbon -- confirmado por separado con
`SendMessageW(pane, kWmOpusX64QuerySelection, 50, 0)` justo antes de
teclear) pero en el momento en que corre el preload de `idle.c:503`
lee `0`. **No cerrado -- pendiente identificar la carrera exacta.**
`vrf.fPreloadSelFont` se marca `fTrue` en dos sitios
(`Opus/cmdcore.c:510`, `Opus/dlglook1.c:781`) y se consume una sola vez
por tick de idle (`Opus/idle.c:488-505`, usa `selCur.chp` tal cual,
sin resolver `hps==0` a un valor real primero). Candidato más probable
sin confirmar: el preload dispara y consume la marca justo después de
elegir la FUENTE (ribbon combo 1), con `selCur.chp.hps` todavía en su
valor original de documento (0, ver `initial_hps=0` en el trace de
arriba) -- antes de que la selección de TALLA (ribbon combo 2) llegue
a escribir 48 ahí. Verificar: en qué tick exacto de idle corre esto
respecto a los dos `combo-select` del trace de ribbon, y si `hps==0`
es en sí un estado legítimo (placeholder "heredar de estilo") que
otros caminos resuelven antes de tocar GDI y este no.

**Diagnóstico dejado en el árbol, sin commitear** (continuidad para la
sesión en Debian 13/LXQt -- todo bajo `#ifdef OPUS_X64`, guardado,
no afecta MSVC):

- `Opus/LOADFONT.C` -- traza `lfheight-calc` (entrada a
  `FGraphicsFcidToPlf`, imprime `fcid.hps`/`vfli.dysInch`/
  `lf.lfHeight`), `matfont-set` (fallo de `CreateFontIndirect`),
  `screenfail` (fallo de `OurSelectObject` en el DC de pantalla --
  no se disparó esta sesión, el fallo real fue siempre
  `CreateFontIndirect`).
- `Opus/wproc.c` -- traza en `FIsKeyMessage` (entrada,
  `WM_CHAR`/`WM_KEYDOWN`) y en el `PeekMessage` del loop principal de
  `OpusOriginalWinMain` (mismo filtro).
- `Opus/iconbar3.c` -- traza de entrada/salida de `IBDlgLoop()`
  (descartó la hipótesis de que el loop se quedaba atascado ahí).
- `Opus/rulerdrw.c` -- traza alrededor de `FGetCharState` en
  `UpdateRibbon` (descartó que ahí se corrompiera `selCur.chp`).
- `Opus/wordtech/insert.c` -- traza en `InsertLoopCh` justo después de
  `GetSelCurChp` (cero hits durante `--font-typing`, confirmando que
  esa rutina nunca corre -- consistente con el diálogo bloqueante).
- `src/port/original/opus_sdm_runtime.cpp` -- `SetForegroundWindow`
  especulativo junto al reassert de foco de Task 6 (no ayudó, inerte,
  se puede revertir cuando se retome).
- `src/port/original/opus_word1_ui_test.cpp` -- diagnóstico
  `[pre-type]` (foco/enabled/active/capture), `[enum-window]`/
  `[dialog-child]` (el que encontró el diálogo real), llamada a
  `make_foreground_and_focus` antes de teclear (no ayudó, inerte).

Nada de esto se commiteó esta sesión -- queda como `M` en
`git status` en `src/Opus/{LOADFONT.C,iconbar3.c,rulerdrw.c,wproc.c,
wordtech/insert.c}` y `src/port/original/{opus_sdm_runtime.cpp,
opus_word1_ui_test.cpp}`. `git status` también muestra `M` en
`Opus/{ddeclnt.c,etcmd.c,filecvt.c,raremsg.c,spelcore.c}` -- eso es
contenido de una migración `OpusMem*` de otra sesión en paralelo (mas
un fix mío de "takeover" sobre una etiqueta `LError` colgante en
`etcmd.c`/`spelcore.c` que dejaba esos dos archivos sin compilar); no
tocar esos cinco al continuar salvo que sea la misma sesión que los
dejó así.

**Próximo paso concreto al retomar:** trazar el orden exacto entre
(a) el momento en que `Opus/dlglook1.c:781`/`Opus/cmdcore.c:510` ponen
`vrf.fPreloadSelFont = fTrue` tras cada selección de ribbon, y (b) el
tick de idle que lo consume en `Opus/idle.c:503` -- lo más probable es
que el preload dispare tras la selección de FUENTE, antes de que la de
TALLA llegue a escribir `selCur.chp.hps`, y que el arreglo real sea
usar un hps ya resuelto (o posponer el preload hasta que ambas
selecciones se hayan aplicado) en vez de tocar `Opus/idle.c` a ciegas.
Una vez cerrado esto, falta además verificar que el nombre visible del
diálogo `"Low memory: cannot display requested font"` no es en sí un
mensaje legítimo de Word 1.1a bajo otras circunstancias (`eidCantRealizeFont`,
`Opus/wordtech/error.c:933`) -- aquí es un falso positivo de una
llamada de precarga con datos a medio actualizar, no una condición
real de memoria.

**Cuarta actualización 2026-08-20 (debian-VM, rama
`investigate/font-typing-idle-preload`): la teoría de la carrera `hps`
queda descartada con evidencia directa -- cinco hipótesis probadas y
descartadas, causa raíz real aún sin localizar.** Esta sesión tenía
como objetivo *confirmar* la teoría de la "Tercera actualización" de
arriba y arreglarla en `idle.c`. El arreglo propuesto (`&&
selCur.chp.hps != 0` en el guard de `idle.c:503`) se implementó,
compiló y se corrió contra el test real -- y no cambió el síntoma ni
un bit. Investigar por qué llevó a descartar la teoría entera con
trazas directas, no lectura de código.

**Entorno de esta sesión:** máquina nueva (`debian-VM`, Debian 13
trixie, KVM), sin toolchain previo -- se instaló `cmake`/`ninja`/
`wine`(10.0~repack-6)/`wine64-tools`/`qt6-base-dev`/`Xvfb` vía `apt`, se
inicializó el prefix de Wine (`wineboot --init`), y se resolvió el
bloqueo de Configure documentado en el README (`src/port/tools/host/`
gitignored) con un `CMakeLists.txt` ya preparado para esa carpeta.
Build limpio de `opus_original_engine`/`WORD1`/`opus_word1_ui_test`,
0 errores. Repro estable de `opus_word1_font_typing_test ***Failed`
igual que en exia.

**Hipótesis 1 -- carrera `hps` en `idle.c:503` (la de la sesión
anterior): descartada.** Se instrumentó un trace nuevo
(`idle-preload-check`) justo al entrar al bloque de preload de
`Opus/idle.c:488`, antes de cualquier guard. **Cero apariciones en
todo el test.** El bloque de preload de `idle.c` nunca se alcanza
durante `--font-typing` -- ni con el guard puesto ni sin él. La
atribución de la sesión anterior a `idle.c:503` nunca se trazó
directamente; se infirió leyendo código (`vrf.fPreloadSelFont` /
`DoLooks`). El guard con `hps != 0` se implementó, compiló, corrió, y
el diálogo de error siguió apareciendo idéntico.

**La llamada real que falla:** instrumentando el return address de
`C_LoadFont` (`__builtin_return_address(0)`) se confirmó que la
llamada que sí truena viene de otro lado, no de `LoadFont(pchp,
fFalse)`. El sitio real, confirmado por `nm`/lectura de código, es
`Opus/disp1.c`'s `LoadFcidFull(pchr->fcid)` -- llamado una vez por
carácter durante el repintado del panel del documento (dos sitios,
disp1.c:832 y disp1.c:1698), usando el `fcid` cacheado en cada
`CHR`, no `selCur.chp`. Con el documento vacío en `[pre-type]`, esto
repinta el carácter de fin de párrafo con el `fcid` que sea que tenga
en ese momento.

**Hipótesis 2 -- `hps==0` es la causa: descartada con datos, no
lectura.** Se añadió un trace (`fcid-identity`) al entrar a
`C_FGraphicsFcidToPlf` (`Opus/LOADFONT.C`), antes del `Assert(fcid.hps
> 0)`, mostrando `ibstFont`/`wProps`/`hps`/`kul` en cada llamada,
éxito o fallo:

```
fcid-identity msg=2(ibstFont) hps=0   -> "Helv"             -> OK
fcid-identity msg=4(ibstFont) hps=0   -> "Liberation Sans"  -> matfont-set (falla)
```

`hps` es **idéntico** (0) en ambos casos -- uno pasa, el otro falla.
El único que cambia es qué fuente (`ibstFont`) se pide. `hps==0` no es
la causa; es incidental.

**Hipótesis 3 -- charset corrupto (`lfCharSet=255`, `OEM_CHARSET`):
real pero insuficiente por sí sola.** Añadiendo el nombre real de la
fuente al trace (pasando `pffn->szFfn` como `stage` de
`OpusX64TraceRibbon`), se confirmó `lfCharSet=255` para "Liberation
Sans" contra `lfCharSet=0` para "Helv". Sondas standalone
(`EnumFontFamiliesExA` y el `EnumFontsA` legacy que usa
`Opus/SYSCHG.C:FontNameEnum`, ambas contra un DC de pantalla) **nunca**
devuelven 255 para esa fuente -- los valores reales enumerados son 0,
238, 204, 161, 162, 177, 186, 163. `FontNameEnum` copia fielmente
`lplf->lfCharSet` (línea ~775: `ChsPffn(pffn) = lplf->lfCharSet;`),
así que el 255 no es un bug de copia -- viene de que `FillHsttbPaf`
enumera contra `vpri.hdc` (el DC de **impresora**, no pantalla), y
este prefix de Wine no tenía ninguna impresora configurada
(`wine control printers` vacío, `lpstat` "No se han añadido
destinos.", `GetDefaultPrinterA` devolvía error 2).

Arreglo aplicado en `Opus/LOADFONT.C` (`C_FGraphicsFcidToPlf`, antes
de `bltbyte(...lfFaceName...)`): si `plf->lfCharSet == OEM_CHARSET`,
usar `DEFAULT_CHARSET` en su lugar (guardado bajo `OPUS_X64`). Este
fix es correcto y se queda -- `OEM_CHARSET` sin impresora real es un
valor sin sentido, no algo que Opus debería propagar a
`CreateFontIndirect` -- pero **no arregla el test solo**: con el
charset ya saneado (confirmado por trace, `lfCharSet=1`),
`CreateFontIndirect` **sigue fallando** para "Liberation Sans".

**Efecto secundario explorado: agregar una impresora real cambia el
síntoma pero no lo arregla.** Se configuró una cola CUPS dummy
(`GenericPS`, PPD genérico PostScript, `device-uri file:///dev/null`)
para darle a Wine una impresora real -- confirmado con una sonda
standalone (`GetDefaultPrinterA`/`EnumPrintersA`) que Wine la veía en
vivo. Con impresora presente, `vfli.fFormatAsPrint` activa el camino
de dos dispositivos en `C_LoadFont` (`LOADFONT.C:121-135`): una
petición de fuente de impresora *además* de la de pantalla. El
charset de pantalla salió limpio (163, un valor real de la
enumeración), pero la petición del **lado impresora** trajo su propio
dato corrupto (`lfHeight=-5`, `fcid.wProps=1920` -- un valor de
bitfield sin sentido) y también falló. Esto es un bug distinto,
dormido, en un subsistema distinto (formato para impresión, no
tecleo/ribbon) -- fuera de alcance de este ítem. **La impresora
`GenericPS` se quitó** (`sudo lpadmin -x GenericPS`, confirmado con la
misma sonda que Wine ya no ve ninguna impresora) -- no queda cambio de
entorno persistente. Sin la impresora, el camino vuelve a ser de un
solo dispositivo (pantalla), el mismo que protege el fix de charset de
arriba.

**Hipótesis 4 -- caché de fuentes (`PfceLruGet`, desalojo de slot LRU):
descartada.** `ifceMax` = 32 (`Opus/fontwin.h:11`) -- nada cerca de
agotarse con 2 fuentes distintas pedidas. Se instrumentaron ambos
puntos de retorno de `PfceLruGet` (`Opus/LOADFONT.C:1037`): slot libre
encontrado sin desalojo (`pfce-free-slot`) vs. desalojo de LRU
(`pfce-evicted-slot`). Resultado: "Helv" recibe el slot 0 libre;
"Liberation Sans" recibe el slot 1 libre. **Ningún desalojo ocurre.**
La caché está sana.

**Hipótesis 5 -- `GetLastError()` real (183, `ERROR_ALREADY_EXISTS`):
descartada -- era ruido, no señal.** El `183` apareció idéntico en
tres estados de código/entorno distintos a lo largo de la sesión, lo
cual parecía significativo -- pero GDI no garantiza fijar un error
fresco en cada llamada. Se añadió `SetLastError(0)` justo antes del
`CreateFontIndirect` que falla (`Opus/LOADFONT.C:333`). Resultado:
`GetLastError()` da **0** después de la falla. El `183` que se venía
seteando era remanente de alguna llamada anterior en el mismo tick,
no algo que `CreateFontIndirect` haya fijado. Esta pista quedó
inválida desde el principio.

**Hipótesis 6 -- buffer de `szFfn` demasiado chico para nombres largos
(Win16-legacy, ≤7 chars): descartada.** `struct FFN` (`fontwin.h:39`)
tiene `CHAR szFfn[]` -- flexible, no un tamaño fijo chico. `LF_FACESIZE`
está definido correctamente en `Opus/lib/qwindows.h:1235` como `32`
(el valor Win32 real, no un valor Win16 heredado más chico). El buffer
de staging en `FontNameEnum` (`Opus/SYSCHG.C`), `CHAR rgbFfn[cbFfnLast]`
con `cbFfnLast = offset(FFN,szFfn) + LF_FACESIZE + 1`, tiene margen de
sobra para "Liberation Sans\0" (16 bytes de 33 disponibles). No se
llegó a revisar el *storage* permanente en `vhsttbFont`
(`IbstAddStToSttb`/`FChangeStInSttb`, `Opus/SYSCHG.C:~789`) -- ese es
un candidato real para una próxima sesión, distinto de lo que se
descartó acá.

**Estado al cortar: seis hipótesis descartadas con evidencia directa
(trazas y sondas standalone, no lectura de código sola). Causa raíz de
por qué `CreateFontIndirect` devuelve `NULL` para "Liberation Sans"
(segunda fuente distinta pedida, cualquier `hps`, charset ya válido,
slot de caché sano, sin error GDI fijado) sigue sin localizar.**
Patrón que sí se sostiene en todas las corridas: la *primera* fuente
distinta pedida (`Helv`) siempre funciona; la *segunda* (`Liberation
Sans`) siempre falla -- pero el mecanismo detrás de ese patrón no está
identificado.

**Candidatos no explorados para la próxima sesión:**
- `IbstAddStToSttb`/`FChangeStInSttb` (`Opus/SYSCHG.C`): el storage
  permanente en `vhsttbFont`, distinto del buffer de staging ya
  descartado en la Hipótesis 6.
- Diferencia entre `pffn->fGraphics`/`fRaster` para "Helv" vs.
  "Liberation Sans" -- se notó (sin confirmar del todo) que
  `fGraphics` probablemente da `fFalse` para "Liberation Sans" via la
  enumeración de impresora (`fty & DEVICE_FONTTYPE` puesto), algo que
  el guard de charset de arriba tuvo que dejar de usar como condición
  porque nunca disparaba.
- Instrumentar el propio `lf` completo (`LOGFONT`) justo antes de
  `CreateFontIndirect` -- se comparó `lfHeight`/`lfWeight`/`lfCharSet`
  pero no `lfPitchAndFamily`, `lfQuality`, ni el contenido final de
  `lfFaceName` byte a byte.

**Archivos modificados esta sesión (sin commitear):**
- `src/Opus/idle.c`: el guard `hps != 0` **se revirtió** (la condición
  original queda intacta) porque nunca se ejecuta y su justificación
  quedó descartada; se dejó el trace `idle-preload-check` (prueba de
  que el bloque nunca se alcanza) y un comentario apuntando a esta
  sección.
- `src/Opus/LOADFONT.C`: el fix real que se queda (`OEM_CHARSET` ->
  `DEFAULT_CHARSET` en `C_FGraphicsFcidToPlf`, sin condicionar a
  `fGraphics`) + trazas nuevas (`loadfont-caller` con return address,
  `fcid-identity`, nombre real de fuente como `stage`,
  `pfce-free-slot`/`pfce-evicted-slot`, `SetLastError(0)` antes de
  `CreateFontIndirect`). Todo guardado bajo `#ifdef OPUS_X64`, no toca
  MSVC.

**Cambio de entorno completamente revertido:** la impresora CUPS
`GenericPS` que se probó para la Hipótesis 3 se quitó; confirmado con
sonda standalone que Wine ya no ve ninguna impresora. No queda drift
de entorno.

**Quinta actualización 2026-08-20 (exia): tres hipótesis más
descartadas, un bug real distinto encontrado y arreglado (no relacionado
con la falla), y confirmación decisiva de que la causa NO es una
limitación de Wine.** Continuación directa de la sesión anterior (pull
de `6417213`), revisando los dos candidatos que quedaron anotados sin
explorar.

**Hipótesis 7 -- `pffn->fGraphics`/`fty` (candidato anotado, no un
mecanismo nuevo): descartada como explicación, ya estaba confirmada en
el código.** El comentario ya presente en `Opus/LOADFONT.C` (líneas
~996-999, agregado en la sesión anterior) confirma que `fGraphics` da
`fFalse` para "Liberation Sans" por la misma enumeración contra
`vpri.hdc` que corrompió el charset -- mismo origen ya identificado, no
un mecanismo distinto. `fGraphics` solo se guarda en `pfce->fGraphics`/
`vfli.fGraphics` (grep confirmado: sin ningún `if` que dependa de él
antes de `CreateFontIndirect`), así que no puede ser la causa de que la
llamada falle.

**Hipótesis 8 -- `lfPitchAndFamily` corrupto (`FF_DONTCARE=0` vs.
`FF_SWISS=32` de "Helv"): real, pero no causal -- descartada
empíricamente, no por lectura.** Un trace del `LOGFONT` completo justo
antes de `CreateFontIndirect` (todos los campos no revisados antes:
`lfPitchAndFamily`, `lfQuality`, `lfOutPrecision`, `lfClipPrecision`,
`lfWidth`, `lfEscapement`) mostró que **todo es idéntico entre "Helv" y
"Liberation Sans" excepto `lfPitchAndFamily`**: 32 (`FF_SWISS`) contra 0
(`FF_DONTCARE`) -- mismo origen que la Hipótesis 3/7 (`pffn->ffid`
corrupto por la misma enumeración sin impresora). Se forzó
`lfPitchAndFamily` a `FF_SWISS` para "Liberation Sans" como prueba
empírica directa (no como arreglo definitivo) -- **mismo fallo
idéntico**, `CreateFontIndirect` sigue devolviendo `NULL`. Revertido.
`FF_DONTCARE` es un valor legítimo y común en Windows real; no es la
causa.

**Bug real encontrado (no causal para esta falla, arreglado de todos
modos): lectura fuera de límites en `bltbyte(pffn->szFfn,
plf->lfFaceName, LF_FACESIZE)`.** `struct FFN.szFfn` es un flexible
array member (`CHAR szFfn[]; /* Variable length */`, `Opus/fontwin.h`),
almacenado en la STTB (`IbstAddStToSttb`/`FInsStInSttb1`,
`Opus/wordtech/sttb.c`) con tamaño exacto Pascal-string (`CbSzOfPffn`),
nunca 32 bytes. El `bltbyte` original copiaba 32 bytes fijos sin
importar el tamaño real asignado -- lectura fuera de límites genuina
para cualquier nombre de fuente más corto de 31 caracteres (es decir,
prácticamente todos). Confirmado con un dump hexadecimal de
`lf.lfFaceName[16..31]` para ambas fuentes: para "Helv" (entrada
sembrada al arranque, junto a otras entradas cortas válidas en memoria)
el byte de posición 15 es un carácter real ('e') seguido de más
contenido no-nulo -- el over-read cae en datos de OTRA entrada válida
adyacente. Para "Liberation Sans" (15 caracteres, agregada en runtime)
el byte 15 es correctamente `\0` (el nombre termina bien), pero el byte
16 es `0xFF` seguido de ceros -- **el over-read cae en memoria genuina
fuera de la entrada real.** En ambos casos, sin embargo, el terminador
nulo real cae en la posición correcta (4 para "Helv", 15 para
"Liberation Sans") -- y GDI/`CreateFontIndirect`, igual que `%s`, deja
de leer en el primer `\0`. **Esto descarta la hipótesis de corrupción
de nombre como causa de esta falla específica** -- el nombre que
`CreateFontIndirect` realmente ve es correcto en ambos casos -- pero
sigue siendo un bug real (comportamiento indefinido, lectura de heap
sin inicializar) que se arregló de todos modos: `Opus/LOADFONT.C`
ahora hace `SetBytes(plf->lfFaceName, 0, LF_FACESIZE)` seguido de un
`bltbyte` acotado a `CbSzOfPffn(pffn)` bytes reales (guardado bajo
`#if defined(__GNUC__) && !defined(_MSC_VER)`, MSVC sigue con el
`bltbyte` de 32 bytes original sin cambios).

**Confirmación decisiva: no es una limitación de Wine para estos
parámetros.** Con cada campo de `LOGFONT` inspeccionable ya descartado
o idéntico, y el nombre confirmado correcto hasta su `\0`, se armó una
sonda standalone (`winegcc`, sin código de este proyecto -- mismo
patrón que la "Confirmación decisiva" de §12/`--interaction`) que
llama `CreateFontIndirectA` con el `LOGFONT` **byte por byte idéntico**
al que falla dentro de WORD1 (`lfFaceName="Liberation Sans"`,
`lfHeight=0`, `lfWeight=400`, `lfCharSet=1`, `lfQuality=4`,
`lfPitchAndFamily=0` y también probado con `32`, resto en cero).
**Ambas variantes tienen éxito** (`hfont` no nulo, `GetLastError=0`) en
un proceso limpio, mismo binario de Wine, mismo prefix. Esto descarta
por completo que sea una limitación de Wine para esta combinación de
parámetros -- el mismo `CreateFontIndirectA` con los mismos datos
funciona perfecto fuera de WORD1. La causa está genuinamente en algo
del estado de proceso/GDI de WORD1 en ese momento, no en los datos que
se piden.

**Hipótesis de agotamiento de handles GDI: descartada, sin necesidad de
medir vía `GetGuiResources`.** Se intentó medir el conteo de objetos
GDI del proceso WORD1 desde el arnés de test (`GetGuiResources(hProcess,
GR_GDIOBJECTS)`) -- esta build de Wine lo tiene sin implementar,
siempre devuelve 0. Señal inútil, descartada como método. Pero un
conteo más simple resultó decisivo: el trace `loadfont-caller`
(`__builtin_return_address` de cada llamada a `C_LoadFont`) aparece
**35 veces** en toda la corrida del test, pero `fcid-identity` (el
único punto que de verdad llega a `C_FGraphicsFcidToPlf`/
`CreateFontIndirect`, tras pasar los CASE 1-3 de caché de
`C_LoadFcid`) aparece solo **2 veces** -- las otras 33 son *cache
hits*, nunca llegan a pedir una fuente nueva a GDI. Con solo 2
creaciones reales de fuente en toda la corrida, un límite de 10.000
handles por proceso (el límite clásico de Windows) queda
completamente fuera de alcance. Descartado sin ambigüedad.

**Estado al cortar (segunda vez): nueve hipótesis descartadas en total
con evidencia directa** (las seis de la actualización anterior + estas
tres). La causa de por qué `CreateFontIndirect` devuelve `NULL`
específicamente para la segunda fuente distinta pedida, con datos
confirmados correctos en cada campo inspeccionable, sigue sin
localizar -- y ya no hay ningún candidato más a nivel de datos/estado
de aplicación que revisar por trazas o sondas standalone. Lo único que
queda, si se retoma, es bajar un nivel: adjuntar `gdb` al proceso
WORD1 real (no una sonda standalone limpia) y poner un breakpoint
dentro de la implementación de `CreateFontIndirect` de Wine mismo
(`dlls/gdi32` o `dlls/win32u`, según versión) para ver en qué paso
interno diverge esa segunda llamada respecto a la primera -- una
herramienta bastante más pesada que trazas de aplicación o sondas
standalone, y no intentada todavía.

**Archivos modificados esta ronda (sin commitear al momento de
escribir esto):**
- `src/Opus/LOADFONT.C`: el arreglo real que se queda (bltbyte
  acotado + `SetBytes` de cero para `lfFaceName`, guardado GNUC-only,
  MSVC sin cambios) -- corrige un bug genuino de comportamiento
  indefinido, no relacionado con la falla de este test. Trace de
  `fcid-identity` (ya existente) se mantiene tal cual.
- `src/port/original/opus_word1_ui_test.cpp`: sin cambio neto (se
  agregó y luego se quitó el chequeo de `GetGuiResources`, que resultó
  no implementado en este Wine).

**Verificado, sin regresión:**
```
DISPLAY=:99 ctest --test-dir out/linux-winelib-debug -L word1_startup_blocked
    7/9 -- mismos dos fallos conocidos (--interaction, --font-typing)

DISPLAY=:99 ctest --test-dir out/linux-winelib-debug -LE word1_startup_blocked
    9/9 -- gating sin cambios
```

## 9. `--clipboard`: "Ctrl+A did not execute Select All" -- confirmado, verify-only, cierra Task 7

**Task 7, 2026-08-19 en exia.** El plan (Step 1) pedía correr
`opus_word1_clipboard_shortcut_test` aislado como primer paso. Igual
que Task 4, ya venía pasando en cada corrida completa de la etiqueta
de esta sesión -- se verificó aislado para confirmarlo formalmente:

```
ctest -R "^opus_word1_clipboard_shortcut_test$" --output-on-failure
    Passed    3.61 sec
```

Mismo patrón que Task 4 (File > New): comparte causa raíz con Task 3
(`_setjmp`/`longjmp` ABI). Sin cambios de código -- commit de
documentación solamente.

Rama `fix/winelib-startup-blocked` (no está en `main`). Plan:
`docs/superpowers/plans/2026-08-15-terminar-winelib.md`. Ledger SDD:
`.superpowers/sdd/2026-08-15-terminar-winelib/progress.md`.

Build/test solo en debian13 contra `/home/pablo/build-debian13-verify`,
`DISPLAY=:59`. No usar el `--preset` del host. **Este build dir es
compartido con al menos otra sesión** (visto en vivo el 2026-08-15, ver
§4 arriba) — antes de fiarse de un resultado de `ctest` rojo, confirmar
que no hay otro proceso `cmake`/`ninja`/`ctest` corriendo ahí al mismo
tiempo (`ps aux | grep -E 'cmake|ninja|ctest'` dentro del contenedor).

Task 3 **cerrada** en Fix round 3 + cierre de revisión (ambas rondas de
review, clean): el AV era `_setjmp` con ABI Microsoft-x64 pisando
`*vhpllbs`. Las dos mitades del par (`_setjmp` y `longjmp`) están fijadas
a System V por definición, con guard de build que lo verifica.
`opus_word1_about_test` pasa; la etiqueta llegó a 5/9. Tasks 1–2 hechas.
11 hallazgos menores quedaron diferidos a la revisión final de toda la
rama (lista completa en el ledger SDD).

Task 4 **cerrada** 2026-08-19 en exia (§6 arriba) — verify-only,
confirma la hipótesis de §4: `opus_word1_ui_test` (modo base, ejercita
File > New) pasa limpio y aislado, sin ningún cambio de código. Mismo
root cause que About (Task 3). El fallo de §4 fue el entorno
compartido de esa sesión en debian13, no un bug independiente de
File > New.

Task 5 (Save As) **no empezada** — el único de los 4 tests que ya
fallaba con mensaje propio (no el AV genérico) antes de esta sesión;
candidato a causa raíz independiente, ver la nota del brief sobre
`run_word95_common_file_dialog`.

Tasks 6–10 **no empezadas**.

Aparte y sin relación con Task 3/4: `opus_x64_runtime_test` (gating)
se cuelga sin imprimir nada, confirmado pre-existente (binario de un
día antes, sin símbolo `setjmp`, se cuelga igual). Sigue sin
investigar.

Sesión cerrada 2026-08-15 a pedido del usuario tras ~2 h de trabajo
(no por límite de uso). Sin trabajo a medias sin commitear — árbol
limpio en `25325c0`.

**Actualización 2026-08-19 (exia, revisión independiente de Task 3 a
Task 9):** ver §5-§11 arriba. 4 hallazgos de fidelidad corregidos y
verificados (Task 3), Task 4 cerrada verify-only, Task 5 (Save As) con
causa raíz independiente real encontrada y arreglada (señuelo
`OpusSdmDialog` sin conexión al diálogo real `GetSaveFileNameA`),
Task 6 (`--font-typing`) con 2 de 3 bugs reales arreglados (nombres de
fuente Windows nunca enumerables; `union FCID` de 8 bytes en Linux por
LP64) y un tercero localizado sin cerrar (el foco no vuelve al panel
tras elegir fuente del ribbon -- toca `Opus/iconbar1.c` restringido,
necesita autorización), Task 7 (Ctrl+A) cerrada verify-only, Task 8
(`--selection`) con 3 bugs de arnés encadenados encontrados y
arreglados (falta de foco real, mensajes sintéticos en vez de input
real, constante de píxel obsoleta que asumía margen izquierdo cero),
Task 9 (`--typing`) con el mismo bug de foco real que Task 8, arreglado
igual. **7/9** en la etiqueta, subiendo de 5/9 al empezar esta sesión.
Reproducido en un segundo entorno (exia, no debian13/hp-15). Árbol
limpio, 11 commits nuevos sobre `16145b6`, pusheados a
`origin/fix/winelib-startup-blocked`.

## 10. `--selection`: "sentence-end click produced an invalid selection" -- 3 bugs de arnés encadenados, arreglados

**Task 8, 2026-08-19 en exia.** El plan esperaba `"typing did not leave
a canonical insertion selection"` (fail 38); esa parte ya pasaba (Task
2 no la rompió). El fallo real, más adelante, era fail 39. **Los 3
bugs son del arnés de test, no de WORD1** -- verificado con
`kWmOpusX64QuerySelection` en cada paso antes de tocar nada.

**Bug A -- clic sintético sin foco real:** `selection_mode` era el
único bloque de este archivo que hacía clics posicionales
(`WM_LBUTTONDOWN`/`UP` con coordenadas) sin llamar antes
`make_foreground_and_focus` -- cada otro bloque con input real de este
mismo archivo sí lo hace (grep confirma 9 sitios). Sin foco/activación
real, cualquier clic (sintético o real) resolvía siempre a `cp=0` sin
importar `x`. Fix: añadir la llamada, mismo patrón que el resto.

**Bug B -- mensajes sintéticos en vez de input real:** incluso con
foco, `SendMessageW(pane, WM_LBUTTONDOWN, ...)` entrega directo al
window proc sin pasar por la cola de mensajes real -- se cambió a
`SetCursorPos`+`SendInput` (`send_mouse_button`), el patrón ya probado
en este mismo archivo para el caso idéntico "clic cerca del final de
la oración" (`interaction_mode`, ~línea 1853).

**Bug C -- constante de píxel obsoleta:** con A y B arreglados, el
mapeo `x=10..250` reveló un margen izquierdo real de ~185-190px antes
del primer carácter (`x=180` seguía en `cp=0`; recién en `x=190`
`cp=1`) -- la constante hardcodeada `x=250` para "cerca del final de
la oración" (32 caracteres, ~7px cada uno) solo alcanzaba `cp=12`, no
los `>=15` que pide la aserción. Fix: en vez de adivinar un nuevo
píxel fijo, el bucle de mapeo ahora extiende su rango (10-450) y
guarda el primer `x` real que alcanza `cp >= sentence_length/2` --
usado como blanco del clic final, sin asumir ningún margen.

**Efecto secundario del Bug B, encontrado y arreglado en el camino:**
el bucle de mapeo con `SendInput` real, 24 clics seguidos con solo
20ms entre down/up, disparaba detección de doble-clic real de Wine/
Win32 justo antes del clic final (`clicked_double=1` en vez de `0`,
rompiendo esa aserción por separado). Fix: `Sleep(60)` entre cada
probe del bucle, y `Sleep(GetDoubleClickTime()+150)` antes del clic
final dedicado.

**Verificado:**
```
DISPLAY=:99 ctest -R "^opus_word1_selection_test$" --output-on-failure
    Passed    9.85 sec

DISPLAY=:99 ctest -L word1_startup_blocked --output-on-failure
    6/9 -- sin regresión en los demás
```

Archivo: `src/port/original/opus_word1_ui_test.cpp` únicamente --
ningún cambio en `Opus/` ni `opus_sdm_runtime.cpp` para esta tarea.

## 11. `--typing`: "typed text was not painted in the document pane" -- mismo patrón que Task 8, arreglado

**Task 9, 2026-08-19 en exia.** El plan esperaba salidas 13/15 con
variabilidad entre corridas (§27 de `01-diagnostico...`); esta sesión
el fallo real ya llegaba consistentemente hasta el último check, fail
16, sin variar entre corridas -- Task 2's fix del crash movió el punto
de fallo más allá de donde el plan lo dejó.

**Causa raíz:** `typing_mode` era el único modo interactivo de este
archivo que **nunca** buscaba explícitamente el panel `OpusWwd` ni
llamaba `make_foreground_and_focus` -- confiaba ciegamente en lo que
`GetGUIThreadInfo` reportara como ya enfocado al arrancar. El check de
`fail(13)` solo verificaba `hwndFocus != nullptr`, nunca que fuera
*el panel correcto*. Si otra ventana tenía el foco por accidente del
orden de creación, los `WM_CHAR` posteados con
`post_keyboard_character` se encolaban sin error (`fail(15)` nunca
dispara) pero no llegaban a ningún sitio visible -- exactamente el
síntoma: el test llega hasta el último check (`fail(16)`, conteo de
píxeles oscuros) y falla ahí, nunca antes.

**Fix:** buscar `OpusWwd` explícitamente y llamar
`make_foreground_and_focus` antes de postear, mismo patrón que Task 8
(§10) estableció como el requerido para todo bloque de este archivo
que depende del foco real.

**Verificado:**
```
DISPLAY=:99 ctest -R "^opus_word1_typing_test$" --output-on-failure
    Passed   14.78 sec

DISPLAY=:99 ctest -L word1_startup_blocked --output-on-failure
    7/9 -- sin regresión en los demás
```

Archivo: `src/port/original/opus_word1_ui_test.cpp` únicamente.

## 12. `--interaction`: "dragging the caption did not move the WORD1 window" -- limitación de entorno confirmada, no bug del proyecto

**Task 10, 2026-08-19 en exia -- último ítem del plan.** El plan (Step
2) ya anticipaba esta posibilidad: "check whether this is a Wine/
window-manager limitation similar in kind to the `CreateProcessW`
zero-PID precedent (§25)". Lo es, confirmado con una réplica
independiente, sin código del proyecto.

**Descartado primero (ninguno de los dos era la causa):**
- **Sin window manager:** `:99` (el Xvfb compartido de esta sesión) no
  tenía ninguno corriendo. Se instaló `openbox` (via `apt`, con
  autorización) y se levantó un Xvfb propio y aislado (`:77`, no
  toca el `:99` compartido) -- mismo resultado exacto.
- **Salto de cursor único en vez de arrastre incremental:**
  `SetCursorPos` de una sola vez entre el down y el up podría no
  disparar el umbral de arrastre (`SM_CXDRAG`/`SM_CYDRAG`) que el
  loop de `SC_MOVE` de Wine espera. Se cambió a 8 pasos incrementales
  con `Sleep(15)` entre cada uno -- mismo resultado exacto.

**Confirmación decisiva:** una sonda standalone (`winegcc`, sin código
de este proyecto) contra `wine notepad` -- el builtin de Wine, la
misma referencia "conocida-buena" que ya usa `01-diagnostico-heap-
corruption-arranque.md` en otros puntos -- con la *misma* secuencia
exacta (`WM_NCHITTEST` confirma `HTCAPTION`, `SetCursorPos`+`SendInput`
incremental, `MOUSEEVENTF_LEFTDOWN`/`LEFTUP`) bajo el mismo `:77`+
`openbox`: **tampoco se mueve** (`before=0,0 after=0,0`, idéntico al
síntoma de WORD1). Si ni siquiera notepad puede arrastrarse así en
este entorno, no es un bug de WORD1 ni de `Opus/wproc.c` -- es una
limitación de cómo Wine/este `winex11.drv` maneja el loop `SC_MOVE`
frente a input sintetizado vía `SendInput`, en este entorno
específico.

Se revisó también si `Opus/wproc.c` intercepta `WM_NCLBUTTONDOWN` con
lógica propia que pudiera estar interfiriendo -- no lo hace; la única
mención de ese mensaje en ese archivo es una tabla de logging bajo
`#ifdef RSH` (build de investigación, no activa aquí), no un handler
real. Confirma que el mensaje cae directo a `DefWindowProc`, igual que
en `notepad`.

**Sin cambio de comportamiento -- se mantiene el arrastre incremental
en el test** (más fiel a un arrastre real de usuario que el salto
único original, aunque no fue la causa) y se agregó un diagnóstico
(`before=`/`after=`/`caption_point=`) para que una futura sesión no
tenga que re-derivar esto. Test sigue fallando, documentado como
limitación de entorno, no bug -- mismo tratamiento que §25.

**Verificado:**
```
DISPLAY=:99 ctest -L word1_startup_blocked --output-on-failure
    7/9 -- sin regresión (--interaction seguía fallando antes y
    después, por la razón ahora documentada, no una nueva)
```

Archivo: `src/port/original/opus_word1_ui_test.cpp` (arrastre
incremental + diagnóstico). Dependencias del sistema instaladas esta
sesión (`apt`, con autorización): `twm` (descartado, crashea sin
`xfonts-base` que también se instaló), `openbox` (usado para la
réplica).

## Resumen

Los 8 ítems de comportamiento de la lista original de §26 de
`01-diagnostico-heap-corruption-arranque.md`, estado final tras esta
sesión (2026-08-19, exia):

1. `--about` -- **arreglado** (Task 3, AV de `_setjmp`/`longjmp` ABI)
2. `--new` (File > New) -- **arreglado**, mismo root cause que #1
   (Task 4, §6)
3. `--save-as` -- **arreglado**, causa independiente: diálogo señuelo
   sin conectar al real (Task 5, §7)
4. `--font-typing` -- **parcial**: 3 de 4 bugs arreglados (nombres de
   fuente, `union FCID` LP64, foco tras selección de ribbon -- este
   último arreglado 2026-08-20 en `opus_sdm_runtime.cpp`, no en
   `Opus/iconbar1.c`, ver actualización en §8); el cuarto bug sigue sin
   arreglar. Diagnóstico engañoso descartado dos veces: "no conserva la
   fuente" (el tecleo nunca llega a Opus) y luego la teoría de carrera
   `idle.c:503`/`selCur.chp.hps==0` de la "Tercera actualización"
   -- **descartada con trazas directas** en la "Cuarta actualización"
   (debian-VM): el bloque de preload de `idle.c` nunca se alcanza
   durante el test. `CreateFontIndirect` falla realmente en
   `Opus/disp1.c`'s `LoadFcidFull` (repintado de documento, no
   preload), para la segunda fuente distinta pedida en el proceso,
   con `hps` idéntico al de la primera (que sí funciona). Seis
   hipótesis descartadas con evidencia (carrera `hps`, charset
   `OEM_CHARSET`, caché de fuentes, `GetLastError` remanente, buffer
   `szFfn`); un fix real y menor se queda (charset saneado en
   `LOADFONT.C`) pero no cierra el test solo. Causa raíz real aún sin
   localizar -- ver "Cuarta actualización" en §8 para candidatos no
   explorados
5. `--clipboard` (Ctrl+A) -- **arreglado**, mismo root cause que #1
   (Task 7, §9)
6. `--selection` -- **arreglado**, 3 bugs de arnés encadenados (Task
   8, §10)
7. `--typing` -- **arreglado**, mismo patrón de foco que #6 (Task 9,
   §11)
8. `--interaction` (arrastre de ventana) -- **limitación de entorno
   confirmada, no bug** (Task 10, §12)

**Etiqueta `word1_startup_blocked`: 7/9** (los 2 que faltan son el
bug 4 sin cerrar de `--font-typing` y la limitación de entorno de
`--interaction`, ambos ya explicados arriba, no misterios). El noveno
test de la etiqueta era `word1_port_smoke_test`, que ya pasaba desde
antes de esta sesión.

Aparte de la lista de 8, sigue sin investigar: `opus_x64_runtime_test`
(gating, cuelga sin imprimir nada, confirmado pre-existente y no
relacionado con ningún fix de esta sesión).

Rama `fix/winelib-startup-blocked`, no fusionada a `main`. Todo
pusheado a `origin/fix/winelib-startup-blocked`.
