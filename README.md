# Tracker

Tracker is a video pipeline application with target detection for 
the [raspberry pi 3b+](https://www.raspberrypi.org/products/raspberry-pi-3-model-b-plus). Targets 
are idenified in the output video with bounding boxes.  Target detection is 
provided by [Tensorflow Lite](https://www.tensorflow.org/lite) running 
the [COCO SSD MobileNet v1 model](http://storage.googleapis.com/download.tensorflow.org/models/tflite/coco_ssd_mobilenet_v1_1.0_quant_2018_06_29.zip).  The resulting
video can be saved to an H264 elemental stream file or served up via RTSP.

Many thanks to the [Tensorflow](https://www.tensorflow.org) project for doing the target detection heavy lifting.

Many thinks to the [Live555](http://www.live555.com/) project for providing the RTSP implementation.

### Installation

Tracker is cross compiled on a desktop and copied to the target rpi3b+.  As such, the build 
process will require some environment variables so it can find the various pieces of 
Tracker.  The following is how my environment is setup:
```
# project directory
export RASPBIAN=~/your/workspace/raspbian
# project cross-compiler
export RASPBIANCROSS=$RASPBIAN/tools/arm-bcm2708/gcc-6-arm-linux-gnueabihf/armv6-rpi-linux-gnueabihf/bin/arm-linux-gnueabihf-
# OMX libraries for encoding
export OMXSUPPORT=$RASPBIAN/vc
# RTSP source location
export LIVE555=$RASPBIAN/live
# TensorFlow location
export TFLOWSDK=$RASPBIAN/tensorflow
```

Get Tensorflow, Live555 and Tracker like this:
```
cd your/workspace/raspbian
git clone https://gitlab.com:tylerjbrooks/tensorflow.git
cd tensorflow
git checkout rpi
cd ..
git clone https://gitlab.com:tylerjbrooks/live.git
git clone https://gitlab.com:tylerjbrooks/tracker.git

```

Get the Raspbian tool chain like this:
```
cd your/workspace/raspbian
git clone https://github.com/raspberrypi/tools.git
cd tools/arm-bcm2708
git clone https://github.com/africanmudking/gcc-6-arm-linux-gnueabihf.git
cd gcc-6-arm-linux-gnueabihf.git
cat armv6-rpi-linux-gnueabihf.tar.xz.parta* > armv6-rpi-linux-gnueabihf.tar.xz
tar xvJf armv6-rpi-linux-gnueabihf.tar.xz
cd ../../..
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

At this point, you should have all the software you need to build Tracker.

### Build Notes

Start by building Tensorflow Lite:
```
cd your/workspace/raspbian
cd tensorflow/tensorflow/lite/tools/make
./download_dependencies.sh
./cross_rpi_lib.sh
```

Build the RTSP source:
```
cd your/workspace/raspbian
cd live
./genMakefiles rpi
make
```

Build Tracker:
```
cd your/workspace/raspbian
cd tracker
make
```

### Usage

Tracker requires the model and label file be downloaded to the rpib3+ separately.  Setup a 
deployment directory and download the model zip file like this:
```
# on the target rpi3b+
cd your/deployment/dir/tracker
mkdir models
cd models
wget http://storage.googleapis.com/download.tensorflow.org/models/tflite/coco_ssd_mobilenet_v1_1.0_quant_2018_06_29.zip
unzip coco_ssd_mobilenet_v1_1.0_quant_2018_06_29.zip
cd ..
# copy your tracker executable from your cross compile machine to here.
```

This is how you invoke tracker:
```
tracker -?qrutdfwhb [output]
version: 0.5

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
  (m)odel      = path to model       (default = ./models/detect.tflite)
  (l)abels     = path to labels      (default = ./models/labelmap.txt)
  output       = output file name
               = leave blank for stdout
               = no output if testtime is 0
```



### Discussion

### To Do

- Try different tflite detection models
- Try NNApi for acceleration
