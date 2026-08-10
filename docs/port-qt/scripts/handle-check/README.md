# Verificación de round-trip de handle real (B3, Fase 3)

Programa de un solo uso que probó, con un handle/puntero real (no un valor
por copia como `link-check/`), que la frontera memoria-Win16 sostiene
ownership y ciclo de vida a través de winegcc/wineg++. No forma parte del
build.

Ver "Verificación B3: round-trip de handle real" en
`docs/port-qt/01-frontera-nucleo-shell.md` para el detalle completo.

| Archivo | Qué hace |
|---|---|
| `handle_check.c` | Alloc → Lock → escribe patrón → Unlock → Lock → verifica patrón → `OpusMemHandle` (puntero→handle) → Free → Lock tras Free (debe dar NULL). Con `--double-free`: Alloc → Free → Free (debe abortar) |
| `run.sh` | Compila y enlaza con `wineg++`, corre ambos modos, verifica los códigos de salida |

Requiere `opus_core_build` ya compilado:

```sh
cmake --build build/linux-winelib-debug --target opus_core_build
docs/port-qt/scripts/handle-check/run.sh
```

El volcado de WineDbg al correr `--double-free` es esperado: Wine
intercepta el `SIGABRT` de `abort()` y muestra su propio diagnóstico de
crash antes de terminar con exit 134 (128+SIGABRT). Eso es exactamente la
"falla controlada, no corrupción silenciosa" que la prueba busca
confirmar, no un error de la sonda.
