// SPDX-License-Identifier: GPL-3.0-or-later
// Two independent physical-key click detectors. Uses only unsigned elapsed
// time, so millis() wraparound does not corrupt a short click window.
#pragma once
#include <cstdint>

class ButtonClicks {
public:
  static constexpr uint32_t kDoubleWindowMs = 360;

  // Return true exactly once for a second release in the click window.
  bool onRelease(uint32_t nowMs) {
    if (pending_ && uint32_t(nowMs - lastReleaseMs_) <= kDoubleWindowMs) {
      pending_ = false;
      return true;
    }
    pending_ = true;
    lastReleaseMs_ = nowMs;
    return false;
  }

  // First click becomes a single only AFTER the double-click window.
  // Call this before onRelease() each UI loop, to expire old singles.
  bool singleDue(uint32_t nowMs) {
    if (pending_ && uint32_t(nowMs - lastReleaseMs_) > kDoubleWindowMs) {
      pending_ = false;
      return true;
    }
    return false;
  }

private:
  bool pending_ = false;
  uint32_t lastReleaseMs_ = 0;
};
