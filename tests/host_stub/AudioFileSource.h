// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Layer812

#pragma once
#include <stdint.h>
class AudioFileSource {
public:
 virtual ~AudioFileSource(){};
 virtual uint32_t read(void*,uint32_t){return 0;}
 virtual bool seek(int32_t,int){return false;}
 virtual bool isOpen(){return false;}
 virtual uint32_t getSize(){return 0;}
};
