# Zu sichernde Quellen — Bestandsaufnahme

Datum: 2026-05-26  
Kontext: Alle von fiview/fidlib referenzierten externen Ressourcen, bewertet nach
Verfügbarkeit und Relevanz für das Projekt.

---

## Status-Übersicht

| Ressource | URL / Quelle | Status | Priorität |
|---|---|---|---|
| fidlib.txt (API-Doku) | uazu.net/fidlib/fidlib.txt | ✅ gesichert | ✅ erledigt |
| firun.txt (CLI-Doku) | uazu.net/fidlib/firun.txt | ✅ gesichert | ✅ erledigt |
| fidlib-0.9.10.tgz (Original) | uazu.net/fidlib/ | ✅ erreichbar | mittel |
| mkfilter Quellcode | University of York | ❌ **tot** | **hoch** |
| mkfilter (billthefarmer-Fork) | github.com/billthefarmer/mkfilter | ✅ erreichbar | **hoch** |
| Audio EQ Cookbook (original) | harmony-central.com | ❌ **tot** (Casino-Site) | — |
| Audio EQ Cookbook (W3C-Kanonisch) | webaudio.github.io/Audio-EQ-Cookbook | ✅ gesichert | ✅ erledigt |
| GMeteor 0.95 | gmeteor.sourceforge.net | ✅ gesichert | ✅ erledigt |
| OpenEEG | openeeg.sf.net | ⚠️ SourceForge | niedrig |
| fiview_log.txt | uazu.net/fiview/fiview_log.txt | ✅ gesichert | ✅ erledigt |
| fiview-0.9.10 Quellcode | uazu.net/fiview/ | ✅ gesichert | ✅ erledigt |
| fidlib (JamesHight) | github.com/JamesHight/fidlib | ✅ in fidlib/ | ✅ erledigt |
| SDL 2 | github.com/libsdl-org/SDL | ✅ lokaler Mirror vorhanden | ✅ erledigt |
| SDL 1.2 | github.com/libsdl-org/SDL-1.2 | Mirror ausstehend | offen |

---

## Detailbewertung

### 1. fidlib.txt — API-Dokumentation (HOCH)

**URL:** https://uazu.net/fidlib/fidlib.txt (25 KB)

Die einzige vollständige Referenz für alle 47 Filtertypen, die Fispec-String-Syntax,
die komplette C-API, und die internen Datenstrukturen (`FidFilter`, `FidRun`).
Nicht im GitHub-Repo von JamesHight enthalten (nur als komprimierter Teil des `.tgz`).

→ **Sichern:** `doc/reference/fidlib.txt`

---

### 2. firun.txt — CLI-Dokumentation (HOCH)

**URL:** https://uazu.net/fidlib/firun.txt (5 KB)

Vollständige Beschreibung aller firun-Optionen, Datenformate (`a`, `b`, `s`, `S`, `f`, …),
Mehrkanal-Filterchaining-Syntax und Testmodi (Impuls-, Sprung-, Frequenzantwort).
Ebenfalls nicht im JamesHight-Repo, aber im vendor/fiview-Tarball als `README.firun`.

→ **Sichern:** `doc/reference/firun.txt` (Originalversion von uazu.net)

---

### 3. mkfilter — Mathematischer Kern (HOCH, GEFÄHRDET)

**Original-URL:** http://www-users.cs.york.ac.uk/~fisher/mkfilter — **DEAD**

Tony Fisher (University of York) ist verstorben. Alle URLs unter `cs.york.ac.uk/~fisher/`
wurden durch einen 301-Redirect auf die allgemeine Neuigkeitenseite der Informatik-Abteilung
ersetzt. Der Quellcode ist nicht mehr offiziell erreichbar.

**Warum kritisch:** `fidmkf.h` in fidlib ist ein direkter Ableger von mkfilter. Der
Algorithmus für Butterworth-, Bessel- und Chebyshev-Filter (Pol-Berechnung, Bilinear-
Transform, Prewarping) stammt vollständig aus mkfilter. Ohne die Quelle ist die
mathematische Basis des Projekts nicht mehr nachvollziehbar.

**Verfügbare Archive:**
- `github.com/billthefarmer/mkfilter` — 22 Commits, C++, leicht modernisiert für
  aktuelle GCC-Versionen, PNG-Plot-Unterstützung hinzugefügt. Enthält Originaldoku
  (`doc.pdf`). **Beste verfügbare Quelle.**
- `github.com/minimum-necessary-change/mkfilter` — Portierung für moderne Compiler,
  referenziert York-URL als Quelle.
- `github.com/MikeCurrington/mkfilter` — Includes Bugfixes von Miriam Ruiz (Debian).

→ **Sichern:** `billthefarmer/mkfilter` als zweites Submodule unter `vendor/mkfilter`

---

### 4. Audio EQ Cookbook — Biquad-Referenz (MITTEL)

**Original-URL:** http://www.harmony-central.com/Computer/Programming/Audio-EQ-Cookbook.txt  
→ **DEAD** (Domain zeigt jetzt eine französische Online-Casino-Seite)

**Kanonische Ersatz-URL (W3C/WebAudio WG):**  
https://webaudio.github.io/Audio-EQ-Cookbook/audio-eq-cookbook.html  
Herausgegeben mit Genehmigung von Robert Bristow-Johnson, editiert von Raymond Toy
und Doug Schepers. Enthält alle Biquad-Koeffizienten-Formeln (LP, HP, BP, Notch,
Allpass, Peaking EQ, Low-Shelf, High-Shelf) mit vollständiger mathematischer Herleitung.

fidlib nutzt diese Formeln für die Audio-EQ-Filtertypen (`LpBq`, `HpBq`, `PkBq`,
`LsBq`, `HsBq` etc.).

→ **Sichern:** `doc/reference/audio-eq-cookbook.html` (lokale Kopie der W3C-Version)

---

### 5. GMeteor 0.95 — FIR-Constraint-Design (MITTEL, GEFÄHRDET)

**URL:** https://gmeteor.sourceforge.net/

Werkzeug für equiripple FIR-Filter mit linearer Phase nach beliebigen
Frequenzgangsvorgaben (über Guile/Scheme-Skripte). Letzte Veröffentlichung: 2013.
SourceForge-Projekte sind historisch gefährdet (Downtime, Link-Rot,
Adware-Injektionen in Binaries vor einigen Jahren).

Relevanz: fidlib enthält keine FIR-Constraint-Design-Algorithmen — GMeteor füllt
diese Lücke für komplexe FIR-Anforderungen.

→ **Sichern:** Quell-Tarball `gmeteor-0.95.tar.gz` unter `vendor/gmeteor/`

---

### 6. fidlib-0.9.10.tgz — Original Jim Peters (MITTEL)

**URL:** https://uazu.net/fidlib/fidlib-0.9.10.tgz (86 KB)

Enthält die originale Version ohne JamesHight-Patches, dazu den vorkompilierten
Linux-Binary von firun. Relevant als Diff-Referenz: Welche Änderungen hat
JamesHight gegenüber dem Original vorgenommen?

Der Tarball enthält auch `fidlib.txt` und `firun.txt` — falls uazu.net irgendwann
offline geht, ist dies ein weiteres Backup.

→ **Sichern:** Kann als direkter Download gespeichert werden oder der enthaltene
Quellcode wird mit dem Original-Tarball-Inhalt in `vendor/fidlib-orig/` abgelegt.

---

### 7. OpenEEG — Historischer Anwendungskontext (NIEDRIG)

**URL:** http://openeeg.sf.net/

Das OpenEEG-Projekt war der ursprüngliche Anlass für die Entwicklung von fidlib.
Enthält EEG-Hardware-Schemata und Software. Historisch interessant als
Anwendungskontext, aber für das Filterdesign-Projekt selbst nicht relevant.

→ Kein Sicherungsbedarf

---

## Empfohlene Sicherungsreihenfolge

```
1. vendor/mkfilter/     ← billthefarmer/mkfilter als Submodule  (KRITISCH)
2. doc/reference/fidlib.txt                                       (wichtig)
3. doc/reference/firun.txt                                        (wichtig)
4. doc/reference/audio-eq-cookbook.html                           (wichtig)
5. vendor/gmeteor/      ← Tarball von SourceForge                 (risikogetrieben)
6. vendor/fidlib-orig/  ← uazu.net Original-Tarball               (optional)
```
