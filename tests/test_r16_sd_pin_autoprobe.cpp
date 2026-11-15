#include "sd_spi_autodetect.hpp"
#include <cassert>
#include <cstdint>
#include <deque>
#include <vector>
#include <cstdio>

struct MockSpi {
  const sdautodetect::Pins* actual;
  int type; // 0 = SDHC, 1 = SDSC v1, 2 = wrong echo
  bool selected=false;
  bool connected=false;
  bool prepared=false;
  std::vector<uint8_t> command;
  std::deque<uint8_t> response;
  int attempts=0;
  int sends=0;
  MockSpi(const sdautodetect::Pins* p,int t=0):actual(p),type(t){}
  bool prepare(const sdautodetect::Pins& p){
    prepared=true; selected=false; command.clear();response.clear();++attempts;
    connected=actual && p.cs==actual->cs && p.mosi==actual->mosi
       && p.sck==actual->sck && p.miso==actual->miso;
    return true;
  }
  void select(bool active){
    selected=active; command.clear();response.clear();
  }
  uint8_t transfer(uint8_t sent){
    assert(prepared);++sends;
    if(!connected || !selected) return 0xff;
    if(!response.empty()){
      uint8_t v=response.front();response.pop_front();return v;
    }
    if(command.empty() && sent!=0x40 && sent!=0x48)return 0xff;
    command.push_back(sent);
    if(command.size()==6){
      if(command[0]==0x40 && command[5]==0x95)response.push_back(0x01);
      if(command[0]==0x48 && command[5]==0x87){
        if(type==1)response.push_back(0x05);
        else {
          response.push_back(0x01);
          response.push_back(0x00);response.push_back(0x00);
          response.push_back(type==2?0x02:0x01);response.push_back(0xaa);
        }
      }
      command.clear();
    }
    return 0xff;
  }
  void finish(){ selected=false; prepared=false; }
};

int main(){
  for(const auto& p:sdautodetect::kProfiles){
    MockSpi io(&p);
    auto* got=sdautodetect::detect(io);
    assert(got && got->cs==p.cs && got->mosi==p.mosi && got->sck==p.sck && got->miso==p.miso);
    assert(io.sends>0 && !io.prepared);
    MockSpi old(&p,1);
    got=sdautodetect::detect(old); assert(got && got->sck==p.sck);
    MockSpi broken(&p,2);
    assert(sdautodetect::detect(broken)==nullptr);
  }
  MockSpi absent(nullptr);
  assert(sdautodetect::detect(absent)==nullptr);
  assert(absent.attempts==3);
  puts("PASS: CMD0/CMD8 autodetection for A, B, Conta; legacy and missing card");
}
