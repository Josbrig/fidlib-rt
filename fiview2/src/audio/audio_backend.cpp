// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig
#include "audio_backend.hpp"

#ifdef FIVIEW2_AUDIO_ENABLED
#include <portaudio.h>
#include <cmath>

namespace fiview2 {

class PortAudioBackend final : public AudioBackend {
public:
    PortAudioBackend()  { Pa_Initialize(); }
    ~PortAudioBackend() { stop(); Pa_Terminate(); }

    bool start(double rate, int frames, AudioCallback cb) override
    {
        cb_     = std::move(cb);
        rate_   = rate;
        frames_ = frames;
        level_  = -96.0f;

        PaStreamParameters inp, out;
        inp.device                    = in_dev_ < 0 ? Pa_GetDefaultInputDevice()  : in_dev_;
        out.device                    = out_dev_< 0 ? Pa_GetDefaultOutputDevice() : out_dev_;
        inp.channelCount = out.channelCount = 1;
        inp.sampleFormat = out.sampleFormat = paFloat32;
        inp.suggestedLatency = Pa_GetDeviceInfo(inp.device)->defaultLowInputLatency;
        out.suggestedLatency = Pa_GetDeviceInfo(out.device)->defaultLowOutputLatency;
        inp.hostApiSpecificStreamInfo = out.hostApiSpecificStreamInfo = nullptr;

        PaError err = Pa_OpenStream(&stream_, &inp, &out,
            rate, static_cast<unsigned long>(frames),
            paClipOff, &PortAudioBackend::pa_callback, this);
        if (err != paNoError) return false;
        return Pa_StartStream(stream_) == paNoError;
    }

    void stop() override
    {
        if (stream_) { Pa_StopStream(stream_); Pa_CloseStream(stream_); stream_ = nullptr; }
    }

    bool running() const override { return stream_ && Pa_IsStreamActive(stream_); }

    std::vector<std::string> input_devices() const override
    {
        std::vector<std::string> out;
        int n = Pa_GetDeviceCount();
        for (int i = 0; i < n; ++i) {
            const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
            if (info && info->maxInputChannels > 0)
                out.push_back(info->name);
        }
        return out;
    }

    std::vector<std::string> output_devices() const override
    {
        std::vector<std::string> out;
        int n = Pa_GetDeviceCount();
        for (int i = 0; i < n; ++i) {
            const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
            if (info && info->maxOutputChannels > 0)
                out.push_back(info->name);
        }
        return out;
    }

    bool select_input(int idx)  override { in_dev_  = idx; return true; }
    bool select_output(int idx) override { out_dev_ = idx; return true; }
    float input_level_db() const override { return level_.load(); }

private:
    static int pa_callback(const void* in_buf, void* out_buf,
        unsigned long frames, const PaStreamCallbackTimeInfo*,
        PaStreamCallbackFlags, void* user)
    {
        auto* self = static_cast<PortAudioBackend*>(user);
        const float* in  = static_cast<const float*>(in_buf);
        float*       out = static_cast<float*>(out_buf);

        // Update peak level
        float peak = 0.0f;
        for (unsigned long i = 0; i < frames; ++i)
            if (std::fabs(in[i]) > peak) peak = std::fabs(in[i]);
        float db = (peak > 1e-9f) ? 20.0f*std::log10(peak) : -96.0f;
        self->level_.store(db);

        if (self->cb_)
            self->cb_(in, out, static_cast<int>(frames));
        else
            for (unsigned long i = 0; i < frames; ++i) out[i] = in[i];
        return paContinue;
    }

    PaStream*          stream_   = nullptr;
    AudioCallback      cb_;
    double             rate_     = 44100.0;
    int                frames_   = 256;
    int                in_dev_   = -1;
    int                out_dev_  = -1;
    std::atomic<float> level_{-96.0f};
};

std::unique_ptr<AudioBackend> AudioBackend::create()
{
    return std::make_unique<PortAudioBackend>();
}

} // namespace fiview2

#else  // !FIVIEW2_AUDIO_ENABLED

namespace fiview2 {

class NullAudioBackend final : public AudioBackend {
public:
    bool start(double, int, AudioCallback) override { return false; }
    void stop() override {}
    bool running() const override { return false; }
    std::vector<std::string> input_devices()  const override { return {}; }
    std::vector<std::string> output_devices() const override { return {}; }
    bool select_input(int)  override { return false; }
    bool select_output(int) override { return false; }
    float input_level_db() const override { return -96.0f; }
};

std::unique_ptr<AudioBackend> AudioBackend::create()
{
    return std::make_unique<NullAudioBackend>();
}

} // namespace fiview2

#endif
