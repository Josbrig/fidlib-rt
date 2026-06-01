# SIMD/NEON-Optimierung fidlib — Konzept und Analyse

## Was ist das?

**SIMD** (Single Instruction, Multiple Data) ist eine CPU-Erweiterung, die mit einem
einzigen Maschinenbefehl mehrere Zahlen gleichzeitig verrechnet. Auf dem Raspberry Pi 5
(ARM Cortex-A76, AArch64) heißt das Befehlsset **NEON** und arbeitet mit 128-Bit-Registern.
Für `double` (64-Bit) passen genau zwei Werte in ein NEON-Register.

Der zentrale Befehl ist `vfmaq_f64` — eine vektorisierte Fused Multiply-Add:

```
acc[0] += coef[0] * data[0]
acc[1] += coef[1] * data[1]   ← beides in EINEM Takt
```

Die Implementierung nutzt zudem **doppelte Akkumulation** (zwei Akkumulatoren `acc0`
und `acc1` wechseln sich ab), sodass bei je 4 Elementen pro Schleifenrunde 4 MACs in
~2 Takten erledigt werden statt in ~4.

---

## Wo nützt das?

**Ausschließlich bei langen FIR-Filtern** (Finite Impulse Response). Ein FIR-Filter der
Länge N berechnet pro Ausgabesample ein Skalarprodukt:

```
y[t] = Σ h[k] · x[t−k]   für k = 0..N−1
```

Das sind N unabhängige Multiplikationen + Additionen — der ideale Fall für SIMD, weil
keine serielle Abhängigkeit besteht.

**Nicht verbessert** werden rekursive IIR-Filter (Butterworth, Chebyshev, Bessel), da
deren Rückkopplungsstruktur eine serielle Abhängigkeit erzwingt:

```
y[t] = b0·x[t] + b1·x[t−1] − a1·y[t−1] − a2·y[t−2]
```

`y[t]` hängt von `y[t−1]` ab → keine Parallelisierung über Taps möglich.

**Typische FIR-Anwendungen** wo die Optimierung wirkt:

- Windowed-Sinc-Tiefpassfilter (Audio-Resampling, Antialiasing)
- Parks-McClellan-Equalizer mit vielen Taps
- Bandpassfilter mit scharfer Flanke (> 50 Taps)
- FIR-Differenzierer und Hilbert-Transformer

---

## Wo im Code ist das eingebaut?

### `fidlib/fid_simd.h` — Primitiv-Ebene

Plattformerkennung zur Compile-Zeit und `fid_fir_dot()`:

```c
// Erkennung
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#  define FID_SIMD_NEON 1       // AArch64 (Pi 4, Pi 5, Apple M1/M2, ...)
#elif defined(__SSE2__)
#  define FID_SIMD_SSE2 1       // x86_64
// sonst: skalarer C-Fallback

// Kernfunktion (NEON-Variante)
static inline double fid_fir_dot(const double *coef, const double *data, int n) {
    float64x2_t acc0 = vdupq_n_f64(0.0);
    float64x2_t acc1 = vdupq_n_f64(0.0);
    for (int i = 0; i <= n-4; i += 4) {
        acc0 = vfmaq_f64(acc0, vld1q_f64(coef+i),   vld1q_f64(data+i));
        acc1 = vfmaq_f64(acc1, vld1q_f64(coef+i+2), vld1q_f64(data+i+2));
    }
    // + Rest-Taps (1–3) skalar
```

### `fidlib/fidrf_cmdlist.h` — Hotpath-Ebene

`filter_step()` verarbeitet einen Befehlsstrom aus Opcodes. **Opcode 8** deckt den Fall
`4N× reine FIR-Taps` ab — genau der lange FIR-Block:

```c
// Skalarer Pfad (FIDLIB_SIMD aus):
case 8:
    cnt = *cmd++;
    do { FIR; FIR; FIR; FIR; } while (--cnt > 0);  // 4 skalare MACs/Runde

// SIMD-Pfad (FIDLIB_SIMD an):
case 8: {
    int n = (int)(unsigned char)*cmd++ * 4;
    buf[-1] = tmp;                         // Sentinel schreiben
    fir += fid_fir_dot(coef, buf-1, n);    // SIMD-Dotprodukt
    coef += n;  buf += n;  tmp = buf[-1];  // State-Maschine weiterschalten
}
```

#### Technisches Detail: Sentinel-Slot

Der Delay-Buffer-Invariant besagt: zum Zeitpunkt des Opcode-8-Eintritts gilt immer
`buf[-1] == tmp`. Für den Fall `j == 0` (Opcode 8 ist der allererste Befehl im Stream,
was bei reinen FIR-Filtern häufig vorkommt) muss `buf[-1]` jedoch beschreibbar sein.
Dafür reserviert `fid_run_newbuf()` einen zusätzlichen `double` vor `buf[0]`:

```
[ RunBuf-Header | sentinel_double | buf[0..siz-1] | coef[] | cmd[] ]
                                    ^--- rb->buf
```

Alle Puffer-Verwaltungsfunktionen wurden konsistent angepasst:

| Funktion | Änderung |
|---|---|
| `fid_run_newbuf()` | `+1 double` allokiert, `rb->buf = alloc+1` |
| `fid_run_bufsize()` | gibt `+sizeof(double)` zurück |
| `fid_run_initbuf()` | identisches Layout mit `memset(base, 0, buf_bytes)` |
| `fid_run_zapbuf()` | nullt zusätzlich `buf[-1]` |

### `fidlib/CMakeLists.txt` — Build-Ebene

```cmake
option(FIDLIB_SIMD "SIMD-Beschleunigung für den FIR-Hotpath" OFF)
if(FIDLIB_SIMD)
    target_compile_definitions(fidlib PUBLIC FIDLIB_SIMD)
    if(aarch64)  → NEON ist immer vorhanden, kein Extra-Flag nötig
    if(x86_64)   → -msse2
```

`PUBLIC` sorgt dafür dass auch Testcode und alle Abnehmer-Targets automatisch
`FIDLIB_SIMD` erhalten.

---

## Geschwindigkeitsverbesserung

### Theoretische Analyse (Cortex-A76, Raspberry Pi 5)

| Metrik | Skalar | NEON (dual-acc) | Faktor |
|---|---|---|---|
| MACs pro Takt (Compute) | 1 | 4 (2× `vfmaq_f64` parallel) | **4×** |
| Praktisch (mit Memory, Loop-Overhead) | — | — | **1,8×–3×** |

Der Cortex-A76 hat zwei Floating-Point-Pipelines, die `vfmaq_f64` mit Durchsatz 1/Takt
ausführen. Die doppelte Akkumulation (`acc0`/`acc1` alternierend) versteckt die 4-Takt-
Latenz und hält beide Pipes beschäftigt → ~4 MACs pro Takt.

### Hochrechnung nach Filter-Länge

| FIR-Taps N | Skalare Takte | NEON-Takte | Speed-up |
|---|---|---|---|
| 16 | ~16 | ~8 | **1,8×** |
| 64 | ~64 | ~22 | **2,9×** |
| 256 | ~256 | ~70 | **3,7×** |
| 1024 | ~1024 | ~268 | **3,8×** |

Werte sind Schätzwerte für cache-residenten Zugriff. Memory-bound-Filter (Delay-Line
passt nicht in L1/L2) konvergieren gegen ~2×, da dann der Speicherdurchsatz limitiert.

### Was NICHT schneller wird

| Filtertyp | Opcode | SIMD-Effekt |
|---|---|---|
| IIR-Biquad (Butterworth / Chebyshev / Bessel) | 18, 21 | **0 %** |
| IIR-only (Pol-only) | 16, 19 | **0 %** |
| Kurze FIR < 8 Taps | 5, 6, 7 | **0 %** (ENDFIR-Pfad) |
| Lange FIR ≥ 12 Taps | **8** | **1,8×–3,8×** |

Der bei weitem häufigste Filtertyp in diesem Projekt (Butterworth LP/HP via `LpBu`/`HpBu`)
verwendet IIR und profitiert **gar nicht**. Die Optimierung zahlt sich erst aus wenn gezielt
FIR-Filter mit `fid_cv_array` oder einem Parks-McClellan-Designer eingesetzt werden.

---

## Aktivierung

Alle drei Performance-Optionen sind **seit 2026-05-28 standardmäßig ON**:

| cmake-Option | Default | Wirkung |
|---|---|---|
| `FIDLIB_SIMD` | **ON** | NEON / SSE2 FIR-Dotprodukt |
| `FIDLIB_FAST_MATH` | **ON** | `-O3 -ffast-math` scoped auf fidlib |
| `FIDLIB_LTO` | **ON** | Link-Time Optimization auf filter_step |

Ein normaler Build aktiviert also automatisch alle Optimierungen:

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -S . -B build_release
cmake --build build_release -j$(nproc)
```

Zum Deaktivieren einzelner Stufen:

```bash
cmake ... -DFIDLIB_SIMD=OFF -DFIDLIB_FAST_MATH=OFF -DFIDLIB_LTO=OFF
```

---

## Verwandte Konzepte

- `doc/concepts/fidlib-cpp20-rt-optimierung.md` — RT-Sicherheit, Cache-Layout
- `doc/concepts/fidlib-rt-todo.md` — ursprüngliche Optimierungs-Roadmap
