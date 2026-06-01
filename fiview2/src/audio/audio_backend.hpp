// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig
#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fiview2 {

using AudioCallback = std::function<void(const float* in, float* out, int frames)>;

class AudioBackend {
public:
    virtual ~AudioBackend() = default;

    virtual bool start(double rate, int frames_per_buf,
                       AudioCallback cb) = 0;
    virtual void stop() = 0;
    virtual bool running() const = 0;

    virtual std::vector<std::string> input_devices()  const = 0;
    virtual std::vector<std::string> output_devices() const = 0;
    virtual bool select_input(int idx)  = 0;
    virtual bool select_output(int idx) = 0;

    // Input level in dBFS (peak, updated every callback)
    virtual float input_level_db() const = 0;

    static std::unique_ptr<AudioBackend> create();
};

} // namespace fiview2
