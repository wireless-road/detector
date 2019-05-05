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

#include "tflow.h"

namespace tracker {

Tflow::Tflow(unsigned int yield_time) 
  : Base(yield_time),
    tflow_empty_(true) {
}

Tflow::~Tflow() {
}

std::unique_ptr<Tflow> Tflow::create(unsigned int yield_time, bool quiet, 
    Encoder* enc, unsigned int width, unsigned int height, 
    const char* filename, unsigned int threads) {
  auto obj = std::unique_ptr<Tflow>(new Tflow(yield_time));
  obj->init(quiet, enc, width, height, filename, threads);
  return obj;
}

bool Tflow::init(bool quiet, Encoder* enc, unsigned int width, 
    unsigned int height, const char* filename, unsigned int threads) {

  quiet_ = quiet;

  enc_ = enc;
  
  width_ = width;
  height_ = height;

  frame_len_ = ALIGN_16B(width_) * ALIGN_16B(height_) * channels_;

  model_fname_ = filename;
  model_threads_ = threads;

  tflow_on_ = false;

  return true; 
}

bool Tflow::addMessage(Base::Listener::Message msg, void* data) {

  if (msg != Base::Listener::Message::kScratchBuf) {
    dbgMsg("tflow message not recognized\n");
    return false;
  }

  std::unique_lock<std::timed_mutex> lck(tflow_lock_, std::defer_lock);

  if (!lck.try_lock_for(std::chrono::microseconds(Base::Listener::timeout_))) {
    dbgMsg("tflow busy\n");
    return false;
  }

  if (tflow_empty_) {
    auto buf = *static_cast<std::shared_ptr<Base::Listener::ScratchBuf>*>(data);
    if (frame_len_ != buf->length) {
      dbgMsg("tflow buffer size mismatch\n");
      return false;
    }
    scratch_ = buf;
    tflow_empty_ = false;
  }

  return true;
}

bool Tflow::waitingToRun() {

  if (!tflow_on_) {

    model_ = tflite::FlatBufferModel::BuildFromFile(model_fname_.c_str());
    tflite::ops::builtin::BuiltinOpResolver resolver;
    tflite::InterpreterBuilder builder(*model_, resolver);
    builder(&interpreter_);

    interpreter_->UseNNAPI(false);
    interpreter_->SetNumThreads(model_threads_);
//    interpreter_->SetNumThreads(1);

    tflow_on_ = true;
  }

  return true;
}

Base::Listener::BoxBuf::Type Tflow::targetType(const char* label)
{
  std::string str = label;
  if (str.compare("person") == 0) {
    return Base::Listener::BoxBuf::Type::kPerson;
  } else if (str.compare("pet") == 0) {
    return Base::Listener::BoxBuf::Type::kPet;
  } else {
    return Base::Listener::BoxBuf::Type::kVehicle;
  }
}

bool Tflow::post(bool result, unsigned int id, bool report) {

  if (result) {

    // todo:  find results
    // todo:  make bounding boxes
    // todo:  send boxes to encoder

  }

  return true;
}

//unsigned int counter = 10;
bool Tflow::oneRun(bool report) {
  std::unique_lock<std::timed_mutex> lck(tflow_lock_);

  if (!tflow_empty_) {
    differ_image_.begin();
    int input = interpreter_->inputs()[0];
    const std::vector<int> inputs = interpreter_->inputs();
    const std::vector<int> outputs = interpreter_->outputs();

    if (interpreter_->AllocateTensors() != kTfLiteOk) {
      dbgMsg("allocatetensors failed\n");
    }

    TfLiteIntArray* dims = interpreter_->tensor(input)->dims;
    int wanted_height = dims->data[1];
    int wanted_width = dims->data[2];
    int wanted_channels = dims->data[3];

//    dbgMsg("wanted dims:  w:%d, h:%d, chn:%d\n", wanted_width, wanted_height,
//        wanted_channels);

    switch (interpreter_->tensor(input)->type) {
      case kTfLiteFloat32:
//        dbgMsg("float output\n");
        resize<float>(interpreter_->typed_tensor<float>(input),
            scratch_->buf.data(), height_, width_, channels_,
            wanted_height, wanted_width, wanted_channels, true, 127.5f, 127.5f);
        break;
      case kTfLiteUInt8:
//        dbgMsg("uint8 output\n");
        resize<uint8_t>(interpreter_->typed_tensor<uint8_t>(input),
            scratch_->buf.data(), height_, width_, channels_,
            wanted_height, wanted_width, wanted_channels, false, 0, 0);
        break;
      default:
        dbgMsg("unrecognized output\n");
        break;
    }
    differ_image_.end();

#if 0
    if (counter != 0) {
      counter--;
      if (counter == 0) {
        FILE* fd = fopen("resize.out", "wb");
        if (fd == nullptr) {
          dbgMsg("failed: open resize frame file\n");
        }
        
        dbgMsg("***writing resized picture\n");
        fwrite(interpreter_->typed_tensor<uint8_t>(input), 1, 
            wanted_height * wanted_width * wanted_channels, fd);
        fclose(fd);
      }
    }
#endif

    differ_eval_.begin();
    interpreter_->Invoke();
    differ_eval_.end();

    scratch_ = std::shared_ptr<Base::Listener::ScratchBuf>(new Base::Listener::ScratchBuf());
    tflow_empty_ = true;
  }

  return true;
}

bool Tflow::running() {

  if (tflow_on_) {
    return oneRun(true);
  }
  return true;
}

bool Tflow::paused() {
  return true;
}

bool Tflow::waitingToHalt() {

  if (tflow_on_) {
    tflow_on_ = false;

    // finish processing
    while (!tflow_empty_) {
      oneRun(false);
    }

    // report
    if (!quiet_) {
      fprintf(stderr, "\n\nTflow Results...\n");
      fprintf(stderr, "  image prep time (us): high:%u avg:%u low:%u frames:%d\n", 
          differ_image_.getHigh_usec(), differ_image_.getAvg_usec(), 
          differ_image_.getLow_usec(), differ_image_.getCnt());
      fprintf(stderr, "  image eval time (us): high:%u avg:%u low:%u frames:%d\n", 
          differ_eval_.getHigh_usec(), differ_eval_.getAvg_usec(), 
          differ_eval_.getLow_usec(), differ_eval_.getCnt());
      fprintf(stderr, "\n");
    }
  }
  return true;
}

} // namespace tracker

