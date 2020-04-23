# environment variables:
#   RASPBIAN is the location of your raspbian projects
#   RASPBIANCROSS is the location of the raspbian compiler tools
#   LIBYUV is the location of your libyuv
#   OMXSUPPORT is the location of your 'video core' support (OMX and such)
#   TFLOWLITESDK is the location of your 'tensorflow-lite' sdk

CXX = $(RASPBIANCROSS)g++

SRC = \
	detector.cpp \
	base.cpp \
	capturer.cpp \
	tflow.cpp \
	tracker.cpp \
	encoder.cpp \
	rtsp.cpp \
	utils.cpp \
	./third_party/Hungarian.cpp
OBJ = $(SRC:.cpp=.o)
EXE = detector

# Turn on 'CAPTURE_ONE_RAW_FRAME' to write the 10th frame
# in to './frame_wxh_ffps.yuv' file (w=width, h=height, f=framerate).
#
# Turn on 'OUTPUT_VARIOUS_BITS_OF_INFO' to print out various
# bits of interesting information about the setup.
#
# Turn on 'DEBUG_MESSAGES' to turn on debug messages.
#FEATURES = -DCAPTURE_ONE_RAW_FRAME -DOUTPUT_VARIOUS_BITS_OF_INFO -DDEBUG_MESSAGES

CFLAGS =-DSTANDALONE -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -DTARGET_POSIX -D_LINUX -fPIC -DPIC -D_REENTRANT -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -U_FORTIFY_SOURCE -Wall -DHAVE_LIBOPENMAX=2 -DOMX -DOMX_SKIP64BIT -ftree-vectorize -pipe -DUSE_EXTERNAL_OMX -DHAVE_LIBBCM_HOST -DUSE_EXTERNAL_LIBBCM_HOST -DUSE_VCHIQ_ARM -std=c++17 -march=armv7-a -mfpu=neon-vfpv4 -Wno-psabi $(FEATURES)
CFLAGS += -g 
#CFLAGS += -O3

LDFLAGS = \
	-L$(OMXSUPPORT)/lib \
	-L$(LIBYUV) \
	-L$(LIVE555)/liveMedia \
	-L$(LIVE555)/UsageEnvironment \
	-L$(LIVE555)/BasicUsageEnvironment \
	-L$(LIVE555)/groupsock \
	-L$(TFLOWSDK)/tensorflow/lite/tools/make/gen/rpi_armv7l/lib \
	-L$(EDGETPUSDK)/libedgetpu/direct/armv7a

LIBS = -ltensorflow-lite -lliveMedia -lgroupsock -lBasicUsageEnvironment -lUsageEnvironment 
LIBS += -l:libedgetpu.so.1.0 
LIBS += -lopenmaxil -lbcm_host -lvcos -lvchiq_arm -lbrcmEGL -lbrcmGLESv2 -lpthread -ldl -lrt -lm

#add these if cross compiling
# this is weird but I can't seem to 'apt install libuse-1.0-dev' so I have 
# copied them into the home directory from the rpi
LDFLAGS += -L./lib
LIBS += -l:libc.so.6 -l:libudev.so.1 -l:libusb-1.0.so.0

INCLUDES = \
	-I. \
	-I$(OMXSUPPORT)/include \
	-I$(LIBYUV)/include \
	-I$(LIVE555)/liveMedia/include \
	-I$(LIVE555)/UsageEnvironment/include \
	-I$(LIVE555)/BasicUsageEnvironment/include \
	-I$(LIVE555)/groupsock/include \
	-I$(TFLOWSDK) \
	-I$(TFLOWSDK)/tensorflow/lite/tools/make/downloads/flatbuffers/include \
	-I$(TFLOWSDK)/tensorflow/lite/tools/make/downloads/absl \
	-I$(EDGETPUSDK) \
	-I$(EDGETPUSDK)/libedgetpu 


$(EXE): $(OBJ)
	$(CXX) $(LDFLAGS) $(OBJ) $(LIBS) -o $@

.cpp.o:
	$(CXX) $(CFLAGS) $(INCLUDES) -c $< -o $@

.PHONY: clean
clean:
	rm -f $(EXE) $(OBJ)

