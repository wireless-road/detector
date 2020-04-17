## Detector

Detector is a video pipeline application for the 
[raspberry pi 3b+](https://www.raspberrypi.org/products/raspberry-pi-3-model-b-plus) with 
realtime object detection.  Objects are identified in the output video with bounding 
boxes.  Object detection is provided by [Tensorflow Lite](https://www.tensorflow.org/lite) 
running the [COCO SSD MobileNet V2 Quantized model](http://download.tensorflow.org/models/object_detection/ssd_mobilenet_v2_quantized_300x300_coco_2019_01_03.tar.gz).  The resulting video can be saved to an H264 elemental 
stream file or served up via RTSP.  In addition, the application will take advantage of a [Edge TPU](https://coral.ai/products/accelerator/) if one is present.

The application is written in C++. 

Many thanks to the [Tensorflow](https://www.tensorflow.org) project for doing the object detection heavy lifting.

Many thanks to the [Google Coral](https://coral.ai/) project for providing the TPU accelerator.

Many thinks to the [Live555](http://www.live555.com/) project for providing the RTSP implementation.

### Installation

Detector is cross compiled on a desktop and copied to the target rpi3b+.  As such, the build 
process will require some environment variables so it can find the various pieces of 
Detector.  The following is how my environment is setup:
```
# project directory
export RASPBIAN=~/your/workspace/raspbian
# project cross-compiler
export RASPBIANCROSS=arm-linux-gnueabihf-
# OMX libraries for encoding
export OMXSUPPORT=$RASPBIAN/vc
# RTSP source location
export LIVE555=$RASPBIAN/live
# TensorFlow location
export TFLOWSDK=$RASPBIAN/tensorflow
# Edge Tpu location
export EDGETPUSDK=$RASPBIAN/edgetpu
```

Get Tensorflow, Edgetpu, Live555 and Detector like this:
```
cd your/workspace/raspbian
git clone https://gitlab.com:tylerjbrooks/tensorflow.git
cd tensorflow
git checkout d855adf  #tensorflow commit d855adf
cd ..
https://github.com/google-coral/edgetpu
git clone https://gitlab.com:tylerjbrooks/live.git
git clone https://gitlab.com:tylerjbrooks/detector.git

```
note:  the Edgetpu sdk currently only works with the 'd855adf' commit of tensorflow.

Get the Raspbian tool chain like this:
```
sudo apt-get install build-essential
sudo apt-get install g++-arm-linux-gnueabihf
```

Get the rpi3b+ VideoCore software by taring up the '/opt/vc' directory on your rpi3b+
and copying the tar to your test directory.  Then untar directory.  Something like this:
```
# on your rpi3b+
cd /opt
tar -vczf vc.tar.gz vc
# copy vc.tar.gz to your cross compile machine
cd your/workspace/raspbian
tar -vxzf vc.tar.gz
```

At this point, you should have all the software you need to build Detector.

### Build Notes

Start by building Tensorflow Lite:
```
cd your/workspace/raspbian
cd tensorflow/tensorflow/lite/tools/make
./download_dependencies.sh
./build_rpi_lib.sh
```

Build the RTSP source:
```
cd your/workspace/raspbian
cd live
./genMakefiles rpi
make
```

Build Detector:
```
cd your/workspace/raspbian
cd detector
make
```

### Usage

Detector requires the model and label file.  The default 
models are alreadiy in the './models' directory.

This is how you invoke detector:
```
detector -?qprutdfwhbyesml [output]
version: 1.0

  where:
  ?            = this screen
  (q)uiet      = suppress messages   (default = false)
  (r)tsp       = rtsp server         (default = off)
  (u)nicast    = rtsp unicast addr   (default = none)
               = multicast if no address specified
  (t)esttime   = test duration       (default = 30sec)
               = 0 to run until ctrl-c
  (d)device    = video device num    (default = 0)
  (f)ramerate  = capture framerate   (default = 20)
  (w)idth      = capture width       (default = 640)
               = negative value means flip
  (h)eight     = capture height      (default = 480)
               = negative value means flip
  (b)itrate    = encoder bitrate     (default = 1000000)
  (y)ield time = yield time          (default = 1000usec)
  thr(e)ads    = number of tflow threads (default = 1)
  thre(s)hold  = object detect threshold (default = 0.5)
  t(p)u        = use Edge TPU        (default = false)
  (m)odel      = path to model       (default = ./models/detect.tflite)
                                     (default = ./models/edgetpu_detect.tflite)
  (l)abels     = path to labels      (default = ./models/labels.txt)
                                     (default = ./models/edgetpu_labels.txt)
  (o)utput     = output file name
               = no output if testtime is 0
```

#### Simple Example

A typical example command would be:
```
./detector -t 10 -w -640 -h -480 -e 4 -o output.h264
```

This command will capture a 640x480 video for 10 seconds and write the H264 elemental stream
to 'output.h264'.  The TensorFlow Lite object detection engine will use 4 threads.  The output 
will look like this:

```
./detector -t 10 -w -640 -h -480 -e 4 -o output.h264


Test Setup...
   test time: 10 seconds
      device: /dev/video0
        rtsp: no
   framerate: 20 fps
       width: 640 pix (flipped)
      height: 480 pix (flipped)
     bitrate: 1000000 bps
  yield time: 1000 usec
     threads: 4
   threshold: 0.500000
         tpu: no
       model: ./models/detect.tflite
      lables: ./models/labelmap.txt
      output: output.h264

         pid: top -H -p 1350


..<person>..<person>..<person>.<person>..<person>..<person>.....


Capturer Results...
   number of frames captured: 220
   tflow copy time (us): high:1439 avg:1014 low:9 frames:220
  encode copy time (us): high:1982 avg:918 low:709 frames:220
        total test time: 10.113406 sec
      frames per second: 21.753304 fps

<person>
Tflow Results...
  image copy time (us): high:1423 avg:800 low:734 frames:31
  image prep time (us): high:102889 avg:75376 low:74075 frames:31
  image eval time (us): high:186585 avg:181407 low:178261 frames:31
  image post time (us): high:264 avg:96 low:69 frames:31
       total test time: 10.896498 sec
     frames per second: 2.844951 fps


Encoder Results...
  image copy   time (us): high:1939 avg:900 low:701 frames:220
  image encode time (us): high:36954 avg:7700 low:3446 frames:220
         total test time: 10.983922 sec
       frames per second: 20.029276 fps

```
This tells us the test setup and results.  While the test is working it will 
display a series of '.' characters plus a label indicating what object was detected 
(like: person).  The output tells us that 220 frames were captured.  That
they were copied to the tensorflow and encoder worker threads for an average of 
~1ms each.  The tensorflow thread took, on average ~75ms to scale the input image and
~181ms to run an inference.  Also, 
the tensorflow thread worked for ~10.9 seconds and processed 50 frames.  This means it 
processed ~2.8 frames a second.  The encoder had similar results.

Of special interest, is the 'top -H -p 1350' command which is printed just under the 'Test Setup'
section.  You can run this command in a seperate terminal window on your rpi3b+ to see what 
threads are created and how much of the CPU they are consuming.  The program prints out this 
command for each run for convenience.

#### RTSP Example

As another example, the follow command allows you to see object detection in realtime:
```
./detector -r -u 192.168.1.85 -t 0 -w -640 -h -480
```
This command will unicast RTSP to the given ip address until you hit ctrl-c.  The typical
output from this command looks like this:
```

Test Setup...
   test time: run until ctrl-c
      device: /dev/video0
        rtsp: yes
rstp address: 192.168.1.85
   framerate: 20 fps
       width: 640 pix (flipped)
      height: 480 pix (flipped)
     bitrate: 1000000 bps
  yield time: 1000 usec
     threads: 1
   threshold: 0.500000
         tpu: no
       model: ./models/detect.tflite
      lables: ./models/labelmap.txt
      output: none

         pid: top -H -p 1373

Play this stream using: rtsp://192.168.1.156:8554/camera


Hit ctrl-c to terminate...

....<person>...<person>...<person>..<person>...<person>...<person>...^C

Capturer Results...
   number of frames captured: 590
   tflow copy time (us): high:1662 avg:1037 low:4 frames:590
  encode copy time (us): high:2340 avg:978 low:717 frames:590
        total test time: 29.083607 sec
      frames per second: 20.286343 fps

Tflow Results...
  image copy time (us): high:1539 avg:810 low:718 frames:50
  image prep time (us): high:145951 avg:82424 low:74303 frames:50
  image eval time (us): high:486392 avg:430224 low:416651 frames:50
  image post time (us): high:859 avg:108 low:54 frames:50
       total test time: 30.092148 sec
     frames per second: 1.661563 fps


Encoder Results...
  image copy   time (us): high:2327 avg:958 low:706 frames:588
  image encode time (us): high:402848 avg:5012 low:3455 frames:588
         total test time: 30.181433 sec
       frames per second: 19.482178 fps

```
Notice the rtsp url is given just below the Test Setup report.  You can use cvlc to view this 
stream like this:
```
cvlc rtsp://192.168.1.156:8554/camera 
```

#### Edge TPU Example

```
./detector -r -u 192.168.1.85 -t 0 -w -640 -h -480 -p

Test Setup...
   test time: run until ctrl-c
      device: /dev/video0
        rtsp: yes
rstp address: 192.168.1.85
   framerate: 20 fps
       width: 640 pix (flipped)
      height: 480 pix (flipped)
     bitrate: 1000000 bps
  yield time: 1000 usec
     threads: 1
   threshold: 0.500000
     use tpu: yes
       model: ./models/edgetpu_detect.tflite
      lables: ./models/edgetpu_labels.txt
      output: none

         pid: top -H -p 1211

Play this stream using: rtsp://192.168.1.156:8554/camera


Hit ctrl-c to terminate...

....<person>...<person>...<person>..<person>...<person>...<person>...^C

Capturer Results...
   number of frames captured: 1603
   tflow copy time (us): high:1316 avg:279 low:5 frames1603
  encode copy time (us): high:1749 avg:900 low:747 frames1603
        total test time: 74.120842 sec
      frames per second: 21.626846 fps

<person>
Tflow Results...
  image copy time (us): high:1299 avg:812 low:768 frames532
  image prep time (us): high:103796 avg:67993 low:64642 frames532
  image eval time (us): high:255985 avg:58495 low:56647 frames532
  image post time (us): high:265 avg:108 low:42 frames532
       total test time: 74.665337 sec
     frames per second: 7.125127 fps


Encoder Results...
  image copy   time (us): high:1732 avg:886 low:737 frames1603
  image encode time (us): high:72334 avg:4519 low:3229 frames1603
         total test time: 78.029808 sec
       frames per second: 20.543432 fps
```
Notice in this example the image evaluation takes about 60ms on average as compared to ~180ms
when running without the TPU (using 4 engines, however).  So using the TPU is about 
3 times as fast altough the inferences per second are about double.  The reason for 
this is that the TPU is connecteed to the rpi3b+ via USB 2.0 and the resize and 
inferencing are IO bound.


### Discussion

Detector is composed of a UI thread plus four worker threads.

- detector.cpp:  UI thread.  It launches the other threads and goes to sleep for the 
duration of the test.
- capturer.{h,cpp}:  V4L2 image video capture thread.  It sets up the V4L2 device, captures
frames from the device and sends them to the encoder and object detection threads.
- encoder.{h,cpp}:  OMX encoder thread.  It waits for images from the capture thread
and encodes them into H264 NALs.  Those NALs are put into an output file and/or sent to the RTSP
server
- tflow.{h,cpp}:  Tensorflow Lite object detection engine.  It waits for images from the 
capturer thread, scales the images for the object model and then runs an inference.  The result are 
object 'boxes' which are sent to the encoder as an overlay for the image before it is encoded.
- rtsp.{h,cpp}:  Live555 RTSP server implementation.  

All the significate threads in the program are derived from a base state machine (base.{h,cpp}).  See
the comment at the top of base.h for more details.

### Notes

### To Do

- Get RTSP to work faster than 20fps
- Done. ~Try different tflite detection models~
- Done. ~Try tflite delegation for faster object detection~
