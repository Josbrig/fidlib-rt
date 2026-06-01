# TODO: GPU-Beschleunigung — Umsetzungsplan

Stand: 2026-05-28  
Branch: feature/gpu-beschleunigung  
Basis: `doc/concepts/gpu-beschleunigung-analyse.md`

---

## Designprinzipien

- IIR-Filter bleiben **immer** FP64 auf der CPU (numerische Stabilität, serielle Abhängigkeit)
- FIR-Filter erhalten optionalen FP32-Pfad (NEON und GPU)
- Systeme ohne GPU bleiben vollständig kompatibel — kein Pflicht-Dependency
- Compile-Zeit-Auswahl über cmake-Optionen; Runtime-Dispatch nur innerhalb eines gewählten Pfads
- Koeffizientendesign (`fidmkf.h`, `fidlib.c`) bleibt immer FP64

---

## Phase 1 — Präzisions-Template-Fundament

Voraussetzung für alle weiteren Phasen. Kein GPU-Code, aber die Grundlage für FP32-Pfade.

- [ ] **1.1** `fidrf_cmdlist.h`: `double` im Execution-Hotpath auf `FID_REAL` umstellen  
      44 Stellen; `FID_REAL` per cmake-Option auf `float` oder `double` setzbar.  
      IIR-Opcodes (16, 18, 19, 21): Warnung oder Compilerfehler wenn `FID_REAL=float`.

- [ ] **1.2** cmake-Option `FIDLIB_PRECISION` anlegen  
      Werte: `double` (Default), `float`  
      Erkennung: wenn `aarch64` → Default bleibt `double`; FP32 nur explizit opt-in.  
      Definiert `FID_REAL` als Compile-Definition für fidlib und alle abhängigen Targets.

- [ ] **1.3** `fid_simd.h`: FP32-NEON-Variante ergänzen  
      `fid_fir_dot_f32()`: `float32x4_t` statt `float64x2_t`, 4 Elemente/Iteration statt 2.  
      SSE2-Analogon: `_mm_mul_ps` / `_mm_add_ps` (4×float statt 2×double).  
      Beide Varianten koexistieren; Auswahl über `FID_REAL`.

- [ ] **1.4** `FidFunc`-Typedef erweitern  
      `typedef double (FidFunc)(void *buf, double input);` bleibt für FP64-Pfad.  
      Neues `typedef float (FidFuncF32)(void *buf, float input);` für FP32-Pfad.  
      `fid_run_new()` gibt je nach `FID_REAL` den passenden Funktionszeiger zurück.

- [ ] **1.5** Koeffizientenkonvertierung bei `fid_run_new()`  
      Wenn `FID_REAL=float`: FP64-Koeffizienten aus `FidFilter.val[]` nach FP32 konvertieren  
      und im `FidRun`-Objekt als `float`-Array ablegen.  
      Einmalig, nicht pro Sample — kein RT-Overhead.

- [ ] **1.6** Test: FP32 vs. FP64 Präzisionsvergleich  
      Neuer Test `test_fidlib_precision.c`:  
      FIR-Filter mit bekannter Impulsantwort in FP64 und FP32 berechnen.  
      Prüfen dass FP32-Ergebnis innerhalb akzeptabler Toleranz liegt (< 1e-6 relativ).  
      IIR-Filter in FP32: Instabilitätswarnung auslösen oder Test schlägt fehl.

---

## Phase 2 — CPU FFT Overlap-Add (FP64, kein GPU)

Höchste Priorität für lange FIR-Filter. Kein GPU-Overhead, FP64-sicher, alle Zielsysteme.

- [ ] **2.1** cmake: `FIDLIB_FFT` Option anlegen  
      Sucht nach FFTW3 (`find_package(FFTW3)`).  
      Fallback auf KissFFT (Header-only, im Repo einbettbar) wenn FFTW3 nicht gefunden.  
      Kompatibel mit RPi 3+, Desktop, Jetson — überall wo libfftw3 verfügbar.

- [ ] **2.2** `fidlib/fid_fft.h` / `fidlib/fid_fft.c`: Overlap-Add-Engine  
      API: `FidFftPlan *fid_fft_plan(const FidFilter *fir, int block_size);`  
      `void fid_fft_execute(FidFftPlan *plan, const double *in, double *out, int n);`  
      Intern: H(f) vorberechnen (FFT der Koeffizienten), Overlap-Save pro Block.  
      Automatisch aktiv wenn FIR-Länge > `FIDLIB_FFT_THRESHOLD` (Default: 512 Taps).

- [ ] **2.3** Integration in `fidrf_cmdlist.h` Opcode 8  
      Wenn `FIDLIB_FFT` aktiv und FIR-Länge ≥ Schwellwert:  
      Block-Modus statt Sample-Modus. `fid_run_new()` wählt Pfad automatisch.  
      Fallback auf NEON-Pfad bei kürzeren Filtern oder wenn `FIDLIB_FFT` nicht aktiv.

- [ ] **2.4** Test: Overlap-Add Korrektheit  
      Gleiche Impulsantwort wie direkter FIR-Pfad (Ergebnis muss bitidentisch bis Rundungsfehler sein).  
      Randbedingungen: Blockgrenzen, nicht-vielfache Blocklängen, Filter-Tail nach Impuls.

---

## Phase 3 — Vulkan Compute (FP32, RPi 4/5)

Voraussetzung: Phase 1 abgeschlossen. Nur für FIR, nur FP32.

- [ ] **3.1** cmake: `FIDLIB_VULKAN` Option anlegen  
      `find_package(Vulkan)` — wenn nicht gefunden, Option automatisch OFF.  
      Benötigt: `libvulkan-dev`, `glslang-tools` (für `glslc`).  
      Auf RPi 4/5 mit Mesa V3DV sofort nutzbar (keine Zusatzpakete).  
      GPU-freie Systeme: Option bleibt OFF, kein Effekt auf Verhalten.

- [ ] **3.2** `fidlib/fid_vulkan.h` / `fidlib/fid_vulkan.c`: Vulkan-Kontext  
      Einmalige Initialisierung: `VkInstance`, `VkPhysicalDevice`, `VkDevice`, `VkQueue`.  
      Compute-Queue suchen (nicht Graphics-Queue).  
      `fid_vulkan_init()` / `fid_vulkan_shutdown()` — lazy init, beim ersten GPU-Aufruf.  
      Fehlerfall (kein Vulkan-Device): transparenter Fallback auf NEON/Skalar.

- [ ] **3.3** `fidlib/shaders/fir_dot.comp`: GLSL Compute Shader  
      Eingabe: SSBO mit `float` Koeffizienten, SSBO mit `float` Samples.  
      Ausgabe: SSBO mit `float` Resultaten (ein Wert pro Workgroup).  
      Workgroup-Größe: 64 oder 256 Threads (per Spezialisierungskonstante konfigurierbar).  
      cmake baut `.comp` → `.spv` (SPIR-V) zur Build-Zeit via `glslc`.

- [ ] **3.4** `fidlib/fid_vulkan.c`: Buffer-Management  
      Host-sichtbare `VkBuffer` für Koeffizienten (einmalig bei `fid_run_new()`).  
      Host-sichtbare `VkBuffer` für Eingangs-/Ausgangsdaten (pro Block).  
      Auf RPi 5 (unified memory): `VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT` ohne Transfer-Overhead.

- [ ] **3.5** Auto-Dispatch in `fidrf_cmdlist.h` Opcode 8  
      Entscheidungslogik:  
      - FIR-Länge < 64 Taps → NEON (Overhead überwiegt)  
      - FIR-Länge 64–512 Taps → NEON-FP32 (GPU marginal)  
      - FIR-Länge > 512 Taps + `FIDLIB_VULKAN` aktiv + Block-Modus → GPU  
      - Sonst → NEON / Skalar  
      Schwellwerte als cmake-Defines überschreibbar.

- [ ] **3.6** Test: Vulkan FIR Korrektheit  
      `test_fidlib_vulkan.c`: Skip wenn kein Vulkan-Device.  
      Gleiche Impulsantwort wie CPU-Pfad, Toleranz 1e-6 (FP32-Genauigkeit).  
      Testen mit 64, 256, 1024 Taps.

---

## Phase 4 — OpenCL (optional, Desktop / Jetson)

Niedriger Priorität. Nicht für RPi (Clover ohne V3D-Pipe, kein GPU-Nutzen).

- [ ] **4.1** cmake: `FIDLIB_OPENCL` Option anlegen  
      `find_package(OpenCL)` — wenn nicht gefunden, Option automatisch OFF.  
      Explizite Ausschlussbedingung: wenn `CMAKE_SYSTEM_PROCESSOR` matches `aarch64` und  
      kein Rusticl-ICD nachweisbar → Warnung ausgeben, Option auf OFF erzwingen.  
      Zielplattformen: x86_64 (AMD/NVIDIA), Jetson (CUDA-OpenCL-ICD).

- [ ] **4.2** `fidlib/fid_opencl.h` / `fidlib/fid_opencl.c`  
      Analog zu Vulkan-Backend: Kontext, Queue, Buffer, Kernel-Launch.  
      `fidlib/kernels/fir_dot.cl`: OpenCL C Kernel (FP32).  
      cmake kompiliert Kernel zur Laufzeit via `clBuildProgram` (kein SPIR-V nötig).

- [ ] **4.3** Test: OpenCL FIR Korrektheit  
      Skip wenn kein OpenCL-Platform mit GPU-Device (nicht CPU-Fallback).

---

## Phase 5 — Benchmarks

- [ ] **5.1** `tests/bench_fir_backends.c`  
      Misst Durchsatz (Samples/s) für alle verfügbaren Pfade:  
      Skalar-FP64, NEON-FP64, NEON-FP32, FFT-Overlap-Add, Vulkan (falls aktiv), OpenCL (falls aktiv).  
      FIR-Längen: 16, 64, 256, 512, 1024, 4096 Taps.  
      Ausgabe als CSV für Auswertung.

- [ ] **5.2** cmake: `BUILD_BENCHMARKS` Option  
      Separates cmake-Target, nicht Teil von `ctest` (Benchmarks sind nicht deterministisch).

---

## Kompatibilitätsinvarianten (müssen in jedem Schritt gelten)

| Bedingung | Invariante |
|---|---|
| `FIDLIB_VULKAN=OFF` (Default) | Kein Vulkan-Header included, kein Linker-Dependency |
| `FIDLIB_OPENCL=OFF` (Default) | Kein OpenCL-Header included, kein Linker-Dependency |
| `FIDLIB_FFT=OFF` (Default) | Kein FFTW3/KissFFT-Dependency |
| `FIDLIB_PRECISION=double` (Default) | Kein Verhaltensunterschied zu aktuellem Stand |
| IIR-Filter immer | FP64, CPU, kein GPU-Dispatch, kein Ausnahme |
| Kein GPU-Device zur Laufzeit | Transparenter Fallback auf NEON/Skalar, kein Fehler |
| RPi 1/2/3 | Alle Optionen kompilierbar, GPU-Optionen ohne Effekt |

---

## Reihenfolge-Abhängigkeiten

```
Phase 1 (Präzisions-Template)
  └── Phase 2 (FFT, FP64) — unabhängig von Phase 1, paralleler Start möglich
  └── Phase 3 (Vulkan)    — benötigt Phase 1 (FP32-Pfad)
       └── Phase 4 (OpenCL) — unabhängig von Phase 3, nach Phase 1
            └── Phase 5 (Benchmarks) — nach Phasen 1–4
```
