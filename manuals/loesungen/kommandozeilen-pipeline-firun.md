<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2025-2026 Jörg Simbrig -->

# Lösung: Signalverarbeitung in der Shell mit firun

## Das Problem

Messdaten sollen auf der Kommandozeile gefiltert werden — ohne eigenes C-Programm
zu schreiben. Die Daten liegen als ASCII-Text, binäres PCM oder WAV vor.
firun liest von `stdin` und schreibt nach `stdout` und lässt sich in beliebige
Shell-Pipelines einbetten.

**Anwendungsfälle:**
- WAV-Datei filtern und als neue WAV speichern
- CSV-Messdaten glätten (gleitender Mittelwert / IIR-Tiefpass)
- Impuls- und Stufenantwortt eines Filters visualisieren
- Mehrkanal-PCM-Streams parallel filtern
- Filter-Kaskaden (mehrere Specs hintereinander)

---

## Welche Mittel des Projekts helfen

- **`firun`** — CLI-Werkzeug: `firun [Optionen] <Rate> <Format> <FilterSpec...>`
- **fidlib Fispec-DSL** — Filter-Typ, Ordnung, Frequenz als kompakter String
- **Format-Codes** — ASCII, 16-Bit PCM, 32-Bit Float, Multi-Kanal
- **Synthetische Signale** — `%I` (Impuls), `%S` (Stufe) als Eingang ohne Datei

---

## Schritt 1: firun bauen

```bash
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
      -DFIDLIB_SIMD=ON -DFIDLIB_FFT=ON \
      -DBUILD_TOOLS=ON \
      -S . -B build

cmake --build build -j$(nproc)
# Ergebnis: build/bin/firun
```

Kurztest:
```bash
build/bin/firun -d 5 44100 %I LpBu4/1000
# Gibt 5 ASCII-Werte der Impulsantwort aus
```

---

## Format-Codes Übersicht

| Code | Bedeutung |
|------|-----------|
| `a` | ASCII floating-point (ein Wert pro Zeile) |
| `s` | Signed 16-Bit little-endian (PCM Audio) |
| `S` | Signed 16-Bit big-endian |
| `w` | Unsigned 16-Bit little-endian |
| `f` | 32-Bit float (machine order) |
| `d` | 64-Bit double (machine order) |
| `b` | Unsigned 8-Bit |
| `c` | Signed 8-Bit |
| `_` | Dummy-Byte (überspringen) |
| `%I` | Synthetischer Impuls (stdin ignorieren) |
| `%S` | Synthetische Stufe (stdin ignorieren) |

Mehrkanal: Format-Code + Zahl = Samples pro Frame, z.B. `s2` = Stereo S16LE.

---

## Anwendungsfall 1: Filter-Eigenschaften analysieren

### Impulsantwort ausgeben

```bash
# 500 Samples der Impulsantwort eines 4-poligen Butterworth-Tiefpasses:
build/bin/firun -d 500 44100 %I LpBu4/4000
```

### Stufenantwortt (DC-Gain prüfen)

```bash
# Stufenantwortt: muss für Tiefpass gegen 1.0 konvergieren:
build/bin/firun -d 1000 44100 %S LpBu4/4000 | tail -3
```

### Frequenzantwort visualisieren (mit gnuplot)

```bash
build/bin/firun -d 8192 44100 %I LpBu4/4000 > impulse.dat

gnuplot << 'EOF'
set terminal qt
N = 8192
set xrange [0:22050]
set xlabel "Frequenz [Hz]"
set ylabel "Magnitude [dB]"
set title "Frequenzantwort LpBu4/4000 @ 44100 Hz"
fft_mag(filename) = system(sprintf("python3 -c \"\
import numpy as np; \
x=np.loadtxt('%s'); \
f=np.fft.rfftfreq(len(x),1/44100.0); \
H=20*np.log10(abs(np.fft.rfft(x))+1e-12); \
[print(f[i],H[i]) for i in range(len(f))]\"", filename))
# einfacher: Daten mit Python vorberechnen, dann plotten
EOF
```

---

## Anwendungsfall 2: WAV-Datei filtern

Mit `sox` als Konvertierungs-Wrapper:

```bash
# Mono WAV → Tiefpass 4000 Hz → neues WAV:
sox eingabe.wav -t raw -e signed -b 16 -r 44100 - \
    | build/bin/firun 44100 s LpBu4/4000 \
    | sox -t raw -e signed -b 16 -r 44100 -c 1 - ausgabe.wav
```

```bash
# Stereo WAV (interleaved L+R) → Bandpass 100-3000 Hz auf beiden Kanälen:
sox eingabe_stereo.wav -t raw -e signed -b 16 -r 44100 - \
    | build/bin/firun 44100 s2 BpBu4/100-3000 BpBu4/100-3000 \
    | sox -t raw -e signed -b 16 -r 44100 -c 2 - ausgabe_stereo.wav
```

---

## Anwendungsfall 3: CSV-Messdaten glätten

Messdaten als ASCII (ein Messwert pro Zeile, z.B. von einem Sensor):

```bash
# Messdaten glätten: IIR-Tiefpass bei 10 Hz, Sensor-Rate 100 Hz:
cat sensor_daten.csv | build/bin/firun 100 a LpBu2/10 > geglättet.csv
```

Für einen gleitenden Mittelwert über N Werte: Boxcar FIR der Länge N.
Da fidlib keinen direkten Boxcar-CLI-Syntax hat, nutze den IIR-Approximation:

```bash
# Sehr sanfter IIR-LP als Glättungsfilter (niedriger Cutoff):
cat sensor.csv | build/bin/firun 100 a LpBu1/1 > geglättet.csv
```

---

## Anwendungsfall 4: Filter-Kaskade

Mehrere Filter-Specs hintereinander = serielle Kaskade:

```bash
# DC-Entfernung → Bandpass → Notch 50 Hz (EEG-Vorverarbeitung):
cat eeg.txt | \
    build/bin/firun 250 a \
        HpBu2/0.5 \
        BpBu4/0.5-45 \
        BsBu2/49-51 \
    > eeg_gefiltert.txt
```

```bash
# Zwei Kerbfilter: 50 Hz und 100 Hz Oberwelle:
cat signal.txt | \
    build/bin/firun 44100 a \
        BsBu2/49-51 \
        BsBu2/99-101 \
    > entstört.txt
```

---

## Anwendungsfall 5: Ausgabe zeitlich begrenzen

```bash
# Nur die ersten 2 Sekunden filtern (bei 44100 Hz = 88200 Samples):
cat long_audio.raw | \
    build/bin/firun -d 2s 44100 s LpBu4/4000 > short_filtered.raw

# Oder exakt N Samples:
build/bin/firun -d 44100 44100 %I LpBu4/4000 > one_second_impulse.dat
```

Die `-d`-Option akzeptiert: `Ns` (Sekunden), `Nm` (Minuten), `N` (Samples).

---

## Anwendungsfall 6: Live-Monitoring mit aplay (Linux)

```bash
# Mikrofon → Hochpass (DC-Entfernung) → Lautsprecher in Echtzeit:
arecord -f S16_LE -r 44100 -c 1 | \
    build/bin/firun 44100 s HpBu2/20 | \
    aplay -f S16_LE -r 44100 -c 1
```

```bash
# Bandpass-Radio-Demodulation (SDR):
# rtl_sdr → FM-Demodulator → Tiefpass → aplay (vereinfacht):
rtl_sdr -f 100e6 -s 250000 -g 30 - 2>/dev/null | \
    # ... FM-Demodulation hier ... | \
    build/bin/firun 48000 s LpBu4/15000 | \
    aplay -f S16_LE -r 48000 -c 1
```

---

## Fispec-Schnellreferenz

| Typ | Kürzel | Beispiel | Beschreibung |
|-----|--------|---------|--------------|
| Butterworth LP | `LpBu` | `LpBu4/1000` | Tiefpass 4. Ord., -3dB bei 1 kHz |
| Butterworth HP | `HpBu` | `HpBu4/200` | Hochpass |
| Butterworth BP | `BpBu` | `BpBu4/300-3000` | Bandpass |
| Butterworth BS | `BsBu` | `BsBu2/49-51` | Band-Stop (Notch) |
| Bessel LP | `LpBe` | `LpBe4/1000` | Tiefpass, linearer Phasengang |
| Chebyshev LP | `LpCh` | `LpCh4/-1/1000` | Tiefpass, 1 dB Ripple, steilere Flanke |
| Biquad Peaking | `PkBq` | `PkBq/1000/1/6` | Parametrischer EQ: +6 dB bei 1 kHz, Q=1 |
| Biquad Low-Shelf | `LsBq` | `LsBq/200/6` | Bassanhebung +6 dB unter 200 Hz |
| Allpass | `ApBu` | `ApBu2/1000` | Nur Phasendrehung |
| Resonator | `BpRe` | `BpRe/1000/10` | Schmalbandiger BP, Q=10 |

---

## Problemlösungen

| Problem | Ursache | Lösung |
|---------|---------|--------|
| Ausgabe bleibt leer | stdin liefert nichts | `-d N` setzen oder `%I`/`%S` verwenden |
| Falsche Amplitude | Falsches Format (s vs a) | Format an Eingabedaten anpassen |
| Rauschen/Artefakte | Falscher Kanal-Count | Format-Digit prüfen (`s2` für Stereo) |
| `firun` nicht gefunden | `BUILD_TOOLS` nicht aktiviert | cmake mit `-DBUILD_TOOLS=ON` neu |
| Sehr langsame Ausgabe | ASCII-Format (a) | Binäres Format (`s`, `f`) für Performance |
