# Concept: fiview — SDL2 Migration, C++20, Modernization

## Goal

fiview is an interactive filter visualizer (SDL 1.2, ~1500 lines of C).
Currently it builds via ExternalProject against SDL 1.2 (with 3 aarch64 patches).

Mid-term goal:
1. SDL 2 migration (SDL 1.2 has been end-of-life since 2012, no Wayland)
2. Establish C++20 compilability
3. Modernize fidlib integration (uses an embedded copy, should use our lib target — already done via include redirect in cmake)

---

## Phase 1 — SDL2 Migration

### Differences SDL1 → SDL2 in fiview

| Area | SDL 1.2 | SDL 2 |
|---|---|---|
| Initialization | `SDL_SetVideoMode()` | `SDL_CreateWindow()` + `SDL_CreateRenderer()` |
| Surface/pixel | `SDL_Surface *disp` directly | `SDL_Texture` + `SDL_Renderer` |
| Pixel access | `disp->pixels` directly | `SDL_LockTexture()` |
| Events | `SDL_PollEvent` (same) | `SDL_PollEvent` (largely the same) |
| Keyboard | `SDL_keysym.sym` | `SDL_Keysym.sym` (same) |
| Audio | not used | — |

fiview uses direct pixel access (`disp_pix32`, `disp_pix16`).
This is the largest change point: SDL2 only allows this through textures.

### Migration Plan

```
1. display.c: SDL_SetVideoMode → SDL_CreateWindow + SDL_CreateRenderer
   + SDL_CreateTexture (SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING)
2. Pixel write: redirect all disp_pix32[y*disp_sx+x] = col accesses into a
   managed pixel buffer; SDL_UpdateTexture() once per frame
3. graphics.c: adapt color mapping to SDL2 pixel format
4. Event loop in main (fiview.c): SDL_Quit event, SDL_WINDOWEVENT_RESIZED
5. SDL2 via ExternalProject_Add from upstream GitHub (libsdl-org/SDL)
   → analogous to SDL12_ext
```

### cmake Conversion

```cmake
# tools/fiview/CMakeLists.txt — SDL2 variant:
option(FIVIEW_USE_SDL2 "Build fiview against SDL2 instead of SDL1.2" OFF)

if(FIVIEW_USE_SDL2)
  # SDL2 via ExternalProject
  ExternalProject_Add(SDL2_ext
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        <SHA-of-last-SDL2-release>
    CMAKE_ARGS     -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
                   -DSDL_STATIC=ON -DSDL_SHARED=OFF
  )
  # SDL2 has CMakeLists.txt — no Autotools wrapper needed
  ...
else()
  # existing SDL1.2 ExternalProject remains
endif()
```

SDL 2 has its own cmake build system — no autoreconf, no
aarch64 patches needed. The build is considerably simpler than SDL 1.2.

---

## Phase 2 — C++20 Compilability

fiview uses many global `char *` string literals (help texts in
`helptext.c`). Analogous to fidlib:

- `char *` → `const char *` for all literals
- Check for VLA usage (if present)
- Pixel arithmetic: cast correctness under `-Wold-style-cast`

---

## Phase 3 — Architecture Improvements (optional)

- Filter switching without restart: `fid_run_free` + `fid_run_new` at runtime
- Multi-filter overlay: display multiple `RunBuf` instances simultaneously
- Export: output frequency response as CSV/JSON (no SDL needed → separate CLI target)

---

## Dependencies

- SDL2: upstream GitHub (libsdl-org/SDL) via ExternalProject_Add ✓
- fidlib integration via cmake include redirect: ✓ (already done)

---

*Created: 2026-05-27*
