// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig
#include "spectrum.hpp"
#include "kiss_fftr.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace fiview2 {

SpectrumAnalyser::SpectrumAnalyser(int fft_size)
    : fft_size_(fft_size)
{
    ring_.assign(static_cast<size_t>(fft_size), 0.0f);
    mag_db_.assign(static_cast<size_t>(fft_size / 2 + 1), -96.0f);
    fft_cfg_ = kiss_fftr_alloc(fft_size, 0, nullptr, nullptr);
}

SpectrumAnalyser::~SpectrumAnalyser()
{
    kiss_fftr_free(static_cast<kiss_fftr_cfg>(fft_cfg_));
}

void SpectrumAnalyser::push(const float* samples, int n)
{
    for (int i = 0; i < n; ++i) {
        ring_[static_cast<size_t>(ring_write_)] = samples[i];
        ring_write_ = (ring_write_ + 1) % fft_size_;
        ring_fill_  = std::min(ring_fill_ + 1, fft_size_);
    }
    if (ring_fill_ >= fft_size_) compute();
}

void SpectrumAnalyser::compute()
{
    // Apply Hann window and copy to contiguous buffer
    std::vector<kiss_fft_scalar> buf(static_cast<size_t>(fft_size_));
    for (int i = 0; i < fft_size_; ++i) {
        int idx = (ring_write_ - fft_size_ + i + fft_size_) % fft_size_;
        float w = 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(std::numbers::pi) * static_cast<float>(i) / static_cast<float>(fft_size_ - 1)));
        buf[static_cast<size_t>(i)] = ring_[static_cast<size_t>(idx)] * w;
    }

    std::vector<kiss_fft_cpx> out(static_cast<size_t>(fft_size_ / 2 + 1));
    kiss_fftr(static_cast<kiss_fftr_cfg>(fft_cfg_), buf.data(), out.data());

    float scale = 2.0f / static_cast<float>(fft_size_);
    for (int k = 0; k <= fft_size_ / 2; ++k) {
        float re = out[static_cast<size_t>(k)].r * scale;
        float im = out[static_cast<size_t>(k)].i * scale;
        float m  = std::hypot(re, im);
        mag_db_[static_cast<size_t>(k)] = (m > 1e-9f) ? 20.0f * std::log10(m) : -96.0f;
    }
    ring_fill_ = 0;
}

} // namespace fiview2
