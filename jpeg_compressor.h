#ifndef __JPEG_COMPRESSOR_H__
#define __JPEG_COMPRESSOR_H__
#include <sys/types.h>
#include <string>

class JpegCompressor {
public:
    JpegCompressor();

    ~JpegCompressor();


    class Result {
        friend class JpegCompressor;
    public:
        const unsigned char *data() const;

        unsigned long size() const;

        void free();
    private:
        Result();

        unsigned char *data_;
        unsigned long size_;
    };

    Result compress(unsigned width, unsigned height, const unsigned char *raw_image);

    bool compressToFile(unsigned width, unsigned height,
	 const unsigned char *raw_image, const std::string &filename);
private:
    JpegCompressor(const JpegCompressor &);
    class Impl;
    Impl *impl;
};

#endif // __JPEG_COMPRESSOR_H__
