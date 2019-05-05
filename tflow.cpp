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
  : Base(yield_time) {
}

Tflow::~Tflow() {
}

std::unique_ptr<Tflow> Tflow::create(unsigned int yield_time, bool quiet, 
    Encoder* enc, unsigned int width, unsigned int height, 
    const char* filename, unsigned int threads, unsigned int engines) {
  auto obj = std::unique_ptr<Tflow>(new Tflow(yield_time));
  obj->init(quiet, enc, width, height, filename, threads, engines);
  return obj;
}

bool Tflow::init(bool quiet, Encoder* enc, unsigned int width, 
    unsigned int height, const char* filename, unsigned int threads, 
    unsigned int engines) {

  quiet_ = quiet;

  enc_ = enc;
  
  width_ = width;
  height_ = height;

  frame_len_ = ALIGN_16B(width_) * ALIGN_16B(height_) * channels_;

  model_fname_ = filename;
  model_threads_ = threads;

  engine_num_ = engines;

  tflow_on_ = false;

  return true; 
}

bool Tflow::addMessage(Base::Listener::Message msg, void* data) {

  if (msg != Base::Listener::Message::kScratchBuf) {
    dbgMsg("tflow message not recognized\n");
    return false;
  }

  std::unique_lock<std::timed_mutex> lck(engine_lock_, std::defer_lock);

  if (!lck.try_lock_for(std::chrono::microseconds(Base::Listener::timeout_))) {
    dbgMsg("tflow busy\n");
    return false;
  }

  if (engine_pool_.size() == 0) {
//    dbgMsg("no tflow buffers available\n");
    return false;
  }

  auto scratch = *static_cast<std::shared_ptr<Base::Listener::ScratchBuf>*>(data);
  if (frame_len_ != scratch->length) {
    dbgMsg("tflow buffer size mismatch\n");
    return false;
  }

  auto frame = engine_pool_.front();
  engine_pool_.pop();
  frame->scratch = scratch;
  engine_work_.push(frame);

  return true;
}

bool Tflow::waitingToRun() {

  if (!tflow_on_) {

    for (unsigned int i = 0; i < engine_num_; i++) {
      auto model = tflite::FlatBufferModel::BuildFromFile(model_fname_.c_str());
      tflite::ops::builtin::BuiltinOpResolver resolver;
      std::unique_ptr<tflite::Interpreter> interpreter;
      tflite::InterpreterBuilder builder(*model, resolver);
      builder(&interpreter);

      interpreter->UseNNAPI(false);
//      interpreter->SetNumThreads(model_threads_);
      interpreter->SetNumThreads(1);

      engine_pool_.push(std::shared_ptr<Tflow::Frame>(
            new Tflow::Frame( 'A' + i, model, interpreter)));
    }

    tflow_on_ = true;
  }

  return true;
}

//unsigned int counter = 10;
bool Tflow::eval(Tflow::Frame* frame) {

  frame->differ_image.begin();
  int input = frame->interpreter->inputs()[0];
  const std::vector<int> inputs = frame->interpreter->inputs();
  const std::vector<int> outputs = frame->interpreter->outputs();

  if (frame->interpreter->AllocateTensors() != kTfLiteOk) {
    dbgMsg("allocatetensors failed\n");
  }

  TfLiteIntArray* dims = frame->interpreter->tensor(input)->dims;
  int wanted_height = dims->data[1];
  int wanted_width = dims->data[2];
  int wanted_channels = dims->data[3];

//  dbgMsg("wanted dims:  w:%d, h:%d, chn:%d\n", wanted_width, wanted_height,
//      wanted_channels);

  switch (frame->interpreter->tensor(input)->type) {
    case kTfLiteFloat32:
//      dbgMsg("float output\n");
      resize<float>(frame->interpreter->typed_tensor<float>(input),
          frame->scratch->buf.data(), height_, width_, channels_,
          wanted_height, wanted_width, wanted_channels, true, 127.5f, 127.5f);
      break;
    case kTfLiteUInt8:
//      dbgMsg("uint8 output\n");
      resize<uint8_t>(frame->interpreter->typed_tensor<uint8_t>(input),
          frame->scratch->buf.data(), height_, width_, channels_,
          wanted_height, wanted_width, wanted_channels, false, 0, 0);
      break;
    default:
      dbgMsg("unrecognized output\n");
      break;
  }
  frame->differ_image.end();

#if 0
  if (counter != 0) {
    counter--;
    if (counter == 0) {
      FILE* fd = fopen("resize.out", "wb");
      if (fd == nullptr) {
        dbgMsg("failed: open resize frame file\n");
      }
      
      dbgMsg("***writing resized picture\n");
      fwrite(frame->interpreter->typed_tensor<uint8_t>(input), 1, 
          wanted_height * wanted_width * wanted_channels, fd);
      fclose(fd);
    }
  }
#endif

  frame->differ_eval.begin();
  frame->interpreter->Invoke();
  frame->differ_eval.end();
  return true;
}

bool Tflow::eval0(Tflow* self, Tflow::Frame* frame) {
  return self->eval(frame);
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

bool Tflow::oneRun(bool report) {
  std::unique_lock<std::timed_mutex> lck(engine_lock_);

  // check for new data
  if (engine_work_.size() != 0) {
    auto frame = engine_work_.front();
    engine_work_.pop();

    engine_pile_.push(frame);
    frame->fut = std::async(std::launch::async, Tflow::eval0, this, frame.get());

  // check for data in process
  } else if (engine_pile_.size() != 0) {

    auto frame = engine_pile_.front();

    if (frame->fut.wait_for(
          std::chrono::microseconds(eval_timeout_)) == std::future_status::ready) {
      engine_pile_.pop();

      bool result = frame->fut.get();

      if (result) {
        post(result, frame->scratch->id, report);
      }
      frame->scratch = std::shared_ptr<Base::Listener::ScratchBuf>(new Base::Listener::ScratchBuf());
      engine_pool_.push(frame);
    }
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
    while (engine_work_.size() || engine_pile_.size()) {
      oneRun(false);
    }

    // report
    if (!quiet_) {
      fprintf(stderr, "\n\nTflow Results...\n");
      std::vector<std::shared_ptr<Tflow::Frame>> vec;
      while (engine_pool_.size()) {
        auto frame = engine_pool_.front();
        engine_pool_.pop();
        vec.push_back(frame);
      }
      std::sort(vec.begin(), vec.end(), 
          [](const std::shared_ptr<Tflow::Frame> & a, 
            const std::shared_ptr<Tflow::Frame> & b) -> bool { return a->name < b->name; });
      for (auto& frame : vec) {
        fprintf(stderr, "  buffer %c image prep time (us): high:%u avg:%u low:%u frames:%d\n", 
            frame->name, frame->differ_image.getHigh_usec(), frame->differ_image.getAvg_usec(), 
            frame->differ_image.getLow_usec(), frame->differ_image.getCnt());
        fprintf(stderr, "  buffer %c image eval time (us): high:%u avg:%u low:%u frames:%d\n", 
            frame->name, frame->differ_eval.getHigh_usec(), frame->differ_eval.getAvg_usec(), 
            frame->differ_eval.getLow_usec(), frame->differ_eval.getCnt());
        fprintf(stderr, "\n");
      }
    }
  }
  return true;
}

} // namespace tracker

