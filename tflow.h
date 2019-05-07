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

#ifndef TFLOW_H
#define TFLOW_H

#include <string>
#include <queue>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <future>

#include "utils.h"
#include "base.h"
#include "encoder.h"

#include <tensorflow/lite/builtin_op_data.h>
#include <tensorflow/lite/interpreter.h>
#include <tensorflow/lite/kernels/register.h>
#include <tensorflow/lite/kernels/internal/tensor_ctypes.h>
#include <tensorflow/lite/model.h>

namespace tracker {

template <class T>
void resize(T* out, uint8_t* in, 
    int image_height, int image_width, int image_channels, 
    int wanted_height, int wanted_width, int wanted_channels, 
    int threads, bool is_float, float input_mean, float input_std) {

  int number_of_pixels = image_height * image_width * image_channels;
  std::unique_ptr<tflite::Interpreter> interpreter(new tflite::Interpreter);

  interpreter->UseNNAPI(false);
  interpreter->SetNumThreads(threads);
  int base_index = 0;

  // two inputs: input and new_sizes
  interpreter->AddTensors(2, &base_index);
  // one output
  interpreter->AddTensors(1, &base_index);
  // set input and output tensors
  interpreter->SetInputs({0, 1});
  interpreter->SetOutputs({2});

  // set parameters of tensors
  TfLiteQuantizationParams quant;
  interpreter->SetTensorParametersReadWrite(
      0, kTfLiteFloat32, "input",
      {1, image_height, image_width, image_channels}, 
      quant);
  interpreter->SetTensorParametersReadWrite(
      1, kTfLiteInt32, "new_size", 
      {2}, 
      quant);
  interpreter->SetTensorParametersReadWrite(
      2, kTfLiteFloat32, "output",
      {1, wanted_height, wanted_width, wanted_channels}, 
      quant);

  tflite::ops::builtin::BuiltinOpResolver resolver;
  const TfLiteRegistration* resize_op =
      resolver.FindOp(tflite::BuiltinOperator_RESIZE_BILINEAR, 1);
  auto* params = reinterpret_cast<TfLiteResizeBilinearParams*>(
      malloc(sizeof(TfLiteResizeBilinearParams)));
  params->align_corners = false;
  interpreter->AddNodeWithParameters(
      {0, 1}, {2}, nullptr, 0, params, resize_op, nullptr);

  interpreter->AllocateTensors();

  // fill input image
  // in[] are integers, cannot do memcpy() directly
  auto input = interpreter->typed_tensor<float>(0);
  for (int i = 0; i < number_of_pixels; i++) {
    input[i] = in[i];
  }

  // fill new_sizes
  interpreter->typed_tensor<int>(1)[0] = wanted_height;
  interpreter->typed_tensor<int>(1)[1] = wanted_width;

  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  interpreter->Invoke();

  auto output = interpreter->typed_tensor<float>(2);
  auto output_number_of_pixels = wanted_height * wanted_width * wanted_channels;

  for (int i = 0; i < output_number_of_pixels; i++) {
    if (is_float) {
      out[i] = (output[i] - input_mean) / input_std;
    } else {
      out[i] = (uint8_t)output[i];
    }
  }
}

class Tflow : public Base, Base::Listener {
  public:
    static std::unique_ptr<Tflow> create(unsigned int yield_time, bool quiet, 
        Encoder* enc, unsigned int width, unsigned int height, const char* model,
        const char* labels, unsigned int threads, float threshold);
    virtual ~Tflow();

  public:
    virtual bool addMessage(Base::Listener::Message msg, void* data);

  protected:
    Tflow() = delete;
    Tflow(unsigned int yield_time);
    bool init(bool quiet, Encoder* enc, unsigned int width, 
        unsigned int height, const char* model, const char* labels, 
        unsigned int threads, float threshold);

  protected:
    virtual bool waitingToRun();
    virtual bool running();
    virtual bool paused();
    virtual bool waitingToHalt();

  private:
    bool quiet_;
    Encoder* enc_;
    unsigned int width_;
    unsigned int height_;
    const unsigned int channels_ = {3};
    float threshold_;

    std::string model_fname_;
    unsigned int model_threads_;

    std::string labels_fname_;
    std::vector<std::string> labels_;
    std::vector<std::pair<unsigned int,Base::Listener::BoxBuf::Type>> labels_pairs_;
    bool addLabel(const char* label, Base::Listener::BoxBuf::Type type);

    class Frame {
      public:
        Frame() {}
        ~Frame() {}
      public:
        unsigned int id;
        unsigned int length;
        std::vector<unsigned char> buf;
    };
    unsigned int frame_len_;
    Tflow::Frame frame_;

    std::unique_ptr<tflite::FlatBufferModel> model_;
    std::unique_ptr<tflite::Interpreter> interpreter_;

    Differ differ_copy_;
    Differ differ_prep_;
    Differ differ_eval_;
    Differ differ_post_;

    unsigned int post_id_ = {0};
    const unsigned int result_num_ = {10};
    bool prep();
    bool eval();
    bool post(bool report);
    const char* boxBufTypeStr(Base::Listener::BoxBuf::Type t);
    bool oneRun(bool report);

    std::timed_mutex tflow_lock_;
    std::atomic<bool> tflow_on_;
    std::atomic<bool> tflow_empty_;

#ifdef CAPTURE_ONE_RAW_FRAME
    unsigned int counter = {10};
#endif
};

} // namespace tracker

#endif // TFLOW_H
