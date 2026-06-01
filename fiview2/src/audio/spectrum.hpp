// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig
#pragma once
#include <complex>
#include <vector>

namespace fiview2 {

// Real-valued FFT spectrum analyser using KissFFT.
// Feed audio samples via push(); read magnitude spectrum via magnitude_db().
class SpectrumAnalyser {
public:
    explicit SpectrumAnalyser(int fft_size = 1024);
    ~SpectrumAnalyser();

    // Push new samples; when ring buffer has enough, compute FFT
    void push(const float* samples, int n);

    // Latest magnitude spectrum in dB, size = fft_size/2 + 1
    const std::vector<float>& magnitude_db() const { return mag_db_; }

    int fft_size() const { return fft_size_; }

private:
    void compute();

    int                fft_size_;
    std::vector<float> ring_;
    int                ring_write_ = 0;
    int                ring_fill_  = 0;
    std::vector<float> mag_db_;
    void*              fft_cfg_   = nullptr;
};

} // namespace fiview2
