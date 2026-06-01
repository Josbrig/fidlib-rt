<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->
<!-- Copyright (C) 2025-2026 Jörg Simbrig -->

# Manuals

## Installation

Plattformspezifische Installationsanleitungen: was die Install-Scripts tun und
wie man es manuell Schritt für Schritt durchführt.

| Plattform | Manual |
|-----------|--------|
| Raspberry Pi 5 (BCM2712, VideoCore VII, Vulkan 1.2) | [installation/raspi5.md](installation/raspi5.md) |
| Raspberry Pi 4 (BCM2711, VideoCore VI) | [installation/raspi4.md](installation/raspi4.md) |
| Raspberry Pi 3 / Zero 2 W (BCM2837, VideoCore IV) | [installation/raspi3.md](installation/raspi3.md) |
| Desktop Linux x86_64 (Ubuntu/Debian, NVIDIA/AMD/Intel) | [installation/desktop-x86.md](installation/desktop-x86.md) |
| NVIDIA Jetson (JetPack 5/6, AArch64) | [installation/jetson.md](installation/jetson.md) |

## Lösungen

Exemplarische Probleme mit Schritt-für-Schritt-Anleitung wie sie mit
den Mitteln dieses Projekts gelöst werden.

| Problem | Manual |
|---------|--------|
| Tiefpass-Rauschunterdrückung in Audiosignalen | [loesungen/tiefpass-rauschunterdrueckung.md](loesungen/tiefpass-rauschunterdrueckung.md) |
| Bandpassfilterung für Bio- und Messsignale (EEG) | [loesungen/bandpass-biosignale.md](loesungen/bandpass-biosignale.md) |
| Echtzeit-Filterung im eigenen C-Programm (C-API) | [loesungen/echtzeit-c-api.md](loesungen/echtzeit-c-api.md) |
| Lange FIR-Filter effizient mit Overlap-Save FFT | [loesungen/fir-fft-beschleunigung.md](loesungen/fir-fft-beschleunigung.md) |
| GPU-beschleunigte FIR-Filterung via Vulkan (RPi 5) | [loesungen/gpu-vulkan-rpi5.md](loesungen/gpu-vulkan-rpi5.md) |
| Signalverarbeitung in der Shell mit firun | [loesungen/kommandozeilen-pipeline-firun.md](loesungen/kommandozeilen-pipeline-firun.md) |
