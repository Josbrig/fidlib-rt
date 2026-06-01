#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2025-2026 Jörg Simbrig
#
# bundle.py — package fiview2.html + WASM into a single offline file
#
# Usage:
#   python3 web/bundle.py [--wasm build-web/fiview2_wasm.wasm]
#                         [--js   build-web/fiview2_wasm.js]
#                         [--out  dist/fiview2.html]
#                         [--html web/fiview2.html]

import argparse, base64, pathlib, re, sys

def bundle(html_path, wasm_path, js_path, out_path):
    html = pathlib.Path(html_path).read_text(encoding='utf-8')
    wasm_bytes = pathlib.Path(wasm_path).read_bytes()
    js_text    = pathlib.Path(js_path).read_text(encoding='utf-8')

    wasm_b64 = base64.b64encode(wasm_bytes).decode('ascii')
    wasm_size = len(wasm_bytes)
    b64_size  = len(wasm_b64)

    print(f"WASM size:      {wasm_size/1024:.1f} KB")
    print(f"Base64 size:    {b64_size/1024:.1f} KB")

    # Patch the JS loader so it uses the embedded base64 instead of fetch()
    # The Emscripten-generated module checks for wasmBinary — we inject it.
    embedded_js = (
        f"const __WASM_B64__='{wasm_b64}';\n"
        f"const __WASM_BINARY__=Uint8Array.from(atob(__WASM_B64__),c=>c.charCodeAt(0));\n"
    ) + js_text.replace(
        'var FidlibModule=',
        'var FidlibModule=/*embedded*/'
    )

    # No src-patching needed — FidlibModule already defined by inline script,
    # boot() detects this via typeof FidlibModule !== 'undefined'
    patched_html = html

    # Add inline WASM JS block + modify FidlibModule call to inject wasmBinary
    wasm_js_block = (
        f"<script id='__wasm_js__'>\n{embedded_js}\n</script>\n"
    )
    patched_html = patched_html.replace('<body>', f'<body>\n{wasm_js_block}')

    # wasmBinary injection already in fiview2.html boot() function

    pathlib.Path(out_path).parent.mkdir(parents=True, exist_ok=True)
    pathlib.Path(out_path).write_text(patched_html, encoding='utf-8')

    total = pathlib.Path(out_path).stat().st_size
    print(f"Output:         {out_path}")
    print(f"Total size:     {total/1024:.1f} KB")
    print("Done.")

if __name__ == '__main__':
    p = argparse.ArgumentParser(description='Bundle fiview2 into a single HTML file')
    p.add_argument('--html', default='web/fiview2.html')
    p.add_argument('--wasm', default='build-web/fiview2_wasm.wasm')
    p.add_argument('--js',   default='build-web/fiview2_wasm.js')
    p.add_argument('--out',  default='dist/fiview2.html')
    args = p.parse_args()

    for f in (args.html, args.wasm, args.js):
        if not pathlib.Path(f).exists():
            print(f"ERROR: {f} not found", file=sys.stderr)
            sys.exit(1)

    bundle(args.html, args.wasm, args.js, args.out)
