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
 * max_distributed under the License is max_distributed on an "AS IS" BASIS,
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

#include "Eigen/Dense"

namespace detector {

class Tracker : public Base, Listener<std::shared_ptr<std::vector<BoxBuf>>> {
  
  public:
    class Track {
      public:
        enum class State {
          kUnknown = 0,
          kInit,
          kActive
        };

      public: 
        Track() = delete;
        Track(unsigned int track_id, const BoxBuf& box);
        ~Track() {}

      public:
        double getDistance(double mid_x, double mid_y);
        void addTarget(const BoxBuf& box);

      private:
        unsigned int id_;
        unsigned int frm_;
        BoxBuf::Type type_;
        double x_, y_, w_, h_;

        Track::State state_{Track::State::kInit};

        const double initial_error_{1.0};
        const double process_variance_{1.0};
        const double measure_variance_{1.0};

        const Eigen::Matrix<double, 6, 6> A_{
          { 1, 0, 1, 0, 0, 0 },
          { 0, 1, 0, 1, 0, 0 },
          { 0, 0, 1, 0, 1, 0 },
          { 0, 0, 0, 1, 0, 1 },
          { 0, 0, 0, 0, 0, 0 },
          { 0, 0, 0, 0, 0, 0 }
        };
        const Eigen::Matrix<double, 2, 6> H_{
          { 1, 0, 0, 0, 0, 0 },
          { 0, 1, 0, 0, 0, 0 }
        };

        Eigen::Matrix<double, 6, 1> X_;
        Eigen::Matrix<double, 6, 6> P_;
        Eigen::Matrix<double, 2, 2> R_;
        Eigen::Matrix<double, 6, 6> Q_;
        Eigen::Matrix<double, 2, 1> Z_;

        void updateTime();
        void updateMeasure();
    };

  public:
    static std::unique_ptr<Tracker> create(unsigned int yield_time, bool quiet, 
        Encoder* enc, double max_dist, unsigned max_frm);
    virtual ~Tracker();

  public:
    virtual bool addMessage(std::shared_ptr<std::vector<BoxBuf>>& boxes);

  protected:
    Tracker() = delete;
    Tracker(unsigned int yield_time);
    bool init(bool quiet, Encoder* enc, double max_dist, unsigned int max_frm);

  protected:
    virtual bool waitingToRun();
    virtual bool running();
    virtual bool paused();
    virtual bool waitingToHalt();

  private:
    bool quiet_;
    Encoder* enc_;
    double max_dist_;
    unsigned int max_frm_;

    unsigned int frm_;
    unsigned int last_frm_;
    unsigned int track_cnt_;
    std::vector<Track> tracks_;

    MicroDiffer<uint32_t> differ_tot_;
    std::timed_mutex targets_lock_;
    std::vector<BoxBuf> targets_;
    std::atomic<bool> tracker_on_;

    bool associateTracks();
    bool createNewTracks();
    bool cleanupTracks();
};

} // namespace detector

#endif // TRACKER_H
