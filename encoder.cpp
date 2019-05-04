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

#include "encoder.h"

namespace tracker {

#define OMX_INIT_STRUCTURE(a) \
  std::memset(&(a), 0, sizeof(a)); \
  (a).nSize = sizeof(a); \
  (a).nVersion.nVersion = OMX_VERSION; \
  (a).nVersion.s.nVersionMajor = OMX_VERSION_MAJOR; \
  (a).nVersion.s.nVersionMinor = OMX_VERSION_MINOR; \
  (a).nVersion.s.nRevision = OMX_VERSION_REVISION; \
  (a).nVersion.s.nStep = OMX_VERSION_STEP

Encoder::Encoder(unsigned int yield_time)
  : Base(yield_time) {
}

Encoder::~Encoder() {
}

std::unique_ptr<Encoder> Encoder::create(unsigned int yield_time, bool quiet, 
    Rtsp* rtsp, unsigned int framerate, unsigned int width, unsigned int height, 
    unsigned int bitrate, std::string& output, unsigned int testtime) {
  auto obj = std::unique_ptr<Encoder>(new Encoder(yield_time));
  obj->init(quiet, rtsp, framerate, width, height, bitrate, output, testtime);
  return obj;
}

bool Encoder::init(bool quiet, Rtsp* rtsp, unsigned int framerate, 
    unsigned int width, unsigned int height, unsigned int bitrate, 
    std::string& output, unsigned int testtime) {

  quiet_ = quiet;
  rtsp_ = rtsp;
  framerate_ = framerate;
  width_ = width;
  height_ = height;

  luma_len_ = ALIGN_16B(width_) * ALIGN_16B(height_);
  chrom_len_ = luma_len_ / 4;

  bitrate_ = bitrate;
  output_ = output;
  testtime_ = testtime;

  encode_on_ = false;

  return true; 
}

bool Encoder::addMessage(Base::Listener::Message msg, void* data) {

  if (msg != Base::Listener::Message::kScratchBuf &&
      msg != Base::Listener::Message::kBoxBuf) {
    dbgMsg("encoder message not recognized\n");
    return false;
  }

  if (msg == Base::Listener::Message::kScratchBuf) {
    std::unique_lock<std::timed_mutex> lck(frame_lock_, std::defer_lock);

    if (!lck.try_lock_for(std::chrono::microseconds(Base::Listener::timeout_))) {
      dbgMsg("encoder frame lock busy\n");
      return false;
    }

    if (frame_pool_.size() == 0) {
      dbgMsg("no encoder buffers available\n");
      return false;
    }

    auto scratch = *static_cast<std::shared_ptr<Base::Listener::ScratchBuf>*>(data);
    if (frame_len_ != scratch->length) {
      dbgMsg("encoder buffer size mismatch\n");
      return false;
    }

    differ_copy_.begin();
    auto frame = frame_pool_.front();
    frame_pool_.pop();
    frame->scratch = scratch;
    frame_work_.push(frame);
    differ_copy_.end();

  } else if (msg == Base::Listener::Message::kBoxBuf) {
    std::unique_lock<std::timed_mutex> lck(targets_lock_, std::defer_lock);

    if (!lck.try_lock_for(std::chrono::microseconds(Base::Listener::timeout_))) {
      dbgMsg("encoder target lock busy\n");
      return false;
    }

    targets_ = *(static_cast<std::shared_ptr<std::vector<Base::Listener::BoxBuf>>*>(data));
  }

  return true;
}

#ifdef OUTPUT_VARIOUS_BITS_OF_INFO
void Encoder::printDef(OMX_PARAM_PORTDEFINITIONTYPE def) {
  const char* dir;
  if (def.eDir == OMX_DirInput) {
    dir = "in";
  } else {
    dir = "out";
  }
  dbgMsg("  Port %u: %s %u/%u %u %u %s,%s,%s %ux%u %ux%u @%u %u\n",
      def.nPortIndex,
      dir,
      def.nBufferCountActual,
      def.nBufferCountMin,
      def.nBufferSize,
      def.nBufferAlignment,
      def.bEnabled ? "enabled" : "disabled",
      def.bPopulated ? "populated" : "not pop",
      def.bBuffersContiguous ? "contig" : "not contig",
      def.format.video.nFrameWidth,
      def.format.video.nFrameHeight,
      def.format.video.nStride,
      def.format.video.nSliceHeight,
      def.format.video.xFramerate,
      def.format.video.eColorFormat);
}
#endif

OMX_ERRORTYPE Encoder::eventHandler(OMX_HANDLETYPE hnd, OMX_PTR self,
    OMX_EVENTTYPE evt, OMX_U32 d1, OMX_U32 d2, OMX_PTR data) {
  Encoder* enc = static_cast<Encoder*>(self);
  switch (evt) {
    case OMX_EventCmdComplete:
      if (d1 == OMX_CommandFlush) {
        enc->omx_flush_sem_.post();
      }
      break;
    case OMX_EventError:
      break;
    default:
      break;
  }
  return OMX_ErrorNone;
}

OMX_ERRORTYPE Encoder::emptyHandler(OMX_HANDLETYPE hnd, OMX_PTR self,
    OMX_BUFFERHEADERTYPE* buf) {
  Encoder* enc = static_cast<Encoder*>(self);
  enc->omx_encode_sem_.post();
  return OMX_ErrorNone;
}

OMX_ERRORTYPE Encoder::fillHandler(OMX_HANDLETYPE hnd, OMX_PTR self,
    OMX_BUFFERHEADERTYPE* buf) {
  Encoder* enc = static_cast<Encoder*>(self);
  enc->omx_encode_sem_.post();
  return OMX_ErrorNone;
}

void Encoder::blockOnPortChange(OMX_U32 idx, OMX_BOOL enable) {
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  OMX_INIT_STRUCTURE(port_def);
  port_def.nPortIndex = idx;
  unsigned int i = 0;
  while (i++ == 0 || port_def.bEnabled != enable) {
    OMX_ERRORTYPE err = OMX_GetParameter(omx_hnd_, 
        OMX_IndexParamPortDefinition, &port_def);
    if (err != OMX_ErrorNone) {
      dbgMsg("failed block port\n");
    }
    if (port_def.bEnabled != enable) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
}

void Encoder::blockOnStateChange(OMX_STATETYPE state) {
  OMX_STATETYPE s;
  unsigned int i = 0;
  while (i++ == 0 || s != state) {
    OMX_GetState(omx_hnd_, &s);
    if (s != state) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
}

bool Encoder::waitingToRun() {

  if (!encode_on_) {

    // create encoded file
    if (testtime_ != 0) {
      dbgMsg("create output file\n");
      if (output_.empty()) {
        fd_enc_ = stdout;
      } else {
        fd_enc_ = fopen(output_.c_str(), "wb");
        if (fd_enc_ == nullptr) {
          dbgMsg("failed: create outputfile\n");
          return false;
        }
      }
    }

    // create frame pool
    dbgMsg("create frame pool\n");
    frame_len_ = ALIGN_16B(width_) * ALIGN_16B(height_) * 3 / 2;
    for (unsigned int i = 0; i < frame_num_; i++) {
      frame_pool_.push(std::shared_ptr<Encoder::Frame>(new Encoder::Frame()));
    }

    // init bcm
    dbgMsg("int bcm\n");
    bcm_host_init();

    // init omx
    dbgMsg("init omx\n");
    if (OMX_Init() != OMX_ErrorNone) {
      return false;
    }

    // create omx component handles
    dbgMsg("create omx component handles\n");
    OMX_CALLBACKTYPE callbacks;
    std::memset(&callbacks, 0, sizeof(callbacks));
    callbacks.EventHandler    = eventHandler;
    callbacks.EmptyBufferDone = emptyHandler;
    callbacks.FillBufferDone  = fillHandler;
    OMX_ERRORTYPE err = OMX_GetHandle(&omx_hnd_, 
        const_cast<char*>("OMX.broadcom.video_encode"), 
        this, &callbacks);
    if (err != OMX_ErrorNone) {
      dbgMsg("failed: create omx component handles: 0x%x\n", err);
      return false;
    }
    OMX_INDEXTYPE types[] = {
      OMX_IndexParamAudioInit, OMX_IndexParamVideoInit,
      OMX_IndexParamImageInit, OMX_IndexParamOtherInit
    };
    OMX_PORT_PARAM_TYPE ports;
    OMX_INIT_STRUCTURE(ports);
    OMX_GetParameter(omx_hnd_, OMX_IndexParamVideoInit, &ports);
    for (unsigned int i = 0; i < 4; i++) {
      if (OMX_GetParameter(omx_hnd_, types[i], &ports) == OMX_ErrorNone) {
        for (unsigned int idx = ports.nStartPortNumber; 
            idx < ports.nStartPortNumber + ports.nPorts; idx++) {
          err = OMX_SendCommand(omx_hnd_, OMX_CommandPortDisable, idx, NULL);
          if (err != OMX_ErrorNone) {
            dbgMsg("failed: disable ports\n");
            return false;
          }
          blockOnPortChange(idx, OMX_FALSE);
        }
      }
    }

    // get video encode settings port 200
    dbgMsg("get video encode settings port 200\n");
    OMX_PARAM_PORTDEFINITIONTYPE port_def;
    OMX_INIT_STRUCTURE(port_def);
    port_def.nPortIndex = 200;
    err = OMX_GetParameter(omx_hnd_, OMX_IndexParamPortDefinition, &port_def);
    if (err != OMX_ErrorNone) {
      dbgMsg("failed: get omx paramter port 200\n");
      return false;
    }

#ifdef OUTPUT_VARIOUS_BITS_OF_INFO
    printDef(port_def);
#endif

    // set video encode settings port 200
    dbgMsg("set video encode settings port 200\n");
    port_def.format.video.nFrameWidth = width_;
    port_def.format.video.nFrameHeight = height_;
    port_def.format.video.xFramerate = framerate_ << 16;
    port_def.format.video.nSliceHeight = ALIGN_16B(port_def.format.video.nFrameHeight);
    port_def.format.video.nStride = ALIGN_16B(port_def.format.video.nFrameWidth);
    port_def.format.video.eColorFormat = OMX_COLOR_FormatYUV420PackedPlanar;
    err = OMX_SetParameter(omx_hnd_, OMX_IndexParamPortDefinition, &port_def);
    if (err != OMX_ErrorNone) {
      dbgMsg("failed: set omx paramter port 200\n");
      return false;
    }

#ifdef OUTPUT_VARIOUS_BITS_OF_INFO
    printDef(port_def);
#endif

    // set video encode settings for port 201
    dbgMsg("set video encode settings port 201\n");
    OMX_VIDEO_PARAM_PORTFORMATTYPE format;
    OMX_INIT_STRUCTURE(format);
    format.nPortIndex = 201;
    format.eColorFormat = OMX_COLOR_FormatUnused;
    format.eCompressionFormat = OMX_VIDEO_CodingAVC;
    err = OMX_SetParameter(omx_hnd_, OMX_IndexParamVideoPortFormat, &format);
    if (err != OMX_ErrorNone) {
      dbgMsg("failed: set omx parameter to port 201\n");
      return false;
    }

    // set bitrate
    dbgMsg("set bitrate\n");
    OMX_VIDEO_PARAM_BITRATETYPE bitrate_type;
    OMX_INIT_STRUCTURE(bitrate_type);
    bitrate_type.eControlRate = OMX_Video_ControlRateVariable;
    bitrate_type.nTargetBitrate = bitrate_;
    bitrate_type.nPortIndex = 201;
    err = OMX_SetParameter(omx_hnd_, OMX_IndexParamVideoBitrate, &bitrate_type);
    if (err != OMX_ErrorNone) {
      dbgMsg("failed: set bitrate\n");
      return false;
    }
    OMX_INIT_STRUCTURE(bitrate_type);
    bitrate_type.nPortIndex = 201;
    err = OMX_GetParameter(omx_hnd_, OMX_IndexParamVideoBitrate, &bitrate_type);
    if (err != OMX_ErrorNone) {
      dbgMsg("failed: get bitrate\n");
      return false;
    }
    dbgMsg("current bitrate:%u\n", bitrate_type.nTargetBitrate);

    // idle omx
    dbgMsg("idle omx\n");
    err = OMX_SendCommand(omx_hnd_, OMX_CommandStateSet, OMX_StateIdle, NULL);
    if (err != OMX_ErrorNone) {
      dbgMsg("failed: change to idle state\n");
      return false;
    }
    blockOnStateChange(OMX_StateIdle);

    // enable ports
    dbgMsg("enable ports\n");
    err = OMX_SendCommand(omx_hnd_, OMX_CommandPortEnable, 200, NULL);
    if (err != OMX_ErrorNone) {
      dbgMsg("failed: enable port 200x\n");
      return false;
    }
    blockOnPortChange(200, OMX_TRUE);
    err = OMX_SendCommand(omx_hnd_, OMX_CommandPortEnable, 201, NULL);
    if (err != OMX_ErrorNone) {
      dbgMsg("failed: enable port 201\n");
      return false;
    }
    blockOnPortChange(200, OMX_TRUE);

    // allocate buffers
    dbgMsg("allocate buffers\n");
    OMX_INIT_STRUCTURE(port_def);
    port_def.nPortIndex = 200;
    err = OMX_GetParameter(omx_hnd_, OMX_IndexParamPortDefinition, &port_def);
    if (err != OMX_ErrorNone) {
      dbgMsg("failed: allocate port 200 buffers get param\n");
      return false;
    }
    dbgMsg("port 200 allocate size: %d\n", port_def.nBufferSize);
    err = OMX_AllocateBuffer(omx_hnd_, &omx_buf_in_, 200, NULL, port_def.nBufferSize);
    if (err != OMX_ErrorNone) {
      dbgMsg("failed: allocate port 200 buffers\n");
      return false;
    }
    OMX_INIT_STRUCTURE(port_def);
    port_def.nPortIndex = 201;
    err = OMX_GetParameter(omx_hnd_, OMX_IndexParamPortDefinition, &port_def);
    if (err != OMX_ErrorNone) {
      dbgMsg("failed: allocate port 201 buffers get param\n");
      return false;
    }
    dbgMsg("port 201 allocate size: %d\n", port_def.nBufferSize);
    err = OMX_AllocateBuffer(omx_hnd_, &omx_buf_out_, 201, NULL, port_def.nBufferSize);
    if (err != OMX_ErrorNone) {
      dbgMsg("failed: allocate port 201 buffers\n");
      return false;
    }

    // execute omx
    dbgMsg("execute omx\n");
    err = OMX_SendCommand(omx_hnd_, OMX_CommandStateSet, OMX_StateExecuting, NULL);
    if (err != OMX_ErrorNone) {
      dbgMsg("failed: change to idle state\n");
      return false;
    }
    blockOnStateChange(OMX_StateExecuting);

    encode_on_ = true;
  }

  return true;
}

void Encoder::overlay(std::shared_ptr<Encoder::Frame> frame) {

  std::unique_lock<std::timed_mutex> tlck(targets_lock_);

  if (targets_ != nullptr) {
    if (targets_->size() != 0) {
      std::for_each(targets_->begin(), targets_->end(),
          [=](Base::Listener::BoxBuf const & box) {
          Encoder::YUV yuv;
            if (box.type == Base::Listener::BoxBuf::Type::kPerson) {
              yuv = red_yuv_;
            } else if (box.type == Base::Listener::BoxBuf::Type::kPet) {
              yuv = green_yuv_;
            } else {
              yuv = blue_yuv_;
            }
            drawYUVBox(thickness_,
                frame->scratch->buf.data(), width_, 
                frame->scratch->buf.data() + luma_len_, width_ / 2,
                frame->scratch->buf.data() + luma_len_ + chrom_len_, width_ / 2,
                box.x, box.y, box.w, box.h,
                yuv.y, yuv.u, yuv.v);
          });
    }
  }
}

bool Encoder::running() {

  if (encode_on_) {
    {
      std::unique_lock<std::timed_mutex> lck(frame_lock_);

      // a frame is ready
      if (frame_work_.size() != 0) {
        auto frame = frame_work_.front();

        // overlay target boxes
        overlay(frame);

        // fill the input buffer
        std::memcpy(omx_buf_in_->pBuffer, frame->scratch->buf.data(), frame->scratch->length);
        omx_buf_in_->nOffset = 0;
        omx_buf_in_->nFilledLen = frame->scratch->length;

        // return the frame to the queue
        frame_work_.pop();
        frame->scratch = 
          std::shared_ptr<Base::Listener::ScratchBuf>(new Base::Listener::ScratchBuf());
        frame_pool_.push(frame);

        // let capture fill another buffer while we wait
        lck.unlock();

        // start encoding...
        differ_encode_.begin();
        OMX_ERRORTYPE err = OMX_EmptyThisBuffer(omx_hnd_, omx_buf_in_);
        if (err != OMX_ErrorNone) {
          dbgMsg("failed: omx empty buffer\n");
          return false;
        }
        omx_encode_sem_.wait();

        // ... wait for result
        err = OMX_FillThisBuffer(omx_hnd_, omx_buf_out_);
        if (err != OMX_ErrorNone) {
          dbgMsg("failed: omx fill buffer\n");
          return false;
        }
        omx_encode_sem_.wait();
        differ_encode_.end();

        // record the h264
        if (testtime_ != 0) {
          fwrite(omx_buf_out_->pBuffer, 1, omx_buf_out_->nFilledLen, fd_enc_);
        }

        // stream the h264
        if (rtsp_) {
          Base::Listener::NalBuf nal(omx_buf_out_->nFilledLen, omx_buf_out_->pBuffer);
          if (!rtsp_->addMessage(Base::Listener::Message::kNalBuf, &nal)) {
            dbgMsg("warning: rtsp is busy\n");
          }
        }

        // omx buffer used;
        omx_buf_out_->nFilledLen = 0;
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  return true;
}

bool Encoder::paused() {
  return true;
}

bool Encoder::waitingToHalt() {

  if (encode_on_) {
    encode_on_ = false;

    // flush the port buffers
    dbgMsg("flush the port buffers\n");
    OMX_ERRORTYPE err = OMX_SendCommand(omx_hnd_, OMX_CommandFlush, 200, NULL);
    if (err != OMX_ErrorNone) {
      dbgMsg("failed: flush port 200 buffers\n");
      return false;
    }
    omx_flush_sem_.wait();
    err = OMX_SendCommand(omx_hnd_, OMX_CommandFlush, 201, NULL);
    if (err != OMX_ErrorNone) {
      dbgMsg("failed: flush port 201 buffers\n");
      return false;
    }
    omx_flush_sem_.wait();

    // disable all ports
    dbgMsg("disable all ports\n");
    err = OMX_SendCommand(omx_hnd_, OMX_CommandPortDisable, 200, NULL);
    if (err != OMX_ErrorNone) {
      dbgMsg("failed: disable port 200\n");
      return false;
    }
    blockOnPortChange(200, OMX_FALSE);
    err = OMX_SendCommand(omx_hnd_, OMX_CommandPortDisable, 201, NULL);
    if (err != OMX_ErrorNone) {
      dbgMsg("failed: disable port 201\n");
      return false;
    }
    blockOnPortChange(201, OMX_FALSE);

    // free all buffers
    dbgMsg("free all buffers\n");
    err = OMX_FreeBuffer(omx_hnd_, 200, omx_buf_in_);
    if (err != OMX_ErrorNone) {
      dbgMsg("failed:  free port 200 buffer\n");
      return false;
    }
    err = OMX_FreeBuffer(omx_hnd_, 201, omx_buf_out_);
    if (err != OMX_ErrorNone) {
      dbgMsg("failed:  free port 201 buffer\n");
      return false;
    }

    // transition to idle state
    dbgMsg("transition to loaded state\n");
    err = OMX_SendCommand(omx_hnd_, OMX_CommandStateSet, OMX_StateIdle, NULL);
    if (err != OMX_ErrorNone) {
      dbgMsg("failed: transition to idle state: 0x%x\n", err);
      return false;
    }
    blockOnStateChange(OMX_StateIdle);
    err = OMX_SendCommand(omx_hnd_, OMX_CommandStateSet, OMX_StateLoaded, NULL);
    if (err != OMX_ErrorNone) {
      dbgMsg("failed: transition to loaded state\n");
      return false;
    }
    blockOnStateChange(OMX_StateLoaded);

   // free component handle
    dbgMsg("free component handle\n");
    err = OMX_FreeHandle(omx_hnd_);
    if (err != OMX_ErrorNone) {
      dbgMsg("failed: free component handle\n");
      return false;
    }

    OMX_Deinit();
    bcm_host_deinit();

    if (testtime_ != 0) {
      if (fd_enc_ != stdout) {
        fclose(fd_enc_);
      }
    }

    // report
    if (!quiet_) {
      fprintf(stderr, "\nEncoder Results...\n");
      fprintf(stderr, "  image copy time (us):   high:%u avg:%u low:%u frames:%d\n", 
          differ_copy_.getHigh_usec(), differ_copy_.getAvg_usec(), 
          differ_copy_.getLow_usec(),differ_copy_.getCnt());
      fprintf(stderr, "  image encode time (us): high:%u avg:%u low:%u frames:%d\n", 
          differ_encode_.getHigh_usec(), differ_encode_.getAvg_usec(), 
          differ_encode_.getLow_usec(),differ_encode_.getCnt());
    }
  }
  return true;
}

} // namespace tracker

