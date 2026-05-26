# fidlib Fork-Analyse — Welche Basis nehmen wir?

Datum: 2026-05-26  
Kontext: Auswahl der Upstream-Basis für das Modernisierungsprojekt

---

## Kandidaten im Überblick

| Merkmal | [uazu/fidlib] | [JamesHight/fidlib] |
|---|---|---|
| Autor / Ursprung | Jim Peters (Originalautor) | JamesHight als Sammler mehrerer Patches |
| Commits | **2** | **18** |
| Letzte Aktivität | 26. Aug 2014 | 29. Aug 2014 |
| Build-System | Shell-Skripte (`mk-firun`, `mk-firun-mingw`) | **Autotools** (`configure.ac`, `Makefile.am`, `bootstrap.sh`) |
| Version | 0.9.10 | **0.9.11** |
| C++-Kompatibilität | nein | **ja** (`extern "C"` Guards) |
| `const`-Korrektheit | nein (`char *spec`) | **ja** (`const char *spec`) |
| Compiler-Warnungen | unbereinigt | **bereinigt** |
| pkg-config | nein | **ja** (`fidlib.pc.in`) |
| Sterne (GitHub) | 5 | **11** |
| Eigene Forks | 6 | 2 |
| Status laut README | **unmaintained** | unmaintained (implizit) |

[uazu/fidlib]: https://github.com/uazu/fidlib
[JamesHight/fidlib]: https://github.com/JamesHight/fidlib

---

## Was ist JamesHight/fidlib genau?

Es ist **kein klassischer Fork** von `uazu/fidlib` über den GitHub-Fork-Mechanismus — der Repo-Graph zeigt keine direkte Abstammung. JamesHight hat 2013 eine Kopie des Originalcodes importiert und dann systematisch Patches aus der Community zusammengeführt:

### Enthaltene Patches (Commit-Rekonstruktion)

```
2013-07  JamesHight    Lib import (Originalkopie von uazu.net)
2013-07  JamesHight    README-Korrekturen

2014-08  daschuer      "Johns initial Mixxx commit" — Mixxx-Integration
2014-08  daschuer      API: char *spec → const char *spec
2014-08  daschuer      Ersetze #ifdef MIXXX durch #ifdef __cplusplus
2014-08  daschuer      Bereinige discards-'const'-qualifier-Warnungen
2014-08  daschuer/     Windows/MinGW-Cross-Compile-Fixes
         ulatekh
2014-08  daschuer      -Wsign-compare-Warnungen behoben
2014-08  daschuer      Unused-Parameter-Warnungen behoben
2014-08  daschuer      Zero-size-Array entfernt
2014-08  daschuer      Debug-Modus mit extra Warnungen (mk_firun -d)

2014-08  kwhat         Autotools-Build-System (configure.ac, Makefile.am)
2014-08  kwhat         Bugfixes für Packaging
2014-08  kwhat         Unitialisiert-vor-Benutzung-Warnungen behoben
```

**Kernaussage:** JamesHight/fidlib ist die inoffizielle "Community Edition" von fidlib — es sammelt
alle praktisch erprobten Patches, die nie in das Original zurückgeflossen sind.

---

## Die anderen Forks

Alle übrigen Forks (`daschuer/fidlib`, `gdkar/fidlib`, `EEGKit/fidlib`, `wjcroft/fidlib-1`) haben
genau **2 Commits** und sind identisch mit dem uazu-Original. Keiner enthält eigene Weiterentwicklungen.
`EEGKit/fidlib` enthält sogar explizit den Hinweis:

> *CURRENTLY UNMAINTAINED — Consider daschuer/mixxxdj fork if this doesn't work for you.*

Der in uazu/fidlib empfohlene `daschuer/mixxxdj`-Branch ist genau der Satz Patches, der via
JamesHight/fidlib bereits konsolidiert vorliegt.

---

## API-Unterschied: der entscheidende Bruch

Der wichtigste inhaltliche Unterschied zwischen den beiden Kandidaten ist die const-Korrektheit:

```c
// uazu/fidlib — original
FidFilter *fid_design(char *spec, ...);
void fid_rewrite_spec(char *spec, ...);

// JamesHight/fidlib — gepatchte Version
FidFilter *fid_design(const char *spec, ...);
void fid_rewrite_spec(const char *spec, ...);
```

Wer Filterspezifikationen aus String-Literalen übergibt (der häufigste Anwendungsfall),
erhält mit dem Original einen Compiler-Fehler in C++ und eine Warnung in C. Die JamesHight-Version
ist hier API-kompatibel aber const-korrekt — kein Breaking Change für bestehende Nutzer.

---

## Empfehlung

**Wir bauen auf [JamesHight/fidlib] auf.**

Begründung:

1. **Konsolidiert die besten verfügbaren Patches** aus mehreren unabhängigen Quellen
   (Mixxx-Team, kwhat, ulatekh) — alles, was am Original fehlte, ist bereits gesammelt.

2. **Autotools-Basis** macht den Schritt zu CMake oder Meson trivial — es gibt bereits eine
   saubere `configure.ac` mit Versionierung, `pkg-config`-Support und Optionen.

3. **const-Korrektheit** ist eine Voraussetzung für sauberes C++17/C99 und muss sonst
   als erstes nachgebessert werden.

4. **C++-Guards** sind vorhanden — die Library kann schon heute in C++-Projekten ohne
   Name-Mangling-Probleme eingebunden werden.

5. Version **0.9.11** gegenüber 0.9.10 zeigt, dass die Patches inhaltlich als Patch-Release
   betrachtet wurden, nicht als Abspaltung.

Das uazu/fidlib-Repo dient als **Referenz für den mathematischen Kern** — insbesondere
`fidmkf.h` (mkfilter-Algorithmen) — aber nicht als Arbeitsbasis.

---

## Empfohlene nächste Schritte

1. `JamesHight/fidlib` als Git-Subtree oder direkten Import in `vendor/fidlib/` übernehmen.
2. Build-System von Autotools auf **CMake** (oder Meson) migrieren — `configure.ac` als
   Vorlage für alle Abhängigkeiten und Feature-Flags.
3. `fidlib.h` / `fidlib.c` auf **C99** bereinigen (VLAs, `//`-Kommentare bereits ok,
   `__attribute__` portabel wrappen).
4. Ersten Smoke-Test schreiben: Butterworth LP 4. Ordnung, Impulsantwort gegen Referenzwerte
   aus fiview prüfen.
