// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Layer812
// External microSD reader (StopWatch rear 6-pin connector), read-only music.
#include "sd_music.hpp"
#include "media_selection.hpp"
#include "sd_spi_autodetect.hpp"
#include <driver/gpio.h>
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <cstring>
#include <strings.h>
#include <cstdio>
#include <atomic>
#include <LittleFS.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
namespace sdmusic { namespace {
// Search root and child directories to a bounded depth, rather than only
// the first 23 immediate folders. Keep folder table in PSRAM, not internal SRAM.
constexpr unsigned kMaxFolders=192, kMaxTracks=256, kMaxDepth=8;
constexpr unsigned kScanEntryLimit=4096;
struct Folder { char path[512]={}; char name[256]={}; uint8_t depth=0; bool hasAudio=false; bool scanned=false; };
Folder* folders=nullptr;
Track* tracks=nullptr;
std::atomic<uint8_t> folderN{0}; uint8_t currentFolder=0;
uint16_t trackN=0,currentTrack=0;
uint32_t skippedNames=0;
bool folderLimitReached=false;
bool ready=false;
SemaphoreHandle_t sdMutex=nullptr;
TaskHandle_t catalogTaskHandle=nullptr;
volatile bool catalogRunning=false;
bool cacheLoaded=false, cacheFS=false;
char preferredFolder[512]={};
constexpr uint32_t kCacheMagic=0x56534458u; // "XDSV", LE
constexpr uint16_t kCacheVersion=2;
constexpr char kCachePath[]="/.vinyl_sd_catalog.bin";
constexpr char kCacheTmp[]="/.vinyl_sd_catalog.tmp";
struct CacheHeader { uint32_t magic; uint16_t version; uint16_t count; uint64_t size; uint32_t hash; };
uint32_t fnv(uint32_t h,const uint8_t* p,size_t n){while(n--){h^=*p++;h*=16777619u;}return h;}
void takeSd(){if(sdMutex)xSemaphoreTake(sdMutex,portMAX_DELAY);}
void giveSd(){if(sdMutex)xSemaphoreGive(sdMutex);}
// A low-priority worker publishes audio flags while the UI can browse.
bool hasMusic(unsigned i){return __atomic_load_n(&folders[i].hasAudio,__ATOMIC_ACQUIRE);}
void setMusic(unsigned i,bool v){__atomic_store_n(&folders[i].hasAudio,v,__ATOMIC_RELEASE);}

// Persist the result of the SINGLE startup scan. The USB CDC/serial monitor
// may connect after these startup messages have already been sent.
// Logging this POD state does not touch SD, SPI, I2C or the display.
enum class SdPhase : uint8_t {
 NotStarted, SpiMountFailed, CatalogAllocFailed, MountedNoMusic, MusicReady
};
SdPhase sdPhase=SdPhase::NotStarted;
bool muxVerified=false;
bool spiMounted=false;
uint8_t spiAttempts=0;
uint32_t lastTriedHz=0, workingHz=0;
uint64_t cardBytes=0;
const char* detectedProfile="NONE";
// StopWatch AMOLED occupies ESP32-S3 SPI2; SD uses independent SPI3/HSPI.
SPIClass sdSPI(HSPI);

// Official StopWatch EXT bus: MUX_IO_1/2 is a CH442E switch.
// M5IOE1 (0x4F), PYG1 (bit 0): LOW -> GPIO43/GPIO44 (UART-side),
// HIGH -> USB D+/D-. It is NOT enough to configure SPI GPIO pins while
// the external switch is still routing the header to USB.
// Run only AFTER the unchanged R11 M5.begin(), when the shared I2C
// controller is initialized. Modify ONLY PYG1; do not touch the OLED,
// audio amplifier, touch reset or power-related IOE1 outputs.
static bool selectRearGpioRoute(){
 constexpr uint8_t kIoAddr=0x4F, kModeReg=0x03, kOutReg=0x05, kMuxMask=0x01;
 constexpr uint32_t kI2cHz=100000;
 uint8_t mode=0, level=0;
 bool readOk=false;
 for(unsigned attempt=0;attempt<2 && !readOk;++attempt){
   readOk=M5.In_I2C.readRegister(kIoAddr,kModeReg,&mode,1,kI2cHz)
       && M5.In_I2C.readRegister(kIoAddr,kOutReg,&level,1,kI2cHz);
   if(!readOk)delay(15);
 }
 if(!readOk){
   Serial.println("VinylM5 SD: MUX I2C unavailable; trying default GPIO43/44 route");
   return false;
 }
 // First preset the output LOW, then drive PYG1 as an output.
 // This prevents a transient selection of the USB D+/D- signals.
 if((level & kMuxMask) &&
    !M5.In_I2C.writeRegister8(kIoAddr,kOutReg,uint8_t(level & ~kMuxMask),kI2cHz)){
   Serial.println("VinylM5 SD: MUX output level write failed");
   return false;
 }
 if(!(mode & kMuxMask) &&
    !M5.In_I2C.writeRegister8(kIoAddr,kModeReg,uint8_t(mode | kMuxMask),kI2cHz)){
   Serial.println("VinylM5 SD: MUX output mode write failed");
   return false;
 }
 uint8_t verifyMode=0,verifyLevel=0;
 const bool ok=M5.In_I2C.readRegister(kIoAddr,kModeReg,&verifyMode,1,kI2cHz)
             && M5.In_I2C.readRegister(kIoAddr,kOutReg,&verifyLevel,1,kI2cHz)
             && (verifyMode & kMuxMask) && !(verifyLevel & kMuxMask);
 Serial.printf("VinylM5 SD: rear MUX GPIO43/44 %s (mode=%02X out=%02X)\n",
               ok?"READY":"NOT CONFIRMED",verifyMode,verifyLevel);
 return ok;
}


// Use the SD library's tested SPI initialization on the two supported
// board pinouts (C then A). No arbitrary GPIO combinations are probed.
// Between failed trials, CS is HIGH, the SPI peripheral is shut down
// and GPIO0/9/44 are released. Power and ground must match physically.
class AutoMountBus {
  static void releasePins() {
    digitalWrite(43, HIGH);
    pinMode(43, OUTPUT);
    for (int pin : {0, 9, 44}) pinMode(pin, INPUT_PULLUP);
  }
public:
  bool tryMount(const sdautodetect::Pins& pins, uint32_t hz) {
    ++spiAttempts;
    lastTriedHz = hz;
    releasePins();
    Serial.printf("VinylM5 SD AUTO: TRY profile=%s speed=%lu\n",
                  pins.label, static_cast<unsigned long>(hz));
    digitalWrite(43, HIGH);
    pinMode(43, OUTPUT);
    sdSPI.begin(pins.sck, pins.miso, pins.mosi, pins.cs);
    delay(20);
    if (SD.begin(pins.cs, sdSPI, hz, "/sd", 5, false)) {
      return true;
    }
    // Deselect before changing which pad is the clock or card output.
    digitalWrite(pins.cs, HIGH);
    SD.end();
    sdSPI.end();
    releasePins();
    vTaskDelay(pdMS_TO_TICKS(40));
    return false;
  }
};

const char* leaf(const char* n){
 if(!n)return "";
 const char* p=strrchr(n,'/');return p?p+1:n;
}
bool pathFor(char* out,size_t cap,const char* dir,const char* file){
 if(!out||!dir||!file||!file[0])return false;
 const int n=snprintf(out,cap,"%s%s%s",dir,strcmp(dir,"/")==0?"":"/",file);
 return n>0 && size_t(n)<cap;
}
// When the internal music partition is FAT16, LittleFS cannot be used for an
// index. Save a BOUNDED compact index in existing NVS instead. No FAT sectors,
// boot metadata or partition table are written by the indexer.
constexpr size_t kNvsMax=6144;
constexpr char kNvsNamespace[]="vinyl_sdc";
constexpr char kNvsKey[]="dirs";
bool readNvsCache(){
 Preferences pref;
 if(!pref.begin(kNvsNamespace,true))return false;
 const size_t n=pref.getBytesLength(kNvsKey);
 if(n<sizeof(CacheHeader) || n>kNvsMax){pref.end();return false;}
 auto* buf=static_cast<uint8_t*>(heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(!buf){pref.end();return false;}
 const bool okRead=pref.getBytes(kNvsKey,buf,n)==n;
 pref.end();
 CacheHeader hdr{};
 if(okRead)memcpy(&hdr,buf,sizeof(hdr));
 bool ok=okRead && hdr.magic==kCacheMagic && hdr.version==kCacheVersion
   && hdr.count>0 && hdr.count<=kMaxFolders && hdr.size==cardBytes;
 uint32_t hash=2166136261u;
 size_t off=sizeof(CacheHeader);
 if(ok){
  for(unsigned i=0;i<hdr.count;++i){
   if(off+4>n){ok=false;break;}
   uint16_t len;memcpy(&len,buf+off,2);
   uint8_t depth=buf[off+2], audio=buf[off+3];
   const size_t start=off;off+=4;
   if(!len || len>=sizeof(Folder::path) || depth>kMaxDepth || off+len>n){ok=false;break;}
   Folder& item=folders[i];memcpy(item.path,buf+off,len);item.path[len]=0;
   if(item.path[0]!='/' || (i==0 && strcmp(item.path,"/"))){ok=false;break;}
   strlcpy(item.name,i==0?"ROOT":leaf(item.path),sizeof(item.name));
   item.depth=depth;item.hasAudio=audio!=0;item.scanned=false;
   hash=fnv(hash,buf+start,len+4);off+=len;
  }
 }
 heap_caps_free(buf);
 if(!ok || off!=n || hdr.hash!=hash){folderN=0;return false;}
 folderN=uint8_t(hdr.count);
 Serial.printf("VinylM5 SD CACHE: NVS LOAD %u folders\n",unsigned(folderN));
 return true;
}
void writeNvsCache(){
 if(!folderN)return;
 auto* buf=static_cast<uint8_t*>(heap_caps_malloc(kNvsMax,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(!buf)return;
 CacheHeader hdr{kCacheMagic,kCacheVersion,0,cardBytes,2166136261u};
 size_t off=sizeof(hdr);unsigned count=0;
 for(unsigned i=0;i<folderN;++i){
  const auto& item=folders[i];
  const size_t n=strlen(item.path);
  if(!n || n>=sizeof(item.path) || off+4+n>kNvsMax)break;
  const uint16_t len=uint16_t(n);
  memcpy(buf+off,&len,2);buf[off+2]=item.depth;buf[off+3]=hasMusic(i)?1:0;
  memcpy(buf+off+4,item.path,n);
  hdr.hash=fnv(hdr.hash,buf+off,4+n);
  off+=4+n;++count;
 }
 if(count){
  hdr.count=uint16_t(count);
  memcpy(buf,&hdr,sizeof(hdr));
  Preferences pref;
  if(pref.begin(kNvsNamespace,false)){
    const size_t wrote=pref.putBytes(kNvsKey,buf,off);
    pref.end();
    Serial.printf("VinylM5 SD CACHE: NVS %s %u/%u folders (%u bytes)\n",
      wrote==off?"SAVED":"SAVE_FAILED",count,unsigned(folderN),unsigned(off));
  }
 }
 heap_caps_free(buf);
}
// The index is self-contained, versioned and checksummed. A cache is never
// accepted without a matching card size AND checksum. On another card of the
// same capacity the selected folder is reopened, and the index is rebuilt.
// Only use an already mountable LittleFS; NEVER format/repartition internal Flash.
bool readCache(){
 if(!cacheFS)return false;
 File f=LittleFS.open(kCachePath,"r");
 if(!f)return false;
 CacheHeader hdr{};
 bool ok=f.read(reinterpret_cast<uint8_t*>(&hdr),sizeof(hdr))==sizeof(hdr)
    && hdr.magic==kCacheMagic && hdr.version==kCacheVersion && hdr.size==cardBytes
    && hdr.count>0 && hdr.count<=kMaxFolders;
 uint32_t hash=2166136261u;
 if(ok){
   for(unsigned i=0;i<hdr.count;++i){
     uint16_t len=0;uint8_t depth=0,audio=0;
     if(f.read(reinterpret_cast<uint8_t*>(&len),2)!=2 || f.read(&depth,1)!=1 ||
        f.read(&audio,1)!=1 || len<1 || len>=sizeof(Folder::path) || depth>kMaxDepth){ok=false;break;}
     Folder& item=folders[i];
     if(f.read(reinterpret_cast<uint8_t*>(item.path),len)!=len){ok=false;break;}
     item.path[len]=0;
     if(item.path[0]!='/' || (i==0 && strcmp(item.path,"/")!=0)){ok=false;break;}
     strlcpy(item.name,i==0?"ROOT":leaf(item.path),sizeof(item.name));
     item.depth=depth;item.hasAudio=audio!=0;item.scanned=false;
     hash=fnv(hash,reinterpret_cast<uint8_t*>(&len),2);
     hash=fnv(hash,&depth,1);hash=fnv(hash,&audio,1);
     hash=fnv(hash,reinterpret_cast<const uint8_t*>(item.path),len);
   }
 }
 f.close();
 if(!ok || hash!=hdr.hash){folderN=0;return false;}
 folderN=hdr.count;
 Serial.printf("VinylM5 SD CACHE: LOAD %u directories from internal Flash\n",unsigned(folderN));
 return true;
}
void writeCache(){
 if(!cacheFS || !folderN)return;
 File f=LittleFS.open(kCacheTmp,"w");
 if(!f)return;
 CacheHeader hdr{kCacheMagic,kCacheVersion,0,cardBytes,0};
 bool ok=f.write(reinterpret_cast<uint8_t*>(&hdr),sizeof(hdr))==sizeof(hdr);
 uint32_t hash=2166136261u;
 uint16_t count=0;
 for(unsigned i=0;ok && i<folderN;++i){
   const Folder& item=folders[i];
   const size_t n=strlen(item.path);
   if(!n || n>=sizeof(item.path)){ok=false;break;}
   const uint16_t len=uint16_t(n);
   const uint8_t depth=item.depth,audio=hasMusic(i)?1:0;
   ok=f.write(reinterpret_cast<const uint8_t*>(&len),2)==2
       && f.write(&depth,1)==1 && f.write(&audio,1)==1
       && f.write(reinterpret_cast<const uint8_t*>(item.path),len)==len;
   hash=fnv(hash,reinterpret_cast<const uint8_t*>(&len),2);
   hash=fnv(hash,&depth,1);hash=fnv(hash,&audio,1);
   hash=fnv(hash,reinterpret_cast<const uint8_t*>(item.path),len);
   ++count;
   if((i&7u)==0)vTaskDelay(pdMS_TO_TICKS(1));
 }
 hdr.count=count;hdr.hash=hash;
 if(ok)ok=f.seek(0,SeekSet) && f.write(reinterpret_cast<uint8_t*>(&hdr),sizeof(hdr))==sizeof(hdr);
 f.flush();f.close();
 if(ok && count==folderN){
   if(!LittleFS.rename(kCacheTmp,kCachePath)){
     LittleFS.remove(kCachePath);
     ok=LittleFS.rename(kCacheTmp,kCachePath);
   }
   Serial.printf("VinylM5 SD CACHE: %s %u folders\n",ok?"SAVED":"SAVE_FAILED",unsigned(count));
 }else{LittleFS.remove(kCacheTmp);Serial.println("VinylM5 SD CACHE: incomplete, ignored");}
}
bool folderScan(uint8_t id){
 if(!ready||id>=folderN||!tracks)return false;
 trackN=0;currentTrack=0;
 takeSd(); File d=SD.open(folders[id].path);
 if(!d || !d.isDirectory()){d.close();giveSd();return false;}
 unsigned visited=0;
 while(File f=d.openNextFile()){
   if(!f.isDirectory() && media::isAudio(f.name()) && f.size() && trackN<kMaxTracks){
     Track entry;const char* name=leaf(f.name());
     if(strlen(name)<sizeof(entry.filename) &&
        pathFor(entry.path,sizeof(entry.path),folders[id].path,name)){
        strlcpy(entry.filename,name,sizeof(entry.filename));
        entry.size=static_cast<uint32_t>(f.size());entry.flac=media::isFlac(name);
        tracks[trackN++]=entry;
     } else {
        ++skippedNames; // Never silently truncate a UTF-8 filename or path.
     }
   }
   f.close();
   if ((++visited & 31u) == 0) { delay(0); }
   if (visited >= kScanEntryLimit) {
     Serial.println("VinylM5 SD: folder entry limit reached");
     break;
   }
 }
 d.close();giveSd();
 // Stable order across reboot / card insertion, no expensive sort on audio task.
 std::sort(tracks,tracks+trackN,[](const Track& a,const Track& b){
   return strcasecmp(a.filename,b.filename)<0;
 });
 return trackN>0;
}
// Discover folders breadth-first, including nested albums (depth <= 8).
// `hasAudio` lets selection skip folders that contain no playable files.
// All paths are validated before opening; names are stored as UTF-8 bytes.
void scanDirectoryCatalog(uint8_t index) {
 if(!folders || index>=folderN || folders[index].scanned)return;
 // A directory handle remains open, but every FAT API access is protected.
 takeSd();File dir=SD.open(folders[index].path);
 if(!dir || !dir.isDirectory()){dir.close();giveSd();folders[index].scanned=true;setMusic(index,false);return;}
 giveSd();
 bool directoryHasAudio=false;
 unsigned visited=0;
 for(;;) {
   takeSd();File entry=dir.openNextFile();
   if(!entry){giveSd();break;}
   char entryNameCopy[256]={};
   const char* original=leaf(entry.name());
   const bool validEntryName=strlen(original)<sizeof(entryNameCopy);
   if(validEntryName)strlcpy(entryNameCopy,original,sizeof(entryNameCopy));
   const bool entryDir=entry.isDirectory();
   const uint32_t entrySize=uint32_t(entry.size());
   entry.close();giveSd();
   if(!validEntryName){++skippedNames;continue;}
   const char* entryName=entryNameCopy;
   char path[512]={};
   const bool validPath=entryName[0] &&
     pathFor(path,sizeof(path),folders[index].path,entryName);
   if(entryDir) {
     if(folders[index].depth>=kMaxDepth) {
       // A bounded search, never recurse without a depth limit.
     } else if (!validPath || strlen(entryName)>=sizeof(Folder::name)) {
       ++skippedNames;
     } else if(folderN>=kMaxFolders) {
       folderLimitReached=true;
     } else {
       // Avoid cycles / duplicate physical directory names where possible.
       bool duplicate=false;
       for(unsigned j=0;j<folderN;++j){
         if(strcasecmp(folders[j].path,path)==0){duplicate=true;break;}
       }
       if(!duplicate){
         const uint8_t indexNew=folderN.load(std::memory_order_relaxed);
         Folder& next=folders[indexNew];
         strlcpy(next.path,path,sizeof(next.path));
         strlcpy(next.name,entryName,sizeof(next.name));
         next.depth=uint8_t(folders[index].depth+1);
         next.scanned=false;setMusic(indexNew,false);
         folderN.store(uint8_t(indexNew+1),std::memory_order_release);
       }
     }
   } else if(validPath && entrySize>0 && media::isAudio(entryName)) {
     if(strlen(entryName)<sizeof(Track::filename) &&
        strlen(path)<sizeof(Track::path)) {
       directoryHasAudio=true;
     } else {
       ++skippedNames;
     }
   } else if(!validPath && entryName[0]) {
     ++skippedNames;
   }
   if((++visited&7u)==0)vTaskDelay(pdMS_TO_TICKS(1));
   if(visited>=kScanEntryLimit){
     Serial.printf("VinylM5 SD: scan limit reached in %s\n", folders[index].path);
     break;
   }
 }
 takeSd();dir.close();giveSd();
 setMusic(index,directoryHasAudio);
 folders[index].scanned=true;
}
}
bool begin(){
 if(ready)return true;
 if(!sdMutex)sdMutex=xSemaphoreCreateMutex();
 if(!sdMutex){Serial.println("VinylM5 SD: mutex allocation failed");return false;}
 // Reset only our status snapshot, not the SPI/display hardware.
 sdPhase=SdPhase::SpiMountFailed;
 muxVerified=false;
 spiMounted=false;
 spiAttempts=0;
 lastTriedHz=workingHz=0;
 cardBytes=0;
 detectedProfile="NONE";
 // 3V3_L2 is powered in normal L2/L3A states (M5PM1 management).
 // Do not manually toggle the power rail, OLED or PMIC.
 const bool muxReady=selectRearGpioRoute();
 muxVerified=muxReady;
 if(!muxReady){
   Serial.println("VinylM5 SD: MUX control unverified; SD access canceled");
   return false;
 }
 // Try the C/A SPI layouts using the FAT32 mount path that
 // already succeeded on StopWatch. The weak open-drain CMD0-only preflight
 // could fail before any attempt reached the working SD driver.
 AutoMountBus bus;
 uint32_t mountedHz=0;
 const sdautodetect::Pins* detected=sdautodetect::mount(bus,mountedHz);
 if(!detected){
   Serial.println("VinylM5 SD AUTO: C/A mount failed; check FAT32, pins, card contact, and 3V3/GND");
   return false;
 }
 detectedProfile=detected->label;
 workingHz=mountedHz;
 spiMounted=true;
 cardBytes=SD.cardSize();
 sdPhase=SdPhase::MountedNoMusic;
 Serial.printf("VinylM5 SD AUTO: MOUNT OK profile=%s clock=%lu Hz capacity=%llu MiB\n",
   detected->label,static_cast<unsigned long>(mountedHz),
   static_cast<unsigned long long>(cardBytes/1048576ull));
 // No new video or audio setup here: this is only the SD catalogue.
 folders=static_cast<Folder*>(heap_caps_calloc(kMaxFolders,sizeof(Folder),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 tracks=static_cast<Track*>(heap_caps_calloc(kMaxTracks,sizeof(Track),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(!folders||!tracks){
   sdPhase=SdPhase::CatalogAllocFailed;
   if(folders){heap_caps_free(folders);folders=nullptr;}
   if(tracks){heap_caps_free(tracks);tracks=nullptr;}
   SD.end();sdSPI.end();return false;
 }
 folderN=1;currentFolder=0;trackN=0;currentTrack=0;
 strlcpy(folders[0].path,"/",sizeof(folders[0].path));
 strlcpy(folders[0].name,"ROOT",sizeof(folders[0].name));
 Preferences last;
 if(last.begin("vinyl_sd",true)){
   String p=last.getString("path","");last.end();
   if(p.length()<sizeof(preferredFolder))strlcpy(preferredFolder,p.c_str(),sizeof(preferredFolder));
 }
 // The internal music partition may hold FAT16, not LittleFS. False means
 // NO formatting and no erase. If already mounted, LittleFS.begin is harmless.
 cacheFS=LittleFS.begin(false);
 cacheLoaded=readCache();
 if(!cacheLoaded)cacheLoaded=readNvsCache();
 if(!cacheLoaded){
   for(unsigned i=0;i<kMaxFolders;++i)folders[i]=Folder{};
   folderN=1;
   strlcpy(folders[0].path,"/",sizeof(folders[0].path));
   strlcpy(folders[0].name,"ROOT",sizeof(folders[0].name));
   // Try the last playing folder immediately, without walking the whole card.
   if(preferredFolder[0]=='/' && strcmp(preferredFolder,"/") && folderN<kMaxFolders){
     Folder& saved=folders[folderN++];
     strlcpy(saved.path,preferredFolder,sizeof(saved.path));
     strlcpy(saved.name,leaf(saved.path),sizeof(saved.name));
     saved.depth=0;
     scanDirectoryCatalog(1);
   }
   // Search breadth-first only until the FIRST playable folder. Do not
   // hold boot for the rest of the music library; background will finish it.
   if(folderN==1 || !hasMusic(1)){
     for(unsigned i=0;i<folderN;++i){
       scanDirectoryCatalog(uint8_t(i));
       if(hasMusic(i))break;
     }
   }
 }
 ready=true;
 Serial.printf("VinylM5 SD: FAST start, %u indexed folders, cached=%u; full scan deferred\n",
               unsigned(folderN),unsigned(cacheLoaded));
 return true;
}
// Read-only serial report; deliberately does NOT call SD, SPI or I2C.
// The report repeats in the existing player's 2-second status interval,
// so opening a serial monitor late still shows why SD was not selected.
void printStatus(){
 const char* result="NOT_STARTED";
 switch(sdPhase){
   case SdPhase::SpiMountFailed:     result="MOUNT_FAILED_CARD_OR_FS"; break;
   case SdPhase::CatalogAllocFailed:result="CATALOG_MEMORY_FAILED"; break;
   case SdPhase::MountedNoMusic:    result="MOUNTED_NO_MP3_FLAC"; break;
   case SdPhase::MusicReady:        result="MUSIC_READY"; break;
   default:                         break;
 }
 Serial.printf("VinylM5 SD STATUS: AUTO=%s MUX=%s SPI=%s ATTEMPTS=%u LAST_HZ=%lu WORK_HZ=%lu CARD_MiB=%llu FOLDERS=%u TRACKS=%u RESULT=%s SKIPPED_NAMES=%lu LIMIT=%u CACHE=%s INDEXING=%u POWER=NOT_MEASURED\n",
   detectedProfile,
   muxVerified?"OK":"UNVERIFIED",
   spiMounted?"MOUNTED":"NOT_MOUNTED",
   static_cast<unsigned>(spiAttempts),
   static_cast<unsigned long>(lastTriedHz),
   static_cast<unsigned long>(workingHz),
   static_cast<unsigned long long>(cardBytes/1048576ull),
   static_cast<unsigned>(folderN),
   static_cast<unsigned>(trackN), result,
   static_cast<unsigned long>(skippedNames),unsigned(folderLimitReached),cacheLoaded?"HIT":"MISS",unsigned(catalogRunning));
}
void lockBus(){takeSd();}
void unlockBus(){giveSd();}
void backgroundTask(void*){
 catalogRunning=true;
 // Let the ID3/APIC task finish its first artwork reads before directory I/O.
 vTaskDelay(pdMS_TO_TICKS(2500));
 for(unsigned i=0;i<folderN;++i){
   scanDirectoryCatalog(uint8_t(i));
   vTaskDelay(pdMS_TO_TICKS(2));
 }
 if(cacheFS)writeCache(); // Internal LittleFS, when available.
 else writeNvsCache();  // Existing NVS only; partial compact cache OK.
 // Neither branch writes to microSD or formats internal storage.
 catalogRunning=false;
 catalogTaskHandle=nullptr;
 Serial.printf("VinylM5 SD INDEX: COMPLETE folders=%u\n",unsigned(folderN));
 vTaskDelete(nullptr);
}
void startBackgroundCatalog(){
 if(!ready || catalogTaskHandle)return;
 catalogRunning=true;
 if(xTaskCreatePinnedToCore(backgroundTask,"sd-index",6144,nullptr,1,&catalogTaskHandle,0)!=pdPASS){
   catalogTaskHandle=nullptr;catalogRunning=false;
   Serial.println("VinylM5 SD INDEX: worker allocation failed; playback still OK");
 }
}
bool catalogBuilding(){return catalogRunning;}
uint16_t randomTrackIndex(uint32_t seed){
 if(trackN<=1)return currentTrack;
 uint16_t n=uint16_t(seed%(trackN-1));
 if(n>=currentTrack)++n;
 return n;
}
const char* folderPath(){return ready && currentFolder<folderN?folders[currentFolder].path:"/";}
bool mounted(){return ready;}
bool selectByIndex(uint8_t folder,uint16_t track){
 if(!ready || !folderN)return false;
 uint8_t requested=folder<folderN?folder:0;
 if(preferredFolder[0]){
   for(unsigned j=0;j<folderN;++j)
     if(strcmp(folders[j].path,preferredFolder)==0){requested=uint8_t(j);break;}
 }
 for(unsigned i=0;i<folderN;++i){
   const uint8_t idx=uint8_t((unsigned(requested)+i)%folderN);
   if(!folders[idx].scanned && !cacheLoaded)scanDirectoryCatalog(idx);
   if(!hasMusic(idx))continue;
   if(folderScan(idx)){
     currentFolder=idx;
     currentTrack=(i==0 && track<trackN)?track:0;
     sdPhase=SdPhase::MusicReady;
     if(strcmp(preferredFolder,folders[idx].path)!=0){
       Preferences pref;
       if(pref.begin("vinyl_sd",false)){
         pref.putString("path",folders[idx].path);pref.end();
       }
       strlcpy(preferredFolder,folders[idx].path,sizeof(preferredFolder));
     }
     Serial.printf("VinylM5 SD: %u playable tracks in [%s]\n",unsigned(trackN),folders[idx].path);
     return true;
   }
 }
 // Cached index may refer to old folders. A background reindex will fix it.
 sdPhase=SdPhase::MountedNoMusic;
 Serial.println("VinylM5 SD: no playable files in fast index; fallback internal Flash");
 return false;
}

uint8_t folderIndex(){return currentFolder;}
uint8_t folderCount(){return folderN;}
uint16_t trackIndex(){return currentTrack;}
uint16_t trackCount(){return trackN;}
uint8_t nextFolderIndex(){
 // Prefer already indexed musical folders. If none yet, the next folder
 // will be scanned on the following boot; never force a full startup scan.
 if(!folderN)return 0;
 for(unsigned i=1;i<=folderN;++i){
   const uint8_t idx=uint8_t((currentFolder+i)%folderN);
   if(hasMusic(idx))return idx;
 }
 return uint8_t((currentFolder+1)%folderN);
}
const char* folderPathAt(uint8_t index){return ready&&index<folderN?folders[index].path:"/";}
uint16_t nextTrackIndex(){return trackN?uint16_t((currentTrack+1)%trackN):0;}
const char* folderName(){return folders[currentFolder].name;}
const Track& selectedTrack(){return tracks[currentTrack];}
bool loadFirstFolderPng(uint8_t*& data,uint32_t& bytes,uint16_t& width,uint16_t& height){
 data=nullptr;bytes=0;width=height=0;
 if(!ready)return false;
 takeSd();File dir=SD.open(folders[currentFolder].path);
 if(!dir||!dir.isDirectory()){dir.close();giveSd();return false;}
 char first[256]={};
 unsigned visited=0;
 while(File f=dir.openNextFile()){
   if(++visited>kScanEntryLimit){f.close();break;}
   const char* name=leaf(f.name());
   if(!f.isDirectory() && media::endsWith(name,".png") && f.size()>=24 && f.size()<=1536u*1024u){
     char candidate[256];
     if(pathFor(candidate,sizeof(candidate),folders[currentFolder].path,name) &&
       (!first[0] || strcasecmp(candidate,first)<0))strlcpy(first,candidate,sizeof(first));
   }
   f.close();
 }
 dir.close();
 if(!first[0]){giveSd();return false;}
 File file=SD.open(first,FILE_READ);
 if(!file){giveSd();return false;}
 bytes=static_cast<uint32_t>(file.size());
 data=static_cast<uint8_t*>(heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(!data){file.close();giveSd();bytes=0;return false;}
 const size_t got=file.read(data,bytes);file.close();giveSd();
 const bool valid=got==bytes && bytes>=24 && data[0]==0x89 &&
    memcmp(data+1,"PNG\r\n\x1a\n",7)==0;
 if(valid){
   const auto be=[](const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];};
   const uint32_t w=be(data+16),h=be(data+20);
   if(w&&h&&w<=8192&&h<=8192){width=uint16_t(w);height=uint16_t(h);return true;}
 }
 heap_caps_free(data);data=nullptr;bytes=0;return false;
}
}