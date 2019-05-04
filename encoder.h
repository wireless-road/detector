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

#ifndef ENCODER_H
#define ENCODER_H

#include <string>
#include <queue>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>

#include "utils.h"
#include "base.h"
#include "rtsp.h"

extern "C" {
#include <IL/OMX_Core.h>
#include <IL/OMX_Component.h>
#include <IL/OMX_Video.h>
}

extern "C" {
#include <bcm_host.h>
#include <IL/OMX_Broadcom.h>
}

namespace tracker {

class Encoder : public Base, Base::Listener {
  public:
    static std::unique_ptr<Encoder> create(unsigned int yield_time, bool quiet, 
        Rtsp* rtsp, unsigned int framerate, unsigned int width, unsigned int height, 
        unsigned int bitrate, std::string& output, unsigned int testtime);
    virtual ~Encoder();

  public:
    virtual bool addMessage(Base::Listener::Message msg, void* data);

  protected:
    Encoder() = delete;
    Encoder(unsigned int yield_time);
    bool init(bool quiet, Rtsp* rtsp, unsigned int framerate, unsigned int width,
        unsigned int height, unsigned int bitrate, std::string& output, 
        unsigned int testtime);

  protected:
    virtual bool waitingToRun();
    virtual bool running();
    virtual bool paused();
    virtual bool waitingToHalt();

  private:
    bool quiet_;
    Rtsp* rtsp_;
    unsigned int framerate_;
    unsigned int width_;
    unsigned int height_;
    unsigned int luma_len_;
    unsigned int chrom_len_;
    unsigned int bitrate_;
    std::string output_;
    unsigned int testtime_;

    class YUV {
      public:
        YUV()
          : y(255), u(255), v(255) {}
        YUV(unsigned char luma, unsigned char cr, unsigned char cb) 
          : y(luma), u(cr), v(cb) {}
        YUV(Encoder::YUV const& yuv) = default;
        ~YUV() {}
      public:
        unsigned char y;
        unsigned char u;
        unsigned char v;
    };
    const Encoder::YUV red_yuv_   = { 76,  84, 255};
    const Encoder::YUV green_yuv_ = {149,  43,  21};
    const Encoder::YUV blue_yuv_  = { 29, 255, 107};

    FILE* fd_enc_;

    Semaphore omx_encode_sem_;
    Semaphore omx_flush_sem_;
    OMX_HANDLETYPE omx_hnd_;
    OMX_BUFFERHEADERTYPE* omx_buf_in_;
    OMX_BUFFERHEADERTYPE* omx_buf_out_;
    static OMX_ERRORTYPE eventHandler(OMX_HANDLETYPE hnd, OMX_PTR self,
        OMX_EVENTTYPE evt, OMX_U32 d1, OMX_U32 d2, OMX_PTR data);
    static OMX_ERRORTYPE emptyHandler(OMX_HANDLETYPE hnd, OMX_PTR self,
        OMX_BUFFERHEADERTYPE* buf);
    static OMX_ERRORTYPE fillHandler(OMX_HANDLETYPE hnd, OMX_PTR self,
        OMX_BUFFERHEADERTYPE* buf);
    void blockOnPortChange(OMX_U32 idx, OMX_BOOL enable);
    void blockOnStateChange(OMX_STATETYPE state);

    class Frame {
      public:
        Frame() : scratch(nullptr) {}
        ~Frame() {}
      public:
        std::shared_ptr<Base::Listener::ScratchBuf> scratch;
    };

    std::timed_mutex frame_lock_;
    const unsigned int frame_num_ = 3;
    unsigned int frame_len_;
    std::queue<std::shared_ptr<Encoder::Frame>> frame_pool_;
    std::queue<std::shared_ptr<Encoder::Frame>> frame_work_;

    void overlay(std::shared_ptr<Encoder::Frame> frame);

    std::atomic<bool> encode_on_;

    Differ differ_copy_;
    Differ differ_encode_;

    std::timed_mutex targets_lock_;
    std::shared_ptr<std::vector<Base::Listener::BoxBuf>> targets_;

    const unsigned int thickness_ = 4;

#ifdef OUTPUT_VARIOUS_BITS_OF_INFO
    void printDef(OMX_PARAM_PORTDEFINITIONTYPE def);
#endif
};

} // namespace tracker

#endif // ENCODER_H
