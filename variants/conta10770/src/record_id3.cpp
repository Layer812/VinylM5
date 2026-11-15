// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Layer812

#include "record_id3.hpp"
#include <AudioFileSource.h>
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <cstring>
#include <algorithm>

namespace recordid3 {
namespace {
constexpr uint32_t kMaxCoverBytes = 1536u * 1024u; // 1.5MiB encoded art in PSRAM.
uint32_t be32(const uint8_t* p) {return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|p[3];}
uint32_t syncsafe(const uint8_t* p) {return (uint32_t(p[0]&0x7f)<<21)|(uint32_t(p[1]&0x7f)<<14)|((p[2]&0x7f)<<7)|(p[3]&0x7f);}
void appendCp(char* out,size_t cap,size_t& used,uint32_t cp) {
  if(!cp || cp>0x10ffff || (cp>=0xd800&&cp<=0xdfff) || used+4>=cap)return;
  if(cp<0x80)out[used++]=char(cp);
  else if(cp<0x800){out[used++]=char(0xc0|(cp>>6));out[used++]=char(0x80|(cp&63));}
  else if(cp<0x10000){out[used++]=char(0xe0|(cp>>12));out[used++]=char(0x80|((cp>>6)&63));out[used++]=char(0x80|(cp&63));}
  else{out[used++]=char(0xf0|(cp>>18));out[used++]=char(0x80|((cp>>12)&63));out[used++]=char(0x80|((cp>>6)&63));out[used++]=char(0x80|(cp&63));}
}
size_t removeUnsync(uint8_t* b,size_t n){
  size_t w=0;
  for(size_t i=0;i<n;++i){
    b[w++]=b[i];
    if(b[i]==0xff && i+1<n && b[i+1]==0)++i;
  }
  return w;
}
void textFrame(const uint8_t* b,size_t len,char dst[128]) {
  if(!b || len<2)return;
  const uint8_t enc=b[0];size_t used=0;
  if(enc==3){ // UTF-8, preserve full codepoints up to buffer limit
    for(size_t i=1;i<len && b[i] && used+1<128;++i)dst[used++]=char(b[i]);
  }else if(enc==0){ // ISO-8859-1 -> UTF-8
    for(size_t i=1;i<len && b[i] && used+3<128;++i)appendCp(dst,128,used,b[i]);
  }else if(enc==1 || enc==2){
    size_t i=1;bool big=enc==2;
    if(enc==1 && len>=3){
      if(b[1]==0xfe&&b[2]==0xff){big=true;i=3;}
      else if(b[1]==0xff&&b[2]==0xfe){big=false;i=3;}
    }
    for(;i+1<len && used+4<128;){
      uint32_t cp=big?(uint32_t(b[i])<<8)|b[i+1]:b[i]|(uint32_t(b[i+1])<<8);i+=2;
      if(!cp)break;
      if(cp>=0xd800 && cp<=0xdbff && i+1<len){
        const uint32_t lo=big?(uint32_t(b[i])<<8)|b[i+1]:b[i]|(uint32_t(b[i+1])<<8);
        if(lo>=0xdc00 && lo<=0xdfff){cp=0x10000+((cp-0xd800)<<10)+(lo-0xdc00);i+=2;}
        else cp='?';
      }else if(cp>=0xdc00&&cp<=0xdfff)cp='?';
      appendCp(dst,128,used,cp);
    }
  }
  dst[used]=0;
}
bool dimensionsJpg(const uint8_t* b,uint32_t n,uint16_t& w,uint16_t& h){
  if(n<4||b[0]!=0xff||b[1]!=0xd8)return false;
  for(uint32_t p=2;p+9<n;){
    if(b[p]!=0xff){++p;continue;}
    uint8_t marker=b[p+1];
    if(marker==0xd9||marker==0xda)break;
    if(marker==0xff){++p;continue;}
    if(marker==0x00||marker==0x01||(marker>=0xd0&&marker<=0xd7)){p+=2;continue;}
    if(p+4>n)break;
    uint32_t len=(uint32_t(b[p+2])<<8)|b[p+3];
    if(len<2||p+len+2>n)break;
    if(((marker>=0xc0&&marker<=0xc3)||(marker>=0xc5&&marker<=0xc7)||
        (marker>=0xc9&&marker<=0xcb)||(marker>=0xcd&&marker<=0xcf)) && len>=7){
      h=uint16_t(b[p+5]<<8|b[p+6]);w=uint16_t(b[p+7]<<8|b[p+8]);
      return w&&h;
    }
    p+=len+2;
  }
  return false;
}
// Find the real ID3 APIC/PIC binary payload rather than looking for a JPEG
// magic signature at an arbitrary offset in the tag.
bool extractImage(uint8_t* blob,uint32_t& n,uint8_t version,Info& info){
  const uint8_t enc=blob[0];
  uint32_t i=1;
  if(version==2){if(n<7)return false;i+=3;} // PIC: image format "JPG"/"PNG"
  else{
    while(i<n&&blob[i]!=0)++i; // APIC MIME, NUL terminated
    if(i>=n)return false;
    ++i;
  }
  if(i>=n)return false;
  ++i; // picture type
  if(enc==0 || enc==3){while(i<n&&blob[i])++i;if(i>=n)return false;++i;}
  else if(enc==1 || enc==2){
    if(i+1<n && ((blob[i]==0xff&&blob[i+1]==0xfe)||(blob[i]==0xfe&&blob[i+1]==0xff)))i+=2;
    while(i+1<n && (blob[i]!=0||blob[i+1]!=0))i+=2;
    if(i+1>=n)return false;
    i+=2;
  }else return false;
  // Some taggers pad APIC's empty description with extra NUL bytes.
  // Ignore only a short zero pad; never scan the whole image for a magic signature.
  const uint32_t afterDescription=i;
  while(i<n && blob[i]==0 && i-afterDescription<64) ++i;
  if(i>=n)return false;
  const uint8_t* data=blob+i;
  const uint32_t size=n-i;
  const bool jpeg=size>=3&&data[0]==0xff&&data[1]==0xd8&&data[2]==0xff;
  const bool png=size>=24&&data[0]==0x89&&memcmp(data+1,"PNG\r\n\x1a\n",7)==0;
  if(!jpeg&&!png)return false;
  uint16_t w=0,h=0;
  if(jpeg){if(!dimensionsJpg(data,size,w,h))return false;}
  else{
    const uint32_t iw=be32(data+16),ih=be32(data+20);
    if(iw>UINT16_MAX||ih>UINT16_MAX)return false;
    w=uint16_t(iw);h=uint16_t(ih);
  }
  if(!w||!h||w>8192||h>8192)return false;
  info.coverBlob=blob;info.coverSize=n;info.coverOffset=i;
  info.width=w;info.height=h;
  info.format=jpeg?ImageFormat::Jpeg:ImageFormat::Png;
  return true;
}
}

void read(AudioFileSource& src,Info& info){
  if(!src.isOpen())return;
  struct ResetFile {AudioFileSource& s;~ResetFile(){s.seek(0,SEEK_SET);}} reset{src};
  if(!src.seek(0,SEEK_SET))return;
  uint8_t tag[10];
  if(src.read(tag,sizeof(tag))!=sizeof(tag)||memcmp(tag,"ID3",3)!=0)return;
  const uint8_t version=tag[3];
  if(version<2||version>4)return;
  info.id3Version=version;
  const uint32_t tagSize=syncsafe(tag+6);
  if(tagSize>16u*1024u*1024u||uint64_t(tagSize)+10>src.getSize())return;
  const bool tagUnsync=(tag[5]&0x80)!=0;
  uint32_t at=10;
  const uint32_t limit=10+tagSize;
  if(version>=3 && (tag[5]&0x40)){
    uint8_t hdr[4];
    if(src.read(hdr,4)!=4)return;
    const uint32_t ext=version==3?be32(hdr):syncsafe(hdr);
    const uint32_t skip=version==3?4+ext:ext;
    if(skip<4||uint64_t(at)+skip>limit)return;
    at+=skip;
  }
  for(unsigned int frames=0;frames<4096;++frames){
    const uint32_t hlen=version==2?6:10;
    if(at+hlen>limit||!src.seek(at,SEEK_SET))break;
    uint8_t frame[10]={};
    if(src.read(frame,hlen)!=hlen)break;
    if(frame[0]==0||frame[0]==0xff)break;
    const uint32_t len=version==2?(uint32_t(frame[3])<<16)|(uint32_t(frame[4])<<8)|frame[5]:
      version==4?syncsafe(frame+4):be32(frame+4);
    const uint32_t dataAt=at+hlen;
    if(!len||uint64_t(dataAt)+len>limit)break;
    const bool isTitle=version==2?memcmp(frame,"TT2",3)==0:memcmp(frame,"TIT2",4)==0;
    const bool isArtist=version==2?memcmp(frame,"TP1",3)==0:memcmp(frame,"TPE1",4)==0;
    const bool isPicture=version==2?memcmp(frame,"PIC",3)==0:memcmp(frame,"APIC",4)==0;
    const bool unsupportedFlags=version==3?(frame[9]&0xc0)!=0:
      version==4?(frame[9]&0x0c)!=0:false;
    const bool unsync=tagUnsync || (version==4 && (frame[9]&0x02));
    uint32_t prefix=0;
    if(version==3 && (frame[9]&0x20))prefix++;
    if(version==4){if(frame[9]&0x40)prefix++;if(frame[9]&0x01)prefix+=4;}
    if(!unsupportedFlags && len>prefix && ((isTitle&&!info.title[0])||(isArtist&&!info.artist[0])||(isPicture&&!info.image()))){
      const uint32_t payloadLen=len-prefix;
      if((isTitle||isArtist) && payloadLen <= 512){
        uint8_t buf[512];
        if(src.seek(dataAt+prefix,SEEK_SET) && src.read(buf,payloadLen)==payloadLen){
          const uint32_t actual=unsync?uint32_t(removeUnsync(buf,payloadLen)):payloadLen;
          textFrame(buf,actual,isTitle?info.title:info.artist);
        }
      }else if(isPicture){
        info.pictureBytes=payloadLen;
        if(payloadLen<=kMaxCoverBytes && payloadLen>12){
          uint8_t* blob=static_cast<uint8_t*>(heap_caps_malloc(payloadLen,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
          if(blob){
            if(src.seek(dataAt+prefix,SEEK_SET) && src.read(blob,payloadLen)==payloadLen){
              uint32_t actual=unsync?uint32_t(removeUnsync(blob,payloadLen)):payloadLen;
              if(extractImage(blob,actual,version,info)){
                info.coverBytes=info.imageBytes();
              }else heap_caps_free(blob);
            }else heap_caps_free(blob);
          }
        }
      }
    }
    at=dataAt+len;
  }
}
}
