// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig
#pragma once
#include "../filter_state.hpp"

namespace fiview2::panels {

// Frequency response panel: magnitude + optional group-delay overlay.
// Shows cascade response in white, individual filters in muted colours,
// and up to 4 frozen compare snapshots.
void draw_freq_plot(FilterState& state);

} // namespace fiview2::panels
