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

#include "base.h"

namespace detector {

Base::Base(unsigned int yield_time)
  : yield_time_(yield_time),
    state_(Base::State::kStopped) {
}

Base::~Base() {
  stop();
}

Base::State Base::getState() {
  std::unique_lock<std::mutex> lck(lock_);
  return state_;
}

void Base::setState(State s) {
  std::unique_lock<std::mutex> lck(lock_);
  state_ = s;
}

unsigned int Base::getPriority() {
  return priority_;
}

bool Base::setPriority(int priority) {
  sched_param sch_params;
  sch_params.sched_priority = priority;
  priority_ = priority;
  if(pthread_setschedparam(thread_.native_handle(), SCHED_RR, &sch_params)) {
    dbgMsg("failed to set thread scheduling\n");
    return false;
  }
  return true;
}

std::string Base::getName() {
  return name_;
}

bool Base::setName(const char* name) {
  if (name) {
    std::string str(name, max_name_len_);
    name_ = str;
    int err = pthread_setname_np(thread_.native_handle(), str.c_str());
    return err != 0;
  }
  return true;
}

void Base::wait(State s, int usec) {
  while (getState() != s) {
    std::this_thread::sleep_for(std::chrono::microseconds(usec));
  }
}

bool Base::start(const char* name, int priority) {
  {
    std::unique_lock<std::mutex> lck(lock_);
    if (state_ != Base::State::kStopped) {
      return false;
    }
    state_ = Base::State::kWaitingToPause;
  }

  thread_ = std::thread(Base::wrapper0, this);
  setPriority(priority);
  setName(name);

  wait(Base::State::kPaused, 10);
  return true;
}

bool Base::run() {
  {
    std::unique_lock<std::mutex> lck(lock_);
    if (state_ == Base::State::kRunning) {
      return true;
    }

    if (state_ != Base::State::kPaused) {
      return false;
    }
    state_ = Base::State::kWaitingToRun;
  }

  wait(Base::State::kRunning, 10);
  return true;
}

bool Base::pause() {
  {
    std::unique_lock<std::mutex> lck(lock_);
    if (state_ == Base::State::kPaused) {
      return true;
    }

    if (state_ != Base::State::kRunning) {
      return false;
    }
    state_ = Base::State::kWaitingToPause;
  }

  wait(Base::State::kPaused, 10);

  return true;
}

bool Base::stop() {
  {
    std::unique_lock<std::mutex> lck(lock_);
    if (state_ == Base::State::kStopped) {
      return true;
    }

    state_ = Base::State::kWaitingToStop;
  }

  wait(Base::State::kStopped, 10);
  thread_.join();

  return true;
}

void Base::wrapper() { 

  while (1) {
    {
      std::unique_lock<std::mutex> lck(lock_);
      if (state_ == Base::State::kWaitingToRun) {

        if (!waitingToRun()) { return; }
        state_ = Base::State::kRunning;

      } else if (state_ == Base::State::kRunning) {

        if (!running()) { return; }

      } else if (state_ == Base::State::kWaitingToPause) {

        if (!waitingToHalt()) { return; }
        state_ = Base::State::kPaused;

      } else if (state_ == Base::State::kPaused) {

        if (!paused()) { return; }

      } else if (state_ == Base::State::kWaitingToStop) {

        if (!waitingToHalt()) { return; }
        state_ = Base::State::kStopped;

      } else if (state_ == Base::State::kStopped) {

        break;

      }
    }

    std::this_thread::sleep_for(std::chrono::microseconds(yield_time_));
  }
}

void Base::wrapper0(Base* self) { 
  self->wrapper();
}

} // namespace detector

