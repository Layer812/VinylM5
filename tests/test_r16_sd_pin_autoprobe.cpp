#include "sd_spi_autodetect.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
#include <utility>

struct MockMount {
  const sdautodetect::Pins* actual;
  uint32_t maxSpeed;
  std::vector<std::pair<char,uint32_t>> trials;
  explicit MockMount(const sdautodetect::Pins* a, uint32_t maxHz = 4000000u)
      : actual(a), maxSpeed(maxHz) {}
  bool tryMount(const sdautodetect::Pins& p, uint32_t hz) {
    trials.emplace_back(p.label[0], hz);
    return actual && actual->sck==p.sck && actual->mosi==p.mosi &&
           actual->miso==p.miso && actual->cs==p.cs && hz<=maxSpeed;
  }
};
int main() {
  const auto* profiles=sdautodetect::kProfiles;
  for (int i=0;i<2;++i) {
    MockMount m(&profiles[i]);
    uint32_t speed=0;
    auto* found=sdautodetect::mount(m,speed);
    assert(found==&profiles[i]);
    assert(speed==4000000u);
    assert(m.trials.size()==static_cast<size_t>(3*i+1));
  }
  { // slow card must receive speed fallbacks
    MockMount m(&profiles[1], 1000000u);
    uint32_t speed=0;
    auto* found=sdautodetect::mount(m,speed);
    assert(found==&profiles[1] && speed==1000000u);
    assert(m.trials.size()==6);
  }
  { // absent card never produces a false positive
    MockMount m(nullptr);
    uint32_t speed=123u;
    assert(sdautodetect::mount(m,speed)==nullptr);
    assert(speed==0 && m.trials.size()==6);
  }
  puts("PASS: C/A SD mount fallback, slower card, absent card");
}