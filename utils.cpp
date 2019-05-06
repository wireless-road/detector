/*
 * Copyright © 2019 Tyler J. Brooks <tylerjbrooks@digispeaker.com> <https://www.digispeaker.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * <http://www.apache.org/licenses/LICENSE-2.0>
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Try './tracker -h' for usage.
 */

#include "utils.h"

namespace tracker {

static void yuv420_to_yuv420(
    unsigned char* src, unsigned int src_width, unsigned int src_height,
    unsigned char* dst, unsigned int dst_width, unsigned int dst_height)
{
  unsigned int   src_blk     = src_width * src_height;
  unsigned int   src_qtr_blk = src_blk / 4;
  unsigned char* src_y       = src;
  unsigned char* src_u       = src + src_blk;
  unsigned char* src_v       = src + src_blk + src_qtr_blk;

  unsigned int   dst_blk     = dst_width * dst_height;
  unsigned int   dst_qtr_blk = dst_blk / 4;
  unsigned char* dst_y       = dst;
  unsigned char* dst_u       = dst + dst_blk;
  unsigned char* dst_v       = dst + dst_blk + dst_qtr_blk;

  std::memset(dst_y, 0, dst_width * dst_height * 3 / 2);

  for (unsigned int i = 0; i < dst_height; i++) {
    std::memcpy(dst_y, src_y, src_width);
    src_y += src_width;
    dst_y += dst_width;
  }

  for (unsigned int i = 0; i < dst_height / 2; i++) {
    std::memcpy(dst_u, src_u, src_width / 2);
    std::memcpy(dst_v, src_v, src_width / 2);
    src_u += src_width / 2;
    src_v += src_width / 2;
    dst_u += dst_width / 2;
    dst_v += dst_width / 2;
  }
}

static void yuyv_or_yvyu_to_yuv420(
    unsigned char* source, unsigned int src_width, unsigned int src_height,
    unsigned char* dst,    unsigned int dst_width, unsigned int dst_height, bool flip) {

  struct yuyv {
    unsigned char y0;
    unsigned char u;
    unsigned char y1;
    unsigned char v;
  };
  struct yuv420_y {
    unsigned char y0;
    unsigned char y1;
    unsigned char y2;
    unsigned char y3;
  };
  struct yuv420_u {
    unsigned char u;
  };
  struct yuv420_v {
    unsigned char v;
  };

  unsigned int dst_blk = dst_width * dst_height;
  unsigned int dst_qtr_blk = dst_blk / 4;

  unsigned int diff = dst_width - src_width;

  struct yuyv*     src    = (struct yuyv*)source;
  struct yuv420_y* dst_y  = (struct yuv420_y*)(dst);
  struct yuv420_u* dst_u  = (struct yuv420_u*)(dst + dst_blk);
  struct yuv420_v* dst_v  = (struct yuv420_v*)(dst + dst_blk + dst_qtr_blk);

  std::memset(dst_y, 0, dst_width * dst_height * 3 / 2);

  for (unsigned int i = 1; i <= src_height; i++) {
    for (unsigned int j = 1; j <= src_width / 2; j++) {
      if (j % 2) {
        dst_y->y0 = src->y0;
        dst_y->y1 = src->y1;
      } else {
        dst_y->y2 = src->y0;
        dst_y->y3 = src->y1;
        dst_y++;
      }

      if (i % 2) {
        dst_u->u = (flip) ? src->v : src->u;
        dst_v->v = (flip) ? src->u : src->v;
        dst_u++;
        dst_v++;
      }
      src++;
    }
    dst_y += diff;
    dst_u += diff / 2;
    dst_v += diff / 2;
  }
}

static void yuyv_to_yuv420(
    unsigned char* src, unsigned int src_width, unsigned int src_height,
    unsigned char* dst, unsigned int dst_width, unsigned int dst_height) {
  yuyv_or_yvyu_to_yuv420(src, src_width, src_height, dst, dst_width, dst_height, false);
}

static void yvyu_to_yuv420(
    unsigned char* src, unsigned int src_width, unsigned int src_height,
    unsigned char* dst, unsigned int dst_width, unsigned int dst_height) {
  yuyv_or_yvyu_to_yuv420(src, src_width, src_height, dst, dst_width, dst_height, true);
}

static void nv12_or_nv21_to_yuv420(
    unsigned char* src, unsigned int src_width, unsigned int src_height,
    unsigned char* dst, unsigned int dst_width, unsigned int dst_height, bool flip) {

  struct uv {
    unsigned char u;
    unsigned char v;
  };

  unsigned int   dst_blk     = dst_width * dst_height;
  unsigned int   dst_qtr_blk = dst_blk / 4;

  unsigned int   src_blk     = src_width * src_height;

  unsigned int   diff        = dst_width - src_width;

  unsigned char* src_y  = src;
  struct uv*     src_uv = (struct uv*)(src + src_blk);

  unsigned char* dst_y  = dst;
  unsigned char* dst_u  = dst + dst_blk;
  unsigned char* dst_v  = dst + dst_blk + dst_qtr_blk;

  std::memset(dst_y, 0, dst_width * dst_height * 3 / 2);

  for (unsigned int i = 0; i < src_height; i++) {
    std::memcpy(dst_y, src_y, src_width);
    src_y += src_width;
    dst_y += dst_width;
  }

  // de-interlace uv data
  for (unsigned int i = 0; i < src_height / 2; i++) {
    for (unsigned int j = 0; j < src_width / 4; j++) {
      *dst_u++ = (flip) ? src_uv->v : src_uv->u;
      *dst_v++ = (flip) ? src_uv->u : src_uv->v;
      src_uv++;
    }
    dst_u += diff / 2;
    dst_v += diff / 2;
  }
}

static void nv12_to_yuv420(
    unsigned char* src, unsigned int src_width, unsigned int src_height,
    unsigned char* dst, unsigned int dst_width, unsigned int dst_height) {
  nv12_or_nv21_to_yuv420(src, src_width, src_height, dst, dst_width, dst_height, false);
}

static void nv21_to_yuv420(
    unsigned char* src, unsigned int src_width, unsigned int src_height,
    unsigned char* dst, unsigned int dst_width, unsigned int dst_height) {
  nv12_or_nv21_to_yuv420(src, src_width, src_height, dst, dst_width, dst_height, true);
}

void convert_to_yuv420(int fmt, 
    unsigned char* src, unsigned int src_width, unsigned int src_height,
    unsigned char* dst, unsigned int dst_width, unsigned int dst_height) {

  switch (fmt) {
    case V4L2_PIX_FMT_YUV420:
      yuv420_to_yuv420(src, src_width, src_height, dst, dst_width, dst_height);
      break;
    case V4L2_PIX_FMT_YUYV:
      yuyv_to_yuv420(src, src_width, src_height, dst, dst_width, dst_height);
      break;
    case V4L2_PIX_FMT_YVYU:
      yvyu_to_yuv420(src, src_width, src_height, dst, dst_width, dst_height);
      break;
    case V4L2_PIX_FMT_NV12:
      nv12_to_yuv420(src, src_width, src_height, dst, dst_width, dst_height);
      break;
    case V4L2_PIX_FMT_NV21:
      nv21_to_yuv420(src, src_width, src_height, dst, dst_width, dst_height);
      break;
  }
}

// this converstion is done with integers with the byte values left shifted 
// by 10 places to give it precision.  So the max rgb value would be = 2 ^ 18 - 1.
#define RGB_MAXVAL  262143  // = 2 ^ 18 - 1
#define RGB_MAX(a,b) (((a)>(b))?(a):(b))
#define RGB_MIN(a,b) (((a)<(b))?(a):(b))
static unsigned char* yuv2rgb(unsigned char* dst, int y, int u, int v) {
  y -= 16;
  u -= 128;
  v -= 128;

  int r = 1192 * y + 1634 * v;
  int g = 1192 * y - 833 * v - 400 * u;
  int b = 1192 * y + 2066 * u;

  r = RGB_MIN(RGB_MAXVAL, RGB_MAX(0,r));
  g = RGB_MIN(RGB_MAXVAL, RGB_MAX(0,g));
  b = RGB_MIN(RGB_MAXVAL, RGB_MAX(0,b));

  *dst++ = (r >> 10) & 0xff;
  *dst++ = (g >> 10) & 0xff;
  *dst++ = (b >> 10) & 0xff;
  return dst;
}

void convert_yuv420_to_rgb24(unsigned char* src, unsigned char* dst, 
    unsigned int width, unsigned int height) {

  unsigned int y_size = width * height;
  unsigned int uv_size = y_size / 4;

  unsigned char* pY = src;
  unsigned char* pU = src + y_size;
  unsigned char* pV = src + y_size + uv_size;

  for (unsigned int y = 0; y < height; y++) {
    for (unsigned int x = 0; x < width; x++) {
      dst = yuv2rgb(dst, pY[x], pU[x/2], pV[x/2]);
    }
    pY += width;
    if (y % 2) {
      pU += width / 2;
      pV += width / 2;
    }
  }
}

bool drawYUVHorizontalLine(unsigned int thick, unsigned char* start, 
    unsigned int stride, unsigned int width, unsigned char val) {

  for (unsigned int i = 0; i < thick; i++) {
    std::memset(start, val, width);
    start += stride;
  }
  return true;
}

bool drawYUVVerticalLine(unsigned int thick, unsigned char* start, 
    unsigned int stride, unsigned int height, unsigned char val) {

  for (unsigned int i = 0; i < height; i++) {
    std::memset(start, val, thick);
    start += stride;
  }
  return true;
}

bool drawYUVBox(unsigned int thick,
    unsigned char* dst_y, unsigned int dst_stride_y,
    unsigned char* dst_u, unsigned int dst_stride_u,
    unsigned char* dst_v, unsigned int dst_stride_v,
    unsigned int x, 
    unsigned int y, 
    unsigned int width, 
    unsigned int height,
    unsigned char val_y, 
    unsigned char val_u, 
    unsigned char val_v) {
  unsigned int half_thick = thick / 2;
  unsigned int half_width = width / 2;
  unsigned int half_height = height / 2;
  unsigned char* start_y = dst_y + y * dst_stride_y + x;
  unsigned char* start_u = dst_u + (y / 2) * dst_stride_u + (x / 2);
  unsigned char* start_v = dst_v + (y / 2) * dst_stride_v + (x / 2);
  if (!dst_y || !dst_u || !dst_v || thick == 0) {
    return false;
  }
  if (width == 0 || height == 0) {
    return true;
  }

  // draw horizontal lines
  drawYUVHorizontalLine(thick, start_y, dst_stride_y, width, val_y);
  drawYUVHorizontalLine(thick, start_y + (height - thick) * dst_stride_y, 
      dst_stride_y, width, val_y);

  drawYUVHorizontalLine(half_thick, start_u, dst_stride_u, half_width, val_u);
  drawYUVHorizontalLine(half_thick, start_u + (half_height - half_thick) * dst_stride_u, 
      dst_stride_u, half_width, val_u);

  drawYUVHorizontalLine(half_thick, start_v, dst_stride_v, half_width, val_v);
  drawYUVHorizontalLine(half_thick, start_v + (half_height - half_thick) * dst_stride_v, 
      dst_stride_v, half_width, val_v);

  // draw vertical lines
  drawYUVVerticalLine(thick, start_y, dst_stride_y, height, val_y);
  drawYUVVerticalLine(thick, start_y + width - thick, 
      dst_stride_y, height, val_y);

  drawYUVVerticalLine(half_thick, start_u, dst_stride_u, half_height, val_u);
  drawYUVVerticalLine(half_thick, start_u + half_width - half_thick,
      dst_stride_u, half_height, val_u);

  drawYUVVerticalLine(half_thick, start_v, dst_stride_v, half_height, val_v);
  drawYUVVerticalLine(half_thick, start_v + half_width - half_thick,
      dst_stride_v, half_height, val_v);

  return true;
}

struct draw_rgb {
  unsigned char r;
  unsigned char g;
  unsigned char b;
};
static void drawRGBHorizontalLine(unsigned int thick,
    struct draw_rgb* start, unsigned int len, unsigned int stride,
    unsigned char val_r, unsigned char val_g, unsigned char val_b) {

  for (unsigned int t = 0; t < thick; t++) {
    for (unsigned int l = 0; l < len; l++) {
      struct draw_rgb* p = start + l;
      p->r = val_r;
      p->g = val_g;
      p->b = val_b;
    }
    start += stride;
  }
}
static void drawRGBVerticalLine(unsigned int thick,
    struct draw_rgb* start, unsigned int len, unsigned int stride, 
    unsigned char val_r, unsigned char val_g, unsigned char val_b) {
  for (unsigned int l = 0; l < len; l++) {
    for (unsigned int t = 0; t < thick; t++) {
      struct draw_rgb* p = start + t;
      p->r = val_r;
      p->g = val_g;
      p->b = val_b;
    }
    start += stride;
  }
}
bool drawRGBBox(unsigned int thick, 
    unsigned char* dst, unsigned int width, unsigned int height,
    unsigned int x, unsigned int y, unsigned int w, unsigned int h,
    unsigned char val_r, unsigned char val_g, unsigned char val_b) {

//  dbgMsg("width: %d, height: %d\n", width, height);
//  dbgMsg("x: %d, y: %d, w: %d, h: %d\n", x, y, w, h);
//  dbgMsg("val_r: 0x%x, val_g: 0x%x, val_b: 0x%x\n", val_r, val_g, val_b);

  if (!dst) {
    return false;
  }
  if (width == 0 || height == 0) {
    return true;
  }

  unsigned int size_rgb = sizeof(struct draw_rgb);
  unsigned int stride_y   = width * size_rgb;
  struct draw_rgb* start  = nullptr;

  // draw horizontal lines
  start = (struct draw_rgb*)(dst + (y * stride_y) + (x * size_rgb));
  drawRGBHorizontalLine(thick, start, w, width, val_r, val_g, val_b);
  start += (h - thick) * width;
  drawRGBHorizontalLine(thick, start, w, width, val_r, val_g, val_b);

  // draw vertical lines
  start = (struct draw_rgb*)(dst + (y * stride_y) + (x * size_rgb));
  drawRGBVerticalLine(thick, start, h, width, val_r, val_g, val_b);
  start += (w - thick);
  drawRGBVerticalLine(thick, start, h, width, val_r, val_g, val_b);

  return true;
}

const char* BufTypeToStr(unsigned int bt) {
  switch (bt) {
    case V4L2_BUF_TYPE_VIDEO_CAPTURE:
      return "capture";
    case V4L2_BUF_TYPE_VIDEO_OUTPUT:
      return "output";
    case V4L2_BUF_TYPE_VIDEO_OVERLAY:
      return "overlay";
    case V4L2_BUF_TYPE_VBI_CAPTURE:
      return "vbi_capture";
    case V4L2_BUF_TYPE_VBI_OUTPUT:
      return "vbi_output";
    case V4L2_BUF_TYPE_SLICED_VBI_CAPTURE:
      return "sliced_vbi_capture";
    case V4L2_BUF_TYPE_SLICED_VBI_OUTPUT:
      return "sliced_vbi_output";
    case V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY:
      return "output_overlay";
    case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
      return "captuer_mplane";
    case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
      return "output_mplane";
    case V4L2_BUF_TYPE_PRIVATE:
      return "private";
    case V4L2_BUF_TYPE_SDR_CAPTURE:
      return "sdr_capture";
    case V4L2_BUF_TYPE_SDR_OUTPUT:
      return "sdr_output";
  }
  return "unknown";
}

const char* BufFieldToStr(unsigned int bf) {
  switch (bf) {
    case V4L2_FIELD_ANY:            return "any";
    case V4L2_FIELD_NONE:           return "none";
    case V4L2_FIELD_TOP:            return "top";
    case V4L2_FIELD_BOTTOM:         return "bottom";
    case V4L2_FIELD_INTERLACED:     return "interlaced";
    case V4L2_FIELD_SEQ_TB:         return "seq_tb";
    case V4L2_FIELD_SEQ_BT:         return "seq_bt";
    case V4L2_FIELD_ALTERNATE:      return "alternate";
    case V4L2_FIELD_INTERLACED_TB:  return "interlaced_tb";
    case V4L2_FIELD_INTERLACED_BT:  return "interlaced_bt";
  }
  return "unknown";
}

const char* BufTimecodeTypeToStr(unsigned int tt) {
  switch (tt) {
    case V4L2_TC_TYPE_24FPS:  return "24fps";
    case V4L2_TC_TYPE_25FPS:  return "25fps";
    case V4L2_TC_TYPE_30FPS:  return "30fps";
    case V4L2_TC_TYPE_50FPS:  return "50fps";
    case V4L2_TC_TYPE_60FPS:  return "60fps";
  }
  return "unknown";
}
 
const char* BufMemoryToStr(unsigned int bm) {
  switch (bm) {
    case V4L2_MEMORY_MMAP:    return "mmap";
    case V4L2_MEMORY_USERPTR: return "userptr";
    case V4L2_MEMORY_OVERLAY: return "overlay";
    case V4L2_MEMORY_DMABUF:  return "dmabuf";
  }
  return "unknown";
}

const char* ColorspaceToStr(unsigned int cs) {
  switch (cs) {
    case V4L2_COLORSPACE_SMPTE170M: return "smpte170m";
    case V4L2_COLORSPACE_SMPTE240M: return "smpte240m";
    case V4L2_COLORSPACE_REC709:    return "rec709";
    case V4L2_COLORSPACE_BT878:     return "bt878";
    case V4L2_COLORSPACE_470_SYSTEM_M:  return "470_system_m";
    case V4L2_COLORSPACE_470_SYSTEM_BG: return "470_system_bg";
    case V4L2_COLORSPACE_JPEG:      return "jpeg";
    case V4L2_COLORSPACE_SRGB:      return "srgb";
    case V4L2_COLORSPACE_DEFAULT:   return "default";
    case V4L2_COLORSPACE_ADOBERGB:  return "adobergb";
    case V4L2_COLORSPACE_BT2020:    return "bt2020";
    case V4L2_COLORSPACE_RAW:       return "raw";
    case V4L2_COLORSPACE_DCI_P3:    return "dci_p3";
  }
  return "unknown";
}

const char* PixelFormatToStr(unsigned int pix) {
  switch (pix) {
    case V4L2_PIX_FMT_RGB332:       return "rgb332";
    case V4L2_PIX_FMT_RGB444:       return "rgb444";
    case V4L2_PIX_FMT_RGB555:       return "rgb555";
    case V4L2_PIX_FMT_RGB565:       return "rgb565";
    case V4L2_PIX_FMT_RGB555X:      return "rgb555x";
    case V4L2_PIX_FMT_RGB565X:      return "rgb565x";
    case V4L2_PIX_FMT_BGR666:       return "bgr666";
    case V4L2_PIX_FMT_BGR24:        return "bgr24";
    case V4L2_PIX_FMT_RGB24:        return "rgb24";
    case V4L2_PIX_FMT_BGR32:        return "bgr32";
    case V4L2_PIX_FMT_RGB32:        return "rgb32";
    case V4L2_PIX_FMT_GREY:         return "grey";
    case V4L2_PIX_FMT_Y4:           return "y4";
    case V4L2_PIX_FMT_Y6:           return "y6";
    case V4L2_PIX_FMT_Y10:          return "y10";
    case V4L2_PIX_FMT_Y12:          return "y12";
    case V4L2_PIX_FMT_Y16:          return "y16";
    case V4L2_PIX_FMT_Y10BPACK:     return "y10bpack";
    case V4L2_PIX_FMT_PAL8:         return "pal8";
    case V4L2_PIX_FMT_YVU410:       return "yvu410";
    case V4L2_PIX_FMT_YVU420:       return "yvu420";
    case V4L2_PIX_FMT_YUYV:         return "yuyv";
    case V4L2_PIX_FMT_YYUV:         return "yyuv";
    case V4L2_PIX_FMT_YVYU:         return "yvyu";
    case V4L2_PIX_FMT_UYVY:         return "uyvy";
    case V4L2_PIX_FMT_VYUY:         return "vyuy";
    case V4L2_PIX_FMT_YUV422P:      return "yuv422p";
    case V4L2_PIX_FMT_YUV411P:      return "yuv411p";
    case V4L2_PIX_FMT_Y41P:         return "y41p";
    case V4L2_PIX_FMT_YUV444:       return "yuv444";
    case V4L2_PIX_FMT_YUV555:       return "yuv555";
    case V4L2_PIX_FMT_YUV565:       return "yuv565";
    case V4L2_PIX_FMT_YUV32:        return "yuv32";
    case V4L2_PIX_FMT_YUV410:       return "yuv410";
    case V4L2_PIX_FMT_YUV420:       return "yuv420";
    case V4L2_PIX_FMT_HI240:        return "hi240";
    case V4L2_PIX_FMT_HM12:         return "hm12";
    case V4L2_PIX_FMT_M420:         return "m420";
    case V4L2_PIX_FMT_NV12:         return "nv12";
    case V4L2_PIX_FMT_NV21:         return "nv21";
    case V4L2_PIX_FMT_NV16:         return "nv16";
    case V4L2_PIX_FMT_NV61:         return "nv61";
    case V4L2_PIX_FMT_NV24:         return "nv24";
    case V4L2_PIX_FMT_NV42:         return "nv42";
    case V4L2_PIX_FMT_NV12M:        return "nv12m";
    case V4L2_PIX_FMT_NV12MT:       return "nv12mt";
    case V4L2_PIX_FMT_YUV420M:      return "yuv420m";
    case V4L2_PIX_FMT_SBGGR8:       return "sbggr8";
    case V4L2_PIX_FMT_SGBRG8:       return "sgbrg8";
    case V4L2_PIX_FMT_SGRBG8:       return "sgrbg8";
    case V4L2_PIX_FMT_SRGGB8:       return "srggb8";
    case V4L2_PIX_FMT_SBGGR10:      return "sbggr10";
    case V4L2_PIX_FMT_SGBRG10:      return "sgbrg10";
    case V4L2_PIX_FMT_SGRBG10:      return "sgrbg10";
    case V4L2_PIX_FMT_SRGGB10:      return "srggb10";
    case V4L2_PIX_FMT_SBGGR12:      return "sbggr12";
    case V4L2_PIX_FMT_SGBRG12:      return "sgbrg12";
    case V4L2_PIX_FMT_SGRBG12:      return "sgrbg12";
    case V4L2_PIX_FMT_SRGGB12:      return "srggb12";
    case V4L2_PIX_FMT_SGRBG10DPCM8: return "sgrbg10dpcm8";
    case V4L2_PIX_FMT_SBGGR16:      return "sbggr16";
    case V4L2_PIX_FMT_MJPEG:        return "mjpeg";
    case V4L2_PIX_FMT_JPEG:         return "jpeg";
    case V4L2_PIX_FMT_DV:           return "dv";
    case V4L2_PIX_FMT_MPEG:         return "mpeg";
    case V4L2_PIX_FMT_H264:         return "h264";
    case V4L2_PIX_FMT_H264_NO_SC:   return "h264_no_sc";
    case V4L2_PIX_FMT_H263:         return "h263";
    case V4L2_PIX_FMT_MPEG1:        return "mpeg1";
    case V4L2_PIX_FMT_MPEG2:        return "mpeg2";
    case V4L2_PIX_FMT_MPEG4:        return "mpeg4";
    case V4L2_PIX_FMT_XVID:         return "xvid";
    case V4L2_PIX_FMT_VC1_ANNEX_G:  return "vc1_annex_g";
    case V4L2_PIX_FMT_VC1_ANNEX_L:  return "vc1_annex_l";
    case V4L2_PIX_FMT_CPIA1:        return "cpia1";
    case V4L2_PIX_FMT_WNVA:         return "wnva";
    case V4L2_PIX_FMT_SN9C10X:      return "sn9c10x";
    case V4L2_PIX_FMT_SN9C20X_I420: return "sn9c20x_i420";
    case V4L2_PIX_FMT_PWC1:         return "pwc1";
    case V4L2_PIX_FMT_PWC2:         return "pwc2";
    case V4L2_PIX_FMT_ET61X251:     return "et61x251";
    case V4L2_PIX_FMT_SPCA501:      return "spca501";
    case V4L2_PIX_FMT_SPCA505:      return "spca505";
    case V4L2_PIX_FMT_SPCA508:      return "spca508";
    case V4L2_PIX_FMT_SPCA561:      return "spca561";
    case V4L2_PIX_FMT_PAC207:       return "pac207";
    case V4L2_PIX_FMT_MR97310A:     return "mr97310a";
    case V4L2_PIX_FMT_JL2005BCD:    return "jl2005bcd";
    case V4L2_PIX_FMT_SN9C2028:     return "sn9c2028";
    case V4L2_PIX_FMT_SQ905C:       return "sq905c";
    case V4L2_PIX_FMT_PJPG:         return "pjpg";
    case V4L2_PIX_FMT_OV511:        return "ov511";
    case V4L2_PIX_FMT_OV518:        return "ov518";
    case V4L2_PIX_FMT_STV0680:      return "stv0680";
    case V4L2_PIX_FMT_TM6000:       return "tm6000";
    case V4L2_PIX_FMT_CIT_YYVYUY:   return "cit_yyvyuy";
    case V4L2_PIX_FMT_KONICA420:    return "konica420";
    case V4L2_PIX_FMT_JPGL:         return "jpgl";
    case V4L2_PIX_FMT_SE401:        return "se401";

    default: return "unknown";
  }
  return "unknown";
}

} // namespace tracker

