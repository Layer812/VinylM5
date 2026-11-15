#include "sd_spi_autodetect.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
#include <utility>
struct MockMount {
  const sdautodetect::Pins* actual;
  uint32_t maxSpeed;
  std::vector<std::pair<char,uint32_t>> trials;
  explicit MockMount(const sdautodetect::Pins* a,uint32_t hz=4000000u):actual(a),maxSpeed(hz){}
  bool tryMount(const sdautodetect::Pins& p,uint32_t hz) {
    trials.emplace_back(p.label[0],hz);
    return actual && actual->cs==p.cs && actual->mosi==p.mosi &&
      actual->sck==p.sck && actual->miso==p.miso && hz<=maxSpeed;
  }
};
int main(){
  using namespace sdautodetect;
  static_assert(kProfiles[0].sck==0 && kProfiles[0].miso==9,"C mapping");
  static_assert(kProfiles[1].miso==0 && kProfiles[2].miso==0,"B/A MISO");
  assert(decodeMode(255)==Mode::OfficialC);
  assert(selectMode(Mode::OfficialC,true,false)==Mode::LegacyAB);
  assert(selectMode(Mode::LegacyAB,false,false)==Mode::LegacyAB);
  assert(selectMode(Mode::LegacyAB,false,true)==Mode::OfficialC);
  assert(selectMode(Mode::LegacyAB,true,true)==Mode::OfficialC);
  for(int i=0;i<3;++i){
    const Mode mode=i==0?Mode::OfficialC:Mode::LegacyAB;
    MockMount m(&kProfiles[i]);
    uint32_t speed=0;
    const auto* found=mount(m,speed,mode);
    assert(found==&kProfiles[i] && speed==4000000u);
    assert(m.trials.size()==static_cast<size_t>(i==2?4:1));
    for(auto t:m.trials) assert(mode==Mode::OfficialC?t.first=='C':t.first!='C');
  }
  { MockMount wrong(&kProfiles[2]);uint32_t hz=123;
    assert(mount(wrong,hz,Mode::OfficialC)==nullptr && hz==0);
    assert(wrong.trials.size()==3);
    for(auto t:wrong.trials) assert(t.first=='C');
  }
  { MockMount slow(&kProfiles[2],1000000u);uint32_t hz=0;
    assert(mount(slow,hz,Mode::LegacyAB)==&kProfiles[2]);
    assert(hz==1000000u && slow.trials.size()==6);
  }
  { MockMount absent(nullptr);uint32_t hz=123;
    assert(mount(absent,hz,Mode::LegacyAB)==nullptr && hz==0);
    assert(absent.trials.size()==6);
    for(auto t:absent.trials) assert(t.first!='C');
  }
  puts("PASS: official C only, persistent B/A only, boot selection, zero cross-group probing");
}
