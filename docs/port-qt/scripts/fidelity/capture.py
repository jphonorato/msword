#!/usr/bin/env python3
"""docs/port-qt/scripts/fidelity/capture.py

Regenera src/core/src/opus_shell_font_metrics_oracle_table.h a partir de
mediciones reales contra el oraculo Winelib (via gdi_metrics.c, ya
compilado como gdi_metrics.exe en este mismo directorio -- ver README.md
para el comando de compilacion con winegcc).

Uso:
    winegcc -o gdi_metrics.exe gdi_metrics.c -lgdi32 -luser32
    python3 capture.py > ../../../../src/core/src/opus_shell_font_metrics_oracle_table.h

No forma parte del build -- se ejecuta a mano cuando cambia la version de
Wine/fuentes instaladas y hace falta re-capturar el oraculo (docs/port-qt/
01-core-shell-boundary.md Sec.B2.3, ultima linea de la seccion).
"""
import subprocess, os

sizes = [8, 10, 12, 14, 18, 24, 36]
faces = ["Liberation Serif", "Liberation Sans", "Liberation Mono"]
env = dict(os.environ)
env["WINEDEBUG"] = "-all"

data = {}  # (face, pt) -> {"ascent":x,"descent":y,"widths":[...]}
for face in faces:
    for pt in sizes:
        out = subprocess.run(["wine", "gdi_metrics.exe", face, str(pt)],
                              capture_output=True, text=True, env=env).stdout
        ascent = descent = overhang = None
        widths = [0]*95
        for line in out.splitlines():
            if line.startswith("GDI tm"):
                parts = dict(p.split("=") for p in line.split()[2:])
                ascent = int(parts["ascent"])
                descent = int(parts["descent"])
                overhang = int(parts["overhang"])
            elif line.startswith("GDI adv"):
                _, _, ch, w = line.split()
                widths[int(ch)-32] = int(w)
        data[(face, pt)] = {"ascent": ascent, "descent": descent,
                             "overhang": overhang, "widths": widths}

# emit C++ arrays
eras = [("Tms Rmn", "Liberation Serif"), ("Symbol", "Liberation Sans"),
        ("Helv", "Liberation Sans"), ("Courier", "Liberation Mono")]

lines = []
lines.append("#pragma once")
lines.append("")
lines.append("/* Generado por docs/port-qt/scripts/fidelity/capture.py contra Wine 10.0,")
lines.append("   Debian trixie (mismo entorno de Sec.B2.3/B2.6). No editar a mano --")
lines.append("   regenerar con la sonda si cambia la version de Wine/fuentes.")
lines.append("")
lines.append("   Tabla capturada una vez del oraculo (Opus/LOADFONT.C:187 C_LoadFcid,")
lines.append("   via Opus/dispspec.c:787,796 GetTextExtent-tmOverhang -- reproducido")
lines.append("   aqui por gdi_metrics.c bajo wine real), usada como snapshot de")
lines.append("   regresion en cada cambio del shell -- no se re-mide el oraculo en cada")
lines.append("   corrida de test (Sec.B2.3, ultima linea: \"La tabla capturada del")
lines.append("   oraculo deja de ser mecanismo y queda solo como prueba de regresion\").")
lines.append(f"   {len(eras)*len(sizes)} filas ({len(eras)} nombres de epoca x {len(sizes)} tamanos: "
              + ",".join(str(s) for s in sizes) + "pt) x 95")
lines.append("   caracteres ASCII imprimibles = "
              + str(len(eras)*len(sizes)*95) + " puntos de dato. */")
lines.append("")
lines.append("struct OracleRow { const char *eraName; int pt; int ascent; int descent;")
lines.append("                   int overhang; unsigned short widths[95]; };")
lines.append("")
lines.append("static const OracleRow kOracleTable[] = {")
for eraName, face in eras:
    for pt in sizes:
        d = data[(face, pt)]
        w = ",".join(str(x) for x in d["widths"])
        lines.append(f'    {{"{eraName}", {pt}, {d["ascent"]}, {d["descent"]}, {d["overhang"]}, {{{w}}}}},')
lines.append("};")
lines.append("")
lines.append("static const int kOracleTableCount = sizeof(kOracleTable)/sizeof(kOracleTable[0]);")

print("\n".join(lines))
