# TODO: fiview — SDL2 Migration, C++20 ✓

Branch: `feature/fiview-sdl2` → merged into develop
Concept: `doc/concepts/fiview-optimierung.md`

## Phase 1 — SDL2 Migration
- [x] SDL2 ExternalProject in `fiview/CMakeLists.txt` (option `FIVIEW_USE_SDL2`)
- [x] `graphics.c`: SDL2 globals (sdl_window, sdl_renderer, sdl_texture), `graphics_init()` SDL2 path
- [x] Pixel buffer: `disp_pix32` as own heap buffer, `disp_pix16=NULL` for SDL2
- [x] `graphics.c`: color mapping to `SDL_PIXELFORMAT_ARGB8888` (disp_rl/rs/gl/gs/bl/bs fixed)
- [x] `graphics.c`: `update()`/`update_force()`: SDL_UpdateTexture + RenderCopy + RenderPresent
- [x] `fiview.c`: `SDL_VIDEORESIZE` → `SDL_WINDOWEVENT_RESIZED`
- [x] `fiview.c`: `SDL_EnableKeyRepeat` removed for SDL2 (built into SDL2)
- [x] `all.h`: SDL include path conditional (`SDL2/SDL.h` vs `SDL/SDL.h`)
- [x] `proto.h`: `SDL_Surface *disp` hidden for SDL2
- [x] SDL1 build error-free (backward compatible)
- [x] Manual visual inspection SDL1: fiview starts, frequency response correct (X11 screenshot on Pi)
- [x] Manual visual inspection SDL2: SDL2 built via FIVIEW_SDL_UPSTREAM=ON, 4s run without crash (DISPLAY:0, Pi 5)

## Phase 2 — C++20 Compilability
- [x] `helptext.c`: duplicate definition fixed — gen_helptext() moved after helptext_src definition
- [x] `helptext.c`: `static const char *helptext_src=`, `const char *p` in gen_helptext()
- [x] `all.h`: `#include "fidlib/fidlib.h"` → `#include <fidlib/fidlib.h>` (cmake redirect active)
- [x] `FIVIEW_CXX20_COMPAT` cmake option: -std=c++20 -fpermissive (vendor code with -w)
- [x] VLA check: no VLAs found in fiview code
- [x] Build with `-std=c++20` completes without errors

## Phase 3 — Architecture (optional, separate tickets)
- [x] Filter switching at runtime without restart (F5 key → prompt → filter_load_immed)
- [x] Multi-filter overlay (o key toggle, all loaded filters overlaid in yellow)
- [x] Frequency response export as CSV (e key → fiview_freq.csv)

## Completion
- [x] Existing SDL1.2 build still functional (backward compatibility)
- [x] Commit + merge → develop
