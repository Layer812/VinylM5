// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Layer812

#include "music_flash.hpp"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <cstring>

namespace musicflash {
namespace {
const esp_partition_t* part = nullptr;
uint32_t fatFirst = 0, rootFirst = 0, dataFirst = 0, totalSectors = 0;
uint16_t fatSectors = 0, rootEntries = 0;
uint8_t sectorsPerCluster = 0, fatCopies = 0;
Track selected;
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t le32(const uint8_t* p) { return uint32_t(le16(p)) | (uint32_t(le16(p+2))<<16); }
void put16(uint8_t* p, uint16_t n) { p[0]=uint8_t(n);p[1]=uint8_t(n>>8); }
void put32(uint8_t* p, uint32_t n) { put16(p,n);put16(p+2,n>>16); }
uint32_t clusterBytes() {return uint32_t(sectorsPerCluster)*512;}
uint32_t clusterOffset(uint16_t cl) { return (dataFirst + uint32_t(cl-2)*sectorsPerCluster)*512; }
bool nextCluster(uint16_t cl, uint16_t& next) {
 uint8_t b[2]; if (!rawRead(fatFirst*512 + uint32_t(cl)*2,b,2))return false;
 next=le16(b);return true;
}
}
bool begin() {
 part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,"spiffs");
 return part && part->size == 0xCF0000u && part->address == 0x310000u;
}
uint32_t capacityBytes(){return part?part->size:0;}
bool rawRead(uint32_t addr, void* dst, uint32_t len) {
 if(!part || uint64_t(addr)+len>part->size)return false;
 return esp_partition_read(part,addr,dst,len)==ESP_OK;
}
bool rawWrite(uint32_t addr, const void* src,uint32_t len) {
 if(!part || uint64_t(addr)+len>part->size)return false;
 static uint8_t page[4096];
 auto p=static_cast<const uint8_t*>(src);
 while(len){const uint32_t base=addr&~4095u, off=addr-base, n=std::min(len,4096u-off);
  if(esp_partition_read(part,base,page,sizeof(page))!=ESP_OK)return false;
  memcpy(page+off,p,n);
  if(esp_partition_erase_range(part,base,sizeof(page))!=ESP_OK)return false;
  if(esp_partition_write(part,base,page,sizeof(page))!=ESP_OK)return false;
  addr+=n;p+=n;len-=n;
 }
 return true;
}
bool isFat16() {
 uint8_t b[512];
 if(!rawRead(0,b,sizeof(b)) || b[510]!=0x55 || b[511]!=0xaa) return false;
 if(le16(b+11)!=512 || b[16]<1 || b[13]==0 || (b[13]&(b[13]-1)))return false;
 if(memcmp(b+54,"FAT16",5)!=0)return false;
 sectorsPerCluster=b[13];fatCopies=b[16];rootEntries=le16(b+17);
 fatSectors=le16(b+22);totalSectors=le16(b+19);
 if(!totalSectors)totalSectors=le32(b+32);
 fatFirst=le16(b+14);
 const uint32_t rootSectors=((uint32_t(rootEntries)*32)+511)/512;
 rootFirst=fatFirst+uint32_t(fatCopies)*fatSectors;
 dataFirst=rootFirst+rootSectors;
 if(!fatSectors || fatFirst<1 || rootEntries<16 || rootEntries>1024 || dataFirst>=totalSectors || totalSectors>part->size/512)return false;
 const uint32_t clusters=(totalSectors-dataFirst)/sectorsPerCluster;
 return clusters>=4085 && clusters<65525;
}
bool formatFAT16() {
 if(!part)return false;
 // 4 sectors per cluster -> 2KiB, making this 12.94MiB volume FAT16.
 constexpr uint16_t reserved=1, roots=512, rootsSecs=32;
 constexpr uint8_t spc=4, nfats=2;
 const uint32_t sectors=part->size/512;
 uint16_t fatSecs=1;
 for(int k=0;k<16;k++){
   const uint32_t clusters=(sectors-reserved-rootsSecs-nfats*fatSecs)/spc;
   const uint16_t want=uint16_t((uint64_t(clusters+2)*2+511)/512);
   if(want==fatSecs)break;
   fatSecs=want;
 }
 const uint32_t dataStart=reserved+nfats*fatSecs+rootsSecs;
 const uint32_t clusters=(sectors-dataStart)/spc;
 if(clusters<4085 || clusters>=65525)return false;
 // Quick FAT initialization: erase only boot/FAT/root metadata sectors.
 // Every later MSC block write performs read-modify-erase-write of its 4KiB
 // sector, so old LittleFS payload bytes do not prevent writing new MP3 data.
 const uint32_t eraseBytes=(dataStart*512u+4095u)&~4095u;
 if(esp_partition_erase_range(part,0,eraseBytes)!=ESP_OK)return false;
 uint8_t sector[512]={};
 sector[0]=0xeb;sector[1]=0x3c;sector[2]=0x90;
 memcpy(sector+3,"MSDOS5.0",8);put16(sector+11,512);sector[13]=spc;
 put16(sector+14,reserved);sector[16]=nfats;put16(sector+17,roots);
 put16(sector+19,uint16_t(sectors));sector[21]=0xf8;put16(sector+22,fatSecs);
 put16(sector+24,32);put16(sector+26,64);sector[36]=0x80;sector[38]=0x29;
 put32(sector+39,0x20261106);memcpy(sector+43,"VINYLUSB   ",11);
 memcpy(sector+54,"FAT16   ",8);sector[510]=0x55;sector[511]=0xaa;
 if(esp_partition_write(part,0,sector,sizeof(sector))!=ESP_OK)return false;
 // All FAT/root data must be zero, not erased 0xff, before Windows sees it.
 // All FAT/root sectors are initialized in a single programming pass.
 // Flash programming cannot turn 0 bits back into 1; never write the FAT
 // reserved entries on top of an already-programmed all-zero page.
 uint8_t zeros[4096]={};
 const uint32_t metaEnd=dataStart*512;
 const uint32_t fatHead0=reserved*512;
 const uint32_t fatHead1=(reserved+fatSecs)*512;
 const uint32_t labelAt=(reserved+nfats*fatSecs)*512;
 for(uint32_t addr=512;addr<metaEnd;){
  const uint32_t n=std::min(uint32_t(sizeof(zeros)),metaEnd-addr);
  memset(zeros,0,n);
  auto addFat=[&](uint32_t head){if(head>=addr && head+4<=addr+n){
      uint32_t i=head-addr;zeros[i]=0xf8;zeros[i+1]=0xff;zeros[i+2]=0xff;zeros[i+3]=0xff;}};
  addFat(fatHead0);addFat(fatHead1);
  if(labelAt>=addr && labelAt+32<=addr+n){
    const uint32_t i=labelAt-addr;memcpy(zeros+i,"VINYLUSB   ",11);zeros[i+11]=0x08;
  }
  if(esp_partition_write(part,addr,zeros,n)!=ESP_OK)return false;
  addr+=n;
 }
 return isFat16();
}
// FAT16 root is a fixed directory. Child folders contain 32-byte
// directory entries in a FAT cluster chain. LFN UTF-16 is converted to UTF-8.
constexpr int kMaxFolders=32, kMaxTracks=128;
struct Folder {char name[96];uint16_t cl;};
Folder folders[kMaxFolders]={{"ROOT",0}};
Track tracks[kMaxTracks];
uint8_t folderN=1, currentFolder=0;
uint16_t trackN=0, currentTrack=0;
uint8_t* fatCache=nullptr;
uint32_t fatCacheBytes=0;
uint16_t cachedNext(uint16_t cl) {
 const uint32_t offset=uint32_t(cl)*2;
 if(fatCache && offset+2<=fatCacheBytes)return le16(fatCache+offset);
 uint16_t value=0;if(!nextCluster(cl,value))return 0xffff;return value;
}
void appendUtf8(char* output,size_t cap,size_t& pos,uint32_t cp){
 if(cp<0x80){if(pos+1<cap)output[pos++]=char(cp);}
 else if(cp<0x800){if(pos+2<cap){output[pos++]=char(0xc0|(cp>>6));output[pos++]=char(0x80|(cp&63));}}
 else if(cp<0x10000){if(pos+3<cap){output[pos++]=char(0xe0|(cp>>12));output[pos++]=char(0x80|((cp>>6)&63));output[pos++]=char(0x80|(cp&63));}}
 else if(cp<=0x10ffff){if(pos+4<cap){output[pos++]=char(0xf0|(cp>>18));output[pos++]=char(0x80|((cp>>12)&63));output[pos++]=char(0x80|((cp>>6)&63));output[pos++]=char(0x80|(cp&63));}}
}
struct Entry {char name[128]={};char shortName[13]={};uint16_t cl=0;uint32_t size=0;bool directory=false;};
void decodeLongName(const uint16_t* longName,char* out,size_t cap){
 size_t pos=0;
 for(int i=0;i<260 && longName[i] && longName[i]!=0xffff;i++){
  uint32_t cp=longName[i];
  if(cp>=0xd800&&cp<=0xdbff && i+1<260 && longName[i+1]>=0xdc00&&longName[i+1]<=0xdfff){cp=0x10000+((cp-0xd800)<<10)+(longName[++i]-0xdc00);}
  else if(cp>=0xd800&&cp<=0xdfff)cp='?';
  appendUtf8(out,cap,pos,cp);
 }
 out[pos]=0;
}
template<typename F> bool scanDir(uint16_t dir,F visitor){
 if(!part)return false;
 uint8_t sector[512];
 uint16_t lfn[260];uint8_t lfnChecksum=0;bool lfnValid=false;
 for(int i=0;i<260;i++)lfn[i]=0xffff;
 auto parseSector=[&](const uint8_t* b)->int{
  for(int j=0;j<16;j++){
   const uint8_t* d=b+32*j;
   if(!d[0])return 0;
   if(d[0]==0xe5){lfnValid=false;continue;}
   if(d[11]==0x0f){
    int ord=d[0]&31;
    if(d[0]&0x40){for(int k=0;k<260;k++)lfn[k]=0xffff;lfnValid=true;lfnChecksum=d[13];}
    if(!lfnValid||!ord||ord>20||lfnChecksum!=d[13]){lfnValid=false;continue;}
    const int offs=(ord-1)*13;int p=0;
    for(int k=1;k<11;k+=2)lfn[offs+p++]=le16(d+k);
    for(int k=14;k<26;k+=2)lfn[offs+p++]=le16(d+k);
    for(int k=28;k<32;k+=2)lfn[offs+p++]=le16(d+k);
    continue;
   }
   if(d[11]&0x08){lfnValid=false;continue;} // volume label
   Entry e;char base[9]={},ext[4]={};
   memcpy(base,d,8);memcpy(ext,d+8,3);
   for(int k=7;k>=0&&base[k]==' ';k--)base[k]=0;
   for(int k=2;k>=0&&ext[k]==' ';k--)ext[k]=0;
   snprintf(e.shortName,sizeof(e.shortName),"%s%s%s",base,ext[0]?".":"",ext);
   e.directory=(d[11]&0x10)!=0;
   e.size=le32(d+28);e.cl=le16(d+26);
   if(lfnValid)decodeLongName(lfn,e.name,sizeof(e.name));
   if(!e.name[0])strncpy(e.name,e.shortName,sizeof(e.name)-1);
   lfnValid=false;
   if(!visitor(e))return -1;
  }
  return 1;
 };
 if(dir==0){
  for(uint32_t ix=0;ix<rootEntries;ix+=16){
   if(!rawRead(rootFirst*512+ix*32,sector,512))return false;
   const int action=parseSector(sector);
   if(action<=0)break;
  }
 }else{
  const uint32_t maxCl=(totalSectors-dataFirst)/sectorsPerCluster+2;
  uint16_t cl=dir;
  for(uint32_t visits=0;visits<maxCl && cl>=2&&cl<maxCl;visits++){
   for(int sec=0;sec<sectorsPerCluster;sec++){
    if(!rawRead(clusterOffset(cl)+uint32_t(sec)*512,sector,512))return false;
    const int action=parseSector(sector);
    if(action<=0)return true;
   }
   const uint16_t next=cachedNext(cl);
   if(next>=0xfff8||next<2||next>=maxCl)break;
   cl=next;
  }
 }
 return true;
}
bool hasExtension(const char* str,const char* ext){
 const char* dot=strrchr(str,'.');if(!dot)return false;
 for(int i=0;ext[i];i++){
  if(!dot[i+1])return false;
  char a=dot[i+1],b=ext[i];
  if(a>='a'&&a<='z')a-=32;
  if(b>='a'&&b<='z')b-=32;
  if(a!=b)return false;
 }
 return dot[strlen(ext)+1]==0;
}
void buildFolderCatalog(){
 folderN=1;strncpy(folders[0].name,"ROOT",sizeof(folders[0].name));folders[0].cl=0;
 // Breadth-first, bounded traversal. Hierarchy flattened for the left button.
 for(uint8_t i=0;i<folderN && folderN<kMaxFolders;i++){
  scanDir(folders[i].cl,[&](const Entry& e){
   if(e.directory&&e.cl>=2 && e.shortName[0]!='.' && folderN<kMaxFolders){
    // Avoid recursion on damaged cyclic directory chains.
    bool unique=true;for(uint8_t k=0;k<folderN;k++)if(folders[k].cl==e.cl)unique=false;
    if(unique){strncpy(folders[folderN].name,e.name,95);folders[folderN].name[95]=0;folders[folderN].cl=e.cl;folderN++;}
   }
   return true;
  });
 }
}
void buildTrackList(){
 trackN=0;currentTrack=0;
 scanDir(folders[currentFolder].cl,[&](const Entry& e){
  if(!e.directory && e.size>0&&e.cl>=2 && hasExtension(e.name,"MP3") && trackN<kMaxTracks){
   Track& tr=tracks[trackN++];strncpy(tr.filename,e.name,sizeof(tr.filename)-1);tr.filename[127]=0;tr.firstCluster=e.cl;tr.size=e.size;
  }
  return true;
 });
}
bool selectByIndex(uint8_t f,uint16_t t){
 if(!part||!isFat16())return false;
 // One FAT copy (~13-15KiB) cached in PSRAM, replaces O(clusters) tiny read calls.
 if(!fatCache){
  fatCacheBytes=uint32_t(fatSectors)*512;
  fatCache=static_cast<uint8_t*>(heap_caps_malloc(fatCacheBytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if(fatCache && !rawRead(fatFirst*512,fatCache,fatCacheBytes)){heap_caps_free(fatCache);fatCache=nullptr;}
 }
 buildFolderCatalog();
 currentFolder=f<folderN?f:0;
 buildTrackList();
 if(!trackN){
  // Skip empty folders in catalogue order, wrapping once. Do not jump
  // back to the first child whenever the selected folder was empty.
  for(uint8_t n=1;n<folderN;n++){
    currentFolder=uint8_t((uint16_t(currentFolder)+1)%folderN);
    buildTrackList();
    if(trackN)break;
  }
 }
 if(!trackN)return false;
 currentTrack=t<trackN?t:0;
 return true;
}
uint8_t folderIndex(){return currentFolder;}
uint8_t folderCount(){return folderN;}
uint16_t trackIndex(){return currentTrack;}
uint16_t trackCount(){return trackN;}
const char* folderName(){return folders[currentFolder].name;}
const Track& selectedTrack(){return tracks[currentTrack];}
uint8_t nextFolderIndex(){return folderN?uint8_t((currentFolder+1)%folderN):0;}
uint16_t nextTrackIndex(){return trackN?uint16_t((currentTrack+1)%trackN):0;}
bool findFirstMP3(Track& tr){
 if(!selectByIndex(0,0))return false;
 tr=selectedTrack();return true;
}
bool loadFirstFolderPng(uint8_t*& buffer,uint32_t& size,uint16_t& width,uint16_t& height){
 buffer=nullptr;size=0;width=height=0;
 Track image;
 scanDir(folders[currentFolder].cl,[&](const Entry& e){
  if(!e.directory && e.cl>=2 && e.size>=24 && e.size<=1536u*1024u && hasExtension(e.name,"PNG")){
   strncpy(image.filename,e.name,sizeof(image.filename)-1);image.firstCluster=e.cl;image.size=e.size;return false;
  }
  return true;
 });
 if(!image.size)return false;
 FileSource f;
 if(!f.openTrack(image))return false;
 buffer=static_cast<uint8_t*>(heap_caps_malloc(image.size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(!buffer){f.close();return false;}
 size=f.read(buffer,image.size);f.close();
 const bool valid=size==image.size && size>=24 && buffer[0]==0x89 && memcmp(buffer+1,"PNG\r\n\x1a\n",7)==0;
 auto be32=[](const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|uint32_t(p[3]);};
 if(valid){uint32_t w=be32(buffer+16),h=be32(buffer+20);if(w>0&&h>0&&w<=8192&&h<=8192){width=w;height=h;return true;}}
 heap_caps_free(buffer);buffer=nullptr;size=0;return false;
}
FileSource::FileSource(){}
FileSource::~FileSource(){close();}
bool FileSource::open(const char*) { return openTrack(selectedTrack()); }
bool FileSource::openTrack(const Track& tr) {
 close(); if(!tr.size || tr.firstCluster<2)return false;
 const uint32_t clusterSize=clusterBytes();
 nclusters_=(tr.size+clusterSize-1)/clusterSize;
 if(nclusters_>totalSectors/sectorsPerCluster)return false;
 chain_=static_cast<uint16_t*>(heap_caps_malloc(nclusters_*sizeof(uint16_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(!chain_)return false;
 uint16_t current=tr.firstCluster;
 const uint32_t maxCluster=(totalSectors-dataFirst)/sectorsPerCluster+2;
 for(uint32_t i=0;i<nclusters_;++i){
  if(current<2 || current>=maxCluster){close();return false;}
  chain_[i]=current;
  if(i+1<nclusters_){current=cachedNext(current);if(current>=0xfff8){close();return false;}}
 }
 size_=tr.size;pos_=0;return true;
}
uint32_t FileSource::read(void* dst,uint32_t len){
 if(!isOpen() || !dst || pos_>=size_)return 0;
 len=std::min(len,size_-pos_);auto* p=static_cast<uint8_t*>(dst);
 uint32_t got=0,clusterSize=clusterBytes();
 while(len){const uint32_t cix=pos_/clusterSize, inside=pos_%clusterSize;
  const uint32_t n=std::min(len,clusterSize-inside);
  if(cix>=nclusters_ || !rawRead(clusterOffset(chain_[cix])+inside,p,n))break;
  got+=n;pos_+=n;p+=n;len-=n;
 }
 return got;
}
bool FileSource::seek(int32_t move,int dir){if(!isOpen())return false;
 int64_t next=move;
 if(dir==SEEK_CUR)next+=pos_;
 else if(dir==SEEK_END)next+=size_;
 else if(dir!=SEEK_SET)return false;
 if(next<0 || uint64_t(next)>size_)return false;
 pos_=uint32_t(next);return true;
}
bool FileSource::close(){if(chain_)heap_caps_free(chain_);chain_=nullptr;pos_=size_=nclusters_=0;return true;}
bool FileSource::isOpen(){return chain_!=nullptr;}
uint32_t FileSource::getSize(){return size_;}
uint32_t FileSource::getPos(){return pos_;}
}
