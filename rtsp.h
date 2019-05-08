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

#ifndef RTSP_H
#define RTSP_H

#include <string>
#include <deque>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>

#include "utils.h"
#include "base.h"

#include "liveMedia.hh"
#include "BasicUsageEnvironment.hh"
#include "FramedSource.hh"
#include "GroupsockHelper.hh"

namespace detector {

class Rtsp;
class LiveSource : public FramedSource {
  public:
    static LiveSource* createNew(UsageEnvironment* env, Rtsp* owner) {
      return new LiveSource(env, owner);
    }

  public:
    ~LiveSource();

  protected:
    LiveSource(UsageEnvironment* env, Rtsp* owner);

  public:
    EventTriggerId evt_id_;

  private:
    UsageEnvironment* env_;
    Rtsp* owner_;
    virtual void doGetNextFrame();
    static void deliverFrame0(void* data);
    void deliverFrame();
};

class Rtsp : public Base, Base::Listener {
  public:
    static std::unique_ptr<Rtsp> create(unsigned int yield_time, bool quiet, 
        unsigned int bitrate, unsigned int framerate, std::string& unicast);
    virtual ~Rtsp();

  public:
    virtual bool addMessage(Base::Listener::Message msg, void* data);

  protected:
    Rtsp() = delete;
    Rtsp(unsigned int yield_time);
    bool init(bool quiet, unsigned int bitrate, unsigned int framerate, 
        std::string& unicast);

  protected:
    virtual bool waitingToRun();
    virtual bool running();
    virtual bool paused();
    virtual bool waitingToHalt();

  public:
    bool deliverFrame(unsigned int& max_size, unsigned int& frame_size, 
      unsigned int& trunc, struct timeval& pts, unsigned int& duration, 
      unsigned char* fTo);
    static bool deliverFrame0(Rtsp* self, unsigned int& max_size, 
      unsigned int& frame_size, unsigned int& trunc, struct timeval& pts, 
      unsigned int& duration, unsigned char* fTo);

  public:
    LiveSource* live_src_;
    char live_watch_;

  private:
    bool quiet_;
    unsigned int bitrate_;
    unsigned int framerate_;
    std::string unicast_;
    UsageEnvironment* env_;
    const unsigned output_max_ = {3 * 1024 * 1024};
    const unsigned cname_len_ = {100};

    Semaphore live_sem_;
    std::thread live_;
    void liveProc();
    static void liveProc0(Rtsp* self);

    class RtspNal {
      public:
        RtspNal() = delete;
        RtspNal(unsigned int len) 
          : length(len), nal(len) {}
        ~RtspNal() {}
      public:
        unsigned int length;
        std::vector<unsigned char> nal;
    };
    std::timed_mutex nal_lock_;
    const unsigned int nal_timeout_ = {20};
    const unsigned int nal_num_ = {20};
    const unsigned int nal_len_ = {20 * 1024};
    std::deque<std::shared_ptr<Rtsp::RtspNal>> nal_pool_;
    std::deque<std::shared_ptr<Rtsp::RtspNal>> nal_work_;

    unsigned int overflow_len_ = {0};
    std::vector<unsigned char> overflow_;

    std::atomic<bool> rtsp_on_;
    static void afterPlay(void* data);
};

} // namespace detector

#endif // RTSP_H
