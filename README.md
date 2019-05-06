# Tracker

Tracker is a video pipeline application with target detection for 
the [raspberry pi 3b+](https://www.raspberrypi.org/products/raspberry-pi-3-model-b-plus).  
Targets are idenified in the output video with bounding boxes.  Target detection is 
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
export RASPBIAN=~/your/workspace/raspbian
export RASPBIANCROSS=$RASPBIAN/tools/arm-bcm2708/gcc-6-arm-linux-gnueabihf/armv6-rpi-linux-gnueabihf/bin/arm-linux-gnueabihf-
export OMXSUPPORT=$RASPBIAN/vc
export LIVE555=$RASPBIAN/live
export TFLOWSDK=$RASPBIAN/tensorflow
```

'RASPBIAN' is my project directory
'RASPBIANCROSS' is the project cross-compiler
'OMXSUPPORT' is the OMX libraries for encoding
'LIVE555' is the location of the RTSP source
'TFLOWSDK' is the location of TensorFlow

### Build Notes

### Usage

### Discussion

### To Do

