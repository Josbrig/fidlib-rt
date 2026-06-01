// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig
#pragma once
#include <complex>
#include <vector>

namespace fiview2::math {

// Map a linear value to [0,1] within [lo, hi]
float normalize(double v, double lo, double hi);

// Log-normalize: value in [lo_hz, hi_hz] → [0,1]
float log_normalize(double freq_hz, double lo_hz, double hi_hz);

// Inverse log-normalize: [0,1] → [lo_hz, hi_hz]
double log_denormalize(float t, double lo_hz, double hi_hz);

// dB value to Y pixel within a plot area
float db_to_y(double db, double db_min, double db_max, float plot_h);

// Complex magnitude in dB
double mag_db(std::complex<double> z);

// Check stability: all poles inside unit circle
bool all_inside_unit_circle(const std::vector<std::complex<double>>& poles,
                             double margin = 1.0);

} // namespace fiview2::math
