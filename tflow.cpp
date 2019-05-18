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

#include <chrono>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <fstream>

#include "tflow.h"

namespace detector {

Tflow::Tflow(unsigned int yield_time) 
  : Base(yield_time),
    tflow_empty_(true) {
}

Tflow::~Tflow() {
}

std::unique_ptr<Tflow> Tflow::create(unsigned int yield_time, bool quiet, 
    Encoder* enc, unsigned int width, unsigned int height, 
    const char* model, const char* labels, unsigned int threads, float threshold) {
  auto obj = std::unique_ptr<Tflow>(new Tflow(yield_time));
  obj->init(quiet, enc, width, height, model, labels, threads, threshold);
  return obj;
}

bool Tflow::init(bool quiet, Encoder* enc, unsigned int width, 
    unsigned int height, const char* model, const char* labels, 
    unsigned int threads, float threshold) {

  quiet_ = quiet;

  enc_ = enc;
  
  width_ = width;
  height_ = height;

  frame_len_ = ALIGN_16B(width_) * ALIGN_16B(height_) * channels_;
  frame_.buf.resize(frame_len_);

  model_fname_ = model;
  labels_fname_ = labels;
  model_threads_ = threads;
  threshold_ = threshold;

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

bool Tflow::addLabel(const char* label, Base::Listener::BoxBuf::Type type) {

  auto it = std::find_if(labels_.begin(), labels_.end(), 
      [&](const std::string& str) 
      { return str.compare(label) == 0; });

  if (it != labels_.end()) {
    labels_pairs_.emplace_back(
        std::pair<unsigned int,Base::Listener::BoxBuf::Type>(it - labels_.begin(), type));
  }
  return true;
}

bool Tflow::waitingToRun() {

  if (!tflow_on_) {

    edgetpu_context_ = 
      edgetpu::EdgeTpuManager::GetSingleton()->NewEdgeTpuContext().release();

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
    std::string line;
    while (std::getline(ifs, line)) {
      if (line.compare("???") != 0) {
        labels_.emplace_back(line);
      }
    }
    addLabel("person",     Base::Listener::BoxBuf::Type::kPerson);
    addLabel("cat",        Base::Listener::BoxBuf::Type::kPet);
    addLabel("dog",        Base::Listener::BoxBuf::Type::kPet);
    addLabel("car",        Base::Listener::BoxBuf::Type::kVehicle);
    addLabel("bus",        Base::Listener::BoxBuf::Type::kVehicle);
    addLabel("truck",      Base::Listener::BoxBuf::Type::kVehicle);
    addLabel("bicycle",    Base::Listener::BoxBuf::Type::kVehicle);
    addLabel("motorcycle", Base::Listener::BoxBuf::Type::kVehicle);

#ifdef DEBUG_MESSAGES
    std::for_each(labels_pairs_.begin(), labels_pairs_.end(),
        [&](const std::pair<unsigned int,Base::Listener::BoxBuf::Type>& pr) {
          dbgMsg("label pair: %d = %s\n", pr.first, boxBufTypeStr(pr.second));
        });
#endif

    differ_tot_.begin();
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

#ifdef CAPTURE_ONE_RAW_FRAME
    if (counter != 0) {
      counter--;
      if (counter == 0) {
        char buf[100];
        sprintf(buf, "./frm_%dx%d_resized.rgb24", wanted_width, wanted_height);
        FILE* fd = fopen(buf, "wb");
        if (fd == nullptr) {
          dbgMsg("failed: open resize frame file\n");
        }
#ifdef OUTPUT_VARIOUS_BITS_OF_INFO
        dbgMsg("  writing resized - fmt:rgb24 len:%d\n",
            wanted_height * wanted_width * wanted_channels);
#endif
        fwrite(interpreter_->typed_tensor<uint8_t>(input), 1, 
            wanted_height * wanted_width * wanted_channels, fd);
        fclose(fd);
      }
    }
#endif

  return true;
}

bool Tflow::eval() {
  differ_eval_.begin();
  if (interpreter_->Invoke() != kTfLiteOk) {
    dbgMsg("failed invoke\n");
  }
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
#if DEBUG_MESSAGES
  float* tot  = tflite::GetTensorData<float>(interpreter_->tensor(res[3]));
  dbgMsg("total results: %d\n", static_cast<unsigned int>(tot[0]));
#endif
  for (unsigned int i = 0; i < result_num_; i++, locs += 4) {

    unsigned int class_id = static_cast<unsigned int>(clas[i]);

    if (class_id < labels_.size()) {
      if (scor[i] >= threshold_ && scor[i] <= 1.f) {

        // clamp
        float top    = fmin(fmax(locs[0], 0.f), 1.f);
        float left   = fmin(fmax(locs[1], 0.f), 1.f);
        float bottom = fmin(fmax(locs[2], 0.f), 1.f);
        float right  = fmin(fmax(locs[3], 0.f), 1.f);

        if (top < bottom) {
          if (left < right) {

            auto it = std::find_if(labels_pairs_.begin(), labels_pairs_.end(),
                [&](const std::pair<unsigned int,Base::Listener::BoxBuf::Type>& pr) {
                  return pr.first == class_id;
                });

            auto btype = Base::Listener::BoxBuf::Type::kUnknown;
            if (it != labels_pairs_.end()) {
              btype = (*it).second;
            }

#if DEBUG_MESSAGES
            dbgMsg("t:%f,l:%f,b:%f,r:%f, scor:%f, class:%d (%s)\n",
                top, left, bottom, right, scor[i], class_id, labels_[class_id].c_str());
#else
            if (report && !quiet_) {
              fprintf(stderr, "<%s>", labels_[class_id].c_str());
              fflush(stderr);
            }
#endif
            unsigned int top_uint    = top    * height_;
            unsigned int bottom_uint = bottom * height_;
            unsigned int left_uint   = left   * width_;
            unsigned int right_uint  = right  * width_;

            unsigned int width_uint  = right_uint  - left_uint;
            unsigned int height_uint = bottom_uint - top_uint;

            boxes->push_back(Base::Listener::BoxBuf(
                btype, frame_.id, left_uint, top_uint, width_uint, height_uint));
          }
        }
      }
    }
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

bool Tflow::oneRun(bool report) {
  std::unique_lock<std::timed_mutex> lck(tflow_lock_);

  if (!tflow_empty_) {

    // prepare image
    prep();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

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
    differ_tot_.end();

    // finish processing
    while (!tflow_empty_) {
      oneRun(false);
    }

    // report
    if (!quiet_) {
      fprintf(stderr, "\nTflow Results...\n");
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
      fprintf(stderr, "       total test time: %f sec\n", 
          differ_tot_.getAvg_usec() / 1000000.f);
      fprintf(stderr, "     frames per second: %f fps\n", 
          differ_post_.getCnt() * 1000000.f / differ_tot_.getAvg_usec());
      fprintf(stderr, "\n");
    }
  }
  return true;
}

} // namespace detector

