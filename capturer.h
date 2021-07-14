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

#ifndef CAPTURER_H
#define CAPTURER_H

#include <vector>
#include <thread>
#include <atomic>
#include <memory>

#include "utils.h"
#include "listener.h"
#include "base.h"
#ifndef WITHOUT_ENCODER
#include "encoder.h"
#endif
#include "tflow.h"

namespace detector {

class Capturer : public Base {
  public:
    static std::unique_ptr<Capturer> create(unsigned int yield_time, bool quiet, 
#ifndef WITHOUT_ENCODER
        Encoder* enc,
#endif
        Tflow* tfl, unsigned int device, unsigned int framerate, 
        int width, int height);
    virtual ~Capturer();

  protected:
    Capturer() = delete;
    Capturer(unsigned int yield_time);
    bool init(bool quiet,
#ifndef WITHOUT_ENCODER
        Encoder* enc,
#endif
        Tflow* tfl, unsigned int device,
        unsigned int framerate, int width, int height);

  protected:
    virtual bool waitingToRun();
    virtual bool running();
    virtual bool paused();
    virtual bool waitingToHalt();

  private:
    bool quiet_;
#ifndef WITHOUT_ENCODER
    Encoder* enc_;
    MicroDiffer<uint32_t> differ_enc_;
#endif
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

    const unsigned int framebuf_num_ = {3};
    std::vector<FrameBuf> framebuf_pool_;

    std::atomic<bool> stream_on_;

    int xioctl(int fd, int request, void* arg);

    MicroDiffer<uint32_t> differ_tfl_;
    MicroDiffer<uint32_t> differ_tot_;

#ifdef CAPTURE_ONE_RAW_FRAME
    const unsigned int capture_cnt_ = {10};
    FILE* fd_raw_;
    void captureFrame(FILE* fd, int fmt, unsigned int len, unsigned char* buf);
#endif
};

} // namespace detector

#endif // CAPTURER_H
