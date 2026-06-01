<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2025-2026 Jörg Simbrig -->

# Concept: fiview2 — Reimagined GUI · fidgen — Universal Code Generator

**Status:** Concept / Design Study
**Scope:** Architecture, philosophy, technology selection, UX design, code generation strategy

---

## Part I — fiview2: The Reimagined Filter Design Workbench

### 1.1 Leitidee: Das Frequenzgang-Werkzeug als Instrument

Der entscheidende konzeptionelle Bruch gegenüber allen bisherigen Filter-Design-Tools:

**Die Frequenzkurve ist das primäre Eingabeobjekt — nicht die Parameter.**

Der Nutzer malt, was er will. Das System errechnet den nächstliegenden realisierbaren
Filter. Parameter sind sekundär — sie erscheinen als Konsequenz, nicht als Ursache.

Diese Umkehrung der Kausalität ist der Kern des gesamten Konzepts.

---

### 1.2 Die Werkstattmetapher

Ein analoges Elektroniklabor als mentales Modell:

```
┌────────────────────────────────────────────────────────────────────┐
│                       DIE WERKSTATT                                │
│                                                                    │
│  [Messgeräte — immer sichtbar, immer live]                        │
│   Frequenzgang       Pol-Nullstellen    Impulsantwort              │
│   (Hauptleinwand)    (immer live)       (Seitenblick)              │
│                                                                    │
│  [Werkzeuge — greifen und loslegen]                               │
│   Filterbaustein     Kaskade            Messung                    │
│   hinzufügen         verbinden          starten                    │
│                                                                    │
│  [Physikalische Grenzen — sichtbar, nicht hinter Fehlermeldungen] │
│   Stabilitätsgrenze  Nyquist-Linie      Quantisierungsband         │
│                                                                    │
│  [Geführter Pfad — immer sichtbar, nie erzwungen]                 │
│   ① Signaltyp wählen ② Band setzen ③ Filter wählen ④ Exportieren │
└────────────────────────────────────────────────────────────────────┘
```

Die "Physikalischen Grenzen" sind keine Fehlermeldungen — sie sind dauerhaft
sichtbare Zonen im Plot. Der Nutzer kann hinter die Grenzen fahren, sieht aber
sofort und visuell, warum das Design dort nicht realisierbar ist.

---

### 1.3 Das Spannungsfeld: Freiheit ↔ Führung

Traditionelle Lösungen schlagen fehl auf eine von zwei Arten:

**Fehler A: Zu viel Führung** (Wizard-Ansatz)
→ Erfahrene Nutzer werden eingesperrt. Nichts ist änderbar bevor Step 3 abgeschlossen ist.

**Fehler B: Zu viel Freiheit** (Parameter-Grid)
→ Anfänger wissen nicht, wo anfangen. 40 Eingabefelder ohne Kontext.

**fiview2-Lösung: Abhängigkeits-sichtbare Freiheit**

Kein Dialog ist modal. Kein Schritt ist gesperrt.
Aber: Jeder Parameter zeigt seinen Abhängigkeitszustand — visuell, nicht textuell.

```
Beispiel: Frequenzparameter
  [●] Abtastrate      → weiß/aktiv (immer zuerst definierbar)
  [●] Filtertyp       → weiß/aktiv
  [◐] Eckfrequenz     → halbiert/aktiv (editierbar, aber Abtastrate beeinflusst Grenzen)
  [○] Ordnung         → grau/aktiv   (hat Defaultwert, muss nicht berührt werden)
  [○] Rippel          → grau/inaktiv (nur sichtbar wenn Chebyshev gewählt)
```

Der "geführte Pfad" ist durch pulsierende Highlights angedeutet — ein subtiles,
nicht störendes Richtungsgefühl, das verschwindet sobald der Nutzer eigenständig handelt.

---

### 1.4 Architektur: Drei Schichten, Eine Oberfläche

```
┌─────────────────────────────────────────────────────────────────────┐
│  PRÄSENTATION (GUI)                                                 │
│  Dear ImGui (C++ immediate mode) — compiled → native + WASM       │
│  Gleicher C++-Code für: Desktop (Linux/macOS/Windows) + Browser    │
├─────────────────────────────────────────────────────────────────────┤
│  DESIGN ENGINE (C++)                                                │
│  fidlib-rt   Inverse Design   Stability Check   Signal Simulator   │
│  (vorhandene Bibliothek)      (Hurwitz)          (Echtzeit-FFT)     │
├─────────────────────────────────────────────────────────────────────┤
│  CODE GENERATOR (fidgen)                                            │
│  C99   C++20   Rust   Python   MATLAB/Octave   Verilog             │
└─────────────────────────────────────────────────────────────────────┘
```

#### Warum Dear ImGui + WebAssembly?

Dear ImGui ist immediate mode: pro Frame wird die gesamte UI neu gerendert.
Das bedeutet:
- Filter-Parameter ändern sich → Frequenzgang ändert sich → UI-Darstellung ändert sich.
  Alles in einem einzigen Schritt, ohne Observer-Muster, ohne State-Synchronisation.
- Derselbe C++-Code läuft via Emscripten im Browser als WebAssembly.
  kein Electron, kein separater Web-Stack, kein Installations-Overhead.
- Das fidlib-rt-Backend läuft ebenfalls als WASM im Browser.
  Vollständige Filterberechnung clientseitig — kein Server nötig.

**Konsequenz:** fiview2 läuft auf Linux, macOS, Windows, iOS/Android-Browser,
RPi, Jetson, und als einbettbares Webelement in Dokumentationsseiten —
alles aus einer Codebasis.

#### Alternativen und warum sie verworfen werden

| Toolkit | Problem |
|---------|---------|
| Qt | GPLv3 oder kommerzielle Lizenz — inkompatibel mit GPL-2.0-only fiview |
| GTK | Kein WASM, aufwändiges State-Management |
| Flutter | Dart-Runtime, keine direkte fidlib-Integration |
| Tauri/Electron | Chromium-Abhängigkeit, JS-C-Bridge-Overhead für Echtzeit-Plots |
| SDL2 + own widgets | Zu viel eigene Infrastruktur (fiview 0.9 ist bereits dieses Muster) |

Dear ImGui + Emscripten löst alle diese Probleme gleichzeitig.

---

### 1.5 Die fünf Arbeitsbereiche (immer sichtbar, frei anordnebar)

```
┌─────────────────────────────┬──────────────────────────────────────┐
│                             │                                      │
│  FREQUENZGANG               │  SIGNAL-EXPLORER                     │
│  (Hauptleinwand)            │  Mikrofon / Datei / Synthesizer      │
│                             │  "Wie klingt mein Signal?"           │
│  Zeichnen: Wunschkurve      │                                      │
│  Anzeigen: Ist-Kurve        │  Live-Spektrum mit                   │
│  Anzeigen: Abweichung       │  Vor/Nach-Filter-Vergleich           │
│  Anzeigen: Stabilitätszone  │                                      │
├─────────────────────────────┼──────────────────────────────────────┤
│                             │                                      │
│  FILTER-BAUSTEINE           │  POLE & NULLSTELLEN                  │
│  Drag & Drop                │  Interaktiv verschiebbar             │
│  LpBu4 · HpBe2 · BsBu2 …  │  z-Ebene + s-Ebene umschaltbar       │
│                             │  Stabilitätskreis immer sichtbar     │
│  Kaskade: visuell verbinden │                                      │
├─────────────────────────────┴──────────────────────────────────────┤
│  EXPORT / DEPLOY            GEFÜHRTER PFAD                         │
│  fidgen → Sprache wählen    ① Abtastrate ② Typ ③ Band ④ Export    │
└────────────────────────────────────────────────────────────────────┘
```

Jeder Bereich ist ein eigenständiges "Instrument". Alle Instrumente zeigen
immer den aktuellen Zustand. Kein Instrument blockiert ein anderes.

---

### 1.6 Inverse Filterdesign — der "Malen-Modus"

Der radikalste UX-Bruch: der Nutzer zeichnet den gewünschten Frequenzgang
mit der Maus direkt in den Frequenzgang-Plot.

```
Algorithmus:
1. Nutzer zeichnet Kurve (oder importiert Messkurve)
2. System approximiert mit minimaler Filter-Ordnung via:
   - Least-squares-Approximation im Log-Frequenz-Raum
   - Weighting nach Relevanz (Passband gewichtet stärker)
3. Angezeigt werden:
   - Der nächstliegende exakte fidlib-Filter (mit Spec-String)
   - Der Approximationsfehler als schattierte Zone um die Wunschkurve
   - Die Ordnung des erzeugten Filters
4. Nutzer kann iterieren: Ordnung erhöhen, Filtertyp wechseln
```

Dies ist kein neuer Algorithmus — es ist Vektor-Fitting / FDLS (Frequency Domain
Least Squares). Neu ist die Integration in eine unmittelbar-interaktive UI.

---

### 1.7 Echtzeit-Audio-Integration

```
[Mikrofon-Eingang] ─→ [Spektrum live] ─→ [Filter anzeigen] ─→ [Ausgabe]
                                                                    ↑
                       [Nutzer zieht Eckfrequenz] ─────────────────┘
```

Direkte Hörbarkeit jeder Parameteränderung. Latenz unter 20 ms (ein Ringpuffer,
Callback-Architektur identisch mit dem bestehenden firun-Modell).

Auf Desktop: PortAudio oder direkte ALSA/CoreAudio/WASAPI-Integration.
Im Browser: Web Audio API als Wrapper, fidlib-WASM als AudioWorklet-Prozessor.

---

### 1.8 Design-State als URL / QR-Code

Jeder Filter-Zustand ist vollständig in einem JSON-Objekt kodierbar:

```json
{
  "rate": 44100,
  "chain": [
    {"spec": "HpBu2/100", "enabled": true},
    {"spec": "LpBu4/4000", "enabled": true}
  ],
  "view": {"freq_min": 20, "freq_max": 22050, "db_range": 80}
}
```

Dieses JSON wird Base64-URL-kodiert und als URL-Fragment übergeben:
`https://fidlib-rt.app/#eyJyYXRlIjo0NDEwMC4uLn0=`

Ein Link teilt einen vollständigen, reproduzierbaren Filter-Design-Zustand.

---

### 1.9 Anfängerführung: Der "Guided Mode"

Für Anfänger (opt-in, nicht Standard):

```
┌─────────────────────────────────────────────────────────────────┐
│  Was willst du filtern?                                         │
│                                                                 │
│  [Audio-Signal]   [Sensordaten]   [EEG/Biosignal]   [Eigenes]  │
│                                                                 │
│  → Wählt automatisch passende Defaults                          │
│     (Abtastrate, Frequenzbereich, Filtertyp-Vorschläge)        │
└─────────────────────────────────────────────────────────────────┘
        ↓
┌─────────────────────────────────────────────────────────────────┐
│  Was stört dich am Signal?                                      │
│                                                                 │
│  [Hochfrequentes Rauschen]  [DC-Drift]  [Netzbrumm 50Hz]      │
│  [Schmalbandstörung]        [Ich weiß es nicht / zeig mir]     │
│                                                                 │
│  → Erzeugt Filterkaskade als Ausgangspunkt                      │
└─────────────────────────────────────────────────────────────────┘
        ↓
        Workbench öffnet sich mit vorkonfiguriertem Design.
        Guided Mode deaktiviert sich. Volle Freiheit ab jetzt.
```

Der Guided Mode ist ein Startschuss — kein Käfig.

---

### 1.10 Vergleichsmodus: Mehrere Filter gleichzeitig

```
Filter A: LpBu4/4000   ──── gelb
Filter B: LpBe4/4000   ──── blau   (gleiche Eckfrequenz, andere Charakteristik)
Filter C: LpCh4/-1/4000 ─── rot    (Chebyshev, 1 dB Ripple)
```

Alle drei Frequenzgänge überlagert, Gruppendelay getrennt dargestellt.
Interaktiv: Parameter von A ändern → A-Kurve aktualisiert sich, B und C bleiben.

Ideal für Lehrzwecke: "Warum Butterworth statt Bessel?"

---

## Part II — fidgen: Der universelle Codegenerator

### 2.1 Kernphilosophie

fidgen beantwortet eine Frage, die in der Digitalsignalverarbeitung immer wieder
offen bleibt:

**"Ich habe einen Filter — gib mir maximalen Speed auf meiner Zielplattform,
im Code meiner Wahl, ohne DSP-Studium."**

fidgen ist kein Template-System. Es ist ein Compiler für Filter.

---

### 2.2 Was "maximal schnell" wirklich bedeutet

```
Drei Optimierungsdimensionen:

Dimension 1: Algorithmisch
  Direkte Form II    → Standardfall
  Transponierte Form → Bessere numerische Stabilität bei hoher Ordnung
  Lattice-Form       → Garantierte Stabilität bei Koeffizientenanpassung
  SOS (Biquad-Kette) → Standard für Ordnung > 4

Dimension 2: Compile-Zeit-Information
  Ordnung unbekannt  → Schleife über Biquads (fidlib-rt heute)
  Ordnung bekannt    → Schleife vollständig aufgerollt → Compiler kann
                       vollständig optimieren (Register-Allokation, Scheduling)
  Koeffizienten fix  → Konstanten eingebettet (kein Load-Store)
  Koeffizienten var  → Array-Pointer (Standard-Laufzeit-Betrieb)

Dimension 3: SIMD-Strategie
  Skalar             → ein Sample pro Schritt
  Sample-parallel    → ein Biquad verarbeitet N Samples gleichzeitig (NEON/SSE)
  Kanal-parallel     → N Kanäle gleichzeitig durch denselben Filter (NEON/SSE)
  Stage-parallel     → Mehrere Biquad-Stages gleichzeitig (weniger nützlich)
```

fidgen wählt automatisch die beste Kombination — oder der Nutzer überschreibt sie.

---

### 2.3 Zielsprachen und ihre Besonderheiten

#### C99 / C11 (primäres Ziel)

```c
// Generiert von fidgen für LpBu4/4000 @ 44100 Hz, Ordnung=4 bekannt
// Target: AArch64 NEON, Kanal-parallel (4 Kanäle)

#include <arm_neon.h>

// State: 2 Variablen pro Biquad-Stage × 2 Stages = 4 float64x2_t
typedef struct {
    float64x2_t s0, s1, s2, s3;
} LpBu4_State;

// Koeffizienten: zur Laufzeit befüllen via fid_design_coef()
typedef struct {
    double b0, b1, b2, a1, a2;  // Stage 0
    double c0, c1, c2, d1, d2;  // Stage 1
} LpBu4_Coef;

// Verarbeite 4 Kanäle gleichzeitig — ein NEON-Vektor pro Variable
static inline void lpbu4_step_4ch(
    LpBu4_State *restrict st,
    const LpBu4_Coef *restrict coef,
    const double in[4],
    double out[4])
{
    float64x2_t x0 = vld1q_f64(in + 0);
    float64x2_t x1 = vld1q_f64(in + 2);
    // ... vollständig aufgerollte Biquad-Kaskade ...
}
```

Eigenschaften des generierten Codes:
- Keine Schleifen (Ordnung ist zum Generierungszeitpunkt bekannt)
- Keine Branches im heißen Pfad
- Inline-fähig (`static inline`)
- Keine stdlib-Abhängigkeit (embedded-tauglich)
- SPDX-Header automatisch gesetzt
- Mitgelieferter Testvektor gegen fidlib-rt Referenz

#### C++20

```cpp
// Template-basiert: Ordnung als Template-Parameter → vollständiges Compile-Zeit-Unrolling
template<int Order, typename T = double>
class ButterworthLP {
    static constexpr int N_STAGES = Order / 2;
    std::array<T, N_STAGES * 2> state_{};

public:
    struct Coef { std::array<T, N_STAGES * 5> v; };

    [[nodiscard]] T step(const Coef& c, T x) noexcept;
    void reset() noexcept { state_.fill(T{0}); }
};

// Spezialisierungen für häufige Ordnungen können mit AVX2 explizit optimiert werden
template<> [[nodiscard]] double
ButterworthLP<4, double>::step(const Coef&, double) noexcept;
```

#### Rust

```rust
// Generiert als Zero-Cost-Abstraktion
pub struct LpBu4State { s: [f64; 4] }
pub struct LpBu4Coef  { v: [f64; 10] }

impl LpBu4State {
    #[inline(always)]
    pub fn step(&mut self, c: &LpBu4Coef, x: f64) -> f64 { ... }

    pub fn reset(&mut self) { self.s = [0.0; 4]; }
}
// no_std-kompatibel, kein alloc
```

#### Python (NumPy / SciPy-kompatibel)

```python
# Für schnelle Prototypen und Testverifikation
# Nicht RT-tauglich, aber exakt dieselben Koeffizienten

import numpy as np
from scipy.signal import sosfilt

# Generierter Code enthält die SOS-Matrix direkt:
_LpBu4_sos = np.array([
    [0.00093220, 0.00186441, 0.00093220, 1.0, -1.90422, 0.91021],
    [0.00093220, 0.00186441, 0.00093220, 1.0, -1.97186, 0.97395],
])

def lpbu4_filter(x: np.ndarray) -> np.ndarray:
    return sosfilt(_LpBu4_sos, x)
```

#### MATLAB / Octave

```matlab
% SOS-Form für direkte Verwendung in filter() / sosfilt()
sos_LpBu4 = [ ...
  0.00093220  0.00186441  0.00093220  1.0  -1.90422  0.91021; ...
  0.00093220  0.00186441  0.00093220  1.0  -1.97186  0.97395  ...
];
% Verwendung: y = sosfilt(sos_LpBu4, x);
```

#### Julia

```julia
# DSP.jl-kompatibel
using DSP

const LPBU4_SOS = SecondOrderSections(
    [Biquad(0.000932, 0.001864, 0.000932, -1.904221, 0.910208),
     Biquad(0.000932, 0.001864, 0.000932, -1.971863, 0.973948)],
    1.0
)
y = filt(LPBU4_SOS, x)
```

#### Verilog / SystemVerilog (FPGA-Ziel) — der unkonventionelle Sprung

```verilog
// Generiert von fidgen für Xilinx 7-Series / Intel Cyclone
// Fixed-Point: 24-Bit Koeffizienten, 48-Bit Akkumulator
// Latenz: 4 Takte (Pipeline-Tiefe = Filterordnung / 2)

module lpbu4_filter #(
    parameter DATA_WIDTH = 24,
    parameter COEF_WIDTH = 24,
    parameter ACCUM_WIDTH = 48
)(
    input  wire                    clk,
    input  wire                    rst_n,
    input  wire signed [DATA_WIDTH-1:0] x_in,
    input  wire                    x_valid,
    output reg  signed [DATA_WIDTH-1:0] y_out,
    output reg                     y_valid
);
    // ... vollständig synthetisierbarer Biquad-Kaskaden-Code ...
    // ... Koeffizienten als Localparam eingebettet ...
endmodule
```

Das ist der wirklich unkonventionelle Sprung: **der gleiche fidlib-rt Filter-Design-Code
erzeugt FPGA-implementierbares Hardware-Design**. Kein anderes Open-Source-Tool verbindet
diese beiden Welten auf diese Weise.

Warum das möglich ist: Die Biquad-Struktur ist universell — in C und in Verilog
ist es dieselbe Rechenvorschrift, nur die Darstellung der Arithmetik unterscheidet sich
(IEEE-754 Floating Point vs. Fixed-Point mit Skalierung).

---

### 2.4 fidgen CLI Interface

```
fidgen — fidlib-rt code generator
Usage: fidgen [OPTIONS] <spec>

Arguments:
  <spec>          Filter specification (e.g. "LpBu4/4000")

Options:
  -r, --rate <Hz>         Sample rate [default: 44100]
  -l, --lang <LANG>       Output language: c99 c++20 rust python matlab julia verilog
                          [default: c99]
  -o, --output <FILE>     Output file [default: stdout]
  --simd <TARGET>         SIMD target: none scalar neon sse2 avx2 avx512
                          [default: auto]
  --channels <N>          Generate N-channel parallel code [default: 1]
  --coef-style <STYLE>    fixedpoint | float32 | float64 [default: float64]
  --form <FORM>           sos | direct1 | direct2 | lattice [default: sos]
  --with-test             Include golden-reference test vectors
  --with-bench            Include benchmark harness
  --func-name <NAME>      Function/class name prefix [default: derived from spec]
  --fpga-width <BITS>     Fixed-point data width for Verilog [default: 24]
  --realtime              Add RT-safety annotations and zero-alloc guarantee

Examples:
  fidgen -l c99 --simd neon --channels 4 --with-test "LpBu4/4000" -r 44100
  fidgen -l verilog --fpga-width 24 "BpBu4/100-3000" -r 44100
  fidgen -l rust --simd sse2 --realtime "HpBe2/200" -r 48000
  fidgen -l python --with-test "BsBu2/49-51" -r 250
```

---

### 2.5 Optimierungslevels im Detail

```
Level 0: Portable
  Keine Intriniscs. Funktioniert auf jeder C99-Plattform.
  GCC/Clang -O2 rollt Schleifen ab wenn Ordnung als Compile-Zeit-Konstante.

Level 1: Compiler-guided
  Ordnung als Compile-Zeit-Konstante übergeben (template<int N> in C++).
  __builtin_expect für Branch-Prediction-Hints.
  restrict-Qualifier für Alias-freie Zeiger.
  GCC/Clang können jetzt vollständig auto-vektorisieren.

Level 2: SIMD-explicit (NEON / SSE2 / AVX2)
  Kanal-parallele Verarbeitung: 2 (float64/NEON), 4 (float32/NEON),
  2 (float64/SSE2), 4 (float64/AVX2) Kanäle gleichzeitig.
  Handgeschriebene Intrinsics-Sequenz für den Biquad-Kern.
  Compiler übernimmt Scheduling und Load-Store-Optimierung.

Level 3: SIMD-explicit mit Sample-Batching
  Nicht ein Sample pro Aufruf, sondern ein Block (z.B. 64 Samples).
  Ermöglicht SIMD über die Zeit-Dimension — effizient bei großen Puffern.
  Latenz: Blockgröße Samples.

Level 4: FPGA Fixed-Point
  Koeffizienten als Fixed-Point (Qm.n-Format), vollständig pipelined.
  Latenz: Filterordnung/2 Takte.
  Durchsatz: ein Sample pro Takt (bei ausreichender Taktfrequenz).
```

---

### 2.6 Was immer mitgeneriert wird (niemals weglassbar)

1. **SPDX-Header** (GPL-2.0-or-later für generierten Code, oder nach Nutzerwahl)
2. **Testvektor** gegen fidlib-rt Skalar-Referenz (Abweichung < 1 ULP)
3. **Koeffizient-Listing** als Kommentar (Transferfunktion in Z und S)
4. **Latenz-Dokumentation** als Kommentar (Gruppendelay-Schätzung)
5. **Verwendungsbeispiel** (drei Zeilen Minimal-Code)

---

### 2.7 Integration in fiview2

In der GUI gibt es einen "Export"-Bereich:

```
┌─────────────────────────────────────────────────────────────────────┐
│  EXPORT / DEPLOY                                                    │
│                                                                     │
│  Sprache:  [C99 ▼]   SIMD: [auto ▼]   Kanäle: [1]   Level: [2]   │
│                                                                     │
│  [Vorschau generieren]   [Datei speichern]   [In Zwischenablage]   │
│                                                                     │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │ // Generated by fidgen 1.0 — fidlib-rt                      │  │
│  │ // LpBu4/4000 @ 44100 Hz — Butterworth LP 4th order        │  │
│  │ // SIMD: NEON AArch64 — 2-channel parallel                  │  │
│  │ ...                                                          │  │
│  └──────────────────────────────────────────────────────────────┘  │
│                                                                     │
│  Alternativ:  [URL teilen]  [QR-Code]  [fidgen CLI-Befehl]        │
└─────────────────────────────────────────────────────────────────────┘
```

Der "fidgen CLI-Befehl" zeigt den exakten Kommandozeilenbefehl, der denselben
Code erzeugt — Dokumentation und Reproduzierbarkeit in einem.

---

## Part III — Implementierungspfad

### Phase 0: fidgen allein (CLI, kein GUI)

```
Scope:  fidgen als eigenständiges CLI-Tool
        Sprachen: C99, C++20, Python
        SIMD: none + auto (Compiler-guided)
        Keine FPGA-Unterstützung noch
Aufwand: ~3–4 Wochen

Warum zuerst:
  fidgen ist von fiview2 unabhängig nutzbar.
  Die Qualität des generierten Codes kann ohne GUI verifiziert werden.
  fidgen-Output kann als Qualitätsmesslatte für die GUI dienen.
```

### Phase 1: fiview2-Kern (ohne Audio, ohne WASM)

```
Scope:  Dear ImGui + fidlib-rt, Desktop only
        Frequenzgang-Plot (live), Pol-Nullstellen-Plot
        Parameterbearbeitung, Filterwahl, fidgen-Export
Aufwand: ~6–8 Wochen
```

### Phase 2: Audio-Integration + WASM

```
Scope:  PortAudio/ALSA-Integration, Echtzeit-Hörbarkeit
        Emscripten WASM-Build, Browser-Deployment
        URL-Sharing
Aufwand: ~4 Wochen
```

### Phase 3: Inverse Design + Guided Mode

```
Scope:  Malen-Modus (FDLS-Approximation)
        Anfänger-Wizard
        Vergleichsmodus (mehrere Filter)
Aufwand: ~4–6 Wochen
```

### Phase 4: fidgen — SIMD-Levels + FPGA

```
Scope:  NEON/SSE2/AVX2 explizit, Kanal-parallel
        Verilog/SystemVerilog Fixed-Point
        Rust, Julia
Aufwand: ~4–6 Wochen
```

---

## Zusammenfassung: Was dieses Projekt einzigartig macht

| Eigenschaft | Status Quo | fiview2 + fidgen |
|---|---|---|
| Cross-Platform GUI | x11-only (fiview) | Linux + macOS + Windows + Browser |
| Filterdesign | Parameter eingeben | Kurve malen → Parameter folgen |
| Anfängertauglichkeit | Keine Führung | Guided Mode mit Freiheits-Escape |
| Codegenerierung | fiview GUI only | fidgen CLI + GUI, alle Sprachen |
| FPGA-Ziel | Nicht vorhanden | Verilog-Output aus gleichem Design |
| Teilbarkeit | Nicht vorhanden | URL / QR-Code für jeden Designzustand |
| SIMD-Codegen | Nicht vorhanden | NEON/SSE2/AVX2 kanal-parallel |
| Echtzeit-Audio | Nicht vorhanden | Mikrofoneingang, Hören während Designen |

Das Geniale ist nicht eine einzelne Feature — es ist die Konsequenz, mit der
**eine Entscheidung im Designprozess** (welcher Filter?) direkt und sofort
in alle drei Richtungen ausstrahlt:
- Was klingt es? (Audio-Feedback)
- Wie sieht es aus? (Frequenzgang, Pol-Nullstellen)
- Wie deploye ich es? (Code, Hardware, Befehlszeile)

Gleichzeitig, ohne Wartezeit, ohne modales Nachfragen.

---

*Quellen: fidlib.txt, fidrf_cmdlist.h, doc/fidlib-codegen-analysis.md,
Dear ImGui (github.com/ocornut/imgui), Emscripten, FDLS-Approximation (IEEE TASLP 2005)*
