<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2025-2026 Jörg Simbrig -->

# Lösung: GPU-beschleunigte FIR-Filterung via Vulkan (Raspberry Pi 5)

## Das Problem

Ein sehr langer FIR-Filter (4096 Taps, z.B. Raumimpulsantwort / Faltungshall)
soll auf dem Raspberry Pi 5 in Echtzeit verarbeitet werden. Selbst die
Overlap-Save-FFT-Engine stößt bei sehr langen Filtern an CPU-Grenzen.
Der VideoCore VII (V3D 7.1) des RPi 5 unterstützt Vulkan 1.2 mit Compute Shaders —
der GPU-Kern soll die parallele Faltung übernehmen.

**Anforderungen:**
- FIR-Filter mit ≥ 256 Taps auf GPU auslagern
- Vulkan 1.2 Compute via Mesa V3DV (kein proprietärer Treiber)
- Transparenter Fallback: falls GPU nicht verfügbar → OLA/FFT oder NEON
- Kein API-Unterschied zur CPU-Filterung

---

## Welche Mittel des Projekts helfen

- **`FIDLIB_VULKAN=ON`** — aktiviert Vulkan-Compute-Backend in `fid_vulkan.h`
- **`fir_dot.comp`** — GLSL Compute Shader (64 Threads, FP32)
- **`FIDLIB_VULKAN_THRESHOLD`** — ab dieser Tap-Anzahl wird GPU bevorzugt (Standard: 256)
- **`FIDLIB_VULKAN_BATCH`** — Anzahl Samples pro Dispatch (Standard: 256)
- **`spv_to_header.cmake`** — Shader wird beim Build zu SPIR-V kompiliert und
  als C-Array eingebettet; keine externe Shader-Datei zur Laufzeit nötig

---

## Schritt 1: Abhängigkeiten installieren

```bash
sudo aptitude install libvulkan-dev glslang-tools spirv-tools vulkan-tools

# Prüfen ob V3D 7.1 gefunden wird:
vulkaninfo --summary
# Erwartet: deviceName = V3D 7.1, apiVersion = 1.2.xxx, deviceType = INTEGRATED_GPU
```

Falls `vulkaninfo` keine GPU zeigt:
```bash
# Mesa V3DV aktivieren (sollte in Bookworm automatisch aktiv sein):
ls /usr/share/vulkan/icd.d/
# Erwartet: broadcom_icd.aarch64.json oder ähnlich
```

## Schritt 2: cmake-Build mit Vulkan

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_SIMD=ON \
      -DFIDLIB_FFT=ON \
      -DFIDLIB_VULKAN=ON \
      -S . -B build_vk

cmake --build build_vk -j$(nproc)
```

cmake-Ausgabe (relevante Zeilen):
```
-- Kompiliere fir_dot.comp → SPIR-V
-- Bette SPIR-V als C-Array ein → fir_dot_spv.h
-- fidlib VULKAN: Vulkan 1.2.xxx, Batch=256, Threshold=256
```

Falls cmake `FIDLIB_VULKAN` deaktiviert:
```
-- WARNING: fidlib VULKAN: kein GLSL-Compiler (glslc/glslangValidator) gefunden
```
→ `sudo aptitude install glslang-tools` und cmake neu.

## Schritt 3: Vulkan-Initialisierung verstehen

Die Vulkan-Engine initialisiert sich **lazy** beim ersten `fid_run_new`-Aufruf
mit einem FIR-Filter ≥ Threshold. Falls die GPU nicht gefunden wird oder der
Vulkan-Init fehlschlägt, fällt `fid_run_new` automatisch auf OLA/FFT zurück:

```
fid_run_new(filt, &fn) wird aufgerufen
    → Tap-Anzahl ≥ FIDLIB_VULKAN_THRESHOLD?
    → vk_init() (einmalig): VkInstance → PhysicalDevice → Device → Queue
    → Findet kein Gerät → gibt NULL zurück → nächste Stufe (OLA/FFT)
```

## Schritt 4: Programm unverändert — gleiche API

```c
#include <fidlib/fidlib.h>
#include <stdlib.h>

// Exakt dieselbe API wie ohne Vulkan:
FidFilter *filt = fid_design("...", 44100.0, -1.0, -1.0, 0, NULL);
// Hier muss filt ein FIR-Filter mit ≥ 256 Taps sein — z.B. via fid_cv_array

FidFunc *step_fn;
void    *run = fid_run_new(filt, &step_fn);
// ^ Wählt automatisch: OpenCL → Vulkan → OLA → Scalar

void *buf = fid_run_newbuf(run);
free(filt);

// RT-Phase:
double out = step_fn(buf, input_sample);
// ^ Intern: Samples werden gebuffert, bei B Samples → GPU-Dispatch → Ausgabe

// Cleanup:
fid_run_freebuf(buf);
fid_run_free(run);
```

## Schritt 5: Batch-Mechanismus verstehen

Das Vulkan-Backend sammelt Samples im Host-Buffer bis `FIDLIB_VULKAN_BATCH`
Samples vorliegen, dann:

1. Koeffizienten + Eingabepuffer → GPU-VRAM (Host-Visible Buffer, unified memory)
2. Compute Dispatch: `ceil(B/64)` Workgroups, 64 Threads je → parallele FIR-Faltung
3. `vkQueueWaitIdle` — warten bis GPU fertig
4. Ausgabepuffer von GPU lesen → Sample-by-Sample weiterliefern

Die Ausgabe kommt erst nach `FIDLIB_VULKAN_BATCH` Eingabe-Samples (= Batch-Latenz).
Bei Batch=256 und 44100 Hz: ~5.8 ms Latenz.

**Batch-Größe anpassen:**
```bash
# Größere Batches → weniger Dispatch-Overhead, mehr Latenz:
cmake ... -DFIDLIB_VULKAN_BATCH=1024 ...

# Kleinere Batches → weniger Latenz, mehr Overhead:
cmake ... -DFIDLIB_VULKAN_BATCH=64 ...
```

## Schritt 6: Dispatch-Priorität kontrollieren

Wenn sowohl `FIDLIB_VULKAN=ON` als auch `FIDLIB_OPENCL=ON` und `FIDLIB_FFT=ON`:

```
OpenCL (höchste Priorität) → Vulkan → OLA/FFT → NEON/Scalar
```

Vulkan ohne OpenCL:
```bash
cmake ... -DFIDLIB_VULKAN=ON -DFIDLIB_OPENCL=OFF ...
```

Dann ist Vulkan die erste GPU-Option.

## Schritt 7: Benchmark — CPU vs. GPU

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_FFT=ON -DFIDLIB_VULKAN=ON -DBUILD_BENCHMARKS=ON \
      -S . -B build_bench

cmake --build build_bench --target bench_fir_backends -j$(nproc)
./build_bench/bin/bench_fir_backends
```

CSV-Ausgabe enthält `backend`-Spalte: `scalar`, `neon`, `ola_fftw3`, `vulkan`.

## Schritt 8: Test ausführen

```bash
ctest --test-dir build_vk -R fidlib_vulkan --output-on-failure
```

Der Test prüft Korrektheit des Vulkan-Backends gegen die Direktfaltung.
Falls keine GPU gefunden wird, überspringt er sich mit `SKIP`.

---

## Wie der Shader funktioniert

`fidlib/fir_dot.comp` (GLSL Compute Shader):
```glsl
layout(local_size_x = 64) in;   // 64 Threads pro Workgroup
layout(push_constant) uniform Params { int M; int B; } params;
// CoefBuf: FIR-Koeffizienten (M Werte, FP32)
// InputBuf: Eingabe-Ring mit M-1 Vorgeschichte + B neue Samples
// OutBuf: B Ausgabe-Samples

void main() {
    int i = int(gl_GlobalInvocationID.x);  // Sample-Index
    if (i >= params.B) return;
    float sum = 0.0;
    for (int k = 0; k < params.M; k++)
        sum += coef[k] * x[i + params.M - 1 - k];
    y[i] = sum;
}
```

Jeder Thread berechnet einen Ausgabe-Sample unabhängig → massiv parallel.
Bei B=256 und 64 Threads: 4 Workgroups, alle parallel auf der GPU.

---

## Unified Memory auf RPi 5

Der VideoCore VII hat unified Memory (DRAM wird von CPU und GPU geteilt).
Das Vulkan-Backend erkennt dies via `VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT` und vermeidet unnötige Kopien —
der Buffer ist direkt von beiden Seiten zugreifbar.

---

## Problemlösungen

| Problem | Ursache | Lösung |
|---------|---------|--------|
| cmake deaktiviert VULKAN | libvulkan-dev oder glslangValidator fehlt | Pakete installieren, cmake neu |
| `vk_init()` findet kein Gerät | Kein Vulkan-ICD geladen | `ls /usr/share/vulkan/icd.d/` prüfen |
| Test überspringt sich (SKIP) | Kein Vulkan-Device zur Laufzeit | Erwartet wenn kein GPU — kein Fehler |
| Falsche Ausgabe-Werte | FP32-Präzision bei langen Filtern | Vulkan rechnet FP32; für FP64 OLA/NEON verwenden |
| Hohe Latenz | Batch-Größe zu groß | `FIDLIB_VULKAN_BATCH` reduzieren |
