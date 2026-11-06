// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Layer812

#pragma once
#include <stdint.h>
#include <stddef.h>
struct esp_partition_t{uint32_t address;uint32_t size;};
#define ESP_PARTITION_TYPE_DATA 1
#define ESP_PARTITION_SUBTYPE_DATA_SPIFFS 2
#define ESP_OK 0
const esp_partition_t* esp_partition_find_first(int,int,const char*);
int esp_partition_read(const esp_partition_t*,uint32_t,void*,uint32_t);
int esp_partition_write(const esp_partition_t*,uint32_t,const void*,uint32_t);
int esp_partition_erase_range(const esp_partition_t*,uint32_t,uint32_t);
