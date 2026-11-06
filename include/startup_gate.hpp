// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Layer812

#pragma once
// Single truth table shared by the ESP32 audio task and host-side regression.
namespace startupgate {
constexpr bool canStartPlayback(bool pcmPrimed, bool labelPresented) noexcept {
  return pcmPrimed && labelPresented;
}
}
