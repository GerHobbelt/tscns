/*
MIT License

Copyright (c) 2019 Meng Rao <raomeng1@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <immintrin.h>
#include "tscns.h"

#include "monolithic_examples.h"

using namespace std;

static tscns::TSCNS tn;

#if defined(BUILD_MONOLITHIC)
#define main  tscns_alt_test_main
#endif

extern "C"
int main(int argc, const char** argv) {
  double tsc_ghz;
  if (argc > 1) {
    tsc_ghz = stod(argv[1]);
    tn.init(tsc_ghz);
  }
  else {
    tn.init();
    // it'll be more precise if you wait a while and calibrate
    std::this_thread::sleep_for(std::chrono::seconds(1));
    tn.calibrate();
	tsc_ghz = tn.getTscGhz();
  }

  cout << std::setprecision(17) << "tsc_ghz: " << tsc_ghz << endl;

  // 1) Try binding to different cores with the same tsc_ghz at nearly the same time, see if the offsets are similar(not
  // necessary the same). If not, you're doomed: tsc of your machine's different cores are not synced up, don't share
  // TSCNS among threads then.
  //
  // 2) Try running test with the same tsc_ghz at different times, see if the offsets are similar(not necessary the
  // same). If you find them steadily go up/down at a fast speed, then your tsc_ghz is not precise enough, try
  // calibrating with a longer waiting time and test again.

  int64_t rdns_latency;
  {
    const int N = 1000;
    int64_t tmp = 0;
    _mm_lfence();
    int64_t before = tn.rdns();
    _mm_lfence();
    for (int i = 0; i < N - 1; i++) {
      _mm_lfence();
      tmp += tn.rdns();
      _mm_lfence();
    }
    _mm_lfence();
    int64_t after = tn.rdns();
    _mm_lfence();
    rdns_latency = (after - before) / N;
    cout << "rdns_latency: " << rdns_latency << " tmp: " << tmp << endl;
  }

  {
    int64_t rdsysns_latency;
    const int N = 1000;
    int64_t tmp = 0;
    _mm_lfence();
    int64_t before = tn.rdsysns();
    _mm_lfence();
    for (int i = 0; i < N - 1; i++) {
      _mm_lfence();
      tmp += tn.rdsysns();
      _mm_lfence();
    }
    _mm_lfence();
    int64_t after = tn.rdsysns();
    _mm_lfence();
    rdsysns_latency = (after - before) / N;
    cout << "rdsysns_latency: " << rdsysns_latency << " tmp: " << tmp << endl;
  }

  cout << "a:\ttimestamp from rdns()" << endl
       << "b:\ttimestamp from rdsysns()" << endl
       << "c:\ttimestamp from rdns()" << endl
       << "good:\twhether b-a and c-b both non-negative" << endl
       << "rdns_latency:\ttime rdns() takes in ns" << endl
       << "rdsysns_latency:\tc-a-rdns_latency" << endl;
  while (true) {
    _mm_lfence();
    int64_t a = tn.rdns();
    _mm_lfence();
    int64_t b = tn.rdsysns();
    _mm_lfence();
    int64_t c = tn.rdns();
    _mm_lfence();
    int64_t a2b = b - a;
    int64_t b2c = c - b;
    bool good = a2b >= 0 && b2c >= 0;
    int64_t rdsysns_latency = c - a - rdns_latency;
    cout << "a: " << a << ", b: " << b << ", c: " << c << ", a2b: " << a2b << ", b2c: " << b2c << ", good: " << good
         << ", rdsysns_latency: " << rdsysns_latency << endl;
    // std::this_thread::sleep_for(std::chrono::miliseconds(1000));
    auto expire = tn.rdns() + 1000000000;
    while (tn.rdns() < expire)
      ;
  }

  return 0;
}
