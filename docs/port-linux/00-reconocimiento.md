# Reconocimiento para el port a Winelib (ELF nativo)

Fecha original: 2026-08-09 · Rama `main` @ `ac5472e`. Actualizado el mismo día tras
completar la Fase 0 del plan.

Todas las afirmaciones de este documento están respaldadas por comandos ejecutados en
esta máquina. Los experimentos de reconocimiento se hicieron en un directorio temporal
fuera del repositorio; los cambios de la Fase 0 (`src/cmake/`,
`src/CMakeLists.txt:79-89`) sí están en el árbol y se detallan en la sección «Fase 0 —
ejecutada el 2026-08-09» más abajo.

---

## 0. Resumen ejecutivo

El port es **más viable de lo que sugiere el `CMakeLists.txt`**. Con tres banderas de
compilador (`-std=gnu89 -funsigned-char -fms-extensions -fpermissive`), un directorio
de compatibilidad de mayúsculas para 12 cabeceras, y las cabeceras generadas por las
herramientas de Microsoft, **156 de las 207 unidades de traducción del motor
(`opus_original_engine`) compilan sin tocar una sola línea de `src/Opus/`** (186
errores restantes, repartidos en 51 archivos). La cifra original de reconocimiento era
154/207 con 202 errores; la Fase 0 sumó dos módulos (`menuhelp.c`, `eldlg.c`) al
resolver el ítem 2.

Los cinco obstáculos reales, en orden de riesgo:

| # | Obstáculo | Estado |
|---|---|---|
| 1 | 427 comandos de Word se resuelven con `GetProcAddress` sobre la tabla de exportación del propio `.exe`, generada por `#pragma comment(linker,"/export:")` | **Resuelto en principio**: verificado que un `.spec` de winebuild expone símbolos vía `GetProcAddress` en un ELF Winelib |
| 2 | `src/cmake/` no existía en ningún commit; el build referenciaba dos archivos ausentes | **Resuelto en la Fase 0** (`src/cmake/GenerateMenuHelpHeader.cmake`, `src/cmake/GenerateElxInfoHeader.cmake`). Ya no bloquea. |
| 3 | Sistema de archivos sensible a mayúsculas: 12 cabeceras, 202 archivos afectados | Mecánico |
| 4 | `long` es de 8 bytes en Linux LP64 y de 4 en Windows LLP64 | Acotado, pero requiere auditoría |
| 5 | libstdc++ es inutilizable para cadenas anchas bajo el `-fshort-wchar` de winegcc | Acotado a ~5 archivos, todos en `src/port/` |

---

## 1. Inventario de construcciones específicas de MSVC

Ámbito: el árbol que efectivamente se compila — `src/Opus/**` (207 TUs del motor),
`src/port/original/**`, `src/port/tools/**`, y las 4 herramientas de
`src/OpusEtAl/tools/src/` que el build usa (`mkcmd.c`, `bitapp.c`, `mkdlg.c`,
`mergeelx.c`). Se excluyen `Opus/asm/` (59 `.asm` de referencia, no compilados),
`Opus/sdm/`, `Opus/tools/`, `Opus/program/`, `OpusEtAl/tools/src/draw/`, `convtest/`,
`opustlbx/` y `OpusProg/`.

### 1.1 Palabras clave de segmentación y convención de llamada

| Construcción | Ocurrencias | Situación |
|---|---:|---|
| `PASCAL` | 210 | **No es problema.** `Opus/lib/qwindows.h:105` la define como `pascal`, y `port/original/opus_x64_compat.h:143` define `pascal` a vacío. |
| `FAR` | 557 | Ídem vía `qwindows.h:112` → `far` → vacío (`opus_x64_compat.h:126`). |
| `HUGE` | 556 | Ídem vía `opus_x64_compat.h:152`. |
| `NEAR` | 45 | `qwindows.h:113` la define a vacío. |
| `__stdcall` / `_stdcall` | 2 | Sólo en `port/original/opus_asm_resn2_sttb.cpp:8,13`. winegcc lo define como `__attribute__((ms_abi))`. Sin efecto práctico en x86-64. |

**Conclusión:** el autor original ya neutralizó todo el vocabulario segmentado en
`opus_x64_compat.h`. No hay trabajo pendiente aquí.

### 1.2 `__declspec`

3 ocurrencias, todas viables:

- `Opus/RTFTBL.H:128` — `__declspec(selectany)`. winegcc lo traduce a
  `__attribute__((weak))` (verificado en la línea de comandos que genera: `-D__declspec_selectany=__attribute__((weak))`).
- `port/original/opus_asm_resn2_sttb.cpp:8,13` — `__declspec(dllimport)` →
  `__attribute__((dllimport))`.

**Sin trabajo pendiente.**

### 1.3 `#pragma`

| Pragma | Archivos | Situación |
|---|---|---|
| `#pragma pack(push/pop)` | `Opus/cmdtbl.h:65,208`; `port/original/opus_asm_movecmds.c:12,48`; `OpusEtAl/tools/src/bitapp.h:37,64`; `OpusEtAl/tools/src/mkcmd.c:18,405` | GCC lo soporta idénticamente. Sin trabajo. |
| `#pragma once` | 9 archivos, todos en `port/original/` | Soportado. Sin trabajo. |
| `#pragma comment(linker,"/export:…")` | Generado por `mkcmd.c:1415` en `opuscmd_native.inc`, consumido por `port/original/opus_asm_movecmds.c:162` | **El problema central del port.** Ver §1.7. |
| `#pragma warning` / `intrinsic` / `optimize` | 0 | — |

### 1.4 Intrínsecos, ensamblador en línea y cabeceras exclusivas

- `__asm` / `_asm`: **0 ocurrencias reales.** La única coincidencia
  (`Opus/prompt.h:99`) es texto dentro de un comentario. El autor original ya tradujo
  todo el ensamblador a C/C++ en `port/original/opus_asm_*.{c,cpp}`.
- `<intrin.h>`, `__debugbreak`, `_BitScan*`, `__popcnt`, `_rotl`: **0**.
- `__int8/16/32/64`: **0**.
- `__inline`: 12 ocurrencias (6 en `src/Opus/`, 6 en `opus_x64_compat.h`). GCC lo
  acepta como sinónimo de `inline`. Sin trabajo.
- Cabeceras propias de MSVC en uso: `<direct.h>` (`opus_asm_file2.cpp:3`),
  `<malloc.h>` (`opus_x64_compat.h:19`, `mergeelx.c:7`), `<process.h>`, `<io.h>`
  (sólo en herramientas no compiladas). **Todas existen en `/usr/include/wine/msvcrt/`**,
  pero ver §5.3 sobre por qué ese directorio no es utilizable de forma global.

### 1.5 CRT seguro de Microsoft

Sólo 6 sitios, todos en `src/port/` (territorio modificable):

- `_snprintf_s` × 3 — `port/original/opus_startup_diagnostics.cpp:14,32,60`
- `_vsnwprintf_s`, `_countof` — `port/original/opus_original_startup_probe.cpp:161`
- `_stricmp` — `port/original/opus_sdm_runtime.cpp` (7 sitios),
  `port/original/opus_word1_ui_test.cpp` (3 sitios)
- `_wcsicmp` — `port/original/opus_word1_ui_test.cpp` (3 sitios)

También `_stricmp` aparece 6 veces en `OpusEtAl/tools/src/mkcmd.c` (herramienta de
host; `strcasecmp` es sustituto directo).

### 1.6 Constructos de C anteriores a ANSI que GCC rechaza

Éste es el inventario que el enunciado no pedía explícitamente pero que domina el
esfuerzo real. Cifras obtenidas compilando las 207 TUs del motor con winegcc
(ver §6 para la metodología):

| Clase de error | Ocurrencias | Archivos |
|---|---:|---|
| *cast as lvalue* — `*((int *)p)++` | 21 | `Opus/style.c` (6), `Opus/ffread.c` (3), `Opus/interp/exp.c` (2), `OpusEtAl/tools/src/mkcmd.c` (5), + 5 en variantes de asignación |
| Inicializador no calculable en tiempo de carga | 90 | **una sola cabecera**: `Opus/keys.h:287-…`, expandida por `Opus/iconbar3.c:710` y similares |
| Definición K&R en conflicto con prototipo ANSI | 13 | `Opus/help.h:229` vs `port/original/about.sdm:5`, `Opus/help.c:316`, etc. |
| `static` tras declaración no-`static` | 10 | dispersos |
| Llamada con más argumentos que el prototipo K&R vacío | 9 | punteros a función declarados `()` |
| `struct` anidada con etiqueta y sin nombre de miembro (extensión MS) | ~0 tras `-fms-extensions` | `Opus/wordtech/props.h:229` (`struct PAP` / `struct PAPS`) — **resuelto con `-fms-extensions`** |

Nota importante: `struct PAP` contiene `struct PAPS { … };` sin nombre de miembro.
Ese es el *nameless struct* de MSVC, y `-fms-extensions` lo habilita en GCC. Antes de
añadir la bandera, ese solo patrón hacía fallar `search.c`, `layout.c`, `layout1.c`,
`layout2.c` y otros; después, desaparece por completo.

### 1.7 La dependencia arquitectónica: exportaciones del ejecutable

`port/original/opus_asm_movecmds.c:174-177`:

```c
HMODULE module = GetModuleHandleW(NULL);
return (OPUS_PFN)(uintptr_t)GetProcAddress(module, name);
```

Word resuelve **cada comando** por nombre contra la tabla de exportación de su propio
ejecutable. Esa tabla la puebla `opuscmd_native.inc`, generado por MKCMD, con una línea
`#pragma comment(linker, "/export:NAME")` por función.

Ejecuté MKCMD en Linux (§3) y conté: **427 exportaciones, 427 nombres únicos.**

GCC ignora `#pragma comment` con una advertencia, de modo que la compilación *pasaría*
y el binario fallaría en tiempo de ejecución al resolver el primer comando. Bajo
Winelib el equivalente es un archivo `.spec` de winebuild.

**Verificado experimentalmente** (`exp.c` + `exp.spec` → `winegcc -o exp.exe exp.c exp.spec`):

```
module=0x7f5812880000 CmdHelp=0x7f581289042e -> 4242  CmdFoo=0x7f5812890439
```

`GetProcAddress(GetModuleHandleW(NULL), "CmdHelp")` resuelve correctamente en un ELF
Winelib cuando el símbolo está declarado en el `.spec`. **El supuesto más riesgoso del
port queda validado.**

---

## 2. Estado de `src/port/tools/`

El directorio contiene tres archivos:

| Archivo | Tipo | Portabilidad |
|---|---|---|
| `opus_dibapp_tool.cpp` | C++20 ISO puro (`<cstdint> <fstream> <iomanip> <iostream> <string> <vector>`) | **Totalmente portable.** Verificado: `g++ -std=c++20` lo compila sin errores, y lo ejecuté para regenerar los 37 `.hb` desde `Opus/resource/*.dib`. |
| `opus_cabi_tool.cpp` | C++20 **+ `opus_x64_compat.h` → `<windows.h>` + `sdm.h`** | **No es una herramienta de host portable.** Incluye 78 cabeceras `.hs` y calcula `sizeof`/alineación de las estructuras SDM. Su salida depende del ABI del objetivo, así que debe compilarse con el mismo compilador que el motor. Bajo Winelib eso significa construirlo como ejecutable Winelib y ejecutarlo a través de su stub. Nótese que ya hace `#undef native` en la línea 3 — el autor ya se topó con el problema descrito en §5.4. |
| `make_win95_toolbar_sprite.ps1` | PowerShell | No participa del build (`word95-toolbar.bmp` está versionado). Sin impacto. |

### Las cuatro herramientas de `OpusEtAl/tools/src/` que sí se compilan

Éstas son las que realmente determinan si la cadena de generación puede correr en
Linux. Resultado de compilarlas con `gcc` nativo:

| Herramienta | `-std=gnu17` (por defecto en GCC 16) | `-std=gnu89` | Notas |
|---|---|---|---|
| `bitapp.c` | falla | **compila** | `#define FAR` ya está guardado con `OPUS_X64_TOOL` (`bitapp.h:18`) |
| `mkdlg.c` | falla | **compila** | `main()` K&R sin tipo de retorno |
| `mergeelx.c` | falla | **compila** | declaraciones implícitas |
| `mkcmd.c` | falla | **falla** | 5 sitios de *cast as lvalue* + `_stricmp` |

`mkcmd.c` es la única que necesita edición. Con un parche mecánico de 5 líneas
(macros `OPUS_PI_RD`/`OPUS_PI_WR`) y `-D_stricmp=strcasecmp`, compila y **corre
correctamente en Linux**, produciendo `opuscmd.h`, `IBCM.H`, `RGBCM.H`, `OPUSCMD2.H`,
`OPUSMENU.H`, `MENUHELP.TXT`, `opuscmd.asm` y `opuscmd_native.inc` (§3).

### Un problema de convención POSIX en BITAPP

`OpusEtAl/tools/src/bitapp.c:59`:

```c
if (*(rgszArg[cCurArg]) == '/')
    SetFlag(*(rgszArg[cCurArg]+1));
```

Cualquier argumento que empiece con `/` se interpreta como conmutador estilo DOS. Como
CMake pasa rutas absolutas, **toda invocación de BITAPP falla en Linux** con
`Invalid Switch on command line.` La solución no requiere tocar `bitapp.c`: basta
invocarlo con rutas relativas desde un `WORKING_DIRECTORY`. Verificado: con
`./mk/bitapp res/8hdr.bmp gen/8hdr.hb` el análisis de argumentos ya pasa.

Queda un fallo secundario sin diagnosticar (`Unexpected End Of File Reached in Input
file!`) que casi con seguridad es §4.1: `bitapp.h:29` declara `typedef unsigned long
DWORD;`, de modo que `sizeof(struct BITMAP)` cambia en LP64 y `fread(&bm, sizeof(BITMAP), 1, …)`
lee un número incorrecto de bytes. Es la evidencia más limpia de que el problema de
`long` es real y no teórico.

---

## 3. Cadena de recursos

### 3.1 Qué archivos existen

- **`.rc`**: exactamente tres. Sólo uno participa del build:
  - `src/port/word1.rc` — **el único referenciado por CMake** (línea 857). Contiene
    `RT_MANIFEST`, un `BITMAP` (201), dos `ICON` (301/302) y un bloque `VS_VERSION_INFO`.
  - `src/Opus/resource/misc.rc` — el script de recursos original de Word 1.1a
    (iconos, cursores, bitmaps, con `#include "winrc.h"`). **No se compila hoy.**
  - `src/OpusEtAl/tools/src/draw/mustang.rc` — de una herramienta no compilada.
- **`.dlg`**: **ninguno.** El enunciado asume su existencia; el archivo no está.
- **`.des`**: 86 archivos en `src/Opus/dlg/`. Son las fuentes del Dialog Editor.
- **`.elx`**: **ninguno**, pese a que `src/Opus/dlg/elx.txt` lista 86 nombres `.elx` y
  `CMakeLists.txt:391` los busca con `file(GLOB)`. El comentario en `CMakeLists.txt:74-76`
  lo reconoce: el archivo carece del compilador del Dialog Editor que convertía `.des`
  en `.elx`. **El objetivo `opus_generated_dlgcheck` opera sobre un conjunto vacío.**
- **`.hs` / `.sdm`**: 78 pares en `src/port/original/` — reconstrucciones del autor que
  sustituyen la salida ausente del Dialog Editor.

### 3.2 Cómo los consume CMake hoy

`project(… LANGUAGES C CXX RC)` (línea 3) declara el lenguaje RC, y `port/word1.rc`
entra como fuente del ejecutable `WORD1`. Todo el resto de la "cadena de recursos" no
pasa por `rc.exe` en absoluto: son **cabeceras C generadas** e incrustadas por
`#include`, producidas por BITAPP y DIBAPP desde `.bmp`/`.cur`/`.ico`/`.dib`.

Verifiqué que las 52 entradas de `OPUS_BITAPP_BITMAP_HEADERS` +
`OPUS_BITAPP_FIGURE_HEADERS` y los 7 cursores tienen su archivo fuente presente en
`Opus/resource/` **con la mayúscula/minúscula correcta**, y que hay 37 entradas `.dib`.
No hay archivos faltantes en esa parte.

### 3.3 Qué puede asumir `wrc`

**`wrc` compila `port/word1.rc` tal cual, sin modificaciones.** Verificado:

```
$ wrc -I src/port -o word1.res src/port/word1.rc
FYI: Starting parse
FYI: Writing .res-file      → 28140 bytes
```

Maneja `#include <windows.h>`, `RT_MANIFEST`, `VS_VERSION_INFO`, y **también las rutas
con barra invertida** (`"assets\\word95-toolbar.bmp"`).

Dos restricciones descubiertas:

1. **`winegcc` rechaza archivos `.rc` en su línea de órdenes**:
   `winegcc: Can't compile .rc file at the moment`. El flujo obligatorio es
   `wrc → .res → winegcc`. Verificado que `winegcc -mwindows -o smoke.exe smoke.c word1.res`
   enlaza correctamente y produce el par `.exe` (stub sh) + `.exe.so` (ELF).
2. **`project(… LANGUAGES RC)` falla en Linux**:
   `No CMAKE_RC_COMPILER could be found.` El preset de Ninja debe declarar sólo
   `C CXX` y manejar `wrc` con `add_custom_command`.

---

## 4. Supuestos LP64 vs LLP64 incrustados en el código

Éste es el punto que no aparece en ningún inventario de "constructos MSVC" y que sin
embargo es el de mayor alcance.

### 4.1 `long` cambia de tamaño

```
$ winegcc -o sz.exe sz.c && ./sz.exe
long=8 LONG=4 DWORD=4 ptr=8 int=4 wchar_t=2
```

Wine hace lo correcto: `LONG` y `DWORD` son de 4 bytes. Pero **`long` desnudo es de 8
bytes**, contra 4 bajo MSVC x64. En el árbol compilado hay **1254 apariciones de `long`
repartidas en 206 archivos**. Los concentradores son
`port/original/opus_asm_native_adapters.cpp` (54), `Opus/CLIPBRD2.C` (37),
`Opus/wordtech/plc.c` (29), `Opus/wordtech/savefast.c` (25), `Opus/wordtech/file.h` (25).

Peor: dos cabeceras redefinen los tipos de Windows con `long` propio:

- `Opus/lib/qwindows.h:119` — `typedef unsigned long DWORD;`
- `OpusEtAl/tools/src/bitapp.h:29` — `typedef unsigned long DWORD;`

`qwindows.h` **sí es alcanzable** desde el motor: `Opus/interp/sym.c:6` hace
`#include <qwindows.h>` y `sym.c` está en la lista de fuentes.

No todas las 1254 apariciones son peligrosas — muchas son variables locales de conteo.
Las que importan son las que aparecen en `struct`s serializadas al formato de archivo
de Word, en las tablas PLC, y en los cálculos de `sizeof`. Requiere auditoría dirigida,
no un `sed` global.

### 4.2 `wchar_t` de 2 bytes rompe libstdc++

winegcc siempre pasa `-fshort-wchar`. libstdc++ y glibc están compilados con
`wchar_t` de 4 bytes. Los símbolos enlazan (el *mangling* de `wchar_t` no depende del
tamaño), de modo que **el error es silencioso en tiempo de enlace y visible sólo en
ejecución**:

```cpp
std::wstring w = L"abc";
std::cout << w.size();     // imprime 175, no 3
```

(verificado ejecutando el binario Winelib resultante)

Alcance: acotado y enteramente dentro de `src/port/`. Los sitios que usan funciones
anchas de la CRT/STL son `opus_win95_chrome.cpp` (13), `opus_word1_ui_test.cpp` (~21),
`opus_original_startup_probe.cpp` (3). El uso de `WCHAR`/`L""` con APIs `…W` de Wine
es correcto y no requiere cambios.

---

## 5. Supuestos del generador de Visual Studio

### 5.1 En `CMakePresets.json`

| Línea | Supuesto | Impacto |
|---|---|---|
| `"generator": "Visual Studio 17 2022"` | Generador multiconfiguración | Los tres presets son inservibles en Linux. Hay que **añadir** presets Ninja sin borrar éstos. |
| `"architecture": {"value":"x64","strategy":"set"}` | Sólo VS/Xcode aceptan `architecture` | Ninja lo ignora o falla. |
| `"CMAKE_INTERMEDIATE_DIR_STRATEGY": "SHORT"` | Variable de caché específica de VS | Inocua. |
| `"binaryDir": "${sourceDir}/../out"` | — | Portable. |
| `buildPresets` con `"configuration": "Debug"/"Release"` | Multiconfiguración | Los presets Ninja necesitan `CMAKE_BUILD_TYPE`. |

### 5.2 En `src/CMakeLists.txt` — bloqueantes duros

1. **Línea 5-7** — `if(NOT WIN32) message(FATAL_ERROR …)`. Bloqueo explícito.
2. **Línea 3** — `LANGUAGES … RC`. Falla en Linux (§3.3).
3. **Líneas 59-64** — `$ENV{ProgramFiles(x86)}` + `${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}`
   para localizar `Windows.h` del Windows Kits SDK, con `FATAL_ERROR` si no existe.
   `CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION` **sólo la define el generador de VS**.
   Alimenta `port/original/opus_windows_sdk.h.in`, cuyo único contenido es
   `#include "@OPUS_WINDOWS_SDK_HEADER@"`.
   El mecanismo es sano: `port/original/windows.h` (con `#pragma once`) hace de
   pantalla para que `Opus/windows.h` no eclipse el SDK moderno, y redirige a
   `opus_windows_sdk.h`. **Para Winelib basta que la plantilla apunte a
   `/usr/include/wine/windows/windows.h`.** Lo verifiqué: con esa sustitución la
   cadena de inclusión funciona (sin ella se produce un ciclo silencioso vía
   `#pragma once` que deja `HMODULE`, `BOOL`, `FARPROC` sin definir).
4. **Líneas 79-89** — `add_custom_command` que invoca
   `powershell -File cmake/GenerateElxStid.ps1`.
5. **Línea 349** — `-P cmake/GenerateMenuHelpHeader.cmake`.

### 5.3 `src/cmake/` no existe

```
$ git log --all -- 'src/cmake/*'      → (vacío)
$ git ls-files | grep -iE '\.(ps1|cmake)$'
src/port/tools/make_win95_toolbar_sprite.ps1
```

**Ni `GenerateElxStid.ps1` ni `GenerateMenuHelpHeader.cmake` existen en ningún commit
de este repositorio.** El build tal como está versionado no puede generarse ni siquiera
en Windows. Esto debe resolverse antes de cualquier trabajo de port — y de paso elimina
la dependencia de PowerShell, porque ambos scripts habrá que reescribirlos.

> **Actualizado tras la Fase 0:** sí existen fuera del repositorio, en un pull request
> no fusionado del upstream. Ver «Nota sobre el origen de los dos scripts» al final.

### 5.4 Supuestos inocuos

`FOLDER`, `VS_GLOBAL_VcpkgApplocalDeps`, `VS_DEBUGGER_WORKING_DIRECTORY` y los bucles
`RUNTIME_OUTPUT_DIRECTORY_${config_upper}` son propiedades ignoradas por Ninja. No
requieren acción y deben conservarse para no romper los presets de VS.

### 5.5 Dos trampas de la línea de órdenes de winegcc

- **`winegcc` descarta `-x`.** Verificado: `winegcc -v -c -x c t0.c` no propaga la
  bandera a gcc. Esto importa porque el motor contiene **17 fuentes con extensión `.C`
  mayúscula** (`CLIPBORD.C`, `CLIPBRD2.C`, `CREATE2.C`, `EDIT.C`, `DDESRVR.C`,
  `DLBENUM.C`, `FILE2.C`, `FIELDCMD.C`, `GRSPEC.C`, `LOADFONT.C`, `PIC2.C`, `RTFIN.C`,
  `RTFOUT.C`, `RTFRARE.C`, `SCREEN2.C`, `SYSCHG.C`, `SPELL.C`), que gcc trata como C++.
  `CMakeLists.txt:670` ya las marca `LANGUAGE C` para MSVC, pero eso sólo elige el
  compilador, no el front-end. Sin remedio, esas 17 TUs generan ~2800 errores.
  La solución practicable es materializar enlaces simbólicos en minúscula dentro del
  árbol de compilación (paso de `src/port/`).
- **`-mno-cygwin` (cabeceras msvcrt) es incompatible con libstdc++.** En ese modo
  `_stricmp`, `_snprintf_s`, `<direct.h>`, `<io.h>` están disponibles, pero
  `#include <string>` falla con conflictos en `<cwchar>`. Como `src/port/` usa la STL
  ampliamente (31 archivos), **hay que quedarse en el modo glibc por defecto y aportar
  los ~6 símbolos de la CRT de Microsoft desde `src/port/`.**
- **`#define native` de `opus_x64_compat.h:135` envenena `<bit>` de libstdc++**
  (`enum class endian { little, big, native }`). Es la causa exclusiva del fallo de
  `opus_asm_file2.cpp`, `opus_asm_filewin.cpp` y `opus_asm_misc.cpp`. El autor ya usó
  el remedio en `port/tools/opus_cabi_tool.cpp:3` (`#undef native`). Lo mismo aplica a
  `#define string` y `#define sys`.

---

## 6. Verificación del entorno

```
$ winegcc --version        gcc (GCC) 16.1.1 20260515 (Red Hat 16.1.1-2)
$ winebuild --version      winebuild version 11.0
$ wrc --version            Wine Resource Compiler version 11.0
$ cmake --version          cmake version 4.3.0
$ ninja --version          1.13.2
$ wine --version           wine-11.0 (Staging)
```

Rutas: `/usr/bin/winegcc`, `/usr/bin/winebuild`, `/usr/bin/wrc`, `/usr/bin/cmake`,
`/usr/bin/ninja`. Paquetes `wine-devel-11.0-3.fc44.x86_64` y `wine-core-11.0-3.fc44.x86_64`.

Cabeceras confirmadas: `/usr/include/wine/windows/` (windows.h, commdlg.h, shellapi.h,
**dbghelp.h**) y `/usr/include/wine/msvcrt/` (direct.h, malloc.h, io.h, process.h).
`dbghelp` importa porque `WORD1` enlaza contra él y
`port/original/opus_original_startup_probe.cpp` usa `SymInitialize`, `SymFromAddr`,
`StackWalk64`, `SymGetLineFromAddr64` y `CaptureStackBackTrace` (13 sitios). Wine
provee la biblioteca; la resolución de símbolos sobre un `.exe.so` ELF es de calidad
incierta, pero es código de diagnóstico y no bloquea.

### 6.1 CMake + Ninja + winegcc funciona sin capa de adaptación

```
$ cmake -G Ninja -DCMAKE_C_COMPILER=winegcc -DCMAKE_CXX_COMPILER=wineg++ -S . -B build
-- Check for working C compiler: /usr/bin/winegcc - works
-- Check for working CXX compiler: /usr/bin/wineg++ - works
$ cmake --build build
[2/2] Linking C executable smoke.exe
    build/smoke.exe      POSIX shell script  (stub)
    build/smoke.exe.so   ELF 64-bit LSB shared object, x86-64
```

Nota: CMake reporta `Detecting C compiler ABI info - failed`, así que no puede deducir
las bibliotecas implícitas de enlace. Conviene un archivo de *toolchain* que lo
suprima. `$<TARGET_FILE:…>` apunta al stub `.exe`, que es ejecutable, de modo que
`ctest` seguirá funcionando sin cambios en las 15 pruebas registradas.

### 6.2 Censo de compilación del motor

Metodología: las 207 fuentes de `add_library(opus_original_engine STATIC …)`,
compiladas individualmente con winegcc/wineg++, con las cabeceras generadas ya
producidas (MKCMD parcheado + BITAPP + DIBAPP), un directorio de compatibilidad de
mayúsculas para las 12 cabeceras de §7, `opus_windows_sdk.h` apuntando a Wine, y
enlaces en minúscula para las 17 fuentes `.C`.

| Configuración | Compilan | Fallan |
|---|---:|---:|
| `-std=gnu89 -funsigned-char` (muestra de 20) | 8 / 20 | 12 |
| `+ -fms-extensions -fpermissive` (misma muestra) | 12 / 20 | 8 |
| `+ -fms-extensions -fpermissive` (árbol completo) | **154 / 207** | 53 |

Los 53 fallos restantes producen **202 errores en total**, concentrados como se
describe en §1.6. Noventa de esos 202 provienen de una única cabecera (`Opus/keys.h`).

---

## 7. Sensibilidad a mayúsculas

Análisis de todas las directivas `#include` del árbol compilado contra el contenido
real del disco:

| `#include` | Archivo en disco | Archivos afectados |
|---|---|---:|
| `debug.h` | `Opus/DEBUG.H` | **202** |
| `screen.h` | `Opus/SCREEN.H` | 111 |
| `rareflag.h` | `Opus/RAREFLAG.H` | 50 |
| `resource.h` | `Opus/RESOURCE.H` | 49 |
| `pic.h` | `Opus/PIC.H` | 29 |
| `rtf.h` | `Opus/RTF.H` | 6 |
| `rtftbl.h` | `Opus/RTFTBL.H` | 4 |
| `spell.h` | `Opus/SPELL.H` | 2 |
| `pic3.c` | `Opus/PIC3.C` | 1 |
| `rtfin2.c` | `Opus/RTFIN2.C` | 1 |
| `saveFast.h` | `Opus/wordtech/savefast.h` | 1 |
| `Windows.h` | `port/original/windows.h` | 1 |

A esto se suman las cabeceras **generadas**: MKCMD emite `IBCM.H`, `RGBCM.H`,
`OPUSCMD2.H`, `OPUSMENU.H`, `MENUHELP.TXT` en mayúsculas, mientras que las fuentes
incluyen `ibcm.h` (3 sitios), `rgbcm.h` (1), `menuhelp.h` (1). Como ese directorio es
territorio del port, se arregla en el paso de generación.

Renombrar en `src/Opus/` violaría la restricción del proyecto. La solución limpia es un
directorio `${CMAKE_CURRENT_BINARY_DIR}/generated/case-shim/` con enlaces simbólicos,
antepuesto a la ruta de inclusión. No toca ni un byte del árbol original.

---

## 8. Plan por fases

Cada fase tiene un criterio de éxito verificable por orden. Ninguna fase empieza sin
que la anterior cumpla el suyo.

### Fase 0 — Reconstruir lo que falta del build (bloqueante)

Escribir `src/cmake/GenerateMenuHelpHeader.cmake` (a partir de `MENUHELP.TXT` que
MKCMD ya produce) y sustituir `GenerateElxStid.ps1` por un script CMake equivalente que
extraiga la carga de pantalla oculta de `OpusEtAl/tools/src/mergeelx.c` hacia
`elxinfo.h`. Referencia: `src/port/original/elxinfo.h` versionado muestra el formato
esperado. Sin PowerShell.

> **Éxito:** `cmake -S src -B out --preset x64-debug` avanza más allá de la resolución
> de dependencias en una máquina Windows *o* la configuración Ninja de la Fase 2 no
> reporta reglas faltantes. Ambos scripts existen y son invocables desde `cmake -P`.

### Fase 1 — Herramientas de host en Linux

Parche mínimo y guardado de `mkcmd.c` (5 sitios de *cast as lvalue*, `#ifdef __GNUC__`)
más un `#define _stricmp strcasecmp` aportado desde `src/port/`. Envolver BITAPP para
que reciba rutas relativas desde un `WORKING_DIRECTORY`, y diagnosticar el fallo de EOF
como el problema `unsigned long` de `bitapp.h:29`.

> **Éxito:** `mkcmd`, `mkdlg`, `mergeelx`, `bitapp`, `opus_dibapp_tool` compilan con
> `gcc`/`g++` nativos y, ejecutados, producen byte a byte las mismas 9 cabeceras de
> comandos, 52 cabeceras BITAPP, 7 cursores y 37 `.hb` de DIBAPP que ya sé que produce
> la cadena parcial. Comparación con `sha256sum` contra una ejecución de referencia.

### Fase 2 — Andamiaje CMake/Ninja para Winelib

Presets `linux-winelib-debug` / `linux-winelib-release` con Ninja. Un archivo de
*toolchain* que fije `winegcc`/`wineg++`, elimine `RC` de `project()` cuando
`NOT WIN32`, y sustituya la puerta `FATAL_ERROR` de la línea 5 por una que acepte
Winelib. Localización del SDK: `opus_windows_sdk.h.in` apuntando a Wine. Directorio de
compatibilidad de mayúsculas (12 enlaces) y enlaces en minúscula para las 17 fuentes
`.C`. Banderas del motor: `-std=gnu89 -funsigned-char -fms-extensions -fpermissive`.
Los presets de VS quedan intactos.

> **Éxito:** `cmake --preset linux-winelib-debug` configura sin errores, y
> `ninja opus_x64_runtime` enlaza la biblioteca estática. Los presets `x64-debug` /
> `x64-release` siguen presentes y sintácticamente válidos.

### Fase 3 — Compilar el motor

Cerrar los 186 errores restantes en 51 archivos (cifra tras la Fase 0; ver la sección
«Fase 0 — ejecutada el 2026-08-09»). Prioridad por rendimiento: primero `Opus/keys.h`
(90 errores de un solo golpe), luego los 21 *cast as lvalue*, luego las ~22
discordancias de prototipos K&R, y la discordancia de tipos de
`Opus/elxprocs.c:82,89` que dejó pendiente la Fase 0. Los `#undef native` /
`#undef string` en los tres
`.cpp` de `port/original/` son de `src/port/`, sin justificación necesaria. Todo cambio
en `src/Opus/` se aísla con `#if defined(__GNUC__) && !defined(_MSC_VER)` y se registra
en `docs/port-linux/01-cambios-en-opus.md` con su justificación.

> **Éxito:** `ninja opus_original_engine` produce `libopus_original_engine.a` con las
> 207 TUs. El recuento de líneas modificadas dentro de `src/Opus/` y `src/OpusEtAl/`
> es inferior a 60 y cada bloque está guardado.

### Fase 4 — Enlace, recursos y exportaciones

Generar un `.spec` de winebuild con las 427 exportaciones a partir de la misma pasada
de MKCMD que hoy produce `opuscmd_native.inc` (misma fuente de verdad, un emisor
adicional). Compilar `port/word1.rc` con `wrc` en un `add_custom_command` y pasar el
`.res` al enlace.

> **Éxito:** existe `WORD1.exe.so` como ELF x86-64, y `WORD1.exe` como stub ejecutable.
> `winedump`/`nm` muestran los 427 símbolos `Cmd*` exportados. Un arranque
> instrumentado confirma que `ResolveCommandAddress("CmdHelp")` devuelve un puntero no
> nulo.

### Fase 5 — Auditoría LP64

Recorrer los usos de `long` que participan de estructuras serializadas, tablas PLC y
cálculos de `sizeof`: `Opus/wordtech/plc.c`, `savefast.c`, `file.h`, `CLIPBRD2.C`,
`port/original/opus_asm_native_adapters.cpp`, y la redefinición de `DWORD` en
`qwindows.h:119`. Sanear la barrera de cadenas anchas de libstdc++ en los ~5 archivos
de `src/port/`.

> **Precedente concreto, no advertencia genérica:** `OpusEtAl/tools/src/bitapp.h:29`
> ya rompió en la práctica por este patrón exacto — `typedef unsigned long DWORD;` sin
> condicionar por arquitectura, 4 bytes bajo MSVC x64 (LLP64) pero 8 bajo GCC x64
> (LP64). No fue una deducción teórica: al ejecutar BITAPP en la Fase 1, `sizeof(BITMAP)`
> pasó de 14 a 18 bytes, `fread()` leyó 4 bytes de más en cada recurso, y 46 de 51
> cabeceras de mapa de bits salieron corruptas antes de fallar con «Unexpected End Of
> File». Se corrigió allí (`DWORD` → `uint32_t` sólo bajo `OPUS_X64_TOOL` + GCC, MSVC
> intacto) — ver la sección «Fase 1» más abajo. `Opus/lib/qwindows.h:119` es la misma
> construcción textual (`typedef unsigned long DWORD;`), en el árbol que sí importa
> tocar con más cuidado por su alcance (`Opus/interp/sym.c` la alcanza). Buscar primero
> cualquier campo `DWORD`/`long` desnudo que participe de un `fread`/`fwrite` o de un
> `sizeof` comparado contra una constante de formato de archivo — ese fue exactamente
> el mecanismo de falla en BITAPP, y es más probable que se repita en `plc.c` y
> `savefast.c` (tablas PLC serializadas) que en código que sólo cuenta o itera.

> **Éxito:** una prueba de aserciones estáticas nueva en `src/port/` verifica el
> `sizeof` de las estructuras de formato de archivo de Word contra los valores del
> Win16 original, y pasa bajo Winelib. La suite `opus_x64_runtime_test` y
> `opus_original_plc_test` corren bajo `ctest`.

### Fase 6 — Pruebas y arranque

> **Éxito:** `ctest` ejecuta las 15 pruebas. Las 7 que no dependen de la interfaz
> (`strtbl`, `x64_runtime`, `sttb`, `plc`, `sdm_cab`, `command`, `word1_port_smoke`)
> pasan. Las 8 pruebas de interfaz compilan y se registran; su ejecución requiere
> sesión gráfica y se documenta como tal, no se declara aprobada sin evidencia.

---

## Fase 0 — ejecutada el 2026-08-09

### Entregables

| Archivo | Qué hace |
|---|---|
| `src/cmake/GenerateMenuHelpHeader.cmake` | Reconstruye `menuhelp.h` desde `MENUHELP.TXT`. Define `OPUS_X64_MENU_HELP_STRING(iidstr)`, que `Opus/menuhelp.c:287` referencia y que ninguna cabecera del árbol definía. |
| `src/cmake/GenerateElxInfoHeader.cmake` | Reconstruye `elxinfo.h` extrayendo la carga de pantalla oculta de `OpusEtAl/tools/src/mergeelx.c`. Sustituye a `GenerateElxStid.ps1`. Sin PowerShell. |
| `src/CMakeLists.txt` (líneas 79-89) | Único cambio: la invocación de `powershell -File …GenerateElxStid.ps1` pasa a `${CMAKE_COMMAND} -P …GenerateElxInfoHeader.cmake`, y su `DEPENDS` deja de nombrar un archivo inexistente. |

`src/Opus/` y `src/OpusEtAl/` no fueron tocados.

### Verificación del criterio de éxito

**«Ambos scripts existen y son invocables desde `cmake -P`.»** Sí. Ambos se ejecutan
de forma independiente y producen sus cabeceras. Se comprobaron además cuatro rutas de
error: `INPUT` ausente, `INPUT` inexistente, entrada sin bloque STID, y línea de datos
corrupta en `MENUHELP.TXT`. Las cuatro abortan con un mensaje accionable en lugar de
degradar en silencio — lo que importa en `MENUHELP.TXT`, donde descartar una línea
desplazaría todos los índices `iidstr` posteriores.

**«La configuración Ninja de la Fase 2 no reportará reglas faltantes por causa de estos
dos archivos.»** Verificado con un banco de pruebas que reproduce literalmente las dos
reglas `add_custom_command` del `CMakeLists.txt` real, configurado con `-G Ninja`:
ambas cabeceras se generan y una segunda corrida informa `no work to do`. Se ejecutó
también un **control negativo**: apuntando la regla al `.ps1` ausente, Ninja falla con
`missing and no known rule to make it` — es decir, la prueba detecta exactamente el
fallo que se quería descartar. La escritura condicional de ambos scripts preserva la
marca de tiempo cuando el contenido no cambia, de modo que tocar `mergeelx.c` no
arrastra recompilaciones aguas abajo ni deja la regla permanentemente sucia.

### Fidelidad de la extracción, comprobada por ejecución

El bloque STID se contrastó contra el MERGEELX real: se compiló `mergeelx.c` con su
`main` neutralizado y un controlador que llama a `WriteElxInfo()` directamente. El
`ELXINFO.H` resultante y el generado por el script tienen **102 líneas cada uno y son
idénticos byte a byte salvo la única línea documentada**:

```
< csconst char rgksp [] = StringMap("SUPO", 0, 1);
> csconst char rgksp [] = "SUPO";
```

`StringMap` era una extensión del compilador C de 16 bits y no es un inicializador
estático válido en C nativo; es la misma traducción que el autor del port ya aplicó en
`Opus/renum.c:77`, `Opus/FILE2.C:560` y `Opus/automcr.h:15`. Que la forma correcta sea
`sz` (y no la contada) se confirmó descifrando la carga: `sizeof(rgksp)` es 5, el
disparador es `'S'` y el acorde `'U' 'P' 'O'`, coherente con el bucle de
`Opus/eldlg.c:890-893`. Aplicando el XOR `'9'` de `eldlg.c:907` sobre `mpstiderc` con
los desplazamientos de `rgstid` se obtiene texto legible
(`* Microsoft Word for Windows *`), lo que valida la transcripción de las tres tablas y
el alineamiento de los índices, no sólo su copia literal.

Para `menuhelp.h`, las 208 cadenas producidas coinciden exactamente con las que genera
el script del upstream (ver la nota siguiente).

### Efecto medido sobre la compilación del motor

Mismo censo y mismas banderas de §6.2:

| | Compilan | Fallan | Errores |
|---|---:|---:|---:|
| Antes de la Fase 0 | 154 / 207 | 53 | 202 |
| Después de la Fase 0 | **156 / 207** | 51 | **186** |

Los dos módulos que pasan a compilar son exactamente `Opus/menuhelp.c` y
`Opus/eldlg.c`, los dos consumidores de estas cabeceras. Sin regresiones.

`Opus/elsubs.c` **sigue fallando**, pero pasa de 9 errores a 1. Los 8 que desaparecen
eran símbolos no declarados (`rgeldi`, `rgichName`, `rgchElkNames`, `mpelkistName`); el
que queda es una discordancia de tipos preexistente en el código de Microsoft —
`Opus/elxprocs.c:82` declara `int far * pist` mientras MERGEELX emite
`csconst unsigned mpelkistName []`, y `elxprocs.c:89` resta ambos punteros. MSVC lo
tolera; GCC no. Es un elemento ordinario de la Fase 3 y toca `src/Opus/`, fuera del
alcance de esta fase.

### Nota sobre el origen de los dos scripts

**Sí existieron fuera del repositorio, y fue posible determinarlo.**

- Dentro de este repositorio no hay rastro: `git log -S` sobre todas las referencias
  sitúa las menciones únicamente en el commit inicial `a1c4a1f` (una importación
  aplanada de 1113 archivos), y ningún blob de la historia contiene los archivos. No
  existe configuración de CI de ningún tipo — ni `.github/`, ni YAML, ni AppVeyor, ni
  Azure Pipelines.
- El upstream `jmarshall23/msword` tiene su `main` en `ac5472e`, **el mismo SHA que el
  HEAD local**, de modo que su árbol publicado tampoco los contiene.
- Sin embargo, el pull request **#3** de ese repositorio, abierto y sin fusionar,
  aporta precisamente `src/cmake/GenerateElxStid.ps1` (189 líneas) y
  `src/cmake/GenerateMenuHelpHeader.cmake` (104 líneas), más un stub de enlace en
  `src/port/original/opus_x64_runtime_test.cpp`.

La conclusión es que **el árbol publicado nunca compiló tal como está**, y que la
corrección estaba propuesta pero no integrada. Es un patrón que este repositorio ya
había mostrado: el commit `ac5472e` («Added missing file.») añadió a mano
`opus_word1_ui_test.cpp`, otro archivo que el `CMakeLists.txt` referenciaba y que la
importación inicial había dejado fuera.

Lo que **no** fue posible determinar es si el autor original disponía de estos scripts
en su máquina antes de que el PR los propusiera.

Consecuencias para el trabajo entregado:

1. La versión PowerShell se ejecutó y se comparó. Mi generador de `menuhelp.h` produce
   las **mismas 208 cadenas, byte a byte**. La extracción del bloque STID coincide en
   enfoque, en la traducción de `StringMap` y en las comprobaciones de los cuatro
   símbolos.
2. El PR resolvía algo que yo había descartado por considerarlo fabricación de datos:
   emitir tablas EL **neutras** bajo `#ifdef elkAppMac`, dimensionadas por
   `elxdefs.h` y con contenido cero, rodeando el miembro flexible de `ELDI` con un
   registro de igual disposición y una macro `#define rgeldi`. Es correcto y no inventa
   datos de diálogo. **Lo incorporé**, con atribución en el encabezado del script; de
   ahí la mejora de `elsubs.c` de 9 errores a 1.
3. También adopté del PR la comprobación de rango en `OPUS_X64_MENU_HELP_STRING`.
4. Se conservan dos divergencias deliberadas: el generador de `elxinfo.h` es CMake y no
   PowerShell (requisito del port), y la tabla de ayuda se tipa como `CHAR` en lugar de
   `char`, porque `CchCopySz` recibe `const CHAR *` y con GCC la diferencia produce
   aviso de puntero incompatible. Verificado: ambas cabeceras compilan sin avisos.

**Queda a su criterio** si prefiere tomar el PR #3 del upstream tal cual en lugar de
esta implementación. Mi recomendación es conservar la versión CMake, porque el
requisito «sin PowerShell» es del port y el PR no cambia esa dependencia; pero la
decisión es suya y el PR es la propuesta del autor original.

Un detalle para la Fase 5: la carga STID incluye `csconst long rgrgb []`, que en LP64
ocupa 8 bytes por entrada frente a 4 bajo MSVC. Se emite con fidelidad a MERGEELX. Los
valores caben en 32 bits y los sitios de uso (`Opus/eldlg.c:1132,1166`) convierten a
`COLORREF`, así que funciona; queda anotado para la auditoría de §4.1.

---

## Fase 1 — ejecutada el 2026-08-09

### Alcance y entregables

Las cinco herramientas de host —`mkcmd`, `mkdlg`, `mergeelx`, `bitapp`,
`opus_dibapp_tool`— compilan y se ejecutan como binarios Linux nativos, con `gcc`/`g++`
directos (sin CMake todavía; el andamiaje Ninja es Fase 2).

| Archivo | Cambio | Alcance |
|---|---|---|
| `src/OpusEtAl/tools/src/mkcmd.c` | 5 sitios de *cast as lvalue* reemplazados por las macros `OPUS_POSTINC_READ`/`OPUS_POSTINC_WRITE`, definidas bajo `#if defined(__GNUC__) && !defined(_MSC_VER)` con reproducción exacta del idioma original de Microsoft para MSVC. Un `#include "opus_host_compat.h"` guardado igual. | `src/OpusEtAl/`, aislado |
| `src/OpusEtAl/tools/src/mkdlg.c` | Un `#include` guardado + `#define strcmpi _stricmp`, mismo patrón que ya usaba `mkcmd.c` para el mismo hueco de la CRT de Microsoft. | `src/OpusEtAl/`, aislado |
| `src/OpusEtAl/tools/src/bitapp.h` | `DWORD` pasa a `uint32_t` bajo `#if defined(OPUS_X64_TOOL) && defined(__GNUC__) && !defined(_MSC_VER)`; MSVC conserva `unsigned long` sin cambios. | `src/OpusEtAl/`, aislado |
| `src/port/tools/opus_host_compat.h` | Nuevo. Define `_stricmp` como `strcasecmp` para GCC. | `src/port/` |

`src/Opus/` no fue tocado. `src/OpusEtAl/mergeelx.c` y `opus_dibapp_tool.cpp` no
necesitaron ningún cambio: compilan y corren tal cual.

### Un problema no anticipado: `mkdlg.c` tenía el mismo hueco de `_stricmp` que `mkcmd.c`, con otro nombre

El plan aprobado sólo mencionaba `_stricmp` en `mkcmd.c`. Al enlazar `mkdlg` apareció
`referencia a 'strcmpi' sin definir`: `mkdlg.c:718` llama a `strcmpi()` directamente,
confiando en que el CRT de MSVC lo declare como alias obsoleto de `_stricmp()` — cosa
que hace sin que el código fuente lo pida. GCC no tiene ninguno de los dos nombres. Es
el mismo hueco de la CRT de Microsoft, en otra grafía. Se resolvió con el mismo patrón
que ya usaba `mkcmd.c` (`#define strcmpi _stricmp`), y `opus_host_compat.h` quedó
diseñado para exponer únicamente el primitivo real de Microsoft (`_stricmp`), dejando
que cada archivo mapee su propia grafía sobre él — así se evita que dos archivos que
incluyen la misma cabecera compartida se pisen la macro `strcmpi` entre sí.

### Un bug real encontrado y corregido: `bitapp.h:29`, no sólo diagnosticado

La Fase 0 había señalado `typedef unsigned long DWORD;` (`bitapp.h`, entonces línea 29)
como sospechoso pero sin confirmar. Al ejecutar BITAPP de verdad sobre los 51 recursos
de mapa de bits, **46 de 51 fallaron** con `Unexpected End Of File Reached in Input
file!`, produciendo además datos corruptos (no vacíos) antes de fallar — una regresión
silenciosa peligrosa si no se hubiera detectado por código de salida.

Causa confirmada: `struct BITMAP` (`bitapp.h:40-51`) serializa `bmBits` como `DWORD`,
campo Win16 de 4 bytes que el código siempre descarta. Bajo MSVC x64 (LLP64), `long`
sigue siendo de 4 bytes y el `typedef` es exacto. Bajo GCC x64 (LP64), `long` es de 8
bytes: `sizeof(BITMAP)` pasaba de 14 a 18 bytes, y `fread(&bm, sizeof(BITMAP), 1,
fpOrig)` leía 4 bytes de más en cada recurso, desalineando todo lo que venía después.

Corrección: `DWORD` pasa a `uint32_t` sólo bajo `OPUS_X64_TOOL` **y** GCC no-MSVC — el
build MSVC x64 (Windows) queda byte por byte sin cambios, porque ahí el bug no existe.

**Verificación independiente, sin depender de BITAPP mismo:** se re-leyeron los 46
`.bmp` con Python usando `struct.unpack` directo sobre los bytes crudos del archivo
(`<hhhhBB` en el desplazamiento 2, es decir el layout Win16 real de 14 bytes), y se
comprobó que `2 (firma) + 14 (cabecera) + bmWidthBytes×bmHeight` coincide **exactamente**
con el tamaño de cada uno de los 46 archivos — cero discrepancias. Esta prueba no usa
BITAPP en absoluto; deriva la corrección desde el formato de archivo documentado en el
propio código de `DumpBitmapParameters`. Se hizo lo mismo para los 5 íconos/cursores de
figura (`RCI`, 12 bytes, todos campos `short`, sin `DWORD` — nunca tuvieron el bug) y
para los 7 cursores de `Opus/resource/*.cur`, confirmando que el consumo de bytes de
`DumpFigureParameters` + dos llamadas a `DumpBits` (máscaras AND/XOR) no excede el
tamaño de archivo en ninguno de los 12 casos.

### Por qué la comparación sha256 contra la Fase 0 no cubre BITAPP igual que el resto

Al revisar qué había quedado en el directorio de reconocimiento de la Fase 0, resultó
que **no era una referencia válida para BITAPP**: contenía 38 archivos con nombres
truncados que no coinciden con ninguno de los 58 nombres reales que exige
`CMakeLists.txt` (p. ej. `8iparal3.hb` en vez de `8paralig.hg`), y el único nombre que
sí coincidía (`8hdr.hb`) resultó ser precisamente una salida generada por el mismo
build defectuoso — 1418 bytes de datos corruptos, no la salida correcta. Se confirmó
reconstruyendo un binario `bitapp` desde el `bitapp.h` anterior al parche de esta fase
(`git show HEAD:...`) y ejecutándolo contra el mismo `.bmp`: produce el mismo archivo
corrupto de 1418 bytes, byte a byte idéntico al que había en el directorio de
reconocimiento. Es decir, la Fase 0 nunca llegó a generar una ejecución de referencia
limpia para BITAPP — sólo para MKCMD, MENUHELP y el bloque STID de MERGEELX, donde la
comparación sha256 sí es válida y se reconfirmó sin regresión (ver más abajo).

Por eso, para BITAPP la verificación de esta fase no es "coincide con la Fase 0" sino
las tres pruebas independientes ya descritas: derivación byte a byte desde el formato
de archivo documentado (arriba), y reproducibilidad determinista (abajo).

### Verificación del criterio de éxito

**9 cabeceras de comandos (MKCMD + `menuhelp.h`):** las 9 —`opuscmd.h`, `IBCM.H`,
`RGBCM.H`, `OPUSCMD2.H`, `OPUSMENU.H`, `MENUHELP.TXT`, `opuscmd.asm`,
`opuscmd_native.inc`, `menuhelp.h`— dan **sha256 idéntico** contra la ejecución de
referencia de la Fase 0. El parche de los 5 sitios *cast as lvalue* no alteró un solo
byte de la salida.

**58 archivos de BITAPP (51 cabeceras + 7 cursores):** los 58 se generan sin fallos
(antes del fix: 46/51 cabeceras de mapa de bits fallaban). Verificados por derivación
independiente desde el formato de archivo crudo (arriba) y por reproducibilidad: dos
ejecuciones completas desde binarios independientes dan `diff -rq` vacío.

**37 `.hb` de DIBAPP:** los 37 se generan sin fallos, sin necesidad de ningún cambio en
`opus_dibapp_tool.cpp`. Dos ejecuciones dan `diff -rq` vacío. No hay una herramienta
DIBAPP original de Microsoft con la que comparar — el archivo nunca la incluyó
(§2 de esta misma sección de reconocimiento); `opus_dibapp_tool.cpp` es una
reimplementación del autor del port, no un port de código heredado.

**MKDLG:** compila y enlaza; invocado sin argumentos imprime su mensaje de uso y sale
con código 1, comportamiento correcto. Su ejecución funcional completa (`mkdlg @elx.txt
dlgcheck.h`) sigue bloqueada por la ausencia de los `.elx` de entrada — el mismo hueco
documentado en §3.1, no un problema nuevo de esta fase.

**MERGEELX:** reconstruido y ejecutado con el mismo arnés de verificación de la Fase 0;
el bloque STID que produce coincide con el de `GenerateElxInfoHeader.cmake` salvo la
traducción de `StringMap` ya documentada. Sin regresión.

### Estado del árbol tras esta fase

```
 M src/OpusEtAl/tools/src/bitapp.h   (+15/-0, aislado bajo __GNUC__)
 M src/OpusEtAl/tools/src/mkcmd.c    (+36/-6, aislado bajo __GNUC__)
 M src/OpusEtAl/tools/src/mkdlg.c    (+9/-0,  aislado bajo __GNUC__)
?? src/port/tools/opus_host_compat.h (nuevo)
```

`src/Opus/` sin cambios. Las tres ediciones a `src/OpusEtAl/` están justificadas en
esta sección, aisladas con guardas de preprocesador, y no alteran el binario que
produciría MSVC.

---

## Fase 2 — CERRADA (estado al 2026-08-12)

Ambos criterios de cierre confirmados en vivo, build limpio:
- `ninja opus_x64_runtime` enlaza (criterio original escrito de la fase).
- `ninja -k 0 opus_original_engine`: 0 errores, 0 FAILED,
  libopus_original_engine.a (207 objetos) — condición añadida antes de
  cerrar, alcanzada ya en la sesión del 2026-08-09 (ver "Criterio Fase 3"
  más abajo en este documento) pero nunca comiteada bajo esta etiqueta.

El código de la fase (toolchain-winelib.cmake, OPUS_WINELIB_BUILD,
presets linux-winelib-debug/release) ya estaba presente en main, fusionado
de forma difusa junto con las Fases 4/5 y trabajo posterior de la rama Qt.
Este commit es el cierre administrativo pendiente, no introduce cambios
funcionales nuevos.

## Fase 2 — EN PROGRESO, sin cerrar (estado al 2026-08-09)

**No confundir con las Fases 0 y 1: esta fase no tiene commit todavía.** El árbol de
trabajo tiene cambios reales, verificados contra `ninja` en vivo, pero el criterio de
éxito de la fase («`ninja opus_x64_runtime` enlaza») **sí se cumple**, mientras que
`opus_original_engine` completo queda bloqueado por un problema activo sin resolver.
No se hizo `commit` de nada de esto a propósito: el estado no es estable.

### git status real

```
 M src/CMakeLists.txt
 M src/CMakePresets.json
 M src/port/original/opus_x64_compat.h
 M src/port/original/opus_x64_runtime_test.cpp
?? src/cmake/toolchain-winelib.cmake
```

Los tres commits previos (`e4ad5bd` Fase 0, `5b0d777` Fase 1, `e7eac0d` nota Fase 5)
siguen siendo la última base estable. `build/` y `out/` son directorios de artefactos
de compilación, sin seguimiento, y no forman parte de este inventario.

### Lo que sí está resuelto y verificado en vivo

- **Presets `linux-winelib-debug` / `linux-winelib-release`** en `CMakePresets.json`,
  generador Ninja, vía `src/cmake/toolchain-winelib.cmake` (fija `winegcc`/`wineg++`,
  la marca `OPUS_WINELIB_BUILD`, y fuerza `CMAKE_SIZEOF_VOID_P=8` porque la detección
  automática de ABI de CMake falla contra el stub de shell de `winegcc.exe` — no es
  cosmético como se pensó al escribir el archivo de toolchain, hay que forzarlo).
- **Puerta `FATAL_ERROR` de la línea 5** sustituida por `NOT WIN32 AND NOT
  OPUS_WINELIB_BUILD`.
- **`project(... LANGUAGES C CXX)`** sin `RC`; `enable_language(RC)` sólo bajo
  `WIN32`. `port/word1.rc` y `port/winword.manifest` se excluyen condicionalmente de
  las fuentes de `WORD1` fuera de Windows (se reincorporan en la Fase 4, vía `wrc`).
- **`opus_windows_sdk.h.in`** apunta a `/usr/include/wine/windows/windows.h` bajo
  `OPUS_WINELIB_BUILD`, localizado con `find_path`.
- **Directorio de compatibilidad de mayúsculas** (12 symlinks, `generated/case-shim/`),
  prependido a `OPUS_ORIGINAL_INCLUDE_DIRS`, compartido por todos los targets que ya
  usaban esa variable.
- **Symlinks en minúscula para las fuentes `.C`** — resultaron ser **18, no 17**: al
  ejecutar `add_library` sobre el árbol real, CMake reportó `Cannot find source file:
  Opus/cmd3.c`. El archivo real es `Opus/CMD3.C`; `cmd3.c` (minúscula) es un typo
  preexistente en el `CMakeLists.txt` original (confirmado con `git show ac5472e`),
  invisible bajo NTFS insensible a mayúsculas, que Linux expuso de inmediato. Se
  corrigió la entrada de la lista de fuentes a `Opus/CMD3.C` y se sumó a la lista de
  symlinks en minúscula — y, para no romper MSVC, también a la línea
  `set_source_files_properties(... LANGUAGE C)`, porque bajo MSVC la ortografía en
  minúscula del `CMakeLists.txt` era justamente lo que hacía que ese archivo se
  compilara como C sin necesitar esa propiedad.
- **Banderas del motor** (`-std=gnu89 -funsigned-char -fms-extensions -fpermissive`),
  aplicadas con `$<COMPILE_LANGUAGE:C>` para no alcanzar los ~20 `.cpp` del mismo
  target. **También hicieron falta en `opus_x64_runtime`** (no sólo en
  `opus_original_engine`): ese target tiene dos fuentes `.c` K&R
  (`opus_original_chupper.c`, `opus_x64_layout.c`) que fallaban con
  `-Wimplicit-int` sin ellas.
- **`opus_x64_compat.h`**: tres correcciones reales, no sólo scaffolding —
  1. `#define native` pasa a `#ifndef __cplusplus`, igual que ya estaban guardados
     `export` y `string` en el mismo archivo (mismo patrón preexistente, no una
     invención): sin la guarda, `<bit>` de libstdc++ no compila porque
     `std::endian::native` deja de ser un identificador válido.
  2. `_stricmp` vía `strcasecmp`, igual que en `port/tools/opus_host_compat.h` de la
     Fase 1 pero para el lado runtime/producto (que incluye este header, no aquél).
  3. `_snprintf_s`/`_vsnwprintf_s`/`_TRUNCATE`/`_countof`/`__assume` — la CRT segura de
     MSVC y un intrínseco, ninguno de los dos con equivalente directo en GCC.
     Verificado que los 4 sitios de llamada en todo el árbol usan `_TRUNCATE`, lo que
     permite mapear directamente a `snprintf`/`vswprintf` sin reimplementar el
     parámetro `count`.
- **`src/port/original/opus_x64_runtime_test.cpp`** — un `#include <cstdint>`
  faltante. MSVC lo arrastraba de forma transitoria vía otra cabecera; libstdc++ no
  hace ese mismo arrastre, y `std::uint32_t` (u otro tipo de `<cstdint>`) quedaba sin
  declarar.

**Verificado por reconfiguración y build limpios, dos veces**, no sólo por una
corrida incremental: `rm -rf out/linux-winelib-debug build && cmake --preset
linux-winelib-debug && ninja opus_x64_runtime` produce
`build/lib/Debug/libopus_x64_runtime.a` (18 objetos) sin errores, de forma
reproducible. **El criterio de éxito de la fase para `opus_x64_runtime` está
cumplido.**

También se verificó en vivo (no de memoria) que las tres herramientas de host de la
Fase 1 (`opus_mkcmd_tool`, `opus_bitapp_tool`, `opus_mkdlg_tool`) enlazan bajo este
mismo preset: les faltaba `target_include_directories` hacia `src/port/tools/` (donde
vive `opus_host_compat.h`) y `target_compile_options(... -std=gnu89)` bajo
`if(NOT MSVC)`. Con eso, las tres compilan y enlazan de forma aislada.

### Bloqueador activo: `src/port/tools/opus_cabi_tool.cpp:98`

```cpp
std::ofstream output(directory / file_name, std::ios::trunc);
```

`directory` es `std::filesystem::path`. Bajo `wineg++`, `std::filesystem::path::value_type`
es `wchar_t` (modelo de tipos de Windows, `-fshort-wchar` forzado por winegcc — el mismo
mecanismo documentado en §4.2 de este informe para la STL de libstdc++ y las cadenas
anchas). `std::basic_ofstream<char>` no tiene ningún constructor que acepte un
`const wchar_t*`/`path` de ese tipo por conversión implícita; falla en `<fstream>` con
5 sobrecargas candidatas rechazadas, cada una por una razón distinta — confirmado
compilando el archivo real, no una reproducción reducida.

Es la **única ocurrencia** de `std::ofstream`/`std::ifstream`/`std::fstream` en el
archivo (`grep` confirmado); no hay más sitios del mismo patrón que corregir en el
mismo archivo. Tampoco hay ninguna convención propia ya existente en el resto del árbol
para convertir un `path` antes de pasarlo a un stream —es el único uso de
`std::filesystem` bajo `src/port/` con este problema—, así que la corrección más simple
(forzar `.string()` en el sitio de la llamada) no tiene un precedente que seguir ni que
contradecir.

**Esto no es un problema aislado de una herramienta secundaria.** Cadena de dependencia
confirmada con `ninja -C out/linux-winelib-debug opus_original_engine` real (no
deducida sólo leyendo `CMakeLists.txt`):

```
opus_cabi_tool (falla al compilar)
  → OPUS_GENERATED_CABI_OPEN (cabi/open.hs, custom command, línea ~365)
    → dependencia de la regla que produce opuscmd.h (opus_generated_commands, línea ~407)
      → add_dependencies(opus_original_engine opus_generated_commands ...) (línea ~726)
```

Sin `opuscmd.h`, ningún archivo de `opus_original_engine` compila — el bloqueador de
`opus_cabi_tool` es el bloqueador del motor completo, no de una utilidad prescindible.

### Ruido ya descartado, no tratar como bloqueador del motor

Compilar `opus_original_sttb_test` (target de prueba en C, no el motor) contra el árbol
real muestra **345 errores `-Wimplicit-int`** en `Opus/wordtech/word.h`, todos de la
forma `NATIVE FreeDrs();` sin tipo de retorno explícito (K&R puro). La causa **no** es
un problema nuevo del motor: `opus_original_sttb_test`, `opus_original_plc_test`,
`opus_original_strtbl_test` y el fixture en C de `opus_x64_runtime_test`
(`opus_x64_layout_test_fixture.c`) no tienen ningún bloque `elseif(OPUS_WINELIB_BUILD)`
con `-std=gnu89` — sólo el `if(MSVC)` original. Verificado A/B, en vivo: el mismo
`Opus/wordtech/sttb.c`, compilado a mano con exactamente las mismas banderas que ya
usa `opus_original_engine` (incluidas `-std=gnu89 -funsigned-char -fms-extensions
-fpermissive`), da **cero** errores de este tipo; sin esas banderas, el mismo archivo
también los reproduce. Corresponde arreglarlo en estos cuatro targets de prueba
—mismo patrón `elseif(OPUS_WINELIB_BUILD)` que ya tienen `opus_original_engine` y
`opus_x64_runtime`— pero no bloquea ni contamina el diagnóstico del motor en sí.

### Verificación empírica de la hipótesis de ABI (2026-08-09)

La hipótesis de partida — `winegcc`/`wineg++` define `_WIN32`, lo que activa
`_GLIBCXX_FILESYSTEM_IS_WINDOWS` en los encabezados de libstdc++ y fija
`path::value_type = wchar_t`, mientras que la porción de `std::filesystem` ya
compilada dentro de `libstdc++.so` de Fedora es ELF/POSIX — se sometió a prueba
directa con tres binarios mínimos compilados con `wineg++ -std=c++17`, no se dio por
buena de oficio. Resultado: **confirmada, y más grave de lo que la hipótesis por sí
sola sugería.**

- `sizeof(fs::path::value_type)` = 2 (`wchar_t`): el modelo de encabezados es en
  efecto el de Windows.
- Pero `is_absolute("/tmp/x")` da `false` y `is_absolute("C:\\x")` también da
  `false` con `root_name()`/`root_directory()` vacíos — ninguna de las dos
  semánticas (POSIX o Windows) se aplica de forma consistente. El análisis de la
  ruta está roto, no simplemente "asume Windows".
- **`fs::path::operator/` (el join que usa la línea 98: `directory / file_name`)
  corrompe memoria y cuelga el proceso** (`malloc(): invalid size` seguido de
  `stack overflow` en el unwind de Wine) — reproducido de forma determinista en
  un caso mínimo de 15 líneas, sin relación con `std::ofstream`. La construcción
  simple de un `path` de un solo componente y su `.string()` SÍ son seguras
  (confirmado independientemente).
- **`std::filesystem::create_directories` (línea 119, sin join) también corrompe
  memoria y cuelga el proceso**, con la misma firma de error. Esto es un segundo
  punto de entrada distinto — no textualmente el mismo patrón `std::ofstream` que
  pedía revisar el punto 4 de instrucciones — pero la misma causa raíz: cualquier
  función de `std::filesystem` resuelta fuera de línea en `libstdc++.so`
  (`operator/`, `create_directories`, `directory_iterator` — las tres probadas)
  falla bajo este `wineg++`/`libstdc++`. Sólo lo que resuelve enteramente en el
  encabezado (construcción, `.string()` de un `path` sin componer) se comprobó
  seguro.
- Caso de control que SÍ funciona de punta a punta, sin tocar ningún símbolo
  fuera de línea de `std::filesystem`: construir el `path` del directorio sólo
  para obtener su `.string()`, concatenar con `std::string` normal
  (`dir.string() + "/" + file_name`), y pasar ese `std::string` a
  `std::ofstream`/`std::ifstream`. Ejecutado y verificado con lectura de vuelta
  del contenido escrito, salida limpia (`EXIT=0`), sin corrupción ni en ejecución
  ni al cierre del proceso.

**Consecuencia para el candidato de corrección previamente anotado aquí
(`(directory / file_name).string()`): está descartado.** Esa expresión evalúa
`operator/` antes de `.string()`, y es exactamente `operator/` lo que corrompe
memoria — el candidato original habría cambiado el error de compilación por un
cuelgue en ejecución, no lo habría resuelto.

### Siguiente paso concreto, antes de continuar cualquier otra cosa

El alcance de la corrección es más amplio de lo que el plan original de este punto
preveía (que hablaba sólo de la línea 98 y del patrón `std::ofstream`). Hay dos
sitios en `opus_cabi_tool.cpp`, no uno, y ambos dependen de símbolos rotos de
`std::filesystem` fuera de línea:

1. Línea 98 (`std::ofstream output(directory / file_name, ...)`) — corregible con
   el patrón de control ya verificado: construir el `std::string` de la ruta con
   concatenación simple, sin `operator/`.
2. Línea 119 (`std::filesystem::create_directories(output_directory, error)`) —
   necesita sustituirse por una creación de directorio que no pase por el símbolo
   roto de `libstdc++.so`; candidatos: `mkdir()`/`_mkdir()` de POSIX directo (el
   directorio de salida de `opus_cabi_tool` no tiene anidamiento profundo conocido
   en este árbol, a confirmar antes de asumir que un `mkdir` de un solo nivel
   basta) o una función recursiva propia sobre `mkdir()`.

Los dos comparten causa (evitar todo símbolo de `std::filesystem` resuelto fuera de
línea). La corrección finalmente aplicada va más allá de encapsular la conversión:
elimina `std::filesystem` del archivo por completo. Ver la sección siguiente.

### Cierre de la discrepancia `.string()` / `native()` — regla de plataforma

Hubo una discrepancia entre auditorías sobre si `.string()` sobre un
`std::filesystem::path` era seguro bajo `wineg++`. **Resuelta en contra de
`.string()`**, por una segunda auditoría de Grok Build con probes aislados y
determinismo 3/3: `.string()` es corrupción de heap determinista. (Kimi quedó
descartado en este punto específico.)

Conviene registrar por qué la medición propia hecha aquí no lo detectó, porque el
modo de fallo es instructivo: el probe local `fs_probe4` usó `dir.string()` sobre un
`path` de un solo componente, imprimió el valor correcto y salió con `EXIT=0`. Pero
el probe `fs_probe5`, con el mismo patrón, imprimió resultados correctos y **aun así
abortó al cierre del proceso** con `Inconsistency detected by ld.so: dl-fini.c: 93`.
Es decir: la corrupción ocurre igualmente, sólo que a veces no se manifiesta hasta
después de que la salida ya parece correcta. Una salida limpia no es evidencia de
ausencia de corrupción aquí. La auditoría con probes aislados y repetición 3/3 es la
medida válida; una sola pasada con `EXIT=0` no lo es.

`native()` sobrevive a las pruebas, pero **no es utilizable en la práctica**: bajo
este toolchain devuelve `wstring`/`wchar_t`, y lo que `WriteCabi` necesita es una
ruta en `char` para `std::ofstream`. No existe ningún método de
`std::filesystem::path` que sirva de forma segura para este uso.

Con cuatro mecanismos de conversión independientes ya confirmados rotos
(`operator/`, `.string()`, `directory_iterator`, `create_directories`) y sólo
`native()` y la construcción simple sobrevivientes —ninguno de los dos utilizable
aquí— se registra la siguiente **regla de plataforma**:

> Bajo `wineg++`, `std::filesystem::path` no debe usarse para ninguna operación que
> produzca o consuma una representación de cadena en `char`. La construcción desde
> `const char*` y la comparación estructural pueden ser seguras, pero no hay ningún
> caso de uso real en este árbol para eso. **Regla práctica: no usar
> `std::filesystem` en unidades de traducción compiladas con `wineg++`.**

### Corrección aplicada a `opus_cabi_tool.cpp`

Origen único localizado por Grok Build: la conversión de `argv[1]` (`const char*`) a
`std::filesystem::path` en L117. Eliminada esa conversión, `std::filesystem`
desaparece del archivo entero.

- `#include <filesystem>` eliminado, sustituido por un comentario que explica la
  prohibición y remite a la regla de plataforma de arriba.
- Firma de `WriteCabi`: `const std::filesystem::path& directory` →
  `const std::string& directory`.
- Sitio ~98: `directory + "/" + file_name` con `std::string` normal, a una variable
  local, y esa variable al constructor de `std::ofstream`. Ningún `path` se
  construye en el camino.
- Sitio ~119: `create_directories` eliminado sin sustituto. **No hizo falta ningún
  cambio en CMake**: el `add_custom_command` que invoca la herramienta
  (`src/CMakeLists.txt:366`) ya ejecutaba
  `"${CMAKE_COMMAND}" -E make_directory "${OPUS_GENERATED_CABI_DIR}"` como paso
  previo, en el código original del autor. La creación dentro de la herramienta
  siempre fue redundante; eliminarla deja una sola fuente de verdad en lugar de
  añadir una segunda. No se introdujo `mkdir_p` propio porque no es necesario.
- Verificado con `grep`: no queda ningún uso de `std::filesystem` ni `fs::` en el
  archivo (sólo menciones en comentarios explicativos).

Verificación de ejecución real, no sólo de enlace: el binario, invocado a través de
su stub (`build/tools/Debug/opus_cabi_tool.exe /tmp/cabi_exec_test`), sale con
`EXIT=0`, sin corrupción ni al ejecutar ni al cerrar, y escribe **73 archivos** con
contenido real y correcto (`open.hs` → `#define cabiCABOPEN 268`, `about.hs` →
`#define cabiCABABOUT 1824`, `print.hs` → `#define cabiCABPRINT 1068`). El bloqueador
de `std::filesystem` queda cerrado.

### Bloqueador NUEVO y distinto: sufijo `.exe` frente a `$<TARGET_FILE:>`

Con la herramienta ya funcionando, `ninja -C out/linux-winelib-debug
opus_generated_commands` **sigue fallando, por una causa completamente distinta**:

```
/bin/sh: /home/exia/word1/msword/build/tools/Debug/opus_cabi_tool: No existe el fichero o el directorio
```

`winegcc`/`wineg++` emiten el par `opus_cabi_tool.exe` (stub POSIX) +
`opus_cabi_tool.exe.so` (ELF real). CMake, que no sabe del convenio de dos archivos
de winegcc, expande `$<TARGET_FILE:opus_cabi_tool>` (L367) a la ruta **sin** sufijo,
que no existe. El enlace tiene éxito y la herramienta funciona; lo que falla es
únicamente cómo el custom command la nombra.

Esto afecta por igual a las cinco herramientas host del árbol (`opus_cabi_tool`,
`opus_mkcmd_tool`, `opus_mkdlg_tool`, `opus_bitapp_tool`, `opus_dibapp_tool`), no
sólo a ésta.

**No se aplica corrección aquí.** Las dos salidas plausibles son (a) fijar
`CMAKE_EXECUTABLE_SUFFIX` en el toolchain file, y (b) construir las herramientas host
con el `gcc` nativo en lugar de `winegcc`, con lo que producirían ELF sin sufijo y el
problema desaparecería de raíz. La opción (b) **es exactamente la decisión de
arquitectura pendiente sobre separar herramientas host del target**, que está
reservada explícitamente al responsable del proyecto. Se para aquí en lugar de
improvisar una de las dos.

Obsérvese que estas herramientas son generadores que sólo tienen que ejecutarse en la
máquina de construcción: no hay razón técnica para que pasen por Wine. Eso es un
argumento a favor de (b), pero la decisión no se toma aquí.

### Estado de la cadena, tras esta sesión

`opus_cabi_tool` compila, enlaza y **ejecuta correctamente**. `opus_generated_commands`
sigue bloqueado, por el sufijo `.exe`, no por `std::filesystem`. Por tanto
`generated/original/opuscmd.h` **todavía no se genera**, y los pasos 3 y 4 de la
verificación pedida (contraste de `opuscmd.h` y conteo de errores de
`opus_original_engine`) **no han podido ejecutarse todavía** — no por falta de
intento, sino porque la cadena se corta antes.

No se debe dar la Fase 2 por cumplida ni hacer `commit` hasta que
`opus_original_engine` compile (o hasta acordar explícitamente que el alcance de la
fase se cierra sólo con `opus_x64_runtime`, que es lo único que el plan original
exigía por escrito).

---

## Separación de host tools del toolchain Winelib (2026-08-09)

### Decisión y justificación

Cuatro de las cinco herramientas de construcción pasan a compilarse con el
`gcc`/`g++` nativo, fuera del toolchain Winelib. Son generadores que sólo se
ejecutan en la máquina de construcción: leen fuentes de Opus y emiten
cabeceras/inicializadores que después compila el motor. Compilarlas con
`winegcc` no aportaba nada funcional y las metía gratis en los modos de fallo de
la capa Winelib (el mismo tipo de riesgo que acaba de cerrarse con
`std::filesystem`).

Auditoría de dependencias, hecha por lectura de los `#include` de cada una antes
de mover nada:

| Herramienta | Fuente | Includes | ABI del motor |
|---|---|---|---|
| `opus_mkcmd_tool` | `OpusEtAl/tools/src/mkcmd.c` | `stdio, ctype, stdint, stdlib, string, vk.h, opus_host_compat.h` | no |
| `opus_mkdlg_tool` | `OpusEtAl/tools/src/mkdlg.c` | `stdio, ctype, opus_host_compat.h` | no |
| `opus_bitapp_tool` | `OpusEtAl/tools/src/bitapp.c` | `stdio, stdlib, string, bitapp.h`→`stdint.h` | no |
| `opus_dibapp_tool` | `port/tools/opus_dibapp_tool.cpp` | `cstdint, fstream, iomanip, iostream, string, vector` | no |
| **`opus_cabi_tool`** | `port/tools/opus_cabi_tool.cpp` | `opus_x64_compat.h`→`<windows.h>`, `sdm.h`, 73 `.hs` | **SÍ** |

Ni `vk.h` ni `opus_host_compat.h` ni `bitapp.h` arrastran `<windows.h>`.

### `opus_cabi_tool` no se mueve

Su salida **es** el ABI del motor: evalúa `sizeof` sobre las estructuras del
motor a través de `<windows.h>`, p. ej.

```c
#define cabiCABOPEN Cabi((sizeof(CABOPEN) + sizeof(WORD) - 1) / sizeof(WORD), 1)
```

con `CABOPEN = { CABH cabh; WORD sab; CHAR **hszFile; int iDirectory; BOOL fReadOnly; }`.
Medirlo con un compilador distinto del que compila el motor es exactamente el
error que hay que evitar. Confirmado además que `g++` nativo no lo compila
siquiera: `fatal error: excpt.h: No existe el fichero o el directorio`.

Hallazgo de Grok Build, registrado por ser evidencia real aunque no concluyente:
compilándolo con `g++` nativo más `-I/usr/include/wine/windows` se obtienen los
mismos `sizeof`/macros, tanto en las muestras como en el volcado completo de las
73 `.hs`. No basta para moverlo: ese probe usa los mismos encabezados que
consulta `winegcc`, así que no descarta que algo propio del driver (banderas
implícitas, modelo de convención de llamada, `pragma pack` heredado de
encabezados no incluidos en el probe mínimo) afecte a estructuras no muestreadas.
No se apuesta la corrección del ABI a que alguien recuerde el `-I` correcto en
lugar de usar el compilador diseñado para ello.

### Mecanismo de CMake elegido

`ExternalProject_Add` sobre un `CMakeLists.txt` propio y pequeño en
`src/port/tools/host/`, configurado con `-DCMAKE_C_COMPILER=gcc`
`-DCMAKE_CXX_COMPILER=g++` y **sin** toolchain file, más destinos
`IMPORTED GLOBAL` con los mismos nombres en el proyecto principal. Así los 31
`$<TARGET_FILE:opus_*_tool>` existentes siguen resolviendo sin tocarse.

Lo único que no sobrevive es `DEPENDS`: ninja no puede resolver un `DEPENDS`
sobre la `IMPORTED_LOCATION` de un destino importado (`missing and no known
rule to make it`, reproducido en un proyecto mínimo). Se introducen variables
`OPUS_<TOOL>_TOOL_DEP`, que valen el nombre del sub-build bajo Winelib y el
nombre real de cada destino en el resto de casos — de modo que la ruta MSVC
conserva exactamente las mismas dependencias que antes. Los cuatro bloques
`add_executable` originales quedan intactos dentro de un `if(NOT
OPUS_WINELIB_BUILD)`, así que el build de Visual Studio no cambia en absoluto.

Verificado: los cuatro binarios instalados son ELF nativos
(`ELF 64-bit LSB executable ... interpreter /lib64/ld-linux-x86-64.so.2`), sin
stub `.exe`, y el `CMakeCache.txt` del sub-build registra `/usr/bin/gcc` y
`/usr/bin/g++`. Cero apariciones de `winegcc`/`wineg++` en su `build.ninja`.

### Sufijo `.exe`, corregido en el toolchain file

`CMAKE_EXECUTABLE_SUFFIX` queda vacío bajo este toolchain porque
`CMAKE_SYSTEM_NAME` no es Windows y el build se trata como nativo Linux, de modo
que `$<TARGET_FILE:>` expandía a una ruta sin sufijo inexistente. Reproducido con
un destino CMake mínimo, sin nada específico de `cabi`: es una propiedad del
toolchain, no de un destino. Se fija en `src/cmake/toolchain-winelib.cmake`
(`CMAKE_EXECUTABLE_SUFFIX`, `_C` y `_CXX`), no por destino ni por custom command.

Efecto lateral observado: con el sufijo fijado, la detección de ABI de CMake pasa
a reportar «done» en lugar de fallar. No se ha tocado por ello el forzado de
`CMAKE_SIZEOF_VOID_P`, que puede haber quedado redundante — queda anotado como
posible limpieza futura, no verificado.

### Dos bugs de convención Windows/Linux encontrados al desbloquear la cadena

1. **Profundidad de ruta relativa.** `src/CMakeLists.txt` pasaba a MKCMD
   `-s../../../src/Opus/dlg/`, que asume el `binaryDir` del preset de Visual
   Studio (`out/`). Con `out/<preset>/` hay un nivel más y resolvía a un
   inexistente `out/src`, con el error `opuscmd.cmd:64: cannot open elx.txt
   file`. Sustituido por `-s${CMAKE_CURRENT_SOURCE_DIR}/Opus/dlg/`, absoluto y
   correcto bajo cualquiera de los dos presets.

2. **BITAPP interpreta `/` como switch.** `bitapp.c:59` hace
   `if (*(rgszArg[cCurArg]) == '/') SetFlag(...)`. En Windows las rutas
   absolutas empiezan por letra de unidad, así que nunca colisionaba; en Linux
   una ruta absoluta **es** un `/` inicial, y la herramienta respondía
   `Invalid Switch on command line`. Corregido **en el llamador**, no en la
   herramienta: los dos `add_custom_command` que invocan BITAPP pasan ahora
   rutas relativas al directorio de construcción vía `file(RELATIVE_PATH)`.
   `OpusEtAl/` queda sin tocar.

### Estado de la cadena tras esta sesión

- `opus_cabi_tool` compila, enlaza y ejecuta. Sus 73 `.hs` generadas son
  **byte a byte idénticas** a la corrida previa bajo el stub de Wine
  (`diff -r` limpio), con las mismas muestras: `cabiCABOPEN 268`,
  `cabiCABABOUT 1824`, `cabiCABPRINT 1068`. El cálculo no dependía del
  compilador en estas estructuras.
- `opus_generated_commands` **pasa**. `generated/original/opuscmd.h` se
  regenera con contenido real: 284 líneas, 14 489 bytes, 275 `#define`, con la
  cabecera «This file was created by MKCMD!». Junto a él, `IBCM.H` (11 684 B),
  `RGBCM.H` (2 442 B), `OPUSCMD2.H`, `OPUSMENU.H`, `menuhelp.h` (208 cadenas de
  ayuda).
- `opus_original_engine` avanza de 0 a **más de 300 unidades compiladas**, con
  sólo **2 unidades fallidas y 4 errores reales**. El aluvión de ~345
  `-Wimplicit-int` **no aparece**, como estaba previsto: el motor sí lleva
  `-std=gnu89`.

Los errores reales que quedaban en ese momento, y su estado:

1. ~~`Opus/command2.c:1515: fatal error: rgbcm.h: No existe el fichero`~~ —
   **corregido**, ver «Shim de mayúsculas para cabeceras generadas» abajo.
2. `generated/lowercase-c/clipbrd2.c`, 3 errores: dos
   `too many arguments to function 'lpfn'` (líneas 629 y 1204) y un
   `se requiere un l-valor como operando izquierdo de la asignación` (línea
   783). Misma familia que los `cast-as-lvalue` ya resueltos en `mkcmd.c` en
   Fase 1 con `OPUS_POSTINC_READ`/`OPUS_POSTINC_WRITE`. `CLIPBRD2.C` estaba ya
   en la lista de candidatos LP64/LLP64 a auditar en Fase 5. Toca código
   restringido (`Opus/`), así que requiere decisión explícita y guardas. **En
   espera del veredicto de una auditoría paralela; no se toca.**

### Shim de mayúsculas para cabeceras generadas (2026-08-09)

El shim existente sólo cubría cabeceras del árbol fuente. MKCMD **genera**
`IBCM.H`, `RGBCM.H`, `OPUSCMD2.H` y `OPUSMENU.H` en mayúsculas (y la regla de
copia de `ibcm.rel` también escribe `IBCM.H`), mientras que `src/Opus/` incluye
las cuatro en minúsculas. Verificado por grep, no supuesto: `#include "ibcm.h"`,
`#include "rgbcm.h"`, `#include "opuscmd2.h"`, `#include "opusmenu.h"`.

Sólo `rgbcm.h` había aflorado como error porque `command2.c` fallaba primero;
corregir únicamente ese par habría desplazado el fallo a la siguiente unidad. Se
añaden los cuatro.

Detalle de implementación que conviene no perder: estos enlaces apuntan al
**árbol de construcción**, no al de fuentes, y sus destinos **todavía no existen**
cuando CMake configura — se crean colgantes y se resuelven cuando MKCMD corre.
Por eso la comprobación es `IS_SYMLINK` y no `EXISTS`: `EXISTS` sigue el enlace y
devuelve falso mientras falta el destino, lo que provocaría recrearlo en cada
reconfiguración. `Opus/` no se toca.

Verificado: `command2.c` compila con **0 errores** (objeto de 124 872 bytes).

> ## ⚠ REGLA DE MEDICIÓN — LEER ANTES DE CONTAR ERRORES
>
> **`ninja` usa `-k 1` por omisión y deja de programar trabajo nuevo al primer
> fallo. Cualquier conteo de errores del motor obtenido sin `-k 0` es falso por
> defecto y mide hasta dónde llegó ninja, no cuántos errores hay.**
>
> Usar **siempre** `ninja -k 0 -C out/linux-winelib-debug opus_original_engine`
> para medir. En este árbol la diferencia fue 1 error frente a 182: dos órdenes
> de magnitud. Es un error fácil de repetir y ya se cometió dos veces en esta
> sesión.

### Corrección de método: los conteos de errores anteriores estaban subestimados

**Todos los conteos de errores del motor reportados antes de este punto son
falsos por defecto**, y conviene dejarlo escrito para no volver a caer. `ninja`
usa `-k 1` por omisión: al primer fallo deja de programar trabajo nuevo. Como
`opus_original_engine` tiene 331 pasos, un fallo temprano oculta todo lo que
venía después, y el conteo resultante mide *hasta dónde llegó ninja*, no cuántos
errores hay.

Los números correctos se obtienen con `ninja -k 0`, que sigue hasta agotar el
grafo. Comparación sobre el mismo árbol:

| Medición | Unidades fallidas | Errores |
|---|---|---|
| `ninja` (`-k 1`, por omisión) | 1 | 1 |
| `ninja -k 0` | **43** | **182** |

La discrepancia se detectó porque `disp1.c` "desapareció" de la lista de fallos
sin que nada relacionado hubiera cambiado; al forzar su recompilación aislada
seguía dando sus 14 errores. No era una corrección: ninja simplemente no había
llegado a compilarlo. **Usar siempre `-k 0` para medir el estado del motor.**

### Estado real del motor (`ninja -k 0`, 2026-08-09)

43 unidades fallidas, 182 errores. Familias dominantes:

| Errores | Familia |
|---|---|
| 90 | `el elemento inicializador no es calculable al momento de la carga` — todos en `Opus/keys.h:287+` |
| 24 | `se requiere un l-valor como operando izquierdo de la asignación` (cast-as-lvalue) |
| 12 | `se requiere un l-valor como un operando de incremento` (cast-as-lvalue) |
| 10 | `declaración static de X después de una declaración que no es static` |
| 9 + 4 | `conflicting types for X` |
| 5 | `el argumento X no coincide con el prototipo` |
| 5 | `invalid operands to binary -` |
| 4 + 3 | `too many arguments to function X; expected 0` (misma familia FARPROC que la ya corregida) |

Concentración por archivo, cabeceras incluidas: `keys.h` 90, `disp1.c` 14,
`elcore.c` 6, `style.c` 6, `spell.c` 5, `sdmparse.h` 4, `eldde.c` 4, y una cola
larga de 1–3 errores en ~35 archivos más.

Esto no invalida nada de lo ya corregido —cada corrección se verificó
individualmente y sigue en pie— pero sí redimensiona lo que queda: el motor no
estaba a 4 ni a 17 errores de compilar.

### Estado del motor tras el shim de cabeceras generadas (medición antigua, `-k 1`)

`ninja opus_original_engine`: **2 unidades fallidas, 17 errores**, todos de la
misma familia `cast-as-lvalue` / puntero a función K&R, y todos en código
restringido `Opus/`:

- `generated/lowercase-c/clipbrd2.c` — 3 errores (629, 783, 1204).
- `Opus/disp1.c` — 14 errores (798, 799, 800, 852, 904, 959, 974, 1045, 1046,
  1072, 1073, 1249, 1250, 1722), casi todos
  `se requiere un l-valor como operando izquierdo de la asignación`, más un
  `se requiere un l-valor como un operando de incremento` en la 800.

`disp1.c` aparece ahora porque el shim desbloqueó las unidades que se compilaban
antes que ella; es un hallazgo nuevo, no una regresión. Pertenece a la misma
familia que `clipbrd2.c` y al mismo conjunto restringido, así que **queda igual a
la espera de la auditoría paralela**: no se le escribe ninguna guarda todavía.

### Llamadas a FARPROC por ordinal en CLIPBRD2.C (autorizado, corregido)

`CLIPBRD2.C` alcanza `CreateDIBitmap` (L629) y `GetDIBits` (L1204) por ordinal a
través de `GetProcAddress`, sin enlazarlas, porque Opus todavía tenía que correr
bajo Windows 2 — el motivo está escrito por Microsoft en ambos sitios de llamada.
El `FARPROC` de MSVC es un `int (WINAPI *)()` sin prototipo, así que llamarlo con
argumentos reales compila y los pasa. El de Wine tiene prototipo estricto
`INT_PTR (WINAPI *)(void)`, de ahí
`too many arguments to function 'lpfn'; expected 0, have 6/7`; y su retorno
`INT_PTR` además estrecharía implícitamente a `HBITMAP` (L629) y a `int` (L1204).

Corregido **sin tocar la lista de argumentos ni la intención de la llamada**:
`typedef` locales con la firma real de cada API, y macros que castean `lpfn` a
ese tipo antes de llamar, cubriendo argumentos **y** tipo de retorno. La rama
`#else` expande exactamente a la expresión original `(*(lpfn))`, de modo que el
build MSVC no cambia.

```c
typedef HBITMAP (WINAPI *OPUS_PFN_CREATEDIBITMAP)(
	HDC, const BITMAPINFOHEADER *, DWORD, const void *,
	const BITMAPINFO *, UINT);
typedef int (WINAPI *OPUS_PFN_GETDIBITS)(
	HDC, HBITMAP, UINT, UINT, LPVOID, LPBITMAPINFO, UINT);
```

Guardadas con `#if defined(__GNUC__) && !defined(_MSC_VER)`, según lo
autorizado.

**Por qué locales a `CLIPBRD2.C` y no en `port/original/opus_x64_compat.h`:** se
intentó primero en el encabezado compartido y **rompió `CREATE2.C`**
(`nombre de tipo 'BITMAPINFOHEADER' desconocido`). Varias unidades de traducción
de Opus se compilan contra un encabezado de Windows reducido —`CREATE2.C` define
`NOHDC`, `NOBRUSH`, `NOCOLOR`, `NORASTEROPS`…— donde los tipos GDI no existen.
Un `typedef` que los nombre no puede vivir en un encabezado que ven todas. La
opción de `typedef` local estaba explícitamente autorizada y es además la
correcta aquí. El intento fallido queda registrado por ser justamente la clase de
regresión que un conteo parcial habría ocultado.

Verificado: los dos errores `too many arguments` desaparecen. `clipbrd2.c` baja
de 3 errores a 1, y el que queda es exactamente el reservado a la auditoría
paralela (`CbFromChrm()`, ahora en la línea 812 tras la inserción de las
guardas, antes 783). `command2.c` sigue compilando con 0 errores.

### Presets de Visual Studio: verificación explícita

Requisito pendiente desde el enunciado de Fase 2. `CMakePresets.json` parsea como
JSON válido y contiene los seis presets (`x64`, `x64-debug`, `x64-release`,
`linux-winelib`, `linux-winelib-debug`, `linux-winelib-release`). El `git diff`
del archivo es **sólo altas**: ninguna línea de los presets `x64` fue eliminada ni
modificada. `cmake --list-presets` en Linux muestra únicamente los dos presets
Linux porque el generador «Visual Studio 17 2022» no existe en esta plataforma;
no es una regresión introducida aquí.

---

## Apéndice: qué no está en el árbol

Para evitar que se busque lo que no existe:

- `src/cmake/GenerateElxStid.ps1`, `src/cmake/GenerateMenuHelpHeader.cmake` — nunca
  versionados en este repositorio; existen en el PR #3 del upstream y fueron
  reconstruidos en la Fase 0 (ver «Nota sobre el origen de los dos scripts»)
- `src/Opus/dlg/*.elx` — ninguno; `elx.txt` lista 86 que no están
- Archivos `.dlg` — ninguno en todo el repositorio
- El ejecutable del Dialog Editor que convertía `.des` → `.elx`
- DIBAPP original de Microsoft (sustituido por `port/tools/opus_dibapp_tool.cpp`)

---

## Triage estructurado de los 182 errores del motor (2026-08-09)

Mapa previo a cualquier corrección. **No se propone ni se aplica ningún fix
aquí.** Medido con `ninja -k 0`; 43 unidades fallidas, 182 errores.

### Familia 1 — `initializer element is not computable at load time` (90 errores)

Todos en `Opus/keys.h`, expandidos desde macros de tabla de teclado hacia los
`.c` que las instancian (`iconbar3.c:710` es un sitio de expansión típico).

Sitio representativo, `Opus/keys.h:287`, expandido en
`Opus/iconbar3.c:710` dentro de `csconst KME rgKmeHdrIconBar[]`:

```c
#define rgKmeHdrIBDef \
	{ kcTab,	ktFunc, IBTab },		\
	{ kcReturn,	ktFunc, IBReturn    },		\
	...
```

Con `KME` definido en `Opus/wordwin.h:451`:

```c
typedef struct _kme
	{
	unsigned kc : 12;
	unsigned kt : 3;
	union {
		int w;		/* generic */
		BCM bcm;	/* ktMacro */
		...
		PFN pfn;	/* ktFunc */
		...
		};
	} KME;
```

**Qué es exactamente.** El inicializador por llaves de C89 sin designadores
inicializa **el primer miembro de la unión**, que es `int w`. Las filas
`ktFunc` pasan ahí un puntero a función (`IBTab`, de tipo `void (*)()`). GCC lo
dice en dos pasos, y el primero es el diagnóstico revelador:

```
warning: la inicialización de ‘int’ desde ‘void (*)()’ crea un entero desde un
         puntero sin una conversión [-Wint-conversion]
note: (cerca de la inicialización de ‘rgKmeHdrIconBar[0].<anónimo>.w’)
error: el elemento inicializador no es calculable al momento de la carga
```

**Respuesta a la pregunta planteada: no es inicialización diferida en runtime, y
no es un guard de compatibilidad.** La tabla es `csconst`, está ordenada por
`kc` para búsqueda y se consume como dato constante; su intención siempre fue
ser de tiempo de carga. Lo que falla es el tamaño del puntero: en Win16 un
puntero a función *near* medía 16 bits y cabía en el miembro entero de la unión;
en Win32 medía 32 bits y seguía cabiendo en `int`, así que MSVC lo aceptaba como
constante de tiempo de carga. En x86-64 el puntero mide 64 bits, no cabe en
`int`, y la dirección deja de ser una constante calculable en tiempo de carga
para ese miembro. Es por tanto **la misma clase de defecto LP64/LLP64 que ya
rompió `BITAPP` en Fase 1** (`bitapp.h:29`), no una variante sintáctica.

**Homogeneidad: total, verificada, no estimada.** De las 90 líneas de `keys.h`
que producen error, **90 son filas `ktFunc` y 0 son filas `ktMacro`**
(comprobado extrayendo cada número de línea del log y clasificándolo contra el
fuente). El criterio de discriminación es directo y barato: **las filas
`ktMacro` inicializan el miembro entero con un `bcm`, que es un entero y no da
error; sólo las `ktFunc` meten un puntero a función.** Es decir, la familia es
mecánica y de un solo patrón, y la propia etiqueta `kt` de cada fila dice a qué
grupo pertenece sin necesidad de mirar nada más.

### Familia 2 — cast-as-lvalue (24 asignación + 12 incremento = 36 errores)

Sitio representativo, `Opus/disp1.c:798-800`:

```c
			(char *) pchr = (char *)*vhgrpchr + bchrCur;
			(char *) pchp = (char *)*vhgrpchr + bchpCur;
			((struct CHRT *)pchr)++;
```

Extensión de MSVC (cast como lvalue) que GCC nunca aceptó. Misma familia que los
cinco sitios ya resueltos en `mkcmd.c` en Fase 1 con
`OPUS_POSTINC_READ`/`OPUS_POSTINC_WRITE`.

**Homogeneidad: sintácticamente uniforme, semánticamente NO.** El síntoma es
idéntico en los 36 sitios, pero ya hay constancia de que la causa difiere:
`clipbrd2.c:812` se confirmó que necesita `CbFromChrm()` y no una guarda
sintáctica. `disp1.c` opera sobre el mismo walk de `vhgrpchr` bajo el modelo de
tags `OPUS_X64`.

**Criterio rápido para separarlos:** mirar el operando, no la forma. Si el cast
es a un tipo del mismo ancho y el puntero recorre un buffer plano, es
mecánico. **Si el cast interviene en aritmética sobre `vhgrpchr` o sobre
cualquier estructura cuyo tamaño cambie entre el modelo de 16 bits y
`OPUS_X64`** —`CHRT`, `CHP`, `CHR`— entonces el avance en bytes ya no coincide
con `sizeof` y hace falta la función de conversión de tamaño, no una macro. Los
sitios de `disp1.c` y `clipbrd2.c` caen en el segundo grupo por construcción.

### Familia 3 — `static declaration after non-static declaration` (10 errores)

Sitio representativo, `Opus/initwin.c`:

```c
/* uso en la línea 807, antes de cualquier prototipo */
	if (!FRegisterWnd ())
...
/* definición en la línea 948 */
STATIC BOOL NEAR FRegisterWnd()
```

Diagnóstico de GCC: `previous implicit declaration of 'FRegisterWnd' with type
'int()'`. C89 crea una declaración implícita `extern int()` en el punto de uso;
la definición posterior la contradice al ser `static`.

**Homogeneidad: alta, mecánica.** Es un artefacto puro de C K&R (usar antes de
declarar), sin componente de ABI ni de ancho de tipo. Los 10 sitios están
concentrados en 5 archivos: `initwin.c` (2), `spell.c` (5), `syschg.c` (2),
`wwact.c` (1).

**Criterio rápido:** si la nota de GCC dice `previous implicit declaration`, es
este caso y es mecánico. Si en cambio señala una declaración **explícita** en
una cabecera, entonces hay dos declaraciones reales en desacuerdo y pertenece
en realidad a la Familia 4.

### Familia 4 — `conflicting types` (13 errores)

Sitio representativo, `Opus/ourmath.h:126` contra `Opus/el.h:32`:

```c
/* ourmath.h:126 */
LONG LWholeFromNum ();
/* el.h:32 */
long LWholeFromNum();
```

GCC: `have 'LONG()' {también conocido como 'int()'}` frente a
`previous declaration ... with type 'long int()'`.

**Esta es la familia LP64/LLP64 pura**, la que el plan de Fase 5 anticipaba:
bajo el modelo Windows `LONG` es de 32 bits (`int`), mientras que `long` en
Linux LP64 es de 64. En Windows ambas grafías coincidían y el desacuerdo era
invisible.

**Homogeneidad: media — hay que mirar cada una.** El síntoma agrupa dos causas
distintas: (a) desacuerdo real `LONG`/`long` por LP64, y (b) declaraciones K&R
sin prototipo que chocan con un prototipo posterior (p. ej. `HeliNew`, que
aparece en tres archivos distintos, y `RtError` dos veces en `elinit.c`).

**Criterio rápido:** si los dos tipos en conflicto son `LONG`/`long`,
`DWORD`/`unsigned long` o equivalentes de ancho, es LP64 y se trata como
`bitapp.h:29`. Si difieren en número o tipo de **argumentos**, es K&R contra
prototipo y es otra cosa. Sitios: `HeliNew` ×3, `RtError` ×2, `LWholeFromNum`,
`FDlgAbout`, `CpNextVisiInOutline`, `CpFirstNonBlank`, `FHelp`, `MathError`,
`OurSetCursor`.

### Familia 5 — FARPROC llamado con argumentos (7 errores restantes)

Misma familia ya corregida en `CLIPBRD2.C` (L629/L1204): `FARPROC` de Wine tiene
prototipo estricto `INT_PTR (WINAPI *)(void)`.

Sitios restantes, todos con nombre de variable revelador del API destino:

| Archivo | Línea | Variable |
|---|---|---|
| `dlgmisc.c` | 2337 | `lpfnSetSpeed` |
| `eldde.c` | 1289, 1295, 1299 | `lpfn` |
| `filecvt.c` | 506 | `lpfnInitConvtr` |
| `grspec.c` | 1424, 1586 | `lpfnGetInfo`, `lpfnReadPict` |
| `print2.c` | 1479 | `lpfnDevMode` |
| `quit.c` | 359 | `lpfn` |

**Homogeneidad: uniforme en mecanismo, individual en firma.** Todos requieren
exactamente el mismo tratamiento aplicado en `CLIPBRD2.C` —typedef con la firma
real y cast antes de llamar, cubriendo argumentos y retorno— pero **la firma hay
que averiguarla caso por caso**, y no todas son APIs públicas de Windows:
`lpfnInitConvtr`, `lpfnGetInfo` y `lpfnReadPict` apuntan a DLLs de conversión y
de filtros gráficos propias de Word, cuyo contrato hay que leer en el código.

**Criterio rápido:** si el `GetProcAddress` que produce el `lpfn` resuelve contra
`GDI`/`USER`/`KERNEL`, la firma está en el SDK. Si resuelve contra una DLL de
Word (conversores, filtros), la firma hay que deducirla del propio árbol.

### Cola larga — conteo por archivo

`keys.h` 90 · `disp1.c` 14 · `style.c` 6 · `elcore.c` 6 · `spell.c` 5 ·
`sdmparse.h` 4 · `eldde.c` 4 · `ffread.c` 3 · `elsubs2.c` 3 · `syschg.c` 2 ·
`select.c` 2 · `print2.c` 2 · `ourmath.h` 2 · `opus_asm_filewin.cpp` 2 ·
`inssubs.c` 2 · `initwin.c` 2 · `grspec.c` 2 · `exp.c` 2 · `etcmd.c` 2 ·
`elinit.c` 2 · `dialog3.c` 2 · `dialog2.c` 2 · `wwact.c` 1 · `wproc.c` 1 ·
`tabs.c` 1 · `spelcore.c` 1 · `sdm.h` 1 · `raremsg.c` 1 · `quit.c` 1 ·
`pagevw.c` 1 · `opus_asm_misc.cpp` 1 · `opus_asm_file2.cpp` 1 · `mathapi.c` 1 ·
`help.c` 1 · `formula.c` 1 · `format.c` 1 · `filecvt.c` 1 · `fieldcr.c` 1 ·
`elxprocs.c` 1 · `edmacro.c` 1 · `dlgmisc.c` 1 · `clipbrd2.c` 1 ·
`about.sdm` 1

Nota: `opus_asm_file2.cpp`, `opus_asm_filewin.cpp` y `opus_asm_misc.cpp` están
en `src/port/`, no en código restringido. `about.sdm` y `sdmparse.h`/`sdm.h`
pertenecen a la capa SDM.

---

## Ronda de correcciones cast-as-lvalue y LP64 (2026-08-09)

Medido siempre con `ninja -k 0`. **182 → 141 errores**, 43 → 30 unidades
fallidas.

### `src/port/` — 4 errores, tres causas distintas

- `opus_asm_filewin.cpp` (2): `std::size` vive en `<iterator>`; MSVC lo
  arrastra transitivamente, libstdc++ no. Misma clase que el `<cstdint>` que
  faltaba en `opus_x64_runtime_test.cpp`.
- `opus_asm_misc.cpp` (1): su `index()` chocaba con el `index()` de POSIX que
  glibc declara en `<strings.h>` (alcanzado vía `<windows.h>` → `<string.h>`)
  con distinta constancia del parámetro y enlace C. La semántica es idéntica,
  así que en este toolchain se usa el del sistema y la traducción se omite;
  MSVC, que no tiene `index()`, conserva la definición.
- `opus_asm_file2.cpp` (1): `<direct.h>` es un encabezado del CRT de MSVC sin
  equivalente aquí. Sólo usaba `_getdcwd` y `_chdir`, sustituidos por
  `GetCurrentDirectoryA`/`GetFullPathNameA` y `SetCurrentDirectoryA` — que es
  lo que el resto del archivo ya usaba (`GetFileAttributesA`,
  `CreateDirectoryA`).

### Familia cast-as-lvalue — 36 → 2

`disp1.c` (14) y los 21 restantes reescritos al mismo patrón: se saca el cast
del lado izquierdo y el resultado se asigna directo, con el tipo real de la
variable.

```c
/* antes */                          /* después */
(char *) pchr = (char *)*vhgrpchr + bchrCur;
                                     pchr = (struct CHR *)((char *)*vhgrpchr + bchrCur);
(char *)pchr += cbCHRV;              pchr = (struct CHR *)((char *)pchr + cbCHRV);
((struct CHRT *)pchr)++;             pchr = (struct CHR *)((struct CHRT *)pchr + 1);
*((int *) pb)++ = 0;                 *(int *) pb = 0;
                                     pb = (CHAR *)((int *) pb + 1);
```

**Sin guardas de preprocesador, deliberadamente.** La reescritura es C
estándar y compila igual bajo MSVC; envolver 34 sitios en `#if/#else`
duplicaría el código y dejaría la forma no estándar viva en la rama MSVC. Se
deja constancia aquí por ser una desviación de la pauta general de guardas.

Casos que no eran aritmética de punteros y se trataron aparte:

- `etcmd.c:221,222`, `spelcore.c:157`: el cast `(int)` era redundante sobre un
  campo ya entero (`int uSynList`, `unsigned uSplMMSuggList`); se asigna
  directo al campo.
- `tabs.c:213`: `vptdsd` es la macro `((TDSD *) pcmb->pv)`; se asigna directo a
  `pcmb->pv`, que es el campo real (`void *`).
- `inssubs.c:1687`: **el cast sí cumplía una función.** `w` es `int`, y
  `(uns)w >>= 1` fuerza un desplazamiento *lógico*; sin él, un `w` negativo
  daría desplazamiento aritmético y el bucle `for (isprm = 0; w != 0; ...)` no
  terminaría. Reescrito como `w = (int)((uns)w >> 1)`, preservando la
  semántica sin recurrir al cast como lvalue.

Quedan 2, ambos reservados: `clipbrd2.c:812` (pendiente de `CbFromChrm()`) y
`exp.c:1498` (ver abajo).

### `LWholeFromNum` — corrección de una premisa

El criterio recibido situaba la definición real en `el.h:32`. **`el.h:32` es
una declaración, no la definición**: hay cuatro declaraciones (`el.h:32`,
`ourmath.h`, `elfile.c:1053`, `elmisc.c:20`, más una local en `exp.c:1495`),
todas `long`, y **la definición está en `mathapi.c:1001`, declarada `LONG`** —
es decir, la desviada era la definición, no sólo la declaración vieja.

Aplicado el criterio dado (prevalece el ancho mayor; truncar a 32 bits
perdería resultados legítimos del intérprete): `ourmath.h` y `mathapi.c` pasan
a `long`. La variable local que la función devuelve ya era `long`.

### `FHelp` — verificado seguro y aplicado

El conflicto no era con una declaración de cabecera sino con una **declaración
implícita**: `help.c:211` la llama antes de definirla en `help.c:316`.

Sobre el ancho del segundo parámetro, verificado en los consumidores como se
pidió: sólo hay dos sitios de llamada, ambos en `help.c`
(`FHelp(cmd, (LONG) cxt)` y `FHelp(cmdQuit, 0L)`), ambos pasan valores
numéricos pequeños, sin unión, sin cast a un tipo más angosto y sin aritmética
de bits sobre el valor. Y hacia dentro, `FHelp` pasa `ulData` a `HFill`, que
**lo castea a `LPSTR`** (`CchLpszLen((LPSTR)ulData)`,
`CchCopyLpsz(..., (LPSTR)ulData)`): en esa rama el valor es un puntero, así que
en este destino **tiene que ser de ancho de puntero**. `unsigned long` (8
bytes) es por tanto lo correcto, no un problema a corregir. Añadido el
prototipo adelantado que coincide con la definición.

### `exp.c:1498` — DETENIDO, es un (b) real, no un (a)

```c
	*((long *) pwArgs)++ = LWholeFromNum(&numT, TRUE);
```

Verificado el consumidor antes de tocar nada, como se pidió. La cadena es:

```c
	int  rgwArgs [celpMax];       /* exp.c:1427 */
	int *pwArgs = rgwArgs;        /* exp.c:1456 */
	...
	cwArgs = pwArgs - rgwArgs;    /* exp.c:1536 -- cuenta en unidades de int */
	lT = LPushMacroArgs(hpdkd->lppasproc, rgwArgs, cwArgs);
```

Y el lector, en `port/original/opus_asm_misc.cpp:80`, **pasa cada `int` del
arreglo como un parámetro independiente**:

```cpp
template <std::size_t... Index>
long invoke_macro(void* procedure, const int* arguments,
                  std::index_sequence<Index...>) {
    using Procedure = long(__cdecl*)(std::conditional_t<true, int, ...>...);
    return reinterpret_cast<Procedure>(procedure)(arguments[Index]...);
}
```

Es decir: el lector **asume una ranura de un `int` por argumento** y construye
la firma de la llamada a partir de `cwArgs`. No reensambla un `long` a partir
de dos ranuras consecutivas.

Consecuencia: históricamente (`long` de 4 bytes) esta escritura ocupaba **una**
ranura y `cwArgs` crecía en 1, de modo que el procedimiento del lenguaje de
macros recibía un parámetro. Aquí `long` mide 8 bytes: ocupa **dos** ranuras,
`cwArgs` crece en 2, y el lector genera una llamada con **dos parámetros
`int`** en lugar de uno. Compila limpio y rompe la ABI de invocación.

**No se ha tocado.** Mover el cast sin más cambiaría el error de compilación
por una llamada mal formada en ejecución. La decisión pendiente es si el
empaquetado debe seguir usando ranuras de 32 bits (y entonces esta escritura
ha de partir el valor, o el arreglo dejar de ser de `int`), o si el lector debe
aprender anchos por argumento.


---

## Sesión Grok Build (2026-08-09) — ejecución motor

Medido siempre con `ninja -k 0 -C out/linux-winelib-debug opus_original_engine`.

### Reconciliación inicial (git, no el documento)

- **No** había proceso Claude/ninja paralelo sobre el build tree.
- Baseline al arrancar esta sesión: **141 errores**, 30 unidades fallidas.
- Los siete conflicting-types autorizados al cierre de la sesión Claude
  (**HeliNew ×3, RtError ×2, FDlgAbout, CpNextVisiInOutline, CpFirstNonBlank,
  MathError, OurSetCursor**) **no** estaban en el árbol: `git diff` no los
  mostraba; el log de ninja sí los listaba. (Sí estaban aplicados
  `LWholeFromNum`, `FHelp`, cast-as-lvalue 34/36, port/ y CLIPBRD2 FARPROC.)

### 1. Siete conflicting-types — aplicados (141 → 131)

Patrón: `#if defined(__GNUC__) && !defined(_MSC_VER)`; LP64 o K&R-vs-prototipo.

| Símbolo | Sitio | Fix |
|---|---|---|
| HeliNew ×3 | `elsubs2.c:569`, `edmacro.c:1748`, `eldde.c:1432` | Omitir `extern ELI ** HeliNew();` bajo GCC (`el.h:653` ya prototipa) |
| RtError ×2 | `interp/elinit.c:34`, definición ~422 | Omitir redecl K&R; definición en forma prototipo `VOID RtError(RERR)` |
| FDlgAbout | `help.h:229` (bajo `NOABOUT`) | Forma prototipo que coincide con `about.sdm` |
| CpNextVisiInOutline | `fieldcr.c` | Forward `HANDNATIVE CP ...(int,int,CP)` fuera de solo-DEBUG |
| CpFirstNonBlank | `formula.c` | Forward `NATIVE CP ...(int,CP)` |
| MathError | `mathapi.c` | Forward `EXPORT FAR PASCAL MathError(int)` |
| OurSetCursor | `wproc.c` | Forward `EXPORT PASCAL OurSetCursor(HCURSOR)` |

Verificado: esas 10 menciones dejan de aparecer en el log; **131 errores**.

### 2. Familia 1 — `keys.h` / KME (131 → 41)

Raíz en `wordwin.h` unión de `KME`, no en cada fila de `keys.h`.

```c
/* wordwin.h, primer miembro de la unión */
#if defined(__GNUC__) && !defined(_MSC_VER)
long w;   /* pointer-width en LP64 */
#else
int w;
#endif
```

Las 90 filas `ktFunc` inicializan el **primer** miembro de la unión con un
puntero a función. Con `int` (32 bits) GCC rechaza la constante de carga; con
`long` (64 bits en LP64) las 90 desaparecen de un golpe. Las filas `ktMacro`
siguen cargando un entero en los bits bajos (LE). MSVC conserva `int`.

**Nota de tamaño:** `cwKME` / `sizeof(KME)` crecen bajo este toolchain. La
asignación en memoria vía `HAllocateCw(... * cwKME)` se actualiza sola. La
serialización a fichero (`openrare.c` y afines) es tema de Fase 5 / LP64, no
de esta ronda.

Verificado: 0 errores `keys.h` / «inicializador no calculable»; **41 errores**.

### 3. Familia 3 — static después de no-static (10)

Forwards `static`/`STATIC` antes del primer uso, tras includes que definen
`STATIC` (`debug.h`):

- `initwin.c`: `FRegisterWnd`, `FRegisterWinInfo`
- `SYSCHG.C`: `GetAMPMFromProfile`, `FInvalidCharSetting`
- `SPELL.C`: `FUpdateDictOK`, `FUserDictOK`, `FTryDict`, `FCreateDict`, `SpellDllFree`
- `wwact.c`: `BSBPwwd`
- `elsubs2.c`: `void ModeError(void)` a nivel de archivo (la decl. de bloque en
  ~243 sale de alcance y la llamada en ~593 inventaba `int ModeError()`)

### 4. Familia 5 — FARPROC restantes (9)

Mismo patrón que `CLIPBRD2.C`: `typedef` + macro de cast local, guardas GCC.

| Archivo | Variable / API | Firma usada en el cast |
|---|---|---|
| `dlgmisc.c` | `lpfnSetSpeed` (KEYBOARD ordinal) | `void (WINAPI *)(int)` |
| `print2.c` | `lpfnDevMode` (driver) | `int (WINAPI *)(HWND, HANDLE, LPSTR, LPSTR)` |
| `eldde.c` | Alloc/PrestoChango/FreeSelector (KERNEL) | `(HANDLE)`, `(HANDLE,HANDLE)`, `(HANDLE)` |
| `filecvt.c` | `INITCONVERTER` | `HANDLE (WINAPI *)(HANDLE, HWND)` |
| `GRSPEC.C` | GetInfo / ReadPict (filtros) | 4 args cada uno; typedefs a **ámbito de archivo** |
| `quit.c` | ExitWindows V3 (USER) | `void (WINAPI *)(LPSTR)` |

### 5. SDM prototipos WORD vs int (Familia 4 residual)

- `lib/sdmparse.h`: `WParseIntRange` / `WParseOptRange` — últimos bounds pasan a
  `int` bajo GCC (coinciden con `dialog2.c` / `dialog3.c`; signed necesario
  para `wNinch`).
- `lib/sdm.h`: `FRetrySdmError` primer arg `int` bajo GCC (`raremsg.c`).

Tras 3–5: **41 → 11 errores**, 6 unidades fallidas.

### 6. Lo que NO se tocó (restricción + decisión pendiente)

**Reservados (cast-as-lvalue):**

- `CLIPBRD2.C:812` — `(char *)pchr += pchr->chrm`; pendiente de `CbFromChrm()`
  (auditoría previa).
- `interp/exp.c:1498` — **no tocado**; ver recomendación abajo.

**Familia nueva — resta de punteros incompatibles (9 errores), NO aplicada.**

GCC exige operandos de tipo puntero compatible; MSVC era laxo. No hay patrón
autorizado caso por caso. Sitios:

| Archivo | Línea | Operandos |
|---|---|---|
| `interp/elcore.c` | 1358, 1359, 1395, 1422, 1425, 1469 | `char *` ↔ `BYTE *` (`Global(rgchBuf)`) |
| `elxprocs.c` | 89 | `int *` − `const unsigned *` (`mpelkistName`) — ya citado en plan Fase 0/3 |
| `wordtech/inssubs.c` | 652 | `char *` − `struct FKP *` |
| `wordtech/pagevw.c` | 1380 | `struct DR *` − `char *` (`PInPl`) |

Fix mecánico probable (pendiente de autorización): castear ambos lados a
`char *` (o al tipo del array) antes de restar. **No aplicado en esta sesión.**

### 7. Recomendación `exp.c:1498` (propuesta, no ejecución)

Contexto ya cerrado en la sesión Claude: el escritor empaqueta un `long` (8 B)
en `int rgwArgs[]` y avanza `pwArgs` dos ranuras; el lector
`invoke_macro` en `port/original/opus_asm_misc.cpp:80` pasa **una ranura `int`
por argumento**.

| Opción | Qué cambia | Trade-off |
|---|---|---|
| **(a) Escritor parte el long** | En `exp.c` (o wrapper en port), escribir el valor como dos `int` de 32 bits (p. ej. lo/hi) y avanzar `pwArgs` en +2 de forma explícita y documentada; `cwArgs` sigue contando ranuras `int`. `invoke_macro` **no cambia**. | Toca `src/Opus/interp/exp.c` (restringido) → necesita autorización de caso. Preserva la ABI del lector y el modelo «todo es ranura int» del intérprete Win16/Win32. Hay que fijar endianness de las dos mitades y revisar si hay **otros** empaquetados `long`/`NUM` en el mismo arreglo. Runtime-correcto si el callee histórico recibía un solo `long` de 32 bits: en ese caso partir a dos `int` **rompe** al callee a menos que el callee también espere dos words (modelo Win16 de long en dos words). En Win32 un `long` era **una** ranura de 32 bits; aquí el bug es que 8 bytes ocupan dos ranuras de `int` y el contador miente. La variante (a) correcta para ABI Win32 es: **almacenar el valor en 32 bits** (truncar o `LONG`) en **una** ranura y avanzar +1 — no partir en dos. |
| **(b) Lector con anchos por argumento** | Extender `LPushMacroArgs` / `invoke_macro` para conocer el layout de cada argumento (int vs long/pointer) y reensamblar. | Cambia solo `src/port/` (preferible por política de árbol). Más trabajo: hace falta la firma real de cada `lppasproc` (tabla de macros / EL). Más correcto a largo plazo si hay varios anchos, pero no hay tabla de anchos hoy. |

**Recomendación:** preferir una variante de **(a) orientada a ABI Win32 de una ranura**: escribir
`*(LONG *)pwArgs = (LONG)LWholeFromNum(...); pwArgs += 1;` (o equivalente sin
cast-as-lvalue), de modo que `cwArgs` crezca en 1 y el lector siga pasando un
`int`/`LONG` de 32 bits por parámetro de valor entero del lenguaje de macros.
Eso reproduce el ancho histórico del valor (32 bits), no el `long` LP64 de 64.
Si algún callee necesita 64 bits de verdad, entonces **(b)** o ranuras
etiquetadas.

**No ejecutar** hasta autorización explícita sobre `exp.c` (y confirmación de
que los enteros del lenguaje de macros siguen siendo 32 bits).

### Conteo de cierre (ninja -k 0)

| Momento | Errores | Unidades fallidas |
|---|---|---|
| Inicio sesión Grok | 141 | 30 |
| Tras 7 conflicting-types | 131 | — |
| Tras KME/`keys.h` | 41 | — |
| Tras static + FARPROC + SDM | **11** | **6** |

Las 6 unidades: `clipbrd2.c`, `elxprocs.c`, `elcore.c`, `exp.c`, `inssubs.c`,
`pagevw.c`.

## Estado al cierre de sesión (2026-08-09, Grok Build)

- **Aplicado y verificado con diff + `ninja -k 0`:** 7 conflicting-types;
  KME `long w` (90 de keys.h); 10 static-after-non-static + ModeError; 9
  FARPROC; prototipos SDM int. Motor: **141 → 11** errores.
- **Decisión abierta:** `exp.c:1498` — recomendación arriba (preferir escritura
  en una ranura `LONG` de 32 bits + avance +1; alternativa (b) en port si hay
  anchos mixtos). No tocado.
- **Familia nueva sin autorización:** 9× resta de punteros incompatibles
  (`elcore`, `elxprocs`, `inssubs`, `pagevw`). Documentada; no aplicada.
- **Reservados previos:** `clipbrd2.c:812` (`CbFromChrm`).
- **Dónde retomar:** (1) autorizar y aplicar casteo mecánico en las 9 restas;
  (2) autorizar `exp.c:1498` o el arreglo en `opus_asm_misc.cpp`; (3)
  `CbFromChrm` para clipbrd2. Tras eso, `ninja -k 0` debería poder cerrar el
  motor. Sin commit.


---

## Sesión Grok Build #2 (2026-08-09)

Medido siempre con `ninja -k 0 -C out/linux-winelib-debug opus_original_engine`.
Sin commit. `exp.c:1498` y `clipbrd2.c:812` no tocados.

### 1. Nueve restas de punteros — autorizadas y aplicadas (11 → 2)

Baseline al arrancar: **11 errores**, 6 TUs. Tras el fix: **2 errores**, 2 TUs
(`clipbrd2.c:812`, `exp.c:1498`). Cero `invalid operands to binary -`.

Cada sitio verificado en el árbol antes del cast (no se asumió un tipo único):

| Sitio | Operandos reales | Cast bajo `__GNUC__ && !_MSC_VER` |
|---|---|---|
| `interp/elcore.c` 1358, 1359, 1422, 1425, 1469 | `char *` − `BYTE *` (`Global(rgchBuf)` es `BYTE rgchBuf[cchTokenBuf]` en `priv.h:219`) | segundo operando → `(char *)Global(rgchBuf)` |
| `interp/elcore.c` 1395 | `BYTE *` (`&Global(rgchBuf)[cchTokenBuf]`) − `char *pchBufStart` | primer operando → `(char *)&Global(rgchBuf)[cchTokenBuf]` |
| `elxprocs.c:89` | `int far *pist` − `csconst unsigned mpelkistName[]` (elxinfo.h) | base → `(int far *)mpelkistName` (elemento 4 B en ambos) |
| `wordtech/inssubs.c:652` | `char HUGE *hpch` − `struct FKP HUGE *hpfkp` | base → `(char HUGE *)hpfkp` (offset en bytes; `hpch` ya se formó así en L642) |
| `wordtech/pagevw.c:1380` | `struct DR *pdr` − `char *` (`PInPl` declarado `NATIVE char *PInPl()`) | base → `(struct DR *)PInPl(hpldr, 0)` (índice en unidades DR; mismo patrón que otros call sites que castea el retorno) |

Rama `#else` = expresión original. MSVC no cambia.

### 2. Investigación `exp.c:1498` — evidencia del árbol (sin ejecución)

#### Qué valor llega ahí

El sitio **no** es un camino genérico del intérprete. Está dentro del `switch (dkt)`
que empaqueta argumentos de una **llamada a procedimiento externo** (`sytDkd` /
DKD), justo antes de `LPushMacroArgs(hpdkd->lppasproc, rgwArgs, cwArgs)`:

```c
case dktLong:
{
    extern long LWholeFromNum();
    NUM numT;
    BLTBH(&hpev->num, &numT, cbNUM);
    *((long *) pwArgs)++ = LWholeFromNum(&numT, TRUE);
}
```

Cadena:

1. WordBasic `Declare ... Lib "foo" ... (x As Long) ...` se parsea en
   `interp/main.c` (`DktFromElt(eltLong) → dktLong`, ~1210–1213).
2. Sin `As`, el sufijo del nombre decide: `$`→string, `%`→int, **default→double**
   — **nunca** long. `dktLong` exige `As Long` explícito.
3. `DeclDkd` hace `LoadLibrary` + `GetProcAddress` y guarda el `FARPROC` en
   `DKD.lppasproc` (`el.h:581`) y los tipos en `rgdkt[]` (`el.h:584`).
4. En la llamada, cada arg se resuelve a `elvNum` cuando `dkt == dktLong`
   (`ElvFromDkt`: long y double → `elvNum`), se copia `hpev->num` a `numT`, y
   `LWholeFromNum(&numT, TRUE)` extrae la parte entera (truncando, no redondeando).

**Conclusión de tipo de valor:** es un **entero de interop DLL** etiquetado
WordBasic `Long` / Windows `LONG` de 32 bits en el producto original, no un
entero arbitrario del heap del intérprete. El payload numérico del EL es `NUM`
(doble del math pack); el paso a entero es solo en la frontera del Declare.

#### ¿Puede exceder 32 bits en uso real?

Evidencia en `LWholeFromNum` / `ourmath.h`:

- Comentario de la función: si `*pnum` no cabe en `long`, el retorno es
  **indefinido**; si `wExp > cDigLong` → `MathError(fmerrOver)`.
- `#define cDigLong 10` con comentario explícito: *«A number of digits in
  0x7FFFFFFF»* — el techo de dígitos se dimensionó contra el máximo **positivo
  de 32 bits**, no de 64.
- Bucle de acumulación con `if (l < lPrev) goto lblOvrflw` (wrap en el ancho de
  `long` de la plataforma).

Implicaciones:

| Plataforma | `long` | Comportamiento para enteros grandes |
|---|---|---|
| Win32 histórico | 32 bit | Overflow → MathError vía wrap o `cDigLong` |
| Linux LP64 tras ensanche | 64 bit | Hasta 10 dígitos (~10¹⁰) caben en `long` sin wrap; valores entre 2³¹ y ~10¹⁰ que **fallaban en Win32** ahora **pueden devolverse** |

En uso real de `As Long` hacia DLLs Win16/Win32/Wine, el callee espera un
**LONG de 32 bits** en el stack stdcall/PASCAL. Un valor >32 bits no es un
«resultado legítimo de macros» en esta frontera: es un valor que el producto
original rechazaba o truncaba al ancho de `long` de 32 bits. No hay en el árbol
una tabla de Declares de producto con `As Long` y rangos documentados; el
mecanismo es 100 % dinámico (usuario + `GetProcAddress`).

Otros llamadores de `LWholeFromNum` (ensanche justificado en otra sección):

| Sitio | Uso |
|---|---|
| `elfile.c:1054` | posición de fichero (`DwSeekDw`) — sí se beneficia de >32 bit en LP64 |
| `elmisc.c:1800,1803` | tiempo / tolerancia |
| `elmisc.c:836` | flags de MessageBox (bajo) |
| `elmisc2.c:412` | tamaño de punto tipográfico (rango 8–254) |
| `fieldpic.c:204` | inserta número; el destino `CchInsertLongNfc` toma `LONG` |
| `fltexp.c:2715` | solo el bit bajo (`& 1`) |
| **`exp.c:1498`** | **solo** empaquetado `dktLong` hacia DLL |

Re-estrechar **globalmente** `LWholeFromNum` a 32 bits reintroduciría el
problema en `elfile` y afines. El ensanche y este sitio **no son el mismo
contrato**.

#### Lado lector: ¿hay firmas de `lppasproc`?

- Declaración: `long (FAR PASCAL * lppasproc)();` — **sin prototipo de args**.
- Valor: `GetProcAddress` opaco; **cero** tabla estática de firmas en el árbol.
- La única metadata de anchos es runtime: `DKD.rgdkt[i]` (`dktInt` / `dktLong` /
  `dktDouble` / `dktString`), hasta `celpMax` (16) args.
- `LPushMacroArgs` / `invoke_macro` (`opus_asm_misc.cpp:80`) **ignoran**
  `rgdkt`: tratan cada ranura de `int` como un parámetro `int` independiente.
  No reensamblan `long` ni punteros.

Históricamente en Win32, `long` ≡ `int` ≡ 32 bit → una ranura por `dktLong` y el
lector acertaba por accidente. En LP64, `long` = 8 B → dos ranuras y el lector
inventa dos `int`.

#### Recomendación revisada (sigue sin ejecutarse)

La opción **(a) ingenua** de la sesión #1 («escribir `LONG` y punto») es
**incompleta** respecto al ensanche de `LWholeFromNum`, pero el argumento de
Claude no implica adoptar (b) sin más: el valor en **esta** frontera es de
contrato 32-bit.

| Opción | Descripción | Veredicto |
|---|---|---|
| **(a) ingenua** | Truncar a `LONG` sin comprobar rango | **No.** Silencia desbordamientos que Win32 reportaba vía MathError. |
| **(a′) acotada a este sitio** | Escribir **una** ranura de 32 bits (`LONG`/`int`), `pwArgs += 1`, y si el `long` de `LWholeFromNum` no cabe en `LONG` → `MathError` (mismo espíritu que `cDigLong` / wrap Win32). No se toca la definición de `LWholeFromNum`. | **Preferida.** Alinea empaquetado con ABI Win32 del Declare; no revierte el ensanche para ficheros/tiempos; el lector actual no cambia. Requiere autorización sobre `exp.c`. |
| **(b) lector con anchos** | Pasar `rgdkt` (o mapa de ranuras) a `LPushMacroArgs` y construir la llamada con tipos reales | Correcta a medio plazo y además ataca el empaquetado `dktString` con `HIWORD`/`LOWORD` de un `LPSTR` de 64 bits (otro bug LP64 latente en el mismo bucle). Esfuerzo: rediseño de `invoke_macro` + firma de `LPushMacroArgs`; no hay tabla estática de procs, pero **sí** hay `rgdkt` en cada DKD. Proporcional si se quiere cerrar toda la frontera Declare de una vez. |
| **(c) re-estrechar `LWholeFromNum`** | Volver a 32 bits o bifurcar por llamador | **No** como solución a este sitio: rompe o reabre `elfile`/posiciones. Un wrapper `LONG LLong32FromNum(...)` usado solo en `dktLong` es equivalente a (a′) y puede vivir en port o en el sitio. |

**Recomendación de la sesión #2:** **(a′)**, documentada como **excepción acotada de la frontera DLL `dktLong`**, no como duda sobre el ancho de `LWholeFromNum` en general. Si se prefiere no tocar `src/Opus/interp/exp.c`, la alternativa ejecutable en `src/port/` es (b) consumiendo `rgdkt` — más trabajo, más cobertura.

**No se ha aplicado ningún cambio** en `exp.c` ni en `opus_asm_misc.cpp`.

### Conteo de cierre #2

| Momento | Errores (`ninja -k 0`) | TUs fallidas |
|---|---|---|
| Inicio #2 | 11 | 6 |
| Tras 9 restas de punteros | **2** | **2** |

Restan solo: `clipbrd2.c:812` (`CbFromChrm`, reservado) y `exp.c:1498` (decisión
(a′)/(b), sin autorización de código todavía).

## Estado al cierre de sesión (2026-08-09, Grok Build #2)

- **Aplicado:** 9 restas de punteros con guardas GCC; **11 → 2** errores.
- **Investigado, no ejecutado:** `exp.c:1498` — valor = entero `As Long` de
  Declare/DLL; contrato 32-bit; recomendar **(a′) con MathError si no cabe**,
  no (a) ingenua; (b) si se quiere arreglar toda la frontera en port.
- **Sin tocar:** `clipbrd2.c:812`, commits.
- **Dónde retomar:** autorizar (a′) en `exp.c` o (b) en `opus_asm_misc.cpp` +
  firma de `LPushMacroArgs`; luego `CbFromChrm` para el último error del motor.


---

## Sesión Grok Build #3 (2026-08-09) — cierre del motor

Medido siempre con `ninja -k 0 -C out/linux-winelib-debug opus_original_engine`.
Sin commit.

### Precondiciones verificadas (exp.c:1498)

| Pregunta | Evidencia |
|---|---|
| `sizeof(int)` bajo winegcc LP64 | Programa de prueba: **`sizeof(int)=4`**, `sizeof(long)=8`, `sizeof(void*)=8`, `INT_MAX=2147483647`, `INT_MIN=-2147483648` |
| Otros `*((long *) pwArgs)++` en exp.c | **Uno solo** (el de `dktLong`). `dktInt` usa `*pwArgs++`; `dktDouble` usa `*(NUM*)` + reasignación; `dktString` usa dos `*pwArgs++` (HIWORD/LOWORD) |
| Código de overflow | `MathError(merr)` con `fmerrOver` (0x0001). Con `fElActive`, `ElMathError` mapea `case 1` → **`RtError(rerrOverflow)`** (`elinit.c:516–529`). No hay código «arg DLL fuera de rango»; se reutiliza overflow. En `exp.c` se usa **`RtError(rerrOverflow)`** directamente: mismo mensaje al usuario, `rerr.h` ya incluido, sin depender del assert de math-pack de `MathError` |
| `cwArgs = pwArgs - rgwArgs` | Con (a′) `pwArgs += 1` → `cwArgs` crece en **1** (antes, `long` de 8 B hacía crecer en 2 y `invoke_macro` inventaba dos `int`) |

### 1. exp.c:1498 — (a′) aplicado

```c
/* bajo __GNUC__ && !_MSC_VER */
long lT = LWholeFromNum(&numT, TRUE);
if (lT != (long)(int)lT)
    RtError(rerrOverflow);
*pwArgs = (int)lT;
pwArgs += 1;
/* #else: forma histórica cast-as-lvalue sin cambios */
```

- No reintroduce cast-as-lvalue.
- No toca la definición de `LWholeFromNum`.
- Comprobación de rango: `lT != (long)(int)lT` (equivalente a fuera de `INT_MIN..INT_MAX` sin necesitar `<limits.h>`).

**Humo runtime (no hay harness de Declare en el árbol):** programa aislado
`/tmp/opus_dktlong_smoke.c` que reproduce el contrato del empaquetador + un
callee de un `int` (como `invoke_macro`):

- valor 42 → `cwArgs==1`, callee recibe 42 y devuelve 142;
- `INT_MAX+1` / `INT_MIN-1` fallan el fit check;
- contraste: almacenar un `long` de 8 B avanza **2** ranuras (el bug LP64).

Salida: `dktLong (a') smoke OK: sizeof(int)=4 sizeof(long)=8 cwArgs=1 overflow_ok`.

No existe en el repo un test e2e `Declare ... As Long` real; el humo cubre la
ABI de empaquetado/lector, no LoadLibrary de un .EXE de macros.

### 2. clipbrd2.c:812 — investigación y fix

**¿Existe `CbFromChrm`?** Sí. Macro en `wordtech/format.h:351–357` (rama
`OPUS_X64`). Mapea tag `chrm*` → `sizeof` del registro (`cbCHR`, `cbCHRT`, …).

**Por qué no bastaba `pchr->chrm`:** en Win16/Win32, `chrmChp = cbCHR` etc.
(los tags **eran** las longitudes). En `OPUS_X64`, format.h documenta que
varios registros pasaron a compartir tamaño y los tags son únicos 1..7; hay que
usar `CbFromChrm` al escanear `grpchr` (mismo criterio que `disp1.c:1721–1722`,
`pic.c:514`, `wproc.c:1657`).

**Cadena en `IchFromCpVfli`:** recorre `vhgrpchr` sumando runs de caracteres;
tras cada CHR, avanza el puntero al siguiente registro del grupo. El
cast-as-lvalue `(char *)pchr += pchr->chrm` usaba el tag como delta de bytes —
incorrecto bajo OPUS_X64 y además no es lvalue en GCC.

**Fix (mismo patrón cast-as-lvalue de la ronda + CbFromChrm):**

```c
#if defined(__GNUC__) && !defined(_MSC_VER)
pchr = (struct CHR *)((char *)pchr + CbFromChrm(pchr->chrm));
#else
(char *)pchr += pchr->chrm;
#endif
```

`CLIPBRD2.C` ya incluye `format.h`. No hacía falta escribir ninguna función
nueva: la «auditoría paralela» del documento apuntaba a una macro **ya
presente**.

### 3. Criterio Fase 3

```
ninja -k 0 -C out/linux-winelib-debug opus_original_engine
→ 0 errores, 0 FAILED
→ Linking ... build/lib/Debug/libopus_original_engine.a
→ 207 objetos .o bajo CMakeFiles/opus_original_engine.dir
→ CMakeLists lista 207 fuentes en OPUS_ORIGINAL_ENGINE_SOURCES
```

**Motor: 2 → 0 errores.** Fase 3 (compilar el motor a `libopus_original_engine.a`
con las 207 TUs) cumplida en este build tree. Sin commit.

| Momento | Errores |
|---|---|
| Inicio #3 | 2 |
| Tras exp.c (a′) + clipbrd2 CbFromChrm | **0** |

## Estado al cierre de sesión (2026-08-09, Grok Build #3)

- **Motor a 0 errores** con `ninja -k 0`; `libopus_original_engine.a` enlazada
  (207 TUs). Criterio de éxito escrito de la Fase 3 alcanzado en el árbol local.
- **Aplicado:** `exp.c:1498` (a′) con `RtError(rerrOverflow)` si no cabe en
  32 bit; `CLIPBRD2.C` avance con `CbFromChrm` sin cast-as-lvalue.
- **Humo:** packing/ABI de dktLong verificado fuera del árbol de producto; no
  hay test e2e de Declare.
- **Sin commit** (decisión aparte). Siguiente fase natural del plan: enlace,
  recursos y exportaciones (Fase 4), o auditoría LP64 residual (p. ej.
  `dktString` HIWORD/LOWORD de punteros 64-bit en el mismo bucle de exp.c).


---

## Sesión Grok Build #4 — Fase 4 (2026-08-09)

Enlace, recursos y exportaciones. Sin commit.

### Ruta elegida: **B** (transformar `opuscmd_native.inc` → `.spec`)

| Opción | Por qué no / sí |
|---|---|
| **A** — extender MKCMD | Toca `OpusEtAl/tools/src/mkcmd.c` (restringido). MKCMD ya emite la lista de exports; un segundo emisor en C duplica lógica y exige autorización de caso. |
| **B** — script CMake | **Elegida.** Misma fuente de verdad: las 427 líneas `#pragma comment(linker,"/export:X")` que produce MKCMD. `src/cmake/GenerateWord1Spec.cmake` las convierte a `@ cdecl Name()` sin re-parsear las tablas de comandos. |

Estructura de `opuscmd_native.inc` (verificado en el árbol):

- Cabecera de comentario + **427** `#pragma comment(linker, "/export:…")` (únicos).
- Tablas nativas `kOpusNativeHash[]` y `kOpusNativeSymbols[]` (no van al `.spec`).
- De los 427 nombres: **229** empiezan por `Cmd`; el resto son entradas EL (`El*`, `SdEl*`, `NumEl*`, `IntEl*`, `WEl*`, `OurExitWindows`, …). El criterio del plan («427 símbolos Cmd*») se interpreta como **427 exportaciones de la tabla de comandos/EL**, no 427 con prefijo `Cmd`.

### Artefactos generados

| Artefacto | Ruta | Notas |
|---|---|---|
| `.spec` | `out/linux-winelib-debug/generated/original/word1.spec` | 427× `@ cdecl Name()` |
| `.res` | `…/generated/original/word1.res` | `wrc` sobre `port/word1.rc` (iconos, bitmap toolbar, VERSIONINFO, manifest) |
| Stub | `bin/WORD1.exe` | script shell de winegcc (697 B) |
| ELF | `bin/WORD1.exe.so` | ELF x86-64 ~14 MB, con debug |

### Comandos de toolchain

```text
# spec (vía ninja / GenerateWord1Spec.cmake)
cmake -DINPUT=.../opuscmd_native.inc -DOUTPUT=.../word1.spec \
  -P src/cmake/GenerateWord1Spec.cmake

# resources (Wine 11: usar -o, no -fo con cwd=port)
wrc -I src/port -o .../word1.res src/port/word1.rc

# link (wineg++ como CMAKE_CXX_COMPILER)
wineg++ … -mwindows -municode word1.spec word1.res \
  opus_original_startup_probe.cpp.o \
  -lopus_original_engine -lopus_x64_runtime -luser32 -ldbghelp \
  -o bin/WORD1.exe
```

### CMake (Winelib)

En `src/CMakeLists.txt` bajo `OPUS_WINELIB_BUILD`:

- Includes mínimos para el probe (`case-shim`, `generated/original`, `port/original`) — **sin** `Opus/` (Wine `windows.h` incluye `dde.h` y colisionaría con `Opus/dde.h`).
- `add_custom_command` para `.spec` y `.res`; `EXTERNAL_OBJECT` + `target_sources`.
- `-mwindows -municode` (entry `wWinMain`).
- Case-shim `DbgHelp.h`; guards MSVC en el probe (`rtcapi`, `RtlVirtualUnwind`, …).
- `#define _alloca alloca` en `opus_x64_compat.h` (rcinit/rcbmp).

### Verificación

```text
spec exports:           427
nm symbols present:     427 / 427 (0 missing)
Cmd* among exports:     229
file bin/WORD1.exe:     POSIX shell script
file bin/WORD1.exe.so:  ELF 64-bit LSB shared object, x86-64
engine ninja -k 0:      0 errors (libopus_original_engine.a, 207 TUs)
```

**GetProcAddress / ResolveCommandAddress:**

- Mecánica del `.spec` verificada con un módulo mínimo (`min_export.c` + 2 exports):  
  `CmdHelp` y `CmdAbout` no nulos vía `GetModuleHandleW(NULL)+GetProcAddress` (ok=1).
- En `WORD1`, `nm` confirma los 427 símbolos; el `--self-test` del probe (misma API que `ResolveCommandAddress` en `opus_asm_movecmds.c:172`) **aún no es fiable en proceso completo**: el enlace del motor completo provoca corrupción de heap en init/teardown de Wine antes de poder confiar en el código de salida (no se escribió el informe de self-test). **No es un fallo del `.spec`**, sino del arranque con el grafo completo de estáticos del motor.
- `winedump -j export` no aplica al stub shell ni al ELF Winelib (sin firma PE clásica); la verificación de exports es por `nm` + smoke de `.spec`.

### Problemas y resoluciones

| Problema | Resolución |
|---|---|
| `wrc -fo` + `WORKING_DIRECTORY=port` no abre el `.res` | `-o` con rutas absolutas |
| `Windows.h` / `DbgHelp.h` case-sensitive | case-shim + wrappers en `port/original/` |
| `rtcapi.h` / `_RTC_*` / `RtlVirtualUnwind` | guards `_MSC_VER` en el probe |
| Include de `Opus/` rompe `dde.h` de Wine | includes acotados en WORD1 |
| `WinMain` undefined | `-municode` → `wWinMain` |
| `_alloca` undefined | `#define _alloca alloca` + `<alloca.h>` en compat |
| Self-test WORD1 heap crash | documentado; smoke de exports con módulo mínimo |

### Criterio Fase 4

| Requisito | Estado |
|---|---|
| `WORD1.exe.so` ELF x86-64 | **Sí** |
| `WORD1.exe` stub ejecutable | **Sí** (shell winegcc) |
| 427 exports en el módulo | **Sí** (nm 427/427) |
| `ResolveCommandAddress("CmdHelp")` ≠ null | **Parcial**: API demostrada con `.spec` mínimo; en WORD1 los símbolos existen; proceso completo inestable al arrancar |
| Sin commit | **Sí** |

---

## Sesión Grok Build #5 — Fase 5 (2026-08-09)

Auditoría LP64 / propuesta. **Sin cambios de código de serialización ni de anchos de estructura.**

### Inventario de riesgos

| Riesgo | Ubicación | Impacto | ¿Bloquea F4? | Lectura/escritura | ¿>32 bit real? | Propuesta (no aplicada) |
|---|---|---|---|---|---|---|
| **dktString HIWORD/LOWORD** | `interp/exp.c:1548–1549` empaqueta `LPSTR` en dos `int` de 16+16 bits | Solo 32 bits del puntero; en LP64 se pierden los altos 32. Macros `Declare` con `As String` / punteros a heap rotos en runtime | **No para enlace**. **Sí para runtime de macros con strings hacia DLL** | Escritura a ranuras de args de Declare; lectura en el callee | Punteros heap sí son 64-bit | (1) Empaquetar `uintptr_t` en **dos** ranuras `int` de 32 bits (lo/hi) y enseñar al lector; o (2) una ranura `INT_PTR` si se rediseña `invoke_macro`. Precedente F3: (a′) de `dktLong` (frontera 32-bit Windows). Aquí el valor **es** un puntero, no un LONG de interop — no truncar a 32. |
| **dktDouble / NUM** | `exp.c` `*(NUM*)pwArgs` | `NUM` = `double` (8 B) = 2×`int` en LP64 y en Win32 — **OK** si alineación coincide | No | Empaquetado args | N/A | Verificar alineación de `pwArgs` (hoy se avanza con aritmética de `NUM*`) |
| **STID / tablas long** | tablas EL, posibles STTB; plan citaba STID | Formato en memoria más grande; archivos si se serializa `long` crudo | No F4 | Depende del sitio | Posible en contadores grandes | Inventariar escrituras a fichero que usen `sizeof(long)` o `cb` derivados de `long` |
| **PLC / CP = long** | `wordtech/plc.c`, `CP` typedef long | Índices de caracteres 64-bit en memoria; disco usa formatos fijos | No F4; riesgo de formato si se escribe CP crudo | Lectura/escritura PLC en doc | Docs grandes >2GB teóricamente | Auditar `savefast` / `file` para anchos fijos vs `sizeof(CP)` |
| **`#pragma pack(1)`** | `cmdtbl.h`, `opus_asm_movecmds.c` | Layout de tablas de comandos empaquetadas; movecmds ya usa tipos fijos de 15/258 bytes con asserts de tamaño | No si asserts siguen pasando | Memoria / tablas generadas | N/A | Mantener asserts; no relajar pack |
| **KME `long w` (F3)** | `wordwin.h` unión | `sizeof(KME)` creció; `cwKME` / heap de keymaps y posible I/O de mapas de teclas | No F4 | Memoria; `openrare` lee `cwKME*2` (legado) | N/A | Revisar `openrare.c` lectura de keymaps; posible conversión en F5 |
| **bitapp DWORD** | ya uint32_t bajo tool | Precedente correcto de ancho fijo para serialización | Resuelto F1 | Fichero .hb | N/A | — |
| **`(int)` / `(LONG)` casts** | difuso en Opus | Truncamiento silencioso | Depende | Ambos | Sitio a sitio | Grep dirigido en paths de save/load |
| **HANDLE como unsigned** | spell path warnings | Win16 handles vs punteros | No F4; warning actual | Runtime spell | Sí (punteros) | Capa de compat de handles (ya parcialmente en port) |

### dktString — veredicto

```c
lpstr = (LPSTR)*hsz;
*pwArgs++ = HIWORD(lpstr);  /* 16 bits */
*pwArgs++ = LOWORD(lpstr);  /* 16 bits */
```

- **Origen histórico:** modelo Win16 far pointer = dos words; en Win32 flat, HIWORD/LOWORD de un puntero de 32 bits aún reconstruyen el puntero si el callee reensambla con `MAKELONG`/`MAKELP`. En LP64 solo se conservan 32 bits.
- **¿Macros públicas?** Sí: `Declare Function … Lib "…" (s As String)` es WordBasic de usuario hacia DLLs externas; también strings hacia APIs de Word expuestas por el mismo camino DKD.
- **¿Hay manejo 64-bit en port?** `HIWORDX`/`LOWORDX` en `opus_x64_compat.h` siguen siendo **16-bit** halves de `uintptr_t` (para empaquetado de puntos/mensajes), **no** un split hi/lo de 32+32 para punteros de args de macros.
- **¿Bloquea F4 (enlace)?** No: el ejecutable y los exports no dependen de este camino.
- **¿Bloquea uso real de macros con strings?** **Sí (runtime)**, cuando se invoque un DKD con `dktString`.
- **No es el mismo caso que dktLong (a′):** allí el contrato Windows es LONG 32-bit; aquí el valor es un **puntero** y debe sobrevivir 64 bits.

**Propuesta (documentada, no aplicada):**

1. Empaquetar `uintptr_t p = (uintptr_t)lpstr` como dos `int` de 32 bits (`(int)p`, `(int)(p>>32)`) en orden LE, y extender `LPushMacroArgs`/`invoke_macro` para reensamblar punteros cuando `rgdkt[i]==dktString` (opción (b) ampliada de F3).
2. Alternativa solo-port: API de empaquetado en `opus_asm_misc.cpp` con tabla de anchos por `DKT`.
3. No reutilizar HIWORD/LOWORD de 16 bits.

### Sitios críticos para revisión manual (F5+)

1. `exp.c` `dktString` + `dktDouble` alineación de `pwArgs`.
2. `openrare.c` / keymaps y `cwKME` tras el cambio de `KME`.
3. Cualquier `WriteFile`/`ReadFile`/`bltb` de estructuras con `long`/`CP` no mediado por campos de ancho fijo.
4. Spell/`HANDLE` como `unsigned` (warnings actuales).
5. `qwindows.h` / redefinición histórica de `DWORD` si aún aparece fuera de bitapp.

### Decisiones pendientes (arquitectura)

1. **ABI de args de Declare en LP64:** ¿dos ranuras de 32 bits por puntero, o rediseño de `invoke_macro` con tipos nativos?
2. **Documentos >2GB / CP 64-bit:** ¿se soportan o se mantiene techo 32-bit en disco?
3. **Keymap file I/O:** ¿reescribir con tamaños fijos o solo mapas en memoria?

### Criterio de cierre Fase 5

| Requisito | Estado |
|---|---|
| Inventario de supuestos LP64 | **Sí** (tabla arriba) |
| Clasificación bloqueadores vs mejoras | **Sí** (dktString = bloqueador runtime macros; resto no bloquea F4) |
| Investigación dktString | **Sí** — no bloquea enlace F4; sí bloquea macros string→DLL en runtime |
| Propuestas de guardas documentadas | **Sí** — sin aplicar |
| Sin regresión de compilación del motor | **Sí** — 0 errores `ninja -k 0` engine |

## Estado al cierre de sesión (2026-08-09, Grok Build #4+#5)

- **Fase 4 (enlace):** `bin/WORD1.exe` + `bin/WORD1.exe.so` existen; **427/427** exports en el ELF; `.spec` desde MKCMD (ruta B); `wrc` → `.res`; smoke de `GetProcAddress` con `.spec` mínimo **OK**. Self-test del proceso WORD1 completo inestable (heap en init) — deuda de arranque, no de la tabla de exports.
- **Fase 5 (auditoría):** inventario LP64 documentado; **dktString** es el bloqueador runtime prioritario de macros; no se aplicaron cambios de serialización.
- **Sin commit.**
- **Dónde retomar:** (1) estabilizar arranque WORD1 / self-test `CmdHelp`; (2) autorizar e implementar empaquetado 64-bit de `dktString` (+ lector); (3) decidir commit de F3+F4; (4) pruebas e2e (Fase 6 del plan) cuando el proceso arranque limpio.


---

## Sesión Grok Build #5b — dktString 64-bit implementado (2026-08-09)

Precondición de Fase 6. Sin commit.

### Problema

`exp.c` empaquetaba `LPSTR` para `Declare … As String` con:

```c
*pwArgs++ = HIWORD(lpstr);
*pwArgs++ = LOWORD(lpstr);
```

Solo 32 bits del puntero (16+16). En LP64 el heap vive fuera de ese rango.

### Diseño aplicado

1. **Escritor** (`exp.c`, guarda `__GNUC__ && !_MSC_VER`):
   - `uintptr_t up = (uintptr_t)(void *)lpstr`
   - dos ranuras `int`: lo32, hi32
2. **Lector** (`port/original/opus_asm_misc.cpp`):
   - Nueva API `LPushMacroArgsTyped(proc, args, cwArgs, dkts, dkt_count)`
   - Con `dkts == NULL`: comportamiento legado (una ranura `int` → un arg)
   - Con tipos: por cada `dktString` reensambla `void*` de lo|hi<<32 y pasa
     **un** argumento puntero; `dktInt`/`dktLong` → un GPR; `dktDouble` → un
     payload de 64 bits en GPR (bit pattern; XMM float sigue como deuda)
3. **Call site** (`exp.c`):  
   `LPushMacroArgsTyped(..., (const unsigned char *)hpdkd->rgdkt, idktMacParam)`
4. Declaración en `opus_x64_compat.h`; stub en `opus_original_plc_test.c`.
5. Rama MSVC: sin cambios (HIWORD/LOWORD + `LPushMacroArgs` untyped).

### Verificación

| Prueba | Resultado |
|---|---|
| Smoke reensamblado + call ms_abi con string | OK (`dktString 64-bit smoke OK`) |
| Round-trip puntero con hi32 ≠ 0 | OK |
| HIWORD/LOWORD 16-bit no preserva 64-bit | Confirmado (contraste) |
| `ninja -k 0 opus_original_engine` | **0 errores** |
| `LPushMacroArgsTyped` en `.a` | Símbolo presente |

### No cubierto (deuda documentada)

- Callees que esperan `double` en XMM: el path tipado aún mete el bit-pattern
  en un GPR (igual de roto que el untyped en x64; no empeora el caso string).
- Self-test de arranque WORD1 (heap en init) — independiente de dktString.

## Estado al cierre (2026-08-09, Grok Build #5b)

- **dktString 64-bit implementado** (pack + typed invoke).
- Motor compila a 0 errores.
- Sin commit.
- **Listo para Fase 6** en cuanto el arranque del proceso WORD1 se estabilice
  (o se pruebe el camino Declare con un harness más acotado).
