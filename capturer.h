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

#ifndef CAPTURER_H
#define CAPTURER_H

#include <vector>
#include <thread>
#include <atomic>
#include <memory>

#include "utils.h"
#include "base.h"
#include "encoder.h"
#include "tflow.h"

#include "libyuv.h"

namespace tracker {

class Capturer : public Base {
  public:
    static std::unique_ptr<Capturer> create(unsigned int yield_time, bool quiet, 
        Encoder* enc, Tflow* tfl, unsigned int device, unsigned int framerate, 
        int width, int height);
    virtual ~Capturer();

  protected:
    Capturer() = delete;
    Capturer(unsigned int yield_time);
    bool init(bool quiet, Encoder* enc, Tflow* tfl, unsigned int device,
        unsigned int framerate, int width, int height);

  protected:
    virtual bool waitingToRun();
    virtual bool running();
    virtual bool paused();
    virtual bool waitingToHalt();

  private:
    bool quiet_;
    Encoder* enc_;
    Tflow* tfl_;
    unsigned int device_;
    unsigned int framerate_;
    unsigned int width_;
    unsigned int height_;
    const unsigned int channels_ = {3};
    bool width_flip_;
    bool height_flip_;

    unsigned int pix_fmt_;
    unsigned int pix_width_;
    unsigned int pix_height_;

    std::vector<int> formats_ = {
      V4L2_PIX_FMT_RGB24   // in order of preference
    };

    unsigned int frame_cnt_;
    int fd_video_;

    class Frame {
      public:
        Frame() {}
        ~Frame() {}
      public:
        unsigned int id;
        unsigned int length;
        unsigned char* addr;
    };
    const unsigned int frame_num_ = {3};
    std::vector<Capturer::Frame> frame_pool_;

    const unsigned int scratchbuf_num_ = {6};
    std::vector<std::shared_ptr<Base::Listener::ScratchBuf>> scratchbuf_;

    std::atomic<bool> stream_on_;

    int xioctl(int fd, int request, void* arg);

#ifdef CAPTURE_ONE_RAW_FRAME
    const unsigned int capture_cnt_ = {10};
    FILE* fd_raw_;
    void captureFrame(FILE* fd, int fmt, unsigned int len, unsigned char* buf);
#endif
};

} // namespace tracker

#endif // CAPTURER_H
