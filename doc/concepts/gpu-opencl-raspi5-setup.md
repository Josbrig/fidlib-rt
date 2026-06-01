# GPU-Compute und OpenCL auf dem Raspberry Pi 5 — Setup-Analyse

Stand: 2026-05-28  
System: Raspberry Pi 5 Model B Rev 1.1, BCM2712, Cortex-A76, VideoCore VII (V3D 7.1)  
OS: Debian Bookworm, Kernel 6.12.75+rpt-rpi-2712, Mesa 24.2.8-1~bpo12+rpt4

---

## 1  Ist-Zustand

| Komponente | Status |
|---|---|
| Vulkan Compute | **Funktioniert** — V3D 7.1.10.2, Mesa V3DV, Vulkan 1.2.289 |
| OpenCL (Clover) | Installiert, aber **0 Devices** — kein `pipe_v3d.so` vorhanden |
| OpenCL (Rusticl) | **Nicht enthalten** im Debian-Paket `mesa-opencl-icd` |

```
$ clinfo
Platform Name     Clover / Mesa 24.2.8
Number of devices 0
→ No devices found in platform
```

`RUSTICL_ENABLE=v3d clinfo` bleibt ebenfalls leer — Rusticl ist in der
installierten `libMesaOpenCL.so.1.0.0` nicht kompiliert (0 Rusticl-Symbole).

---

## 2  GPU-Compute ohne zusätzlichen Aufwand: Vulkan

Vulkan Compute ist **sofort einsatzbereit**. Der V3DV-Treiber (Mesa) unterstützt
Compute Shader via `VK_KHR_storage_buffer_storage_class` und allgemeine
`vkCmdDispatch`-Ausführung.

Für dieses Projekt ist Vulkan Compute der empfohlene GPU-Pfad. Kein weiterer
Installationsschritt nötig.

---

## 3  OpenCL via GPU: Rusticl aus Mesa-Quellen bauen

Das einzige verfügbare Verfahren für echtes GPU-OpenCL auf dem RPi 5 ist ein
eigener Mesa-Build mit aktiviertem **Rusticl**-Backend.

Rusticl ist der neue Mesa-OpenCL-Stack (ab Mesa 22.3) der das Vulkan-Backend
(hier: V3DV) als Ausführungsschicht nutzt. Der Debian-Backport enthält Rusticl
nicht, da der Build eine Rust-Toolchain und bindgen voraussetzt.

### 3.1  Build-Abhängigkeiten

```bash
sudo aptitude install \
  build-essential meson ninja-build python3-mako \
  libdrm-dev libgbm-dev libvulkan-dev \
  llvm-dev libclang-dev libclc-dev \
  spirv-tools spirv-headers \
  rustc cargo rust-bindgen \
  glslang-tools \
  libxcb-dri2-0-dev libxcb-dri3-dev libxcb-present-dev libxcb-sync-dev \
  libxshmfence-dev libx11-xcb-dev libxrandr-dev libxext-dev \
  libwayland-dev wayland-protocols
```

Geschätzter Speicherbedarf für Build: ~4 GB (Quellen + Objekte).

### 3.2  Mesa-Quellcode

```bash
git clone https://gitlab.freedesktop.org/mesa/mesa.git
cd mesa
git checkout mesa-24.2.8    # selbe Version wie das installierte Paket
```

### 3.3  Konfiguration (minimaler V3D + Rusticl Build)

```bash
meson setup build \
  -Dgallium-drivers=v3d,kmsro \
  -Dvulkan-drivers=broadcom \
  -Dplatforms=x11,wayland \
  -Dgallium-opencl=rusticl \
  -Dopencl-spirv=true \
  -Dllvm=enabled \
  -Dbuildtype=release \
  --prefix=/usr/local
```

Wichtige Optionen:

| Option | Bedeutung |
|---|---|
| `gallium-opencl=rusticl` | Rusticl statt Clover (oder `disabled`) |
| `opencl-spirv=true` | SPIR-V-Kernel-Unterstützung (für OpenCL 3.0) |
| `llvm=enabled` | LLVM-Backend für Shader-Kompilierung |
| `gallium-drivers=v3d,kmsro` | Nur VideoCore VII + KMS render offload, keine AMD/NVIDIA |
| `vulkan-drivers=broadcom` | V3DV Vulkan-Treiber (Basis für Rusticl) |

### 3.4  Build und Installation

```bash
ninja -C build -j$(nproc)        # ~1–2 Stunden auf RPi 5
sudo ninja -C build install
sudo ldconfig
```

### 3.5  ICD registrieren

Nach dem Build muss Rusticl als OpenCL-ICD bekannt gemacht werden:

```bash
# Prüfen ob Rusticl-ICD automatisch erstellt wurde:
ls /usr/local/etc/OpenCL/vendors/

# Falls nicht, manuell anlegen:
echo "libRusticlOpenCL.so.1" | sudo tee /etc/OpenCL/vendors/rusticl.icd
```

### 3.6  Verifikation

```bash
RUSTICL_ENABLE=v3d clinfo
```

Erwartete Ausgabe (nach erfolgreichem Build):

```
Platform Name     Rusticl
Platform Version  OpenCL 3.0
Number of devices 1
  Device Name     V3D 7.1
  Device Type     GPU
  FP64 support    No
```

---

## 4  Einschränkungen auch nach erfolgreichem Rusticl-Build

| Einschränkung | Details |
|---|---|
| **Kein FP64** | VideoCore VII hat kein natives FP64 — auch Rusticl kann das nicht ändern |
| **OpenCL 3.0, nicht vollständig** | Rusticl auf V3D unterstützt nicht alle optionalen 3.0-Features |
| **Nur FP32-Kernel sinnvoll** | FP64-Emulation ~8–16× langsamer als FP32 |
| **Kein `cl_khr_fp64`** | `double` in OpenCL-Kerneln nicht nutzbar |
| **Experimentell** | Rusticl auf V3D ist produktionsreif, aber weniger getestet als V3DV (Vulkan) |

---

## 5  Gegenüberstellung der GPU-Compute-Wege

| | Vulkan Compute | OpenCL (Rusticl) |
|---|---|---|
| **Verfügbar** | Sofort | Nach Mesa-Eigenbau (~2 h) |
| **Treiber** | V3DV (Mesa, stabil) | Rusticl über V3DV (Mesa, experimentell) |
| **API** | Vulkan 1.2 | OpenCL 3.0 (subset) |
| **FP32** | Ja | Ja |
| **FP64** | Nein | Nein |
| **Portabilität** | RPi 4/5, Desktop (Vulkan) | RPi 5 (Rusticl), Desktop (AMD/NVIDIA) |
| **Wartungsaufwand** | Kein (Paket) | Eigener Build bei Mesa-Updates nötig |
| **Empfehlung** | Primärer Pfad | Nur wenn OpenCL-Portabilität konkret benötigt |

---

## 6  Empfehlung

Für dieses Projekt (fidlib GPU-Beschleunigung):

**Vulkan Compute zuerst** — bereits installiert, stabil, kein Zusatzaufwand.  
Der Rusticl-Mesa-Build lohnt sich nur wenn:

- bestehende OpenCL-Kernels portiert werden sollen (Wiederverwendung von `.cl`-Dateien), oder
- ein Desktop-System ebenfalls über OpenCL bedient werden soll (AMD/NVIDIA haben besseren OpenCL-Support als Vulkan Compute auf deren Plattformen), oder
- künftige Debian-Pakete Rusticl enthalten — dann fällt der Eigenaufwand weg.
