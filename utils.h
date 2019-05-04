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
 * Try './tracker -h' for usage.
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

namespace tracker {

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

class Differ {
  public:
    Differ() 
      : begin_(), end_(),
        diff_(0), diff_sum_(0), 
        cnt_(0),  avg_(0),
        high_(0), low_(std::numeric_limits<uint64_t>::max()) {
    }
    ~Differ() {}

    inline uint64_t toNano(struct timespec& ts) {
      return ts.tv_sec * (uint64_t)1000000000L + ts.tv_nsec;
    }

    inline void begin() { 
      struct timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts);
      begin_ = toNano(ts);
    }

    inline void end() { 
      struct timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts);
      end_ = toNano(ts);

      diff_ = end_ - begin_;
      diff_sum_ += diff_;

      high_ = (high_ < diff_) ? diff_ : high_;
      low_ = (low_ > diff_) ? diff_ : low_;

      cnt_++;
      avg_ = diff_sum_ / cnt_;
    }

    inline unsigned int getCnt()  { return cnt_; } 
    inline unsigned int getAvg_usec()  { return (unsigned int)(avg_ / 1000); }
    inline unsigned int getHigh_usec() { return (unsigned int)(high_ / 1000); }
    inline unsigned int getLow_usec()  { return (unsigned int)(low_ / 1000); }

  private:
    uint64_t begin_;
    uint64_t end_;
    uint64_t diff_;
    uint64_t diff_sum_;
    int cnt_;
    uint64_t avg_;
    uint64_t high_;
    uint64_t low_;
};

} // namespace tracker

#endif // UTILS_H
