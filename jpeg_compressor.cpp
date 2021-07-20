#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <jpeglib.h>
#include "jpeg_compressor.h"


class JpegCompressor::Impl {
public:
    Impl() {
        cinfo.err = jpeg_std_error(&jerr);
        jpeg_create_compress(&cinfo);
        cinfo.input_components = 3;           /* # of color components per pixel */
        cinfo.in_color_space = JCS_RGB;       /* colorspace of input image */
    }

    ~Impl() {
        jpeg_destroy_compress(&cinfo);
    }

    bool
    compressToFile(unsigned width, unsigned height, const unsigned char *raw_image, const std::string &filename) {
        cinfo.image_width = width;
        cinfo.image_height = height;
        FILE *outfile = fopen(filename.c_str(), "wb");

        jpeg_stdio_dest(&cinfo, outfile);
        jpeg_set_defaults(&cinfo);
        jpeg_set_quality(&cinfo, 75, TRUE);

        jpeg_start_compress(&cinfo, TRUE);
        JSAMPROW row_pointer[1];
        int row_stride = width * 3;
        while (cinfo.next_scanline < cinfo.image_height) {
            row_pointer[0] = const_cast<JSAMPROW>(raw_image + cinfo.next_scanline * row_stride);
            (void)jpeg_write_scanlines(&cinfo, row_pointer, 1);
        }

        jpeg_finish_compress(&cinfo);

        fclose(outfile);
        return true;
    }
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
};

JpegCompressor::JpegCompressor() {
    impl = new Impl();
}

JpegCompressor::~JpegCompressor() {
    delete impl;
}


bool
JpegCompressor::compressToFile(unsigned width, unsigned height, const unsigned char *raw_image, 
    const std::string &filename) {

    return impl->compressToFile(width, height, raw_image, filename);
}
