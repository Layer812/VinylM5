// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Layer812

#include <cassert>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>
#include "music_flash.hpp"
static std::vector<uint8_t> flash(0xcf0000,0xff);
static esp_partition_t partition{0x310000,0xcf0000};
const esp_partition_t* esp_partition_find_first(int,int,const char*){return &partition;}
int esp_partition_read(const esp_partition_t*,uint32_t addr,void* dst,uint32_t len){if(uint64_t(addr)+len>flash.size())return -1;memcpy(dst,flash.data()+addr,len);return 0;}
int esp_partition_write(const esp_partition_t*,uint32_t addr,const void* src,uint32_t len){if(uint64_t(addr)+len>flash.size())return -1;for(uint32_t i=0;i<len;i++)flash[addr+i]&=static_cast<const uint8_t*>(src)[i];return 0;}
int esp_partition_erase_range(const esp_partition_t*,uint32_t addr,uint32_t len){if(uint64_t(addr)+len>flash.size())return -1;memset(flash.data()+addr,0xff,len);return 0;}
static uint16_t rd16(uint32_t at){return flash[at] | (flash[at+1]<<8);}
static void put16(uint32_t p,uint16_t n){flash[p]=n;flash[p+1]=n>>8;}
static void put32(uint32_t p,uint32_t n){put16(p,n);put16(p+2,n>>16);}
static void name(uint8_t* dst,const char* base,const char* ext,uint8_t att,uint16_t cl,uint32_t sz){
 memset(dst,' ',11);memcpy(dst,base,strlen(base));memcpy(dst+8,ext,strlen(ext));dst[11]=att;
 dst[26]=cl;dst[27]=cl>>8;dst[28]=sz;dst[29]=sz>>8;dst[30]=sz>>16;dst[31]=sz>>24;
}
int main(){
 assert(musicflash::begin());
 assert(musicflash::formatFAT16());
 assert(musicflash::isFat16());
 const uint16_t rootSec=rd16(14)+flash[16]*rd16(22),spc=flash[13];
 const uint32_t root=rootSec*512;
 uint8_t* d=flash.data()+root;
 name(d,"ONE","MP3",0x20,2,6);
 // UTF-16 FAT long filename for the folder "音楽".
 uint8_t* lfn=d+32;memset(lfn,0xff,32);lfn[0]=0x41;lfn[11]=0x0f;lfn[13]=0x11;
 lfn[1]=0xf3;lfn[2]=0x97;lfn[3]=0x7d;lfn[4]=0x69;lfn[5]=0;lfn[6]=0;
 name(d+64,"ALBUM","",0x10,3,0);
 name(d+96,"TWO","MP3",0x20,4,7);
 d[128]=0;
 const uint32_t dataStart=(rootSec+32)*512;
 uint32_t album=dataStart+uint32_t(spc)*512;
 name(flash.data()+album,"SONG","MP3",0x20,5,12);
 name(flash.data()+album+32,"COVER","PNG",0x20,6,28);
 flash[album+64]=0;
 const uint32_t fatSec=rd16(14)*512;
 const uint32_t clusterBytes=uint32_t(spc)*512;
 // FAT2-6 end-of-chain entries
 for(int i=2;i<=6;i++)put16(fatSec+i*2,0xffff);
 memcpy(flash.data()+dataStart,"HELLO!",6);
 memcpy(flash.data()+dataStart+2*clusterBytes,"GOODBYE",7);
 memcpy(flash.data()+dataStart+3*clusterBytes,"SOUND_SOUND!",12);
 uint8_t png[28]={0x89,'P','N','G',13,10,26,10,0,0,0,13,'I','H','D','R',0,0,0,100,0,0,0,100};
 memcpy(flash.data()+dataStart+4*clusterBytes,png,28);
 assert(musicflash::selectByIndex(0,0));
 assert(musicflash::folderCount()==2 && musicflash::trackCount()==2);
 assert(!strcmp(musicflash::selectedTrack().filename,"ONE.MP3"));
 musicflash::FileSource f;assert(f.open("/track.mp3"));
 char b[40]={};assert(f.read(b,6)==6 && !strcmp(b,"HELLO!"));f.close();
 assert(musicflash::nextTrackIndex()==1);
 assert(musicflash::selectByIndex(0,1));
 assert(musicflash::nextTrackIndex()==0); // album playlist wraps to first track
 assert(musicflash::selectByIndex(0,0));
 assert(musicflash::selectByIndex(1,0));
 assert(!strcmp(musicflash::folderName(),"音楽") && musicflash::trackCount()==1);
 assert(f.open("/song.mp3"));memset(b,0,sizeof(b));assert(f.read(b,12)==12&&!strcmp(b,"SOUND_SOUND!"));f.close();
 uint8_t* image=nullptr;uint32_t n=0;uint16_t w=0,h=0;
 assert(musicflash::loadFirstFolderPng(image,n,w,h));
 assert(image && n==28 && w==100 && h==100);free(image);
 assert(musicflash::nextFolderIndex()==0);
 puts("PASS R8 FAT16: cached FAT, ROOT/ALBUM browse, track switching, PNG fallback");
}
