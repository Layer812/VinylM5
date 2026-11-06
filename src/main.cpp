// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Layer812

// VinylM5 R11 — automatic playlist / single-track loop, preserving R10 DJ scratching and artwork-first startup.
// Uses M5Unified/M5GFX and ESP8266Audio; FAT16 USB music or legacy LittleFS /track.mp3.
// R10 is the verified baseline. R11 loop playback requires hardware verification.
#include <Arduino.h>
#include <LittleFS.h>
#include <Preferences.h>
#include "music_flash.hpp"
#include "usb_music.hpp"
#include "record_id3.hpp"
#include <M5Unified.h>
#include <AudioOutput.h>
#include <AudioFileSource.h>
#include <AudioFileSourceFS.h>
#include <AudioFileSourceID3.h>
#include <esp_heap_caps.h>
#include <AudioGeneratorMP3.h>
#include <atomic>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <sdkconfig.h>

// M5GFX StopWatch display setup is compiled only with OCTAL/OPI PSRAM.
#if !defined(CONFIG_SPIRAM_MODE_OCT)
#error "StopWatch requires OPI PSRAM: set board_build.arduino.memory_type = qio_opi"
#endif
#include "vinyl_core.hpp"
#include "startup_gate.hpp"
#include "label_volume.hpp"
#include "playlist_loop.hpp"

using namespace vinyl;
namespace {
constexpr uint32_t kFifoFrames = 1u << 14;     // 64KiB SPSC decoder->audio FIFO in PSRAM.
constexpr uint32_t kFifoMask = kFifoFrames - 1;
constexpr uint32_t kLeadFrames = 2 * kSampleRate; // Decoder never moves >2 s ahead.
constexpr size_t kAudioBlock = 128;             // 2.90 ms of PCM per playRaw call.
constexpr int kMotorTaskIntervalMs = 5;
constexpr uint32_t kDecoderThrottleMargin = 4096; // > one whole MP3 frame (1152 PCM frames)
constexpr uint16_t kTransparency = 0xf81f;
// StopWatch's 16-bit M5Canvas and AMOLED API take RGB565, NOT 0xRRGGBB.
// R6's 0x101216 etc. were truncated into blue-looking 16-bit values.
constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t(r >> 3) << 11) | (uint16_t(g >> 2) << 5) | uint16_t(b >> 3);
}
constexpr uint16_t kVinylBlack = rgb565(7, 7, 8);
constexpr uint16_t kVinylGroove = rgb565(20, 20, 22);
constexpr uint16_t kVinylGrooveHigh = rgb565(32, 32, 35);
constexpr uint16_t kVinylRim = rgb565(65, 65, 68);

PcmRing* gRing = nullptr;
ScratchEngine* gEngine = nullptr;
AngleLUT gAngles;
Stereo16* gPcm = nullptr;
Stereo16* gFifo = nullptr;
uint16_t* gAngleStorage = nullptr;
std::atomic<uint32_t> gFifoWrite{0};
std::atomic<uint32_t> gFifoRead{0};
std::atomic<uint16_t> gUiPhase{0};
std::atomic<uint8_t> gMotorDuty{0};
std::atomic<bool> gDecoderFinished{false};
std::atomic<bool> gAutoAdvance{false};  // EOF request from audio owner, handled by UI task.
std::atomic<bool> gAudioStarted{false};
std::atomic<bool> gAudioError{false};
std::atomic<bool> gRateUnsupported{false};
std::atomic<bool> gDemoMode{false};
std::atomic<bool> gReady{false};
std::atomic<uint32_t> gDecodedPcmFrames{0};
std::atomic<uint32_t> gTouchMoveCount{0};
std::atomic<uint32_t> gTouchGapMaxUs{0}; // Largest inter-report interval in each 2s reporting window.
std::atomic<uint32_t> gTouchDropCount{0};
std::atomic<uint32_t> gMaxTouchQueueAgeUs{0};
std::atomic<uint32_t> gSpeakerQueueFailures{0};
std::atomic<uint32_t> gMetaRevision{0};
char gTitle[128] = "track.mp3";
recordid3::Info gId3;
bool gFatStorage=false;
bool gUsbBoot=false;
bool gFormatArmed=false;
bool gUsbIsReady=false;
uint32_t gLastUsbDraw=0;
bool gEjectShown=false;
char gArtist[128] = "";
portMUX_TYPE gMetaLock = portMUX_INITIALIZER_UNLOCKED;
QueueHandle_t gTouchQueue = nullptr;
M5Canvas gLabel(&M5.Display);
// A small off-screen composition eliminates the visible erase/repaint interval
// that caused the center label to flash in R4. ~72 KiB of extra PSRAM.
constexpr int kComposeSize = 226;
constexpr int kComposeCenter = kComposeSize / 2;
M5Canvas gCenterFrame(&M5.Display);
std::atomic<bool> gPaused{false};
LabelVolumeGesture gLabelVolume;
uint8_t gUserVolume = 85; // Runtime level; no on-screen volume overlay.
std::atomic<bool> gArtReady{false};
// Becomes true only AFTER the completed ID3/folder label is composed and sent to AMOLED.
// The audio owner waits on this release/acquire handshake before queuing any PCM.
std::atomic<bool> gArtVisible{false};
std::atomic<bool> gArtStarted{false};
uint8_t* gFolderPng=nullptr;
uint32_t gFolderPngBytes=0;
uint16_t gFolderPngW=0,gFolderPngH=0;
std::atomic<uint32_t> gLoadBeginMs{0};
std::atomic<uint32_t> gArtworkElapsedMs{0};
int gCx = 233, gCy = 233, gRadius = 215;

void updateMetadata(const char* kind, const char* str) {
  if (!str || !*str) return;
  portENTER_CRITICAL(&gMetaLock);
  char* target = strcmp(kind, "Title") == 0 ? gTitle : ((strcmp(kind, "Performer") == 0 || strcmp(kind, "Artist") == 0) ? gArtist : nullptr);
  if (target) {
    strncpy(target, str, 127);
    target[127] = 0;
    gMetaRevision.fetch_add(1, std::memory_order_relaxed);
  }
  portEXIT_CRITICAL(&gMetaLock);
}

// ESP8266Audio ID3 reports UTF-16 tags separately from ordinary byte strings.
// Convert common UTF-16LE/BE + BOM tags to UTF-8; keep work off the audio task.
void metadataCallback(void*, const char* type, bool unicode, const char* raw) {
  if (!raw) return;
  if (!unicode) { updateMetadata(type, raw); return; }
  const auto* in = reinterpret_cast<const uint8_t*>(raw);
  const bool big = in[0] == 0xfe && in[1] == 0xff;
  size_t ip = (big || (in[0] == 0xff && in[1] == 0xfe)) ? 2 : 0;
  char out[128]; size_t op = 0;
  for (size_t n = 0; n < 30 && ip + 1 < 63 && op + 4 < sizeof(out); ++n) {
    uint32_t cp = big ? (in[ip] << 8 | in[ip+1]) : (in[ip] | (in[ip+1] << 8));
    ip += 2;
    if (!cp) break;
    if (cp >= 0xd800 && cp <= 0xdbff) {
      if (ip + 1 >= 63) break;
      const uint32_t low = big ? (in[ip] << 8 | in[ip+1]) : (in[ip] | (in[ip+1] << 8));
      if (low >= 0xdc00 && low <= 0xdfff) { ip += 2; cp = 0x10000 + ((cp-0xd800)<<10) + (low-0xdc00); }
      else cp = '?';
    } else if (cp >= 0xdc00 && cp <= 0xdfff) cp = '?';
    if (cp < 0x80) out[op++] = static_cast<char>(cp);
    else if (cp < 0x800) { out[op++]=0xc0|(cp>>6); out[op++]=0x80|(cp&63); }
    else if (cp < 0x10000) { out[op++]=0xe0|(cp>>12); out[op++]=0x80|((cp>>6)&63); out[op++]=0x80|(cp&63); }
    else { out[op++]=0xf0|(cp>>18); out[op++]=0x80|((cp>>12)&63); out[op++]=0x80|((cp>>6)&63); out[op++]=0x80|(cp&63); }
  }
  out[op] = 0;
  updateMetadata(type, out);
}

// Simple 256-point oscillator table, generated once. Fallback test audio is
// synthesized, not a substituted MP3. No additional audio asset is needed.
int16_t gToneLut[256];
void publishPcmFrame(Stereo16 s) {
  for (;;) {
    const uint32_t head = gFifoWrite.load(std::memory_order_relaxed);
    const uint32_t tail = gFifoRead.load(std::memory_order_acquire);
    if ((head - tail) < kFifoFrames) {
      gFifo[head & kFifoMask] = s;
      gFifoWrite.store(head + 1, std::memory_order_release);
      gDecodedPcmFrames.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
void builtInDemoTask() {
  // Every 4 quarter-seconds step through a small original four-note sequence.
  // Only integer + LUT work in this producer; the standard scratch engine is used.
  static constexpr uint32_t kNoteSteps[4] = {654, 824, 979, 734}; // Q8 table step, approximate
  uint32_t phase = 0, tick = 0;
  for (;;) {
    for (int i = 0; i < 128; ++i) {
      const uint32_t note = (tick / (kSampleRate / 2)) & 3u;
      phase += kNoteSteps[note];
      const int32_t envelope = 1024 + static_cast<int32_t>((tick % (kSampleRate/2)) * 512 / (kSampleRate/2));
      const int16_t v = static_cast<int16_t>((static_cast<int32_t>(gToneLut[(phase >> 8) & 255]) * envelope) >> 11);
      publishPcmFrame({v, v});
      ++tick;
    }
    if ((tick & 4095u) == 0) taskYIELD();
  }
}

// The decoder owns writeIndex, audio owns readIndex. No per-sample mutex or malloc.
class AudioCapture : public AudioOutput {
public:
  bool begin() override { return true; }
  bool stop() override { return true; }
  bool SetRate(int hz) override {
    if (hz != static_cast<int>(kSampleRate)) {
      gRateUnsupported.store(true);
      return false;
    }
    return AudioOutput::SetRate(hz);
  }
  bool ConsumeSample(int16_t sample[2]) override {
    const uint32_t head = gFifoWrite.load(std::memory_order_relaxed);
    const uint32_t tail = gFifoRead.load(std::memory_order_acquire);
    if ((head - tail) >= kFifoFrames) return false;
    Stereo16 stereo { sample[0], sample[1] };
    if (channels == 1) stereo.r = stereo.l;
    gFifo[head & kFifoMask] = stereo;
    gFifoWrite.store(head + 1, std::memory_order_release);
    gDecodedPcmFrames.fetch_add(1, std::memory_order_relaxed);
    return true;
  }
};

// Avoid AudioFileSourceID3's per-byte full APIC scan on the realtime startup
// path. Only skip the leading ID3 tag; metadata/artwork are read later, after
// the first audible PCM has begun playing. All seeks use a logical MP3 offset.
class FastMp3Source final : public AudioFileSource {
 public:
  explicit FastMp3Source(AudioFileSource& base):src(base) {}
  bool open(const char* path) override {
    if(!src.open(path))return false;
    start=0;
    uint8_t h[10]={};
    if(src.read(h,10)==10 && h[0]=='I' && h[1]=='D' && h[2]=='3' && h[3]>=2&&h[3]<=4){
      start=10+(uint32_t(h[6]&127)<<21)+(uint32_t(h[7]&127)<<14)
             +(uint32_t(h[8]&127)<<7)+uint32_t(h[9]&127);
      if(h[3]==4 && (h[5]&0x10))start+=10; // ID3v2.4 footer
      if(start>=src.getSize())start=0;
    }
    return src.seek(start,SEEK_SET);
  }
  uint32_t read(void* p,uint32_t n) override{return src.read(p,n);}
  bool seek(int32_t p,int whence) override {
    if(whence==SEEK_SET)return p>=0&&src.seek(int32_t(start)+p,SEEK_SET);
    if(whence==SEEK_END)return src.seek(p,SEEK_END);
    return src.seek(p,SEEK_CUR);
  }
  bool close() override{return src.close();}
  bool isOpen() override{return src.isOpen();}
  uint32_t getSize() override{return src.getSize()-start;}
  uint32_t getPos() override{uint32_t p=src.getPos();return p>start?p-start:0;}
 private:AudioFileSource& src;uint32_t start=0;
};
void artworkTask(void*) {
  // R9: prepare the ID3/folder art BEFORE the first audible PCM.
  // The player draws LOADING while this task reads metadata and cover art.
  const uint32_t startMs=millis();
  gArtStarted.store(true,std::memory_order_release);
  if(!gDemoMode.load()){
    AudioFileSourceFS legacy(LittleFS);
    musicflash::FileSource fat;
    AudioFileSource* src=gFatStorage?static_cast<AudioFileSource*>(&fat):static_cast<AudioFileSource*>(&legacy);
    if(src->open("/track.mp3")){
      recordid3::read(*src,gId3);
      src->close();
      if(gId3.title[0])updateMetadata("Title",gId3.title);
      if(gId3.artist[0])updateMetadata("Artist",gId3.artist);
    }
    if(!gId3.image()&&gFatStorage){
      musicflash::loadFirstFolderPng(gFolderPng,gFolderPngBytes,gFolderPngW,gFolderPngH);
    }
  }
  gArtworkElapsedMs.store(millis()-startMs,std::memory_order_release);
  gMetaRevision.fetch_add(1,std::memory_order_release);
  // Publish all artwork/metadata writes before the UI is permitted to read.
  gArtReady.store(true,std::memory_order_release);
  Serial.printf("VinylM5 ART: ID3=%s folderPNG=%s delay=%lu ms\n",gId3.image()?"YES":"NO",
    gFolderPng?"YES":"NO",(unsigned long)gArtworkElapsedMs.load());
  vTaskDelete(nullptr);
}

void decoderTask(void*) {
  if (gDemoMode.load(std::memory_order_acquire)) {
    builtInDemoTask();
  }
  AudioFileSourceFS legacy(LittleFS);
  musicflash::FileSource fatSource;
  AudioFileSource* src=gFatStorage ? static_cast<AudioFileSource*>(&fatSource)
                                  : static_cast<AudioFileSource*>(&legacy);
  FastMp3Source fast(*src);
  AudioGeneratorMP3 mp3;
  AudioCapture capture;
  // Decode PCM while artwork loads. Playback is gated until the cover is visible.
  // A ONE-track playlist stays in the same decoder task; reopening its MP3
  // appends samples into the SAME FIFO / scratch ring (no reboot at repeat).
  // In a MULTI-track playlist, the decoder ends once and the audio owner
  // requests the next track only when the audible playhead has passed EOF.
  const bool continuousOneTrack = playlistloop::repeatInDecoder(
      gFatStorage ? musicflash::trackCount() : 1);
  if (!fast.open("/track.mp3") || !mp3.begin(&fast, &capture)) {
    gAudioError.store(true);
    gDemoMode.store(true, std::memory_order_release);
    Serial.println("VinylM5: MP3 begin failed; using DEMO audio.");
    builtInDemoTask();
    return;
  }
  bool decodeError = false;
  for (;;) {
    const uint32_t copyStart = gDecodedPcmFrames.load(std::memory_order_acquire);
    while (mp3.isRunning() && !gRateUnsupported.load(std::memory_order_acquire)) {
      const uint32_t queued = gFifoWrite.load(std::memory_order_acquire)
                            - gFifoRead.load(std::memory_order_acquire);
      if (queued >= kFifoFrames - kDecoderThrottleMargin) {
        vTaskDelay(pdMS_TO_TICKS(1));
        continue;
      }
      if (!mp3.loop()) break;
      taskYIELD();
    }
    mp3.stop();
    fast.close();
    const uint32_t decodedThisCopy = gDecodedPcmFrames.load(std::memory_order_acquire) - copyStart;
    if (gRateUnsupported.load(std::memory_order_acquire) || decodedThisCopy < 4096) {
      decodeError = true;
      break; // Prevent a tight loop on corrupt, empty or unsupported files.
    }
    if (!continuousOneTrack) break;
    // Reuse the existing decoder / FIFO without restarting the display,
    // speaker or turntable. The next pass is already buffered ahead of EOF.
    if (!fast.open("/track.mp3") || !mp3.begin(&fast, &capture)) {
      decodeError = true;
      break;
    }
    Serial.println("VinylM5: repeating single MP3");
  }
  gDecoderFinished.store(true, std::memory_order_release);
  if (decodeError) {
    gAudioError.store(true);
    if (gDecodedPcmFrames.load(std::memory_order_acquire) < 4096) {
      gDemoMode.store(true, std::memory_order_release);
      Serial.println("VinylM5: no usable MP3 PCM; using DEMO audio.");
      builtInDemoTask();
    } else {
      Serial.println("VinylM5: decoder stopped; restart selection after buffered PCM drains.");
    }
  }
  vTaskDelete(nullptr);
}

struct TouchEvent { uint8_t command; uint16_t phase; uint32_t elapsedUs; uint32_t stampedUs; };
// command 1=press 2=move 3=release; single audio-task owner avoids 64-bit tearing.
void processTouchEvents() {
  TouchEvent event;
  while (xQueueReceive(gTouchQueue, &event, 0) == pdPASS) {
    uint32_t ageUs = micros() - event.stampedUs;
    uint32_t prevMax = gMaxTouchQueueAgeUs.load(std::memory_order_relaxed);
    if (ageUs > prevMax) gMaxTouchQueueAgeUs.store(ageUs, std::memory_order_relaxed);
    if (event.command == 1) gEngine->touchDown(event.phase);
    else if (event.command == 2) { gEngine->touchMove(event.phase, event.elapsedUs); gTouchMoveCount.fetch_add(1, std::memory_order_relaxed); }
    else if (event.command == 3) gEngine->touchUp();
  }
}

// The only caller of PcmRing and ScratchEngine; never allocate in the audio loop.
void audioTask(void*) {
  static Stereo16 buffer[3][kAudioBlock]; // Speaker uses pointers, so triple-buffer lifetime.
  uint32_t blk = 0;
  uint32_t uiCounter = 0;
  Stereo16 incoming[256];
  playlistloop::EofDrain eofDrain;
  while (true) {
    processTouchEvents();
    static bool wasPaused=false;
    if(gPaused.load(std::memory_order_acquire)){
      eofDrain.reset();
      if(!wasPaused){M5.Speaker.stop(uint8_t(0));wasPaused=true;}
      gMotorDuty.store(0,std::memory_order_release);
      vTaskDelay(pdMS_TO_TICKS(2));
      continue;
    }
    wasPaused=false;
    // Keep 2 s lookahead, preserving >18 s history in a 4MiB ring.
    if ((static_cast<int64_t>(gRing->newest()) - (gEngine->positionQ16() >> 16)) < kLeadFrames) {
      uint32_t r = gFifoRead.load(std::memory_order_relaxed);
      const uint32_t w = gFifoWrite.load(std::memory_order_acquire);
      const uint32_t count = (w - r) < 256 ? (w - r) : 256;
      for (uint32_t i = 0; i < count; ++i) incoming[i] = gFifo[(r + i) & kFifoMask];
      if (count) {
        const uint64_t playhead = static_cast<uint64_t>(gEngine->positionQ16() >> 16);
        const size_t accepted = gRing->append(incoming, count, playhead, gEngine->scratching());
        gFifoRead.store(r + static_cast<uint32_t>(accepted), std::memory_order_release);
      }
    }
    // Never start the first audible PCM until (a) the PCM buffer is primed
    // AND (b) the final cover/label has already been pushed to the display.
    // This also covers the no-artwork and DEMO cases: their default label
    // must be drawn first. No dependency on gAudioStarted in artworkTask.
    if (!gAudioStarted.load(std::memory_order_acquire)) {
      const bool pcmPrimed = gRing->newest() > 4096;
      const bool artworkOnScreen = gArtVisible.load(std::memory_order_acquire);
      if (startupgate::canStartPlayback(pcmPrimed, artworkOnScreen)) {
        gAudioStarted.store(true, std::memory_order_release);
        Serial.printf("VinylM5 R11 START: art drawn, PCM primed, art load %lu ms\n",
                      (unsigned long)gArtworkElapsedMs.load());
      } else { vTaskDelay(pdMS_TO_TICKS(1)); continue; }
    }
    if (M5.Speaker.isPlaying(0) >= 2) {
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }
    gEngine->render(buffer[blk], kAudioBlock);
    if (!M5.Speaker.playRaw(&buffer[blk][0].l, kAudioBlock * 2, kSampleRate, true, 1, 0, false)) {
      gSpeakerQueueFailures.fetch_add(1, std::memory_order_relaxed);
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }
    // Observe the post-render playhead *after* the speaker accepted this PCM.
    // The existing ScratchEngine clamps at newest()-3, so a raw seek-beyond-
    // EOF test would never fire. Pauses and scratches reset the drain counter.
    if (!gAutoAdvance.load(std::memory_order_acquire) &&
        eofDrain.onAudioQueued(gFatStorage ? musicflash::trackCount() : 1,
          gDecoderFinished.load(std::memory_order_acquire),
          gPaused.load(std::memory_order_acquire), gEngine->scratching(),
          gFifoRead.load(std::memory_order_acquire) == gFifoWrite.load(std::memory_order_acquire),
          gEngine->positionQ16(), gRing->newest(), kAudioBlock)) {
      gAutoAdvance.store(true, std::memory_order_release);
    }
    blk = (blk + 1) % 3;
    if ((++uiCounter & 7u) == 0) {
      constexpr int64_t revQ16 = static_cast<int64_t>(kFramesPerRevolution) << 16;
      int64_t p = gEngine->positionQ16() % revQ16;
      if (p < 0) p += revQ16;
      gUiPhase.store(static_cast<uint16_t>(p * kPhaseUnits / revQ16), std::memory_order_release);
    }
    gMotorDuty.store(gEngine->hapticValue(millis()), std::memory_order_release);
  }
}

String cropUtf8(String s, int maxPx) {
  // Remove a complete UTF-8 codepoint from the end until its measured width fits.
  while (s.length() && gLabel.textWidth(s) > maxPx) {
    int i = static_cast<int>(s.length()) - 1;
    while (i > 0 && (static_cast<uint8_t>(s[i]) & 0xc0u) == 0x80u) --i;
    s.remove(i);
  }
  return s;
}

void rebuildLabel() {
  char title[128],artist[128];
  portENTER_CRITICAL(&gMetaLock);
  memcpy(title,gTitle,sizeof(title));memcpy(artist,gArtist,sizeof(artist));
  portEXIT_CRITICAL(&gMetaLock);
  const int c=105;
  gLabel.fillSprite(kTransparency);
  gLabel.fillCircle(c,c,99,rgb565(16,16,17));
  gLabel.fillCircle(c,c,97,rgb565(78,62,45));
  gLabel.fillCircle(c,c,94,rgb565(36,32,29));
  // Artwork is decoded once when changing tracks, not during frame rotation.
  const uint8_t* img=gId3.image();
  uint32_t imgBytes=gId3.imageBytes();
  uint16_t iw=gId3.width,ih=gId3.height;
  bool png=gId3.format==recordid3::ImageFormat::Png;
  if(!img && gFolderPng){img=gFolderPng;imgBytes=gFolderPngBytes;iw=gFolderPngW;ih=gFolderPngH;png=true;}
  bool coverDrawn=false;
  if(img&&imgBytes&&iw&&ih){
    const float scale=std::min(188.f/iw,188.f/ih);
    const int w=std::max(1,int(iw*scale)),h=std::max(1,int(ih*scale));
    coverDrawn=png?gLabel.drawPng(img,imgBytes,c-w/2,c-h/2,w,h,0,0,scale,scale)
                 :gLabel.drawJpg(img,imgBytes,c-w/2,c-h/2,w,h,0,0,scale,scale);
    if(coverDrawn){
      for(int y=8;y<202;y++)for(int x=8;x<202;x++){
        int dx=x-c,dy=y-c;
        if(dx*dx+dy*dy>94*94)gLabel.drawPixel(x,y,kTransparency);
      }
    }
  }
  gLabel.drawCircle(c,c,99,rgb565(155,145,132));
  gLabel.drawCircle(c,c,94,rgb565(80,77,73));
  gLabel.fillCircle(c,c,12,rgb565(139,139,139));
  gLabel.fillCircle(c,c,5,rgb565(9,9,9));
  gLabel.setTextDatum(middle_center);
  gLabel.setFont(&fonts::efontJA_24);
  // Foreground only: background is transparent, jacket art remains visible.
  gLabel.setTextColor(rgb565(244,244,240));
  String titleText=cropUtf8(String(title),174);
  String artistText=cropUtf8(String(artist),174);
  if(titleText.length())gLabel.drawString(titleText,c,60);
  if(artistText.length())gLabel.drawString(artistText,c,147);
  else gLabel.drawString("33 1/3 RPM",c,147);
}

void drawFixedPlatter() {
  // Fully neutral black/charcoal RGB565: the stationary reflection remains fixed.
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.fillCircle(gCx,gCy,gRadius+10,rgb565(13,13,14));
  M5.Display.drawCircle(gCx,gCy,gRadius+10,kVinylRim);
  M5.Display.fillCircle(gCx,gCy,gRadius,kVinylBlack);
  for(int r=92;r<gRadius-5;r+=3)
    M5.Display.drawCircle(gCx,gCy,r,(r%9)?kVinylGroove:kVinylGrooveHigh);
  // No diagonal highlights: they appeared as scratches on the vinyl.
  // Keep only the neutral-black surface and concentric grooves.
}
void prepareCenterBackground() {
  gCenterFrame.fillSprite(kVinylBlack);
  for (int r = 92; r < gRadius - 5; r += 3)
    gCenterFrame.drawCircle(kComposeCenter,kComposeCenter,r,(r%9)?kVinylGroove:kVinylGrooveHigh);
}
void drawPlatterFrame() {
  const uint16_t phase=gUiPhase.load(std::memory_order_acquire);
  // No rotational-position marker on the vinyl. The artwork/label alone rotates.
  prepareCenterBackground();
  gLabel.pushRotateZoom(&gCenterFrame,
    float(kComposeCenter),float(kComposeCenter),
    float(phase)*(360.0f/kPhaseUnits),1.0f,1.0f,kTransparency);
  // Do not claim 100% ready before the artwork is on-screen. While parsing
  // JPEG/PNG, show an indeterminate animated bar instead of PCM percent.
  if(!gAudioStarted.load(std::memory_order_acquire)){
    const bool parsed = gArtReady.load(std::memory_order_acquire);
    const bool drawn = gArtVisible.load(std::memory_order_acquire);
    gCenterFrame.setTextDatum(middle_center);
    gCenterFrame.setFont(&fonts::Font4);
    gCenterFrame.setTextSize(1);
    gCenterFrame.setTextColor(rgb565(255,242,196));
    gCenterFrame.drawString(!parsed ? "LOADING ART..." :
                            !drawn ? "DRAWING LABEL..." : "LOADING AUDIO...",
                            kComposeCenter,kComposeCenter+23);
    gCenterFrame.drawRoundRect(kComposeCenter-47,kComposeCenter+51,94,7,3,rgb565(148,148,148));
    if (!drawn) {
      const uint32_t cursor=(millis()/36u)%66u;
      gCenterFrame.fillRect(kComposeCenter-44+cursor,kComposeCenter+53,22,3,rgb565(230,213,171));
    } else {
      const uint32_t frames=gDecodedPcmFrames.load(std::memory_order_acquire);
      const uint32_t done=std::min(88u,frames*88u/4096u);
      if(done) gCenterFrame.fillRect(kComposeCenter-44,kComposeCenter+53,done,3,rgb565(230,213,171));
    }
  }else if(gPaused.load(std::memory_order_acquire)){
    gCenterFrame.setTextDatum(middle_center);
    gCenterFrame.setFont(&fonts::Font4);
    gCenterFrame.setTextSize(1);
    gCenterFrame.setTextColor(rgb565(255,242,196));
    gCenterFrame.drawString("PAUSED",kComposeCenter,kComposeCenter+23);
  }
  gCenterFrame.pushSprite(gCx-kComposeCenter,gCy-kComposeCenter);
}

// Called after M5.begin(), BEFORE PSRAM allocation and LittleFS access.
// No sprites, MP3 decoder, Japanese font or large buffers are needed here.
void bootScreen(const char* stage, uint16_t color = 0x07E0) {
  if (M5.Display.width() < 100 || M5.Display.height() < 100) return;
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(color, TFT_BLACK);
  M5.Display.setFont(&fonts::Font4);
  M5.Display.setTextSize(2);
  M5.Display.drawString("VinylM5", M5.Display.width()/2, M5.Display.height()/2 - 42);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.drawString(stage, M5.Display.width()/2, M5.Display.height()/2 + 24);
  M5.Display.setTextSize(1);
}
void failScreen(const char* message) {
  Serial.printf("VinylM5 R11 ERROR: %s\n", message);
  bootScreen(message, 0xF800);
}
void usbScreen(const char* heading,const char* sub1,const char* sub2){
 M5.Display.fillScreen(TFT_BLACK);
 M5.Display.setTextDatum(middle_center);
 M5.Display.setFont(&fonts::Font4);
 M5.Display.setTextSize(2);
 M5.Display.setTextColor(0x07FF,TFT_BLACK);
 M5.Display.drawString(heading,gCx,110);
 M5.Display.setTextSize(1);
 M5.Display.setTextColor(TFT_WHITE,TFT_BLACK);
 M5.Display.drawString(sub1,gCx,220);
 M5.Display.drawString(sub2,gCx,275);
}
// R7's USB request is one-shot. A power cycle must ALWAYS boot the player:
// R6 stored "usb=true" indefinitely and could trap users in USB DRIVE.
void setUsbBoot(bool enabled){
  Preferences prefs;
  if(prefs.begin("vinyl",false)) {
    prefs.putUChar("usbver",7);
    prefs.putBool("usb",enabled);
    prefs.end();
  }
}
void returnToPlayer() {
  if (gUsbIsReady) {
    usbmusic::flush();
    usbmusic::stopPresenting();
  }
  setUsbBoot(false);
  M5.Power.setVibration(0);
  usbScreen("VinylM5","RETURNING","PLEASE WAIT");
  delay(100);
  ESP.restart();
}
void requestUsbMode(){
  M5.Power.setVibration(0);
  usbScreen("USB DRIVE","SWITCHING MODE","PLEASE WAIT");
  setUsbBoot(true);
  delay(200);
  ESP.restart();
}
void usbModeSetup(){
  gUsbBoot=true;
  musicflash::begin();
  if(!musicflash::isFat16()){
    usbScreen("USB DRIVE","FORMAT REQUIRED","TAP CENTER TWICE");
    return;
  }
  gUsbIsReady=usbmusic::begin();
  if(gUsbIsReady) usbScreen("VinylM5 USB","COPY MP3 IN WINDOWS","EJECT / HOLD CENTER");
  else usbScreen("USB ERROR","USB OTG NOT READY","HOLD CENTER TO EXIT");
}
void usbModeTick(){
  M5.update();
  const auto touch=M5.Touch.getDetail();
  static bool lastCenter=false;
  static uint32_t downAt=0;
  static bool warningShown=false;
  const int dx=int(touch.x)-gCx,dy=int(touch.y)-gCy;
  const bool center=touch.isPressed() && dx*dx+dy*dy<90*90;
  const uint32_t now=millis();
  if(center && !lastCenter){downAt=now;warningShown=false;}
  const uint32_t heldMs=center?now-downAt:0;

  // Exit is a direct, discoverable touch action (StopWatch has no B button).
  // After Windows eject, 0.8 s is enough. If Windows does not send EJECT,
  // require a deliberate 7s hold and at least 5s since the last host write.
  if(gUsbIsReady && center){
    if(usbmusic::isEjected() && heldMs>=800) returnToPlayer();
    if(heldMs>=7000 && usbmusic::writeIdleFor(5000)) returnToPlayer();
    if(heldMs>=1200 && !warningShown && !usbmusic::isEjected()){
      usbScreen("USB DRIVE","WINDOWS: EJECT FIRST","HOLD 7S TO EXIT");
      warningShown=true;
    }
  }
  // An uninitialized USB volume can be cancelled without erasing anything.
  if(!gUsbIsReady && !musicflash::isFat16() && center && heldMs>=1800)
    returnToPlayer();
  if(!gUsbIsReady && musicflash::isFat16() && center && heldMs>=1200)
    returnToPlayer();

  if(!center && lastCenter && !gUsbIsReady && !musicflash::isFat16()){
    const uint32_t pressDuration=now-downAt;
    if(pressDuration<650){
      if(!gFormatArmed){
        gFormatArmed=true;
        usbScreen("ERASE MUSIC?","OLD MP3 WILL GO","TAP AGAIN: FORMAT");
      }else{
        usbScreen("FORMATTING","ERASING MUSIC AREA","DO NOT POWER OFF");
        const bool ok=musicflash::formatFAT16();
        gUsbIsReady=ok && usbmusic::begin();
        usbScreen(gUsbIsReady?"VinylM5 USB":"FORMAT ERROR",
          gUsbIsReady?"COPY MP3 IN WINDOWS":"HOLD CENTER TO EXIT",
          gUsbIsReady?"EJECT / HOLD CENTER":"CHECK FLASH");
      }
    }
  }
  lastCenter=center;
  if(gUsbIsReady && usbmusic::isEjected() && !gEjectShown){
    usbScreen("EJECTED","COPY COMPLETED","HOLD CENTER TO PLAY");
    gEjectShown=true;
  }
  if(gUsbIsReady && !usbmusic::isEjected() && now-gLastUsbDraw>400){
    usbmusic::flush();gLastUsbDraw=now;
  }
  delay(5);
}


}

void setup() {
  Serial.begin(115200);
  Serial.printf("VinylM5 R11: boot, reset reason=%d, psram=%u\n",
     static_cast<int>(esp_reset_reason()),
     static_cast<unsigned>(ESP.getPsramSize()));
  auto cfg = M5.config();
  // esp32s3box is only a generic PlatformIO build target; do not fall back to
  // AtomS3Lite when StopWatch autodetection fails. The StopWatch has an
  // M5IOE1-powered CO5300 AMOLED and needs the correct board pinmap.
  cfg.fallback_board = m5::board_t::board_M5StopWatch;
  cfg.internal_mic = false;
  cfg.internal_imu = false;
  cfg.internal_rtc = false;
  cfg.external_imu = false;
  cfg.external_rtc = false;
  cfg.internal_spk = true;
  M5.begin(cfg);
  Serial.printf("VinylM5 R11: M5.begin done; board=%d, display=%dx%d, psram=%u\n",
     static_cast<int>(M5.getBoard()), M5.Display.width(), M5.Display.height(),
     static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
  // Avoid a second hardware panel reset on a possible power/IOE failure.
  // M5.begin() is the one supported board-specific initialization path.
  M5.Display.setRotation(0);
  M5.Display.setBrightness(180);
  const int w = M5.Display.width();
  const int h = M5.Display.height();
  if (w < 400 || h < 400) {
    failScreen("DISPLAY ERROR");
    return;
  }
  gCx=w/2;gCy=h/2;
  Preferences prefs;
  // Consume the one-shot request before USB is exposed. This also clears
  // the stale persistent R6 USB flag on the first R7 boot (usbver != 7).
  if(prefs.begin("vinyl",false)) {
    gUsbBoot=prefs.getUChar("usbver",0)==7 && prefs.getBool("usb",false);
    prefs.putBool("usb",false);
    prefs.end();
  }
  if(gUsbBoot){usbModeSetup();return;}
  bootScreen("LOADING...");
  Serial.println("VinylM5 R11: first visible frame drawn");
  gCx = w / 2; gCy = h / 2;
  gRadius = ((w < h) ? w : h) / 2 - 17;

  // Allocation order deliberately leaves plenty of RAM for the system and decoder.
  gPcm = static_cast<Stereo16*>(heap_caps_malloc(kCapacityFrames * sizeof(Stereo16), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  gFifo = static_cast<Stereo16*>(heap_caps_malloc(kFifoFrames * sizeof(Stereo16), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  gAngleStorage = static_cast<uint16_t*>(heap_caps_malloc(static_cast<size_t>(w) * h * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!gPcm || !gFifo || !gAngleStorage) { failScreen("PSRAM ERROR"); return; }
  if (!gAngles.initialize(gAngleStorage, w, h, 98, gRadius - 7)) { failScreen("TOUCH LUT ERROR"); return; }
  static PcmRing ring(gPcm);
  static ScratchEngine engine(ring);
  gRing = &ring; gEngine = &engine;
  gTouchQueue = xQueueCreate(32, sizeof(TouchEvent));
  if (!gTouchQueue) { failScreen("QUEUE ERROR"); return; }
  bootScreen("LOADING / PREPARING...");
  gLabel.setColorDepth(16);
  gLabel.setPsram(true);
  if (!gLabel.createSprite(210, 210)) { failScreen("SPRITE ERROR"); return; }
  gLabel.setPivot(105,105); // Always rotate around the disc centre, including APIC artwork.
  rebuildLabel();
  gCenterFrame.setColorDepth(16);
  gCenterFrame.setPsram(true);
  if (!gCenterFrame.createSprite(kComposeSize, kComposeSize)) { failScreen("COMPOSE ERROR"); return; }
  drawFixedPlatter();
  drawPlatterFrame();
  Serial.println("VinylM5 R11: turntable drawn");
  gLoadBeginMs.store(millis(),std::memory_order_release);
  const bool partitionOk=musicflash::begin();
  gFatStorage=partitionOk && musicflash::isFat16();
  Preferences selection;
  uint8_t folderIndex=0;uint16_t trackIndex=0;
  if(selection.begin("vinyl",true)){
    folderIndex=selection.getUChar("folder",0);
    trackIndex=selection.getUShort("track",0);
    selection.end();
  }
  const bool fatHasMp3=gFatStorage&&musicflash::selectByIndex(folderIndex,trackIndex);
  const bool legacyHasMp3=!gFatStorage&&LittleFS.begin(false)&&LittleFS.exists("/track.mp3");
  const bool hasMp3=fatHasMp3||legacyHasMp3;
  gDemoMode.store(!hasMp3,std::memory_order_release);
  if(fatHasMp3){
    strncpy(gTitle,musicflash::selectedTrack().filename,sizeof(gTitle)-1);
    gTitle[sizeof(gTitle)-1]=0;
    Serial.printf("VinylM5 CATALOG: %s, track %u/%u, FAT cached\n",musicflash::folderName(),
     unsigned(musicflash::trackIndex()+1),unsigned(musicflash::trackCount()));
  }else if(legacyHasMp3){strncpy(gTitle,"track.mp3",sizeof(gTitle)-1);}
  else {
    strncpy(gTitle,"DEMO TRACK",sizeof(gTitle)-1);
    strncpy(gArtist,"VinylM5",sizeof(gArtist)-1);
  }
  rebuildLabel(); // Show default/filename while ID3 and APIC are loading.
  drawPlatterFrame();
  // DEMO has no ID3/APIC parsing task: mark the standard label complete.
  if (!hasMp3) gArtReady.store(true, std::memory_order_release);
  for (uint32_t i = 0; i < 256; ++i) {
    gToneLut[i] = static_cast<int16_t>(sinf((i * 6.28318530718f) / 256.f) * 8000);
  }

  // Avoid masking a visible platter if MP3/LittleFS was missing.
  auto spkConfig = M5.Speaker.config();
  spkConfig.sample_rate = kSampleRate;
  spkConfig.stereo = false;               // built-in physical speaker is mono
  spkConfig.dma_buf_len = 128;
  spkConfig.dma_buf_count = 3;
  spkConfig.task_priority = 6;
  spkConfig.task_pinned_core = 1;
  M5.Speaker.config(spkConfig);
  if (!M5.Speaker.begin()) { failScreen("SPEAKER ERROR"); return; }
  M5.Speaker.setVolume(gUserVolume);
  M5.Power.setVibration(0);
  if (xTaskCreatePinnedToCore(audioTask, "vinyl-audio", 6144, nullptr, 5, nullptr, 1) != pdPASS ||
      xTaskCreatePinnedToCore(decoderTask, "vinyl-mp3", 6144, nullptr, 2, nullptr, 0) != pdPASS) {
    failScreen("TASK ERROR");
    return;
  }
  if(hasMp3 && xTaskCreatePinnedToCore(artworkTask,"vinyl-art",6144,nullptr,1,nullptr,0)!=pdPASS){
    Serial.println("VinylM5 R11 WARNING: artwork task unavailable; using default label");
    // Do not deadlock startup if artwork task allocation fails.
    gMetaRevision.fetch_add(1,std::memory_order_release);
    gArtReady.store(true,std::memory_order_release);
  }
  gReady.store(true, std::memory_order_release);
  Serial.printf("VinylM5 R11 START | %dx%d | PSRAM free=%u | LittleFS MP3=%s\n",
                w, h, static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)), hasMp3 ? "YES" : "NO - DEMO");
}

// A left/right hardware key press changes the cached selection. Reboot is
// currently the safest deterministic switch: no old decoder and new decoder
// can ever race over the same PSRAM playback ring. Fast FAT cache + ID3 defer
// minimize the visible reload. Does not touch Flash partitions or MP3 files.
void changeTrackOrFolder(bool folder){
 if(!gFatStorage)return;
 uint8_t nextFolder=musicflash::folderIndex();
 uint16_t nextTrack=musicflash::trackIndex();
 if(folder){nextFolder=musicflash::nextFolderIndex();nextTrack=0;}
 else nextTrack=musicflash::nextTrackIndex();
 Preferences prefs;
 if(prefs.begin("vinyl",false)){
   prefs.putUChar("folder",nextFolder);prefs.putUShort("track",nextTrack);prefs.end();
 }
 M5.Power.setVibration(0);
 bootScreen(folder?"LOADING FOLDER...":"LOADING NEXT TRACK...");
 delay(80);
 ESP.restart();
}

void loop() {
  if(gUsbBoot){usbModeTick();return;}
  if (!gReady.load(std::memory_order_acquire)) {
    // Keep any failure message on the AMOLED. Do not touch null engine pointers.
    delay(100);
    return;
  }
  M5.update();
  if (gAutoAdvance.exchange(false, std::memory_order_acq_rel)) {
    if (gFatStorage) {
      // nextTrackIndex wraps to zero in the current folder.
      changeTrackOrFolder(false);
    } else {
      // Legacy LittleFS has only /track.mp3, so restart the same file.
      M5.Power.setVibration(0);
      bootScreen("REPEATING TRACK...");
      delay(80);
      ESP.restart();
    }
  }
  // Physical keys: KEY A (yellow / GPIO2) = folder; KEY B (blue /
  // GPIO1) = pause/play on single click and next track on double click.
  // First click acts immediately; a second click within 360ms loads next.
  // Disc touch remains exclusively for DJ scratching.
  static uint32_t lastRightRelease=0;
  const uint32_t keyNow=millis();
  if(M5.BtnB.wasReleased()){
    if(lastRightRelease && (keyNow-lastRightRelease)<360){
      lastRightRelease=0;
      changeTrackOrFolder(false);
    }else {
      // Single click acts immediately for DJ operation; a second click
      // within 360ms switches track rather than delaying first response.
      gPaused.store(!gPaused.load(std::memory_order_acquire),std::memory_order_release);
      lastRightRelease=keyNow;
    }
  }
  if(M5.BtnA.wasReleased())changeTrackOrFolder(true);
  static bool wasPressed = false;
  static uint16_t lastPhase = 0;
  static uint32_t lastSampleTime = 0;
  static int lastX = -1, lastY = -1;
  auto touch = M5.Touch.getDetail();
  // Upper/lower parts of the printed label are invisible volume buttons.
  // A click is <=300ms with <=12px displacement; scratches never count.
  const auto volumeAction = gLabelVolume.update(touch.isPressed(),
      touch.x, touch.y, millis(), gCx, gCy);
  if (volumeAction != LabelVolumeGesture::Action::None) {
    constexpr int kVolumeStep = 12;
    const int value = int(gUserVolume) +
        (volumeAction == LabelVolumeGesture::Action::Up ? kVolumeStep : -kVolumeStep);
    gUserVolume = static_cast<uint8_t>(std::max(0, std::min(200, value)));
    M5.Speaker.setVolume(gUserVolume);
    // Deliberately no visual overlay and no synchronous NVS write here.
  }
  // Only the centre-label hub long press enters USB mode.
  static uint32_t centerSince=0;
  const int ddx=int(touch.x)-gCx, ddy=int(touch.y)-gCy;
  // USB entry is the spindle HUB only. Label quadrants remain volume keys.
  const bool centerTouch=touch.isPressed() && ddx*ddx+ddy*ddy<27*27;
  if(centerTouch){if(!centerSince)centerSince=millis();
    if(millis()-centerSince>=900)requestUsbMode();
  } else centerSince=0;
  if (gTouchQueue && gAudioStarted.load()) {
    const bool pressed = touch.isPressed();
    const uint16_t packed = pressed ? gAngles.packedAt(touch.x, touch.y) : 0;
    if (pressed && AngleLUT::valid(packed)) {
      const uint16_t phase = AngleLUT::phase(packed);
      if (!wasPressed) {
        const TouchEvent e{1, phase, 0, micros()};
        if (xQueueSend(gTouchQueue, &e, 0) == pdPASS) {
          wasPressed = true;
          lastPhase = phase;
          lastX = touch.x; lastY = touch.y;
          lastSampleTime = e.stampedUs;
        } else {
          gTouchDropCount++;
        }
      } else if (lastX != touch.x || lastY != touch.y) {
        const uint32_t now = micros();
        const TouchEvent e{2, phase, now - lastSampleTime, now};
        if (xQueueSend(gTouchQueue, &e, 0) == pdPASS) {
          const uint32_t priorMax = gTouchGapMaxUs.load(std::memory_order_relaxed);
          if (e.elapsedUs > priorMax) gTouchGapMaxUs.store(e.elapsedUs, std::memory_order_relaxed);
          lastPhase = phase;
          lastX = touch.x; lastY = touch.y;
          lastSampleTime = now;
        } else {
          // Keep the previous accepted timestamp. Otherwise the next move
          // describes the entire accumulated angular delta as if it happened
          // in a tiny interval, creating a spurious high scratch velocity.
          gTouchDropCount++;
        }
      }
    } else if (wasPressed && !pressed) {
      const TouchEvent e{3, lastPhase, 0, micros()};
      // A release must never be lost, even if a burst of move reports filled the queue.
      if (xQueueSend(gTouchQueue, &e, 0) != pdPASS) {
        TouchEvent discarded;
        xQueueReceive(gTouchQueue, &discarded, 0);
        xQueueSend(gTouchQueue, &e, 0);
        gTouchDropCount++;
      }
      wasPressed = false;
    }
  }
  static uint32_t lastMotorUpdate = 0;
  static uint8_t lastMotorDuty = 0;
  if (millis() - lastMotorUpdate >= kMotorTaskIntervalMs) {
    lastMotorUpdate = millis();
    const uint8_t level = gMotorDuty.load(std::memory_order_acquire);
    if (level != lastMotorDuty) {
      M5.Power.setVibration(level);
      lastMotorDuty = level;
    }
  }
  static uint32_t lastFrame = 0;
  static uint32_t lastMetaRev = ~0u;
  const bool artworkParsed = gArtReady.load(std::memory_order_acquire);
  const uint32_t rev = gMetaRevision.load(std::memory_order_acquire);
  // Release-ordered presentation gate: do not rebuild from the worker's
  // gId3/gFolderPng until it has finished writing ALL image data. Do not
  // release the audio engine until the finished label is on the AMOLED.
  if (artworkParsed && (rev != lastMetaRev || !gArtVisible.load(std::memory_order_relaxed))) {
    rebuildLabel();
    lastMetaRev = rev;
    drawPlatterFrame();
    gArtVisible.store(true,std::memory_order_release);
  }
  if (millis() - lastFrame >= 33) {
    lastFrame = millis();
    drawPlatterFrame();
  }
  if (gRateUnsupported.load()) {
    // Serial-only warning: the product currently accepts only native 44.1 kHz MP3.
    static bool logged = false;
    if (!logged) { Serial.println("VinylM5: MP3 sample rate must be 44100 Hz"); logged = true; }
  }
  static uint32_t lastReport = 0;
  if (millis() - lastReport >= 2000) {
    lastReport = millis();
    const uint32_t fifoCount = gFifoWrite.load(std::memory_order_acquire) - gFifoRead.load(std::memory_order_acquire);
    Serial.printf("VinylM5 R11 mode=%s pcm=%lu fifo=%lu/16384 touchMove=%lu touchDrop=%lu touchQueueMaxUs=%lu touchGapMaxUs=%lu speakerQueueFail=%lu decodeFinished=%d mp3RateError=%d\n",
                  gDemoMode.load() ? "DEMO" : "MP3", static_cast<unsigned long>(gDecodedPcmFrames.load()),
                  static_cast<unsigned long>(fifoCount), static_cast<unsigned long>(gTouchMoveCount.exchange(0)),
                  static_cast<unsigned long>(gTouchDropCount.load()), static_cast<unsigned long>(gMaxTouchQueueAgeUs.exchange(0)),
                  static_cast<unsigned long>(gTouchGapMaxUs.exchange(0)),
                  static_cast<unsigned long>(gSpeakerQueueFailures.load()), static_cast<int>(gDecoderFinished.load()),
                  static_cast<int>(gRateUnsupported.load()));
    if (gAudioError.load()) Serial.println("VinylM5 ERROR: MP3 decoder failed; check /track.mp3 and 44100Hz input.");
  }
  delay(1);
}
