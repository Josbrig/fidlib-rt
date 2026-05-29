# GPU Compute and OpenCL on the Raspberry Pi 5 — Setup Analysis

Date: 2026-05-28  
System: Raspberry Pi 5 Model B Rev 1.1, BCM2712, Cortex-A76, VideoCore VII (V3D 7.1)  
OS: Debian Bookworm, Kernel 6.12.75+rpt-rpi-2712, Mesa 24.2.8-1~bpo12+rpt4

---

## 1  Current State

| Component | Status |
|---|---|
| Vulkan Compute | **Working** — V3D 7.1.10.2, Mesa V3DV, Vulkan 1.2.289 |
| OpenCL (Clover) | Installed, but **0 devices** — no `pipe_v3d.so` present |
| OpenCL (Rusticl) | **Not included** in Debian package `mesa-opencl-icd` |

```
$ clinfo
Platform Name     Clover / Mesa 24.2.8
Number of devices 0
→ No devices found in platform
```

`RUSTICL_ENABLE=v3d clinfo` also stays empty — Rusticl is not compiled into the
installed `libMesaOpenCL.so.1.0.0` (0 Rusticl symbols).

---

## 2  GPU Compute Without Additional Effort: Vulkan

Vulkan Compute is **ready to use immediately**. The V3DV driver (Mesa) supports
compute shaders via `VK_KHR_storage_buffer_storage_class` and general
`vkCmdDispatch` execution.

For this project, Vulkan Compute is the recommended GPU path. No further
installation step needed.

---

## 3  OpenCL via GPU: Building Rusticl from Mesa Sources

The only available method for real GPU OpenCL on the RPi 5 is a
custom Mesa build with **Rusticl** backend enabled.

Rusticl is the new Mesa OpenCL stack (from Mesa 22.3) that uses the Vulkan backend
(here: V3DV) as the execution layer. The Debian backport does not include Rusticl,
since the build requires a Rust toolchain and bindgen.

### 3.1  Build Dependencies

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

Estimated disk space for build: ~4 GB (sources + objects).

### 3.2  Mesa Source Code

```bash
git clone https://gitlab.freedesktop.org/mesa/mesa.git
cd mesa
git checkout mesa-24.2.8    # same version as installed package
```

### 3.3  Configuration (minimal V3D + Rusticl build)

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

Important options:

| Option | Meaning |
|---|---|
| `gallium-opencl=rusticl` | Rusticl instead of Clover (or `disabled`) |
| `opencl-spirv=true` | SPIR-V kernel support (for OpenCL 3.0) |
| `llvm=enabled` | LLVM backend for shader compilation |
| `gallium-drivers=v3d,kmsro` | Only VideoCore VII + KMS render offload, no AMD/NVIDIA |
| `vulkan-drivers=broadcom` | V3DV Vulkan driver (basis for Rusticl) |

### 3.4  Build and Installation

```bash
ninja -C build -j$(nproc)        # ~1–2 hours on RPi 5
sudo ninja -C build install
sudo ldconfig
```

### 3.5  Register ICD

After the build, Rusticl must be registered as an OpenCL ICD:

```bash
# Check if Rusticl ICD was created automatically:
ls /usr/local/etc/OpenCL/vendors/

# If not, create manually:
echo "libRusticlOpenCL.so.1" | sudo tee /etc/OpenCL/vendors/rusticl.icd
```

### 3.6  Verification

```bash
RUSTICL_ENABLE=v3d clinfo
```

Expected output (after successful build):

```
Platform Name     Rusticl
Platform Version  OpenCL 3.0
Number of devices 1
  Device Name     V3D 7.1
  Device Type     GPU
  FP64 support    No
```

---

## 4  Limitations Even After a Successful Rusticl Build

| Limitation | Details |
|---|---|
| **No FP64** | VideoCore VII has no native FP64 — Rusticl cannot change this |
| **OpenCL 3.0, not complete** | Rusticl on V3D does not support all optional 3.0 features |
| **Only FP32 kernels sensible** | FP64 emulation ~8–16× slower than FP32 |
| **No `cl_khr_fp64`** | `double` not usable in OpenCL kernels |
| **Experimental** | Rusticl on V3D is production-ready, but less tested than V3DV (Vulkan) |

---

## 5  Comparison of GPU Compute Paths

| | Vulkan Compute | OpenCL (Rusticl) |
|---|---|---|
| **Available** | Immediately | After custom Mesa build (~2 h) |
| **Driver** | V3DV (Mesa, stable) | Rusticl via V3DV (Mesa, experimental) |
| **API** | Vulkan 1.2 | OpenCL 3.0 (subset) |
| **FP32** | Yes | Yes |
| **FP64** | No | No |
| **Portability** | RPi 4/5, desktop (Vulkan) | RPi 5 (Rusticl), desktop (AMD/NVIDIA) |
| **Maintenance effort** | None (package) | Custom build required on Mesa updates |
| **Recommendation** | Primary path | Only if OpenCL portability specifically needed |

---

## 6  Recommendation

For this project (fidlib GPU acceleration):

**Vulkan Compute first** — already installed, stable, no additional effort.  
The Rusticl Mesa build is only worthwhile if:

- existing OpenCL kernels are to be ported (reuse of `.cl` files), or
- a desktop system also needs to be served via OpenCL (AMD/NVIDIA have better OpenCL support than Vulkan Compute on their platforms), or
- future Debian packages include Rusticl — then the custom build effort falls away.
