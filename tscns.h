/*
MIT License

Copyright (c) 2022 Meng Rao <raomeng1@gmail.com>

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

#pragma once
#include <chrono>
#include <atomic>
#include <thread>

#ifdef _MSC_VER
#include <intrin.h>
#endif

/**
 * @brief A thread safe clock library to get the current timestamp at nanosecond precision and nanosecond latency.
 * It uses tsc register to get the timestamp counter. However, the value of tsc has to do with CPU frequency,
 * and CPU frequency may change during the time (and can change drastically with turbo boost)
 * Therefore we must calibrate the tsc -> ns ratio occasionally.
 * 
 * It uses seqlock to ensure thread safety and it's SPMC.
 * (Producer : the thread calibrating the clock; Consumer : the thread reading the clock)
 * If we don't seperate calibrating and reading in different threads, we can further simplify this class.
 */
template <int32_t kCachelineSize = 64>
class TSCNS
{
public:
    void init(int64_t init_calibrate_ns = 20'000'000, int64_t calibrate_interval_ns = 3 * NsPerSec);
    void calibrate();
    static int64_t rdtsc();
    int64_t tsc2ns(int64_t tsc) const;
    int64_t rdns() const;
    static int64_t rdsysns();
    double getTscGhz() const;
    static void syncTime(int64_t & tsc_out, int64_t & ns_out);
    void saveParam(int64_t base_tsc, int64_t sys_ns, int64_t base_ns_err, double new_ns_per_tsc);

    static constexpr int64_t NsPerSec = 1'000'000'000;
    alignas(kCachelineSize) std::atomic<uint32_t> param_seq_ {0};
    // atomic sequence number implementing seqlock to ensure thread safety.
    // align the cacheline to avoid false sharing
    double ns_per_tsc_;
    int64_t base_tsc_;
    int64_t base_ns_;
    int64_t calibrate_interval_ns_;
    int64_t base_ns_err_;
    int64_t next_calibrate_tsc_;
    // These data members need not to be declared as atomic variables.  
    // explicit memory fence will protect them
private:
    std::array<uint8_t, kCachelineSize - (sizeof(param_seq_) + sizeof(ns_per_tsc_) + sizeof(base_tsc_) +
        sizeof(base_ns_) + sizeof(calibrate_interval_ns_) + sizeof(base_ns_err_) + sizeof(next_calibrate_tsc_)) % kCachelineSize> padding_;
    // add padding here to prevent false sharing, i.e. we don't want another shared varaible 
    // to be stored in the same cacheline with these data memebers
};

template <int32_t kCachelineSize>
void TSCNS<kCachelineSize>::init(int64_t init_calibrate_ns, int64_t calibrate_interval_ns)
{
    calibrate_interval_ns_ = calibrate_interval_ns;
    int64_t base_tsc, base_ns;
    syncTime(base_tsc, base_ns);
    // Get the baseline timestamp counter and system ns counter
    int64_t expire_ns = base_ns + init_calibrate_ns;
    while (rdsysns() < expire_ns) 
    {
        // wait for an interval
        std::this_thread::yield();
    }
    int64_t delayed_tsc, delayed_ns;
    syncTime(delayed_tsc, delayed_ns);
    // Get the timestamp counter and system ns counter after an interval
    double init_ns_per_tsc = static_cast<double>(delayed_ns - base_ns) / (delayed_tsc - base_tsc);
    // Compute the "ns_per_tsc" linearly
    saveParam(base_tsc, base_ns, 0, init_ns_per_tsc);
    // save it to the class (error == 0)
}

template <int32_t kCachelineSize>
void TSCNS<kCachelineSize>::calibrate()
{
    if(rdtsc() < next_calibrate_tsc_)
    {
        // no need to calibrate
        return;
    }
    int64_t tsc, ns;
    syncTime(tsc, ns);
    int64_t ns_err = tsc2ns(tsc) - ns;
    if(ns_err > 1'000'000)
    {
        ns_err = 1'000'000;
    }
    if(ns_err < -1'000'000)
    {
        ns_err = -1'000'000;
    }
    // avoid exception
    double new_ns_per_tsc_ = ns_per_tsc_ * (1.0 - (ns_err + ns_err - base_ns_err_) / ((tsc - base_tsc_) * ns_per_tsc_));
    // new_ns_per_tsc_ = ns_per_tsc_ - (ns_err + ns_err - base_ns_err_) / (tsc - base_tsc_)
    saveParam(tsc, ns, ns_err, new_ns_per_tsc_);
}

template <int32_t kCachelineSize>
int64_t __attribute__((always_inline)) TSCNS<kCachelineSize>::rdtsc()
{
    return __builtin_ia32_rdtsc();
}

template <int32_t kCachelineSize>
int64_t __attribute__((always_inline)) TSCNS<kCachelineSize>::tsc2ns(int64_t tsc) const
{
    int64_t ns;
    uint32_t before_seq, after_seq;
    do
    {
        before_seq = param_seq_.load(std::memory_order_acquire) & ~1;
        std::atomic_signal_fence(std::memory_order_acq_rel);
        int64_t ns = base_ns_ + static_cast<int64_t>((tsc - base_tsc_) * ns_per_tsc_);
        std::atomic_signal_fence(std::memory_order_acq_rel);
        after_seq = param_seq_.load(std::memory_order_acquire);
    } while(before_seq != after_seq);
    return ns;
}
/* The code above is the same as 
do 
{
    uint32_t before_seq = param_seq_.load(std::memory_order_acquire);
    std::atomic_signal_fence(std::memory_order_acq_rel);
    int64_t ns = base_ns_ + static_cast<int64_t>((tsc - base_tsc_) * ns_per_tsc_);
    std::atomic_signal_fence(std::memory_order_acq_rel);
    uint32_t after_seq = param_seq_.load(std::memory_order_acquire);
} while((seq0 != seq1) || (seq0 & 1));
return ns;
*/

template <int32_t kCachelineSize>
int64_t __attribute__((always_inline)) TSCNS<kCachelineSize>::rdns() const
{
    return tsc2ns(rdtsc());
}

template <int32_t kCachelineSize>
int64_t __attribute__((always_inline)) TSCNS<kCachelineSize>::rdsysns()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

template <int32_t kCachelineSize>
double __attribute__((always_inline)) TSCNS<kCachelineSize>::getTscGhz() const
{
    return 1.0 / ns_per_tsc_;
}

// Linux kernel sync time by finding the first trial with tsc diff < 50000
// We try several times and return the one with the mininum tsc diff.
template <int32_t kCachelineSize>
void TSCNS<kCachelineSize>::syncTime(int64_t & tsc_out, int64_t & ns_out)
{
    // Try N = 3 times, find the closest tsc adjacent pair 
    // (meaning the CPU frequency does not change a lot within this interval)
    // And use the average of the two as tsc result 
    constexpr int32_t N = 3;
    std::array<int64_t, N + 1> tsc;
    std::array<int64_t, N + 1> ns;
    tsc[0] = rdtsc();
    for (int32_t i = 1; i <= N; i++)
    {
        ns[i] = rdsysns();
        tsc[i] = rdtsc();
    }
    int32_t best = 1;
    for(int i = 2; i < N + 1; i++) 
    {
        if(tsc[i] - tsc[i - 1] < tsc[best] - tsc[best - 1])
        {
            // find the closest tsc pair
            best = i;
        }
    }
    tsc_out = (tsc[best] + tsc[best - 1]) >> 1;
    ns_out = ns[best];
}

template <int32_t kCachelineSize>
void TSCNS<kCachelineSize>::saveParam(int64_t base_tsc, int64_t sys_ns, int64_t base_ns_err, double new_ns_per_tsc)
{
    base_ns_err_ = base_ns_err;
    // "tsc2ns" won't access "base_ns_err", no need to protect inside the memory barrier
    next_calibrate_tsc_ = base_tsc + static_cast<int64_t>((calibrate_interval_ns_ - 1'000) / new_ns_per_tsc);
    uint32_t seq = param_seq_.load(std::memory_order_relaxed);
    param_seq_.store(++seq, std::memory_order_release);
    std::atomic_signal_fence(std::memory_order_acq_rel);
    base_tsc_ = base_tsc;
    base_ns_ = sys_ns + base_ns_err;
    ns_per_tsc_ = new_ns_per_tsc;
    std::atomic_signal_fence(std::memory_order_acq_rel);
    // Use memory fence here, protecting the stores of normal variables, while still allowing these normal
    // stores to be reordered with each other for better performance.
    param_seq_.store(++seq, std::memory_order_release);
}
