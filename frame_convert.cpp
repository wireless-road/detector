#include <cstring>
#include <vector>
#include <linux/videodev2.h>
#include <linux/v4l2-controls.h>
#include <libyuv.h>
#include "frame_convert.h"

#include <stdio.h>
namespace detector {

bool
uyvy2rgb(
    const unsigned char *src, unsigned src_len,
    unsigned char *dst, unsigned dst_len,
    int width, int height) {

    unsigned argb_sz = width * height * 4;
    std::vector<unsigned char> argb_buf(argb_sz);
    argb_buf.resize(argb_sz);

    libyuv::UYVYToARGB(src, width * 2, argb_buf.data(), width * 4, width, height);
    libyuv::ARGBToRAW(argb_buf.data(), width * 4, dst, width * 3, width, height);
    return true;
}


class CSConvertor::Impl {
public:
    unsigned width_;
    unsigned height_;
    unsigned target_fmt_;
    bool (detector::CSConvertor::Impl::* cvt)(const unsigned char*, unsigned char*, size_t, size_t);
    
    std::vector<unsigned char> interm_buf_;        
    bool setup(unsigned pix_format, unsigned width, unsigned height) {
        width_ = width;
        height_ = height;
        
        switch(pix_format) {
        case V4L2_PIX_FMT_RGB24:
            cvt = &Impl::rgb2rgb;
            break;
        case V4L2_PIX_FMT_UYVY:
            interm_buf_.resize(width_ * height_ * 4);
            cvt = &Impl::uyvy2rgb;
            break;
        default:
            return false;
        }
        target_fmt_ = pix_format;
        return true;
    }

    bool rgb2rgb(const unsigned char *src, unsigned char *dst, size_t src_len, size_t dst_len) {
        if (src_len != dst_len) {
            return false;
        }
        std::memcpy(dst, src, src_len);
        return true;
    }

    bool uyvy2rgb(const unsigned char *src, unsigned char *dst, size_t src_len, size_t dst_len) {
        if (src_len != width_ * height_ * 2 ) {
            return false;
        } 

        if (dst_len != width_ * height_ * 3) {
            return false;
        }
        libyuv::UYVYToARGB(src, width_ * 2, interm_buf_.data(), width_ * 4, width_, height_);
        libyuv::ARGBToRAW(interm_buf_.data(), width_ * 4, dst, width_ * 3, width_, height_);
        return true;
    }
};


CSConvertor::CSConvertor() {
    impl = new Impl();
}

bool
CSConvertor::setup(unsigned pix_format, unsigned width, unsigned height) {
    fprintf(stderr, "SETUP fmt: %d w: %d h: %d\n", pix_format, width, height);
    return impl->setup(pix_format, width, height);
}

bool
CSConvertor::convert(const unsigned char *src, unsigned char *dst, size_t src_len, size_t dst_len) {
    return ((*impl).*(impl->cvt))(src, dst, src_len, dst_len);
}

CSConvertor::~CSConvertor() {
    delete impl;
}
   
};
