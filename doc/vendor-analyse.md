# Vendor-Analyse: Bestandsaufnahme und cmake-Neuaufbau

Stand: 2026-05-26

---

## 1. Übersicht der Vendor-Komponenten

| Komponente | Pfad | Typ | Größe | Zustand |
|---|---|---|---|---|
| fidlib | `vendor/fidlib/` | Git-Submodul (JamesHight) | ~5 400 LOC | initialisiert, v0.9.11 |
| fiview | `vendor/fiview/` | Archiv-Kopie (uazu.net) | ~6 000 LOC | vollständig, v0.9.10 |
| mkfilter | `vendor/mkfilter/` | Git-Submodul (billthefarmer) | ~2 100 LOC | initialisiert |
| gmeteor | `vendor/gmeteor/` | Archiv (SourceForge) | 769 kB | vollständig, v0.95 (2013-01-06) |

---

## 2. fidlib (vendor/fidlib)

### Was es ist

C-Bibliothek für Laufzeit-Filterdesign und -ausführung. Kernstück des gesamten Stacks.
Ursprung: Jim Peters (uazu.net, 2002–2004). Dieser Fork: JamesHight/fidlib, v0.9.11
— konsolidiert alle Community-Patches (const-Korrektheit, `extern "C"` Guards, C++-Kompatibilität).

### Dateien

| Datei | Funktion |
|---|---|
| `fidlib.h` | Öffentliches API (77 Zeilen) |
| `fidlib.c` | Gesamte Implementierung (2 408 Zeilen) |
| `fidmkf.h` | mkfilter-abgeleitete Filtertypen — intern includiert |
| `fidrf_cmdlist.h` | Empfohlene Ausführungs-Engine (command-list) |
| `fidrf_combined.h` | Alternative Engine (flattened, weniger genau) |
| `fidrf_jit.h` | Veraltete JIT-Engine (x86-only, gewarnt) |
| `firun.c` | CLI-Test-Tool (GPL, optional) |

### Öffentliches API (fidlib.h)

```c
// Housekeeping
void     fid_set_error_handler(void (*rout)(char *));
char    *fid_version(void);
void     fid_list_filters(FILE *out);
int      fid_list_filters_buf(char *buf, char *bufend);

// Filter-Design
FidFilter *fid_design(const char *spec, double rate,
                      double freq0, double freq1,
                      int f_adj, char **descp);
double     fid_design_coef(double *coef, int n_coef, const char *spec,
                           double rate, double freq0, double freq1, int adj);
FidFilter *fid_parse(double rate, char **pp, FidFilter **ffp);
FidFilter *fid_cv_array(double *arr);
FidFilter *fid_cat(int freeme, ...);

// Filter-Analyse
double     fid_response(FidFilter *filt, double freq);
double     fid_response_pha(FidFilter *filt, double freq, double *phase);
int        fid_calc_delay(FidFilter *filt);
FidFilter *fid_flatten(FidFilter *filt);
void       fid_rewrite_spec(const char *spec, double freq0, double freq1,
                            int adj, char **spec1p, char **spec2p,
                            double *freq0p, double *freq1p, int *adjp);

// Filter-Ausführung (Echtzeit-Signalverarbeitung)
void *fid_run_new(FidFilter *filt, double (**funcpp)(void *, double));
void *fid_run_newbuf(void *run);
int   fid_run_bufsize(void *run);
void  fid_run_initbuf(void *run, void *buf);
void  fid_run_zapbuf(void *buf);
void  fid_run_freebuf(void *runbuf);
void  fid_run_free(void *run);
```

### Filterspezifikations-DSL (fispec)

```
LpBu4/100        Butterworth Lowpass, Ordnung 4, Eckfreq 100 Hz
HpBe6/0.1        Bessel Highpass, Ordnung 6, rel. Eckfreq 0.1
BpCh2/0.5/50-60  Chebyshev Bandpass, Ripple 0.5 dB, 50–60 Hz
BsRe/100/50      Resonator Bandstop, Q=100, 50 Hz
LsBq/0.7/-6/100  Lowshelving Biquad (Audio-EQ-Cookbook), -6 dB, 100 Hz
x                Serienschaltung (mehrere Filter hintereinander)
```

Über 47 vordefinierte Filtertypen in drei Klassen:
- **mkfilter-basiert:** Bessel, Butterworth, Chebyshev (beliebige Ordnung) + Resonatoren
- **Audio-EQ-Cookbook:** Biquad-Varianten (LpBq, HpBq, BpBq, PkBq, LsBq, HsBq, ...)
- **FIR-Fenster:** Blackman, Hamming, Hann, Bartlett Lowpass

### Aktuelles Build-System

Autotools (Autoconf + Automake + Libtool).
Erzeugt: `libfidlib.so` / `libfidlib.a`, optional `firun`.
Abhängigkeit: nur `-lm`.

### Lizenz

`fidlib.h` / `fidlib.c` / `fidmkf.h`: **GNU LGPL v2.1**
`firun.c`: **GNU GPL v2**

---

## 3. fiview (vendor/fiview)

### Was es ist

Interaktives SDL-GUI-Tool zur Filterentwicklung und -visualisierung.
Zweck: Frequenzgang + Impulsantwort grafisch sehen, Filter-Parameter interaktiv
justieren, C-Code für den gefundenen Filter exportieren.

### Dateien

| Datei | Funktion |
|---|---|
| `src/fiview.c` | Hauptprogramm, SDL-Loop, Datei-Ausgabe (881 Zeilen) |
| `src/filter.c` | Filter-Laden, Analyse (Frequenz-/Impulsgang), Zeitkonstanten (2 207 Zeilen) |
| `src/display.c` | Layout + Rendering der Anzeige-Bereiche (1 132 Zeilen) |
| `src/graphics.c` | SDL-Grafik-Abstraktionen, Pixel-Handling 16/32 bpp (1 024 Zeilen) |
| `src/helptext.c` | Eingebetteter Hilfetext + dynamische Filterliste (593 Zeilen) |
| `src/scratch.c` | Scratch-Buffer-Verwaltung mit Auto-Realloc (256 Zeilen) |
| `src/fidlib/` | Eingebettete ältere fidlib-Kopie (2 304 Zeilen) |
| `src/mk` | Shell-Skript-Buildsystem (kein Makefile, kein cmake) |

### Abhängigkeiten

- **SDL 1.2** (Grafik + Events)
- **libm**
- Keine FFTW, keine GTK, keine X11 direkt

### Eingebettete fidlib vs. vendor/fidlib

Die in fiview enthaltene `src/fidlib/`-Kopie ist **älter** als `vendor/fidlib`:
- Ohne `extern "C"` Guards
- Ohne const-Korrektheit
- Ohne Autotools-Build

fiview wurde zu seiner Zeit das Referenz-Frontend für fidlib — beide sind inzwischen getrennte Projekte.

### Ausgaben

`fiview.log`: Vollständig kommentierter C-Code für den angezeigten Filter (3 Versionen: lesbar,
compiler-optimiert, mit `fid_design_coef`). Auch Frequenzgang-Analyse, Zeitkonstanten.
`fiview.coef`: Rohe IIR/FIR-Koeffizienten.

Beispiel-Log liegt unter `doc/examples/fiview_log.txt`.

### Lizenz

**GNU GPL v2**

---

## 4. mkfilter (vendor/mkfilter)

### Was es ist

Akademisches Filter-Design-Tool von Dr. A.J. Fisher (Univ. York, 1992).
Berechnet Pol-/Nullstellen klassischer IIR-Filter (Butterworth, Bessel, Chebyshev)
nach der S-Plane-Theorie und transformiert via bilinearer Transform (BLT) oder
Matched Z-Transform (MZT) in den z-Bereich.

### Dateien

| Datei | Funktion |
|---|---|
| `mkfilter.C` | CLI: Pol-Berechnung, BLT/MZT, Differenzengleichung, Ausgabe (699 Zeilen) |
| `mkfilter.h` | Typedefs, Konstanten, Inline-Utils |
| `complex.C/.h` | Komplexe Arithmetik (Operatoren, sqrt, exp(jθ), Polynomauswertung) |
| `gencode.C` | Filter → C-Code-Generator (liest mkfilter-l-Ausgabe) |
| `genplot.C` | Filter → PNG-Graph via libgd |
| `mkshape.C` | FIR-Designer: Raised-Cosine, Root-Raised-Cosine, Hilbert |
| `mkaverage.C` | Moving-Average FIR |
| `readdata.C` | Hilfsfunktion: Parser für mkfilter-`-l`-Ausgabe |

### CLI-Schnittstelle (Kurzform)

```bash
mkfilter -Bu -Lp -o 4 -a 0.2          # Butterworth Lowpass, Ordnung 4, α=0.2
mkfilter -Ch 0.5 -Bp -o 2 -a 0.1 0.2  # Chebyshev Bandpass, Ripple 0.5 dB
mkfilter -Re 10 -Bp -a 0.05            # Resonator, Q=10, α=0.05

mkfilter -Bu -Lp -o 4 -a 0.2 -l | gencode -ansic   # → C-Code
mkfilter -Bu -Lp -o 4 -a 0.2 -l | genplot freq.png  # → PNG-Frequenzgang
```

### Verhältnis zu fidlib

**Überlappung:** `fidmkf.h` in fidlib implementiert dieselben Pol-Berechnungen —
Butterworth, Bessel, Chebyshev für beliebige Ordnung. Die Mathematik ist identisch.

**Funktion im Projekt:** Referenz und Validierung. Wenn fidlib einen Filter berechnet,
kann mkfilter zur Überprüfung herangezogen werden (Pole, Differenzengleichung).

### Aktuelles Build-System

GNU Make + gcc/g++ (`-std=gnu++98`, `-fpermissive`).
`genplot` benötigt **libgd** (PNG-Ausgabe).

### Lizenz

Keine explizite Lizenzangabe. Akademisch freigegeben, seit Jahrzehnten auf GitHub verbreitet.

---

## 5. gmeteor (vendor/gmeteor)

### Was es ist

FIR-Filter-Designer für Equiripple-Filter nach beliebiger Frequenzgangs-Maske,
basierend auf Parks-McClellan / Remez-Exchange-Algorithmus.
Scheme/Guile-basiert. Entwickelt ca. 2005–2013 auf SourceForge.

### Zustand

Vollständiger Quellcode in `vendor/gmeteor/gmeteor-0.95.tar.gz` (769 kB).
Direkt von SourceForge nachgezogen — die ursprünglich gesicherte Datei war eine
404-HTML-Seite (falscher Download-URL). Das Projekt ist inaktiv seit 2013,
der Download läuft aber noch über `https://sourceforge.net/projects/gmeteor/files/`.

### Algorithmus

Nicht Parks-McClellan/Remez, sondern **METEOR** (Steiglitz, Parks, Kaiser — IEEE Trans.
Signal Processing, 1992): Reduktion des FIR-Designs auf ein lineares Programm,
gelöst via Simplex. Ergebnis ebenfalls equiripple, aber allgemeiner als klassischer
Remez: beliebige Frequenzgangs-Masken, auch analytisch via Scheme spezifizierbar.

### Build-System

Autotools (configure.ac, Makefile.am).

### Quelldateien (aus Tarball)

| Datei | Funktion |
|---|---|
| `gmeteor.c` | Haupt-C-Code |
| `simplex.c/.h` | Simplex-Algorithmus (LP-Kern) |
| `lpp*.c` (12 Dateien) | LP-Löser, Fortran-zu-C-Konvertierung |
| `f2c.h` | Fortran-zu-C-Kompatibilitäts-Header |
| `gmeteor-core.scm` | Guile/Scheme-Kern |
| `gmeteor-lib.scm` | Hilfsbibliothek |
| `gmeteor-simple.scm` | Vereinfachte Schnittstelle |
| `gmeteor-getopt.scm` | CLI-Optionsverarbeitung |
| `gmeteor.in` | Skript-Template |
| `examples/*.scm` | 7 Beispiel-Spezifikationen |
| `doc/gmeteor.pdf` | Vollständige Dokumentation |

### Abhängigkeiten

- **libguile** (GNU Scheme-Interpreter)
- **libm**

### Lizenz

**GNU GPL**

### Funktion im Projekt

Schließt die Lücke die fidlib bei Equiripple-FIR-Design hat: fidlib hat keine
METEOR/Remez-Optimierung. gmeteor erlaubt beliebige Frequenzgangs-Masken.

---

## 6. Architektur-Übersicht: Wie die Teile zusammenhängen

```
mkfilter                    gmeteor
  │                              │
  │  Pol-/Nullstellen-           │  METEOR-Algorithmus
  │  Berechnung (Referenz)       │  FIR-Equiripple (beliebige Maske)
  │                              │
  │  Pol-/Nullstellen-           │  METEOR-Algorithmus
  │  Berechnung (Referenz)       │  FIR-Equiripple (beliebige Maske)
  │                              │
  └──────────────┬───────────────┘
                 │
              fidlib
         (Kernbibliothek)
         ┌─────────────────────────────────────────┐
         │  fid_design("LpBu4/100", 44100, ...)    │
         │  → FidFilter* (interne Darstellung)     │
         │  fid_run_new(filt, &funcp)              │
         │  → sample = funcp(buf, input)           │
         └─────────────────────────────────────────┘
                 │
              fiview
         (Referenz-Frontend)
         ┌─────────────────────────────────────────┐
         │  GUI: Frequenzgang + Impulsantwort       │
         │  Export: fiview.log (C-Code)             │
         │  Export: fiview.coef (Koeffizienten)    │
         └─────────────────────────────────────────┘
```

---

## 7. Was das neue cmake-Projekt werden kann

### Leitgedanke

fidlib und fiview sind zwei Seiten einer Sache: fidlib ist die Maschine,
fiview war das Werkzeug zum Beobachten. Das neue Projekt ersetzt beides
durch ein sauber aufgebautes cmake-Projekt — ohne GUI-Abhängigkeit,
ohne embedded-fidlib-Kopie, mit modernem C99 und einer testbaren Library.

### Zielarchitektur

```
digitalfilterdesign/
├── CMakeLists.txt              ← Root-cmake
├── lib/
│   ├── CMakeLists.txt
│   ├── fidlib.c                ← C99-bereinigt, aus vendor/fidlib übernommen
│   ├── fidlib.h                ← öffentliches API, mit extern "C" Guards
│   ├── fidmkf.h                ← intern (mkfilter-Kern)
│   ├── fidrf_cmdlist.h         ← intern (Ausführungs-Engine)
│   └── fidrf_combined.h        ← intern (alternative Engine)
├── cli/
│   ├── CMakeLists.txt
│   └── firun.c                 ← CLI-Tool (aus vendor/fidlib übernommen)
└── tests/
    ├── CMakeLists.txt
    └── test_butterworth.c      ← Impulsantwort-Validierung gegen fiview_log.txt
```

### Was aus vendor/ übernommen wird

| Vendor-Quelle | Übernommen als | Begründung |
|---|---|---|
| `vendor/fidlib/fidlib.c` | `lib/fidlib.c` | Kern der Bibliothek |
| `vendor/fidlib/fidlib.h` | `lib/fidlib.h` | Öffentliches API |
| `vendor/fidlib/fidmkf.h` | `lib/fidmkf.h` | Filtertypen-Implementierung |
| `vendor/fidlib/fidrf_cmdlist.h` | `lib/fidrf_cmdlist.h` | Ausführungs-Engine |
| `vendor/fidlib/fidrf_combined.h` | `lib/fidrf_combined.h` | Alternative Engine |
| `vendor/fidlib/firun.c` | `cli/firun.c` | Referenz-CLI-Tool |
| `vendor/mkfilter/` | — | Nur Referenz/Validierung, kein Code-Transfer |
| `vendor/fiview/` | — | Nur Referenz (Algorithmen bereits in fidlib) |
| `vendor/gmeteor/` | — | Tarball vorhanden, noch nicht integriert — für späteren Schritt |

### Was nicht übernommen wird

- `vendor/fiview/src/fidlib/` — veraltete embedded-Kopie, durch `lib/fidlib.c` ersetzt
- `vendor/fiview/src/*.c` — SDL-GUI, kein Mehrwert für Library-Projekt
- `fidrf_jit.h` — veraltet, x86-only, mit Fehlerhinweis im Code
- `vendor/mkfilter/genplot.C` — libgd-Abhängigkeit, kein Mehrwert

### cmake-Targets

| Target | Typ | Beschreibung |
|---|---|---|
| `fidlib` | STATIC/SHARED Library | Kernbibliothek, öffentlicher Header fidlib.h |
| `firun` | Executable | CLI-Werkzeug, optional (`-DBUILD_TOOLS=ON`) |
| `test_butterworth` | Test | Impulsantwort Butterworth LP 4. Ordnung gegen Referenz |

### Compiler-Flags (aus CLAUDE.md)

```cmake
target_compile_options(fidlib PRIVATE
  -Wall -Wextra -Wconversion -Wshadow -Werror
)
# Für Debug + Tests:
target_compile_options(fidlib PRIVATE
  -fsanitize=address,undefined
)
```

### Wenn vendor/ verzichtbar sein soll

vendor/ kann wegfallen sobald:

1. `lib/fidlib.c` aus `vendor/fidlib/fidlib.c` übernommen und C99-bereinigt ist
   (Sanitizer-clean, keine VLAs im API, keine UB)
2. `cli/firun.c` aus `vendor/fidlib/firun.c` übernommen ist
3. Ein Smoke-Test (Butterworth LP 4. Ordnung, Impulsantwort) gegen die
   Referenz in `doc/examples/fiview_log.txt` besteht
4. cmake korrekt konfiguriert ist (FetchContent-kompatibel)

Dann sind die Git-Submodule `vendor/fidlib` und `vendor/mkfilter` nur noch
Archiv/Referenz — keine Build-Abhängigkeit.

---

## 8. Offene Punkte

| Thema | Status | Handlungsbedarf |
|---|---|---|
| gmeteor Quellcode | vorhanden (`vendor/gmeteor/gmeteor-0.95.tar.gz`) | cmake-Integration für späteren Schritt |
| fiview SDL-GUI | Referenz, nicht integriert | kein Handlungsbedarf solange `fiview_log.txt` als Testorakel reicht |
| fidrf_jit.h | veraltet, deaktiviert | weglassen — nicht in cmake aufnehmen |
| firun Lizenz | GPL v2 (nicht LGPL) | firun als separates optionales Target klar trennen |
| mkfilter libgd | nur für genplot nötig | genplot nicht aufnehmen, falls kein PNG-Bedarf |
| Parks-McClellan FIR | kein Ersatz für gmeteor | für einen späteren Schritt offen lassen |
