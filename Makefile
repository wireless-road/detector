# environment variables:
#   RASPBIAN is the location of your raspbian projects
#   RASPBIANCROSS is the location of the raspbian compiler tools
#   OMXSUPPORT is the location of your 'video core' support (OMX and such)
#   TFLOWLITESDK is the location of your 'tensorflow-lite' sdk

CXX = $(RASPBIANCROSS)g++

SRC = \
	detector.cpp \
	base.cpp \
	capturer.cpp \
	tflow.cpp \
	tracker.cpp \
	utils.cpp \
	./third_party/Hungarian/Hungarian.cpp
OBJ = $(SRC:.cpp=.o)
DEP = $(OBJ:.o=.d)
EXE = detector


# Turn on 'CAPTURE_ONE_RAW_FRAME' to write the 10th frame
# in to './frame_wxh_ffps.yuv' file (w=width, h=height, f=framerate).
#
# Turn on 'OUTPUT_VARIOUS_BITS_OF_INFO' to print out various
# bits of interesting information about the setup.
#
# Turn on 'DEBUG_MESSAGES' to turn on debug messages.
ifdef WITH_DEBUG_FEATURES
FEATURES = -DCAPTURE_ONE_RAW_FRAME -DOUTPUT_VARIOUS_BITS_OF_INFO -DDEBUG_MESSAGES
endif

CFLAGS =-DSTANDALONE -D__STDC_CONSTANT_MACROS -D__STDC_LIMIT_MACROS -DTARGET_POSIX -D_LINUX -fPIC -DPIC -D_REENTRANT -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -U_FORTIFY_SOURCE -Wall -ftree-vectorize -pipe -DUSE_EXTERNAL_OMX -DUSE_VCHIQ_ARM -std=c++17 -march=armv7-a -mfpu=neon-vfpv4 -Wno-psabi -Wno-attributes $(FEATURES)
#CFLAGS += -g
CFLAGS += -O3

DEPFLAGS = -MD -MP

INCLUDES = \
	-I. \
	-I$(TFLOWSDK) \
	-I$(TFLOWSDK)/build/flatbuffers/include \
	-I$(EDGETPUSDK) \
	-I$(EDGETPUSDK)/libedgetpu


LDFLAGS = \
	-L$(OMXSUPPORT)/lib \
	-L$(TFLOWSDK)/build \
	-L$(TFLOWSDK)/build/_deps/xnnpack-build \
	-L$(TFLOWSDK)/build/cpuinfo \
	-L$(TFLOWSDK)/build/clog \
	-L$(TFLOWSDK)/build/pthreadpool \
	-L$(TFLOWSDK)/build/_deps/ruy-build \
	-L$(TFLOWSDK)/build/_deps/flatbuffers-build \
	-L$(TFLOWSDK)/build/_deps/fft2d-build \
	-L$(TFLOWSDK)/build/_deps/farmhash-build \
	-L$(EDGETPUSDK)/libedgetpu/direct/armv7a

LIBS = -ltensorflow-lite
LIBS += -lXNNPACK -lcpuinfo -lclog -lpthreadpool
LIBS += -lruy
LIBS += -lflatbuffers
LIBS += -lfft2d_fftsg -lfft2d_fftsg2d
LIBS += -lfarmhash

LIBS += -l:libedgetpu.so.1.0

ifndef WITHOUT_ENCODER
SRC += encoder.cpp
INCLUDES += -I$(OMXSUPPORT)/include
CFLAGS += -DHAVE_LIBOPENMAX=2 -DOMX -DOMX_SKIP64BIT -DHAVE_LIBBCM_HOST -DUSE_EXTERNAL_LIBBCM_HOST
LIBS += -lopenmaxil -lbcm_host -lvcos -lvchiq_arm -lbrcmEGL -lbrcmGLESv2
else
CFLAGS += -DWITHOUT_ENCODER
endif
LIBS += -lpthread -ldl -lrt -lm

ifndef WITHOUT_RTSP
SRC += rtsp.cpp
INCLUDES += \
	-I$(LIVE555)/liveMedia/include \
	-I$(LIVE555)/UsageEnvironment/include \
	-I$(LIVE555)/BasicUsageEnvironment/include \
	-I$(LIVE555)/groupsock/include
LDFLAGS += \
	-L$(LIVE555)/liveMedia \
	-L$(LIVE555)/UsageEnvironment \
	-L$(LIVE555)/BasicUsageEnvironment \
	-L$(LIVE555)/groupsock
LIBS += -lliveMedia -lgroupsock -lBasicUsageEnvironment -lUsageEnvironment
else
CFLAGS += -DWITHOUT_RTSP
endif

ifdef WITH_JPEG
SRC += jpeg_compressor.cpp
INCLUDES += -I/opt/libjpeg-turbo/include/
CFLAGS += -DWITH_JPEG
LDFLAGS += -L/opt/libjpeg-turbo/lib32/ -lturbojpeg
endif

ifdef WITH_YUV
SRC += frame_convert.cpp
INCLUDES += -I/usr/local/include/
CFLAGS += -DWITH_YUV
LDFLAGS += -lyuv
endif

#add these if cross compiling
# this is weird but I can't seem to 'apt install libuse-1.0-dev' so I have
# copied them into the home directory from the rpi
LDFLAGS += -L./lib
LIBS += -l:libc.so.6 -l:libudev.so.1 -l:libusb-1.0.so.0

$(EXE): $(OBJ)
	$(CXX) $(LDFLAGS) $(OBJ) $(LIBS) -o $@

.cpp.o:
	$(CXX) $(CFLAGS) $(DEPFLAGS) $(INCLUDES) -c $< -o $@

.PHONY: clean
clean:
	rm -f $(EXE) $(OBJ) $(DEP)

print-%:
	@echo "$* = [$($*)]"

$(DEP):

-include $(DEP)
