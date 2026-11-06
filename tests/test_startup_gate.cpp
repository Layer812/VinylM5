// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Layer812

#include "startup_gate.hpp"
#include <cassert>
#include <atomic>
#include <thread>
#include <iostream>
int main() {
  using startupgate::canStartPlayback;
  assert(!canStartPlayback(false,false));
  assert(!canStartPlayback(true,false));  // no audio before art is displayed
  assert(!canStartPlayback(false,true));  // no audio before PCM is primed
  assert(canStartPlayback(true,true));
  // Release/acquire handoff used between the UI and audio tasks.
  std::atomic<bool> visible{false};
  int preparedLabel=0;
  std::thread producer([&]{preparedLabel=42;visible.store(true,std::memory_order_release);});
  while(!visible.load(std::memory_order_acquire))std::this_thread::yield();
  assert(preparedLabel==42);
  producer.join();
  std::cout << "R9 artwork-first playback gate PASS\n";
}
