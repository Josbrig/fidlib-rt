# GPU- und Koprozessor-Beschleunigung — Analyse

Stand: 2026-05-28  
Branch: feature/gpu-beschleunigung  
Primäre Zielplattform: Raspberry Pi 5 (BCM2712, Cortex-A76, VideoCore VII)

---

## 1  Ausgangslage

Die CPU-seitige SIMD/NEON-Beschleunigung (`fid_simd.h`) beschleunigt den FIR-Hotpath
bereits erheblich (bis 3,8× bei langen Filtern). Dieser Bericht analysiert, ob und wo
GPU-basierte oder Koprozessor-basierte Erweiterungen darüber hinaus sinnvoll sind.

---

## 2  Kritischer Vorbehalt: FP64 auf VideoCore-GPUs

fidlib rechnet durchgehend mit `double` (IEEE 754 FP64, 64 Bit). Alle VideoCore-GPUs
(RPi 1–5) unterstützen FP64 auf einer GPU **nicht nativ**:

| GPU-Generation | FP32 | FP64 |
|---|---|---|
| VideoCore IV (RPi 1–3) | QPU, nativ | nicht vorhanden |
| VideoCore VI (RPi 4) | nativ | nicht vorhanden |
| VideoCore VII (RPi 5, V3D 7.1) | nativ | nicht vorhanden |

Ein GPU-Pfad für fidlib würde zwingend FP32 erzwingen oder FP64 software-emuliert
(~8–16× langsamer als FP32) ausführen — beides ist in den meisten Szenarien keine
valide Option für numerisch präzise Filterberechnungen.

**Konsequenz:** GPU-Einsatz ist nur sinnvoll wenn entweder  
(a) bewusst FP32-Präzision akzeptiert wird (Audio-Anwendungen, Visualisierung), oder  
(b) die GPU für Aufgaben außerhalb des Filterhotpaths verwendet wird (Koeffizientenberechnung,
FFT-basierte Faltung mit Overlap-Add).

---

## 3  Raspberry Pi — Hardware-Überblick nach Modell

### 3.1  Tabelle: Was steckt in welchem Pi?

| Modell | SoC | CPU-Kern | NEON | GPU | GPU-Compute |
|---|---|---|---|---|---|
| RPi 1 | BCM2835 | ARM1176JZF-S (ARMv6) | Nein | VideoCore IV | Nein (kein API) |
| RPi 2 v1.1 | BCM2836 | Cortex-A7 (ARMv7) | 32-Bit | VideoCore IV | Nein |
| RPi 2 v1.2 | BCM2837 | Cortex-A53 (ARMv8) | 64-Bit | VideoCore IV | Nein |
| RPi 3 B/B+ | BCM2837 | Cortex-A53 (ARMv8) | 64-Bit | VideoCore IV | Nein |
| RPi 4 B | BCM2711 | Cortex-A72 (ARMv8.2) | 64-Bit | VideoCore VI | Ja (Vulkan 1.1, OpenCL 1.2†) |
| RPi 5 | BCM2712 | Cortex-A76 (ARMv8.2) | 64-Bit | VideoCore VII | Ja (Vulkan 1.2, OpenCL†) |
| RPi 400 | BCM2711 | wie RPi 4 | 64-Bit | VideoCore VI | wie RPi 4 |
| RPi CM4 | BCM2711 | wie RPi 4 | 64-Bit | VideoCore VI | wie RPi 4 |
| RPi Zero 2 W | BCM2837B0 | Cortex-A53 | 64-Bit | VideoCore IV | Nein |

† = erfordert optionale Paketinstallation; native FP64 nicht vorhanden

### 3.2  CPU-Features auf diesem System (RPi 5)

```
fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp
cpuid asimdrdm lrcpc dcpop asimddp
```

Wichtig: **kein `sve`** — Scalable Vector Extension ist auf dem Cortex-A76 nicht
implementiert (ARMv8.2 macht SVE optional; der A76 verzichtet darauf).

---

## 4  Verfügbare Compute-APIs auf Raspberry Pi

### 4.1  Vulkan Compute

Auf diesem System **sofort nutzbar**:

```
GPU0: V3D 7.1.10.2 — Vulkan 1.2.289 — PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU
Treiber: mesa-vulkan-drivers 24.2.8 (installiert)
```

Vulkan Compute bedeutet: beliebige Berechnungen auf der GPU per Compute Shader (GLSL/SPIR-V),
ohne Grafik-Kontext. Die API ist low-level, verbos, aber portabel und auf RPi 4/5 produktiv
einsetzbar.

| Eigenschaft | RPi 4 | RPi 5 |
|---|---|---|
| Vulkan-Version | 1.1 | 1.2 |
| Treiber | Mesa V3DV | Mesa V3DV |
| `VK_KHR_storage_buffer_storage_class` | Ja | Ja |
| Compute Shader | Ja | Ja |
| FP64-Extension | Nein | Nein |
| Workgroup-Größe max. | 256 | 256 |
| Installationsaufwand | keiner (Mesa) | keiner (Mesa) |

### 4.2  OpenCL

`mesa-opencl-icd` 24.2.8 ist auf diesem System installiert. Tatsächliche GPU-Nutzung
ist damit jedoch **nicht** gegeben:

**Implementierung: Clover** — das ältere Gallium-basierte OpenCL von Mesa.
Clover benötigt einen hardware-spezifischen Pipe-Treiber (`pipe_v3d.so` für VideoCore VII),
der im Debian-Paket **nicht enthalten** ist. Die vorhandenen Pipe-Treiber decken
AMD, NVIDIA, Qualcomm Adreno und Software-Rasterizer ab — keiner davon trifft V3D.

```
/usr/lib/aarch64-linux-gnu/gallium-pipe/
  pipe_kmsro.so   pipe_msm.so     pipe_nouveau.so
  pipe_r300.so    pipe_r600.so    pipe_radeonsi.so
  pipe_swrast.so  pipe_vmwgfx.so
  — kein pipe_v3d.so —
```

Folge: OpenCL-Aufrufe fallen auf `pipe_swrast` (llvmpipe, CPU-Software) zurück.
Für GPU-Compute ist das nicht nützlicher als direkte CPU-Berechnung, bei höherem Overhead.

**Rusticl** (der neue Mesa-OpenCL-Stack mit Vulkan-Backend, der V3D über V3DV ansprechen
könnte) ist in diesem Debian-Paket nicht enthalten. Es wäre ein eigener Mesa-Build
mit `-Dgallium-opencl=disabled -Dglx=disabled -Dopencl-spirv=true` nötig.

Für RPi 4 gibt es das Community-Projekt **VC4CL** (OpenCL 1.2 für VideoCore VI),
das 32-Bit-Precision nutzt und nicht mehr aktiv weiterentwickelt wird.

| | RPi 1–3 | RPi 4 | RPi 5 |
|---|---|---|---|
| OpenCL installiert | Nein | VC4CL (community) | mesa-opencl-icd (Clover, installiert) |
| tatsächlich GPU? | — | Ja (FP32, community) | Nein (CPU-Fallback via pipe_swrast) |
| Empfehlung | — | Vulkan bevorzugen | Vulkan — einziger GPU-Pfad |

### 4.3  OpenGL ES Compute Shaders

Ab OpenGL ES 3.1 verfügbar, unterstützt auf RPi 3+ über Mesa. Keine separate
Installation nötig. Praktisch identische Einschränkungen wie Vulkan (FP64 fehlt),
aber ältere und weniger flexible API für Compute-Aufgaben. Nicht empfohlen für
neue Entwicklung — Vulkan ist der modernere Pfad.

### 4.4  CUDA

Nur auf NVIDIA-Hardware (Jetson Nano, Jetson AGX Orin, Desktop-GPUs). Auf
Raspberry Pi nicht verfügbar und nicht portierbar.

### 4.5  Metal

Nur auf Apple-Hardware (M1/M2/M3/M4, A-Series). Auf Raspberry Pi nicht verfügbar.
Für macOS/iOS-Portierungen relevant.

---

## 5  ARM-spezifische Erweiterungen jenseits von NEON

### 5.1  SVE — Scalable Vector Extension

| Architektur | SVE | Auf RPi? |
|---|---|---|
| ARMv8.0–8.1 (Cortex-A53, A72) | Nein | RPi 1–4: Nein |
| ARMv8.2 (Cortex-A76) | Optional | RPi 5: **Nein** (nicht implementiert) |
| ARMv8.2 (AWS Graviton2) | Ja | Nicht RPi |
| ARMv9 (Cortex-X2, A710) | SVE2 | Nicht RPi |
| Apple M1/M2 | AMX (proprietär) | Nicht RPi |

Der Cortex-A76 im RPi 5 implementiert SVE **nicht**, obwohl die Architektur es erlauben
würde. SVE ist auf diesem Chip somit kein gangbarer Pfad.

### 5.2  ASIMD-DP (dot product) — bereits vorhanden

`asimddp` ist im Feature-String des RPi 5 vorhanden. Das ist der
`vdot` / `udot`-Befehl (INT8-Dotprodukt). Für FP64-Filterberechnungen nicht nutzbar —
nur relevant für quantisierte neuronale Netze.

### 5.3  Half-Precision NEON (asimdhp / fphp)

`asimdhp` und `fphp` sind vorhanden: 16-Bit Floating-Point NEON.
Für Audiofilter ungeeignet (zu geringe Präzision, 3–4 Dezimalstellen).
Könnte für Visualisierung in fiview nützlich sein.

### 5.4  NEON mit -march=armv8.2-a+dotprod

Ermöglicht dem Compiler, `vdot`-Instruktionen für Integer-Code zu nutzen.
Nicht relevant für den FP64-Kern von fidlib.

---

## 6  Analyse: Wo würde GPU-Compute helfen?

### 6.1  Streaming vs. Batch — das Grundproblem

firun und der fidlib-Hotpath verarbeiten Audio **sample-by-sample** (ein `double`
pro Aufruf von `filter_step`). Die GPU ist für Batch-Verarbeitung optimiert:
viele Daten parallel, hoher Durchsatz, aber hohe Latenz beim Start.

Transfer-Overhead für einen typischen Audio-Block (1024 Samples × 8 Byte = 8 KB):

- CPU→GPU-Transfer (PCIe / shared memory): ~10–50 µs
- GPU-Kernel-Start: ~5–20 µs  
- Gesamtlatenz: ~15–70 µs

Bei 44100 Hz und 1024 Samples beträgt das verfügbare Zeitfenster **23,2 ms**.
Die Transferlatenz ist damit beherrschbar — aber nur wenn in Blöcken gearbeitet wird.

### 6.2  FIR-Filter (opcode 8) — Kandidat

Der NEON-beschleunigte FIR-Hotpath (`fid_fir_dot`) könnte bei sehr langen Filtern
auf der GPU weiter profitieren:

| FIR-Länge | NEON-Gewinn | GPU-Potential | Fazit |
|---|---|---|---|
| < 64 Taps | 2–3× | Overhead überwiegt | Nein |
| 64–256 Taps | 3–4× | Marginaler Gewinn (~1,2×) | Fraglich |
| 256–1024 Taps | 3,5–4× | Relevant bei Blockbetrieb | Bedingt Ja |
| > 1024 Taps | 3,8× | FFT-Faltung (Overlap-Add) sinnvoller | Ja (FFT) |

**Wichtigster Befund:** Bei sehr langen FIR-Filtern ist **FFT-basierte Faltung
(Overlap-Add / Overlap-Save)** der GPU-Compute-Shader immer überlegen, weil
die FFT O(N log N) statt O(N²) skaliert und auf CPU ebenfalls stark optimierbar ist.

### 6.3  IIR-Filter (opcodes 16, 18, 19, 21) — kein Kandidat

Butterworth, Chebyshev, Bessel: serielle Rückkopplungsstruktur. Das `y[t−1]`
aus dem vorherigen Sample-Schritt muss bekannt sein, bevor `y[t]` berechnet
werden kann. GPU kann diese Abhängigkeit nicht auflösen. Kein Gewinn möglich.

### 6.4  Koeffizientenberechnung (fidmkf.h) — kein Kandidat

Die Koeffizientenberechnung (Butterworth-Pole, bilineare Transformation) läuft
einmalig und dauert auf der CPU < 1 ms. GPU-Overhead würde überwiegen.

### 6.5  FFT-basierte Faltung — echter Kandidat

Für sehr lange FIR-Filter (> 512 Taps, Resampling, Raumakustik-Simulation):

```
Overlap-Add auf GPU:
  1. Eingangsblock FFT  → GPU
  2. Elementweise Multiplikation mit H(f) (Filterfrequenzgang)
  3. IFFT              → GPU
  4. Overlap-Addition  → CPU oder GPU
```

Auf VideoCore VII (RPi 5): Mesa-Vulkan-Compute unterstützt dies in FP32.
Für FP64 wäre Software-Emulation erforderlich — praktisch nicht sinnvoll.

---

## 7  Lohnt es sich — nach Raspberry Pi Modell?

| Modell | CPU-NEON | GPU-Compute | Empfehlung |
|---|---|---|---|
| **RPi 1** | Nein (ARMv6) | Nein | Nur skalares C |
| **RPi 2 v1.1** (A7) | 32-Bit NEON | Nein | NEON 32-bit (float) |
| **RPi 2 v1.2 / RPi 3** | 64-Bit NEON | Nein | NEON (FP64, lohnt sich) |
| **RPi 4** | 64-Bit NEON | Vulkan 1.1 | NEON bevorzugen; GPU nur für FIR > 512 Taps + FP32 |
| **RPi 5** | 64-Bit NEON | Vulkan 1.2 | NEON bevorzugen; GPU für FFT-Faltung + FP32 interessant |
| **RPi Zero 2 W** | 64-Bit NEON | Nein | NEON, kein GPU-Compute |
| **RPi CM4** | wie RPi 4 | wie RPi 4 | wie RPi 4 |

### Zusammenfassung: Wann lohnt GPU auf dem RPi?

**Lohnt sich:**
- RPi 4/5, sehr lange FIR-Filter (> 512 Taps), Blockbetrieb (nicht Echtzeit per Sample)
- FP32-Präzision akzeptabel (Audio-Visualisierung, Echtzeitspektrum in fiview)
- Vulkan Compute ist vorhanden, kein zusätzliches Paket nötig

**Lohnt sich nicht:**
- RPi 1/2/3 (kein Compute-API)
- IIR-Filter (serielle Abhängigkeit, GPU hilft nicht)
- Echtzeit-Sample-Streaming (Latenz > Gewinn)
- FP64-Präzision erforderlich (GPU emuliert das nur)

---

## 8  Andere ARM-Systeme jenseits Raspberry Pi

| System | CPU | GPU | Compute-API | Besonderheit |
|---|---|---|---|---|
| **Jetson Nano** | Cortex-A57 | Maxwell (128 CUDA-Kerne) | CUDA 10, cuDNN | FP64 vorhanden, echte GPU |
| **Jetson Orin Nano** | Cortex-A78AE | Ampere (1024 CUDA-Kerne) | CUDA 11, FP64 | Beste FP64-Performance in ARM-Embedded |
| **Apple M1/M2/M3** | Firestorm+Icestorm (ARMv8.6) | Apple GPU (7–40 Kerne) | Metal Compute | AMX-Matrix-Extension; exzellent für FP64 Compute |
| **AWS Graviton3** | Neoverse V1 (ARMv9, SVE2) | Kein GPU | — | SVE2 für CPU; kein integrierter GPU |
| **BeagleBone Black** | Cortex-A8 | PowerVR (kein Compute) | — | PRU-Koprozessor für Echtzeit-I/O |
| **Qualcomm RB5/RB3** | Cortex-A77 + Hexagon DSP | Adreno 650 | OpenCL 2.0, Hexagon SDK | Hexagon DSP: echter FP32/INT8 DSP-Prozessor |
| **NXP i.MX 8** | Cortex-A53 + Cortex-M4 | Vivante GC7000Lite | OpenCL 1.2 | M4 als DSP-Koprozessor nutzbar |

### Besonders interessant: Qualcomm Hexagon DSP

Qualcomm-Chips (RB5, RB3 Gen2, Snapdragon-basierte Boards) haben einen dedizierten
Hexagon-DSP, der für genau solche Filterverarbeitungs-Aufgaben ausgelegt ist:
- FP32 + INT8 nativ
- Sehr geringe Latenz (kein Bus-Transfer wie bei GPU)
- Hexagon SDK (C-API, keine Shader-Sprache nötig)
- Nachteil: nur Qualcomm-Hardware

---

## 9  Mögliche Implementierungspfade

### Pfad A: Vulkan Compute (FP32, RPi 4/5)

```
fidlib/fid_vulkan.h        — Plattformerkennung + Kontext-Init
fidlib/fid_vulkan.c        — VkDevice, VkQueue, VkBuffer, Compute-Pipeline
fidlib/shaders/fir_dot.comp — GLSL Compute Shader (FP32)
fidlib/CMakeLists.txt      — FIDLIB_VULKAN Option
```

Cmake-Option: `-DFIDLIB_VULKAN=ON`  
Voraussetzung: `libvulkan-dev`, SPIR-V-Compiler (`glslc` aus `glslang-tools`)  
FP32-Präzision; nur sinnvoll für sehr lange FIR-Blöcke

### Pfad B: OpenCL (FP32, Desktop / Jetson / Apple)

```
fidlib/fid_opencl.h
fidlib/fid_opencl.c
fidlib/kernels/fir_dot.cl
```

Cmake-Option: `-DFIDLIB_OPENCL=ON`  
Portabler als Vulkan auf Nicht-RPi-Hardware (AMD, NVIDIA, Jetson, Apple).  
**Auf RPi 5 nicht sinnvoll:** `mesa-opencl-icd` ist Clover ohne `pipe_v3d.so` —
OpenCL landet auf dem CPU-Software-Rasterizer, nicht auf dem VideoCore VII.
Echter GPU-OpenCL auf RPi 5 würde einen eigenen Mesa-Build mit Rusticl erfordern.

### Pfad C: Overlap-Add FFT (FP64-kompatibel, CPU oder GPU)

Langfristig interessantester Pfad für sehr lange FIR-Filter:
- FFT-basierte Faltung auf CPU mit FFTW3 oder KissFFT
- Automatischer GPU-Fallback via cuFFT (Jetson) oder vkFFT (Vulkan)
- FP64 auf CPU-FFT-Pfad vollständig erhalten
- Transparente Schnittstelle: gleiche `fidlib`-API, anderer Execution-Pfad

---

## 10  Gesamtempfehlung

| Priorität | Maßnahme | Aufwand | Ziel |
|---|---|---|---|
| 1 | NEON bleibt der primäre Pfad | — | Alle RPi 2v1.2+, FP64, kein Overhead |
| 2 | Overlap-Add FFT (CPU, FFTW3) | mittel | FIR > 512 Taps, FP64, RPi 3+ |
| 3 | Vulkan Compute (FP32) | hoch | FIR > 512 Taps, FP32 akzeptabel, RPi 4/5 |
| 4 | OpenCL (FP32) | mittel | Desktop + Jetson (nicht RPi — kein GPU-Nutzen) |
| 5 | CUDA | hoch | Nur Jetson; nicht im primären Scope |

**Für die nächste Implementierungsphase empfohlen:** Pfad C (CPU-FFT mit FFTW3).
Das löst das eigentliche Problem langer FIR-Filter ohne FP32-Präzisionsverlust,
läuft auf allen Zielsystemen, und schafft den Unterbau für einen späteren Vulkan-Pfad.

**OpenCL auf RPi explizit ausgeschlossen:** `mesa-opencl-icd` (Clover) hat keinen
V3D-Pipe-Treiber — Aufrufe laufen auf der CPU. Vulkan Compute ist auf RPi 4/5 der
einzige nutzbare GPU-Pfad und wird für GPU-Arbeit bevorzugt.

---

## 11  Einschätzungsänderung durch einen Template-artigen Präzisionsansatz

### 11.1  Ausgangsfrage

Ändert sich die Bewertung wenn alle relevanten Codestellen als typgenerische
C-Funktionen gestaltet werden — analog zu C++ Templates, aber mit C-Mitteln — sodass
der Execution-Pfad bei Bedarf mit `float` statt `double` instanziiert werden kann?

**Kurze Antwort:** Ja, an zwei Stellen ändert sich die Bewertung wesentlich.
An einer Stelle bleibt sie unveränderlich.

---

### 11.2  Was sich ändert: FP32-Ausführungspfad

Das zentrale Hindernis für GPU-Nutzung war bisher, dass fidlib ausschließlich
`double` kennt und VideoCore kein natives FP64 hat. Ein Template-Ansatz löst genau
dieses Problem: **Design-Phase bleibt FP64, Execution-Phase wird präzisions-parametrisch.**

#### Zwei unabhängige Concerns

| Schicht | Datei | double-Stellen | Muss FP64 bleiben? |
|---|---|---|---|
| Koeffizientenberechnung | `fidlib.c`, `fidmkf.h` | ~250 | **Ja.** Polstellenberechnung, bilineare Transformation, komplexe Arithmetik benötigen FP64 |
| Execution-Hotpath | `fidrf_cmdlist.h` | ~44 | **Nein** — für FIR; bedingt für IIR |
| Pufferlayout | `fidrf_cmdlist.h` | ~8 | Nein — folgt dem Ausführungstyp |
| Public API (`FidFunc`) | `fidlib.h` | ~15 | Nein — parallel parametrisierbar |

Die Koeffizientenberechnung designt in FP64 und konvertiert einmalig beim Aufbau
der `FidRun`-Instanz. Pro-Sample-Overhead: null.

#### Mögliche C-Umsetzung: X-Include-Pattern

```c
/* fidrf_execute_impl.h — einmal schreiben, zweimal einbinden */
#ifndef FID_REAL
#  define FID_REAL double
#endif

typedef FID_REAL (FidFuncT)(void *buf, FID_REAL input);

static FID_REAL
filter_step_impl(void *buf, FID_REAL input)
{
    /* identischer Opcode-Dispatch, nur FID_REAL statt double */
    ...
}
```

```c
/* fid_execute_f64.c */
#define FID_REAL double
#include "fidrf_execute_impl.h"

/* fid_execute_f32.c */
#define FID_REAL float
#include "fidrf_execute_impl.h"
```

44 `double`-Stellen in `fidrf_cmdlist.h` müssen auf `FID_REAL` umgestellt werden —
ein überschaubarer Aufwand für die größtmögliche Wirkung.

---

### 11.3  Was sich konkret ändert: NEON + GPU

#### FP32-NEON: bereits heute ein echter Gewinn

Mit einem FP32-Pfad kann `fid_simd.h` ebenfalls FP32-NEON nutzen:

```c
/* FP32-NEON: 4 floats pro Register statt 2 doubles */
float32x4_t acc0 = vdupq_n_f32(0.f);
float32x4_t acc1 = vdupq_n_f32(0.f);
for (int i = 0; i <= n-8; i += 8) {
    acc0 = vfmaq_f32(acc0, vld1q_f32(coef+i),   vld1q_f32(data+i));
    acc1 = vfmaq_f32(acc1, vld1q_f32(coef+i+4), vld1q_f32(data+i+4));
}
```

Statt 2 Doubles pro `vfmaq_f64`-Befehl verarbeitet `vfmaq_f32` 4 Floats.
Auf dem Cortex-A76 (RPi 5): ~2× mehr Durchsatz als der FP64-NEON-Pfad.
Der aktuelle FP64-NEON-Gewinn beträgt bis zu 3,8×. Mit FP32-NEON wären
**~7–8× gegenüber skalarem FP64** erreichbar — ohne GPU, ohne Treiber.

#### GPU Vulkan Compute: jetzt ein valider Pfad

| Szenario | Ohne Templates | Mit Templates + FP32-Pfad |
|---|---|---|
| FIR < 64 Taps | NEON: 2–3× | NEON-FP32: 4–6× |
| FIR 64–512 Taps | NEON: 3–4× | NEON-FP32: 6–8×; GPU marginal |
| FIR > 512 Taps | NEON: 3,8×; GPU kein FP64 | NEON-FP32: 7–8×; GPU FP32 viable |
| IIR (alle Ordnungen) | CPU FP64 | CPU FP64 (bleibt) |

Der GPU-Pfad für FIR > 512 Taps ist mit Templates **kein Workaround mehr**, sondern
ein sauberer paralleler Execution-Pfad. RPi 4 und RPi 5 mit Vulkan Compute werden
damit zu echten GPU-Kandidaten für lange FIR-Filter (Overlap-Add, FP32).

---

### 11.4  Was sich nicht ändert: IIR-Stabilität in FP32

**IIR auf GPU bleibt ausgeschlossen — und FP32-IIR auf CPU ist riskant.**

Für IIR-Filter gilt eine mathematische Einschränkung die kein Template beseitigt:
Pole hoher Ordnung bei tiefen Frequenzen liegen sehr nahe an der Einheitskreis-Grenze.

Beispiel: 8. Ordnung Butterworth, 100 Hz bei 44100 Hz Abtastrate:

```
z-Pol ≈ 0.999985724 + 0.000031416i
```

In FP64 darstellbar. In FP32 (7 Dezimalstellen):

```
z-Pol ≈ 0.9999857 + 0.0000314i
```

Das klingt harmlos — ist es aber nicht. Die Rückkopplungsschleife akkumuliert den
Fehler über viele Samples. Für harte Tiefpassfilter oder hohe Q-Werte kann FP32-IIR
instabil werden oder stärker rauschen als der Filter dämpft.

**Konsequenz für den Template-Ansatz:** Koeffizientendesign in FP64, Execution in FP32
nur für FIR freigeben. Für IIR muss der Pfad weiterhin FP64 bleiben oder gegenüber
dem Nutzer explizit als „Niedrig-Präzisions-Modus" gekennzeichnet werden.

---

### 11.5  Überarbeitete Raspberry-Pi-Empfehlung mit Templates

| Modell | Bisher | Mit FP32-Templates |
|---|---|---|
| **RPi 1** | Nur skalares C | Nur skalares C (kein NEON, kein GPU-API) |
| **RPi 2 v1.1** (A7) | 32-Bit NEON | FP32-NEON (vorher FP64 suboptimal) |
| **RPi 2 v1.2 / RPi 3** | FP64-NEON, kein GPU | FP32-NEON für FIR (~2× Zuwachs) |
| **RPi 4** | FP64-NEON, GPU kein FP64 | FP32-NEON + Vulkan FP32 FIR (**Ja**) |
| **RPi 5** | FP64-NEON 3,8×, GPU blockiert | FP32-NEON ~7–8× + Vulkan FP32 FIR (**Ja**) |
| **RPi Zero 2 W** | FP64-NEON | FP32-NEON für FIR |

**Prägnant:** RPi 3 profitiert durch besseren NEON. RPi 4 und 5 werden GPU-fähig für FIR.

---

### 11.6  Implementierungsaufwand vs. Gewinn

| Maßnahme | Aufwand | Gewinn |
|---|---|---|
| `fidrf_cmdlist.h`: 44 × `double` → `FID_REAL` | Klein | FP32-Execution-Pfad freigeschaltet |
| `fid_simd.h`: FP32-NEON-Variante | Klein | +2× NEON-Durchsatz für FIR |
| Public API: `FidFuncF32` parallel zu `FidFunc` | Mittel | Caller-seitige Typwahl |
| Vulkan-Compute-Shader (FIR FP32) | Groß | GPU für FIR > 512 Taps auf RPi 4/5 |
| IIR-FP32-Pfad | Mittel + Risiko | Fraglich — numerisch instabil bei hohen Ordnungen |

**Empfohlene Reihenfolge:**
1. `fidrf_cmdlist.h` auf `FID_REAL` parametrisieren (Fundament)
2. `fid_simd.h` FP32-NEON ergänzen (sofortiger Gewinn, kein GPU-Aufwand)
3. Vulkan-Pfad nur für FIR, nach Verifikation der FP32-Koeffizientenkonvertierung

---

## Anhang: Paketlage auf diesem System

```
Vulkan:   mesa-vulkan-drivers 24.2.8   — sofort nutzbar (V3D 7.1, GPU)
OpenCL:   mesa-opencl-icd 24.2.8 installiert (ICD: /etc/OpenCL/vendors/mesa.icd)
          Implementierung: Clover, OpenCL 1.1
          Kein pipe_v3d.so → Number of devices: 0
/dev/dri: card0, card1, renderD128 vorhanden
```

clinfo-Ausgabe (Ist-Zustand):
```
Platform Name     Clover
Platform Version  OpenCL 1.1 Mesa 24.2.8-1~bpo12+rpt4
Number of devices 0
  → No devices found in platform
  → clCreateContextFromType(..., CL_DEVICE_TYPE_GPU) No devices found
```

**Fazit:** OpenCL ist formal registriert, aber vollständig unbrauchbar — kein einziges Device,
weder GPU noch CPU-Fallback. Clover findet keine passende Gallium-Pipe für VideoCore VII.
Vulkan Compute ist und bleibt der einzige funktionierende GPU-Compute-Pfad auf diesem System.

**Konsequenz:** Trotz installiertem `mesa-opencl-icd` steht auf diesem RPi 5 kein
GPU-beschleunigtes OpenCL zur Verfügung. Clover hat keinen V3D-Pipe-Treiber.
Rusticl (der neuere Mesa-OpenCL-Stack mit Vulkan-Backend) könnte V3D über V3DV
ansprechen, ist aber in dieser Mesa-Version nicht als ICD registriert.

Für echtes GPU-OpenCL auf dem RPi 5 müsste entweder:
- Rusticl manuell aktiviert werden (Mesa-Build-Option `rusticl`, nicht in Debian-Paket enthalten), oder
- ein künftiger Mesa-Backport mit Rusticl-ICD abgewartet werden.

**Vulkan Compute bleibt der einzige sofort nutzbare GPU-Compute-Pfad auf diesem System.**
