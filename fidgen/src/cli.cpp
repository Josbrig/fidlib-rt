// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig <Josbrig@simbrig.de>

#include "cli.hpp"

#include <fidgen/filter_descriptor.hpp>
#include <fidgen/generator.hpp>
#include <fidgen/version.hpp>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fidgen {

static void print_usage(const char* prog)
{
    std::cerr <<
        "Usage: " << prog << " [OPTIONS] SPEC\n"
        "\n"
        "Generate digital filter code from a fidlib filter specification.\n"
        "\n"
        "Arguments:\n"
        "  SPEC         fidlib filter spec, e.g. 'LpBu4/1000' or 'BpBu2/500-2000'\n"
        "\n"
        "Options:\n"
        "  -l LANG      Output language (default: c99)\n"
        "               Run --list-langs for supported values.\n"
        "  -r RATE      Sample rate in Hz (default: 44100)\n"
        "  -f FREQ      Corner frequency 0 in Hz (overrides spec-embedded value)\n"
        "  -F FREQ      Corner frequency 1 in Hz (for bandpass/bandstop)\n"
        "  -n NAME      C identifier prefix (default: auto-derived from spec)\n"
        "  -o FILE      Output file (default: stdout)\n"
        "  --simd LEVEL SIMD variant: none|neon|sse2|avx2|auto (c99 only, default: none)\n"
        "  --with-test  Append self-test skeleton\n"
        "  --no-guard   Omit include guard / #pragma once\n"
        "  --fpga-bits N   Fixed-point width for Verilog (default: 24)\n"
        "  --fpga-frac N   Fractional bits (default: auto)\n"
        "  --check      Print filter summary to stderr and exit\n"
        "  --list-langs List supported language keys and exit\n"
        "  --version    Print version and exit\n"
        "  -h, --help   Show this help\n";
}

static SimdLevel parse_simd(const char* s)
{
    if (std::strcmp(s, "none") == 0)  return SimdLevel::None;
    if (std::strcmp(s, "neon") == 0)  return SimdLevel::Neon;
    if (std::strcmp(s, "sse2") == 0)  return SimdLevel::Sse2;
    if (std::strcmp(s, "avx2") == 0)  return SimdLevel::Avx2;
    if (std::strcmp(s, "auto") == 0)  return SimdLevel::Auto;
    throw std::invalid_argument(std::string{"Unknown SIMD level: '"} + s + "'");
}

int cli_main(int argc, char* argv[])
{
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string lang    = "c99";
    std::string outfile;
    std::string name;
    std::string spec;
    double rate  = 44100.0;
    double freq0 = -1.0;
    double freq1 = -1.0;
    bool check   = false;
    GeneratorOptions opts;

    for (int i = 1; i < argc; ++i) {
        std::string arg{argv[i]};

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        }
        if (arg == "--version") {
            std::cout << "fidgen " << k_version << "\n";
            return 0;
        }
        if (arg == "--list-langs") {
            Generator::list_langs(std::cout);
            return 0;
        }
        if (arg == "--check") {
            check = true;
        } else if (arg == "--with-test") {
            opts.with_test = true;
        } else if (arg == "--no-guard") {
            opts.with_guard = false;
        } else if (arg == "-l" && i + 1 < argc) {
            lang = argv[++i];
        } else if (arg == "-r" && i + 1 < argc) {
            rate = std::stod(argv[++i]);
        } else if (arg == "-f" && i + 1 < argc) {
            freq0 = std::stod(argv[++i]);
        } else if (arg == "-F" && i + 1 < argc) {
            freq1 = std::stod(argv[++i]);
        } else if (arg == "-n" && i + 1 < argc) {
            name = argv[++i];
        } else if (arg == "-o" && i + 1 < argc) {
            outfile = argv[++i];
        } else if (arg == "--simd" && i + 1 < argc) {
            opts.simd = parse_simd(argv[++i]);
        } else if (arg == "--fpga-bits" && i + 1 < argc) {
            opts.fpga_bits = std::stoi(argv[++i]);
        } else if (arg == "--fpga-frac" && i + 1 < argc) {
            opts.fpga_frac = std::stoi(argv[++i]);
        } else if (!arg.empty() && arg[0] != '-') {
            if (!spec.empty()) {
                std::cerr << "Error: multiple SPEC arguments given\n";
                return 1;
            }
            spec = arg;
        } else {
            std::cerr << "Error: unknown option '" << arg << "'\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    if (spec.empty()) {
        std::cerr << "Error: SPEC argument required\n";
        print_usage(argv[0]);
        return 1;
    }

    FilterDescriptor desc = FilterDescriptor::from_spec(spec, rate, freq0, freq1, name);

    if (check) {
        std::cerr << desc.summary();
        return 0;
    }

    std::unique_ptr<Generator> gen;
    try {
        gen = Generator::create(lang);
    } catch (const std::invalid_argument& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    if (outfile.empty()) {
        gen->generate(std::cout, desc, opts);
    } else {
        std::ofstream ofs{outfile};
        if (!ofs) {
            std::cerr << "Error: cannot open output file '" << outfile << "'\n";
            return 1;
        }
        gen->generate(ofs, desc, opts);
    }

    return 0;
}

} // namespace fidgen
