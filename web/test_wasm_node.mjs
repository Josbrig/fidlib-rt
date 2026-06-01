// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig
// Node.js smoke test for fiview2_wasm.js

import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import path from 'path';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const require = createRequire(import.meta.url);

// Load the compiled WASM module
const modulePath = path.join(__dirname, '../build-web/fiview2_wasm.js');
const FidlibModule = require(modulePath);

let passed = 0;
let failed = 0;

function assert(cond, msg) {
    if (cond) {
        console.log(`  ✓ ${msg}`);
        passed++;
    } else {
        console.error(`  ✗ FAIL: ${msg}`);
        failed++;
    }
}

FidlibModule().then(wasm => {
    const ccall = wasm.ccall;
    const UTF8 = s => wasm.UTF8ToString(s);

    // Helper: call string-returning function
    function callStr(fn, ...args) {
        const types = args.map(a => typeof a === 'number' ? 'number' : 'string');
        const ptr = ccall(fn, 'number', types, args);
        if (!ptr) return null;
        const str = UTF8(ptr);
        ccall('wasm_free_string', null, ['number'], [ptr]);
        return str;
    }

    console.log('\n=== fiview2 WASM Smoke Test ===\n');

    // ── Test 1: build_spec ──────────────────────────────────────────────────
    console.log('1. build_spec');
    const spec = callStr('wasm_build_spec',
        0,  // Butterworth
        0,  // LP
        4,  // order
        1000.0, 2000.0, -1.0, 1.0, 6.0);
    assert(spec !== null, `build_spec returned: ${spec}`);
    assert(spec && spec.includes('Bu'), `spec contains "Bu": ${spec}`);

    // ── Test 2: frequency response ──────────────────────────────────────────
    console.log('2. freq_response_json (LpBu4/1000 @ 44100)');
    const freqJson = callStr('wasm_freq_response_json', 'LpBu4/1000', 44100.0);
    assert(freqJson !== null, 'freq_response_json returned data');
    const freq = freqJson ? JSON.parse(freqJson) : [];
    assert(freq.length === 512, `512 frequency points (got ${freq.length})`);
    assert(freq[0] && typeof freq[0].f === 'number', 'first point has freq');
    assert(freq[0] && typeof freq[0].m === 'number', 'first point has magnitude_db');
    // Index 420 is log-spaced ≈ 4000 Hz (stopband for LpBu4/1000)
    const stopMag = freq[420]?.m ?? -999;
    assert(stopMag < -40.0, `stopband attenuation at ~4kHz: ${stopMag.toFixed(1)} dB`);

    // ── Test 3: impulse response ─────────────────────────────────────────────
    console.log('3. impulse_json');
    const impJson = callStr('wasm_impulse_json', 'LpBu4/1000', 44100.0, 256);
    assert(impJson !== null, 'impulse_json returned data');
    const imp = impJson ? JSON.parse(impJson) : [];
    assert(imp.length === 256, `256 impulse samples (got ${imp.length})`);
    assert(imp.every(v => isFinite(v)), 'all samples finite');

    // ── Test 4: step response ────────────────────────────────────────────────
    console.log('4. step_json');
    const stepJson = callStr('wasm_step_json', 'LpBu4/1000', 44100.0, 256);
    assert(stepJson !== null, 'step_json returned data');
    const step = stepJson ? JSON.parse(stepJson) : [];
    const last = step[step.length - 1];
    assert(Math.abs(last - 1.0) < 0.01, `step converges to 1.0 (last=${last?.toFixed(4)})`);

    // ── Test 5: poles/zeros ──────────────────────────────────────────────────
    console.log('5. poles_zeros_json');
    const pzJson = callStr('wasm_poles_zeros_json', 'LpBu4/1000', 44100.0);
    assert(pzJson !== null, 'poles_zeros_json returned data');
    const pz = pzJson ? JSON.parse(pzJson) : [];
    assert(pz.length > 0, `got ${pz.length} poles/zeros`);
    const poles = pz.filter(p => p.pole);
    assert(poles.every(p => Math.sqrt(p.re**2 + p.im**2) < 1.0),
           'all poles inside unit circle (stable)');

    // ── Test 6: code generation ──────────────────────────────────────────────
    console.log('6. generate_code (c99)');
    const code = callStr('wasm_generate_code', 'LpBu4/1000', 44100.0, 'c99');
    assert(code !== null, 'generate_code returned data');
    assert(code && code.includes('double'), 'c99 output contains "double"');
    assert(code && code.length > 100, `code length: ${code?.length}`);

    // ── Test 7: Python ───────────────────────────────────────────────────────
    console.log('7. generate_code (python)');
    const pyCode = callStr('wasm_generate_code', 'LpBu4/1000', 44100.0, 'python');
    assert(pyCode && pyCode.includes('def '), 'python output has "def"');

    // ── Test 8: error handling ───────────────────────────────────────────────
    console.log('8. error handling');
    const badSpec = callStr('wasm_freq_response_json', 'LpBe20/1000', 44100.0);
    assert(badSpec === null, 'Bessel order 20 returns null');
    const err = wasm.ccall('wasm_last_error', 'string', [], []);
    assert(err && err.length > 0, `error message: "${err}"`);

    // ── Test 9: bad language ─────────────────────────────────────────────────
    console.log('9. unknown language');
    const badCode = callStr('wasm_generate_code', 'LpBu4/1000', 44100.0, 'brainfuck');
    assert(badCode === null, 'unknown language returns null');

    // ── Summary ──────────────────────────────────────────────────────────────
    console.log(`\n${'─'.repeat(40)}`);
    console.log(`PASS: ${passed}   FAIL: ${failed}`);
    if (failed > 0) process.exit(1);
});
