<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2025-2026 Jörg Simbrig -->

# Lösung: Echtzeit-Filterung im eigenen C-Programm

## Das Problem

Eine Anwendung empfängt Audio-Samples sample-by-sample aus einem Callback
(z.B. JACK, ALSA, PortAudio) und muss jeden Sample sofort filtern — ohne
Puffer-Latenz, ohne malloc im RT-Thread, ohne Recompile bei Parameteränderung.

**Anforderungen:**
- RT-sicher: kein malloc/free, kein Lock, kein Systemaufruf im Hot-Path
- Filter-Typ und Frequenz aus Laufzeit-Konfiguration (z.B. Kommandozeilenargument)
- Unterstützung für IIR und FIR in derselben Codebasis
- Mehrere unabhängige Instanzen desselben Filters (z.B. L + R Kanal)

---

## Welche Mittel des Projekts helfen

- **fidlib drei-Phasen-Modell**: Alloc → Run → Free
  - `fid_design()` — Filter-Design (nicht RT-safe, nur einmalig)
  - `fid_run_new()` — Run-Objekt mit Koeffizienten (nicht RT-safe, nur einmalig)
  - `fid_run_newbuf()` — Zustandspuffer (nicht RT-safe, nur einmalig)
  - `FidFunc *step_fn(buf, sample)` — Ein-Sample-Verarbeitung (**RT-safe**)
- **`FIDLIB_SIMD=ON`** — NEON/SSE2-Vektorisierung, automatisch aktiv

---

## Das drei-Phasen-Modell

```
┌─────────────────────────────────────────────────────────────────┐
│  ALLOC-PHASE (vor dem RT-Thread)                                │
│  fid_design()   → FidFilter* (Pole/Nullstellen, Koeffizienten) │
│  fid_run_new()  → Run* (optimierter Ausführungsplan)           │
│  fid_run_newbuf() → Buf* (Verzögerungsleitungen, Zustand)      │
│  free(filt)     → FidFilter freigeben (nicht mehr benötigt)    │
├─────────────────────────────────────────────────────────────────┤
│  RUN-PHASE (im RT-Thread, Callback)                             │
│  step_fn(buf, sample) → gefilterter Sample                     │
│  ← zero-alloc, branch-arm, ~ns-Latenz                         │
├─────────────────────────────────────────────────────────────────┤
│  FREE-PHASE (beim Beenden)                                      │
│  fid_run_freebuf(buf)                                          │
│  fid_run_free(run)                                             │
└─────────────────────────────────────────────────────────────────┘
```

---

## Schritt-für-Schritt

### Schritt 1: Projekt bauen

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_SIMD=ON -DFIDLIB_FFT=ON \
      -S . -B build

cmake --build build -j$(nproc)
```

### Schritt 2: Minimales Programm — ein Filter, ein Kanal

```c
// filter_example.c
#include <fidlib/fidlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Aufruf: %s <fispec>  z.B.  LpBu4/1000\n", argv[0]);
        return 1;
    }

    // ── Alloc-Phase ──────────────────────────────────────────────────
    FidFilter *filt = fid_design(argv[1], 44100.0, -1.0, -1.0, 0, NULL);
    if (!filt) {
        fprintf(stderr, "Ungueltige Filter-Spec: %s\n", argv[1]);
        return 1;
    }

    FidFunc *step_fn;
    void    *run = fid_run_new(filt, &step_fn);
    void    *buf = fid_run_newbuf(run);
    free(filt);

    // ── Run-Phase (RT-sicher ab hier) ─────────────────────────────────
    double sample;
    while (fread(&sample, sizeof(double), 1, stdin) == 1) {
        double out = step_fn(buf, sample);
        fwrite(&out, sizeof(double), 1, stdout);
    }

    // ── Free-Phase ───────────────────────────────────────────────────
    fid_run_freebuf(buf);
    fid_run_free(run);
    return 0;
}
```

Kompilieren und einbinden:
```bash
gcc -o filter_example filter_example.c \
    -I/pfad/zum/projekt/fidlib \
    -L/pfad/zum/projekt/build/fidlib \
    -lfidlib -lm -O2

# Oder beim Build des Projekts via cmake target:
# target_link_libraries(mein_programm PRIVATE fidlib)
```

### Schritt 3: Stereo — ein Run-Objekt, zwei Puffer

Der wichtigste Aspekt: `run` (Koeffizienten) wird geteilt, `buf` ist pro Instanz.

```c
// Ein Filter-Design für L und R:
FidFilter *filt  = fid_design("LpBu4/4000", 44100.0, -1.0, -1.0, 0, NULL);
FidFunc   *fn;
void      *run   = fid_run_new(filt, &fn);
void      *buf_l = fid_run_newbuf(run);   // Zustand für L-Kanal
void      *buf_r = fid_run_newbuf(run);   // Zustand für R-Kanal (getrennt!)
free(filt);

// Im Callback:
double out_l = fn(buf_l, in_l);
double out_r = fn(buf_r, in_r);
```

### Schritt 4: Filter zur Laufzeit ändern (Hot-Swap)

Das Austauschen eines aktiven Filters ohne RT-Unterbrechung erfordert einen
atomaren Zeiger-Swap. Dieses Muster ist ohne Mutex möglich wenn der neue Filter
vor dem Swap vollständig allokiert ist:

```c
// (Vereinfacht — produktiver Code braucht Memory-Barrier / _Atomic)
struct ActiveFilter {
    FidFunc *fn;
    void    *run;
    void    *buf;
};

struct ActiveFilter *active = create_filter("LpBu4/4000", 44100.0);

// Im Nicht-RT-Thread: neuen Filter allokieren:
struct ActiveFilter *next = create_filter("LpBu4/2000", 44100.0);

// Atomarer Swap (hier vereinfacht mit volatile — produktiv: C11 _Atomic):
struct ActiveFilter *old = active;
active = next;            // Pointer-Swap

// Im RT-Thread ist ab jetzt next aktiv.
// Old erst freigeben wenn sicher kein RT-Thread mehr darauf zugreift.
destroy_filter(old);
```

### Schritt 5: Filter-Reset (Zustand auf Null setzen)

Wenn ein Filter-Zustand zurückgesetzt werden soll (z.B. nach Stille):

```c
fid_run_zapbuf(buf);   // Alle Verzögerungsleitungen auf 0
```

---

## Mehrere Filter in Serie (Kaskade)

firun unterstützt Filterkaskaden direkt als mehrere Spec-Argumente.
In der C-API kaskadiert man manuell durch Weitergabe der Ausgabe:

```c
// HP + LP = Bandpass (manuell kaskadiert):
FidFilter *hp_filt = fid_design("HpBu2/100", 44100.0, -1.0, -1.0, 0, NULL);
FidFilter *lp_filt = fid_design("LpBu2/3000", 44100.0, -1.0, -1.0, 0, NULL);

FidFunc *hp_fn, *lp_fn;
void *hp_run = fid_run_new(hp_filt, &hp_fn);
void *lp_run = fid_run_new(lp_filt, &lp_fn);

void *hp_buf = fid_run_newbuf(hp_run);
void *lp_buf = fid_run_newbuf(lp_run);

free(hp_filt); free(lp_filt);

// Im Callback:
double after_hp  = hp_fn(hp_buf, input);
double after_lp  = lp_fn(lp_buf, after_hp);   // Kaskade
```

---

## Typische Latenzen und Durchsatz (RPi 5, AArch64, NEON)

| Filter | Ordnung | Typ | Latenz/Sample |
|--------|---------|-----|---------------|
| `LpBu2/1000` | 2 | IIR | ~10 ns |
| `LpBu4/1000` | 4 | IIR | ~20 ns |
| `LpBu8/1000` | 8 | IIR | ~40 ns |
| Boxcar FIR 64 Taps | — | FIR | ~15 ns (NEON) |
| Boxcar FIR 512 Taps | — | FIR | ~60 ns (NEON) |
| Boxcar FIR 1024 Taps (OLA) | — | FIR+FFT | ~8 ns/Sample (amortisiert) |

IIR: O(order) pro Sample — skaliert linear mit Filterordnung.
FIR > Threshold: O(1) amortisiert dank Overlap-Save.

---

## Verifikation: Unit-Test-Pattern

```c
// Testen ob DC-Gain korrekt (Tiefpass → Gain ≈ 1.0 bei f=0):
void *buf = fid_run_newbuf(run);
fid_run_zapbuf(buf);
for (int i = 0; i < 10000; i++)
    fn(buf, 1.0);   // Einlaufen lassen
double dc_gain = fn(buf, 1.0);
assert(fabs(dc_gain - 1.0) < 0.001);   // Toleranz 0.1%
fid_run_freebuf(buf);
```
