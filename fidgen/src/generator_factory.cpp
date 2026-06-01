// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig <Josbrig@simbrig.de>

#include <fidgen/generator.hpp>

#include <memory>
#include <ostream>
#include <stdexcept>
#include <string_view>

namespace fidgen {

// Forward declarations from individual generator translation units
std::unique_ptr<Generator> make_c99_generator();
std::unique_ptr<Generator> make_cpp20_generator();
std::unique_ptr<Generator> make_python_generator();
std::unique_ptr<Generator> make_rust_generator();
std::unique_ptr<Generator> make_matlab_generator();
std::unique_ptr<Generator> make_julia_generator();
std::unique_ptr<Generator> make_verilog_generator();
std::unique_ptr<Generator> make_systemverilog_generator();

std::unique_ptr<Generator> Generator::create(std::string_view lang)
{
    if (lang == "c99"  || lang == "c")             return make_c99_generator();
    if (lang == "cpp20"|| lang == "c++20"
                       || lang == "cpp"
                       || lang == "cxx")           return make_cpp20_generator();
    if (lang == "python"|| lang == "py")           return make_python_generator();
    if (lang == "rust"  || lang == "rs")           return make_rust_generator();
    if (lang == "matlab"|| lang == "octave"
                       || lang == "m")             return make_matlab_generator();
    if (lang == "julia" || lang == "jl")           return make_julia_generator();
    if (lang == "verilog"|| lang == "v")           return make_verilog_generator();
    if (lang == "systemverilog" || lang == "sv")   return make_systemverilog_generator();

    throw std::invalid_argument(
        std::string{"Unknown language key: '"} + std::string{lang} + "'"
    );
}

void Generator::list_langs(std::ostream& out)
{
    out << "c99         C99 scalar + optional SIMD (NEON/SSE2/AVX2)\n"
        << "cpp20       C++20 header-only class (constexpr coef)\n"
        << "python      Python 3 class (Direct Form II)\n"
        << "rust        Rust struct + impl (no_std compatible)\n"
        << "matlab      MATLAB/Octave .m function file\n"
        << "julia       Julia mutable struct + step!()\n"
        << "verilog     Verilog RTL module (fixed-point pipeline)\n"
        << "systemverilog  SystemVerilog RTL module (logic types)\n";
}

} // namespace fidgen
