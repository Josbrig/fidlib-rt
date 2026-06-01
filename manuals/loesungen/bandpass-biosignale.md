<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2025-2026 Jörg Simbrig -->

# Lösung: Bandpass-Filterung für Bio- und Messsignale

## Das Problem

Ein EEG-Gerät (oder ähnlicher Biosignal-Sensor) liefert Daten mit 250 Hz Sample-Rate.
Das interessante Frequenzband liegt zwischen 8 Hz und 30 Hz (Alpha + Beta-Wellen).
Netzbrumm bei 50 Hz und Gleichanteile (DC-Drift) sollen unterdrückt werden.

**Anforderungen:**
- Durchlassband: 8–30 Hz
- Sperrbandunterdrückung unterhalb 5 Hz und oberhalb 45 Hz
- Linearer Phasengang im Durchlassband erwünscht (Bessel) oder steilste Flanke (Butterworth)
- Echtzeit-fähig bei 250 Hz Sample-Rate (unkritisch für die CPU)
- Mehrkanal-fähig: 8 EEG-Kanäle parallel

---

## Welche Mittel des Projekts helfen

- **`BpBu4/8-30`** — Butterworth-Bandpass 4. Ordnung, Passband 8–30 Hz
- **`BpBe4/8-30`** — Bessel-Bandpass (linearer Phasengang, besser für Biosignale)
- **fidlib C-API** — eine `buf`-Instanz pro Kanal
- **firun** — Mehrkanalverarbeitung mit mehreren Filter-Specs

---

## Schritt-für-Schritt — Variante A: Kommandozeile mit firun

### Schritt 1: Filter-Spezifikation verstehen

```
BpBu4/8-30
│  │ │  └── Obere Eckfrequenz: 30 Hz
│  │ └──── Untere Eckfrequenz: 8 Hz
│  └──── Bu = Butterworth
└──── Bp = Bandpass
```

Ein Butterworth-Bandpass 4. Ordnung mit zweiseitigen Eckfrequenzen hat intern
8 Pole (2 pro Ordnung × 2 Seiten). Die Transferfunktion ist das Produkt zweier
Butterworth-Tiefpässe nach bilinearer Transformation.

### Schritt 2: Einzelkanal-Test mit synthetischer Eingabe

```bash
# Stufenantwortt: muss im Passband auf 0 abklingen (kein DC-Anteil):
build/bin/firun -d 500 250 %S BpBu4/8-30 | tail -10

# Sinusantwort bei 15 Hz (im Passband — muss nahezu unverstärkt sein):
# (Nicht direkt in firun testbar; via C-Programm oder Python)
```

### Schritt 3: Rohdaten eines EEG-Kanals filtern

ASCII-Format (ein Wert pro Zeile, Volt-Werte als Float):

```bash
cat kanal1.txt | build/bin/firun 250 a BpBu4/8-30 > kanal1_gefiltert.txt
```

### Schritt 4: 8 Kanäle parallel (interleaved Format)

Annahme: die Rohdaten sind als 8× Signed-16-Bit pro Frame gespeichert (interleaved):

```bash
cat eeg_8kanal.raw | \
    build/bin/firun 250 s8 \
        BpBu4/8-30 BpBu4/8-30 BpBu4/8-30 BpBu4/8-30 \
        BpBu4/8-30 BpBu4/8-30 BpBu4/8-30 BpBu4/8-30 \
    > eeg_8kanal_gefiltert.raw
```

Das Format `s8` bedeutet: 8 Signed-16-Bit-Werte pro Frame.
Jede Filter-Spec filtert genau einen Kanal.

### Schritt 5: Filter-Kaskade (Hochpass + Tiefpass = Bandpass)

Alternativ kann man Bandpass als Kaskade aus HP + LP realisieren.
Das ist bei extremen Frequenzverhältnissen numerisch stabiler:

```bash
# Äquivalent zu BpBu2/8-30, aber explizit als Kaskade:
cat signal.txt | \
    build/bin/firun 250 a HpBu2/8 LpBu2/30 > gefiltert.txt
```

---

## Schritt-für-Schritt — Variante B: C-API für 8 Kanäle

### Alloc-Phase (einmalig beim Start)

```c
#include <fidlib/fidlib.h>
#include <stdlib.h>

#define N_CHANNELS 8

FidFilter *filt = fid_design("BpBu4/8-30", 250.0, -1.0, -1.0, 0, NULL);
FidFunc   *step_fn;
void      *run = fid_run_new(filt, &step_fn);

// Ein Zustandspuffer pro Kanal — Zustand ist vollständig getrennt
void *buf[N_CHANNELS];
for (int ch = 0; ch < N_CHANNELS; ch++)
    buf[ch] = fid_run_newbuf(run);

free(filt);
```

### Run-Phase (für jeden Frame mit 8 Samples)

```c
void process_frame(double frame_in[8], double frame_out[8]) {
    for (int ch = 0; ch < N_CHANNELS; ch++)
        frame_out[ch] = step_fn(buf[ch], frame_in[ch]);
}
```

Jeder Kanal hat seinen eigenen Zustandspuffer — `run` (Koeffizienten) wird
gemeinsam genutzt, `buf[ch]` enthält den Filter-Zustand (Verzögerungsleitungen)
pro Kanal. Das ist speichereffizient und Thread-sicher wenn jeder Thread
eigene `buf`-Instanzen bekommt.

### Free-Phase

```c
for (int ch = 0; ch < N_CHANNELS; ch++)
    fid_run_freebuf(buf[ch]);
fid_run_free(run);
```

---

## Filter-Varianten für Biosignale

| Anforderung | Fispec | Ordnung | Kommentar |
|---|---|---|---|
| Breites Alpha+Beta | `BpBu4/8-30` | 4 | Standard, schnelle Flanken |
| Linearer Phasengang | `BpBe4/8-30` | 4 | Bessel — keine Phasenverzerrung |
| Nur Alpha (8–13 Hz) | `BpBu4/8-13` | 4 | Engeres Band |
| Nur Beta (13–30 Hz) | `BpBu4/13-30` | 4 | |
| Delta-Band (0.5–4 Hz) | `BpBu4/0.5-4` | 4 | Achtung: nah an Nyquist-Untergrenze |
| Netzbrumm unterdrücken | `BsBu2/49-51` | 2 | Band-Stop / Notch 50 Hz |
| DC-Entfernung | `HpBu2/0.5` | 2 | Hochpass entfernt DC-Drift |

### Typische Kaskade für EEG-Vorverarbeitung

```bash
# DC-Entfernung → Bandpass → Notch:
cat kanal.txt | \
    build/bin/firun 250 a \
        HpBu2/0.5 \
        BpBu4/0.5-45 \
        BsBu2/49-51 \
    > kanal_vorverarbeitet.txt
```

In der C-API: drei separate `fid_run_new`-Objekte, drei `buf`-Instanzen,
Ausgabe des ersten ist Eingabe des zweiten.

---

## Frequenzantwort analysieren (ohne externes Tool)

```bash
# Impulsantwort ausgeben und mit Python/gnuplot FFT machen:
build/bin/firun -d 2048 250 %I BpBu4/8-30 > impulse_bp.dat

# Mit Python (numpy/scipy):
python3 - << 'EOF'
import numpy as np
import matplotlib.pyplot as plt
x = np.loadtxt('impulse_bp.dat')
f = np.fft.rfftfreq(len(x), 1/250)
H = np.abs(np.fft.rfft(x))
plt.plot(f, 20*np.log10(H + 1e-12))
plt.xlabel('Frequenz [Hz]')
plt.ylabel('Magnitude [dB]')
plt.title('BpBu4 8-30 Hz @ 250 Hz')
plt.grid(True)
plt.show()
EOF
```

---

## Verifikation

```bash
# DC-Unterdrückung: Stufenantwortt muss langfristig auf 0 gehen:
build/bin/firun -d 1000 250 %S BpBu4/8-30 | tail -5
# Erwartung: Werte nahe 0.0

# Impulsantwort: muss nach endlicher Zeit abklingen:
build/bin/firun -d 500 250 %I BpBu4/8-30 | awk '{sum+=($1<0?-$1:$1)} END{print "Energie:",sum}'
```
