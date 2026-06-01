#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Copyright (C) 2025-2026 Kai Dieki
# install-hooks.sh — Symlinkt alle Projekt-Hooks nach .git/hooks/
#
# Aufruf: bash scripts/install-hooks.sh
# Run once after git clone.

set -euo pipefail

GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'

REPO_ROOT="$(git rev-parse --show-toplevel)"
HOOKS_SRC="${REPO_ROOT}/scripts/hooks"
HOOKS_DST="${REPO_ROOT}/.git/hooks"

if [[ ! -d "$HOOKS_SRC" ]]; then
    echo "Fehler: ${HOOKS_SRC} nicht gefunden." >&2
    exit 1
fi

installed=0
for hook in "$HOOKS_SRC"/*; do
    name="$(basename "$hook")"
    target="${HOOKS_DST}/${name}"

    if [[ -L "$target" && "$(readlink "$target")" == "$hook" ]]; then
        echo -e "${YELLOW}[skip]${NC}    $name — bereits installiert"
        continue
    fi

    if [[ -e "$target" ]]; then
        mv "$target" "${target}.bak"
        echo -e "${YELLOW}[backup]${NC}  $name → ${name}.bak"
    fi

    ln -s "$hook" "$target"
    chmod +x "$hook"
    echo -e "${GREEN}[install]${NC} $name"
    (( installed++ )) || true
done

echo
if (( installed > 0 )); then
    echo -e "${GREEN}$installed Hook(s) installiert.${NC}"
else
    echo "Alle Hooks bereits installiert."
fi
