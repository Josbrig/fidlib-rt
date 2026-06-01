// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig
#pragma once
#include "../filter_state.hpp"

namespace fiview2::panels {
void draw_stability_panel(FilterState& state);
void draw_guided_mode(FilterState& state);  // Beginner wizard
} // namespace fiview2::panels
