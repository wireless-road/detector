/*
 * Copyright © 2019 Tyler J. Brooks <tylerjbrooks@digispeaker.com> <https://www.digispeaker.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * <http://www.apache.org/licenses/LICENSE-2.0>
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Try './detector -h' for usage.
 */

#ifndef UTILS_H
#define UTILS_H

#include <linux/videodev2.h>
#include <linux/v4l2-controls.h>

#include <time.h>
#include <stdint.h>
#include <limits>
#include <mutex>
#include <condition_variable>
#include <cstring>

namespace detector {

#define ALIGN_X(x,y) (((x) + (y-1)) & ~(y-1))
#define ALIGN_4K(x)   ALIGN_X(x,4094)
#define ALIGN_1K(x)   ALIGN_X(x,1024)
#define ALIGN_32B(x)  ALIGN_X(x,32)
#define ALIGN_16B(x)  ALIGN_X(x,16)
#define ALIGN_8B(x)   ALIGN_X(x,8)

#if defined(DEBUG_MESSAGES)
#define dbgMsg(...) fprintf(stderr,"%s %d: ",__FILE__,__LINE__); fprintf(stderr,__VA_ARGS__)
#else
#define dbgMsg(...)
#endif

void convert_to_yuv420(int fmt, 
    unsigned char* src, unsigned int src_width, unsigned int src_height,
    unsigned char* dst, unsigned int dst_width, unsigned int dst_height);

void convert_yuv420_to_rgb24(unsigned char* src, unsigned char* dst, 
    unsigned int width, unsigned int height);

bool drawYUVHorizontalLine(unsigned int thick, 
    unsigned char* start, unsigned int stride, 
    unsigned int width, unsigned char val);
bool drawYUVVerticalLine(unsigned int thick, 
    unsigned char* start, unsigned int stride, 
    unsigned int height, unsigned char val);
bool drawYUVBox(unsigned int thick,
    unsigned char* dst_y, unsigned int dst_stride_y,
    unsigned char* dst_u, unsigned int dst_stride_u,
    unsigned char* dst_v, unsigned int dst_stride_v,
    unsigned int x, unsigned int y, unsigned int w, unsigned int h,
    unsigned char val_y, unsigned char val_u, unsigned char val_v);

bool drawRGBBox(unsigned int thick, unsigned char* dst,
    unsigned int width, unsigned int height,
    unsigned int x, unsigned int y, unsigned int w, unsigned int h,
    unsigned char val_r, unsigned char val_g, unsigned char val_b);

const char* BufTypeToStr(unsigned int bt);
const char* BufFieldToStr(unsigned int bf);
const char* BufTimecodeTypeToStr(unsigned int tt);
const char* BufMemoryToStr(unsigned int bm);
const char* ColorspaceToStr(unsigned int cs) ;
const char* PixelFormatToStr(unsigned int pix);

class Semaphore {
  public:
    Semaphore (int count = 0) 
      : cnt_(count) {}

    inline void post() {
      std::unique_lock<std::mutex> lck(mtx_);
      cnt_++;
      cv_.notify_one();
    }
    inline void wait() {
      std::unique_lock<std::mutex> lck(mtx_);
      cv_.wait(lck, [this]() { return cnt_ > 0; });
      cnt_--;
    }
    inline bool try_wait() {
      std::lock_guard<std::mutex> lck(mtx_);
      if (cnt_ > 0) {
        --cnt_;
        return true;
      }
      return false;
    }

  private:
    std::mutex mtx_;
    std::condition_variable cv_;
    int cnt_;
};

template<typename U, typename T>
class DifferBase {
  public:
    DifferBase() 
      : cnt(0),  avg(0),
        high(0), low(std::numeric_limits<U>::max()),
        begin_(), end_(),
        diff_(0), diff_sum_(0) {
    }
    ~DifferBase() {}

    inline void begin() { 
      begin_ = std::chrono::steady_clock::now();
    }

    inline void end() { 
      using namespace std::chrono;
      end_ = steady_clock::now();

      duration<U,T> span = 
        duration_cast<duration<U,T>>(end_ - begin_);

      diff_ = span.count();
      diff_sum_ += diff_;

      high= (high < static_cast<U>(diff_)) ? static_cast<U>(diff_) : high;
      low = (low  > static_cast<U>(diff_)) ? static_cast<U>(diff_) : low;

      cnt++;
      avg = diff_sum_ / cnt;
    }

  public:
    U cnt;
    U avg;
    U high;
    U low;

  private:
    std::chrono::steady_clock::time_point begin_;
    std::chrono::steady_clock::time_point end_;
    uint64_t diff_;
    uint64_t diff_sum_;
};

template<typename T> class MilliDiffer;
template<> class MilliDiffer<uint32_t> : public DifferBase<uint32_t,std::milli> {};
template<> class MilliDiffer<uint64_t> : public DifferBase<uint64_t,std::milli> {};

template<typename T> class MicroDiffer;
template<> class MicroDiffer<uint32_t> : public DifferBase<uint32_t,std::micro> {};
template<> class MicroDiffer<uint64_t> : public DifferBase<uint64_t,std::micro> {};

template<typename T> class NanoDiffer;
template<> class NanoDiffer<uint32_t>  : public DifferBase<uint32_t,std::nano>  {};
template<> class NanoDiffer<uint64_t>  : public DifferBase<uint64_t,std::nano>  {};

} // namespace detector

#endif // UTILS_H
