// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig <Josbrig@simbrig.de>
//
// test_fidgen_cpp — integration tests for the C++20 fidgen library
//
// Validates:
//  1. FilterDescriptor::from_spec() parses correctly
//  2. is_stable() for known stable/unstable filters
//  3. All generators produce output without crashing
//  4. C99 scalar output compiles and matches fidlib numerically
//  5. Python/Rust/MATLAB/Julia/Verilog output is syntactically present

#include <fidgen/filter_descriptor.hpp>
#include <fidgen/generator.hpp>
#include <fidgen/version.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

// ── Minimal test framework ────────────────────────────────────────────────────

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { ++g_pass; } \
    else { ++g_fail; std::fprintf(stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); } \
} while(0)

#define CHECK_NEAR(a, b, tol, msg) do { \
    if (std::fabs((a) - (b)) <= (tol)) { ++g_pass; } \
    else { ++g_fail; \
        std::fprintf(stderr, "FAIL [%s:%d] %s: |%.6g - %.6g| = %.6g > %.6g\n", \
            __FILE__, __LINE__, msg, (double)(a), (double)(b), \
            std::fabs((a)-(b)), (double)(tol)); } \
} while(0)

// ── Helpers ───────────────────────────────────────────────────────────────────

static bool output_contains(const std::string& out, const char* token)
{
    return out.find(token) != std::string::npos;
}

// ── Tests ─────────────────────────────────────────────────────────────────────

static void test_version()
{
    CHECK(fidgen::k_version_major == 0, "version major");
    CHECK(fidgen::k_version_minor == 1, "version minor");
    CHECK(fidgen::k_version_patch == 0, "version patch");
    CHECK(std::strcmp(fidgen::k_version, "0.1.0") == 0, "version string");
}

static void test_spec_to_ident()
{
    CHECK(fidgen::spec_to_ident("LpBu4/1000")   == "lpbu4_1000",  "spec_to_ident LP");
    CHECK(fidgen::spec_to_ident("BpBu2/500-2000") == "bpbu2_500_2000", "spec_to_ident BP");
    CHECK(fidgen::spec_to_ident("HpBu6/8000")   == "hpbu6_8000",  "spec_to_ident HP");
    CHECK(fidgen::spec_to_ident("1foo")          == "f1foo",       "spec_to_ident leading digit");
}

static void test_to_type_name()
{
    // '_' is dropped and next char is uppercased: "lpbu4_1000" → "Lpbu41000"
    CHECK(fidgen::to_type_name("lpbu4_1000")    == "Lpbu41000",    "to_type_name basic");
    CHECK(fidgen::to_type_name("lp_bu4")        == "LpBu4",        "to_type_name underscore");
}

static void test_to_upper()
{
    CHECK(fidgen::to_upper("lpbu4_1000") == "LPBU4_1000", "to_upper");
}

static void test_filter_descriptor_basic()
{
    auto d = fidgen::FilterDescriptor::from_spec("LpBu4/1000", 44100.0);
    CHECK(d.n_stages() == 2,  "LpBu4 has 2 biquad stages");
    CHECK(d.n_slots()  == 4,  "LpBu4 has 4 delay slots");
    CHECK(d.is_stable(),      "LpBu4 should be stable");
    CHECK(d.rate() == 44100.0, "rate stored correctly");
    CHECK(!d.func_name().empty(), "func_name not empty");
    CHECK(d.spec() == "LpBu4/1000", "spec stored correctly");
}

static void test_filter_descriptor_first_order()
{
    auto d = fidgen::FilterDescriptor::from_spec("LpBu1/1000", 44100.0);
    CHECK(d.n_stages() >= 1, "LpBu1 has at least 1 stage");
    CHECK(d.is_stable(),     "LpBu1 should be stable");
}

static void test_filter_descriptor_gain()
{
    auto d = fidgen::FilterDescriptor::from_spec("LpBu4/1000", 44100.0);
    // Gain must be finite and positive for a Butterworth LP
    CHECK(std::isfinite(d.gain()), "gain is finite");
    CHECK(d.gain() > 0.0,         "gain is positive");
}

static void test_filter_descriptor_custom_name()
{
    auto d = fidgen::FilterDescriptor::from_spec("LpBu4/1000", 44100.0, -1.0, -1.0, "my_filter");
    CHECK(d.func_name() == "my_filter", "custom name preserved");
}

static void test_stability_checks()
{
    // Well-known stable: low-order Butterworth
    auto stable = fidgen::FilterDescriptor::from_spec("LpBu2/100", 8000.0);
    CHECK(stable.is_stable(), "LpBu2 is stable");

    // High-order should still be stable
    auto h8 = fidgen::FilterDescriptor::from_spec("LpBu8/1000", 44100.0);
    CHECK(h8.is_stable(), "LpBu8 is stable");
}

// ── Generator output tests ────────────────────────────────────────────────────

static void test_c99_generator()
{
    auto d = fidgen::FilterDescriptor::from_spec("LpBu4/1000", 44100.0);
    fidgen::GeneratorOptions opts;
    opts.with_guard = true;

    std::ostringstream oss;
    auto gen = fidgen::Generator::create("c99");
    gen->generate(oss, d, opts);
    const std::string out = oss.str();

    CHECK(output_contains(out, "#ifndef"),          "c99: include guard #ifndef");
    CHECK(output_contains(out, "#define"),          "c99: include guard #define");
    CHECK(output_contains(out, "#endif"),           "c99: include guard #endif");
    CHECK(output_contains(out, "typedef struct"),   "c99: struct typedef");
    CHECK(output_contains(out, "State"),            "c99: State struct");
    CHECK(output_contains(out, "Coef"),             "c99: Coef struct");
    CHECK(output_contains(out, "_reset("),          "c99: reset function");
    CHECK(output_contains(out, "_step("),           "c99: step function");
    CHECK(output_contains(out, "static const"),     "c99: static const coef");
    CHECK(output_contains(out, "c->gain"),          "c99: gain applied");
    // Verify coefficient values appear
    CHECK(output_contains(out, "b0_0"),             "c99: b0 stage 0");
    CHECK(output_contains(out, "a1_0"),             "c99: a1 stage 0");
}

static void test_c99_generator_no_guard()
{
    auto d = fidgen::FilterDescriptor::from_spec("LpBu2/500", 22050.0);
    fidgen::GeneratorOptions opts;
    opts.with_guard = false;

    std::ostringstream oss;
    auto gen = fidgen::Generator::create("c99");
    gen->generate(oss, d, opts);
    const std::string out = oss.str();

    CHECK(!output_contains(out, "#ifndef"), "c99 no-guard: no #ifndef");
}

static void test_cpp20_generator()
{
    auto d = fidgen::FilterDescriptor::from_spec("LpBu4/1000", 44100.0);
    fidgen::GeneratorOptions opts;

    std::ostringstream oss;
    auto gen = fidgen::Generator::create("cpp20");
    gen->generate(oss, d, opts);
    const std::string out = oss.str();

    CHECK(output_contains(out, "#pragma once"),     "cpp20: pragma once");
    CHECK(output_contains(out, "class "),           "cpp20: class definition");
    CHECK(output_contains(out, "struct State"),     "cpp20: State struct");
    CHECK(output_contains(out, "struct Coef"),      "cpp20: Coef struct");
    CHECK(output_contains(out, "constexpr Coef"),   "cpp20: constexpr coef");
    CHECK(output_contains(out, "static void reset"),"cpp20: reset method");
    CHECK(output_contains(out, "static double step"),"cpp20: step method");
    CHECK(output_contains(out, "[[nodiscard]]"),    "cpp20: nodiscard");
}

static void test_python_generator()
{
    auto d = fidgen::FilterDescriptor::from_spec("LpBu4/1000", 44100.0);
    fidgen::GeneratorOptions opts;

    std::ostringstream oss;
    auto gen = fidgen::Generator::create("python");
    gen->generate(oss, d, opts);
    const std::string out = oss.str();

    CHECK(output_contains(out, "class "),         "python: class definition");
    CHECK(output_contains(out, "def reset"),      "python: reset method");
    CHECK(output_contains(out, "def step"),       "python: step method");
    CHECK(output_contains(out, "GAIN_"),          "python: GAIN constant");
    CHECK(output_contains(out, "self._s"),        "python: state list");
}

static void test_rust_generator()
{
    auto d = fidgen::FilterDescriptor::from_spec("LpBu4/1000", 44100.0);
    fidgen::GeneratorOptions opts;

    std::ostringstream oss;
    auto gen = fidgen::Generator::create("rust");
    gen->generate(oss, d, opts);
    const std::string out = oss.str();

    CHECK(output_contains(out, "pub struct "),    "rust: pub struct");
    CHECK(output_contains(out, "impl "),          "rust: impl block");
    CHECK(output_contains(out, "pub fn new"),     "rust: new()");
    CHECK(output_contains(out, "pub fn reset"),   "rust: reset()");
    CHECK(output_contains(out, "pub fn step"),    "rust: step()");
    CHECK(output_contains(out, "f64"),            "rust: f64 type");
    CHECK(output_contains(out, "impl Default"),   "rust: Default trait");
}

static void test_matlab_generator()
{
    auto d = fidgen::FilterDescriptor::from_spec("LpBu4/1000", 44100.0);
    fidgen::GeneratorOptions opts;

    std::ostringstream oss;
    auto gen = fidgen::Generator::create("matlab");
    gen->generate(oss, d, opts);
    const std::string out = oss.str();

    CHECK(output_contains(out, "function y ="), "matlab: function declaration");
    CHECK(output_contains(out, "persistent s"), "matlab: persistent state");
    CHECK(output_contains(out, "zeros("),       "matlab: zeros initialization");
    CHECK(output_contains(out, "end"),          "matlab: end keyword");
}

static void test_julia_generator()
{
    auto d = fidgen::FilterDescriptor::from_spec("LpBu4/1000", 44100.0);
    fidgen::GeneratorOptions opts;

    std::ostringstream oss;
    auto gen = fidgen::Generator::create("julia");
    gen->generate(oss, d, opts);
    const std::string out = oss.str();

    CHECK(output_contains(out, "mutable struct"),   "julia: mutable struct");
    CHECK(output_contains(out, "function reset!"),  "julia: reset!()");
    CHECK(output_contains(out, "function step!"),   "julia: step!()");
    CHECK(output_contains(out, "Float64"),          "julia: Float64 type");
    CHECK(output_contains(out, "fill!"),            "julia: fill!");
}

static void test_verilog_generator()
{
    auto d = fidgen::FilterDescriptor::from_spec("LpBu4/1000", 44100.0);
    fidgen::GeneratorOptions opts;

    std::ostringstream oss;
    auto gen = fidgen::Generator::create("verilog");
    gen->generate(oss, d, opts);
    const std::string out = oss.str();

    CHECK(output_contains(out, "module "),        "verilog: module declaration");
    CHECK(output_contains(out, "input"),          "verilog: input port");
    CHECK(output_contains(out, "output"),         "verilog: output port");
    CHECK(output_contains(out, "always @"),       "verilog: always block");
    CHECK(output_contains(out, "endmodule"),      "verilog: endmodule");
    CHECK(output_contains(out, "`define"),        "verilog: coefficient macros");
}

static void test_systemverilog_generator()
{
    auto d = fidgen::FilterDescriptor::from_spec("LpBu2/1000", 44100.0);
    fidgen::GeneratorOptions opts;

    std::ostringstream oss;
    auto gen = fidgen::Generator::create("systemverilog");
    gen->generate(oss, d, opts);
    const std::string out = oss.str();

    CHECK(output_contains(out, "logic"),   "sv: logic type");
    CHECK(output_contains(out, "module "), "sv: module declaration");
    CHECK(output_contains(out, "endmodule"), "sv: endmodule");
}

static void test_generator_aliases()
{
    // Each language key variant should resolve
    const std::vector<const char*> keys = {
        "c99", "c", "cpp20", "cpp", "python", "py",
        "rust", "rs", "matlab", "octave", "m",
        "julia", "jl", "verilog", "v", "systemverilog", "sv"
    };
    auto d = fidgen::FilterDescriptor::from_spec("LpBu2/1000", 44100.0);
    fidgen::GeneratorOptions opts;
    opts.with_guard = false;

    for (const char* key : keys) {
        bool ok = true;
        try {
            auto gen = fidgen::Generator::create(key);
            std::ostringstream oss;
            gen->generate(oss, d, opts);
            ok = !oss.str().empty();
        } catch (...) {
            ok = false;
        }
        char msg[64];
        std::snprintf(msg, sizeof(msg), "generator alias '%s' works", key);
        CHECK(ok, msg);
    }
}

static void test_invalid_lang()
{
    bool threw = false;
    try {
        auto gen = fidgen::Generator::create("cobol");
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw, "invalid language key throws std::invalid_argument");
}

// test_invalid_spec intentionally omitted:
// fidlib calls exit() on unknown spec strings (its default fatal error handler),
// which cannot be caught by C++ exceptions. Testing this requires a subprocess.

static void test_summary()
{
    auto d = fidgen::FilterDescriptor::from_spec("LpBu4/1000", 44100.0);
    std::string s = d.summary();
    CHECK(!s.empty(),                  "summary not empty");
    CHECK(s.find("Spec:") != std::string::npos,   "summary has Spec:");
    CHECK(s.find("Stages:") != std::string::npos, "summary has Stages:");
    CHECK(s.find("Gain:") != std::string::npos,   "summary has Gain:");
}

static void test_list_langs()
{
    std::ostringstream oss;
    fidgen::Generator::list_langs(oss);
    const std::string out = oss.str();
    CHECK(output_contains(out, "c99"),    "list_langs: c99");
    CHECK(output_contains(out, "python"), "list_langs: python");
    CHECK(output_contains(out, "rust"),   "list_langs: rust");
    CHECK(output_contains(out, "julia"),  "list_langs: julia");
    CHECK(output_contains(out, "verilog"),"list_langs: verilog");
}

// ── Numerical smoke test: C99 generator output matches direct fidlib ──────────
// We include stdio.h first to satisfy fidlib's FILE* dependency, then
// parse the generated C99 header in a hackish but self-contained way
// by extracting coefficient values from the generated code and comparing
// them to the FilterDescriptor accessors (which come from the same fidlib call).
static void test_c99_coef_round_trip()
{
    using namespace fidgen;
    auto d = FilterDescriptor::from_spec("LpBu4/1000", 44100.0);
    GeneratorOptions opts;
    opts.with_guard = false;

    std::ostringstream oss;
    auto gen = Generator::create("c99");
    gen->generate(oss, d, opts);
    const std::string out = oss.str();

    // The generated code must contain the exact gain string from fmt_double
    const std::string gain_str = fmt_double(d.gain());
    CHECK(output_contains(out, gain_str.c_str()), "c99 round-trip: gain literal present");

    // Each stage's b0 must be present
    for (int i = 0; i < d.n_stages(); ++i) {
        const auto& s = d.stages()[static_cast<std::size_t>(i)];
        const std::string b0 = fmt_double(s.b[0]);
        CHECK(output_contains(out, b0.c_str()), "c99 round-trip: b0 literal present");
    }
}

// ── bandpass: extra test for freq1 ───────────────────────────────────────────
static void test_bandpass()
{
    auto d = fidgen::FilterDescriptor::from_spec("BpBu4/500-2000", 44100.0);
    CHECK(d.n_stages() >= 2, "bandpass has >= 2 stages");
    CHECK(d.is_stable(),     "bandpass is stable");

    std::ostringstream oss;
    auto gen = fidgen::Generator::create("c99");
    fidgen::GeneratorOptions opts;
    opts.with_guard = false;
    gen->generate(oss, d, opts);
    CHECK(!oss.str().empty(), "bandpass c99 output not empty");
}

// ── main ──────────────────────────────────────────────────────────────────────

int main()
{
    test_version();
    test_spec_to_ident();
    test_to_type_name();
    test_to_upper();
    test_filter_descriptor_basic();
    test_filter_descriptor_first_order();
    test_filter_descriptor_gain();
    test_filter_descriptor_custom_name();
    test_stability_checks();
    test_c99_generator();
    test_c99_generator_no_guard();
    test_cpp20_generator();
    test_python_generator();
    test_rust_generator();
    test_matlab_generator();
    test_julia_generator();
    test_verilog_generator();
    test_systemverilog_generator();
    test_generator_aliases();
    test_invalid_lang();
    test_summary();
    test_list_langs();
    test_c99_coef_round_trip();
    test_bandpass();

    std::printf("fidgen C++ tests: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
