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
#include <set>
#include <chrono>

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
        Track() = default;
        Track(unsigned int track_id, const BoxBuf& box);
        Track(const Tracker::Track& t) = default;
        Tracker::Track& operator=(Tracker::Track&& t) { return *this; }
        ~Track() {}

      public:
        double getDistance(double mid_x, double mid_y);
        void addTarget(const BoxBuf& box);
        void updateTime();

      public:
        unsigned int id;
        std::chrono::steady_clock::time_point stamp;
        BoxBuf::Type type;
        double x, y, w, h;
        bool touched;

      private:
        Track::State state_{Track::State::kInit};

        const double initial_error_{1.0};
        const double process_variance_{1.0};
        const double measure_variance_{1.0};

        const static Eigen::Matrix<double, 6, 6> A_;
        const static Eigen::Matrix<double, 2, 6> H_;

        Eigen::Matrix<double, 6, 1> X_;
        Eigen::Matrix<double, 6, 6> P_;
        Eigen::Matrix<double, 2, 2> R_;
        Eigen::Matrix<double, 6, 6> Q_;
        Eigen::Matrix<double, 2, 1> Z_;

        void updateMeasure();
    };

  public:
    static std::unique_ptr<Tracker> create(unsigned int yield_time, bool quiet, 
        Encoder* enc, double max_dist, unsigned int max_time);
    virtual ~Tracker();

  public:
    virtual bool addMessage(std::shared_ptr<std::vector<BoxBuf>>& boxes);

  protected:
    Tracker() = delete;
    Tracker(unsigned int yield_time);
    bool init(bool quiet, Encoder* enc, double max_dist, unsigned int max_time);

  protected:
    virtual bool waitingToRun();
    virtual bool running();
    virtual bool paused();
    virtual bool waitingToHalt();

  private:
    bool quiet_;
    Encoder* enc_;
    double max_dist_;
    unsigned int max_time_;

    unsigned int track_cnt_;
    std::vector<Track> tracks_;

    MicroDiffer<uint32_t> differ_tot_;
    MicroDiffer<uint32_t> differ_associate_;
    MicroDiffer<uint32_t> differ_create_;
    MicroDiffer<uint32_t> differ_cleanup_;
    MicroDiffer<uint32_t> differ_post_;

    std::timed_mutex targets_lock_;
    std::vector<BoxBuf> targets_;
    std::set<BoxBuf::Type> target_types_{ 
      BoxBuf::Type::kPerson, 
      BoxBuf::Type::kPet, 
      BoxBuf::Type::kVehicle
    };

    std::atomic<bool> tracker_on_;

    bool untouchTracks();
    bool associateTracks();
    bool createNewTracks();
    bool touchTracks();
    bool cleanupTracks();
    bool postTracks();
};

} // namespace detector

#endif // TRACKER_H
