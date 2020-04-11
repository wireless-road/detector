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

#include <iostream>
#include <algorithm>
#include <memory>
#include <chrono>
#include <cmath>
#include <signal.h>
#include <unistd.h>

#include "utils.h"
#include "base.h"
#include "encoder.h"
#include "rtsp.h"
#include "capturer.h"
#include "tflow.h"

namespace detector {

std::unique_ptr<Encoder>  enc(nullptr);
std::unique_ptr<Rtsp>     rtsp(nullptr);
std::unique_ptr<Capturer> cap(nullptr);
std::unique_ptr<Tflow>    tfl(nullptr);

void usage() {
  std::cout << "detector -?qrutdfwhbyesml [output]" << std::endl;
  std::cout << "version: 0.5"                     << std::endl;
  std::cout                                       << std::endl;
  std::cout << "  where:"                         << std::endl;
  std::cout << "  ?            = this screen"                           << std::endl;
  std::cout << "  (q)uiet      = suppress messages   (default = false)" << std::endl;
  std::cout << "  (r)tsp       = rtsp server         (default = off)"   << std::endl;
  std::cout << "  (u)nicast    = rtsp unicast addr   (default = none)"  << std::endl;
  std::cout << "               = multicast if no address specified"     << std::endl;
  std::cout << "  (t)esttime   = test duration       (default = 30sec)" << std::endl;
  std::cout << "               = 0 to run until ctrl-c"                 << std::endl;
  std::cout << "  (d)device    = video device num    (default = 0)"     << std::endl;
  std::cout << "  (f)ramerate  = capture framerate   (default = 20)"    << std::endl;
  std::cout << "  (w)idth      = capture width       (default = 640)"   << std::endl;
  std::cout << "               = negative value means flip"             << std::endl;
  std::cout << "  (h)eight     = capture height      (default = 480)"   << std::endl;
  std::cout << "               = negative value means flip"             << std::endl;
  std::cout << "  (b)itrate    = encoder bitrate     (default = 1000000)" << std::endl;
  std::cout << "  (y)ield time = yield time          (default = 1000usec)" << std::endl;
  std::cout << "  thr(e)ads    = number of tflow threads (default = 1)"   << std::endl;
  std::cout << "  thre(s)hold  = object detect threshold (default = 0.5)" << std::endl;
  std::cout << "  (m)odel      = path to model       (default = ./models/detect.tflite)" << std::endl;
  std::cout << "  (l)abels     = path to labels      (default = ./models/labelmap.txt)" << std::endl;
  std::cout << "  output       = output file name"                      << std::endl;
  std::cout << "               = leave blank for stdout"                << std::endl;
  std::cout << "               = no output if testtime is 0"            << std::endl;
}

void quitHandler(int s) {
  if (cap)  { cap->stop(); }
  if (tfl)  { tfl->stop(); }
  if (enc)  { enc->stop(); }
  if (rtsp) { rtsp->stop(); }

  cap.reset(nullptr);
  tfl.reset(nullptr);
  enc.reset(nullptr);
  rtsp.reset(nullptr);

  exit(1);
}

int main(int argc, char** argv) {

  // defaults
  bool quiet = false;
  bool streaming = false;
  std::string  unicast;
  unsigned int yield_time = 1000;
  unsigned int testtime = 30;
  unsigned int device = 0;
  unsigned int framerate = 20;
           int wdth = 640;
           int hght = 480;
  unsigned int bitrate = 1000000;
  unsigned int threads = 1;
  float        threshold = 0.5f;
  std::string  model = "./models/detect.tflite";
  std::string  labels = "./models/labelmap.txt";
  std::string  output;

  // cmd line options
  int c;
  while((c = getopt(argc, argv, ":qru:t:d:f:w:h:b:y:e:s:m:l:")) != -1) {
    switch (c) {
      case 'q': quiet     = true;               break;
      case 'r': streaming = true;               break;
      case 'u': unicast   = optarg;             break;
      case 't': testtime  = std::stoul(optarg); break;
      case 'd': device    = std::stoul(optarg); break;
      case 'f': framerate = std::stoul(optarg); break;
      case 'w': wdth      = std::stoi(optarg);  break;
      case 'h': hght      = std::stoi(optarg);  break;
      case 'b': bitrate   = std::stoul(optarg); break;
      case 'y': yield_time= std::stoul(optarg); break;
      case 'e': threads   = std::stoul(optarg); break;
      case 's': threshold = std::stof(optarg);  break;
      case 'm': model     = optarg;             break;
      case 'l': labels    = optarg;             break;

      case '?':
      default:  usage(); return 0;
    }
  }
  if (optind < argc) {
    output = argv[optind];
  }

  // ctrl-c handler
  struct sigaction sig_int;
  sig_int.sa_handler = quitHandler;
  sigemptyset(&sig_int.sa_mask);
  sig_int.sa_flags = 0;
  sigaction(SIGINT, &sig_int, NULL);

  // test setup report
  if (!quiet) {
    fprintf(stderr, "\nTest Setup...\n");
    if (testtime) {
      fprintf(stderr, "   test time: %d seconds\n", testtime);
    } else {
      fprintf(stderr, "   test time: run until ctrl-c\n");
    }
    fprintf(stderr, "      device: /dev/video%d\n", device);
    fprintf(stderr, "        rtsp: %s\n", streaming ? "yes" : "no");
    if (streaming) {
      fprintf(stderr, "rstp address: %s\n", unicast.empty() ? "multicast" : unicast.c_str());
    }
    fprintf(stderr, "   framerate: %d fps\n", framerate);
    fprintf(stderr, "       width: %d pix %s\n", std::abs(wdth), (wdth < 0) ? "(flipped)" : "" );
    fprintf(stderr, "      height: %d pix %s\n", std::abs(hght), (hght < 0) ? "(flipped)" : "" );
    fprintf(stderr, "     bitrate: %d bps\n", bitrate);
    fprintf(stderr, "  yield time: %d usec\n", yield_time);
    fprintf(stderr, "     threads: %d\n", threads);
    fprintf(stderr, "   threshold: %f\n", threshold);
    fprintf(stderr, "       model: %s\n", model.c_str());
    fprintf(stderr, "      lables: %s\n", labels.c_str());
    fprintf(stderr, "      output: %s\n\n", (testtime == 0) ? "none" : output.c_str());
    fprintf(stderr, "         pid: top -H -p %d\n\n", getpid());
  }

  // create worker threads
  if (streaming) { 
    rtsp = Rtsp::create(yield_time, quiet, bitrate, framerate, unicast); 
  }
  enc = Encoder::create(yield_time, quiet, rtsp.get(), framerate, 
      std::abs(wdth), std::abs(hght), bitrate, output, testtime);
  tfl = Tflow::create(2*yield_time, quiet, enc.get(), std::abs(wdth), 
      std::abs(hght), model.c_str(), labels.c_str(), threads, threshold);
  cap = Capturer::create(yield_time, quiet, enc.get(), tfl.get(), 
      device, framerate, wdth, hght);

  // start
  dbgMsg("start\n");
  if (streaming) { rtsp->start("rtsp", 90); }
  enc->start("enc", 50);
  tfl->start("tfl", 20);
  cap->start("cap", 90);

  // run
  dbgMsg("run\n");
  if (streaming) { rtsp->run(); }
  enc->run();
  tfl->run();
  cap->run();

  // run test
  if (!quiet) { fprintf(stderr, "\n\n"); }
  if (testtime) {   // run for testtime...
    for (unsigned int i = 0; i < testtime * 5; i++) {
      if (!quiet) { fprintf(stderr, "."); fflush(stdout); }
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  } else {          // run forever...
    if (!quiet) {
      fprintf(stderr, "Hit ctrl-c to terminate...\n\n");
    }
    while (1) {
      if (!quiet) { fprintf(stderr, "."); fflush(stdout); }
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
  }
  if (!quiet) { fprintf(stderr, "\n\n"); }

  // stop
  dbgMsg("stop\n");
  cap->stop();
  tfl->stop();
  enc->stop();
  if (streaming) { rtsp->stop(); }

  // destroy
  cap.reset(nullptr);
  tfl.reset(nullptr);
  enc.reset(nullptr);
  rtsp.reset(nullptr);

  // done
  dbgMsg("done\n");
  return 0;
}

} // namespace detector

int main(int argc, char** argv) {
  return detector::main(argc, argv);
}

