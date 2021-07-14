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
#include <vector>
#include <cstring>
#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <algorithm>
#include <iterator>

#include "tflow.h"

namespace detector {

Tflow::Tflow(unsigned int yield_time)
  : Base(yield_time),
    tflow_empty_(true) {
}

Tflow::~Tflow() {
}

std::unique_ptr<Tflow> Tflow::create(unsigned int yield_time, bool quiet,
#ifndef WITHOUT_ENCODER
    Encoder* enc,
#endif
    Tracker* trk, unsigned int width, unsigned int height,
    const char* model, const char* labels, unsigned int threads, float threshold,
    bool tpu) {
  auto obj = std::unique_ptr<Tflow>(new Tflow(yield_time));
  obj->init(quiet,
#ifndef WITHOUT_ENCODER
        enc,
#endif
        trk, width, height, model, labels, threads, threshold, tpu);
  return obj;
}

bool Tflow::init(bool quiet,
#ifndef WITHOUT_ENCODER
    Encoder* enc,
#endif
    Tracker* trk, unsigned int width,
    unsigned int height, const char* model, const char* labels,
    unsigned int threads, float threshold, bool tpu) {

  quiet_ = quiet;
  tpu_ = tpu;

#ifndef WITHOUT_ENCODER
  enc_ = enc;
#endif
  trk_ = trk;

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

bool Tflow::addMessage(FrameBuf& fbuf) {

  std::unique_lock<std::timed_mutex> lck(tflow_lock_, std::defer_lock);

  if (!lck.try_lock_for(std::chrono::microseconds(Listener::timeout_))) {
//    dbgMsg("tflow busy\n");
    return false;
  }

  if (tflow_empty_) {
    if (frame_len_ != fbuf.length) {
      dbgMsg("tflow buffer size mismatch\n");
      return false;
    }
    differ_copy_.begin();
    frame_.id = fbuf.id;
    frame_.length = fbuf.length;
    std::memcpy(frame_.buf.data(), fbuf.addr, fbuf.length);
    tflow_empty_ = false;
    differ_copy_.end();
  }

  return true;
}

bool Tflow::waitingToRun() {

  if (!tflow_on_) {

    // find tpu
    dbgMsg("find tpu\n");
    const auto& available_tpus =
        edgetpu::EdgeTpuManager::GetSingleton()->EnumerateEdgeTpu();

    // make model and interpreter
    dbgMsg("make model and interpreter\n");
    if (tpu_ && available_tpus.size()) {
      model_ = tflite::FlatBufferModel::BuildFromFile(model_fname_.c_str());
      edgetpu_context_ = edgetpu::EdgeTpuManager::GetSingleton()->OpenDevice();
      tflite::ops::builtin::BuiltinOpResolver resolver;
      resolver.AddCustom(edgetpu::kCustomOp, edgetpu::RegisterCustomOp());
      tflite::InterpreterBuilder builder(*model_, resolver);
      builder(&model_interpreter_);
      model_interpreter_->SetExternalContext(kTfLiteEdgeTpuContext, edgetpu_context_.get());
      model_interpreter_->SetNumThreads(1);
    } else {
      model_ = tflite::FlatBufferModel::BuildFromFile(model_fname_.c_str());
      tflite::ops::builtin::BuiltinOpResolver resolver;
      tflite::InterpreterBuilder builder(*model_, resolver);
      builder(&model_interpreter_);
      //model_interpreter_->UseNNAPI(false);
      model_interpreter_->SetNumThreads(model_threads_);
    }
    model_interpreter_->AllocateTensors();
    int input = model_interpreter_->inputs()[0];
    const std::vector<int> inputs = model_interpreter_->inputs();
    const std::vector<int> outputs = model_interpreter_->outputs();
    TfLiteIntArray* dims = model_interpreter_->tensor(input)->dims;
    model_height_ = dims->data[1];
    model_width_ = dims->data[2];
    model_channels_ = dims->data[3];

    // make resize interpreter
    dbgMsg("make resize interpreter\n");
    resize_interpreter_ = std::make_unique<tflite::Interpreter>();
    int base_index = 0;
    resize_interpreter_->AddTensors(2, &base_index);  // two inputs: input and new_sizes
    resize_interpreter_->AddTensors(1, &base_index);  // one output
    resize_interpreter_->SetInputs({0, 1});
    resize_interpreter_->SetOutputs({2});
    TfLiteQuantizationParams quant;
    resize_interpreter_->SetTensorParametersReadWrite(
        0, kTfLiteFloat32, "input",
        {1,
          static_cast<int>(height_),
          static_cast<int>(width_),
          static_cast<int>(channels_)
        },
        quant);
    resize_interpreter_->SetTensorParametersReadWrite(
        1, kTfLiteInt32, "new_size",
        {2},
        quant);
    resize_interpreter_->SetTensorParametersReadWrite(
        2, kTfLiteFloat32, "output",
        {1,
          static_cast<int>(model_height_),
          static_cast<int>(model_width_),
          static_cast<int>(model_channels_)
        },
        quant);
    tflite::ops::builtin::BuiltinOpResolver resize_resolver;
    const TfLiteRegistration* resize_op =
        resize_resolver.FindOp(tflite::BuiltinOperator_RESIZE_BILINEAR, 1);
    auto* params = reinterpret_cast<TfLiteResizeBilinearParams*>(
        malloc(sizeof(TfLiteResizeBilinearParams)));
    params->align_corners = false;
    resize_interpreter_->AddNodeWithParameters(
        {0, 1}, {2}, nullptr, 0, params, resize_op, nullptr);
    if (tpu_ && available_tpus.size()) {
      resize_interpreter_->SetExternalContext(kTfLiteEdgeTpuContext, edgetpu_context_.get());
      resize_interpreter_->SetNumThreads(1);
    } else {
      //resize_interpreter_->UseNNAPI(false);
      resize_interpreter_->SetNumThreads(model_threads_);
    }
    resize_interpreter_->AllocateTensors();

    // read labels file
    dbgMsg("read labels file\n");
    std::ifstream ifs(labels_fname_.c_str(), std::ifstream::in);
    if (!ifs) {
      dbgMsg("could not open labels file\n");
    }
    std::string line;
    while (std::getline(ifs, line)) {
      std::istringstream iss(line);
      std::vector<std::string> tokens{
        std::istream_iterator<std::string>{iss},
        std::istream_iterator<std::string>{}
      };
      auto it = boxbuf_pairs_.find(tokens[1]);
      BoxBuf::Type btype = BoxBuf::Type::kUnknown;
      if (it != boxbuf_pairs_.end()) {
        btype = it->second;
      }
      label_pairs_[std::stoul(tokens[0])] = std::make_pair(tokens[1], btype);
    }

    differ_tot_.begin();
    tflow_on_ = true;
  }

  return true;
}

void Tflow::resize(std::unique_ptr<tflite::Interpreter>& interpreter,
    uint8_t* out, uint8_t* in,
    int image_height, int image_width, int image_channels,
    int wanted_height, int wanted_width, int wanted_channels,
    int yield) {

  int number_of_pixels = image_height * image_width * image_channels;

  // fill input image
  // in[] are integers, cannot do memcpy() directly
  auto input = interpreter->typed_tensor<float>(0);
  for (int i = 0; i < number_of_pixels; i++) {
    input[i] = in[i];
  }

  // fill new_sizes
  interpreter->typed_tensor<int>(1)[0] = wanted_height;
  interpreter->typed_tensor<int>(1)[1] = wanted_width;

//  std::this_thread::sleep_for(std::chrono::microseconds(yield));
  interpreter->Invoke();

  auto output = interpreter->typed_tensor<float>(2);
  auto output_number_of_pixels = wanted_height * wanted_width * wanted_channels;

  for (int i = 0; i < output_number_of_pixels; i++) {
    out[i] = (uint8_t)output[i];
  }
}

bool Tflow::prep() {

//  std::this_thread::sleep_for(std::chrono::microseconds(yield_time_));
  differ_prep_.begin();
  int input = model_interpreter_->inputs()[0];
  if (model_interpreter_->tensor(input)->type == kTfLiteUInt8) {
    resize(resize_interpreter_,
        model_interpreter_->typed_tensor<uint8_t>(input), frame_.buf.data(),
        height_, width_, channels_,
        model_height_, model_width_, model_channels_,
        yield_time_);
  } else {
    dbgMsg("unrecognized output\n");
  }
  differ_prep_.end();

#ifdef CAPTURE_ONE_RAW_FRAME
    if (counter != 0) {
      counter--;
      if (counter == 0) {
        char buf[100];
        sprintf(buf, "./frm_%dx%d_resized.rgb24", model_width_, model_height_);
        FILE* fd = fopen(buf, "wb");
        if (fd == nullptr) {
          dbgMsg("failed: open resize frame file\n");
        }
#ifdef OUTPUT_VARIOUS_BITS_OF_INFO
        dbgMsg("  writing resized - fmt:rgb24 len:%d\n",
            model_height_ * model_width_ * model_channels_);
#endif
        fwrite(model_interpreter_->typed_tensor<uint8_t>(input), 1,
            model_height_ * model_width_ * model_channels_, fd);
        fclose(fd);

        sprintf(buf, "./frm_%dx%d_fullsize.rgb24", width_, height_);
        fd = fopen(buf, "wb");
        if (fd == nullptr) {
          dbgMsg("failed: open full frame file\n");
        }
#ifdef OUTPUT_VARIOUS_BITS_OF_INFO
        dbgMsg("  writing fullsize - fmt:rgb24 len:%d\n",
            height_ * width_ * channels_);
#endif
        fwrite(frame_.buf.data(), 1,
            height_ * width_ * channels_, fd);
        fclose(fd);
      }
    }
#endif

  return true;
}

bool Tflow::eval() {
  differ_eval_.begin();
  if (model_interpreter_->Invoke() != kTfLiteOk) {
    dbgMsg("failed invoke\n");
  }
  differ_eval_.end();
  return true;
}

bool Tflow::post(bool report) {

  differ_post_.begin();

  auto boxes = std::make_shared<std::vector<BoxBuf>>();

  const std::vector<int>& res = model_interpreter_->outputs();
  float* locs = tflite::GetTensorData<float>(model_interpreter_->tensor(res[0]));
  float* clas = tflite::GetTensorData<float>(model_interpreter_->tensor(res[1]));
  float* scor = tflite::GetTensorData<float>(model_interpreter_->tensor(res[2]));
#if DEBUG_MESSAGES
  float* tot  = tflite::GetTensorData<float>(model_interpreter_->tensor(res[3]));
  dbgMsg("total results: %d\n", static_cast<unsigned int>(tot[0]));
#endif
  for (unsigned int i = 0; i < result_num_; i++, locs += 4) {

    unsigned int class_id = static_cast<unsigned int>(clas[i]);

    auto it = label_pairs_.find(class_id);
    if ( it != label_pairs_.end()) {
      if (scor[i] >= threshold_ && scor[i] <= 1.f) {

        // clamp
        float top    = fmin(fmax(locs[0], 0.f), 1.f);
        float left   = fmin(fmax(locs[1], 0.f), 1.f);
        float bottom = fmin(fmax(locs[2], 0.f), 1.f);
        float right  = fmin(fmax(locs[3], 0.f), 1.f);

        if (top < bottom) {
          if (left < right) {

#if DEBUG_MESSAGES
            dbgMsg("t:%f,l:%f,b:%f,r:%f, scor:%f, class:%d (%s)\n",
                top, left, bottom, right, scor[i], class_id, label_pairs_[class_id].first.c_str());
#else
            if (report && !quiet_) {
              fprintf(stderr, "<%s>", label_pairs_[class_id].first.c_str());
              fflush(stderr);
            }
#endif
            unsigned int top_uint    = round(top    * height_);
            unsigned int bottom_uint = round(bottom * height_);
            unsigned int left_uint   = round(left   * width_);
            unsigned int right_uint  = round(right  * width_);

            unsigned int width_uint  = right_uint  - left_uint;
            unsigned int height_uint = bottom_uint - top_uint;

            BoxBuf::Type btype = label_pairs_[class_id].second;
            boxes->push_back(BoxBuf(
                btype, frame_.id, left_uint, top_uint, width_uint, height_uint));
          }
        }
      }
    }
  }

#ifdef WITH_JPEG
  if (!boxes->empty()) {
     std::vector<unsigned char> withBoxes(frame_.length);
     withBoxes.resize(frame_.length);
     std::memcpy(withBoxes.data(), frame_.buf.data(), frame_.length);
     for (auto i = boxes->begin(); i != boxes->end(); ++i) {
        drawRGBBox(5, &withBoxes[0], width_, height_,
            i->x, i->y, i->w, i->h, 255, 0, 0);
     }
     char fnBuf[100];
     sprintf(fnBuf, "./frm_%d.jpg", frame_.id);
     compressor.compressToFile(width_, height_, &withBoxes[0], fnBuf);
  }
#endif

  // send boxes if new
  if (post_id_ <= frame_.id) {
#ifndef WITHOUT_ENCODER
    if (enc_) {
      if (!enc_->addMessage(boxes)) {
        dbgMsg("encoder busy\n");
      }
    }
#endif
    if (trk_) {
      if (!trk_->addMessage(boxes)) {
        dbgMsg("tracker busy\n");
      }
    }
    post_id_ = frame_.id;
  }
  differ_post_.end();

  return true;
}

bool Tflow::oneRun(bool report) {

  if (!tflow_empty_) {

    // prepare image
    prep();
    std::this_thread::sleep_for(std::chrono::microseconds(yield_time_));

    // evaluate image
    eval();
    std::this_thread::sleep_for(std::chrono::microseconds(yield_time_));

    // post image
    post(report);
    std::this_thread::sleep_for(std::chrono::microseconds(yield_time_));

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

    // reset tensorflow ojects
    model_interpreter_.reset();
    resize_interpreter_.reset();
    edgetpu_context_.reset();
    model_.reset();

    // report
    if (!quiet_) {
      fprintf(stderr, "\nTflow Results...\n");
      fprintf(stderr, "  image copy time (us): high:%u avg:%u low:%u cnt:%u\n",
          differ_copy_.high, differ_copy_.avg,
          differ_copy_.low,  differ_copy_.cnt);
      fprintf(stderr, "  image prep time (us): high:%u avg:%u low:%u cnt:%u\n",
          differ_prep_.high, differ_prep_.avg,
          differ_prep_.low,  differ_prep_.cnt);
      fprintf(stderr, "  image eval time (us): high:%u avg:%u low:%u cnt:%u\n",
          differ_eval_.high, differ_eval_.avg,
          differ_eval_.low,  differ_eval_.cnt);
      fprintf(stderr, "  image post time (us): high:%u avg:%u low:%u cnt:%u\n",
          differ_post_.high, differ_post_.avg,
          differ_post_.low,  differ_post_.cnt);
      fprintf(stderr, "       total test time: %f sec\n",
          differ_tot_.avg / 1000000.f);
      fprintf(stderr, "     frames per second: %f fps\n",
          differ_post_.cnt * 1000000.f / differ_tot_.avg);
      fprintf(stderr, "\n");
    }
  }
  return true;
}

} // namespace detector

