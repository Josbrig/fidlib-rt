<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2025-2026 Jörg Simbrig -->

# Lösung: Digitale Filter als Code generieren mit fidgen

## Das Problem

Ein Embedded-System, FPGA oder Microcontroller soll einen bekannten IIR-Filter
ausführen — ohne fidlib-Laufzeit, ohne malloc, ohne dynamische Filter-Konfiguration.
Der Filter ist zur Entwicklungszeit bekannt; nur die Ausführung muss maximal
effizient sein.

**Anforderungen:**
- Koeffizienten vollständig ausgerollt, kein Doppelzeiger, kein Array-Loop
- Sprache und Zielumgebung vom Aufrufer gewählt (C, C++20, Python, Rust, MATLAB,
  Julia, Verilog, SystemVerilog)
- Optional: NEON/SSE2/AVX2-SIMD-Varianten generieren ohne eigenen Intrinsics-Code
- Optional: Fixed-Point-RTL für FPGA-Synthese

---

## Welche Mittel des Projekts helfen

- **`fidgen`** — CLI-Tool: Filterspez. → ausgerollter Filtercode in 8 Sprachen
- **`FilterDescriptor`** — C++20 RAII-Wrapper um fidlib; extrahiert SOS-Kaskade
- **`Generator`-Fabrik** — abstrakte Basis + Sprachgeneratoren, erweiterbar
- **`fidgen_generate()` C-API** — Integration in Fremdprojekte ohne C++

---

## Architektur

```
fidlib fid_design()
       │
       ▼
FilterDescriptor::from_spec()   ← SOS-Extraktion, Jury-Stabilitätsprüfung
       │
       ▼
Generator::create("c99")        ← Fabrik wählt Sprachgenerator
       │
       ▼
gen->generate(out, desc, opts)  ← ausgerollter Filtercode auf stdout / Datei
```

Der generierte Code hat **keine Laufzeitabhängigkeit** auf fidlib.

---

## Quickstart

```bash
# Butterworth-Tiefpass 4. Ordnung, 1 kHz, 44.1 kHz Sample-Rate → C99
fidgen -l c99 -r 44100 LpBu4/1000

# In Datei schreiben
fidgen -l c99 -r 44100 -n lpf -o lpf.h LpBu4/1000

# Filter-Zusammenfassung prüfen ohne Code zu erzeugen
fidgen --check LpBu4/1000

# Alle unterstützten Sprachen auflisten
fidgen --list-langs
```

---

## Generierte Sprachen

### C99 (`-l c99`)

```bash
fidgen -l c99 -r 44100 LpBu4/1000
```

Erzeugt `lpbu4_1000.h` (oder stdout) mit:

```c
typedef struct Lpbu41000State { double s[4]; } Lpbu41000State;
typedef struct Lpbu41000Coef  { double b0_0, b1_0, ..., gain; } Lpbu41000Coef;

static const Lpbu41000Coef lpbu4_1000_coef = { ... };

static inline void   lpbu4_1000_reset(Lpbu41000State *st);
static inline double lpbu4_1000_step (Lpbu41000State *st,
                                       const Lpbu41000Coef *c, double x);
```

**Verwendung:**

```c
#include "lpbu4_1000.h"

Lpbu41000State st;
lpbu4_1000_reset(&st);

double y = lpbu4_1000_step(&st, &lpbu4_1000_coef, x);
```

---

### C++20 (`-l cpp20`)

```bash
fidgen -l cpp20 -r 44100 LpBu4/1000
```

```cpp
class Lpbu41000Filter {
    struct State { double s[4] = {}; };
    struct Coef  { double b0_0, b1_0, ...; };
    static constexpr Coef coef = { ... };

    static void reset(State& st) noexcept;
    [[nodiscard]] static double step(State& st, double x) noexcept;
};
```

**Verwendung:**

```cpp
#include "lpbu41000filter.hpp"

Lpbu41000Filter::State st;
Lpbu41000Filter::reset(st);
double y = Lpbu41000Filter::step(st, x);
```

---

### Python (`-l python` / `-l py`)

```bash
fidgen -l python -r 44100 LpBu4/1000
```

```python
class Lpbu41000Filter:
    __slots__ = ('_s',)
    def reset(self) -> None: ...
    def step(self, x: float) -> float: ...
```

---

### Rust (`-l rust` / `-l rs`)

```bash
fidgen -l rust -r 44100 LpBu4/1000
```

```rust
pub struct Lpbu41000State { s: [f64; 4] }

impl Lpbu41000State {
    pub fn new() -> Self { ... }
    pub fn reset(&mut self) { ... }
    pub fn step(&mut self, x: f64) -> f64 { ... }
}

impl Default for Lpbu41000State { ... }
```

`no_std`-kompatibel, kein `alloc`.

---

### MATLAB / Octave (`-l matlab` / `-l octave` / `-l m`)

```bash
fidgen -l matlab -r 44100 LpBu4/1000
```

```matlab
function y = lpbu4_1000(x)
    persistent s
    if isempty(x), s = zeros(4,1); y = []; return; end
    % ... Direct Form II ausgerollt
end
```

Reset via `lpbu4_1000([])`.

---

### Julia (`-l julia` / `-l jl`)

```bash
fidgen -l julia -r 44100 LpBu4/1000
```

```julia
mutable struct Lpbu41000State
    s::Vector{Float64}
end

function reset!(f::Lpbu41000State) ... end
function step!(f::Lpbu41000State, x::Float64)::Float64 ... end
```

---

### Verilog (`-l verilog` / `-l v`)

```bash
fidgen -l verilog --fpga-bits 24 -r 44100 LpBu4/1000
```

Erzeugt vollständig synthetisierbares RTL-Modul:

```verilog
`define LPBU4_1000_B0_0  24'h...    // quantisierte Koeffizienten
module lpbu4_1000 (
    input  wire clk, rst_n,
    input  wire signed [23:0] x_in,
    output reg  signed [23:0] y_out
);
```

Ports: `clk`, `rst_n` (aktiv-low), `x_in[W-1:0]`, `y_out[W-1:0]`.

---

### SystemVerilog (`-l systemverilog` / `-l sv`)

Wie Verilog, aber mit `logic`-Typen statt `wire`/`reg`.

---

## SIMD-Varianten (C99, `--simd`)

```bash
fidgen -l c99 --simd neon   -r 44100 LpBu4/1000   # ARM NEON float64x2_t
fidgen -l c99 --simd sse2   -r 44100 LpBu4/1000   # x86 SSE2 __m128d
fidgen -l c99 --simd avx2   -r 44100 LpBu4/1000   # x86 AVX2 __m256d
fidgen -l c99 --simd auto   -r 44100 LpBu4/1000   # alle drei + #ifdef-Guards
```

SIMD-Varianten verarbeiten 2 (NEON/SSE2) bzw. 4 (AVX2) Kanäle parallel
mit denselben Koeffizienten. Einsatz: Stereo- oder Mehrkanalfilterung.

`--simd auto` erzeugt alle drei Varianten unter `#if defined(__ARM_NEON)`,
`#if defined(__SSE2__)`, `#if defined(__AVX2__)` — ein Header läuft auf allen
Plattformen.

---

## FPGA: Fixed-Point-Optionen

```bash
# Bit-Breite und Fraktionalbits explizit setzen
fidgen -l verilog --fpga-bits 24 --fpga-frac 20 -r 44100 LpBu4/1000

# Fraktionalbits automatisch aus Koeffizientenbereich berechnen (Standard)
fidgen -l verilog --fpga-bits 24 -r 44100 LpBu4/1000
```

| Option | Bedeutung | Standard |
|--------|-----------|---------|
| `--fpga-bits N` | Daten- und Koeffizientenbreite in Bit | 24 |
| `--fpga-frac N` | Fraktionalbits (Q-Format) | auto |

Akkumulatorbreite ist immer `2 * fpga_bits + 2` (kein Overflow bei Produkten).

---

## C-API (Integration ohne C++)

`#include <fidgen/fidgen.h>`, linken gegen `fidgen_lib`.

```c
char *code = NULL;
fidgen_error_t rc = fidgen_generate(
    "LpBu4/1000",   /* Filterspez.   */
    44100.0,         /* Sample-Rate   */
    "c99",           /* Sprache       */
    "lpfilter",      /* Präfix (Name) */
    NULL,            /* Standardoptionen */
    &code            /* Ausgabepuffer */
);
if (rc == FIDGEN_OK) {
    puts(code);
    fidgen_free(code);
}
```

Optionen über `fidgen_options_t`:

```c
fidgen_options_t opts = {
    .simd       = FIDGEN_SIMD_NEON,
    .with_guard = 1,
    .with_test  = 0,
    .fpga_bits  = 24,
    .fpga_frac  = -1,   /* auto */
};
```

---

## Optionsreferenz

| Option | Beschreibung |
|--------|-------------|
| `-l LANG` | Ausgabesprache (Standard: `c99`) |
| `-r RATE` | Sample-Rate in Hz (Standard: `44100`) |
| `-f FREQ` | Eckfrequenz 0 überschreiben |
| `-F FREQ` | Eckfrequenz 1 überschreiben (Bandpass/Bandsperre) |
| `-n NAME` | C-Bezeichner-Präfix (Standard: aus Spez. abgeleitet) |
| `-o FILE` | Ausgabedatei (Standard: stdout) |
| `--simd LEVEL` | SIMD-Variante: `none`\|`neon`\|`sse2`\|`avx2`\|`auto` |
| `--with-test` | Selbstest-Skelett anhängen |
| `--no-guard` | Include-Guard / `#pragma once` weglassen |
| `--fpga-bits N` | Verilog Datenwortbreite |
| `--fpga-frac N` | Verilog Fraktionalbits |
| `--check` | Filter-Zusammenfassung ausgeben, kein Code |
| `--list-langs` | Alle Sprachschlüssel ausgeben |
| `--version` | Versionsnummer ausgeben |

---

## Unterstützte Filterspezifikationen (Auswahl)

| Spezifikation | Typ |
|--------------|-----|
| `LpBu4/1000` | Butterworth-Tiefpass 4. Ordnung, 1 kHz |
| `HpBu2/8000` | Butterworth-Hochpass 2. Ordnung, 8 kHz |
| `BpBu4/500-2000` | Butterworth-Bandpass 4. Ordnung |
| `BsBu4/500-2000` | Butterworth-Bandsperre 4. Ordnung |
| `LpBe4/1000` | Bessel-Tiefpass 4. Ordnung |
| `LpCh4/-1/1000` | Chebyshev-Tiefpass 4. Ordnung, 1 dB Ripple |

Vollständige Spezifikationssyntax: `doc/reference/fidlib.txt`.

---

## Bekannte Einschränkungen

- **fidlib ruft `exit()` auf** bei vollständig unbekannten Filtertyp-Strings
  (z.B. `Xyz/1000`). Die C-API kann diesen Fall nicht abfangen.
- **SIMD** gilt nur für `-l c99`; andere Sprachen ignorieren `--simd`.
- **Verilog-Testbench** wird noch nicht generiert (Phase 4, offen).
- **FIR-Filter** werden noch nicht unterstützt (Phase 4, offen).
