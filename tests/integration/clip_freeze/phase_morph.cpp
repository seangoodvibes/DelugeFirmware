// SPDX-License-Identifier: GPL-3.0-or-later
// Offline experiment: uses the benchmark's actual Fade::apply, with the captured
// audio callback boundaries. No approximation of the fade curve/gain algorithm.
#include "model/clip/clip_freeze.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <cmath>

template<class T> std::vector<T> read(const std::string& path) {
	std::ifstream in(path, std::ios::binary | std::ios::ate);
	assert(in);
	auto size = in.tellg();
	assert(size % sizeof(T) == 0);
	std::vector<T> result(size / sizeof(T));
	in.seekg(0);
	in.read(reinterpret_cast<char*>(result.data()), size);
	return result;
}

struct MatchResult { int first = -1, longest = 0; };
MatchResult scan(const std::vector<StereoSample>& live, const std::vector<StereoSample>& cache,
                 const std::vector<int32_t>& ends, int powerTime, int bandTime, int warmup, bool guard = false) {
 float lows[2][2][3]{}, bandPower[2][4]{}, power[2]{}, quickPower[2]{}, quickBands[2][4]{};
 constexpr float coefficients[3]{.028093f,.107735f,.366131f};
 int start=0, held=0;
 MatchResult result;
 for(int end:ends) {
  if(end>30*44100) break;
  int n=end-start;
  float energy[2]{}, bands[2][4]{};
  for(int i=start;i<end;++i) {
   for(int stream=0;stream<2;++stream) {
    const auto& frame=stream ? cache[i] : live[i];
    energy[stream]+=float(frame.l)*frame.l+float(frame.r)*frame.r;
    for(int channel=0;channel<2;++channel) {
     float value=channel ? frame.r : frame.l;
     auto& lp=lows[stream][channel];
     for(int b=0;b<3;++b) lp[b]+=coefficients[b]*(value-lp[b]);
     float split[4]{lp[0],lp[1]-lp[0],lp[2]-lp[1],value-lp[2]};
     for(int b=0;b<4;++b) bands[stream][b]+=split[b]*split[b];
    }
   }
  }
  float total[2]{}, quickTotal[2]{};
  for(int stream=0;stream<2;++stream) {
   power[stream]+=float(n)/(n+powerTime)*(energy[stream]/n-power[stream]);
   quickPower[stream]+=float(n)/(n+882)*(energy[stream]/n-quickPower[stream]);
   for(int b=0;b<4;++b) {
    bandPower[stream][b]+=float(n)/(n+bandTime)*(bands[stream][b]/n-bandPower[stream][b]);
    total[stream]+=bandPower[stream][b];
    quickBands[stream][b]+=float(n)/(n+2205)*(bands[stream][b]/n-quickBands[stream][b]);
    quickTotal[stream]+=quickBands[stream][b];
   }
  }
  bool matches=power[0]>=power[1]*.81f && power[0]<=power[1]*1.235f;
  for(int b=0;b<4 && matches;++b) {
   float target=bandPower[1][b]/std::max(1.f,total[1]);
   float actual=bandPower[0][b]/std::max(1.f,total[0]);
   if(bandPower[1][b]>total[1]*.0001f) matches=actual>=target*.63f && actual<=target*1.585f;
  }
  bool quick=quickPower[0]>=quickPower[1]*.81f && quickPower[0]<=quickPower[1]*1.235f;
  for(int b=0;b<4 && quick;++b) {
   float target=quickBands[1][b]/std::max(1.f,quickTotal[1]);
   float actual=quickBands[0][b]/std::max(1.f,quickTotal[0]);
   if(quickBands[1][b]>quickTotal[1]*.0001f) quick=actual>=target*.63f && actual<=target*1.585f;
  }
  held=matches ? held+n : 0;
  result.longest=std::max(result.longest,held);
  if(result.first<0 && end>=warmup && held>=11025 && (!guard || quick)) result.first=end;
  start=end;
 }
 return result;
}
int main(int argc, char** argv) {
	assert(argc == 3 || argc == 4);
	std::string root = argv[1], out = argv[2];
	auto live = read<StereoSample>(root + "/probe-live.s32le");
	auto cache = read<StereoSample>(root + "/probe-cache.s32le");
	auto sizes = read<uint8_t>(root + "/probe-blocks.bin");
	std::vector<int32_t> ends;
	int endSample = 0;
	for (auto n : sizes) {
		endSample += n;
		ends.push_back(endSample);
	}
	assert(live.size() == cache.size() && ends.back() == live.size());
 if(argc==4 && std::string(argv[3])=="--scan") {
  std::cout<<"average_ms,first_match_sample,longest_match_samples\n";
  auto baseline=scan(live,cache,ends,882,2205,11025);
  std::cout<<"baseline,"<<baseline.first<<','<<baseline.longest<<'\n';
  for(int ms:{250,500,1000}) {
   auto r=scan(live,cache,ends,ms*44100/1000,ms*44100/1000,ms*44100/500);
   std::cout<<ms<<','<<r.first<<','<<r.longest<<'\n';
   r=scan(live,cache,ends,ms*44100/1000,ms*44100/1000,ms*44100/500,true);
   std::cout<<ms<<"+instant,"<<r.first<<','<<r.longest<<'\n';
  }
  return 0;
 }

	std::cout << "wait_seconds,fade_seconds,event_sample,excerpt_start,frames,file\n";
	std::vector<double> waits{.5, 1., 2., 4., 8., 16., 24., 30., 31., 32.};
	if (argc == 4) waits = {std::stod(argv[3])};
	for (double wait : waits) {
		for (double seconds : {.05, .25, 1., 3.}) {
			int fadeLength = std::lround(seconds * 44100);
			if ((wait + seconds) * 44100 > live.size() + 128)
				continue;
			auto audio = live;
			std::vector<int32_t> send(128), oldSend(128);
			ClipFreeze::Fade fade;
			int start = 0, event = -1;
			for (int end : ends) {
				int n = end - start;
				assert(n >= 0 && n <= 128);
				if (event < 0 && end >= std::lround(wait * 44100)) {
					event = start;
					fade.reset(fadeLength);
				}
				if (event < 0)
					std::copy_n(cache.data() + start, n, audio.data() + start);
				else if (fade.remaining)
					fade.apply({audio.data() + start, size_t(n)}, {send.data(), size_t(n)},
					           {cache.data() + start, size_t(n)}, {oldSend.data(), size_t(n)});
				start = end;
			}
			int first = std::max(0, event - 88200);
			int last = std::min<int>(audio.size(), event + fadeLength + 88200);
			std::string filename = "wait-" + std::to_string(int(wait * 1000)) + "-fade-"
			                       + std::to_string(int(seconds * 1000)) + ".s32le";
			std::ofstream output(out + "/" + filename, std::ios::binary);
			output.write(reinterpret_cast<char*>(audio.data() + first), (last - first) * sizeof(StereoSample));
			std::cout << wait << ',' << seconds << ',' << event << ',' << first << ',' << last - first << ',' << filename << '\n';
		}
	}
}
