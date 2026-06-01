// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig <Josbrig@simbrig.de>
//
// fidgen_c_api.cpp — C linkage wrapper for the fidgen C++ generator

#include <fidgen/fidgen.h>
#include <fidgen/filter_descriptor.hpp>
#include <fidgen/generator.hpp>
#include <fidgen/version.hpp>

#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>

static const fidgen_options_t k_default_opts = {
    FIDGEN_SIMD_NONE,   // simd
    1,                  // with_guard
    0,                  // with_test
    24,                 // fpga_bits
    -1,                 // fpga_frac
};

static fidgen::SimdLevel simd_from_c(fidgen_simd_t s) noexcept
{
    switch (s) {
        case FIDGEN_SIMD_NEON: return fidgen::SimdLevel::Neon;
        case FIDGEN_SIMD_SSE2: return fidgen::SimdLevel::Sse2;
        case FIDGEN_SIMD_AVX2: return fidgen::SimdLevel::Avx2;
        case FIDGEN_SIMD_AUTO: return fidgen::SimdLevel::Auto;
        default:               return fidgen::SimdLevel::None;
    }
}

extern "C" {

fidgen_error_t fidgen_generate(
    const char*             spec,
    double                  rate,
    const char*             lang,
    const char*             name,
    const fidgen_options_t* opts,
    char**                  out_code)
{
    if (out_code)
        *out_code = nullptr;

    if (!out_code || !spec || !lang)
        return FIDGEN_ERR_DESIGN_FAILED;

    if (!opts)
        opts = &k_default_opts;

    // ── Language key ─────────────────────────────────────────────────────────
    std::unique_ptr<fidgen::Generator> gen;
    try {
        gen = fidgen::Generator::create(lang);
    } catch (const std::invalid_argument&) {
        return FIDGEN_ERR_UNKNOWN_LANG;
    }

    // ── Filter design + generate ──────────────────────────────────────────────
    fidgen::GeneratorOptions gopts;
    gopts.with_guard = (opts->with_guard != 0);
    gopts.with_test  = (opts->with_test  != 0);
    gopts.fpga_bits  = opts->fpga_bits;
    gopts.fpga_frac  = opts->fpga_frac;
    gopts.simd       = simd_from_c(opts->simd);

    std::ostringstream oss;
    try {
        auto desc = fidgen::FilterDescriptor::from_spec(
            spec, rate, -1.0, -1.0, name ? name : "");
        gen->generate(oss, desc, gopts);
    } catch (const std::exception&) {
        return FIDGEN_ERR_DESIGN_FAILED;
    }

    const std::string& code = oss.str();
    char* buf = static_cast<char*>(std::malloc(code.size() + 1));
    if (!buf)
        return FIDGEN_ERR_OUT_OF_MEMORY;

    std::memcpy(buf, code.data(), code.size() + 1);
    *out_code = buf;
    return FIDGEN_OK;
}

void fidgen_free(char* code)
{
    std::free(code);
}

const char* fidgen_version(void)
{
    return fidgen::k_version;
}

const char* fidgen_error_str(fidgen_error_t err)
{
    switch (err) {
        case FIDGEN_OK:                return "OK";
        case FIDGEN_ERR_UNKNOWN_LANG:  return "unknown language key";
        case FIDGEN_ERR_DESIGN_FAILED: return "filter design failed";
        case FIDGEN_ERR_UNSTABLE:      return "filter is unstable";
        case FIDGEN_ERR_OUT_OF_MEMORY: return "out of memory";
        default:                       return "unknown error";
    }
}

} // extern "C"
