#
# Copyright 2020 The TensorFlow Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


# Builds the minimal Tensorflow Lite example.

cmake_minimum_required(VERSION 3.16)
project(detector C CXX)

#include(ExternalProject)

set(TENSORFLOW_SOURCE_DIR "" CACHE PATH
  "Directory that contains the TensorFlow project"
)
if(NOT TENSORFLOW_SOURCE_DIR)
  get_filename_component(TENSORFLOW_SOURCE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../../../"
    ABSOLUTE
  )
endif()

add_subdirectory(
  "${TENSORFLOW_SOURCE_DIR}/tensorflow/lite"
  "${CMAKE_CURRENT_BINARY_DIR}/tensorflow-lite"
  EXCLUDE_FROM_ALL
)

#set(UNITY libedgetpu)
#ExternalProject_Add(
#    libedgetpu
#    GIT_REPOSITORY https://github.com/google-coral/libedgetpu.git
#    GIT_TAG 3164995622300286ef2bb14d7fdc2792dae045b7
#    PREFIX ${PROJECT_SOURCE_DIR}/external/${UNITY}
#    CONFIGURE_COMMAND ""
#    BUILD_COMMAND ""
#    INSTALL_COMMAND ""
#    UPDATE_COMMAND ""
#)

#set(EDGETPUSDK ${PROJECT_SOURCE_DIR}/external/${UNITY}/src/libedgetpu)

set(CMAKE_CXX_STANDARD 17)

add_compile_options(-Wno-narrowing)

add_executable(detector detector.cpp base.cpp capturer.cpp tflow.cpp tracker.cpp utils.cpp third_party/Hungarian/Hungarian.cpp jpeg_compressor.cpp)
target_compile_definitions(detector PUBLIC -DWITHOUT_ENCODER -DWITHOUT_EDGETPU -DWITH_JPEG -DWITHOUT_RTSP)
#target_include_directories(detector PRIVATE ${LIBJPEG_PATH}/include/)

target_link_libraries(detector
  tensorflow-lite
  jpeg
)
