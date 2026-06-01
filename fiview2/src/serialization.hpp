// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig
#pragma once
#include "filter_state.hpp"
#include <string>

namespace fiview2 {

// Save/load full application state as JSON
bool save_state(const FilterState& state, const std::string& path);
bool load_state(FilterState& state,       const std::string& path);

// Encode/decode state to/from Base64 URL fragment (for sharing)
std::string state_to_base64(const FilterState& state);
bool        base64_to_state(FilterState& state, const std::string& b64);

} // namespace fiview2
