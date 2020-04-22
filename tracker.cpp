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
#include <limits>

#include "tracker.h"
#include "third_party/Hungarian.h"

namespace detector {

const Eigen::Matrix<double, 6, 6> Tracker::Track::A_{
  { 1, 0, 1, 0, 0, 0 },
  { 0, 1, 0, 1, 0, 0 },
  { 0, 0, 1, 0, 1, 0 },
  { 0, 0, 0, 1, 0, 1 },
  { 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0 }
};
const Eigen::Matrix<double, 2, 6> Tracker::Track::H_{
  { 1, 0, 0, 0, 0, 0 },
  { 0, 1, 0, 0, 0, 0 }
};

Tracker::Track::Track(unsigned int track_id, const BoxBuf& box)
  : id(track_id), frm(box.id), 
    x(box.x), y(box.y), w(box.w), h(box.h) {

  state_ = Tracker::Track::State::kInit;

  // initialize state vector with inital position
  double mid_x = x + w / 2.0;
  double mid_y = y + h / 2.0;
  X_ << mid_x,mid_y,0,0,0,0;

  // initialize error covariance matrix
  P_ << initial_error_,0,0,0,0,0,
        0,initial_error_,0,0,0,0,
        0,0,initial_error_,0,0,0,
        0,0,0,initial_error_,0,0,
        0,0,0,0,initial_error_,0,
        0,0,0,0,0,initial_error_;

  // initialize measurement covariance matrix
  R_ << measure_variance_,0,
        0,measure_variance_;

  // initialize process covariance matrix
  Q_ << process_variance_,0,0,0,0,0,
        0,process_variance_,0,0,0,0,
        0,0,process_variance_,0,0,0,
        0,0,0,process_variance_,0,0,
        0,0,0,0,process_variance_,0,
        0,0,0,0,0,process_variance_;
}

void Tracker::Track::updateTime() {
  //  predict state transition
  X_ = A_ * X_;

  //  update error matrix
  P_ = A_ * (P_ * A_.transpose()) + Q_;
}

void Tracker::Track::updateMeasure() {
  //  compute kalman gain
  auto K_ = P_ * H_.transpose() * (H_ * P_ * H_.transpose() + R_).inverse();

  //  fuse new measurement
  X_ = X_ + K_ * (Z_ - H_ * X_);

  //  update error matrix
  P_ = (Eigen::Matrix<double,6,6>::Identity() - K_ * H_) * P_;
}

double Tracker::Track::getDistance(double mid_x, double mid_y) {
  return std::sqrt(std::pow(mid_x - X_(0), 2) + std::pow(mid_y - X_(1), 2));
}

void Tracker::Track::addTarget(const BoxBuf& box) {

  frm = box.id;
  x = box.x;
  y = box.y;
  w = box.w;
  h = box.h;
  double mid_x = x + w / 2.0;
  double mid_y = y + h / 2.0;

  if (state_ == Tracker::Track::State::kInit) {
    X_(2) = (mid_x - X_(0));
    X_(3) = (mid_y - X_(1));
  }
  updateTime();

  state_ = Tracker::Track::State::kActive;

  Z_ << mid_x, mid_y;
  updateMeasure();
}


Tracker::Tracker(unsigned int yield_time) 
  : Base(yield_time) {
}

Tracker::~Tracker() {
}

std::unique_ptr<Tracker> Tracker::create(
    unsigned int yield_time, bool quiet, 
    Encoder* enc, double max_dist, unsigned int max_frm) {
  auto obj = std::unique_ptr<Tracker>(new Tracker(yield_time));
  obj->init(quiet, enc, max_dist, max_frm);
  return obj;
}

bool Tracker::init(bool quiet, Encoder* enc, double max_dist, unsigned int max_frm) {

  quiet_ = quiet;
  enc_ = enc;
  max_dist_ = max_dist;
  max_frm_ = max_frm;

  current_frm_ = 0;
  track_cnt_ = 0;

  tracker_on_ = false;
  
  return true; 
}

bool Tracker::addMessage(std::shared_ptr<std::vector<BoxBuf>>& boxes) {

  std::unique_lock<std::timed_mutex> lck(targets_lock_, std::defer_lock);

  if (!lck.try_lock_for(std::chrono::microseconds(
          Listener<std::shared_ptr<std::vector<BoxBuf>>>::timeout_))) {
    dbgMsg("encoder target lock busy\n");
    return false;
  }

  targets_.resize(boxes->size());
  std::copy(boxes->begin(), boxes->end(), targets_.begin());

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

  if (tracks_.size() && targets_.size()) {

    // compute cost matrix
    std::vector<std::vector<double>> mat(tracks_.size(),
        std::vector<double>(targets_.size(), 0.0));
    for (unsigned int k = 0; k < targets_.size(); k++) {
      double mid_x = targets_[k].x + targets_[k].w / 2.0;
      double mid_y = targets_[k].y + targets_[k].h / 2.0;
      for (unsigned int i = 0; i < tracks_.size(); i++) {
        mat[i][k] = tracks_[i].getDistance(mid_x, mid_y);
      }
    }

    // assign targets to tracks_
    HungarianAlgorithm hung_algo;
    vector<int> assignments;
    hung_algo.Solve(mat, assignments);

    // add targets to tracks_
    for (unsigned int i = 0; i < assignments.size(); i++) {
      double mid_x = targets_[i].x + targets_[i].w / 2.0;
      double mid_y = targets_[i].y + targets_[i].h / 2.0;
      if (tracks_[i].getDistance(mid_x, mid_y) <= max_dist_) {
        tracks_[i].addTarget(targets_[i]);
        targets_[i].id = std::numeric_limits<unsigned int>::max();
      }
    }

    // remove used targets
    targets_.erase(
        std::remove_if(targets_.begin(), targets_.end(),
          [&] (const BoxBuf& b) {
            return b.id == std::numeric_limits<unsigned int>::max();
          }), 
        targets_.end());
  }

  return true;
}

bool Tracker::createNewTracks() {

  if (targets_.size()) {
    for_each(targets_.begin(), targets_.end(),
        [&](const BoxBuf& b) {
          tracks_.push_back(Tracker::Track(track_cnt_, b));
          track_cnt_ += 1;
        });
  }

  targets_.resize(0);

  return true;
}

bool Tracker::cleanupTracks() {

  // remove old tracks
  tracks_.erase(
      std::remove_if(tracks_.begin(), tracks_.end(),
        [&] (const Tracker::Track& t) {
          return max_frm_ < current_frm_ - t.frm;;
        }), 
      tracks_.end());

  return true;
}

bool Tracker::postTracks() {

  auto tracks = std::make_shared<std::vector<TrackBuf>>();

  for_each(tracks_.begin(), tracks_.end(),
      [&](const Tracker::Track& t) {
        tracks->push_back(TrackBuf(
              t.type, t.id,
              round(t.x), round(t.y), round(t.w), round(t.h)));
      });

  if (enc_) {
    if (!enc_->addMessage(tracks)) {
      dbgMsg("encoder busy");
    }
  }
  return true;
}

bool Tracker::running() {

  if (tracker_on_) {

    std::unique_lock<std::timed_mutex> lck(targets_lock_);
    if (targets_.size() != 0) {

      // all frame ids are the same in a target collection
      // so it is safe to pick off the first one
      current_frm_ = targets_[0].id;  

      associateTracks();

      createNewTracks();

      cleanupTracks();

      postTracks();
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

