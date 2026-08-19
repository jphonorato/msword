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

**Actualización 2026-08-19 (exia, revisión independiente de Task 3 +
Task 4 + Task 5):** ver §5, §6, §7 arriba. 4 hallazgos de fidelidad
corregidos y verificados (Task 3), Task 4 cerrada verify-only, Task 5
(Save As) con causa raíz independiente real encontrada y arreglada
(señuelo `OpusSdmDialog` sin conexión al diálogo real
`GetSaveFileNameA`). **5/9** en la etiqueta, reproducido en un segundo
entorno (exia, no debian13/hp-15). Árbol limpio, 8 commits nuevos sobre
`16145b6`, pusheados a `origin/fix/winelib-startup-blocked`.

Tasks 6-10 siguen sin empezar: `--font-typing` (ribbon, "today's
original bug report" del plan -- necesita un display real, no Xvfb,
ver la nota del Step 1 de Task 6 más abajo), `--typing` ("typed text
was not painted"), `--interaction` ("dragging the caption did not move
the window"), `--selection` ("sentence-end click produced an invalid
selection"). `opus_x64_runtime_test` (gating) sigue colgado, sin
investigar, confirmado también en exia.
