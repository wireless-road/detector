#include <turbojpeg.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include "jpeg_compressor.h"


class JpegCompressor::Impl {
public:
    Impl() {
        instance = tjInitCompress();
        fprintf(stderr, "Instanciated instance %p\n", instance);
    }

    ~Impl() {
        fprintf(stderr, "will destroy instance %p\n", instance);
        tjDestroy(instance);
    }

    tjhandle instance;
};

JpegCompressor::JpegCompressor() {
    impl = new Impl();
}

JpegCompressor::~JpegCompressor() {
    delete impl;
}

JpegCompressor::Result
JpegCompressor::compress(unsigned width, unsigned height, const unsigned char *raw_image) {
    JpegCompressor::Result result;

    if (tjCompress2(impl->instance, raw_image, width, 0, height, TJPF_RGB,
        &result.data_, &result.size_, TJSAMP_444, 75, 0) < 0) {
        fprintf(stderr, "Failed to compress: %s\n", tjGetErrorStr2(impl->instance));
        result.data_ = NULL;
        result.size_ = 0;
    }
    return result;
}

bool
JpegCompressor::compressToFile(unsigned width, unsigned height, const unsigned char *raw_image, 
    const std::string &filename) {
    Result jpeg = compress(width, height, raw_image);
    if (!jpeg.data()) {
        return false;
    }

    int fd = open(filename.c_str(), O_CREAT|O_TRUNC|O_WRONLY, 0666);

    if (fd < 0) {
        jpeg.free();
        return false;
    }

    bool res = write(fd, jpeg.data(), jpeg.size()) == (ssize_t)jpeg.size();

    close(fd);
    jpeg.free();
    return res;
}

JpegCompressor::Result::Result() {
    data_ = 0;
    size_ = 0;
}

void
JpegCompressor::Result::free() {
    tjFree(data_);
    data_ = 0;
}

unsigned long
JpegCompressor::Result::size() const {
    return size_;
}

const unsigned char *
JpegCompressor::Result::data() const {
    return data_;
}
