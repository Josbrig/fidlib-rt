// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig <Josbrig@simbrig.de>
//
// test_fidgen_integration — Integrationstest: alle Sprachen × Filtertypen
//
// Matrix-Test: 8 Sprachen × 5 Filtertypen
//   For each combination: generate() → non-empty, contains expected tokens
//
// SIMD-Matrix: c99 × {None, Neon, SSE2, AVX2, Auto}
//
// C-API-Smoke: fidgen_generate() / fidgen_free() / fidgen_version()

#include <fidgen/generator.hpp>
#include <fidgen/filter_descriptor.hpp>
#include <fidgen/fidgen.h>

#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

// ── Minimales Testframework ───────────────────────────────────────────────────

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { ++g_pass; } \
    else { ++g_fail; \
        std::fprintf(stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); } \
} while(0)

// Returns the generated code for spec+lang, empty string on failure.
static std::string generate(const char* spec, double rate,
                             const char* lang, const char* name = nullptr,
                             fidgen::SimdLevel simd = fidgen::SimdLevel::None)
{
    auto desc = fidgen::FilterDescriptor::from_spec(spec, rate, -1.0, -1.0,
                                                     name ? name : "");
    auto gen  = fidgen::Generator::create(lang);
    fidgen::GeneratorOptions opts;
    opts.simd = simd;
    std::ostringstream oss;
    gen->generate(oss, desc, opts);
    return oss.str();
}

static bool contains(const std::string& haystack, const char* needle)
{
    return haystack.find(needle) != std::string::npos;
}

// ── Matrix: filter specs ──────────────────────────────────────────────────────

struct FilterCase {
    const char* spec;
    double      rate;
    const char* ident;        // expected spec_to_ident result
    const char* type_name;    // expected to_type_name result
};

static const FilterCase k_filters[] = {
    { "LpBu4/1000",    44100.0, "lpbu4_1000",     "Lpbu41000"    },
    { "HpBu2/8000",    44100.0, "hpbu2_8000",     "Hpbu28000"    },
    { "BpBu4/500-2000",44100.0, "bpbu4_500_2000", "Bpbu45002000" },
    { "BsBu4/500-2000",44100.0, "bsbu4_500_2000", "Bsbu45002000" },
    { "LpBe4/1000",    44100.0, "lpbe4_1000",     "Lpbe41000"    },
    { "LpCh4/-1/1000", 44100.0, "lpch4_1_1000",   "Lpch411000"   },
    { "LpHm/1000",     44100.0, "lphm_1000",      "Lphm1000"     },  // FIR
    { "LpBl/1000",     44100.0, "lpbl_1000",      "Lpbl1000"     },  // FIR
    { "LpHn/1000",     44100.0, "lphn_1000",      "Lphn1000"     },  // FIR
    { "LpBa/1000",     44100.0, "lpba_1000",      "Lpba1000"     },  // FIR
    { "PkBq2/1000/1/6",44100.0, "pkbq2_1000_1_6", "Pkbq2100016"  }, // Peaking EQ
    { "BpRe/0.5/1000", 44100.0, "bpre_0_5_1000",  "Bpre051000"   }, // Resonator
    { "ApBq2/1/1000",  44100.0, "apbq2_1_1000",   "Apbq211000"   }, // Allpass
};
static constexpr int N_FILTERS = static_cast<int>(sizeof(k_filters)/sizeof(k_filters[0]));

// ── Matrix: language tests ────────────────────────────────────────────────────

static void test_c99()
{
    for (int i = 0; i < N_FILTERS; ++i) {
        const auto& f = k_filters[i];
        auto code = generate(f.spec, f.rate, "c99");

        char msg[128];
        std::snprintf(msg, sizeof(msg), "c99/%s: non-empty", f.spec);
        CHECK(!code.empty(), msg);

        std::snprintf(msg, sizeof(msg), "c99/%s: contains step fn", f.spec);
        std::string step_fn = std::string(f.ident) + "_step";
        CHECK(contains(code, step_fn.c_str()), msg);

        std::snprintf(msg, sizeof(msg), "c99/%s: contains State struct", f.spec);
        std::string state = std::string(f.type_name) + "State";
        CHECK(contains(code, state.c_str()), msg);
    }
}

static void test_cpp20()
{
    for (int i = 0; i < N_FILTERS; ++i) {
        const auto& f = k_filters[i];
        auto code = generate(f.spec, f.rate, "cpp20");

        char msg[128];
        std::snprintf(msg, sizeof(msg), "cpp20/%s: non-empty", f.spec);
        CHECK(!code.empty(), msg);

        std::snprintf(msg, sizeof(msg), "cpp20/%s: contains Filter class", f.spec);
        std::string cls = std::string(f.type_name) + "Filter";
        CHECK(contains(code, cls.c_str()), msg);

        std::snprintf(msg, sizeof(msg), "cpp20/%s: constexpr coef", f.spec);
        CHECK(contains(code, "constexpr"), msg);
    }
}

static void test_python()
{
    for (int i = 0; i < N_FILTERS; ++i) {
        const auto& f = k_filters[i];
        auto code = generate(f.spec, f.rate, "python");

        char msg[128];
        std::snprintf(msg, sizeof(msg), "python/%s: non-empty", f.spec);
        CHECK(!code.empty(), msg);

        std::snprintf(msg, sizeof(msg), "python/%s: contains class", f.spec);
        std::string cls = std::string("class ") + f.type_name + "Filter";
        CHECK(contains(code, cls.c_str()), msg);

        std::snprintf(msg, sizeof(msg), "python/%s: def step", f.spec);
        CHECK(contains(code, "def step"), msg);
    }
}

static void test_python_alias()
{
    auto a = generate(k_filters[0].spec, k_filters[0].rate, "python");
    auto b = generate(k_filters[0].spec, k_filters[0].rate, "py");
    CHECK(a == b, "python/py alias: identical output");
}

static void test_rust()
{
    for (int i = 0; i < N_FILTERS; ++i) {
        const auto& f = k_filters[i];
        auto code = generate(f.spec, f.rate, "rust");

        char msg[128];
        std::snprintf(msg, sizeof(msg), "rust/%s: non-empty", f.spec);
        CHECK(!code.empty(), msg);

        std::snprintf(msg, sizeof(msg), "rust/%s: pub struct State", f.spec);
        std::string st = std::string("pub struct ") + f.type_name + "State";
        CHECK(contains(code, st.c_str()), msg);

        std::snprintf(msg, sizeof(msg), "rust/%s: fn step", f.spec);
        CHECK(contains(code, "fn step"), msg);
    }
}

static void test_matlab()
{
    for (int i = 0; i < N_FILTERS; ++i) {
        const auto& f = k_filters[i];
        auto code = generate(f.spec, f.rate, "matlab");

        char msg[128];
        std::snprintf(msg, sizeof(msg), "matlab/%s: non-empty", f.spec);
        CHECK(!code.empty(), msg);

        std::snprintf(msg, sizeof(msg), "matlab/%s: function header", f.spec);
        std::string fn = std::string("function y = ") + f.ident;
        CHECK(contains(code, fn.c_str()), msg);

        std::snprintf(msg, sizeof(msg), "matlab/%s: persistent", f.spec);
        CHECK(contains(code, "persistent"), msg);
    }
}

static void test_matlab_aliases()
{
    auto a = generate(k_filters[0].spec, k_filters[0].rate, "matlab");
    auto b = generate(k_filters[0].spec, k_filters[0].rate, "octave");
    auto c = generate(k_filters[0].spec, k_filters[0].rate, "m");
    CHECK(a == b, "matlab/octave alias");
    CHECK(a == c, "matlab/m alias");
}

static void test_julia()
{
    for (int i = 0; i < N_FILTERS; ++i) {
        const auto& f = k_filters[i];
        auto code = generate(f.spec, f.rate, "julia");

        char msg[128];
        std::snprintf(msg, sizeof(msg), "julia/%s: non-empty", f.spec);
        CHECK(!code.empty(), msg);

        std::snprintf(msg, sizeof(msg), "julia/%s: mutable struct", f.spec);
        std::string st = std::string("mutable struct ") + f.type_name + "State";
        CHECK(contains(code, st.c_str()), msg);

        std::snprintf(msg, sizeof(msg), "julia/%s: step!", f.spec);
        CHECK(contains(code, "step!"), msg);
    }
}

static void test_verilog()
{
    for (int i = 0; i < N_FILTERS; ++i) {
        const auto& f = k_filters[i];
        auto code = generate(f.spec, f.rate, "verilog");

        char msg[128];
        std::snprintf(msg, sizeof(msg), "verilog/%s: non-empty", f.spec);
        CHECK(!code.empty(), msg);

        std::snprintf(msg, sizeof(msg), "verilog/%s: module decl", f.spec);
        std::string mod = std::string("module ") + f.ident;
        CHECK(contains(code, mod.c_str()), msg);

        std::snprintf(msg, sizeof(msg), "verilog/%s: always block", f.spec);
        CHECK(contains(code, "always"), msg);
    }
}

static void test_systemverilog()
{
    for (int i = 0; i < N_FILTERS; ++i) {
        const auto& f = k_filters[i];
        auto code = generate(f.spec, f.rate, "systemverilog");

        char msg[128];
        std::snprintf(msg, sizeof(msg), "sv/%s: non-empty", f.spec);
        CHECK(!code.empty(), msg);

        std::snprintf(msg, sizeof(msg), "sv/%s: module decl", f.spec);
        std::string mod = std::string("module ") + f.ident;
        CHECK(contains(code, mod.c_str()), msg);

        std::snprintf(msg, sizeof(msg), "sv/%s: logic type", f.spec);
        CHECK(contains(code, "logic"), msg);
    }
}

// ── SIMD matrix (c99 only) ────────────────────────────────────────────────────

static void test_simd_matrix()
{
    const char* spec = "LpBu4/1000";
    double rate = 44100.0;

    struct { fidgen::SimdLevel lvl; const char* token; const char* name; } cases[] = {
        { fidgen::SimdLevel::None, "_step",            "scalar" },
        { fidgen::SimdLevel::Neon, "float64x2_t",      "neon"   },
        { fidgen::SimdLevel::Sse2, "__m128d",           "sse2"   },
        { fidgen::SimdLevel::Avx2, "__m256d",           "avx2"   },
        { fidgen::SimdLevel::Auto, "__ARM_NEON",        "auto"   },
    };

    for (auto& tc : cases) {
        auto code = generate(spec, rate, "c99", nullptr, tc.lvl);
        char msg[128];
        std::snprintf(msg, sizeof(msg), "c99/simd/%s: non-empty", tc.name);
        CHECK(!code.empty(), msg);
        std::snprintf(msg, sizeof(msg), "c99/simd/%s: contains '%s'", tc.name, tc.token);
        CHECK(contains(code, tc.token), msg);
    }
}

// ── C API smoke test ──────────────────────────────────────────────────────────

static void test_c_api()
{
    // version
    {
        const char* v = fidgen_version();
        CHECK(v != nullptr,   "capi: version non-null");
        CHECK(v[0] != '\0',   "capi: version non-empty");
    }

    // error_str
    {
        CHECK(std::strcmp(fidgen_error_str(FIDGEN_OK), "OK") == 0,
              "capi: error_str OK");
        CHECK(std::strcmp(fidgen_error_str(FIDGEN_ERR_UNKNOWN_LANG),
                          "unknown language key") == 0,
              "capi: error_str UNKNOWN_LANG");
    }

    // successful generate
    {
        char* code = nullptr;
        fidgen_error_t rc = fidgen_generate(
            "LpBu4/1000", 44100.0, "c99", nullptr, nullptr, &code);
        CHECK(rc == FIDGEN_OK,       "capi: generate LpBu4/1000 c99 -> OK");
        CHECK(code != nullptr,       "capi: generate returns non-null code");
        CHECK(code[0] != '\0',       "capi: generate returns non-empty code");
        fidgen_free(code);
    }

    // unknown language
    {
        char* code = nullptr;
        fidgen_error_t rc = fidgen_generate(
            "LpBu4/1000", 44100.0, "brainfuck", nullptr, nullptr, &code);
        CHECK(rc == FIDGEN_ERR_UNKNOWN_LANG, "capi: unknown lang -> ERR_UNKNOWN_LANG");
        CHECK(code == nullptr,               "capi: unknown lang -> out_code is NULL");
    }

    // options round-trip
    {
        fidgen_options_t opts = { FIDGEN_SIMD_NEON, 1, 0, 24, -1 };
        char* code = nullptr;
        fidgen_error_t rc = fidgen_generate(
            "LpBu4/1000", 44100.0, "c99", "lpfilter", &opts, &code);
        CHECK(rc == FIDGEN_OK,          "capi: generate with NEON opts -> OK");
        CHECK(contains(std::string(code ? code : ""), "float64x2_t"),
              "capi: generate with NEON -> contains float64x2_t");
        fidgen_free(code);
    }

    // custom name
    {
        char* code = nullptr;
        fidgen_error_t rc = fidgen_generate(
            "LpBu4/1000", 44100.0, "python", "myfilter", nullptr, &code);
        CHECK(rc == FIDGEN_OK, "capi: generate with custom name -> OK");
        CHECK(contains(std::string(code ? code : ""), "myfilter"),
              "capi: custom name appears in output");
        fidgen_free(code);
    }
}

// ── main ─────────────────────────────────────────────────────────────────────

int main()
{
    test_c99();
    test_cpp20();
    test_python();
    test_python_alias();
    test_rust();
    test_matlab();
    test_matlab_aliases();
    test_julia();
    test_verilog();
    test_systemverilog();
    test_simd_matrix();
    test_c_api();

    std::printf("fidgen integration tests: %d passed, %d failed\n",
                g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
