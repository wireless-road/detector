#!/bin/bash
cp coco_labels.txt ../edgetpu_labels.txt
cp mobilenet_ssd_v2_coco_quant_postprocess_edgetpu.tflite ../edgetpu_detect.tflite

