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
#include <iostream>
#include <fstream>

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
    const char* model, const char* labels, unsigned int threads) {
  auto obj = std::unique_ptr<Tflow>(new Tflow(yield_time));
  obj->init(quiet, enc, width, height, model, labels, threads);
  return obj;
}

bool Tflow::init(bool quiet, Encoder* enc, unsigned int width, 
    unsigned int height, const char* model, const char* labels, 
    unsigned int threads) {

  quiet_ = quiet;

  enc_ = enc;
  
  width_ = width;
  height_ = height;

  frame_len_ = ALIGN_16B(width_) * ALIGN_16B(height_) * channels_;
  frame_.buf.resize(frame_len_);

  model_fname_ = model;
  labels_fname_ = labels;
  model_threads_ = threads;

  tflow_on_ = false;

  return true; 
}

bool Tflow::addMessage(Base::Listener::Message msg, void* data) {

  if (msg != Base::Listener::Message::kFrameBuf) {
    dbgMsg("tflow message not recognized\n");
    return false;
  }

  std::unique_lock<std::timed_mutex> lck(tflow_lock_, std::defer_lock);

  if (!lck.try_lock_for(std::chrono::microseconds(Base::Listener::timeout_))) {
//    dbgMsg("tflow busy\n");
    return false;
  }

  if (tflow_empty_) {
    auto buf = static_cast<Base::Listener::FrameBuf*>(data);
    if (frame_len_ != buf->length) {
      dbgMsg("tflow buffer size mismatch\n");
      return false;
    }
    differ_copy_.begin();
    frame_.id = buf->id;
    frame_.length = buf->length;
    std::memcpy(frame_.buf.data(), buf->addr, buf->length);
    tflow_empty_ = false;
    differ_copy_.end();
  }

  return true;
}

bool Tflow::addLabel(std::vector<std::pair<unsigned int,Base::Listener::BoxBuf::Type>>& labels,
    std::vector<std::string>& strs, const char* label, Base::Listener::BoxBuf::Type type) {

  auto it = std::find_if(strs.begin(), strs.end(), 
      [&](const std::string& str) 
      { return str.compare(label) == 0; });

  if (it != strs.end()) {
    labels.emplace_back(
        std::pair<unsigned int,Base::Listener::BoxBuf::Type>(it - strs.begin() + 1, type));
  }
  return true;
}

bool Tflow::waitingToRun() {

  if (!tflow_on_) {

    // make model and interpreter
    dbgMsg("make model and interpreter\n");
    model_ = tflite::FlatBufferModel::BuildFromFile(model_fname_.c_str());
    tflite::ops::builtin::BuiltinOpResolver resolver;
    tflite::InterpreterBuilder builder(*model_, resolver);
    builder(&interpreter_);
    interpreter_->UseNNAPI(false);
    interpreter_->SetNumThreads(model_threads_);

    // read labels file
    dbgMsg("read labels file\n");
    std::ifstream ifs(labels_fname_.c_str(), std::ifstream::in);
    if (!ifs) {
      dbgMsg("could not open labels file\n");
    }
    std::vector<std::string> strs;
    std::string line;
    while (std::getline(ifs, line)) {
      strs.emplace_back(line);
    }
    addLabel(labels_, strs, "person",     Base::Listener::BoxBuf::Type::kPerson);
    addLabel(labels_, strs, "cat",        Base::Listener::BoxBuf::Type::kPet);
    addLabel(labels_, strs, "dog",        Base::Listener::BoxBuf::Type::kPet);
    addLabel(labels_, strs, "car",        Base::Listener::BoxBuf::Type::kVehicle);
    addLabel(labels_, strs, "bus",        Base::Listener::BoxBuf::Type::kVehicle);
    addLabel(labels_, strs, "truck",      Base::Listener::BoxBuf::Type::kVehicle);
    addLabel(labels_, strs, "bicycle",    Base::Listener::BoxBuf::Type::kVehicle);
    addLabel(labels_, strs, "motorcycle", Base::Listener::BoxBuf::Type::kVehicle);

#ifdef DEBUG_MESSAGES
    std::for_each(labels_.begin(), labels_.end(),
        [&](const std::pair<unsigned int,Base::Listener::BoxBuf::Type>& pr) {
          dbgMsg("label pair: %d = %d\n", pr.first, (int)pr.second);
        });
#endif

    tflow_on_ = true;
  }

  return true;
}

bool Tflow::prep() {

  differ_prep_.begin();
  int input = interpreter_->inputs()[0];
  const std::vector<int> inputs = interpreter_->inputs();
  const std::vector<int> outputs = interpreter_->outputs();

  if (interpreter_->AllocateTensors() != kTfLiteOk) {
    dbgMsg("allocatetensors failed\n");
    return false;
  }

  TfLiteIntArray* dims = interpreter_->tensor(input)->dims;
  int wanted_height = dims->data[1];
  int wanted_width = dims->data[2];
  int wanted_channels = dims->data[3];

  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  switch (interpreter_->tensor(input)->type) {
    case kTfLiteFloat32:
      resize<float>(interpreter_->typed_tensor<float>(input),
          frame_.buf.data(), height_, width_, channels_,
          wanted_height, wanted_width, wanted_channels, model_threads_,
          true, 127.5f, 127.5f);
      break;
    case kTfLiteUInt8:
      resize<uint8_t>(interpreter_->typed_tensor<uint8_t>(input),
          frame_.buf.data(), height_, width_, channels_,
          wanted_height, wanted_width, wanted_channels, model_threads_,
          false, 0, 0);
      break;
    default:
      dbgMsg("unrecognized output\n");
      break;
  }
  differ_prep_.end();

  return true;
}

bool Tflow::eval() {
  differ_eval_.begin();
  interpreter_->Invoke();
  differ_eval_.end();
  return true;
}

bool Tflow::post(bool report) {

  differ_post_.begin();
  
  auto boxes = std::shared_ptr<std::vector<Base::Listener::BoxBuf>>(
        new std::vector<Base::Listener::BoxBuf>);

  const std::vector<int>& res = interpreter_->outputs();
  float* locs = tflite::GetTensorData<float>(interpreter_->tensor(res[0]));
  float* clas = tflite::GetTensorData<float>(interpreter_->tensor(res[1]));
  float* scor = tflite::GetTensorData<float>(interpreter_->tensor(res[2]));
  for (unsigned int i = 0; i < result_num_; i++) {
    unsigned int cls = static_cast<unsigned int>(clas[i]);
    if (cls >= 1 && cls <= 91) {
      if (scor[i] >= 0.f && scor[i] <= 1.f) {
        float t = fmin(fmax(locs[i+0], 0.f), 1.f);
        float l = fmin(fmax(locs[i+1], 0.f), 1.f);
        float b = fmin(fmax(locs[i+2], 0.f), 1.f);
        float r = fmin(fmax(locs[i+3], 0.f), 1.f);
        if (t < b) {
          if (l < r) {

            auto it = std::find_if(labels_.begin(), labels_.end(),
                [&](const std::pair<unsigned int,Base::Listener::BoxBuf::Type>& pr) {
                  return pr.first == cls;
                });

            auto btype = Base::Listener::BoxBuf::Type::kUnknown;
            if (it != labels_.end()) {
              btype = (*it).second;
            }

#if DEBUG_MESSAGES
            dbgMsg("t:%f,l:%f,b:%f,r:%f, scor:%f, class:%d (boxbuf type:%s)\n",
                t, l, b, r, scor[i], cls, boxBufTypeStr(btype));
#else
            if (report && !quiet_) {
              fprintf(stderr, "<%s>", boxBufTypeStr(btype));
              fflush(stderr);
            }
#endif
            unsigned int top    = t * height_;
            unsigned int bottom = b * height_;
            unsigned int left   = l * width_;
            unsigned int right  = r * width_;
            unsigned int width  = right - left;
            unsigned int height = bottom - top;
            boxes->push_back(Base::Listener::BoxBuf(
                btype, frame_.id, left, top, width, height));
          }
        }
      }
    }
    locs += 4;
    clas += 1;
    scor += 1;
  }

  // send boxes if new
  if (enc_) {
    if (post_id_ <= frame_.id) {
      if (!enc_->addMessage(Base::Listener::Message::kBoxBuf, &boxes)) {
        dbgMsg("xnor target encoder busy\n");
      }
      post_id_ = frame_.id;
    }
  }
  differ_post_.end();

  return true;
}

const char* Tflow::boxBufTypeStr(Base::Listener::BoxBuf::Type t) {
  switch (t) {
    case Base::Listener::BoxBuf::Type::kUnknown: return "unknown";
    case Base::Listener::BoxBuf::Type::kPerson:  return "person";
    case Base::Listener::BoxBuf::Type::kPet:     return "pet";
    case Base::Listener::BoxBuf::Type::kVehicle: return "vehicle";
  }
  return "unknown";
}

//unsigned int counter = 10;
bool Tflow::oneRun(bool report) {
  std::unique_lock<std::timed_mutex> lck(tflow_lock_);

  if (!tflow_empty_) {

    // prepare image
    prep();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

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

    // evaluate image
    eval();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // post image
    post(report);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

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
      fprintf(stderr, "  image copy time (us): high:%u avg:%u low:%u frames:%d\n", 
          differ_copy_.getHigh_usec(), differ_copy_.getAvg_usec(), 
          differ_copy_.getLow_usec(),  differ_copy_.getCnt());
      fprintf(stderr, "  image prep time (us): high:%u avg:%u low:%u frames:%d\n", 
          differ_prep_.getHigh_usec(), differ_prep_.getAvg_usec(), 
          differ_prep_.getLow_usec(),  differ_prep_.getCnt());
      fprintf(stderr, "  image eval time (us): high:%u avg:%u low:%u frames:%d\n", 
          differ_eval_.getHigh_usec(), differ_eval_.getAvg_usec(), 
          differ_eval_.getLow_usec(),  differ_eval_.getCnt());
      fprintf(stderr, "  image post time (us): high:%u avg:%u low:%u frames:%d\n", 
          differ_post_.getHigh_usec(), differ_post_.getAvg_usec(), 
          differ_post_.getLow_usec(),  differ_post_.getCnt());
      fprintf(stderr, "\n");
    }
  }
  return true;
}

} // namespace tracker

