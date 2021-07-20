#ifndef FRAME_CONVERT_H
#define FRAME_CONVERT_H

namespace detector {

/**
 * Converts to RGB24 from other pixformats;
 */
class CSConvertor {
public:
   CSConvertor();
   
   ~CSConvertor();

   bool setup(unsigned pix_format, unsigned width, unsigned height);

   bool convert(const unsigned char *src, unsigned char *dst, size_t src_len, size_t dst_len);

private:
   CSConvertor(const CSConvertor &);
   class Impl;

   Impl *impl;
};
/*
typedef bool (* frame_convert_fun)(
   const unsigned char *src, unsigned src_len,
   unsigned char *dst, unsigned dst_len,
   int width, int height);

frame_convert_fun getRGB24Convert(unsigned int pix_fmt_);
*/

};

#endif

