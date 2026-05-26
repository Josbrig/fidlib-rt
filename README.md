# Digital Filter Design — Modernisation Project

This repository collects, modernises, and re-packages the classic digital filter toolchain
originally published at <https://uazu.net/fiview/> into a coherent, maintained runtime
library together with all preparatory tooling.

---

## Project Goal

The aim is to produce a **self-contained runtime filter library** that:

1. **Accepts a filter specification** (type, order, corner frequency, sample rate, …) at
   programme start or even mid-stream — no recompilation required.
2. **Designs the filter on the fly** using the proven algorithms behind fidlib/fiview
   (IIR: Butterworth, Chebyshev, Bessel, …; FIR via windowed-sinc or constraint-based
   design).
3. **Executes the filter** sample-by-sample or block-by-block against arbitrary data
   streams (binary or ASCII), with the same interface as the original `firun` companion
   utility — but modernised, tested, and packaged for reuse.
4. **Ships all preparatory tools** as first-class citizens of the repository, not as
   separate downloads from a static webpage.

The end product is a library + CLI toolkit that takes you from *"I need a bandpass at
300 Hz with 60 dB stopband rejection at 44100 Hz sample rate"* to a running, verified
filter in one step.

---

## Origin — The uazu.net Toolchain

The project is based on three tools originally developed by Jim Peters and hosted at
<https://uazu.net/fiview/>:

### fiview
A GUI application (Windows / Linux / macOS) for interactive digital filter design,
visualisation, and comparison.  It displays frequency response, phase response, group
delay, and impulse/step response, and can export readable C code that uses `fidlib` for
runtime flexibility.  Built on top of Dr. Tony Fisher's classic `mkfilter` algorithms,
extended with stage-based decomposition for improved numerical stability at high filter
orders.

Current upstream version: **0.9.10** (stable, GPL).

### fidlib
A C runtime library (LGPL) for designing *and executing* IIR/FIR filters without a
recompile.  Filters are specified via a compact string format (the *fispec* DSL), for
example:

```
BsBu10/239-247      # 10th-order Butterworth band-stop, 239–247 Hz
HsBq/0.8/-15/12000  # shelving high-shelf filter
LpBe4/0.1           # 4th-order Bessel low-pass at 10 % of Nyquist
```

The library generates filter coefficients at runtime, decomposes them into second-order
sections (biquads) for numerical robustness, and provides a simple `fid_run` / `fid_step`
API to process samples one at a time or in blocks.

### firun
A command-line companion to `fidlib` that pipes binary or ASCII sample streams through
one or more filters specified on the command line.  Useful for offline processing, batch
testing, and scripted signal analysis.  Supports impulse, step, and frequency response
measurement modes.

---

## Scope of This Repository

| Area | Status |
|---|---|
| Collect upstream source (fiview, fidlib, firun) | planned |
| Audit and modernise C code (C99/C11, warnings clean, sanitisers) | planned |
| Add CMake / Meson build system | planned |
| Port fispec parser to a standalone, reusable module | planned |
| Add comprehensive test suite (impulse response, frequency response) | planned |
| Packaging as a shared/static library with public headers | planned |
| Modern CLI for firun (argparse, JSON output option) | planned |
| Language bindings (Python, Rust FFI) | future |
| Replace fiview GUI with a TUI or web-based visualiser | future |

---

## Non-Goals

- This project is **not** about re-hosting the uazu.net website.
- It does **not** aim to replace the underlying filter mathematics — the proven algorithms
  from mkfilter / fidlib are kept as-is; only the engineering around them is modernised.
- No real-time audio plugin format (VST, LV2, …) is targeted in the initial phase.

---

## References

- Original toolchain: <https://uazu.net/fiview/>
- fidlib project page: <https://uazu.net/fidlib/>
- mkfilter (Tony Fisher, University of York): classical IIR filter design algorithms
- OpenEEG project: early real-world user of fidlib (EEG signal processing)
- EEGMIR: biofeedback application that uses fidlib internally
