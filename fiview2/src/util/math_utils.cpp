// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig
#include "math_utils.hpp"
#include <algorithm>
#include <cmath>

namespace fiview2::math {

float normalize(double v, double lo, double hi)
{
    if (hi <= lo) return 0.0f;
    return static_cast<float>((v - lo) / (hi - lo));
}

float log_normalize(double freq_hz, double lo_hz, double hi_hz)
{
    if (freq_hz <= 0.0 || lo_hz <= 0.0 || hi_hz <= lo_hz) return 0.0f;
    return static_cast<float>(
        std::log(freq_hz / lo_hz) / std::log(hi_hz / lo_hz));
}

double log_denormalize(float t, double lo_hz, double hi_hz)
{
    return lo_hz * std::pow(hi_hz / lo_hz, static_cast<double>(t));
}

float db_to_y(double db, double db_min, double db_max, float plot_h)
{
    double t = (db - db_max) / (db_min - db_max);
    t = std::clamp(t, 0.0, 1.0);
    return static_cast<float>(t) * plot_h;
}

double mag_db(std::complex<double> z)
{
    double m = std::abs(z);
    return (m > 1e-30) ? 20.0 * std::log10(m) : -600.0;
}

bool all_inside_unit_circle(const std::vector<std::complex<double>>& poles,
                             double margin)
{
    for (const auto& p : poles)
        if (std::abs(p) >= margin) return false;
    return true;
}

} // namespace fiview2::math
