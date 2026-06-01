# TODO: fiview — SDL2-Migration, C++20 ✓

Branch: `feature/fiview-sdl2` → gemergt nach develop
Konzept: `doc/concepts/fiview-optimierung.md`

## Phase 1 — SDL2-Migration
- [x] SDL2-ExternalProject in `fiview/CMakeLists.txt` (Option `FIVIEW_USE_SDL2`)
- [x] `graphics.c`: SDL2-Globals (sdl_window, sdl_renderer, sdl_texture), `graphics_init()` SDL2-Pfad
- [x] Pixel-Buffer: `disp_pix32` als eigener Heap-Buffer, `disp_pix16=NULL` für SDL2
- [x] `graphics.c`: Farb-Mapping auf `SDL_PIXELFORMAT_ARGB8888` (disp_rl/rs/gl/gs/bl/bs fix)
- [x] `graphics.c`: `update()`/`update_force()`: SDL_UpdateTexture + RenderCopy + RenderPresent
- [x] `fiview.c`: `SDL_VIDEORESIZE` → `SDL_WINDOWEVENT_RESIZED`
- [x] `fiview.c`: `SDL_EnableKeyRepeat` entfernt für SDL2 (in SDL2 eingebaut)
- [x] `all.h`: SDL-Include-Pfad konditionell (`SDL2/SDL.h` vs `SDL/SDL.h`)
- [x] `proto.h`: `SDL_Surface *disp` für SDL2 ausgeblendet
- [x] SDL1-Build fehlerfrei (rückwärtskompatibel)
- [x] Manuelle Sichtprüfung SDL1: fiview startet, Frequenzgang korrekt (X11-Screenshot auf Pi)
- [x] Manuelle Sichtprüfung SDL2: SDL2 via FIVIEW_SDL_UPSTREAM=ON gebaut, 4s Lauf ohne Crash (DISPLAY:0, Pi 5)

## Phase 2 — C++20-Kompilierbarkeit
- [x] `helptext.c`: Doppeldefinition behoben — gen_helptext() nach helptext_src-Definition verschoben
- [x] `helptext.c`: `static const char *helptext_src=`, `const char *p` in gen_helptext()
- [x] `all.h`: `#include "fidlib/fidlib.h"` → `#include <fidlib/fidlib.h>` (cmake-Redirect aktiv)
- [x] `FIVIEW_CXX20_COMPAT` cmake-Option: -std=c++20 -fpermissive (Vendor-Code mit -w)
- [x] VLA-Prüfung: keine VLAs im fiview-Code gefunden
- [x] Build mit `-std=c++20` fehlerfrei

## Phase 3 — Architektur (optional, eigene Tickets)
- [x] Filter-Wechsel zur Laufzeit ohne Neustart (F5-Taste → Prompt → filter_load_immed)
- [x] Multi-Filter-Overlay (o-Taste toggle, alle geladenen Filter in gelb überlagert)
- [x] Frequenzgang-Export als CSV (e-Taste → fiview_freq.csv)

## Abschluss
- [x] Bestehender SDL1.2-Build weiterhin funktionsfähig (Rückwärtskompatibilität)
- [x] Commit + Merge → develop
