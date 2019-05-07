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

#include <chrono>
#include <cstring>
#include <algorithm>

#include "rtsp.h"

namespace tracker {

LiveSource::LiveSource(UsageEnvironment* env, Rtsp* owner) 
  : FramedSource(*env), env_(env), owner_(owner) {

  if (evt_id_ == 0) {
    evt_id_ = env_->taskScheduler().createEventTrigger(deliverFrame0);
  }
}

LiveSource::~LiveSource() {
  if (evt_id_ != 0) {
    env_->taskScheduler().deleteEventTrigger(evt_id_);
    evt_id_ = 0;
  }
}

void LiveSource::doGetNextFrame() {
  if (owner_->live_watch_) {
    dbgMsg("doGetNextFrame: shutting down\n");
    return;
  }
  deliverFrame();
}

void LiveSource::deliverFrame0(void* data) {
  LiveSource* self = static_cast<LiveSource*>(data);
  self->deliverFrame();
}

void LiveSource::deliverFrame() {
  if (isCurrentlyAwaitingData()) {
    if (owner_->deliverFrame0(owner_, fMaxSize, fFrameSize, fNumTruncatedBytes, 
          fPresentationTime, fDurationInMicroseconds, fTo)) {
      FramedSource::afterGetting(this);
    }
  }
}


Rtsp::Rtsp(unsigned int yield_time)
  : Base(yield_time) {
}

Rtsp::~Rtsp() {
}

std::unique_ptr<Rtsp> Rtsp::create(unsigned int yield_time, bool quiet, 
    unsigned int bitrate, unsigned int framerate, std::string& unicast) {
  auto obj = std::unique_ptr<Rtsp>(new Rtsp(yield_time));
  obj->init(quiet, bitrate, framerate, unicast);
  return obj;
}

bool Rtsp::init(bool quiet, unsigned int bitrate, unsigned int framerate, 
    std::string& unicast) {

  quiet_ = quiet;
  bitrate_ = bitrate;
  framerate_ = framerate;
  unicast_ = unicast;
  rtsp_on_ = false;

  return true; 
}

bool Rtsp::addMessage(Base::Listener::Message msg, void* data) {

  if (msg != Base::Listener::Message::kNalBuf) {
    dbgMsg("rtsp message not recognized\n");
    return false;
  }

  std::unique_lock<std::timed_mutex> lck(nal_lock_, std::defer_lock);

  if (!lck.try_lock_for(std::chrono::microseconds(Base::Listener::timeout_))) {
    dbgMsg("rtsp nal lock busy\n");
    return false;
  }

  // drop a queued frame if the pool is empty
  std::shared_ptr<Rtsp::RtspNal> rtsp_nal;
  if (nal_pool_.size() == 0) {
    dbgMsg("dropping back.  queue size: %d\n", nal_work_.size());
    rtsp_nal = nal_work_.back();
    nal_work_.pop_back();
  } else {
    rtsp_nal = nal_pool_.front();
    nal_pool_.pop_front();
  }

  auto nal = static_cast<Base::Listener::NalBuf*>(data);

  if (nal->length > rtsp_nal->nal.size()) {
    dbgMsg("--------------------------resize nal: sz=%d\n", nal->length);
    rtsp_nal->nal.resize(nal->length, 0);
  }
  std::memcpy(rtsp_nal->nal.data(), nal->addr, nal->length);
  rtsp_nal->length = nal->length;

  nal_work_.push_back(rtsp_nal);

  return true;
}

bool Rtsp::deliverFrame(unsigned int& max_size, unsigned int& frame_size, 
    unsigned int& trunc, struct timeval& pts, unsigned int& duration, unsigned char* to) {

  std::unique_lock<std::timed_mutex> lck(nal_lock_, std::defer_lock);

  if (!lck.try_lock_for(std::chrono::microseconds(nal_timeout_))) {
    dbgMsg("deliver frame nal lock busy\n");
    return false;
  }

  if (nal_work_.size() != 0) {

    auto rtsp_nal = nal_work_.front();
    nal_work_.pop_front();

    if (rtsp_nal->length > max_size) {
      frame_size = max_size;
      trunc = rtsp_nal->length - max_size;
      if (overflow_.size() < trunc + overflow_len_) {
        overflow_.resize(trunc + overflow_len_);
      }
      memcpy(overflow_.data() + overflow_len_, rtsp_nal->nal.data() + frame_size, trunc);
      overflow_len_ += trunc;
    } else {
      frame_size = rtsp_nal->length;
      if (overflow_len_) {
        auto it = rtsp_nal->nal.begin();
        rtsp_nal->nal.insert(it, overflow_len_, 0);
        memcpy(rtsp_nal->nal.data(), overflow_.data(), overflow_len_);
        frame_size += overflow_len_;
        overflow_len_ = 0;
      }
    }
    gettimeofday(&pts, NULL);
    duration = 0;
//    duration = 1000000 / framerate_;
    memcpy(to, rtsp_nal->nal.data(), frame_size);
    nal_pool_.push_back(rtsp_nal);
    return true;
  }

  return false;
}

bool Rtsp::deliverFrame0(Rtsp* self, unsigned int& max_size, unsigned int& frame_size, 
    unsigned int& trunc, struct timeval& pts, unsigned int& duration, unsigned char* to) {
  return self->deliverFrame(max_size, frame_size, trunc, pts, duration, to);
}

void Rtsp::liveProc() {
  // create task scheduler and environment
  dbgMsg("create task scheduler and environment\n");
  auto schd = std::unique_ptr<BasicTaskScheduler>(BasicTaskScheduler::createNew());
  env_ = BasicUsageEnvironment::createNew(*schd.get());

  // unicast or multicast address
  dbgMsg("unicast or multicast address\n");
  struct in_addr dst_addr;
  if (unicast_.empty()) {
    dbgMsg("  multicast address\n");
    dst_addr.s_addr = chooseRandomIPv4SSMAddress(*env_);
  } else {
    dbgMsg("  unicast address\n");
    dst_addr.s_addr = our_inet_addr(unicast_.c_str());
  }
 
  // create ports
  dbgMsg("create ports\n");
  const unsigned short rtpPortNum = 18888;
  const unsigned short rtcpPortNum = rtpPortNum+1;
  const unsigned char ttl = 255;
  const Port rtpPort(rtpPortNum);
  const Port rtcpPort(rtcpPortNum);

  // create sockets
  dbgMsg("create sockets\n");
  Groupsock rtp_sock(*env_, dst_addr, rtpPort, ttl);
  Groupsock rtcp_sock(*env_, dst_addr, rtcpPort, ttl);
  if (unicast_.empty()) {
    rtp_sock.multicastSendOnly();
    rtcp_sock.multicastSendOnly();
  }

  // create video sink
  dbgMsg("create video sink\n");
  OutPacketBuffer::maxSize = output_max_;
  RTPSink* video_snk = H264VideoRTPSink::createNew(*env_, &rtp_sock, 96);
  if (video_snk == nullptr) {
    dbgMsg("failed:  create video sink\n");
  }

  // create rtcp
  dbgMsg("create rtcp\n");
  std::vector<char> cname(cname_len_, 0);
  gethostname(cname.data(), cname_len_);
  RTCPInstance* rtcp = RTCPInstance::createNew(*env_, &rtcp_sock,
    bitrate_ * 10 / 1000, (unsigned char*)cname.data(), video_snk, NULL, 
    unicast_.empty() ? True : False);
  if (rtcp == nullptr) {
    dbgMsg("failed:  create rtcp\n");
  }

  // create rtsp server
  dbgMsg("create rtsp server\n");
  RTSPServer* rtsp_server = RTSPServer::createNew(*env_, 8554);
  if (rtsp_server == nullptr) {
    dbgMsg("failed: create RTSP server %s\n", env_->getResultMsg());
  }

  // create media session
  dbgMsg("create media session\n");
  ServerMediaSession* sms = ServerMediaSession::createNew(*env_, "camera", "tracker",
      "Session streamed by -tracker-", unicast_.empty() ? True : False);
  sms->addSubsession(PassiveServerMediaSubsession::createNew(*video_snk, rtcp));
  rtsp_server->addServerMediaSession(sms);

  // display stream url
  dbgMsg("display stream url\n");
  char* url = rtsp_server->rtspURL(sms);
  fprintf(stderr, "Play this stream using: %s\n", url);
  delete[] url;

  // start play
  dbgMsg("start play...\n");
  live_src_ = LiveSource::createNew(env_, this);
  H264VideoStreamFramer* video_src = 
    H264VideoStreamFramer::createNew(*env_, live_src_, False);
  video_snk->startPlaying(*video_src, afterPlay, video_snk);

  // run until cancelled
  live_sem_.post();
  env_->taskScheduler().doEventLoop(&live_watch_);

  // shutdown
  dbgMsg("rtsp shutdown\n");
  video_snk->stopPlaying();
  Medium::close(video_snk);
  Medium::close(video_src);
  Medium::close(rtsp_server);
  Medium::close(rtcp);
  env_->reclaim();
}

void Rtsp::liveProc0(Rtsp* self) {
  self->liveProc();
}

bool Rtsp::waitingToRun() {

  if (!rtsp_on_) {

    // create nal pool
    dbgMsg("create nal pool\n");
    for (unsigned int i = 0; i < nal_num_; i++) {
      nal_pool_.push_back(std::shared_ptr<Rtsp::RtspNal>(new RtspNal(nal_len_)));
    }

    // launch live thread
    dbgMsg("launch live thread\n");
    live_watch_ = 0;
    live_ = std::thread(liveProc0, this);

    // wait...
    live_sem_.wait();

    // begin streaming
    dbgMsg("begin streaming\n");
    rtsp_on_ = true;
 }

  return true;
}

void Rtsp::afterPlay(void* data) {
  dbgMsg("afterPlay\n");
}

bool Rtsp::running() {
  if (rtsp_on_) {
    std::unique_lock<std::timed_mutex> lck(nal_lock_, std::defer_lock);
    if (lck.try_lock_for(std::chrono::microseconds(Base::Listener::timeout_))) {
      if (nal_work_.size() != 0) {
        env_->taskScheduler().triggerEvent(live_src_->evt_id_, live_src_);
      }
    }
  }
  return true;
}

bool Rtsp::paused() {
  return true;
}

bool Rtsp::waitingToHalt() {

  if (rtsp_on_) {

    // kill live thread
    dbgMsg("kill live thread\n");
    live_watch_ = 1;
    live_.join();

    rtsp_on_ = false;
  }

  return true;
}

} // namespace tracker

