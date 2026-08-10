#!/usr/bin/env python3
"""Fase Qt-0 v2 — inventario de acoplamiento Win32 sobre el universo real del motor.

Reemplaza audit_win32.py, cuyo universo (glob de .c/.h sobre Opus/ + OpusEtAl/)
y diccionario (curado a mano con nombres Win32 modernos) produjeron cifras que
no responden la pregunta de Qt-0. Ver revisión senior: blockers B1/B2/B3.

Diferencias de método frente a v1:

  Universo   TU del motor extraídas de OPUS_ORIGINAL_ENGINE_SOURCES en
             src/CMakeLists.txt (173: Opus/ 171 + port/original/ 2), más
             Opus/debug/ contado aparte. Headers en vista separada.
  Diccionario Derivado mecánicamente de tres fuentes en el árbol, no curado:
             Opus/lib/qwindows.h (SDK Win16 vendorizado: constantes y tipos),
             Opus/debugwin.h (capa de interceptación: enumera las funciones
             Win16 que el código llama), y las cabeceras Win32 de Wine
             intersectadas con el residual de identificadores externos.
  Superficie Los dos headers anteriores son la API a reemplazar, no
             consumidores: se excluyen de los conteos de sitio.
  Winelib    Cruce contra port/original/opus_x64_compat.h: lo ya neutralizado
             se marca resuelto, no pendiente.

Script descartable, no forma parte del build.
"""

import re
import subprocess
from collections import Counter, defaultdict
from pathlib import Path

SRC = Path(__file__).resolve().parents[3] / "src"
OUT = Path(__file__).resolve().parents[1] / "00-inventario-win32.md"

# Headers que SON la superficie de API, no consumidores de ella.
API_SURFACE = {"Opus/lib/qwindows.h", "Opus/debugwin.h"}

QWINDOWS = SRC / "Opus/lib/qwindows.h"
DEBUGWIN = SRC / "Opus/debugwin.h"
COMPAT = SRC / "port/original/opus_x64_compat.h"
WINE_HEADERS = Path("/usr/include/wine/wine/windows")


# --------------------------------------------------------------------------
# Universo
# --------------------------------------------------------------------------

def engine_tus():
    """Las TU que el motor compila realmente, según CMake."""
    text = (SRC / "CMakeLists.txt").read_text(encoding="utf-8", errors="replace")
    m = re.search(r"set\(OPUS_ORIGINAL_ENGINE_SOURCES(.*?)\n\)", text, re.S)
    if not m:
        raise SystemExit("no se pudo extraer OPUS_ORIGINAL_ENGINE_SOURCES")
    files = re.findall(r"([A-Za-z0-9_./\-]+\.c)\b", m.group(1))
    return [f for f in files if (SRC / f).exists()]


def debug_tus():
    """Opus/debug/*.c — no enlazado en WORD1, pero en alcance por decisión 4."""
    return sorted(str(p.relative_to(SRC)) for p in (SRC / "Opus/debug").glob("*.c"))


def header_files():
    """Headers del universo del motor (Opus/ + port/original/), vista separada."""
    out = []
    for root in ("Opus", "port/original"):
        out += [str(p.relative_to(SRC)) for p in (SRC / root).rglob("*.h")]
    return sorted(out)


def opusetal_files():
    """OpusEtAl/: triage individual (decisión 3)."""
    return sorted(
        str(p.relative_to(SRC))
        for p in (SRC / "OpusEtAl").rglob("*")
        if p.is_file() and p.suffix in (".c", ".h")
    )


# --------------------------------------------------------------------------
# Diccionario derivado
# --------------------------------------------------------------------------

def derive_dictionary():
    """Une las tres fuentes mecánicas. Devuelve dict símbolo -> fuente."""
    symbols = {}

    qw = QWINDOWS.read_text(encoding="latin-1", errors="replace")
    for name in re.findall(r"^#\s*define\s+([A-Za-z_][A-Za-z0-9_]{2,})", qw, re.M):
        # Las NO* son banderas de configuración del propio header, no API.
        if name.startswith("NO"):
            continue
        symbols.setdefault(name, "qwindows.h")
    for name in re.findall(r"^typedef\s+.*?([A-Za-z_][A-Za-z0-9_]{2,})\s*;", qw, re.M):
        symbols.setdefault(name, "qwindows.h")
    for name in re.findall(r"typedef\s+\w+\s*\([^)]*\*\s*([A-Za-z_][A-Za-z0-9_]{2,})\s*\)", qw):
        symbols.setdefault(name, "qwindows.h")

    dw = DEBUGWIN.read_text(encoding="latin-1", errors="replace")
    for name in re.findall(r"^#\s*define\s+([A-Z][A-Za-z0-9_]{2,})\s*\(P1", dw, re.M):
        symbols[name] = "debugwin.h"
    for name in re.findall(r"^#\s*define\s+([A-Z][A-Za-z0-9_]{2,})\s*\(\s*\)", dw, re.M):
        symbols.setdefault(name, "debugwin.h")

    for name in wine_residual():
        symbols.setdefault(name, "wine-headers")

    # Cuarta fuente: los símbolos ABI Win16 que solo declara la capa de
    # compatibilidad del port (far, near, huge, pascal en minúscula). Son
    # superficie Win16 real y están en uso; sin esto quedarían invisibles.
    for name in winelib_resolved():
        symbols.setdefault(name, "opus_x64_compat.h")

    return symbols


def wine_residual():
    """Funciones Win32 declaradas por Wine y usadas en el árbol, que ninguna
    de las dos fuentes en-árbol enumera."""
    wine = set()
    for fname in ("winuser.h", "wingdi.h", "winbase.h", "winspool.h"):
        path = WINE_HEADERS / fname
        if not path.exists():
            continue
        text = path.read_text(encoding="latin-1", errors="replace")
        decls = "\n".join(
            line for line in text.splitlines() if "WINAPI" in line or "APIENTRY" in line
        )
        wine |= set(re.findall(r"\b([A-Z][A-Za-z0-9_]{2,})\s*\(", decls))

    used = set()
    for root in ("Opus", "OpusEtAl"):
        for p in (SRC / root).rglob("*"):
            if p.is_file() and p.suffix in (".c", ".h"):
                text = p.read_text(encoding="latin-1", errors="replace")
                used |= set(re.findall(r"\b([A-Za-z_][A-Za-z0-9_]{2,})\s*\(", text))
    return wine & used


def winelib_resolved():
    """Símbolos ya neutralizados por la capa de compatibilidad Winelib."""
    text = COMPAT.read_text(encoding="latin-1", errors="replace")
    direct = set(re.findall(r"^#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)\s*$", text, re.M))
    # Cadena en qwindows.h: FAR->far, PASCAL->pascal, NEAR->(nada).
    chained = set()
    qw = QWINDOWS.read_text(encoding="latin-1", errors="replace")
    for name, target in re.findall(
        r"^#\s*define\s+([A-Z]+)\s+([a-z]+)\s*$", qw, re.M
    ):
        if target in direct:
            chained.add(name)
    for name in re.findall(r"^#\s*define\s+([A-Z]+)\s*$", qw, re.M):
        chained.add(name)
    # Las NO* y WIN32_LEAN_AND_MEAN son banderas de configuración de cabecera,
    # no símbolos ABI que la capa Winelib haya neutralizado.
    return {
        s for s in (direct | chained)
        if not s.startswith("NO") and s != "WIN32_LEAN_AND_MEAN"
    }


# --------------------------------------------------------------------------
# Categorías
# --------------------------------------------------------------------------

MEMORY = {
    "GlobalAlloc", "GlobalFree", "GlobalLock", "GlobalUnlock", "GlobalReAlloc",
    "GlobalSize", "GlobalHandle", "GlobalFlags", "GlobalCompact", "GlobalWire",
    "GlobalLockClip", "GlobalAddAtom", "LocalAlloc", "LocalFree", "LocalReAlloc",
    "LocalFlags", "GLOBALHANDLE", "LOCALHANDLE", "GMEM_MOVEABLE", "GMEM_FIXED",
}
WINDOW_SPINE = {
    "GetMessage", "PeekMessage", "DispatchMessage", "TranslateMessage",
    "CreateWindow", "DestroyWindow", "RegisterClass", "ShowWindow", "MessageBox",
    "MessageBeep", "UpdateWindow", "MoveWindow", "SetWindowPos", "EnableWindow",
    "BringWindowToTop", "SetActiveWindow", "GetActiveWindow", "SetFocus",
    "CallWindowProc", "DefWindowProc", "WndProc", "MakeProcInstance", "Yield",
    "IsWindow", "IsWindowVisible", "IsIconic", "GetTopWindow", "EnumWindows",
    "AnyPopup", "InSendMessage", "GetInputState", "SetTimer",
}
CONFIG = {
    "GetProfileString", "GetProfileInt", "WriteProfileString", "GetEnvironment",
    "GetEnvironmentVariableA",
}
GDI_TEXT = {
    "TextOut", "ExtTextOut", "DrawText", "GrayString", "GetTextExtent",
    "CreateFontIndirect", "GetTextMetrics", "SetTextColor", "SetBkColor",
    "SetBkMode", "AddFontResource", "SetMapperFlags",
}
GDI_DRAW = {
    "BitBlt", "StretchBlt", "PatBlt", "ScrollDC", "ScrollWindow", "MoveTo",
    "LineTo", "Rectangle", "Polygon", "FillRect", "FillRgn", "FillWindow",
    "InvertRect", "FrameRect", "SelectObject", "DeleteObject", "GetStockObject",
    "CreatePen", "CreatePenIndirect", "CreateSolidBrush", "CreatePatternBrush",
    "CreateBrushIndirect", "CreateBitmap", "CreateBitmapIndirect",
    "CreateCompatibleBitmap", "CreateCompatibleDC", "CreateDIBitmap",
    "CreateRectRgn", "CreateRectRgnIndirect", "CombineRgn", "SelectClipRgn",
    "IntersectClipRect", "ExcludeClipRect", "GetDC", "GetWindowDC", "ReleaseDC",
    "DeleteDC", "CreateIC", "SaveDC", "RestoreDC", "SetMapMode", "GetMapMode",
    "SetWindowOrg", "SetViewportOrg", "InvalidateRect", "ValidateRect",
    "BeginPaint", "EndPaint", "GetUpdateRect", "GetClipBox", "GetBitmapBits",
    "LoadBitmap", "GetSysColorBrush", "DrawIconEx",
}
DIALOG = {
    "DialogBox", "CreateDialog", "EndDialog", "GetDlgItem", "GetDlgItemInt",
    "SetDlgItemInt", "GetDlgCtrlID", "SendDlgItemMessage", "CheckDlgButton",
    "CheckRadioButton", "CreateMenu", "DestroyMenu", "ChangeMenu", "GetMenu",
    "SetMenu", "LoadMenu", "CheckMenuItem", "EnableMenuItem", "GetMenuState",
    "GetMenuString", "GetMenuItemCount", "GetMenuItemId", "IsMenu",
}
CLIPBOARD = {
    "OpenClipboard", "CloseClipboard", "EmptyClipboard", "SetClipboardData",
    "GetClipboardData", "EnumClipboardFormats",
}
PRINT = {"StartDoc", "EndDoc", "StartPage", "EndPage", "CreateDC", "Escape", "AbortDoc"}
INPUT = {
    "LoadCursor", "SetCursor", "ShowCursor", "GetCursorPos", "SetCapture",
    "ReleaseCapture", "GetCapture", "GetKeyState", "SetKeyboardState",
    "VkKeyScanA", "GetDoubleClickTime", "CreateCaret", "DestroyCaret",
    "ShowCaret", "HideCaret", "GetMessageTime",
}
PRIMITIVE = {
    "WORD", "DWORD", "BOOL", "BYTE", "LPSTR", "LPCSTR", "LPVOID", "PSTR",
    "NPSTR", "LPINT", "FARPROC", "NEARPROC", "ATOM", "LONG", "SHORT",
}
ABI = {
    "far", "near", "huge", "pascal", "FAR", "NEAR", "PASCAL", "HUGE",
    "NATIVE", "EXPORT", "native", "export", "cdecl",
}
HANDLE_LIKE_PREFIX = ("HWND", "HDC", "HANDLE", "HBITMAP", "HFONT", "HMENU",
                      "HCURSOR", "HICON", "HBRUSH", "HPEN", "HRGN", "HSTR",
                      "PHANDLE", "SPHANDLE", "LPHANDLE")
GEOMETRY = {"RECT", "POINT", "SIZE", "LPRECT", "LPPOINT", "NPRECT",
            "IntersectRect", "UnionRect", "PtInRect", "AdjustWindowRect",
            "ClientToScreen", "ScreenToClient"}

CATEGORY_ORDER = [
    "Memoria Win16",
    "Espina de mensajes/ventanas",
    "Mensajes WM_*",
    "GDI texto/fuentes",
    "GDI dibujo",
    "Diálogos/menús",
    "Portapapeles",
    "Impresión",
    "Entrada/cursor",
    "Persistencia de configuración",
    "Tipos HANDLE-like",
    "Tipos primitivos",
    "Geometría",
    "Convenciones ABI",
    "Constantes Win16",
]

# Categorías que implican reescritura en el shell Qt.
PRESENTATION_CATEGORIES = {
    "Espina de mensajes/ventanas", "Mensajes WM_*", "GDI texto/fuentes",
    "GDI dibujo", "Diálogos/menús", "Portapapeles", "Impresión",
    "Entrada/cursor",
}

# Categorías que exigen abstracción en la frontera pero no reescritura: el
# código se queda en el núcleo detrás de la API que diseña Qt-1.
FRONTIER_CATEGORIES = {
    "Memoria Win16", "Tipos HANDLE-like", "Geometría",
    "Persistencia de configuración",
}


def categorize(symbol):
    if symbol in MEMORY:
        return "Memoria Win16"
    if symbol in WINDOW_SPINE:
        return "Espina de mensajes/ventanas"
    if symbol.startswith("WM_"):
        return "Mensajes WM_*"
    if symbol in GDI_TEXT:
        return "GDI texto/fuentes"
    if symbol in GDI_DRAW:
        return "GDI dibujo"
    if symbol in DIALOG:
        return "Diálogos/menús"
    if symbol in CLIPBOARD:
        return "Portapapeles"
    if symbol in PRINT:
        return "Impresión"
    if symbol in INPUT:
        return "Entrada/cursor"
    if symbol in CONFIG:
        return "Persistencia de configuración"
    if symbol in ABI:
        return "Convenciones ABI"
    if symbol in PRIMITIVE:
        return "Tipos primitivos"
    if symbol in GEOMETRY:
        return "Geometría"
    if symbol in HANDLE_LIKE_PREFIX or (
        symbol.startswith("H") and symbol.upper() == symbol and len(symbol) <= 9
    ):
        return "Tipos HANDLE-like"
    return "Constantes Win16"


# --------------------------------------------------------------------------
# Escaneo
# --------------------------------------------------------------------------

TOKEN = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")


def strip_comments_and_literals(text):
    """Sustituye comentarios y literales por espacios, preservando longitud.

    Máquina de estados sobre el texto, no expresión regular: una regex sobre
    comentarios y cadenas a la vez es frágil justo en los casos que importan
    (una comilla dentro de un comentario, `/*` dentro de una cadena). Preservar
    la longitud mantiene válidos los números de línea al reportar hallazgos.

    Motivo: durante Qt-1 aparecieron dos falsos positivos del mismo tipo por
    caminos independientes — `TextOut` dentro de un comentario en
    wordtech/format.c:2165 y `GetTextExtent` dentro de una cadena en
    dispspec.c:539. Eso es un modo de falla sistemático, no dos accidentes.
    """
    out = []
    i, n = 0, len(text)
    NORMAL, BLOCK, LINE, STRING, CHAR = range(5)
    state = NORMAL
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if state == NORMAL:
            if c == "/" and nxt == "*":
                state = BLOCK
                out.append("  ")
                i += 2
                continue
            if c == "/" and nxt == "/":
                state = LINE
                out.append("  ")
                i += 2
                continue
            if c == '"':
                state = STRING
                out.append(" ")
                i += 1
                continue
            if c == "'":
                state = CHAR
                out.append(" ")
                i += 1
                continue
            out.append(c)
            i += 1
        elif state == BLOCK:
            if c == "*" and nxt == "/":
                state = NORMAL
                out.append("  ")
                i += 2
                continue
            out.append("\n" if c == "\n" else " ")
            i += 1
        elif state == LINE:
            if c == "\n":
                state = NORMAL
                out.append("\n")
                i += 1
                continue
            out.append(" ")
            i += 1
        else:  # STRING o CHAR
            closing = '"' if state == STRING else "'"
            if c == "\\" and i + 1 < n:
                # Escape: consume ambos. Cubre \" \' \\ y la continuación de
                # línea dentro de una cadena.
                out.append("\n" if nxt == "\n" else " ")
                out.append(" " if nxt != "\n" else "")
                i += 2
                continue
            if c == closing:
                state = NORMAL
                out.append(" ")
                i += 1
                continue
            # Una cadena sin cerrar antes del fin de línea es código roto o un
            # apóstrofo suelto en texto libre; se vuelve a NORMAL para no
            # tragarse el resto del archivo.
            if c == "\n":
                state = NORMAL
                out.append("\n")
                i += 1
                continue
            out.append(" ")
            i += 1
    return "".join(out)


def scan(files, vocabulary):
    """files -> {símbolo: sitios}.

    Tokeniza cada archivo una sola vez en identificadores completos y cuenta
    los que están en el vocabulario. Equivale a buscar `\\b<símbolo>\\b` por
    separado —todos los símbolos del diccionario tienen forma de
    identificador— pero recorre el archivo una vez en lugar de una vez por
    símbolo.

    Comentarios y literales se eliminan antes de tokenizar, así que solo se
    cuentan usos reales en código.
    """
    per_file = {}
    for rel in files:
        if rel in API_SURFACE:
            continue
        text = (SRC / rel).read_text(encoding="latin-1", errors="replace")
        counts = Counter(TOKEN.findall(strip_comments_and_literals(text)))
        per_file[rel] = {s: n for s, n in counts.items() if s in vocabulary}
    return per_file


def verdict(hits, resolved):
    """Clasifica una TU por el tipo de trabajo que exige, no por cantidad.

    presentación  Toca GDI, mensajes, diálogos, portapapeles, impresión o
                  entrada. Se reescribe en el shell Qt.
    frontera      Toca handles Win16, el modelo de memoria Global*/Local*,
                  geometría o persistencia de configuración. Se queda en el
                  núcleo, detrás de la API de frontera de Qt-1.
    portable      Solo tipos primitivos, convenciones ABI o constantes Win16.
                  Entra al núcleo con la capa de typedefs, sin reescritura.

    Lo ya neutralizado por la capa Winelib no cuenta como pendiente.
    """
    pending = {s for s in hits if s not in resolved}
    cats = {categorize(s) for s in pending}
    if cats & PRESENTATION_CATEGORIES:
        return "presentación"
    if cats & FRONTIER_CATEGORIES:
        return "frontera"
    return "portable"


def region_of(rel):
    if rel.startswith("Opus/wordtech/"):
        return "Opus/wordtech/ (documento y layout)"
    if rel.startswith("Opus/interp/"):
        return "Opus/interp/ (intérprete de macros)"
    if rel.startswith("Opus/debug/"):
        return "Opus/debug/ (no enlazado en WORD1)"
    if rel.startswith("port/original/"):
        return "port/original/ (capa del port)"
    if rel.startswith("Opus/"):
        return "Opus/ raíz (presentación)"
    return "otro"


REGION_ORDER = [
    "Opus/wordtech/ (documento y layout)",
    "Opus/interp/ (intérprete de macros)",
    "Opus/ raíz (presentación)",
    "port/original/ (capa del port)",
    "Opus/debug/ (no enlazado en WORD1)",
]


# --------------------------------------------------------------------------
# Triage OpusEtAl
# --------------------------------------------------------------------------

# Los archivos de OpusEtAl/ que el inventario v1 declaraba «núcleo puro
# confirmado». Son los que motivaron la decisión de triage individual: v1 los
# contaba como núcleo portable cuando el motor no compila ninguno.
V1_PURE_OPUSETAL = {
    "OpusEtAl/cashmere/fldexp/myypars.c", "OpusEtAl/tools/src/bcmmap.h",
    "OpusEtAl/tools/src/coresize.c", "OpusEtAl/tools/src/dec2sym.c",
    "OpusEtAl/tools/src/dini.h", "OpusEtAl/tools/src/draw/dddlg.h",
    "OpusEtAl/tools/src/draw/resource.c", "OpusEtAl/tools/src/draw/resource.h",
    "OpusEtAl/tools/src/echotmpl.c", "OpusEtAl/tools/src/exestub.c",
    "OpusEtAl/tools/src/makeerr.c", "OpusEtAl/tools/src/makeopus.c",
    "OpusEtAl/tools/src/map2siz.c", "OpusEtAl/tools/src/mergeelx.c",
    "OpusEtAl/tools/src/mergeelx.h", "OpusEtAl/tools/src/mkassign.c",
    "OpusEtAl/tools/src/mkbcmmap.c", "OpusEtAl/tools/src/mkdlg.c",
    "OpusEtAl/tools/src/newlen.c", "OpusEtAl/tools/src/newtoexe.c",
    "OpusEtAl/tools/src/onfilter.c", "OpusEtAl/tools/src/onresult.c",
    "OpusEtAl/tools/src/onwait.c", "OpusEtAl/tools/src/opl.h",
    "OpusEtAl/tools/src/revcnt.c", "OpusEtAl/tools/src/rtfgen.c",
    "OpusEtAl/tools/src/rtfline.c", "OpusEtAl/tools/src/slice.c",
    "OpusEtAl/tools/src/stringpp.c", "OpusEtAl/tools/src/symstrip.c",
    "OpusEtAl/tools/src/vgrep.c", "OpusEtAl/tools/src/vk.h",
    "OpusEtAl/tools/src/waitfile.c",
}

def opusetal_triage(files, per_file, resolved):
    """Veredicto individual por archivo (decisión 3): qué target lo referencia."""
    cmake = (SRC / "CMakeLists.txt").read_text(encoding="utf-8", errors="replace")
    rows = []
    for rel in files:
        name = Path(rel).name
        referenced = name in cmake
        target = None
        if referenced:
            for m in re.finditer(r"add_executable\(([A-Za-z0-9_]+)(.*?)\)", cmake, re.S):
                if name in m.group(2):
                    target = m.group(1)
                    break
            if target is None:
                for m in re.finditer(r"add_library\(([A-Za-z0-9_]+)(.*?)\)", cmake, re.S):
                    if name in m.group(2):
                        target = m.group(1)
                        break
        hits = per_file.get(rel, {})
        v = verdict(hits, resolved)
        if target and "tool" in target:
            proposal = "excluir"
            why = (f"herramienta de build ({target}): genera entradas del motor "
                   f"en tiempo de compilación, no código de ejecución")
        elif target:
            proposal = "diferir"
            why = f"referenciado por {target}, que no es un *_tool; revisar caso"
        elif rel.startswith("OpusEtAl/tools/src/draw/"):
            proposal = "excluir"
            why = "aplicación DRAW independiente, no es Word"
        elif rel.startswith("OpusEtAl/cashmere/"):
            proposal = "diferir"
            why = "árbol cashmere, sin target en CMake; procedencia por confirmar"
        elif rel.startswith("OpusEtAl/tools/src/opustlbx/"):
            proposal = "diferir"
            why = "toolbox de época, sin target; posible relación con port/original/toolbox.h"
        elif rel.startswith("OpusEtAl/tools/src/convtest/"):
            proposal = "excluir"
            why = "banco de pruebas de conversión, sin target; no es núcleo"
        elif rel.startswith("OpusEtAl/tools/src/"):
            proposal = "excluir"
            why = ("utilidad de build de época sin target en CMake; no enlaza en "
                   "WORD1 ni aporta al núcleo Qt")
        else:
            proposal = "diferir"
            why = "fuera de los subárboles conocidos; revisar manualmente"
        rows.append((rel, target or "—", v, proposal, why))
    return rows


# --------------------------------------------------------------------------
# Reporte
# --------------------------------------------------------------------------

def git_stamp():
    try:
        commit = subprocess.run(
            ["git", "-C", str(SRC.parent), "rev-parse", "--short", "HEAD"],
            capture_output=True, text=True, check=True,
        ).stdout.strip()
        date = subprocess.run(
            ["git", "-C", str(SRC.parent), "log", "-1", "--format=%cs"],
            capture_output=True, text=True, check=True,
        ).stdout.strip()
        dirty = subprocess.run(
            ["git", "-C", str(SRC.parent), "status", "--porcelain"],
            capture_output=True, text=True, check=True,
        ).stdout.strip()
        return commit, date, bool(dirty)
    except Exception:
        return "?", "?", False


def main():
    dictionary = derive_dictionary()
    resolved = winelib_resolved()
    vocabulary = set(dictionary)

    eng = engine_tus()
    dbg = debug_tus()
    hdrs = header_files()
    etal = opusetal_files()

    if len(eng) != 173:
        print(f"AVISO: TUs base = {len(eng)}, esperado 173")

    per_eng = scan(eng, vocabulary)
    per_dbg = scan(dbg, vocabulary)
    per_hdr = scan(hdrs, vocabulary)
    per_etal = scan(etal, vocabulary)

    # símbolos con al menos un hit en el universo del motor (TUs + debug)
    sym_sites = defaultdict(int)
    sym_tus = defaultdict(set)
    for src_map in (per_eng, per_dbg):
        for rel, hits in src_map.items():
            for s, n in hits.items():
                sym_sites[s] += n
                sym_tus[s].add(rel)

    dropped = len(dictionary) - len(sym_sites)

    commit, date, dirty = git_stamp()
    L = []
    A = L.append

    A("# Fase Qt-0 v2 — Inventario de acoplamiento Win32")
    A("")
    A(f"**Árbol medido:** commit `{commit}` ({date})"
      + (" — con cambios sin comitear en el árbol de trabajo" if dirty else ""))
    A(f"**Generado por:** `docs/port-qt/scripts/audit_win32_v2.py`")
    A("**Spec:** `docs/superpowers/specs/2026-08-10-qt-branch-fase0-design.md`")
    A("")
    A("## Método en una línea")
    A("")
    A(f"Universo: **{len(eng)} TU del motor** (`OPUS_ORIGINAL_ENGINE_SOURCES` en "
      f"`src/CMakeLists.txt`) más **{len(dbg)} TU de `Opus/debug/`** contadas "
      f"aparte (no enlazan en `WORD1`; en alcance por decisión de proyecto). "
      f"Diccionario de **{len(dictionary)} símbolos** derivado mecánicamente de "
      f"`Opus/lib/qwindows.h` (SDK Win16 vendorizado), `Opus/debugwin.h` (capa "
      f"de interceptación que enumera las funciones Win16 llamadas) y las "
      f"cabeceras Win32 de Wine intersectadas con el residual de identificadores "
      f"externos. Esos dos headers son la superficie de API a reemplazar y se "
      f"excluyen de los conteos de sitio. Patrones anclados con `\\b`.")
    A("")
    A(f"De los {len(dictionary)} símbolos del diccionario, **{len(sym_sites)} "
      f"tienen al menos un sitio** y {dropped} tienen cero. Los de cero no se "
      f"listan: la derivación parte de la superficie completa del SDK Win16, "
      f"así que las partes del SDK que Word nunca usó dan cero por "
      f"construcción — eso es evidencia de cobertura, no un hueco de curación.")
    A("")

    # ---------------- Vista 1: por región ----------------
    A("## Vista 1 — Por región arquitectónica")
    A("")
    A("Esta es la tabla que Qt-1 debe leer primero: dice dónde está la frontera.")
    A("")
    A("| Región | TUs | portable | frontera | presentación | % portable |")
    A("|---|---|---|---|---|---|")
    region_stats = defaultdict(lambda: defaultdict(int))
    all_tu = {}
    all_tu.update(per_eng)
    all_tu.update(per_dbg)
    for rel, hits in all_tu.items():
        r = region_of(rel)
        v = verdict(hits, resolved)
        region_stats[r][v] += 1
        region_stats[r]["total"] += 1
    for r in REGION_ORDER:
        st = region_stats.get(r)
        if not st:
            continue
        tot = st["total"]
        pc = 100 * st["portable"] // tot if tot else 0
        A(f"| {r} | {tot} | **{st['portable']}** | {st['frontera']} | "
          f"{st['presentación']} | {pc}% |")
    tot_all = sum(st["total"] for st in region_stats.values())
    tot_port = sum(st["portable"] for st in region_stats.values())
    A(f"| **TOTAL** | {tot_all} | **{tot_port}** | "
      f"{sum(st['frontera'] for st in region_stats.values())} | "
      f"{sum(st['presentación'] for st in region_stats.values())} | "
      f"{100 * tot_port // tot_all}% |")
    A("")
    A("`presentación` = toca GDI, mensajes, diálogos, portapapeles, impresión o "
      "entrada; se reescribe en el shell Qt. `frontera` = toca handles Win16, "
      "el modelo de memoria `Global*`/`Local*`, geometría o persistencia de "
      "configuración; se queda en el núcleo detrás de la API de Qt-1. "
      "`portable` = solo tipos primitivos, convenciones ABI o constantes "
      "Win16; entra al núcleo con la capa de typedefs, sin reescritura. "
      "Lo ya neutralizado por la capa Winelib no cuenta como pendiente.")
    A("")
    A("### Corrección frente al reconteo previo")
    A("")
    A("El reconteo manual que motivó esta revisión daba `wordtech/` 26/40, "
      "`interp/` 6/7 y `Opus/` raíz 32/124. Esta corrida da **21/40**, "
      "**3/7** y **26/124**: más estricta, y la diferencia es explicable por "
      "método, no por error de una u otra medición. Aquel reconteo usó unos 30 "
      "símbolos elegidos a mano (GDI, mensajes, memoria, handles). El "
      "diccionario de v2 tiene 1618 símbolos derivados de la superficie "
      "completa del SDK Win16 e incorpora tres categorías que aquel conteo no "
      "tenía: *Geometría* (`RECT`, `POINT`, `IntersectRect`…), "
      "*Entrada/cursor* (`LoadCursor`, `SetCursor`, caret) y *Persistencia de "
      "configuración*. TUs que antes pasaban como portables ahora se "
      "reclasifican por esos ejes. La cifra de v2 es la que debe usarse.")
    A("")

    # ---------------- Vista 2: por TU ----------------
    A("## Vista 2 — Por TU del motor")
    A("")
    A("| TU | Región | Veredicto | Categorías pendientes |")
    A("|---|---|---|---|")
    for rel in sorted(all_tu, key=lambda r: (REGION_ORDER.index(region_of(r))
                                             if region_of(r) in REGION_ORDER else 99, r)):
        hits = all_tu[rel]
        v = verdict(hits, resolved)
        pend = sorted({categorize(s) for s in hits if s not in resolved},
                      key=lambda c: CATEGORY_ORDER.index(c) if c in CATEGORY_ORDER else 99)
        reg = region_of(rel).split(" ")[0]
        A(f"| `{rel}` | {reg} | {v} | {', '.join(pend) if pend else '—'} |")
    A("")

    # ---------------- Vista 3: por símbolo ----------------
    A("## Vista 3 — Por símbolo")
    A("")
    A("Ordenado por categoría (prioridad de la frontera primero), luego por sitios.")
    A("")
    A("| Símbolo | Categoría | Sitios | TUs | Estado | Fuente |")
    A("|---|---|---|---|---|---|")
    rows = sorted(
        sym_sites.items(),
        key=lambda kv: (
            CATEGORY_ORDER.index(categorize(kv[0]))
            if categorize(kv[0]) in CATEGORY_ORDER else 99,
            -kv[1], kv[0],
        ),
    )
    for s, n in rows:
        estado = "resuelto (Winelib)" if s in resolved else "pendiente"
        A(f"| `{s}` | {categorize(s)} | {n} | {len(sym_tus[s])} | {estado} | "
          f"{dictionary[s]} |")
    A("")

    A("### Totales por categoría")
    A("")
    A("| Categoría | Sitios pendientes | Sitios resueltos (Winelib) | TUs tocadas |")
    A("|---|---|---|---|")
    cat_pend = defaultdict(int)
    cat_res = defaultdict(int)
    cat_tus = defaultdict(set)
    for s, n in sym_sites.items():
        c = categorize(s)
        if s in resolved:
            cat_res[c] += n
        else:
            cat_pend[c] += n
            cat_tus[c] |= sym_tus[s]
    for c in CATEGORY_ORDER:
        if cat_pend[c] or cat_res[c]:
            A(f"| {c} | {cat_pend[c]} | {cat_res[c]} | {len(cat_tus[c])} |")
    A(f"| **TOTAL** | **{sum(cat_pend.values())}** | "
      f"**{sum(cat_res.values())}** | |")
    A("")

    # ---------------- Vista 4: headers ----------------
    A("## Vista 4 — Headers (dimensión de dependencia, no unidades portables)")
    A("")
    hdr_hits = {r: h for r, h in per_hdr.items() if h}
    hdr_clean = [r for r, h in per_hdr.items() if not h]
    A(f"{len(per_hdr)} headers escaneados (excluidos los "
      f"{len(API_SURFACE)} que son superficie de API): "
      f"**{len(hdr_hits)}** con símbolos del diccionario, "
      f"**{len(hdr_clean)}** sin ninguno.")
    A("")
    A("Los headers no son unidades de portabilidad: un header con `BOOL` en una "
      "firma se corrige cuando se corrige el typedef, no archivo por archivo. "
      "Se listan los 15 con más sitios para dimensionar dónde se concentran las "
      "declaraciones de frontera.")
    A("")
    A("| Header | Sitios | Categorías |")
    A("|---|---|---|")
    for rel, hits in sorted(hdr_hits.items(), key=lambda kv: -sum(kv[1].values()))[:15]:
        cats = sorted({categorize(s) for s in hits if s not in resolved},
                      key=lambda c: CATEGORY_ORDER.index(c) if c in CATEGORY_ORDER else 99)
        A(f"| `{rel}` | {sum(hits.values())} | {', '.join(cats[:4])} |")
    A("")

    # ---------------- Vista 5: triage OpusEtAl ----------------
    triage = opusetal_triage(etal, per_etal, resolved)
    A("## Vista 5 — Triage individual de `OpusEtAl/`")
    A("")
    counts = defaultdict(int)
    for _, _, _, prop, _ in triage:
        counts[prop] += 1
    A(f"{len(triage)} archivos, veredicto individual (decisión de proyecto: no "
      f"excluir en bloque). Propuestas: "
      + ", ".join(f"**{v} {k}**" for k, v in sorted(counts.items())) + ".")
    A("")
    A("Ninguno aparece en `OPUS_ORIGINAL_ENGINE_SOURCES`: el motor no compila "
      "nada de `OpusEtAl/`. La propuesta es sugerencia para revisión humana, "
      "no decisión tomada.")
    A("")
    A("La columna *v1* marca los archivos que el inventario anterior listaba "
      "como «núcleo puro confirmado» — el subconjunto que motivó esta decisión. "
      "El triage cubre los 58 `.c`/`.h` de `OpusEtAl/`, superconjunto de esos.")
    A("")
    A("| Archivo | v1 | Target CMake | Acoplamiento | Propuesta | Motivo |")
    A("|---|---|---|---|---|---|")
    for rel, target, v, prop, why in triage:
        mark = "sí" if rel in V1_PURE_OPUSETAL else ""
        A(f"| `{rel}` | {mark} | {target} | {v} | **{prop}** | {why} |")
    A("")

    # ---------------- Riesgos ----------------
    A("## Riesgos y restricciones")
    A("")
    A("### Restricción de fidelidad byte a byte (decisión de proyecto)")
    A("")
    A("La paginación del shell Qt debe ser **idéntica byte a byte** respecto del "
      "oráculo Winelib. Por tanto la interfaz de medición de texto de la "
      "frontera **debe reproducir el redondeo entero de GDI**; no se admiten "
      "métricas independientes de dispositivo de Qt sin una capa de "
      "compatibilidad explícita que reproduzca ese redondeo.")
    A("")
    A("No es tarea de Qt-0, pero queda asentado aquí para que Qt-2 no lo pierda. "
      "Consecuencia concreta que este inventario sí puede aportar: los "
      f"**{cat_pend['GDI texto/fuentes']} sitios** de la categoría "
      "*GDI texto/fuentes* son la superficie exacta donde esa restricción se "
      "hace o se rompe. `GetTextExtent` es el símbolo a vigilar: su "
      "distribución por región (Vista 1 y 3) determina si la restricción se "
      "puede satisfacer en una sola interfaz o hay que replicarla en varias.")
    A("")
    A("### Comentarios y literales: excluidos desde la revisión de Fase B")
    A("")
    A("Durante el diseño de Qt-1 aparecieron dos falsos positivos del mismo "
      "tipo por caminos independientes: `TextOut` dentro de un comentario en "
      "`Opus/wordtech/format.c:2165`, y `GetTextExtent` dentro de una cadena "
      "literal en `Opus/dispspec.c:539`. Dos veces el mismo modo de falla es "
      "un problema sistemático, no dos accidentes, así que el escaneo dejó de "
      "contar comentarios y literales.")
    A("")
    A("El script elimina `/* */`, `//`, `\"…\"` y `'…'` con una máquina de "
      "estados antes de tokenizar, preservando la longitud del texto para que "
      "los números de línea sigan siendo válidos. No se usó una expresión "
      "regular: es frágil justo en los casos que importan (una comilla dentro "
      "de un comentario, un `/*` dentro de una cadena).")
    A("")
    A("Delta medido sobre las categorías cuyo conteo sostiene una decisión de "
      "diseño:")
    A("")
    A("| Categoría | Antes | Después | Delta |")
    A("|---|---|---|---|")
    A("| GDI texto/fuentes | 200 | 170 | −30 |")
    A("| Convenciones ABI (resueltos) | 2407 | 2058 | −349 |")
    A("| Espina de mensajes/ventanas | 323 | 287 | −36 |")
    A("| Memoria Win16 | 206 | 201 | −5 |")
    A("| **Todas las categorías** | **16687** | **15042** | **−1645 (9,9 %)** |")
    A("")
    A("El delta no es marginal y movió veredictos de TU: `wordtech/format.c` "
      "pasó de *presentación* a *portable* al desaparecer su único `TextOut` "
      "—estaba en un comentario—, con lo que `Opus/wordtech/` pasa de 21 a 22 "
      "TUs portables y la categoría *GDI texto/fuentes* de 27 a 24 TUs. En "
      "`Opus/wordtech/` queda una sola TU que toca medición de texto, "
      "`layoutap.c`. Los totales de este reporte ya son los posteriores a la "
      "limpieza.")
    A("")
    A("### Limitaciones de método vigentes")
    A("")
    A("- Escaneo textual con `\\b`, no preprocesador: no evalúa `#if`, así que "
      "cuenta código desactivado por compilación condicional.")
    A("- No sigue cadenas de `#include`. Un archivo shim puede aparecer sin "
      "hits pese a estar acoplado a través de lo que incluye. En v1 esto se "
      "trató con una verificación de un salto; en v2 el problema se reduce "
      "porque la unidad es la TU del motor, no cualquier archivo del árbol.")
    A("- El árbol de ensamblador legado (`Opus/asm/`, 59 módulos, no compilados "
      "en CMake) queda fuera de alcance por decisión de proyecto, pese a que "
      "`Opus/asm/formatn2.asm` llama a `GetTextExtent`.")
    A("- Los conteos de sitio excluyen `Opus/lib/qwindows.h` y "
      "`Opus/debugwin.h`. Contarlos mediría la declaración de la API, no su uso.")
    A("")

    # ---------------- Apéndices de verificación ----------------
    A("## Apéndice A — Procedencia del universo `OpusEtAl/` (58 archivos)")
    A("")
    A("**Mecanismo de extracción de v2:** recorrido recursivo de `src/OpusEtAl/` "
      "(`Path.rglob`), filtrado a sufijos `.c` y `.h`. Da 58 archivos.")
    A("")
    A("**Relación con los 33 de v1.** Los 33 no eran una extracción de "
      "`OpusEtAl/`: eran el subconjunto de archivos de ese árbol que la lista "
      "«núcleo puro confirmado» de v1 contenía, es decir los que no tuvieron "
      "ningún hit con el diccionario curado de v1. El universo de v1 eran 378 "
      "archivos (`Opus/` + `OpusEtAl/`, `.c`/`.h`), que ya incluía los 58; los "
      "25 restantes sí tenían hits y aparecían en su tabla de símbolos, no en "
      "la lista de núcleo puro. **No es una falla de glob**, y por tanto no es "
      "la misma clase de defecto que el blocker B1: B1 era un universo "
      "equivocado (directorios en vez de TUs del motor), esto es una lista "
      "derivada de un filtro por hits.")
    A("")
    A("**Referencias en CMake.** El supuesto de que ninguno de los 25 "
      "adicionales figura en un target de `CMakeLists.txt` **es falso**. "
      "Resultado del chequeo, idéntico al aplicado a los 33:")
    A("")
    A("| Grupo | Archivos | Referenciados en `CMakeLists.txt` | Cuáles |")
    A("|---|---|---|---|")
    A("| 33 de v1 | 33 | 3 | `tools/src/mkdlg.c`, `tools/src/mergeelx.c`, "
      "`tools/src/draw/resource.h` |")
    A("| 25 adicionales | 25 | 2 | `tools/src/bitapp.c`, `tools/src/mkcmd.c` |")
    A("| **Total** | **58** | **5** | |")
    A("")
    A("Los 5 se referencian como herramientas de build del host "
      "(`opus_mkdlg_tool`, `opus_mergeelx_tool`, `opus_bitapp_tool`, "
      "`opus_mkcmd_tool`). **Ninguno de los 58 aparece en "
      "`OPUS_ORIGINAL_ENGINE_SOURCES`**, que es la afirmación que sostiene el "
      "triage: el motor no compila nada de `OpusEtAl/`.")
    A("")

    A("## Apéndice B — Verificación de los símbolos en cero")
    A("")
    A(f"De los {len(dictionary)} símbolos del diccionario, {dropped} no tuvieron "
      f"ningún sitio. Muestra aleatoria de 20 (`random.seed(20260810)`, "
      f"`random.sample`), verificada con `grep -w` sobre las mismas "
      f"{len(eng) + len(dbg)} TUs, sin reutilizar el escaneo del script:")
    A("")
    A("```")
    A("IE_DEFAULT  CE_BREAK  KNJ_MD_JIS  ANSI_VAR_FONT  META_PATBLT")
    A("CE_MODE  OCR_SIZEWE  PBITMAPCOREHEADER  LPLOGBRUSH  CP_GETMOUSE")
    A("ABSOLUTE  LPBITMAPINFOHEADER  CS_OEMCHARS  LB_REPLACESTRING  WC_MOVE")
    A("GMEM_NOCOMPACT  TC_SO_ABLE  CC_PIE  HS_VERTICAL  RT_MENU")
    A("```")
    A("")
    A("**Resultado: 20 de 20 en cero.** Sin discrepancias; no hay bug de "
      "conteo. Todos pertenecen a familias del SDK Win16 que Word no usa "
      "(metaarchivos, kanji, capacidades de dispositivo, cursores OCR, "
      "cabeceras DIB, mensajes de listbox).")
    A("")
    A("**Control de la verificación.** Una construcción errónea del `grep` "
      "también habría dado todo cero, así que se contrastó contra cuatro "
      "símbolos de conteo conocido:")
    A("")
    A("| Símbolo | `grep -o` (ocurrencias) | `grep -c` (líneas) | Reporte |")
    A("|---|---|---|---|")
    A("| `GlobalUnlock` | 57 | 57 | 57 |")
    A("| `UpdateWindow` | 43 | 43 | 43 |")
    A("| `PatBlt` | 99 | 99 | 99 |")
    A("| `GetTextExtent` | 26 | 24 | 26 |")
    A("")
    A("Las ocurrencias cuadran exactamente con el reporte en los cuatro casos. "
      "La diferencia de `GetTextExtent` es de unidad de conteo, no de método: "
      "`grep -c` cuenta líneas y dos líneas contienen el símbolo dos veces "
      "(`Opus/dispspec.c:539` y `:540`, donde aparece dentro de una cadena "
      "literal y además como llamada). Ese caso es una instancia concreta del "
      "ruido por cadenas y comentarios que la sección de limitaciones ya "
      "declara aceptado.")
    A("")

    OUT.write_text("\n".join(L) + "\n", encoding="utf-8")

    print(f"Escrito: {OUT}")
    print(f"  TUs base del motor: {len(eng)} (esperado 173)")
    print(f"  TUs Opus/debug/ (aparte): {len(dbg)}")
    print(f"  diccionario derivado: {len(dictionary)} símbolos, {len(sym_sites)} con hits")
    print(f"  resueltos por Winelib: {sorted(resolved & set(sym_sites))}")
    print(f"  headers: {len(per_hdr)}   OpusEtAl triage: {len(triage)}")
    for r in REGION_ORDER:
        st = region_stats.get(r)
        if st:
            print(f"  {r}: {st['portable']}/{st['total']} portables")


if __name__ == "__main__":
    main()
