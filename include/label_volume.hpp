// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Layer812

#pragma once
#include <cstdint>
#include <cstdlib>

namespace vinyl {
// Touch labels act as invisible up/down buttons. No label movement or
// scratch-gesture may inadvertently adjust the volume.
class LabelVolumeGesture {
public:
  enum class Action : uint8_t { None, Up, Down };
  Action update(bool pressed, int x, int y, uint32_t now_ms, int cx, int cy) {
    if (pressed && !was_pressed_) {
      was_pressed_ = true;
      down_ms_ = now_ms;
      start_x_ = x;
      start_y_ = y;
      const int dx=x-cx,dy=y-cy;
      const int rr=dx*dx+dy*dy;
      const bool label_ring = rr >= 34*34 && rr <= 94*94;
      // Ignore the spindle hub and the small band around the equator.
      armed_ = label_ring && (dy <= -14 || dy >= 14);
    } else if (pressed && was_pressed_ && armed_) {
      if (std::abs(x-start_x_) > 12 || std::abs(y-start_y_) > 12 ||
          uint32_t(now_ms-down_ms_) > 300) armed_ = false;
    } else if (!pressed && was_pressed_) {
      was_pressed_ = false;
      const bool click = armed_ && uint32_t(now_ms-down_ms_) <= 300;
      armed_ = false;
      if (click) return start_y_ < cy ? Action::Up : Action::Down;
    }
    return Action::None;
  }
private:
  bool was_pressed_ = false;
  bool armed_ = false;
  int start_x_ = 0, start_y_ = 0;
  uint32_t down_ms_ = 0;
};
} // namespace vinyl
