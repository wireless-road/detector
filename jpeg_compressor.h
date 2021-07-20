#ifndef __JPEG_COMPRESSOR_H__
#define __JPEG_COMPRESSOR_H__
#include <sys/types.h>
#include <string>

class JpegCompressor {
public:
    JpegCompressor();

    ~JpegCompressor();


    bool compressToFile(unsigned width, unsigned height,
	 const unsigned char *raw_image, const std::string &filename);
private:
    JpegCompressor(const JpegCompressor &);
    class Impl;
    Impl *impl;
};

#endif // __JPEG_COMPRESSOR_H__
