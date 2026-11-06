// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Layer812

// MSC is deliberately ONLY activated in the separate, rebooted USB DRIVE mode.
// Player never reads the flash while the PC owns it.
#include "usb_music.hpp"
#include "music_flash.hpp"
#include <Arduino.h>
#include <USB.h>
#include <USBMSC.h>
#include <freertos/semphr.h>
#include <esp_partition.h>
#include <atomic>
#include <cstring>
#include <algorithm>

namespace {
USBMSC disk;  // Must exist before USB.begin() (TinyUSB descriptor registration).
SemaphoreHandle_t ioMutex = nullptr;
std::atomic<bool> ejected{false};
std::atomic<bool> active{false};
std::atomic<bool> everAccessed{false};
uint32_t cachedSector = UINT32_MAX;
uint8_t cache[4096];
bool dirty=false;
std::atomic<uint32_t> lastWriteMs{0};
bool flushLocked() {
 if(!dirty)return true;
 const auto* part=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,"spiffs");
 if(!part || cachedSector>=part->size)return false;
 if(esp_partition_erase_range(part,cachedSector,4096)!=ESP_OK)return false;
 if(esp_partition_write(part,cachedSector,cache,4096)!=ESP_OK)return false;
 dirty=false;return true;
}
bool loadLocked(uint32_t base){
 if(base==cachedSector)return true;
 if(!flushLocked())return false;
 if(!musicflash::rawRead(base,cache,sizeof(cache)))return false;
 cachedSector=base;return true;
}
int32_t onRead(uint32_t lba,uint32_t offset,void* dest,uint32_t len){
 if(!active.load() || !dest || uint64_t(lba)*512+offset+len>musicflash::capacityBytes())return -1;
 everAccessed.store(true);
 if(xSemaphoreTake(ioMutex,pdMS_TO_TICKS(2000))!=pdTRUE)return -1;
 uint32_t addr=lba*512+offset, left=len;auto* dst=static_cast<uint8_t*>(dest);bool ok=true;
 while(left){const uint32_t base=addr&~4095u, ix=addr-base,n=std::min(left,4096u-ix);
  if(base==cachedSector)memcpy(dst,cache+ix,n);
  else if(!musicflash::rawRead(addr,dst,n)){ok=false;break;}
  addr+=n;dst+=n;left-=n;
 }
 xSemaphoreGive(ioMutex);
 return ok?int32_t(len):-1;
}
int32_t onWrite(uint32_t lba,uint32_t offset,uint8_t* src,uint32_t len){
 if(!active.load() || !src || uint64_t(lba)*512+offset+len>musicflash::capacityBytes())return -1;
 everAccessed.store(true);
 if(xSemaphoreTake(ioMutex,pdMS_TO_TICKS(2000))!=pdTRUE)return -1;
 uint32_t addr=lba*512+offset,left=len;bool ok=true;
 while(left){uint32_t base=addr&~4095u,ix=addr-base,n=std::min(left,4096u-ix);
  if(!loadLocked(base)){ok=false;break;}
  memcpy(cache+ix,src,n);dirty=true;lastWriteMs.store(millis(),std::memory_order_release);
  addr+=n;src+=n;left-=n;
 }
 xSemaphoreGive(ioMutex);
 return ok?int32_t(len):-1;
}
bool onStartStop(uint8_t,bool start,bool loadEject){
 if(loadEject&&!start){
  if(!ioMutex || xSemaphoreTake(ioMutex,pdMS_TO_TICKS(3000))!=pdTRUE)return false;
  const bool ok=flushLocked();xSemaphoreGive(ioMutex);
  if(!ok)return false;
  active.store(false);disk.mediaPresent(false);
  ejected.store(true);
 }
 return true;
}
}
namespace usbmusic {
bool begin(){
 if(!musicflash::isFat16() || !musicflash::capacityBytes())return false;
 ioMutex=xSemaphoreCreateMutex();if(!ioMutex)return false;
 disk.vendorID("M5STACK");disk.productID("VINYL USB");disk.productRevision("R6");
 disk.onRead(onRead);disk.onWrite(onWrite);disk.onStartStop(onStartStop);
 // USBMSC in Arduino-ESP32 2.0.17 has no isWritable() method.
 // Supplying an onWrite callback makes the medium writable to the host.
 disk.mediaPresent(false);
 if(!disk.begin(musicflash::capacityBytes()/512,512))return false;
 if(!USB.begin())return false;
 active.store(true);disk.mediaPresent(true);ejected.store(false);everAccessed.store(false);
 return true;
}
bool isActive(){return active.load();}
bool isEjected(){return ejected.load();}
bool everOpenedByPC(){return everAccessed.load();}
bool writeIdleFor(uint32_t ms){
 const uint32_t at=lastWriteMs.load(std::memory_order_acquire);
 return at==0 || (millis()-at)>=ms;
}
bool flush(){
 if(!ioMutex)return false;
 if(xSemaphoreTake(ioMutex,pdMS_TO_TICKS(2000))!=pdTRUE)return false;
 bool r=flushLocked();xSemaphoreGive(ioMutex);return r;
}
void stopPresenting(){
 active.store(false);disk.mediaPresent(false);flush();
}
}
