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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <string>
#include <algorithm>
#include <cmath>

#include "capturer.h"

namespace detector {

Capturer::Capturer(unsigned int yieldtime) 
  : Base(yieldtime), 
    framebuf_pool_(framebuf_num_) {
}

Capturer::~Capturer() {
}

std::unique_ptr<Capturer> Capturer::create(unsigned int yield_time, bool quiet, 
    Encoder* enc, Tflow* tfl, unsigned int device, unsigned int framerate, 
    int width, int height) {
  auto obj = std::unique_ptr<Capturer>(new Capturer(yield_time));
  obj->init(quiet, enc, tfl, device, framerate, width, height);
  return obj;
}

bool Capturer::init(bool quiet, Encoder* enc, Tflow* tfl, unsigned int device, 
    unsigned int framerate, int width, int height) { 

  quiet_ = quiet;
  enc_ = enc;
  tfl_ = tfl;
  device_ = device;
  framerate_ = framerate;

  width_flip_ = width < 0;
  height_flip_ = height < 0;
  width_ = std::abs(width);
  height_ = std::abs(height);

  fd_video_ = -1;

  frame_cnt_ = 0;
  stream_on_ = false;

#ifdef CAPTURE_ONE_RAW_FRAME
  fd_raw_ = nullptr;
#endif

 return true;
}

bool Capturer::waitingToRun() {
 
  if (!stream_on_) {

    // open video device
    dbgMsg("open video device\n");
    std::string str = "/dev/video" + std::to_string(device_);
    fd_video_ = open(str.c_str(), O_RDWR | O_NONBLOCK);
    if (fd_video_ == -1) {
      dbgMsg("failed: open video device %d\n", device_);
      return false;
    }

    int res = -1;

#ifdef OUTPUT_VARIOUS_BITS_OF_INFO
    dbgMsg("v4l2 capabilities\n");
    struct v4l2_capability cap;
    res = xioctl(fd_video_, VIDIOC_QUERYCAP, &cap);
    if (res < 0) {
      dbgMsg("failed: get capabilities\n");
      return false;
    }
    dbgMsg("  driver name: %s\n", (char*)cap.driver);
    dbgMsg("  card name: %s\n", (char*)cap.card);
    dbgMsg("  bus name: %s\n", (char*)cap.bus_info);
    dbgMsg("  kernel version: %d\n", cap.version);
    dbgMsg("  capabilities: 0x%x\n", cap.capabilities);
    dbgMsg("  device capabilities: 0x%x\n", cap.device_caps);
    dbgMsg("  capture: %s\n", (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) ? "yes" : "no");
    dbgMsg("  streaming: %s\n", (cap.capabilities & V4L2_CAP_STREAMING) ? "yes" : "no");
#endif

    pix_fmt_ = V4L2_PIX_FMT_RGB24;

    dbgMsg("v4l2 formats\n");
    struct v4l2_fmtdesc fmtdesc;
    memset(&fmtdesc, 0, sizeof(fmtdesc));
    fmtdesc.index = 0;
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    bool found = false;
    while (xioctl(fd_video_, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
      auto it = std::find_if(formats_.begin(), formats_.end(), 
          [&](unsigned int const & fmt) -> bool { return fmt == fmtdesc.pixelformat; });
      if (!found && it != formats_.end()) {
        pix_fmt_ = fmtdesc.pixelformat;
        found = true;
      }
      dbgMsg("  fmt %02d) %s, %s, %s\n", fmtdesc.index, BufTypeToStr(fmtdesc.type),
          fmtdesc.description, PixelFormatToStr(fmtdesc.pixelformat));
      fmtdesc.index++;
    }
    if (!found) {
      if (!quiet_) {
        fprintf(stderr, "  no supported pixel format found\n");
      }
      return false;
    }

#ifdef CAPTURE_ONE_RAW_FRAME
    char buf[100];
    sprintf(buf, "./frm_%dx%d_%dfps_raw.%s", width_, height_, framerate_,
        PixelFormatToStr(pix_fmt_));
    fd_raw_ = fopen(buf, "wb");
    if (fd_raw_ == nullptr) {
      dbgMsg("failed: open raw frame file\n");
      return false;
    }
#endif

    // v4l2 set input
    dbgMsg("v4l2 set input\n");
    int inp = 0;
    res = xioctl(fd_video_, VIDIOC_S_INPUT, &inp);
    if (res < 0) {
      dbgMsg("failed: set input\n");
      return false;
    }

    // v4l2 set horizontal flip
    dbgMsg("v4l2 set horizontal flip\n");
    struct v4l2_queryctrl queryctrl;
    memset(&queryctrl, 0, sizeof(queryctrl));
    queryctrl.id = V4L2_CID_HFLIP;
    res = xioctl(fd_video_, VIDIOC_QUERYCTRL, &queryctrl);
    if (res < 0) {
      dbgMsg("failed: query horizontal flip\n");
      return false;
    } else if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED) {
      dbgMsg("warning: horizontal flip not supported\n");
    } else {
      struct v4l2_control control;
      memset(&control, 0, sizeof (control));
      control.id = V4L2_CID_HFLIP;
      res = xioctl(fd_video_, VIDIOC_G_CTRL, &control);
      if (res < 0) {
        dbgMsg("failed: get horizontal flip\n");
      }
      control.value = width_flip_;
      res = xioctl(fd_video_, VIDIOC_S_CTRL, &control);
      if (res < 0) {
        dbgMsg("failed: set horizontal flip\n");
        return false;
      }
    }

    // v4l2 set vertical flip
    dbgMsg("v4l2 set vertical flip\n");
    memset(&queryctrl, 0, sizeof(queryctrl));
    queryctrl.id = V4L2_CID_VFLIP;
    res = xioctl(fd_video_, VIDIOC_QUERYCTRL, &queryctrl);
    if (res < 0) {
      dbgMsg("failed: query vertical flip\n");
      return false;
    } else if (queryctrl.flags & V4L2_CTRL_FLAG_DISABLED) {
      dbgMsg("warning: vertical flip not supported\n");
    } else {
      struct v4l2_control control;
      memset(&control, 0, sizeof (control));
      control.id = V4L2_CID_VFLIP;
      res = xioctl(fd_video_, VIDIOC_G_CTRL, &control);
      if (res < 0) {
        dbgMsg("failed: get vertical flip\n");
      }
      control.value = height_flip_;
      res = xioctl(fd_video_, VIDIOC_S_CTRL, &control);
      if (res < 0) {
        dbgMsg("failed: set vertical flip\n");
        return false;
      }
    }

    // v4l2 set stream params
    dbgMsg("v4l2 set stream params\n");
    struct v4l2_streamparm params;
    memset(&params, 0, sizeof(params));
    params.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    res = xioctl(fd_video_, VIDIOC_G_PARM, &params);
    if (res < 0) {
      dbgMsg("failed: get stream params\n");
      return false;
    }
    params.parm.capture.timeperframe.numerator = 1;
    params.parm.capture.timeperframe.denominator = framerate_;
    params.parm.capture.capturemode |= V4L2_CAP_TIMEPERFRAME;
    res = xioctl(fd_video_, VIDIOC_S_PARM, &params);
    if (res < 0) {
      dbgMsg("failed: set stream params\n");
      return false;
    }

    // set v4l2 format
    dbgMsg("set v4l2 format to %s\n", PixelFormatToStr(pix_fmt_));
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width_;
    fmt.fmt.pix.height = height_;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;
    fmt.fmt.pix.pixelformat = pix_fmt_;
    res = xioctl(fd_video_, VIDIOC_S_FMT, &fmt);
    if (res < 0) {
      dbgMsg("failed: set %s format\n", PixelFormatToStr(pix_fmt_));
      return false;
    }

    dbgMsg("get v4l2 format\n");
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    res = xioctl(fd_video_, VIDIOC_G_FMT, &fmt);
    if (res < 0) {
      dbgMsg("failed: get v4l2 format\n");
      return false;
    }
    pix_width_ = fmt.fmt.pix.width;
    pix_height_ = fmt.fmt.pix.height;
#ifdef OUTPUT_VARIOUS_BITS_OF_INFO
    dbgMsg("  format: %s\n", PixelFormatToStr(fmt.fmt.pix.pixelformat));
    dbgMsg("  width:  %d\n", fmt.fmt.pix.width);
    dbgMsg("  height: %d\n", fmt.fmt.pix.height);
#endif

    // v4l2 request buffers
    dbgMsg("v4l2 request buffers\n");
    struct v4l2_requestbuffers rb;
    memset(&rb, 0, sizeof(rb));
    rb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    rb.memory = V4L2_MEMORY_MMAP;
    rb.count = framebuf_num_;
    res = xioctl(fd_video_, VIDIOC_REQBUFS, &rb);
    if (res < 0) {
      dbgMsg("  failed: request buffers (errno: %d)", errno);
      return false;
    }
    dbgMsg("  buffer count: %d\n", rb.count);
    for (unsigned int i = 0; i < framebuf_num_; i++) {
      struct v4l2_buffer buf;
      memset(&buf, 0, sizeof(buf));
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory = V4L2_MEMORY_MMAP;
      buf.index = i;
      res = xioctl(fd_video_, VIDIOC_QUERYBUF, &buf);
      if (res < 0) {
        dbgMsg("  failed: query buffer %d (errno: %d)\n", i, errno);
        return false;
      }
      framebuf_pool_[i].addr = (unsigned char*)mmap(nullptr, 
            buf.length, PROT_READ|PROT_WRITE, MAP_SHARED, fd_video_, buf.m.offset);
      if (framebuf_pool_[i].addr == MAP_FAILED) {
        dbgMsg("  failed: make buffer %d (error: %d)\n", i, errno);
        framebuf_pool_[i].addr = 0;
        return false;
      }
      framebuf_pool_[i].length = buf.length;
    }
    for (unsigned int i = 0; i < framebuf_num_; i++) {
      struct v4l2_buffer buf;
      memset(&buf, 0, sizeof(buf));
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory = V4L2_MEMORY_MMAP;
      buf.index = i;
      res = xioctl(fd_video_, VIDIOC_QBUF, &buf);
      if (res < 0) {
        dbgMsg("  failed: queue buffer %d\n", i);
        return false;
      }
      dbgMsg("  buffer %d queued.  size: %zu\n", i, buf.length);
    }

    // v4l2 stream on
    dbgMsg("v4l2 stream on\n");
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    res = xioctl(fd_video_, VIDIOC_STREAMON, &type);
    if (res < 0) {
      dbgMsg("failed: stream on (errno: %d)\n", errno);
      return false;
    }

    differ_tot_.begin();
    stream_on_ = true;
  }

  return true;
}

#ifdef CAPTURE_ONE_RAW_FRAME
void Capturer::captureFrame(FILE* fd, int fmt, unsigned int len, unsigned char* buf) {

#ifdef OUTPUT_VARIOUS_BITS_OF_INFO
  dbgMsg("  writing frame - fmt:%s len:%d\n", PixelFormatToStr(fmt), len);
#endif

  fwrite(buf, 1, len, fd);
}
#endif

bool Capturer::running() {

  if (stream_on_) {
    fd_set fd_Set;
    struct timeval timeout;

    FD_ZERO(&fd_Set);
    FD_SET(fd_video_, &fd_Set);
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;

    int res = select(fd_video_+1, &fd_Set, NULL, NULL, &timeout);

    if (res < 0 && errno != EINTR) {
      dbgMsg("select failed\n");
    } else if (res == 0) {
      dbgMsg("select timed out\n");
    } else if (FD_ISSET(fd_video_, &fd_Set)) {

      // dequeue buffer
      struct v4l2_buffer buf;
      memset(&buf, 0, sizeof(struct v4l2_buffer));
      buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buf.memory = V4L2_MEMORY_MMAP;
      int res = xioctl(fd_video_, VIDIOC_DQBUF, &buf);
      if (res < 0) {
        dbgMsg("failed: dequeue (errno: %d)\n", errno);
        return false;
      }

      framebuf_pool_[buf.index].id = frame_cnt_++;

#ifdef CAPTURE_ONE_RAW_FRAME
      // write frames
      if (frame_cnt_ == capture_cnt_) {
        captureFrame(fd_raw_, pix_fmt_, framebuf_pool_[buf.index].length, 
            framebuf_pool_[buf.index].addr);
      }
#endif
      // send frame to tflow
      if (tfl_) {
        differ_tfl_.begin();
        if (!tfl_->addMessage(framebuf_pool_[buf.index])) {
//          dbgMsg("warning: tflow is busy\n");
        }
        differ_tfl_.end();
      }

      // send frame to encoder
      if (enc_) {
        differ_enc_.begin();
        if (!enc_->addMessage(framebuf_pool_[buf.index])) {
//          dbgMsg("warning: encoder is busy\n");
        }
        differ_enc_.end();
      }

      // enqueue buffer
      res = xioctl(fd_video_, VIDIOC_QBUF, &buf);
      if (res < 0) {
        dbgMsg("failed: enqueue (errno: %d)\n", errno);
        return false;
      }
    }
  }
  return true;
}

bool Capturer::paused() {
  return true;
}

bool Capturer::waitingToHalt() {

  if (stream_on_) {

    stream_on_ = false;
    differ_tot_.end();

    // v4l2 stream off
    dbgMsg("v4l2 stream off\n");
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    int res = xioctl(fd_video_, VIDIOC_STREAMOFF, &type);
    if (res < 0) {
      dbgMsg("failed: stream off (errno: %d)", errno);
    }

    // return v4l2 buffers
    dbgMsg("return v4l2 buffers\n");
    for (unsigned int i = 0; i < framebuf_num_; i++) {
      if (framebuf_pool_[i].addr != 0) {
        int res = munmap(framebuf_pool_[i].addr, framebuf_pool_[i].length);
        if (res < 0) {
          dbgMsg("failed: unmap buffer: %d (errno: %d)", i, errno);
        }  
        framebuf_pool_[i].addr = 0;
      }
    }

    // close video device
    dbgMsg("close video device\n");
    if (fd_video_ != -1) {
      close(fd_video_);
    }

#ifdef CAPTURE_ONE_RAW_FRAME
    if (fd_raw_) {
      fclose(fd_raw_);
    }
#endif

    // report
    if (!quiet_) {
      fprintf(stderr, "\n\nCapturer Results...\n");
      fprintf(stderr, "   number of frames captured: %d\n", frame_cnt_); 
      fprintf(stderr, "   tflow copy time (us): high:%u avg:%u low:%u frames:%d\n", 
          differ_tfl_.getHigh_usec(), differ_tfl_.getAvg_usec(), 
          differ_tfl_.getLow_usec(),  differ_tfl_.getCnt());
      fprintf(stderr, "  encode copy time (us): high:%u avg:%u low:%u frames:%d\n", 
          differ_enc_.getHigh_usec(), differ_enc_.getAvg_usec(), 
          differ_enc_.getLow_usec(),  differ_enc_.getCnt());
      fprintf(stderr, "        total test time: %f sec\n", 
          differ_tot_.getAvg_usec() / 1000000.f);
      fprintf(stderr, "      frames per second: %f fps\n", 
          differ_enc_.getCnt() * 1000000.f / differ_tot_.getAvg_usec());
      fprintf(stderr, "\n");
    }
  }

  return true;
}

int Capturer::xioctl(int fd, int request, void* arg) {
  int res;
  do {
    res = ioctl(fd, request, arg);
  } while (res == -1 && errno == EINTR);
  return res;
}

} // namespace detector

