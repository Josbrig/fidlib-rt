// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig
//
// fiview2 — Filter Design Workbench
// Entry point.

#include "app.hpp"
#include <cstdio>

int main(int /*argc*/, char** /*argv*/)
{
    fiview2::App app;

    if (!app.init("fiview2 — Filter Design Workbench")) {
        std::fprintf(stderr, "fiview2: failed to initialise window\n");
        return 1;
    }

    app.run();
    return 0;
}
