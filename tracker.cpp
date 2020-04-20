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

#include "tracker.h"

namespace detector {

Tracker::Tracker(unsigned int yield_time) 
  : Base(yield_time) {
}

Tracker::~Tracker() {
}

std::unique_ptr<Tracker> Tracker::create(
    unsigned int yield_time, 
    bool quiet, 
    Encoder* enc
) {
  auto obj = std::unique_ptr<Tracker>(new Tracker(yield_time));
  obj->init(quiet, enc);
  return obj;
}

bool Tracker::init(bool quiet, Encoder* enc) {

  quiet_ = quiet;
  enc_ = enc;

  tracker_on_ = false;
  
  return true; 
}

bool Tracker::addMessage(std::shared_ptr<std::vector<BoxBuf>>& boxes) {

  std::unique_lock<std::timed_mutex> lck(targets_lock_, std::defer_lock);

  if (!lck.try_lock_for(
        std::chrono::microseconds(
          Listener<std::shared_ptr<std::vector<BoxBuf>>>::timeout_))) {
    dbgMsg("encoder target lock busy\n");
    return false;
  }

  targets_ = boxes;

  return true;
}

bool Tracker::waitingToRun() {

  if (!tracker_on_) {

    differ_tot_.begin();
    tracker_on_ = true;
  }

  return true;
}

bool Tracker::associateTracks() {
  return true;
}

bool Tracker::createNewTracks() {
  return true;
}

bool Tracker::running() {

  if (tracker_on_) {
    std::unique_lock<std::timed_mutex> lck(targets_lock_);

    if (targets_ != nullptr) {
      if (targets_->size() != 0) {

        associateTracks();

        createNewTracks();

      }
    }
  }
  return true;
}

bool Tracker::paused() {
  return true;
}

bool Tracker::waitingToHalt() {

  if (tracker_on_) {
    differ_tot_.end();
    tracker_on_ = false;

    if (!quiet_) {
      fprintf(stderr, "\nTracker Results...\n");
      fprintf(stderr, "       total test time: %f sec\n", 
          differ_tot_.avg / 1000000.f);
      fprintf(stderr, "\n");
    }

  }
  return true;
}

} // namespace detector

