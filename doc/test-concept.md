# Testkonzept: digitalfilterdesign

**Projektpfad:** `digitalfilterdesign`  
**Branch:** `feature/test-coverage`  
**Stand:** 2026-05-27

---

## 1. Teststrategie — Übersicht

Das Projekt gliedert sich in drei unabhängig testbare Komponenten:

| Komponente | Testbare Anteile | Ausschluss |
|---|---|---|
| **fidlib** | API vollständig | — |
| **firun** | Parsing, Format-Dekodierung, Pipeline | Binary-I/O auf echten FDs nur per SIL |
| **fiview** | scratch, filter, display-Logik | SDL-Rendercode |

Teststufen:
1. **Unit** — isolierte Funktion, keine externen Deps
2. **Component** — Modul-intern, ggf. Mock für error-Handler / File-I/O
3. **SIL (Software-in-the-Loop)** — firun als Blackbox, Referenzdaten verglichen
4. **Numerisch-mathematisch** — Filter gegen analytische Sollwerte
5. **Regression** — goldene Referenz aus fiview_log.txt / CSV-Exports

Alle Tests laufen unter CTest via `ctest --test-dir build --output-on-failure`.  
Debug-Builds nutzen `-fsanitize=address,undefined` — Tests müssen sanitizer-clean sein.

---

## 2. Numerische Filter-Testbarkeit

IIR- und FIR-Filter sind vollständig durch ihre Übertragungsfunktion H(z) bestimmt.
Jede ihrer Eigenschaften ist analytisch vorhersagbar und eignet sich als Pass/Fail-Kriterium.

### 2.1 Prüfbare Kenngrößen

| Kenngröße | Methode | Erwartungswert |
|---|---|---|
| DC-Verstärkung (f=0) | `fid_response(ff, 0.0)` | 1.0 (Lowpass), 0.0 (Highpass) |
| Nyquist-Verstärkung | `fid_response(ff, 0.5)` | 0.0 (Lowpass), 1.0 (Highpass) |
| -3 dB Eckfrequenz | Suche per Bisektion | fc ± 1 % |
| -6 dB Grenze | Suche per Bisektion | f6 ± 1 % |
| Sperrband-Dämpfung | max `fid_response` im Sperrband | < -40 dB (Butterworth Ord.4) |
| Passband-Welligkeit | max/min `fid_response` im Passband | < 0.1 dB |
| Gruppenlatenz | Impulsantwort: Schwerpunkt | samples ≈ N/2 (FIR) |
| Impulsantwort-Abfall | letzte signifikante Amplitude | < 1e-6 nach cnt_max samples |
| Phase bei fc | `fid_response_pha(ff, fc)` | -N·45° (Butterworth Ord.N) |

### 2.2 Toleranzband-Philosophie

Floating-point DSP hat endliche Genauigkeit. Toleranzen:
- Frequenzangaben: ± 0.5 % der Sollfrequenz
- Amplituden in Passband: ± 0.01 (linear), entspricht ~ 0.09 dB
- Amplituden in Sperrband: absolutwert < 0.003 (≈ -50 dB) für BuN≥4
- Phasenwinkel: ± 1°

### 2.3 Referenz-Testfall (bereits implementiert)

`tests/test_butterworth.c`: LpBu6 bei 44100 Hz / fc=400 Hz

```
DC-Gain:        ~1.000  (tolerance ±0.001)
Gain bei fc:    ~0.707  (tolerance ±0.005)
Gain bei 10·fc: <0.005
```

Dieser Test bildet das Muster für alle weiteren Filtertyp-Tests.

---

## 3. fidlib — Unit-Tests

### 3.1 API-Abdeckung

Alle öffentlichen Funktionen aus `fidlib/fidlib.h`:

#### fid_design / fid_design_coef

```
Testfälle:
  LpBu2/1000    @ sr=44100  → Typ 0 (low-pass), 2 Biquad-Sektionen
  HpBu4/5000    @ sr=44100  → Typ 1 (high-pass)
  BpBu2/1000-2000 @ sr=44100 → Typ 2 (band-pass), n_poles=4
  BsRa4/1000-2000 → Typ 3 (band-stop / Notch via Resonanz)
  ApBu2/1000    → Typ 4 (all-pass), Phasengang
  LpBe6/100     → Bessel Lowpass (flache Gruppenlatenz)
  LpCh2/1000/3dB → Chebyshev, 3dB-Welligkeit
  LpCh4/1000/0.5dB → Chebyshev, 0.5dB-Welligkeit

Für jeden: prüfe
  - fid_response(ff, 0.0)   → Passband-Erwartung
  - fid_response(ff, fc/sr) → ~0.707 bei Butterworth
  - fid_response(ff, 10·fc/sr) → Sperrbandwert
  - fid_response_pha(ff, fc/sr) → Phasenerwartung
  - Korrekte FidFilter-Struktur: info[0].type, info[0].len
```

#### fid_flatten / fid_cat

```
Testfälle:
  flatten(ff1) ∘ flatten(ff2) == flatten(cat(ff1, ff2))
  Leerer Filter: cat(NULL, ff) == ff
  Reihenfilter: cat(LpBu2, HpBu2) → Bandpass-ähnliche Response
```

#### fid_run_* Lifecycle

```
Testfälle:
  newbuf  → bufsize stimmt mit fid_run_bufsize überein
  initbuf → Buffer ist genullt
  zapbuf  → Reset-Verhalten: Impulsantwort von neuem identisch
  freebuf → kein Leak (prüfbar mit ASan)
  free    → kein Leak

  Sonderfall fid_run_newbuf_inplace (RT-safe):
    Vorab allokierter Buffer, Mindestgröße = fid_run_bufsize
    Ergebnis identisch zu fid_run_newbuf
```

#### fid_cv_array

```
  fid_design_coef(...) → double*-Array
  Werte stimmen mit FidFilter-Koeffizienten überein (float64 round-trip)
```

#### fid_parse / fid_rewrite_spec

```
  "BpBu2/1000-2000" → korrekte Tokenisierung
  "LpBu4/400" @ sr=44100 → gleiche Response wie fid_design
  fid_rewrite_spec: Normalisierung prüfen (z.B. Leerzeichen, Großschreibung)
  Ungültige Spec: fid_set_error_handler fängt den Fehler ab (mock test)
```

#### fid_set_error_handler

```
  Normaler Fehler: custom handler aufgerufen, nicht abort()
  NULL-Handler: default-Verhalten (abort) — NUR prüfen ob aufrufbar, kein
                tatsächlicher Absturz im Test
  test_butterworth.c zeigt bereits das Muster: longjmp aus Error-Handler
```

#### fid_list_filters / fid_version

```
  fid_list_filters: Rückgabepuffer nicht-NULL, mindestens "Butterworth" drin
  fid_version: Nicht-leerer String
```

#### fid_calc_delay

```
  LpBu6/400: delay <= cnt_max aus filter.c-Analyse
  Verhältnis delay(BuN) ~ N·sr/fc
```

### 3.2 Grenzfälle (Boundary / Fehler)

```
  Ordnung 0:   fid_design schlägt fehl → error_handler
  fc = 0:      fid_design schlägt fehl
  fc > sr/2:   fid_design schlägt fehl (Nyquist)
  sr = 0:      fid_design schlägt fehl
  Bandpass mit f0 > f1: Fehler
  Extrem hohe Ordnung (32): keine UB, kein Overflow (ASan)
```

---

## 4. filter.c — Component-Tests (fiview-intern)

fiview/src/filter.c enthält Analyse-Logik die kein SDL benötigt.

### 4.1 filter_load_immed / filter_load_file

```
  filter_load_immed("LpBu4/400"):
    - n_filt == 1
    - curr->typ == 0
    - curr->filt->filt != NULL

  filter_load_immed("LpBu4/400,HpBu4/5000"):
    - n_filt == 2 (zwei SubFilt)

  filter_load_file("tests/fixtures/simple.filt"):
    - Datei mit "LpBu2/1000" → gleich wie immed

  Fehlerfall: filter_load_immed("") → error_handler (mock)
  Fehlerfall: filter_load_file("/nonexistent") → return 0
```

### 4.2 filter_response / filter_resp_range

```
  LpBu4/400:
    filter_response(ff, 0.0, NULL)       == 1.0 ± 0.001
    filter_response(ff, 400.0/44100, NULL) ~= 0.707 ± 0.01
    filter_response(ff, 4000.0/44100, NULL) < 0.005

  Mit Phase:
    filter_response(ff, 400.0/44100, &phase)
    phase ~= -N*45° ± 2° (Butterworth)

  filter_resp_range(ff, 0.0, 0.5, 512, 4):
    - Rückgabe-Array länge 512
    - Maximum bei 0.0 ist ~1.0
    - Werte fallen monoton bis Nyquist
```

### 4.3 filter_setup_gain / filter_setup_cnt

```
  filter_setup_gain:
    gain100 > 0
    typ korrekt erkannt (0=LPF, 1=HPF, 2=BPF, 3=BSF, 4=APF)
    m3db[0] ~= fc ± 1%
    m6db vorhanden für alle Standard-Typen

  filter_setup_cnt:
    cnt50 < cnt90 < cnt95 < cnt99 < cnt999 < cnt_max
    cnt_max < 50*sr/fc (Filter klingt ab in endlicher Zeit)
```

### 4.4 filter_dump

```
  LpBu4/400: dump != NULL
  Enthält "Butterworth", "low-pass", "400"
  kein Crash bei mehrstufigem Filter
```

### 4.5 filter_run / runfilter_step

```
  Impulsantwort LpBu4/400:
    Schritt 1: runfilter_step(rr, 1.0)
    Schritte 2..N: runfilter_step(rr, 0.0)
    Summe aller Samples ~= 1.0 (gain 1.0 * sr/fc ... proportional)
    Wert nach cnt_max Schritten < 1e-5

  Sprungantwort:
    Alle Schritte: runfilter_step(rr, 1.0)
    Wert konvergiert gegen 1.0 ± 0.001 nach cnt99 Schritten

  zapbuf-Äquivalenz:
    runfilter_free → new run → gleiche Antwort
```

---

## 5. scratch.c — Unit-Tests

scratch ist zustandsbehaftet aber vollständig isolierbar (globaler Buffer).

### 5.1 Grundoperationen

```
  scr_zap():         scr_len == 0, scratch[0] == '\0'
  scr_pr("hello"):   scr_len == 5, strcmp(scratch, "hello") == 0
  scr_lf():          scratch[scr_len-1] == '\n'
  SCR_PUTC('x'):     korrekte Terminierung
```

### 5.2 scr_vpr / scr_pr Format

```
  Numerisch: scr_pr("%d", 42)      → "42"
  Float:     scr_pr("%.3f", 3.14)  → "3.140"
  Concat:    scr_pr("a"); scr_pr("b") → "ab"
```

### 5.3 scr_prw — Word-Wrap

```
  scr_wrap(20, "  "):
  scr_prw("dies ist ein langer Text der umgebrochen werden muss")
  → Jede Zeile <= 20 Zeichen
  → Folgezeilen beginnen mit "  "
  → Keine Wörter abgeschnitten

  Sonderfall: Wort länger als Zeilenbreite → kein Loop, keine UB
  Sonderfall: leerer String → kein Crash
```

### 5.4 scr_inc / scr_wrD / scr_wrI

```
  scr_inc(16): Zeiger != NULL, 16 Bytes genullt
  scr_wrD(3.14): memcpy-Round-trip korrekt
  scr_wrI(0xDEAD): memcpy-Round-trip korrekt
```

### 5.5 scr_realloc — Kapazitätswachstum

```
  32768 bytes schreiben → kein Crash, kein Overflow
  64001 bytes schreiben → scr_max >= 65536
  ASan: kein Heap-Overflow an Grenze
```

### 5.6 scr_dup

```
  scr_zap_pr("test"):
  char *s = scr_dup()
  strcmp(s, "test") == 0
  s != scratch  (unabhängige Kopie)
  free(s) → kein Leak
```

---

## 6. firun — Tests

### 6.1 Unit: decode_spec / spec_count

```
  decode_spec("a")  → FORMAT_ASCII
  decode_spec("d")  → FORMAT_DOUBLE
  decode_spec("f")  → FORMAT_FLOAT
  decode_spec("w")  → FORMAT_INT16_LE
  decode_spec("W")  → FORMAT_INT16_BE
  decode_spec("s")  → FORMAT_INT16_LE (synonym)
  decode_spec("b")  → FORMAT_INT8
  decode_spec("")   → Fehler / default
  decode_spec("z")  → Fehler (unbekannt)

  spec_count("a,b,c") → 3
  spec_count("LpBu4/400") → 1
  spec_count("LpBu2/1000,HpBu2/5000") → 2
```

### 6.2 Unit: parse_filters

```
  "LpBu4/400" @ sr=44100:
    filt_count == 1
    filt[0] != NULL

  "LpBu2/1000,HpBu2/5000":
    filt_count == 2

  Fehlerfall: "" → Fehler
```

### 6.3 SIL: firun als Blackbox (Prozess-Tests)

SIL-Tests rufen `firun` über `popen`/`subprocess` auf und prüfen stdout gegen Referenzdaten.

#### Impulse response (Format ASCII)

```sh
echo "" | ./firun -r LpBu4/400 -s 44100
```

Erwartung: Frequenzgang-Zeilen, `0.000000 1.0` bei f=0.

```sh
./firun -I 44100 LpBu4/400 -o a
```

Erwartung: Impuls-Antwort als ASCII, erste Zeile ~ 1.0, klingt ab nach cnt_max samples.

#### Frequency response (Format ASCII)

```sh
./firun -r LpBu4/400 -s 44100 -n 100
```

Zeilen: `freq response [phase]`.  
Prüfe: Zeile bei f≈0 → response ≈ 1.0; Zeile bei f≈fc → response ≈ 0.707; Zeile bei f≈10fc → response < 0.01.

#### Format-Round-Trip (Binary)

```sh
./firun -I 44100 LpBu4/400 -o d | ./firun -I 44100 LpBu4/400 -i d -o a
```

ASCII-Ausgabe muss mit direkter ASCII-Ausgabe übereinstimmen (float64-Round-trip ist verlustfrei).

#### Mehrkanaliger Betrieb

```sh
./firun -n 2 -I 44100 LpBu4/400 -o a
```

Ausgabe hat doppelt so viele Spalten wie Einkanalversion.

#### Streaming-Modus

```sh
dd if=/dev/zero bs=4 count=1 | ./firun -s 44100 LpBu4/400 -i f -o f | wc -c
```

Ausgabe: 4 Bytes (genau ein float32 zurück).

### 6.4 Regression: Referenzdaten aus fiview_log.txt

`doc/examples/fiview_log.txt` enthält eine vollständige Butterworth-LP-Analyse.
Extrakte daraus als goldene Referenz:

```
tests/fixtures/LpBu6_400_44100_response.csv   ← Frequenzgang
tests/fixtures/LpBu6_400_44100_impulse.csv    ← Impulsantwort (cnt99 samples)
```

Test: firun-Ausgabe gegen diese CSVs verglichen (maximale Abweichung < 1e-5).

---

## 7. Mock-Strategien

### 7.1 Error-Handler-Mock (fidlib)

fidlib ruft `abort()` per default bei Fehler. Für Tests:

```c
static int error_triggered;
static void mock_error(const char *msg) {
    error_triggered = 1;
    longjmp(error_jmp, 1);
}
// Im Test:
fid_set_error_handler(mock_error);
if (setjmp(error_jmp) == 0) {
    fid_design(0, 44100.0, 0, 0.0, 0.0, "INVALID");
    assert(0); // should not reach
}
assert(error_triggered);
```

Dieses Muster ist bereits in `test_butterworth.c` etabliert.

### 7.2 File-I/O-Mock (filter_load_file)

`filter_load_file` öffnet eine Datei per `fopen`. Tests nutzen temporäre Dateien:

```c
// Fixture schreiben:
FILE *f = fopen("/tmp/test_filter.filt", "w");
fputs("LpBu4/400\n", f);
fclose(f);
// Test:
assert(filter_load_file("/tmp/test_filter.filt") == 1);
```

Keine Interposition nötig — echte I/O mit tmp-Dateien ist stabiler als dlsym-Mocks.

### 7.3 Scratch-Buffer-Isolation

Da `scratch` global ist, muss jeder Test `scr_zap()` vorher aufrufen. CTest-Tests
sind sowieso separate Prozesse — kein cross-test-Zustand möglich.

### 7.4 firun stdin/stdout-Mock

firun liest `stdin` und schreibt `stdout`. SIL-Tests via Pipes ohne echten Mock:

```c
// popen-basierter Test (POSIX):
FILE *p = popen("./firun -I 44100 LpBu4/400 -o a", "r");
// parse output
pclose(p);
```

Alternativ: firun in subprocess mit `pipe()/fork()/exec()` für Timing-Kontrolle.

---

## 8. display.c / Nicht-SDL-Logik

fiview/src/display.c enthält neben SDL-Rendercode auch reine Daten-Logik:

### 8.1 setup_pager / progress_init (state-only)

```
  setup_pager("text", "info", 0):
    s_pager_txt == "text"
    s_pager_inf == "info"
    s_pager_typ == 0
    s_pager_cnt == 0

  progress_init(&pr, 100, "loading", 40):
    pr.max == 100
    pr.txt == "loading"
    pr.wid == 40
    pr.cur == 0
```

Diese Funktionen brauchen kein SDL wenn der Renderpfad über ein Flag abstrahiert wird
(z.B. `#ifdef TEST_NO_SDL`). Voraussetzung: Refactoring des display.c-Initialisierungspfads.

### 8.2 inRect

```
  Rect r = {10, 10, 100, 100};
  inRect(&r, 50, 50) == 1
  inRect(&r, 5, 5)   == 0
  inRect(&r, 10, 10) == 1  (Grenzpunkt)
  inRect(&r, 110, 110) == 0
```

Diese Funktion ist pure (kein Seiteneffekt), direkt testbar.

---

## 9. CTest-Infrastruktur

### 9.1 Verzeichnisstruktur (Ziel)

```
tests/
  CMakeLists.txt
  test_butterworth.c          ← vorhanden
  test_fidlib_api.c           ← neu: vollständige API-Abdeckung
  test_scratch.c              ← neu
  test_filter_analysis.c      ← neu: filter_response, setup_gain, setup_cnt, dump
  test_filter_load.c          ← neu: load_immed, load_file
  test_firun_sil.c            ← neu: popen-basierte SIL-Tests
  test_display_logic.c        ← neu: inRect, pager/progress state (wenn #ifdef)
  fixtures/
    simple.filt
    LpBu6_400_44100_response.csv
    LpBu6_400_44100_impulse.csv
```

### 9.2 CMakeLists.txt — Erweiterung

```cmake
add_executable(test_fidlib_api     test_fidlib_api.c)
target_link_libraries(test_fidlib_api PRIVATE fidlib)
target_compile_options(test_fidlib_api PRIVATE -fsanitize=address,undefined)
target_link_options(test_fidlib_api PRIVATE -fsanitize=address,undefined)
add_test(NAME fidlib_api COMMAND test_fidlib_api)

add_executable(test_scratch        test_scratch.c
    ${CMAKE_SOURCE_DIR}/fiview/src/scratch.c)
target_include_directories(test_scratch PRIVATE
    ${CMAKE_SOURCE_DIR}/fiview/src
    ${CMAKE_SOURCE_DIR}/vendor/fidlib)
add_test(NAME scratch COMMAND test_scratch)

# ... analog für weitere Targets
```

### 9.3 Sanitizer-Baseline

Alle Tests laufen im Debug-Build mit:
```
-fsanitize=address,undefined
-fno-omit-frame-pointer
```

Kein Test darf einen ASan/UBSan-Report erzeugen — das ist Pflicht-Gate für CI.

### 9.4 CTest Labels

```cmake
set_tests_properties(fidlib_api    PROPERTIES LABELS "unit;fidlib")
set_tests_properties(scratch       PROPERTIES LABELS "unit;fiview")
set_tests_properties(filter_analysis PROPERTIES LABELS "unit;fiview;numerical")
set_tests_properties(firun_sil     PROPERTIES LABELS "sil;integration")
```

Ausführung nach Label:
```sh
ctest --test-dir build -L unit
ctest --test-dir build -L sil
```

---

## 10. Testpyramide und Priorisierung

```
        [ Regression / SIL ]   ← firun_sil, fiview_log golden ref
       [  Component / Integration ]  ← filter_analysis, filter_load
      [   Unit / Numerical  ]   ← fidlib_api, scratch, display_logic
```

Implementierungsreihenfolge nach Risiko:

| Priorität | Test | Wert |
|---|---|---|
| 1 (jetzt) | `test_fidlib_api.c` | Fundament — alle anderen bauen darauf |
| 2 | `test_filter_analysis.c` | Numerische Korrektheit der Analysefunktionen |
| 3 | `test_scratch.c` | Einfach, hohes Sicherheitsnetz |
| 4 | `test_filter_load.c` | I/O-Pfade, Fehlerbehandlung |
| 5 | `test_firun_sil.c` | Pipeline-Volltest, braucht firun-Build |
| 6 | Fixture-CSVs | Regression-Basis aus fiview_log.txt |
| 7 | `test_display_logic.c` | Nur nach #ifdef-Refactoring sinnvoll |

---

## 11. Was explizit NICHT getestet wird

- SDL-Rendercode (Fenster, Pixel, Events) — kein headless-SDL in CI-Umgebung
- fiview-Hauptschleife (event loop) — zu viel SDL-Kopplung
- Plattformspezifika (MSVC/Windows-Pfade in fidlib) — out of scope für RasPi-Target

---

## Anhang A: Bekannte Referenzwerte

Aus `doc/examples/fiview_log.txt`, LpBu6 @ fc=400 Hz, sr=44100 Hz:

```
DC gain:             1.000000
Gain @ fc:          ~0.707107   (-3.01 dB)
Gain @ 10·fc (4000 Hz): ~3.2e-7  (-130 dB)
cnt50:               ~9
cnt90:               ~32
cnt99:               ~59
cnt_max:             ~130
```

Diese Werte sind Pflicht-Referenz für `test_filter_analysis.c` und SIL-Tests.

---

## Anhang B: Checkliste neue Filtertypen

Für jeden neuen zu testenden Filtertyp:

- [ ] DC-Gain korrekt (0.0 oder 1.0)
- [ ] Nyquist-Gain korrekt
- [ ] Eckfrequenz innerhalb ±1 % von fc
- [ ] Sperrbandwert unterhalb Spezifikation
- [ ] Impulsantwort klingt ab (< 1e-5 nach cnt_max)
- [ ] ASan/UBSan clean
- [ ] fid_run_zapbuf erzeugt identische Antwort beim zweiten Durchlauf
