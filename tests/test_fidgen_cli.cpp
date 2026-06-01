// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig <Josbrig@simbrig.de>
//
// test_fidgen_cli — CLI behaviour and generator completeness
//
// Ersetzt scripts/test-fidgen.sh durch echte C++-Tests.
//
// Kategorien:
//   Meta        — --version, --list-langs, --check (cli_main + rdbuf-Capture)
//   Generatoren — alle 8 Sprachen + alle Aliase (Generator-API direkt)
//   Filtertypen — HP, BP, BS, Bessel, Chebyshev, Ordnung 1/8
//   Optionen    — -r, -n, --no-guard, --fpga-bits, --fpga-frac
//   SIMD        — none/neon/sse2/avx2/auto + Makro-Inhalte
//   Dateiausgabe — -o FILE via cli_main
//   Fehlerbehandlung — unbekannte Option, fehlendes SPEC

#include <cli.hpp>
#include <fidgen/generator.hpp>
#include <fidgen/filter_descriptor.hpp>
#include <fidgen/version.hpp>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// ── Testframework ────────────────────────────────────────────────────────────

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { ++g_pass; } \
    else { ++g_fail; \
        std::fprintf(stderr, "FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); } \
} while(0)

// ── Hilfsfunktionen ──────────────────────────────────────────────────────────

// Redirect stdout to ostringstream, call fn, return content.
static std::string capture_cout(int (*fn)(int, char**),
                                 std::vector<const char*> args)
{
    std::vector<std::string> strs(args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& s : strs) argv.push_back(s.data());

    std::ostringstream oss;
    auto* old = std::cout.rdbuf(oss.rdbuf());
    fn(static_cast<int>(argv.size()), argv.data());
    std::cout.rdbuf(old);
    return oss.str();
}

// Stderr auf ostringstream umleiten.
static std::string capture_cerr(int (*fn)(int, char**),
                                  std::vector<const char*> args)
{
    std::vector<std::string> strs(args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& s : strs) argv.push_back(s.data());

    std::ostringstream oss;
    auto* old = std::cerr.rdbuf(oss.rdbuf());
    fn(static_cast<int>(argv.size()), argv.data());
    std::cerr.rdbuf(old);
    return oss.str();
}

// Call cli_main, return exit code (stdout/stderr discarded).
static int invoke_cli(std::vector<const char*> args)
{
    std::vector<std::string> strs(args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& s : strs) argv.push_back(s.data());

    std::ostringstream devnull;
    auto* o = std::cout.rdbuf(devnull.rdbuf());
    auto* e = std::cerr.rdbuf(devnull.rdbuf());
    int rc = fidgen::cli_main(static_cast<int>(argv.size()), argv.data());
    std::cout.rdbuf(o);
    std::cerr.rdbuf(e);
    return rc;
}

// Generate code for spec+lang, return it.
static std::string gen(const char* spec, double rate, const char* lang,
                        fidgen::GeneratorOptions opts = {})
{
    auto desc = fidgen::FilterDescriptor::from_spec(spec, rate);
    auto g    = fidgen::Generator::create(lang);
    std::ostringstream oss;
    g->generate(oss, desc, opts);
    return oss.str();
}

static bool contains(const std::string& s, const char* needle)
{
    return s.find(needle) != std::string::npos;
}

// ── 1. Meta ──────────────────────────────────────────────────────────────────

static void test_meta()
{
    // --version
    {
        auto out = capture_cout(fidgen::cli_main, {"fidgen", "--version"});
        CHECK(contains(out, "fidgen"),        "version: contains 'fidgen'");
        CHECK(contains(out, fidgen::k_version),"version: contains version string");
    }

    // --list-langs
    {
        auto out = capture_cout(fidgen::cli_main, {"fidgen", "--list-langs"});
        CHECK(contains(out, "c99"),           "list-langs: c99");
        CHECK(contains(out, "cpp20"),         "list-langs: cpp20");
        CHECK(contains(out, "python"),        "list-langs: python");
        CHECK(contains(out, "rust"),          "list-langs: rust");
        CHECK(contains(out, "matlab"),        "list-langs: matlab");
        CHECK(contains(out, "julia"),         "list-langs: julia");
        CHECK(contains(out, "verilog"),       "list-langs: verilog");
        CHECK(contains(out, "systemverilog"), "list-langs: systemverilog");
    }

    // --check → stderr, contains stability status
    {
        auto err = capture_cerr(fidgen::cli_main,
                                {"fidgen", "--check", "LpBu4/1000"});
        CHECK(contains(err, "Stable"),        "check: contains 'Stable'");
        CHECK(contains(err, "LpBu4/1000"),    "check: contains spec");
    }

    // --help → exit 0
    {
        int rc = invoke_cli({"fidgen", "--help"});
        CHECK(rc == 0, "help: exit 0");
    }
}

// ── 2. Sprachgeneratoren ─────────────────────────────────────────────────────

static void test_generators()
{
    const char* spec = "LpBu4/1000";
    const double rate = 44100.0;

    struct { const char* lang; const char* token; } cases[] = {
        { "c99",          "lpbu4_1000_step"   },
        { "cpp20",        "Lpbu41000Filter"    },
        { "python",       "class Lpbu41000"    },
        { "rust",         "pub struct"         },
        { "matlab",       "function y ="       },
        { "julia",        "mutable struct"     },
        { "verilog",      "module lpbu4_1000"  },
        { "systemverilog","module lpbu4_1000"  },
    };
    for (auto& tc : cases) {
        auto code = gen(spec, rate, tc.lang);
        char msg[80];
        std::snprintf(msg, sizeof(msg), "generator/%s: non-empty", tc.lang);
        CHECK(!code.empty(), msg);
        std::snprintf(msg, sizeof(msg), "generator/%s: contains '%s'", tc.lang, tc.token);
        CHECK(contains(code, tc.token), msg);
    }
}

// ── 3. Sprach-Aliase ─────────────────────────────────────────────────────────

static void test_aliases()
{
    struct { const char* canonical; const char* alias; } pairs[] = {
        { "c99",          "c"      },
        { "python",       "py"     },
        { "rust",         "rs"     },
        { "matlab",       "octave" },
        { "matlab",       "m"      },
        { "julia",        "jl"     },
        { "verilog",      "v"      },
        { "systemverilog","sv"     },
    };
    for (auto& p : pairs) {
        auto a = gen("LpBu4/1000", 44100.0, p.canonical);
        auto b = gen("LpBu4/1000", 44100.0, p.alias);
        char msg[80];
        std::snprintf(msg, sizeof(msg), "alias: %s == %s", p.canonical, p.alias);
        CHECK(a == b, msg);
    }
}

// ── 4. Filtertypen ───────────────────────────────────────────────────────────

static void test_filter_types()
{
    struct { const char* spec; double rate; } cases[] = {
        { "LpBu4/1000",     44100.0 },
        { "HpBu4/8000",     44100.0 },
        { "BpBu4/500-2000", 44100.0 },
        { "BsBu4/500-2000", 44100.0 },
        { "LpBe4/1000",     44100.0 },
        { "LpCh4/-1/1000",  44100.0 },
        { "LpBu8/1000",     44100.0 },
        { "LpBu1/1000",     44100.0 },
        { "LpBl/1000",      44100.0 },  // FIR Blackman window
        { "LpHm/1000",      44100.0 },  // FIR Hamming window
        { "LpHn/1000",      44100.0 },  // FIR Hann window
        { "LpBa/1000",      44100.0 },  // FIR Bartlett window
    };
    for (auto& tc : cases) {
        auto code = gen(tc.spec, tc.rate, "c99");
        char msg[80];
        std::snprintf(msg, sizeof(msg), "filter/%s: non-empty", tc.spec);
        CHECK(!code.empty(), msg);
    }
}

// ── 5. Optionen ──────────────────────────────────────────────────────────────

static void test_options()
{
    // Verschiedene Sample-Rate
    {
        auto a = gen("LpBu4/1000", 44100.0, "c99");
        auto b = gen("LpBu4/1000", 48000.0, "c99");
        CHECK(a != b, "option/-r: unterschiedliche Rate → unterschiedlicher Code");
    }

    // Custom name
    {
        auto desc = fidgen::FilterDescriptor::from_spec("LpBu4/1000", 44100.0,
                                                         -1.0, -1.0, "myfilter");
        auto g = fidgen::Generator::create("c99");
        std::ostringstream oss;
        g->generate(oss, desc, {});
        CHECK(contains(oss.str(), "myfilter"), "option/-n: custom name in output");
    }

    // --no-guard
    {
        fidgen::GeneratorOptions with_guard, without;
        with_guard.with_guard = true;
        without.with_guard    = false;
        auto a = gen("LpBu4/1000", 44100.0, "c99", with_guard);
        auto b = gen("LpBu4/1000", 44100.0, "c99", without);
        CHECK(contains(a, "#ifndef"),  "option/with-guard: contains #ifndef");
        CHECK(!contains(b, "#ifndef"), "option/no-guard: no #ifndef");
    }

    // --fpga-bits
    {
        fidgen::GeneratorOptions o16, o32;
        o16.fpga_bits = 16;
        o32.fpga_bits = 32;
        auto a = gen("LpBu4/1000", 44100.0, "verilog", o16);
        auto b = gen("LpBu4/1000", 44100.0, "verilog", o32);
        CHECK(contains(a, "[15:0]"), "fpga-bits/16: 16-bit port");
        CHECK(contains(b, "[31:0]"), "fpga-bits/32: 32-bit port");
    }

    // --fpga-frac (explizit vs. auto — beide erzeugen Code)
    {
        fidgen::GeneratorOptions o_auto, o_20;
        o_auto.fpga_frac = -1;
        o_20.fpga_frac   = 20;
        auto a = gen("LpBu4/1000", 44100.0, "verilog", o_auto);
        auto b = gen("LpBu4/1000", 44100.0, "verilog", o_20);
        CHECK(!a.empty(), "fpga-frac/auto: non-empty");
        CHECK(!b.empty(), "fpga-frac/20: non-empty");
    }
}

// ── 6. SIMD ──────────────────────────────────────────────────────────────────

static void test_simd()
{
    struct {
        fidgen::SimdLevel lvl;
        const char*       name;
        const char*       guard;
    } cases[] = {
        { fidgen::SimdLevel::None, "none", "_step"        },
        { fidgen::SimdLevel::Neon, "neon", "__ARM_NEON"   },
        { fidgen::SimdLevel::Sse2, "sse2", "__SSE2__"     },
        { fidgen::SimdLevel::Avx2, "avx2", "__AVX2__"     },
        { fidgen::SimdLevel::Auto, "auto", "__ARM_NEON"   },
    };
    for (auto& tc : cases) {
        fidgen::GeneratorOptions opts;
        opts.simd = tc.lvl;
        auto code = gen("LpBu4/1000", 44100.0, "c99", opts);
        char msg[80];
        std::snprintf(msg, sizeof(msg), "simd/%s: non-empty", tc.name);
        CHECK(!code.empty(), msg);
        std::snprintf(msg, sizeof(msg), "simd/%s: contains '%s'", tc.name, tc.guard);
        CHECK(contains(code, tc.guard), msg);
    }

    // Auto contains all three guards
    {
        fidgen::GeneratorOptions opts;
        opts.simd = fidgen::SimdLevel::Auto;
        auto code = gen("LpBu4/1000", 44100.0, "c99", opts);
        CHECK(contains(code, "__ARM_NEON"), "simd/auto: ARM_NEON guard");
        CHECK(contains(code, "__SSE2__"),   "simd/auto: SSE2 guard");
        CHECK(contains(code, "__AVX2__"),   "simd/auto: AVX2 guard");
    }
}

// ── 7. Dateiausgabe ──────────────────────────────────────────────────────────

static void test_file_output()
{
    char tmp2[] = "/tmp/fidgen_cli_test_XXXXXX";
    int fd = mkstemp(tmp2);
    if (fd >= 0) close(fd);
    std::string path{tmp2};

    int rc = invoke_cli({"fidgen", "-l", "c99", "LpBu4/1000", "-o", path.c_str()});
    CHECK(rc == 0, "file-output: exit 0");

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
    CHECK(!content.empty(),              "file-output: non-empty");
    CHECK(contains(content, "typedef"),  "file-output: contains typedef");
    CHECK(contains(content, "_step"),    "file-output: contains step function");

    std::remove(path.c_str());
}

// ── 8. Fehlerbehandlung ──────────────────────────────────────────────────────

static void test_errors()
{
    CHECK(invoke_cli({"fidgen"})                    != 0, "error: no args");
    CHECK(invoke_cli({"fidgen", "--bogus"})          != 0, "error: unknown option");
    CHECK(invoke_cli({"fidgen", "-l", "brainfuck",
                      "LpBu4/1000"})                != 0, "error: unknown lang");
}

// ── main ─────────────────────────────────────────────────────────────────────

int main()
{
    test_meta();
    test_generators();
    test_aliases();
    test_filter_types();
    test_options();
    test_simd();
    test_file_output();
    test_errors();

    std::printf("fidgen cli tests: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
