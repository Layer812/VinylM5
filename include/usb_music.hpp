// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Layer812

#pragma once
#include <stdint.h>
namespace usbmusic {
bool begin();
bool isActive();
bool isEjected();
bool everOpenedByPC();
bool flush();
// Ensures emergency exit does not interrupt active USB writes.
bool writeIdleFor(uint32_t ms);
void stopPresenting();
}
