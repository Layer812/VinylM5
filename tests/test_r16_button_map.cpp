#include "button_clicks.hpp"
#include <cassert>
#include <cstdint>
#include <cstdio>

int main() {
  // Right single: no immediate pause and no second action after it fires.
  ButtonClicks right;
  assert(!right.onRelease(100));
  assert(!right.singleDue(460));
  assert(right.singleDue(461));
  assert(!right.singleDue(999));
  // Right double: only next track, never pause/unpause.
  assert(!right.onRelease(1000));
  assert(right.onRelease(1360));
  assert(!right.singleDue(2000));
  // Left double: one random-selection event, no folder switch.
  ButtonClicks left;
  assert(!left.onRelease(2000));
  assert(left.onRelease(2190));
  assert(!left.singleDue(2500));
  // Left single: next folder after the double window.
  assert(!left.onRelease(2600));
  assert(left.singleDue(2961));
  assert(!left.singleDue(2962));
  // Independent keys cannot consume each other's press state.
  assert(!right.onRelease(3000));
  assert(!left.onRelease(3001));
  assert(right.onRelease(3160));
  assert(left.onRelease(3190));
  // Late second press is a new first press after single is dispatched.
  assert(!right.onRelease(4000));
  assert(right.singleDue(4361));
  assert(!right.onRelease(4400));
  assert(right.singleDue(4761));
  // millis() wraparound.
  ButtonClicks wrap;
  assert(!wrap.onRelease(UINT32_MAX - 10));
  assert(wrap.onRelease(80));
  assert(!wrap.singleDue(500));
  std::puts("PASS R16 FIX1: independent single/double mapping, no accidental folder switch or pause, 360ms window and wraparound");
}
