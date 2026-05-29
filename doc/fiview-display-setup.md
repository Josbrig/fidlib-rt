# fiview — Display Setup for Visual Tests

fiview is an SDL application and requires an X11 connection. This guide
describes the setup for the typical working environment: development on the
Raspberry Pi 5, displayed on a remote Linux machine (DAN-O-MAT) via SSH.

---

## Recommended: SSH with X11 Forwarding (`-Y`)

Open a new SSH session on the local machine (DAN-O-MAT):

```bash
ssh -Y jos@raspi5
```

The shell variable `DISPLAY` is set automatically (e.g. `localhost:10.0`).
Then start fiview directly:

```bash
cd ~/Projects/digitalfilterdesign/build/tools/fiview
./fiview 44100 -i "LpBu6/400"
```

The window appears on DAN-O-MAT's screen.

**`-Y` instead of `-X`:** `-Y` is trusted forwarding and avoids rejections by
restrictive X security extensions. Always use `-Y` if there are problems with `-X`.

---

## Alternative: Screenshot Without a Visible Window

When no interactive display is needed (e.g. CI-like visual inspection), the
Pi's local X server (`DISPLAY=:0`) can be used:

```bash
# Start fiview in the background
DISPLAY=:0 ./fiview 44100 -i "LpBu6/400" &
FPID=$!
sleep 6

# Determine window ID and take screenshot
WIN=$(DISPLAY=:0 xwininfo -root -tree 2>/dev/null | grep '"fiview"' | head -1 | awk '{print $1}')
DISPLAY=:0 xwd -id "$WIN" -silent -out /tmp/fiview.xwd

# Convert XWD → PNG (PIL)
python3 - <<'EOF'
import struct
from PIL import Image

with open('/tmp/fiview.xwd', 'rb') as f:
    data = f.read()

fields = struct.unpack('>25I', data[0:100])
hdr_size, w, h, bpl, ncolors = fields[0], fields[4], fields[5], fields[12], fields[19]
raw = data[hdr_size + ncolors * 12 : hdr_size + ncolors * 12 + h * bpl]
img = Image.frombuffer('RGBA', (w, h), raw, 'raw', 'BGRA', bpl, 1)
Image.merge('RGB', img.split()[:3]).save('/tmp/fiview.png')
print(f"Saved: /tmp/fiview.png ({w}x{h})")
EOF

kill $FPID 2>/dev/null
pkill -f fiview 2>/dev/null
```

Prerequisites: `xwd` and Python package `Pillow` are installed.

---

## Filter Specification

fiview always expects a filter specification. Syntax with `-i`:

```bash
# Lowpass Butterworth order 6, cutoff 400 Hz, sample rate 44100 Hz
./fiview 44100 -i "LpBu6/400"

# Highpass Chebyshev order 4, 1 kHz
./fiview 44100 -i "HpCh4/1000"

# Load from file
./fiview 44100 myfilter.flt
```

Without sample rate and filter spec, fiview outputs an error message. The sample rate
comes before `-i`, the filter spec after it.
