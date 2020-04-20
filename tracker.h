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

#ifndef TRACKER_H
#define TRACKER_H

#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <map>

#include "utils.h"
#include "listener.h"
#include "base.h"
#include "encoder.h"

namespace detector {

class Tracker : public Base, Listener<std::shared_ptr<std::vector<BoxBuf>>> {
  public:
    static std::unique_ptr<Tracker> create(unsigned int yield_time, bool quiet, 
        Encoder* enc);
    virtual ~Tracker();

  public:
    virtual bool addMessage(std::shared_ptr<std::vector<BoxBuf>>& boxes);

  protected:
    Tracker() = delete;
    Tracker(unsigned int yield_time);
    bool init(bool quiet, Encoder* enc);

  protected:
    virtual bool waitingToRun();
    virtual bool running();
    virtual bool paused();
    virtual bool waitingToHalt();

  private:
    bool quiet_;
    Encoder* enc_;

    MicroDiffer<uint32_t> differ_tot_;
    std::timed_mutex targets_lock_;
    std::shared_ptr<std::vector<BoxBuf>> targets_;
    std::atomic<bool> tracker_on_;


    // todo

};

} // namespace detector

#endif // TRACKER_H
