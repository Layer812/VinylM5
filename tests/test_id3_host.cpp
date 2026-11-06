// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Layer812

#include <vector>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <algorithm>
#include "record_id3.hpp"
#include <AudioFileSource.h>
class Mock final:public AudioFileSource{
public:
 std::vector<uint8_t> a;uint32_t p=0;
 bool isOpen()override{return true;}
 uint32_t getSize()override{return a.size();}
 uint32_t read(void* out,uint32_t n)override{n=std::min(n,(uint32_t)a.size()-p);memcpy(out,a.data()+p,n);p+=n;return n;}
 bool seek(int32_t off,int whence)override{int64_t q=off+(whence==SEEK_CUR?p:whence==SEEK_END?a.size():0);if(q<0||q>(int64_t)a.size())return false;p=q;return true;}
};
void makeFrame(std::vector<uint8_t>&b,const char* name,const std::vector<uint8_t>& bytes){
 b.insert(b.end(),name,name+4);
 uint32_t n=bytes.size();b.push_back(n>>24);b.push_back(n>>16);b.push_back(n>>8);b.push_back(n);
 b.push_back(0);b.push_back(0);b.insert(b.end(),bytes.begin(),bytes.end());
}
int main(){
 Mock f;f.a={'I','D','3',3,0,0,0,0,0,0};
 makeFrame(f.a,"TIT2",{3,'H','e','l','l','o'});
 makeFrame(f.a,"TPE1",{1,0xff,0xfe,0xc6,0x30,0xb9,0x30}); // 'テス' UTF16LE
 const std::vector<uint8_t> jpeg={0xff,0xd8,0xff,0xe0,0,4,0,0,0xff,0xc0,0,17,8,0,64,0,64,3,1,0x11,0,2,0x11,0,3,0x11,0,0xff,0xd9};
 std::vector<uint8_t> pic={0,'i','m','a','g','e','/','j','p','e','g',0,3,0,0,0,0,0,0,0};
 pic.insert(pic.end(),jpeg.begin(),jpeg.end());
 makeFrame(f.a,"APIC",pic);
 const uint32_t size=f.a.size()-10;
 f.a[6]=(size>>21)&127;f.a[7]=(size>>14)&127;f.a[8]=(size>>7)&127;f.a[9]=size&127;
 recordid3::Info info;recordid3::read(f,info);
 assert(!strcmp(info.title,"Hello"));assert(info.artist[0]);
 assert(info.format==recordid3::ImageFormat::Jpeg && info.width==64 && info.height==64);
 assert(info.imageBytes()>10 && f.p==0);
 free(info.coverBlob);
 // v2.4 has synchsafe frame sizes; all test payloads are below 128 bytes.
 f.p=0;f.a[3]=4;
 recordid3::Info info4;recordid3::read(f,info4);
 assert(!strcmp(info4.title,"Hello"));
 assert(info4.image() && info4.width==64 && info4.height==64 && f.p==0);
 free(info4.coverBlob);
 // ID3v2.2 PIC support: three-character frame ID and 24-bit size.
 auto addV22=[](std::vector<uint8_t>& b,const char* id,const std::vector<uint8_t>& value){
   b.insert(b.end(),id,id+3);
   uint32_t n=value.size();b.push_back(n>>16);b.push_back(n>>8);b.push_back(n);
   b.insert(b.end(),value.begin(),value.end());
 };
 f.a={'I','D','3',2,0,0,0,0,0,0};f.p=0;
 addV22(f.a,"TT2",{3,'R','7'});
 std::vector<uint8_t> png={0x89,'P','N','G',13,10,26,10,0,0,0,13,'I','H','D','R',0,0,0,120,0,0,0,100};
 std::vector<uint8_t> apic={0,'P','N','G',3,0};apic.insert(apic.end(),png.begin(),png.end());
 addV22(f.a,"PIC",apic);
 uint32_t sz=f.a.size()-10;
 f.a[6]=(sz>>21)&127;f.a[7]=(sz>>14)&127;f.a[8]=(sz>>7)&127;f.a[9]=sz&127;
 recordid3::Info info22;recordid3::read(f,info22);
 assert(!strcmp(info22.title,"R7"));
 assert(info22.id3Version==2 && info22.format==recordid3::ImageFormat::Png);
 assert(info22.width==120 && info22.height==100 && info22.imageBytes()==png.size());
 free(info22.coverBlob);
 // v2.3 extended header: size field excludes its own four bytes.
 f.p=0;f.a={'I','D','3',3,0,0x40,0,0,0,0};
 f.a.insert(f.a.end(),{0,0,0,6,0,0,0,0,0,0}); // 4+6 bytes total.
 makeFrame(f.a,"TIT2",{3,'E','X','T'});
 sz=f.a.size()-10;
 f.a[6]=(sz>>21)&127;f.a[7]=(sz>>14)&127;f.a[8]=(sz>>7)&127;f.a[9]=sz&127;
 recordid3::Info infoExt;recordid3::read(f,infoExt);
 assert(!strcmp(infoExt.title,"EXT") && !infoExt.image());
 puts("PASS: R7 ID3v2.2 PNG PIC, extended-header v2.3, plus v2.3/v2.4 UTF-16 and JPEG APIC");
 puts("PASS: ID3 v2.3/v2.4 title, UTF-16 artist, JPEG APIC 64x64, rewind");
}
