<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2025-2026 Kai Dieki -->

# Manuals

## Installation

Platform-specific installation guides: what the install scripts do and
how to perform each step manually.

| Platform | Manual |
|-----------|--------|
| Raspberry Pi 5 (BCM2712, VideoCore VII, Vulkan 1.2) | [installation/raspi5.md](installation/raspi5.md) |
| Raspberry Pi 4 (BCM2711, VideoCore VI) | [installation/raspi4.md](installation/raspi4.md) |
| Raspberry Pi 3 / Zero 2 W (BCM2837, VideoCore IV) | [installation/raspi3.md](installation/raspi3.md) |
| Desktop Linux x86_64 (Ubuntu/Debian, NVIDIA/AMD/Intel) | [installation/desktop-x86.md](installation/desktop-x86.md) |
| NVIDIA Jetson (JetPack 5/6, AArch64) | [installation/jetson.md](installation/jetson.md) |

## Solutions

Representative problems with step-by-step instructions for how to solve them
using the tools provided by this project.

| Problem | Manual |
|---------|--------|
| Low-pass noise suppression in audio signals | [loesungen/tiefpass-rauschunterdrueckung.md](loesungen/tiefpass-rauschunterdrueckung.md) |
| Bandpass filtering for bio- and measurement signals (EEG) | [loesungen/bandpass-biosignale.md](loesungen/bandpass-biosignale.md) |
| Real-time filtering in your own C program (C API) | [loesungen/echtzeit-c-api.md](loesungen/echtzeit-c-api.md) |
| Long FIR filters efficiently with overlap-save FFT | [loesungen/fir-fft-beschleunigung.md](loesungen/fir-fft-beschleunigung.md) |
| GPU-accelerated FIR filtering via Vulkan (RPi 5) | [loesungen/gpu-vulkan-rpi5.md](loesungen/gpu-vulkan-rpi5.md) |
| Signal processing in the shell with firun | [loesungen/kommandozeilen-pipeline-firun.md](loesungen/kommandozeilen-pipeline-firun.md) |
