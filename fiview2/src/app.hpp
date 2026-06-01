// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig
#pragma once
#include "filter_state.hpp"
#include "audio/audio_backend.hpp"
#include <imtile/imtile.hpp>
#include <memory>
#include <optional>
#include <string>

struct GLFWwindow;

namespace fiview2 {

class App {
public:
    App();
    ~App();

    bool init(const char* title = "fiview2 — Filter Design Workbench");
    void run();      // blocking main loop

private:
    void render_frame();
    void draw_menu_bar();
    void open_file_dialog(bool save);

    GLFWwindow*                    window_  = nullptr;
    FilterState                    state_;
    std::unique_ptr<AudioBackend>  audio_;
    std::optional<imtile::Cockpit> cockpit_;
    std::string                    current_file_;
    bool                           show_guided_ = false;
};

} // namespace fiview2
