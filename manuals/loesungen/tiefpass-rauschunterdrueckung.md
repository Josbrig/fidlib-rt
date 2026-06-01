<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2025-2026 Jörg Simbrig -->

# Lösung: Tiefpass-Rauschunterdrückung in Audiosignalen

## Das Problem

Ein Audiosignal (Sample-Rate 44100 Hz) enthält hochfrequentes Rauschen oberhalb
von 4000 Hz — typisch bei Schaltgeräuschen, HF-Interferenz oder billigen A/D-Wandlern.
Das Rauschen soll entfernt werden, ohne das Nutzsignal (< 3500 Hz) zu beeinflussen.

**Anforderungen:**
- Cutoff: 4000 Hz
- Ausreichende Sperrdämpfung im Rauschband (> 3 dB bei 4 kHz, > 20 dB bei 8 kHz)
- Kausaler Filter (Echtzeit-fähig, kein Look-ahead)
- Minimalaufwand: kein Recompile wenn sich Cutoff ändert

---

## Welche Mittel des Projekts helfen

- **fidlib** — entwirft und führt den Filter per Laufzeit-Spezifikation aus
- **firun** — wendet den Filter auf der Kommandozeile auf Rohdaten an
- **`LpBu4/4000`** — Butterworth-Tiefpass 4. Ordnung, Cutoff 4000 Hz
- **`FIDLIB_SIMD=ON`** — NEON/SSE2-Beschleunigung für Echtzeit-Betrieb

---

## Schritt-für-Schritt — Variante A: Kommandozeile mit firun

### Schritt 1: Projekt bauen (falls noch nicht geschehen)

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_SIMD=ON \
      -S . -B build

cmake --build build -j$(nproc)
```

Das erzeugt `build/bin/firun`.

### Schritt 2: Filter-Spezifikation verstehen

```
LpBu4/4000
│  │ │  └── Cutoff-Frequenz: 4000 Hz
│  │ └──── Ordnung: 4 (zwei verkettete Biquads = 4 Pole)
│  └──── Bu = Butterworth (maximal flacher Durchlassbereich)
└──── Lp = Lowpass (Tiefpass)
```

Butterworth 4. Ordnung bei 44100 Hz: -3 dB bei 4000 Hz, -80 dB bei ~16 kHz.

### Schritt 3: Impulse-Antwort testen (ohne echte Audiodaten)

```bash
build/bin/firun -d 100 44100 %I LpBu4/4000
```

- `%I` — synthetischer Impuls als Eingang (1, 0, 0, 0, ...)
- `-d 100` — 100 Samples ausgeben
- Ausgabe ist ASCII, ein Wert pro Zeile

Erwartung: erster Wert ≈ 0.0003 (Filter-Gain), dann klingende Impulsantwort,
abklingend auf 0.

### Schritt 4: Rohe 16-Bit-Audio-Daten filtern

```bash
# Signed 16-Bit PCM (Little-Endian), 1 Kanal, 44100 Hz:
cat eingabe.raw | build/bin/firun 44100 s LpBu4/4000 > ausgabe.raw
```

### Schritt 5: Mit sox als Pre-/Post-Prozessor

```bash
# WAV → Raw PCM → Filter → Raw PCM → WAV:
sox eingabe.wav -t raw -e signed -b 16 -r 44100 - | \
    build/bin/firun 44100 s LpBu4/4000 | \
    sox -t raw -e signed -b 16 -r 44100 -c 1 - ausgabe.wav
```

### Schritt 6: Stereo-Signal (2 Kanäle, interleaved)

Bei Stereo enthält jeder Frame L+R abwechselnd. firun filtert jeden Kanal
mit einer eigenen Filter-Instanz:

```bash
# Zwei identische Filter-Specs → L und R werden je mit LpBu4/4000 gefiltert:
cat stereo.raw | build/bin/firun 44100 s2 LpBu4/4000 LpBu4/4000 > gefiltert.raw
```

Das Format `s2` bedeutet: 2 Signed-16-Bit-Werte pro Frame (L, R).

---

## Schritt-für-Schritt — Variante B: C-API im eigenen Programm

### Schritt 1: Alloc-Phase (einmalig, vor der RT-Schleife)

```c
#include <fidlib/fidlib.h>
#include <stdlib.h>

// Filter entwerfen: Butterworth LP 4. Ordnung, Cutoff 4000 Hz, Rate 44100 Hz
FidFilter *filt = fid_design("LpBu4/4000", 44100.0, -1.0, -1.0, 0, NULL);

// Run-Objekt erzeugen (wählt bestes Backend: NEON/FFT/Vulkan)
FidFunc *step_fn;
void    *run = fid_run_new(filt, &step_fn);

// Filter-Zustandspuffer (einen pro Kanal/Instanz)
void *buf = fid_run_newbuf(run);

free(filt);   // FidFilter nicht mehr nötig — Koeffizienten sind in run
```

### Schritt 2: Run-Phase (RT-sicher, kein malloc)

```c
// Für jeden eingehenden Sample:
double output = step_fn(buf, input_sample);
```

`step_fn` ist ein direkter Funktionszeiger — kein virtueller Dispatch,
kein Heap. Sicher in Audio-Callbacks (JACK, ALSA, PortAudio).

### Schritt 3: Free-Phase (beim Beenden)

```c
fid_run_freebuf(buf);
fid_run_free(run);
```

### Vollständiges Mini-Beispiel

```c
#include <fidlib/fidlib.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    FidFilter *filt = fid_design("LpBu4/4000", 44100.0, -1.0, -1.0, 0, NULL);
    FidFunc   *fn;
    void      *run = fid_run_new(filt, &fn);
    void      *buf = fid_run_newbuf(run);
    free(filt);

    // Impuls verarbeiten und erste 20 Samples ausgeben
    printf("%.8f\n", fn(buf, 1.0));
    for (int i = 0; i < 19; i++)
        printf("%.8f\n", fn(buf, 0.0));

    fid_run_freebuf(buf);
    fid_run_free(run);
    return 0;
}
```

Kompilieren:
```bash
gcc -o lp_test lp_test.c \
    -I build/fidlib -L build/fidlib -lfidlib -lm \
    -O2
```

---

## Filter-Varianten für ähnliche Anforderungen

| Anforderung | Fispec | Kommentar |
|---|---|---|
| Weicherer Übergang | `LpBu6/4000` | Ordnung 6, steilere Flanke |
| Kein Phasenfehler wichtig | `LpBe4/4000` | Bessel: linearer Phasengang |
| Maximale Sperrdämpfung | `LpCh4/-1/4000` | Chebyshev: 1 dB Ripple, steile Flanke |
| Höhere Cutoff | `LpBu4/8000` | Cutoff 8 kHz bei 44100 Hz |
| Niedrige Sample-Rate (z.B. 8000 Hz) | `LpBu4/800` | Cutoff = 10% der Nyquist-Frequenz |

---

## Verifikation

```bash
# Stufenantwortt ausgeben und letzten Wert prüfen (→ muss → 1.0 gehen):
build/bin/firun -d 200 44100 %S LpBu4/4000 | tail -5

# Impulsantwort plotten (mit gnuplot):
build/bin/firun -d 300 44100 %I LpBu4/4000 > impulse.dat
gnuplot -e "plot 'impulse.dat' with lines title 'LpBu4 @ 44100 Hz'; pause -1"
```
