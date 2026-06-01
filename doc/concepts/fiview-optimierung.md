# Konzept: fiview — SDL2-Migration, C++20, Modernisierung

## Ziel

fiview ist ein interaktiver Filter-Visualizer (SDL 1.2, ~1500 Zeilen C).
Aktuell baut er via ExternalProject gegen SDL 1.2 (mit 3 aarch64-Patches).

Mittelfristiges Ziel:
1. SDL 2 Migration (SDL 1.2 ist End-of-Life seit 2012, kein Wayland)
2. C++20-Kompilierbarkeit herstellen
3. fidlib-Integration modernisieren (nutzt eingebettete Kopie, soll unser lib-Target nutzen — bereits erledigt via include-Redirect in cmake)

---

## Phase 1 — SDL2-Migration

### Unterschiede SDL1 → SDL2 in fiview

| Bereich | SDL 1.2 | SDL 2 |
|---|---|---|
| Initialisierung | `SDL_SetVideoMode()` | `SDL_CreateWindow()` + `SDL_CreateRenderer()` |
| Surface/Pixel | `SDL_Surface *disp` direkt | `SDL_Texture` + `SDL_Renderer` |
| Pixel-Zugriff | `disp->pixels` direkt | `SDL_LockTexture()` |
| Events | `SDL_PollEvent` (gleich) | `SDL_PollEvent` (weitgehend gleich) |
| Keyboard | `SDL_keysym.sym` | `SDL_Keysym.sym` (gleich) |
| Audio | nicht genutzt | — |

fiview nutzt direkten Pixel-Zugriff (`disp_pix32`, `disp_pix16`).
Das ist der größte Änderungspunkt: SDL2 erlaubt das nur über Textures.

### Migrationsplan

```
1. display.c: SDL_SetVideoMode → SDL_CreateWindow + SDL_CreateRenderer
   + SDL_CreateTexture (SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING)
2. Pixel-Write: alle disp_pix32[y*disp_sx+x] = col Zugriffe in einen
   gemanagten Pixel-Buffer umleiten; SDL_UpdateTexture() einmal pro Frame
3. graphics.c: Farb-Mapping auf SDL2-Pixel-Format anpassen
4. Event-Loop in main (fiview.c): SDL_Quit-Event, SDL_WINDOWEVENT_RESIZED
5. SDL2: https://github.com/libsdl-org/SDL.git
   → ExternalProject_Add SDL2_ext analog SDL12_ext
```

### cmake-Umstellung

```cmake
# tools/fiview/CMakeLists.txt — SDL2-Variante:
option(FIVIEW_USE_SDL2 "fiview gegen SDL2 statt SDL1.2 bauen" OFF)

if(FIVIEW_USE_SDL2)
  # SDL2 via ExternalProject
  ExternalProject_Add(SDL2_ext
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        <SHA-des-letzten-SDL2-release>
    CMAKE_ARGS     -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
                   -DSDL_STATIC=ON -DSDL_SHARED=OFF
  )
  # SDL2 hat CMakeLists.txt — kein Autotools-Wrapper nötig
  ...
else()
  # bestehende SDL1.2-ExternalProject bleibt
endif()
```

SDL 2 hat ein eigenes cmake-Build-System — kein autoreconf, keine
aarch64-Patches nötig. Der Build ist erheblich einfacher als SDL 1.2.

---

## Phase 2 — C++20-Kompilierbarkeit

fiview nutzt viele globale `char *` String-Literale (Hilfetexte in
`helptext.c`). Analog zu fidlib:

- `char *` → `const char *` für alle Literale
- VLA-Nutzung (falls vorhanden) prüfen
- Pixel-Arithmetik: Cast-Korrektheit unter `-Wold-style-cast`

---

## Phase 3 — Architektur-Verbesserungen (optional)

- Filter-Wechsel ohne Neustart: `fid_run_free` + `fid_run_new` zur Laufzeit
- Multi-Filter-Overlay: mehrere `RunBuf`-Instanzen gleichzeitig darstellen
- Export: Frequenzgang als CSV/JSON ausgeben (ohne SDL nötig → eigenes CLI-Target)

---

## Abhängigkeiten

- SDL2: https://github.com/libsdl-org/SDL.git
- fidlib-Integration via cmake-Include-Redirect: ✓ (bereits erledigt)

---

*Erstellt: 2026-05-27*
