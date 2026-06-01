<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2025-2026 Jörg Simbrig -->

# Lösung: Lange FIR-Filter effizient mit Overlap-Save FFT

## Das Problem

Ein FIR-Filter mit 2048 Taps soll auf einen Audio-Stream mit 44100 Hz angewendet
werden. Die direkte Faltung benötigt 2048 Multiplikationen pro Sample — bei 44100
Samples/s sind das ~90 Millionen Multiplikationen/Sekunde. Auf einem eingebetteten
System (RPi 5, Cortex-A76) ist das schon spürbar; bei noch längeren FIR-Filtern
(z.B. Raumimpulsantworten mit 16384 Taps) ist Direktfaltung schlicht zu langsam.

**Anforderungen:**
- FIR-Filter mit ≥ 512 Taps auf eingebettetem System in Echtzeit
- Keine manuelle FFT-Programmierung — automatischer Dispatch
- Korrektheitsprüfung: OLA-Ausgabe muss mit Direktfaltung übereinstimmen

---

## Welche Mittel des Projekts helfen

- **`FIDLIB_FFT=ON`** — aktiviert die Overlap-Save-Engine in `fid_fft.h`
- **`FIDLIB_FFT_THRESHOLD`** — ab dieser Tap-Anzahl wird automatisch OLA gewählt
  (Standard: 512)
- **`FIDLIB_FFT_FFTW3`** — wenn `libfftw3-dev` installiert ist, wird FFTW3 statt
  dem eingebauten Radix-2-Algorithmus verwendet (2–3× schneller)
- **`FidFunc *step_fn`** — identische API wie bei Direktfaltung; kein Code-Umbau

---

## Overlap-Save Prinzip (Kurzfassung)

Direkte FIR-Faltung: für jeden Ausgabe-Sample werden M Multiplikationen benötigt.
Kosten: O(M) pro Sample.

Overlap-Save (OLA): der Eingang wird in Blöcke der Größe B geteilt,
jeder Block wird per FFT-Faltung verarbeitet:
- FFT-Größe: N = nächste Potenz von 2 mit N ≥ 2M
- Blockgröße: B = N − M + 1
- Kosten pro Block: O(N log N) — amortisiert O((N log N) / B) pro Sample

Bei M=1024: N=2048, B=1025 → amortisiert ~22 Operationen/Sample statt 1024.

**Latenz:** jeder Block wird erst ausgegeben wenn B Eingabe-Samples vorliegen.
Latenz = B − 1 Samples.

---

## Schritt 1: cmake-Build mit FFT-Backend

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_SIMD=ON \
      -DFIDLIB_FFT=ON \
      -S . -B build_fft

cmake --build build_fft -j$(nproc)
```

cmake-Ausgabe prüfen:
```
-- fidlib FFT: Overlap-Save + FFTW3 3.3.x    ← FFTW3 gefunden (schneller)
-- oder --
-- fidlib FFT: Overlap-Save + built-in Radix-2  ← Fallback
```

Falls FFTW3 nicht gefunden: `sudo aptitude install libfftw3-dev` und cmake neu.

## Schritt 2: Threshold konfigurieren

Der Threshold bestimmt ab wann OLA statt Direktfaltung verwendet wird.
Standard ist 512 Taps. Für Echtzeit-Anwendungen mit knappem Speicher kann
ein niedrigerer Wert sinnvoll sein:

```bash
# OLA schon ab 128 Taps:
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_FFT=ON \
      -DFIDLIB_FFT_THRESHOLD=128 \
      -S . -B build_lowthresh

cmake --build build_lowthresh -j$(nproc)
```

## Schritt 3: Langer FIR-Filter erzeugen

fidlib erlaubt FIR-Filter via `fid_cv_array` (Koeffizientenarray) oder
über externe Koeffizienten-Dateien. Für typische Audio-Anwendungen:

```c
// Beispiel: Boxcar FIR mit 1024 Taps (Rechteck-Impulsantwort)
#include <fidlib/fidlib.h>
#include <stdlib.h>
#include <string.h>

static void *make_boxcar_fir(int M, double **fn_out_step, void **run_out) {
    // Koeffizienten: alle gleich 1/M (normiertes Mittelungsfilter)
    double *coef = malloc(M * sizeof(double));
    for (int i = 0; i < M; i++) coef[i] = 1.0 / M;

    // FidFilter aus Koeffizientenarray bauen
    FidFilter *ff = fid_cv_array(coef, M);
    free(coef);

    FidFunc *fn;
    void    *run = fid_run_new(ff, &fn);
    free(ff);

    *fn_out_step = fn;
    *run_out     = run;
    return fid_run_newbuf(run);
}
```

## Schritt 4: Identische API wie Direktfaltung

Das OLA-Backend ist vollständig transparent — `step_fn` und `buf` sind dieselben
Typen wie bei Direktfaltung:

```c
FidFunc *step_fn;
void    *run;
void    *buf = make_boxcar_fir(1024, &step_fn, &run);
// ^ Bei 1024 > 512 (Threshold): automatisch OLA-Engine aktiv

// Run-Phase: identisch zur Direktfaltung:
double out = step_fn(buf, input_sample);

// Free-Phase: identisch:
fid_run_freebuf(buf);
fid_run_free(run);
```

Kein einziger Code-Umbau wenn man von Direktfaltung auf OLA wechselt —
nur cmake mit `-DFIDLIB_FFT=ON` neu aufrufen.

## Schritt 5: Latenz berücksichtigen

Bei OLA sind die ersten B−1 Ausgabe-Samples 0 (der erste Block wird erst nach
B Eingabe-Samples fertig). Bei M=1024, N=2048, B=1025 beträgt die Latenz
**1024 Samples** ≈ 23 ms bei 44100 Hz.

Für Echtzeit-Anwendungen mit Latenz-Anforderungen:
- Threshold erhöhen um kleine FIR-Filter direkt zu rechnen (keine Latenz)
- Threshold senken um früher in OLA zu wechseln (kleine Blöcke, weniger Latenz)

Berechnung der OLA-Latenz:
```
N = nächste Potenz von 2 mit N ≥ 2*M
B = N - M + 1
Latenz = B - 1 Samples = N - M Samples
```

Für M=512:  N=1024, Latenz=512 Samples = 11.6 ms @ 44100 Hz
Für M=1024: N=2048, Latenz=1024 Samples = 23 ms
Für M=4096: N=8192, Latenz=4096 Samples = 93 ms

## Schritt 6: Benchmark — OLA vs. Direktfaltung

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_FFT=ON -DBUILD_BENCHMARKS=ON \
      -S . -B build_bench

cmake --build build_bench --target bench_fir_backends -j$(nproc)
./build_bench/bin/bench_fir_backends
```

Erwartete CSV-Ausgabe (Beispiel RPi 5):
```
backend,taps,samples_per_sec
scalar,64,15000000
neon,64,43000000
ola_fftw3,1024,280000000
ola_fftw3,4096,210000000
```

OLA/FFTW3 ist bei langen FIR-Filtern typisch 5–20× schneller als NEON-Direktfaltung.

---

## firun mit langem FIR-Filter

firun wählt automatisch OLA wenn das Projekt mit `FIDLIB_FFT=ON` gebaut wurde
und der Filter genug Taps hat:

```bash
# Boxcar FIR via firun — 'x' ist der FIR-Identity-Opcode in fidlib:
# (Eigene FIR-Koeffizienten: via fid_cv_array in C oder externe Datei)

# Test: Impulsantwort eines langen FIR (1024 Taps, falls implementiert):
build_fft/bin/firun -d 2200 44100 %I "x1024" 2>/dev/null || echo "FIR-Spec je nach Filtertyp"
```

---

## Verifikation: OLA-Korrektheit prüfen

```bash
# Test-Binary aus dem Testsuite:
cd build_fft && ctest -R fidlib_fft --output-on-failure
```

Der `test_fidlib_fft` prüft:
- **Impulstest**: 600-Tap Boxcar → nach 600 Samples exakt Ausgabe 1/600 × 600 = 1.0
- **DC-Test**: konstante Eingabe → Ausgabe konvergiert zu Eingangsignal
- **Energie-Erhaltung**: Parseval-Theorem
- **Nyquist-Test**: π-Frequenz → Ausgabe 0 bei gerader Tap-Anzahl

---

## Problemlösungen

| Problem | Ursache | Lösung |
|---------|---------|--------|
| OLA nicht aktiv (cmake meldet Radix-2) | libfftw3-dev fehlt | `sudo aptitude install libfftw3-dev` |
| Filter nicht in OLA-Engine (Direktfaltung) | Tap-Anzahl < Threshold | `FIDLIB_FFT_THRESHOLD` senken |
| Ausgabe hat 0-Präfix | OLA-Latenz | Normal — erste B-1 Samples sind 0 |
| Hoher Speicherverbrauch | Großes N (viele Taps) | Threshold erhöhen, Tap-Anzahl reduzieren |
