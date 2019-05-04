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

#include <tensorflow/lite/interpreter.h>
#include <tensorflow/lite/kernels/register.h>
#include <tensorflow/lite/model.h>

namespace tracker {

class Tflow : public Base, Base::Listener {
  public:
    static std::unique_ptr<Tflow> create(unsigned int yield_time, bool quiet, 
        Encoder* enc, unsigned int width, unsigned int height, const char* filename,
        unsigned int threads, unsigned int engines);
    virtual ~Tflow();

  public:
    virtual bool addMessage(Base::Listener::Message msg, void* data);

  protected:
    Tflow() = delete;
    Tflow(unsigned int yield_time);
    bool init(bool quiet, Encoder* enc, unsigned int width, 
        unsigned int height, const char* filename, unsigned int threads,
        unsigned int engines);

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

    std::string model_fname_;
    unsigned int model_threads_;
    std::unique_ptr<tflite::FlatBufferModel> model_;
    std::unique_ptr<tflite::Interpreter> interpreter_;

    class Frame {
      public:
        Frame() = delete;
        Frame(char c, unsigned int sz) 
          : name(c), id(0), rgb(sz),
            differ_image(), differ_eval(), 
            fut() {}
        ~Frame() {}
      public:
        char name;
        unsigned int id;
        std::vector<unsigned char> rgb;
        Differ differ_image;
        Differ differ_eval;
        std::future<bool> fut;
    };
    std::timed_mutex engine_lock_;
    unsigned int engine_num_;
    unsigned int frame_len_;
    std::queue<std::shared_ptr<Tflow::Frame>> engine_pool_;
    std::queue<std::shared_ptr<Tflow::Frame>> engine_work_;
    std::queue<std::shared_ptr<Tflow::Frame>> engine_pile_;

    unsigned int post_id_ = {0};
    const unsigned int eval_timeout_ = {1000};
    bool eval(Tflow::Frame* frame);
    static bool eval0(Tflow* self, Tflow::Frame* frame);
    Base::Listener::BoxBuf::Type targetType(const char* label);
    bool post(bool result, unsigned int id, bool report);
    bool oneRun(bool report);

    std::atomic<bool> tflow_on_;

    Differ differ_copy_;
};

} // namespace tracker

#endif // TFLOW_H
