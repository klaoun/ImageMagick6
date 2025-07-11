/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%                QQQ   U   U   AAA   N   N  TTTTT  U   U  M   M               %
%               Q   Q  U   U  A   A  NN  N    T    U   U  MM MM               %
%               Q   Q  U   U  AAAAA  N N N    T    U   U  M M M               %
%               Q  QQ  U   U  A   A  N  NN    T    U   U  M   M               %
%                QQQQ   UUU   A   A  N   N    T     UUU   M   M               %
%                                                                             %
%                   IIIII  M   M  PPPP    OOO   RRRR   TTTTT                  %
%                     I    MM MM  P   P  O   O  R   R    T                    %
%                     I    M M M  PPPP   O   O  RRRR     T                    %
%                     I    M   M  P      O   O  R R      T                    %
%                   IIIII  M   M  P       OOO   R  R     T                    %
%                                                                             %
%                 MagickCore Methods to Import Quantum Pixels                 %
%                                                                             %
%                             Software Design                                 %
%                                  Cristy                                     %
%                               October 1998                                  %
%                                                                             %
%                                                                             %
%  Copyright 1999 ImageMagick Studio LLC, a non-profit organization           %
%  dedicated to making software imaging solutions freely available.           %
%                                                                             %
%  You may not use this file except in compliance with the License.  You may  %
%  obtain a copy of the License at                                            %
%                                                                             %
%    https://imagemagick.org/script/license.php                               %
%                                                                             %
%  Unless required by applicable law or agreed to in writing, software        %
%  distributed under the License is distributed on an "AS IS" BASIS,          %
%  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   %
%  See the License for the specific language governing permissions and        %
%  limitations under the License.                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%
*/

/*
  Include declarations.
*/
#include "magick/studio.h"
#include "magick/property.h"
#include "magick/blob.h"
#include "magick/blob-private.h"
#include "magick/color-private.h"
#include "magick/exception.h"
#include "magick/exception-private.h"
#include "magick/cache.h"
#include "magick/constitute.h"
#include "magick/delegate.h"
#include "magick/geometry.h"
#include "magick/list.h"
#include "magick/magick.h"
#include "magick/memory_.h"
#include "magick/monitor.h"
#include "magick/option.h"
#include "magick/pixel.h"
#include "magick/pixel-private.h"
#include "magick/quantum.h"
#include "magick/quantum-private.h"
#include "magick/resource_.h"
#include "magick/semaphore.h"
#include "magick/statistic.h"
#include "magick/stream.h"
#include "magick/string_.h"
#include "magick/utility.h"

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   I m p o r t Q u a n t u m P i x e l s                                     %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  ImportQuantumPixels() transfers one or more pixel components from a user
%  supplied buffer into the image pixel cache of an image.  The pixels are
%  expected in network byte order.  It returns the number of imported pixels.
%
%  The format of the ImportQuantumPixels method is:
%
%      size_t ImportQuantumPixels(Image *image,CacheView *image_view,
%        const QuantumInfo *quantum_info,const QuantumType quantum_type,
%        const unsigned char *magick_restrict pixels,ExceptionInfo *exception)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o image_view: the image cache view.
%
%    o quantum_info: the quantum info.
%
%    o quantum_type: Declare which pixel components to transfer (red, green,
%      blue, opacity, RGB, or RGBA).
%
%    o pixels:  The pixel components are transferred from this buffer.
%
%    o exception: return any errors or warnings in this structure.
%
*/

static inline IndexPacket PushColormapIndex(const Image *image,
  const size_t index,MagickBooleanType *range_exception)
{
  if (index < image->colors)
    return((IndexPacket) index);
  *range_exception=MagickTrue;
  return((IndexPacket) 0);
}

static inline const unsigned char *PushDoublePixel(
  const QuantumInfo *quantum_info,const unsigned char *magick_restrict pixels,
  double *pixel)
{
  double
    *p;

  unsigned char
    quantum[8];

  if (quantum_info->endian == LSBEndian)
    {
      quantum[0]=(*pixels++);
      quantum[1]=(*pixels++);
      quantum[2]=(*pixels++);
      quantum[3]=(*pixels++);
      quantum[4]=(*pixels++);
      quantum[5]=(*pixels++);
      quantum[6]=(*pixels++);
      quantum[7]=(*pixels++);
    }
  else
    {
      quantum[7]=(*pixels++);
      quantum[6]=(*pixels++);
      quantum[5]=(*pixels++);
      quantum[4]=(*pixels++);
      quantum[3]=(*pixels++);
      quantum[2]=(*pixels++);
      quantum[1]=(*pixels++);
      quantum[0]=(*pixels++);
    }
  p=(double *) quantum;
  *pixel=(*p);
  *pixel-=quantum_info->minimum;
  *pixel*=quantum_info->scale;
  return(pixels);
}

static inline float ScaleFloatPixel(const QuantumInfo *quantum_info,
  const unsigned char *quantum)
{
  double
    pixel;

  pixel=(double) (*((float *) quantum));
  pixel-=quantum_info->minimum;
  pixel*=quantum_info->scale;
  if (pixel < (double) -FLT_MAX)
    return(-FLT_MAX);
  if (pixel > (double) FLT_MAX)
    return(FLT_MAX);
  return(pixel);
}

static inline const unsigned char *PushQuantumFloatPixel(
  const QuantumInfo *quantum_info,const unsigned char *magick_restrict pixels,
  float *pixel)
{
  unsigned char
    quantum[4];

  if (quantum_info->endian == LSBEndian)
    {
      quantum[0]=(*pixels++);
      quantum[1]=(*pixels++);
      quantum[2]=(*pixels++);
      quantum[3]=(*pixels++);
    }
  else
    {
      quantum[3]=(*pixels++);
      quantum[2]=(*pixels++);
      quantum[1]=(*pixels++);
      quantum[0]=(*pixels++);
    }
  *pixel=ScaleFloatPixel(quantum_info,quantum);
  return(pixels);
}

static inline const unsigned char *PushQuantumFloat24Pixel(
  const QuantumInfo *quantum_info,const unsigned char *magick_restrict pixels,
  float *pixel)
{
  unsigned char
    quantum[4];

  if (quantum_info->endian == LSBEndian)
    {
      quantum[0]=(*pixels++);
      quantum[1]=(*pixels++);
      quantum[2]=(*pixels++);
    }
  else
    {
      quantum[2]=(*pixels++);
      quantum[1]=(*pixels++);
      quantum[0]=(*pixels++);
    }
  if ((quantum[0] | quantum[1] | quantum[2]) == 0U)
    quantum[3]=0;
  else
    {
      unsigned char
        exponent,
        sign_bit;

      sign_bit=(quantum[2] & 0x80);
      exponent=(quantum[2] & 0x7F);
      if (exponent != 0)
        exponent=exponent-63+127;
      quantum[3]=sign_bit | (exponent >> 1);
      quantum[2]=((exponent & 1) << 7) | ((quantum[1] & 0xFE) >> 1);
      quantum[1]=((quantum[1] & 0x01) << 7) | ((quantum[0] & 0xFE) >> 1);
      quantum[0]=(quantum[0] & 0x01) << 7;
    }
  *pixel=ScaleFloatPixel(quantum_info,quantum);
  return(pixels);
}

static inline const unsigned char *PushQuantumPixel(QuantumInfo *quantum_info,
  const unsigned char *magick_restrict pixels,unsigned int *quantum)
{
  ssize_t
    i;

  size_t
    quantum_bits;

  *quantum=(QuantumAny) 0;
  for (i=(ssize_t) quantum_info->depth; i > 0L; )
  {
    if (quantum_info->state.bits == 0UL)
      {
        quantum_info->state.pixel=(*pixels++);
        quantum_info->state.bits=8UL;
      }
    quantum_bits=(size_t) i;
    if (quantum_bits > quantum_info->state.bits)
      quantum_bits=quantum_info->state.bits;
    i-=(ssize_t) quantum_bits;
    quantum_info->state.bits-=quantum_bits;
    if (quantum_bits < 64)
      *quantum=(unsigned int) (((MagickSizeType) *quantum << quantum_bits) |
        ((quantum_info->state.pixel >> quantum_info->state.bits) &~
        ((~0UL) << quantum_bits)));
  }
  return(pixels);
}

static inline const unsigned char *PushQuantumLongPixel(
  QuantumInfo *quantum_info,const unsigned char *magick_restrict pixels,
  unsigned int *quantum)
{
  ssize_t
    i;

  size_t
    quantum_bits;

  *quantum=0UL;
  for (i=(ssize_t) quantum_info->depth; i > 0; )
  {
    if (quantum_info->state.bits == 0)
      {
        pixels=PushLongPixel(quantum_info->endian,pixels,
          &quantum_info->state.pixel);
        quantum_info->state.bits=32U;
      }
    quantum_bits=(size_t) i;
    if (quantum_bits > quantum_info->state.bits)
      quantum_bits=quantum_info->state.bits;
    *quantum|=(((quantum_info->state.pixel >> (32U-quantum_info->state.bits)) &
      quantum_info->state.mask[quantum_bits]) << (quantum_info->depth-i));
    i-=(ssize_t) quantum_bits;
    quantum_info->state.bits-=quantum_bits;
  }
  return(pixels);
}

static void ImportAlphaQuantum(QuantumInfo *quantum_info,
  const MagickSizeType number_pixels,const unsigned char *magick_restrict p,
  PixelPacket *magick_restrict q)
{
  ssize_t
    x;

  unsigned int
    pixel;

  switch (quantum_info->depth)
  {
    case 8:
    {
      unsigned char
        pixel;

      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushCharPixel(p,&pixel);
        SetPixelAlpha(q,ScaleCharToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 16:
    {
      unsigned short
        pixel;

      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelAlpha(q,ClampToQuantum((double) QuantumRange*(double)
              HalfToSinglePrecision(pixel)));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelAlpha(q,ScaleShortToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 32:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelAlpha(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelAlpha(q,ScaleLongToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 24:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelAlpha(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    case 64:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          double
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelAlpha(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    default:
    {
      QuantumAny
        range;

      range=GetQuantumRange(quantum_info->depth);
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelAlpha(q,ScaleAnyToQuantum(pixel,range));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
  }
}

static void ImportBGRQuantum(QuantumInfo *quantum_info,
  const MagickSizeType number_pixels,const unsigned char *magick_restrict p,
  PixelPacket *magick_restrict q)
{
  QuantumAny
    range;

  ssize_t
    x;

  ssize_t
    bit;

  unsigned int
    pixel;

  switch (quantum_info->depth)
  {
    case 8:
    {
      unsigned char
        pixel;

      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushCharPixel(p,&pixel);
        SetPixelBlue(q,ScaleCharToQuantum(pixel));
        p=PushCharPixel(p,&pixel);
        SetPixelGreen(q,ScaleCharToQuantum(pixel));
        p=PushCharPixel(p,&pixel);
        SetPixelRed(q,ScaleCharToQuantum(pixel));
        SetPixelOpacity(q,OpaqueOpacity);
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 10:
    {
      range=GetQuantumRange(quantum_info->depth);
      if (quantum_info->pack == MagickFalse)
        {
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushLongPixel(quantum_info->endian,p,&pixel);
            SetPixelRed(q,ScaleAnyToQuantum((pixel >> 22) & 0x3ff,range));
            SetPixelGreen(q,ScaleAnyToQuantum((pixel >> 12) & 0x3ff,range));
            SetPixelBlue(q,ScaleAnyToQuantum((pixel >> 2) & 0x3ff,range));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      if (quantum_info->quantum == 32U)
        {
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumLongPixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ScaleAnyToQuantum(pixel,range));
            p=PushQuantumLongPixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ScaleAnyToQuantum(pixel,range));
            p=PushQuantumLongPixel(quantum_info,p,&pixel);
            SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelBlue(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelGreen(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
        q++;
      }
      break;
    }
    case 12:
    {
      range=GetQuantumRange(quantum_info->depth);
      if (quantum_info->pack == MagickFalse)
        {
          unsigned short
            pixel;

          for (x=0; x < (ssize_t) (3*number_pixels-1); x+=2)
          {
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            switch (x % 3)
            {
              default:
              case 0:
              {
                SetPixelRed(q,ScaleAnyToQuantum((QuantumAny) (pixel >> 4),
                  range));
                break;
              }
              case 1:
              {
                SetPixelGreen(q,ScaleAnyToQuantum((QuantumAny) (pixel >> 4),
                  range));
                break;
              }
              case 2:
              {
                SetPixelBlue(q,ScaleAnyToQuantum((QuantumAny) (pixel >> 4),
                  range));
                q++;
                break;
              }
            }
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            switch ((x+1) % 3)
            {
              default:
              case 0:
              {
                SetPixelRed(q,ScaleAnyToQuantum((QuantumAny) (pixel >> 4),
                  range));
                break;
              }
              case 1:
              {
                SetPixelGreen(q,ScaleAnyToQuantum((QuantumAny) (pixel >> 4),
                  range));
                break;
              }
              case 2:
              {
                SetPixelBlue(q,ScaleAnyToQuantum((QuantumAny) (pixel >> 4),
                  range));
                q++;
                break;
              }
            }
            p+=(ptrdiff_t) quantum_info->pad;
          }
          for (bit=0; bit < (ssize_t) (3*number_pixels % 2); bit++)
          {
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            switch ((x+bit) % 3)
            {
              default:
              case 0:
              {
                SetPixelRed(q,ScaleAnyToQuantum((QuantumAny) (pixel >> 4),
                  range));
                break;
              }
              case 1:
              {
                SetPixelGreen(q,ScaleAnyToQuantum((QuantumAny) (pixel >> 4),
                  range));
                break;
              }
              case 2:
              {
                SetPixelBlue(q,ScaleAnyToQuantum((QuantumAny) (pixel >> 4),
                  range));
                q++;
                break;
              }
            }
            p+=(ptrdiff_t) quantum_info->pad;
          }
          if (bit != 0)
            p++;
          break;
        }
      if (quantum_info->quantum == 32U)
        {
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumLongPixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ScaleAnyToQuantum(pixel,range));
            p=PushQuantumLongPixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ScaleAnyToQuantum(pixel,range));
            p=PushQuantumLongPixel(quantum_info,p,&pixel);
            SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelBlue(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelGreen(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
        q++;
      }
      break;
    }
    case 16:
    {
      unsigned short
        pixel;

      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelRed(q,ClampToQuantum((double) QuantumRange*(double)
              HalfToSinglePrecision(pixel)));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelGreen(q,ClampToQuantum((double) QuantumRange*(double)
              HalfToSinglePrecision(pixel)));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelBlue(q,ClampToQuantum((double) QuantumRange*(double)
              HalfToSinglePrecision(pixel)));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelBlue(q,ScaleShortToQuantum(pixel));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelGreen(q,ScaleShortToQuantum(pixel));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelRed(q,ScaleShortToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 32:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelBlue(q,ScaleLongToQuantum(pixel));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelGreen(q,ScaleLongToQuantum(pixel));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelRed(q,ScaleLongToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 24:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    case 64:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          double
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    default:
    {
      range=GetQuantumRange(quantum_info->depth);
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelBlue(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelGreen(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
        q++;
      }
      break;
    }
  }
}

static void ImportBGRAQuantum(QuantumInfo *quantum_info,
  const MagickSizeType number_pixels,const unsigned char *magick_restrict p,
  PixelPacket *magick_restrict q)
{
  QuantumAny
    range;

  ssize_t
    x;

  unsigned int
    pixel;

  switch (quantum_info->depth)
  {
    case 8:
    {
      unsigned char
        pixel;

      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushCharPixel(p,&pixel);
        SetPixelBlue(q,ScaleCharToQuantum(pixel));
        p=PushCharPixel(p,&pixel);
        SetPixelGreen(q,ScaleCharToQuantum(pixel));
        p=PushCharPixel(p,&pixel);
        SetPixelRed(q,ScaleCharToQuantum(pixel));
        p=PushCharPixel(p,&pixel);
        SetPixelAlpha(q,ScaleCharToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 10:
    {
      pixel=0;
      if (quantum_info->pack == MagickFalse)
        {
          ssize_t
            i;

          size_t
            quantum;

          ssize_t
            n;

          n=0;
          quantum=0;
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            for (i=0; i < 4; i++)
            {
              switch (n % 3)
              {
                case 0:
                {
                  p=PushLongPixel(quantum_info->endian,p,&pixel);
                  quantum=(size_t) (ScaleShortToQuantum((unsigned short)
                    (((pixel >> 22) & 0x3ff) << 6)));
                  break;
                }
                case 1:
                {
                  quantum=(size_t) (ScaleShortToQuantum((unsigned short)
                    (((pixel >> 12) & 0x3ff) << 6)));
                  break;
                }
                case 2:
                {
                  quantum=(size_t) (ScaleShortToQuantum((unsigned short)
                    (((pixel >> 2) & 0x3ff) << 6)));
                  break;
                }
              }
              switch (i)
              {
                case 0: SetPixelRed(q,quantum); break;
                case 1: SetPixelGreen(q,quantum); break;
                case 2: SetPixelBlue(q,quantum); break;
                case 3: SetPixelAlpha(q,quantum); break;
              }
              n++;
            }
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelRed(q,ScaleShortToQuantum((unsigned short) (pixel << 6)));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelGreen(q,ScaleShortToQuantum((unsigned short) (pixel << 6)));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelBlue(q,ScaleShortToQuantum((unsigned short) (pixel << 6)));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelAlpha(q,ScaleShortToQuantum((unsigned short) (pixel << 6)));
        q++;
      }
      break;
    }
    case 16:
    {
      unsigned short
        pixel;

      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelRed(q,ClampToQuantum((double) QuantumRange*(double)
              HalfToSinglePrecision(pixel)));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelGreen(q,ClampToQuantum((double) QuantumRange*(double)
              HalfToSinglePrecision(pixel)));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelBlue(q,ClampToQuantum((double) QuantumRange*(double)
              HalfToSinglePrecision(pixel)));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelAlpha(q,ClampToQuantum((double) QuantumRange*(double)
              HalfToSinglePrecision(pixel)));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelBlue(q,ScaleShortToQuantum(pixel));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelGreen(q,ScaleShortToQuantum(pixel));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelRed(q,ScaleShortToQuantum(pixel));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelAlpha(q,ScaleShortToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 32:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelAlpha(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelBlue(q,ScaleLongToQuantum(pixel));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelGreen(q,ScaleLongToQuantum(pixel));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelRed(q,ScaleLongToQuantum(pixel));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelAlpha(q,ScaleLongToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 24:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelAlpha(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    case 64:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          double
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelAlpha(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    default:
    {
      range=GetQuantumRange(quantum_info->depth);
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelBlue(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelGreen(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelAlpha(q,ScaleAnyToQuantum(pixel,range));
        q++;
      }
      break;
    }
  }
}

static void ImportBGROQuantum(QuantumInfo *quantum_info,
  const MagickSizeType number_pixels,const unsigned char *magick_restrict p,
  PixelPacket *magick_restrict q)
{
  QuantumAny
    range;

  ssize_t
    x;

  unsigned int
    pixel;

  switch (quantum_info->depth)
  {
    case 8:
    {
      unsigned char
        pixel;

      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushCharPixel(p,&pixel);
        SetPixelBlue(q,ScaleCharToQuantum(pixel));
        p=PushCharPixel(p,&pixel);
        SetPixelGreen(q,ScaleCharToQuantum(pixel));
        p=PushCharPixel(p,&pixel);
        SetPixelRed(q,ScaleCharToQuantum(pixel));
        p=PushCharPixel(p,&pixel);
        SetPixelOpacity(q,ScaleCharToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 10:
    {
      pixel=0;
      if (quantum_info->pack == MagickFalse)
        {
          ssize_t
            i;

          size_t
            quantum;

          ssize_t
            n;

          n=0;
          quantum=0;
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            for (i=0; i < 4; i++)
            {
              switch (n % 3)
              {
                case 0:
                {
                  p=PushLongPixel(quantum_info->endian,p,&pixel);
                  quantum=(size_t) (ScaleShortToQuantum((unsigned short)
                    (((pixel >> 22) & 0x3ff) << 6)));
                  break;
                }
                case 1:
                {
                  quantum=(size_t) (ScaleShortToQuantum((unsigned short)
                    (((pixel >> 12) & 0x3ff) << 6)));
                  break;
                }
                case 2:
                {
                  quantum=(size_t) (ScaleShortToQuantum((unsigned short)
                    (((pixel >> 2) & 0x3ff) << 6)));
                  break;
                }
              }
              switch (i)
              {
                case 0: SetPixelRed(q,quantum); break;
                case 1: SetPixelGreen(q,quantum); break;
                case 2: SetPixelBlue(q,quantum); break;
                case 3: SetPixelOpacity(q,quantum); break;
              }
              n++;
            }
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelRed(q,ScaleShortToQuantum((unsigned short) (pixel << 6)));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelGreen(q,ScaleShortToQuantum((unsigned short) (pixel << 6)));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelBlue(q,ScaleShortToQuantum((unsigned short) (pixel << 6)));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelOpacity(q,ScaleShortToQuantum((unsigned short) (pixel << 6)));
        q++;
      }
      break;
    }
    case 16:
    {
      unsigned short
        pixel;

      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelRed(q,ClampToQuantum((double) QuantumRange*(double)
              HalfToSinglePrecision(pixel)));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelGreen(q,ClampToQuantum((double) QuantumRange*(double)
              HalfToSinglePrecision(pixel)));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelBlue(q,ClampToQuantum((double) QuantumRange*(double)
              HalfToSinglePrecision(pixel)));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelOpacity(q,ClampToQuantum((double) QuantumRange*(double)
              HalfToSinglePrecision(pixel)));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelBlue(q,ScaleShortToQuantum(pixel));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelGreen(q,ScaleShortToQuantum(pixel));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelRed(q,ScaleShortToQuantum(pixel));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelOpacity(q,ScaleShortToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 32:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelOpacity(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelBlue(q,ScaleLongToQuantum(pixel));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelGreen(q,ScaleLongToQuantum(pixel));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelRed(q,ScaleLongToQuantum(pixel));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelOpacity(q,ScaleLongToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 24:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelOpacity(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    case 64:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          double
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelOpacity(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    default:
    {
      range=GetQuantumRange(quantum_info->depth);
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelBlue(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelGreen(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelOpacity(q,ScaleAnyToQuantum(pixel,range));
        q++;
      }
      break;
    }
  }
}

static void ImportBlackQuantum(const Image *image,QuantumInfo *quantum_info,
  const MagickSizeType number_pixels,const unsigned char *magick_restrict p,
  PixelPacket *magick_restrict q,IndexPacket *magick_restrict indexes,
  ExceptionInfo *exception)
{
  ssize_t
    x;

  unsigned int
    pixel;

  if (image->colorspace != CMYKColorspace)
    {
      (void) ThrowMagickException(exception,GetMagickModule(),ImageError,
        "ColorSeparatedImageRequired","`%s'",image->filename);
      return;
    }
  switch (quantum_info->depth)
  {
    case 8:
    {
      unsigned char
        pixel;

      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushCharPixel(p,&pixel);
        SetPixelIndex(indexes+x,ScaleCharToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
      }
      break;
    }
    case 16:
    {
      unsigned short
        pixel;

      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelIndex(indexes+x,ClampToQuantum((double)
              QuantumRange*(double) HalfToSinglePrecision(pixel)));
            p+=(ptrdiff_t) quantum_info->pad;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelIndex(indexes+x,ScaleShortToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
      }
      break;
    }
    case 32:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelIndex(indexes+x,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelIndex(indexes+x,ScaleLongToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 24:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelIndex(indexes+x,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    case 64:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          double
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelIndex(indexes+x,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    default:
    {
      QuantumAny
        range;

      range=GetQuantumRange(quantum_info->depth);
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelIndex(indexes+x,ScaleAnyToQuantum(pixel,range));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
  }
}

static void ImportBlueQuantum(QuantumInfo *quantum_info,
  const MagickSizeType number_pixels,const unsigned char *magick_restrict p,
  PixelPacket *magick_restrict q)
{
  ssize_t
    x;

  unsigned int
    pixel;

  switch (quantum_info->depth)
  {
    case 8:
    {
      unsigned char
        pixel;

      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushCharPixel(p,&pixel);
        SetPixelBlue(q,ScaleCharToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 16:
    {
      unsigned short
        pixel;

      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelBlue(q,ClampToQuantum((double) QuantumRange*(double)
              HalfToSinglePrecision(pixel)));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelBlue(q,ScaleShortToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 32:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelBlue(q,ScaleLongToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 24:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    case 64:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          double
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    default:
    {
      QuantumAny
        range;

      range=GetQuantumRange(quantum_info->depth);
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelBlue(q,ScaleAnyToQuantum(pixel,range));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
  }
}

static void ImportCbYCrYQuantum(const Image *image,QuantumInfo *quantum_info,
  const MagickSizeType number_pixels,const unsigned char *magick_restrict p,
  PixelPacket *magick_restrict q)
{
  ssize_t
    x;

  unsigned int
    pixel;

  switch (quantum_info->depth)
  {
    case 10:
    {
      Quantum
        cbcr[4];

      pixel=0;
      if (quantum_info->pack == MagickFalse)
        {
          ssize_t
            i;

          size_t
            quantum;

          ssize_t
            n;

          n=0;
          quantum=0;
          for (x=0; x < (ssize_t) (number_pixels-3); x+=4)
          {
            for (i=0; i < 4; i++)
            {
              switch (n % 3)
              {
                case 0:
                {
                  p=PushLongPixel(quantum_info->endian,p,&pixel);
                  quantum=(size_t) (ScaleShortToQuantum((unsigned short)
                    (((pixel >> 22) & 0x3ff) << 6)));
                  break;
                }
                case 1:
                {
                  quantum=(size_t) (ScaleShortToQuantum((unsigned short)
                    (((pixel >> 12) & 0x3ff) << 6)));
                  break;
                }
                case 2:
                {
                  quantum=(size_t) (ScaleShortToQuantum((unsigned short)
                    (((pixel >> 2) & 0x3ff) << 6)));
                  break;
                }
              }
              cbcr[i]=(Quantum) (quantum);
              n++;
            }
            p+=(ptrdiff_t) quantum_info->pad;
            SetPixelRed(q,cbcr[1]);
            SetPixelGreen(q,cbcr[0]);
            SetPixelBlue(q,cbcr[2]);
            q++;
            SetPixelRed(q,cbcr[3]);
            SetPixelGreen(q,cbcr[0]);
            SetPixelBlue(q,cbcr[2]);
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    default:
    {
      QuantumAny
        range;

      range=GetQuantumRange(image->depth);
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelGreen(q,ScaleAnyToQuantum(pixel,range));
        q++;
      }
      break;
    }
  }
}

static void ImportCMYKQuantum(const Image *image,QuantumInfo *quantum_info,
  const MagickSizeType number_pixels,const unsigned char *magick_restrict p,
  PixelPacket *magick_restrict q,IndexPacket *magick_restrict indexes,
  ExceptionInfo *exception)
{
  QuantumAny
    range;

  ssize_t
    x;

  unsigned int
    pixel;

  if (image->colorspace != CMYKColorspace)
    {
      (void) ThrowMagickException(exception,GetMagickModule(),ImageError,
        "ColorSeparatedImageRequired","`%s'",image->filename);
      return;
    }
  switch (quantum_info->depth)
  {
    case 8:
    {
      unsigned char
        pixel;

      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushCharPixel(p,&pixel);
        SetPixelRed(q,ScaleCharToQuantum(pixel));
        p=PushCharPixel(p,&pixel);
        SetPixelGreen(q,ScaleCharToQuantum(pixel));
        p=PushCharPixel(p,&pixel);
        SetPixelBlue(q,ScaleCharToQuantum(pixel));
        p=PushCharPixel(p,&pixel);
        SetPixelIndex(indexes+x,ScaleCharToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 16:
    {
      unsigned short
        pixel;

      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelRed(q,ClampToQuantum((double) QuantumRange*(double)
              HalfToSinglePrecision(pixel)));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelGreen(q,ClampToQuantum((double) QuantumRange*(double)
              HalfToSinglePrecision(pixel)));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelBlue(q,ClampToQuantum((double) QuantumRange*(double)
              HalfToSinglePrecision(pixel)));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelIndex(indexes+x,ClampToQuantum((double) QuantumRange*
              (double) HalfToSinglePrecision(pixel)));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelRed(q,ScaleShortToQuantum(pixel));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelGreen(q,ScaleShortToQuantum(pixel));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelBlue(q,ScaleShortToQuantum(pixel));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelIndex(indexes+x,ScaleShortToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 32:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelIndex(indexes+x,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelRed(q,ScaleLongToQuantum(pixel));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelGreen(q,ScaleLongToQuantum(pixel));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelBlue(q,ScaleLongToQuantum(pixel));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelIndex(indexes+x,ScaleLongToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 24:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelIndex(indexes+x,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    case 64:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          double
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelIndex(indexes+x,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    default:
    {
      range=GetQuantumRange(quantum_info->depth);
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelGreen(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelBlue(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelIndex(indexes+x,ScaleAnyToQuantum(pixel,range));
        q++;
      }
      break;
    }
  }
}

static void ImportCMYKAQuantum(const Image *image,QuantumInfo *quantum_info,
  const MagickSizeType number_pixels,const unsigned char *magick_restrict p,
  PixelPacket *magick_restrict q,IndexPacket *magick_restrict indexes,
  ExceptionInfo *exception)
{
  QuantumAny
    range;

  ssize_t
    x;

  unsigned int
    pixel;

  if (image->colorspace != CMYKColorspace)
    {
      (void) ThrowMagickException(exception,GetMagickModule(),ImageError,
        "ColorSeparatedImageRequired","`%s'",image->filename);
      return;
    }
  switch (quantum_info->depth)
  {
    case 8:
    {
      unsigned char
        pixel;

      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushCharPixel(p,&pixel);
        SetPixelRed(q,ScaleCharToQuantum(pixel));
        p=PushCharPixel(p,&pixel);
        SetPixelGreen(q,ScaleCharToQuantum(pixel));
        p=PushCharPixel(p,&pixel);
        SetPixelBlue(q,ScaleCharToQuantum(pixel));
        p=PushCharPixel(p,&pixel);
        SetPixelIndex(indexes+x,ScaleCharToQuantum(pixel));
        p=PushCharPixel(p,&pixel);
        SetPixelAlpha(q,ScaleCharToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 16:
    {
      unsigned short
        pixel;

      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelRed(q,ClampToQuantum((double) QuantumRange*
              (double) HalfToSinglePrecision(pixel)));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelGreen(q,ClampToQuantum((double) QuantumRange*
              (double) HalfToSinglePrecision(pixel)));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelBlue(q,ClampToQuantum((double) QuantumRange*
              (double) HalfToSinglePrecision(pixel)));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelIndex(indexes+x,ClampToQuantum((double) QuantumRange*
              (double) HalfToSinglePrecision(pixel)));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelAlpha(q,ClampToQuantum((MagickRealType) QuantumRange*
              (double) HalfToSinglePrecision(pixel)));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelRed(q,ScaleShortToQuantum(pixel));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelGreen(q,ScaleShortToQuantum(pixel));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelBlue(q,ScaleShortToQuantum(pixel));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelIndex(indexes+x,ScaleShortToQuantum(pixel));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelAlpha(q,ScaleShortToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 32:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelIndex(indexes+x,ClampToQuantum(pixel));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelAlpha(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelRed(q,ScaleLongToQuantum(pixel));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelGreen(q,ScaleLongToQuantum(pixel));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelBlue(q,ScaleLongToQuantum(pixel));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelIndex(indexes+x,ScaleLongToQuantum(pixel));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelAlpha(q,ScaleLongToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 24:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelIndex(indexes+x,ClampToQuantum(pixel));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelAlpha(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    case 64:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          double
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelIndex(indexes+x,ClampToQuantum(pixel));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelAlpha(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    default:
    {
      range=GetQuantumRange(image->depth);
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelGreen(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelBlue(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelIndex(indexes+x,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelAlpha(q,ScaleAnyToQuantum(pixel,range));
        q++;
      }
      break;
    }
  }
}

static void ImportCMYKOQuantum(const Image *image,QuantumInfo *quantum_info,
  const MagickSizeType number_pixels,const unsigned char *magick_restrict p,
  PixelPacket *magick_restrict q,IndexPacket *magick_restrict indexes,
  ExceptionInfo *exception)
{
  QuantumAny
    range;

  ssize_t
    x;

  unsigned int
    pixel;

  if (image->colorspace != CMYKColorspace)
    {
      (void) ThrowMagickException(exception,GetMagickModule(),ImageError,
        "ColorSeparatedImageRequired","`%s'",image->filename);
      return;
    }
  switch (quantum_info->depth)
  {
    case 8:
    {
      unsigned char
        pixel;

      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushCharPixel(p,&pixel);
        SetPixelRed(q,ScaleCharToQuantum(pixel));
        p=PushCharPixel(p,&pixel);
        SetPixelGreen(q,ScaleCharToQuantum(pixel));
        p=PushCharPixel(p,&pixel);
        SetPixelBlue(q,ScaleCharToQuantum(pixel));
        p=PushCharPixel(p,&pixel);
        SetPixelIndex(indexes+x,ScaleCharToQuantum(pixel));
        p=PushCharPixel(p,&pixel);
        SetPixelOpacity(q,ScaleCharToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 16:
    {
      unsigned short
        pixel;

      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelRed(q,ClampToQuantum((double) QuantumRange*
              (double) HalfToSinglePrecision(pixel)));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelGreen(q,ClampToQuantum((double) QuantumRange*
              (double) HalfToSinglePrecision(pixel)));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelBlue(q,ClampToQuantum((double) QuantumRange*
              (double) HalfToSinglePrecision(pixel)));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelIndex(indexes+x,ClampToQuantum((double) QuantumRange*
              (double) HalfToSinglePrecision(pixel)));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelOpacity(q,ClampToQuantum((double) QuantumRange*
              (double) HalfToSinglePrecision(pixel)));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelRed(q,ScaleShortToQuantum(pixel));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelGreen(q,ScaleShortToQuantum(pixel));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelBlue(q,ScaleShortToQuantum(pixel));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelIndex(indexes+x,ScaleShortToQuantum(pixel));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelOpacity(q,ScaleShortToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 32:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelIndex(indexes+x,ClampToQuantum(pixel));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelOpacity(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelRed(q,ScaleLongToQuantum(pixel));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelGreen(q,ScaleLongToQuantum(pixel));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelBlue(q,ScaleLongToQuantum(pixel));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelIndex(indexes+x,ScaleLongToQuantum(pixel));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelOpacity(q,ScaleLongToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 24:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelIndex(indexes+x,ClampToQuantum(pixel));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelOpacity(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    case 64:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          double
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelIndex(indexes+x,ClampToQuantum(pixel));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelOpacity(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    default:
    {
      range=GetQuantumRange(image->depth);
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelGreen(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelBlue(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelIndex(indexes+x,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelOpacity(q,ScaleAnyToQuantum(pixel,range));
        q++;
      }
      break;
    }
  }
}

static void ImportGrayQuantum(const Image *image,QuantumInfo *quantum_info,
  const MagickSizeType number_pixels,const unsigned char *magick_restrict p,
  PixelPacket *magick_restrict q)
{
  QuantumAny
    range;

  ssize_t
    x;

  ssize_t
    bit;

  unsigned int
    pixel;

  pixel=0;
  switch (quantum_info->depth)
  {
    case 1:
    {
      Quantum
        black,
        white;

      black=(Quantum) 0;
      white=QuantumRange;
      if (quantum_info->min_is_white != MagickFalse)
        {
          black=QuantumRange;
          white=(Quantum) 0;
        }
      for (x=0; x < ((ssize_t) number_pixels-7); x+=8)
      {
        for (bit=0; bit < 8; bit++)
        {
          SetPixelRed(q,((*p) & (1 << (7-bit))) == 0 ?  black : white);
          SetPixelGreen(q,GetPixelRed(q));
          SetPixelBlue(q,GetPixelRed(q));
          q++;
        }
        p++;
      }
      for (bit=0; bit < (ssize_t) (number_pixels % 8); bit++)
      {
        SetPixelRed(q,((*p) & (0x01 << (7-bit))) == 0 ?  black : white);
        SetPixelGreen(q,GetPixelRed(q));
        SetPixelBlue(q,GetPixelRed(q));
        q++;
      }
      if (bit != 0)
        p++;
      break;
    }
    case 4:
    {
      unsigned char
        pixel;

      range=GetQuantumRange(quantum_info->depth);
      for (x=0; x < ((ssize_t) number_pixels-1); x+=2)
      {
        pixel=(unsigned char) ((*p >> 4) & 0xf);
        SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
        SetPixelGreen(q,GetPixelRed(q));
        SetPixelBlue(q,GetPixelRed(q));
        q++;
        pixel=(unsigned char) ((*p) & 0xf);
        SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
        SetPixelGreen(q,GetPixelRed(q));
        SetPixelBlue(q,GetPixelRed(q));
        p++;
        q++;
      }
      for (bit=0; bit < (ssize_t) (number_pixels % 2); bit++)
      {
        pixel=(unsigned char) (*p++ >> 4);
        SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
        SetPixelGreen(q,GetPixelRed(q));
        SetPixelBlue(q,GetPixelRed(q));
        q++;
      }
      break;
    }
    case 8:
    {
      unsigned char
        pixel;

      if (quantum_info->min_is_white != MagickFalse)
        {
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushCharPixel(p,&pixel);
            SetPixelRed(q,QuantumRange-ScaleCharToQuantum(pixel));
            SetPixelGreen(q,GetPixelRed(q));
            SetPixelBlue(q,GetPixelRed(q));
            SetPixelOpacity(q,OpaqueOpacity);
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushCharPixel(p,&pixel);
        SetPixelRed(q,ScaleCharToQuantum(pixel));
        SetPixelGreen(q,GetPixelRed(q));
        SetPixelBlue(q,GetPixelRed(q));
        SetPixelOpacity(q,OpaqueOpacity);
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 10:
    {
      range=GetQuantumRange(quantum_info->depth);
      if (quantum_info->pack == MagickFalse)
        {
          if (image->endian == LSBEndian)
            {
              for (x=0; x < (ssize_t) (number_pixels-2); x+=3)
              {
                p=PushLongPixel(quantum_info->endian,p,&pixel);
                SetPixelRed(q,ScaleAnyToQuantum((pixel >> 22) & 0x3ff,range));
                SetPixelGreen(q,GetPixelRed(q));
                SetPixelBlue(q,GetPixelRed(q));
                q++;
                SetPixelRed(q,ScaleAnyToQuantum((pixel >> 12) & 0x3ff,range));
                SetPixelGreen(q,GetPixelRed(q));
                SetPixelBlue(q,GetPixelRed(q));
                q++;
                SetPixelRed(q,ScaleAnyToQuantum((pixel >> 2) & 0x3ff,range));
                SetPixelGreen(q,GetPixelRed(q));
                SetPixelBlue(q,GetPixelRed(q));
                p+=(ptrdiff_t) quantum_info->pad;
                q++;
              }
              if (x++ < (ssize_t) (number_pixels-1))
                {
                  p=PushLongPixel(quantum_info->endian,p,&pixel);
                  SetPixelRed(q,ScaleAnyToQuantum((pixel >> 22) & 0x3ff,range));
                  SetPixelGreen(q,GetPixelRed(q));
                  SetPixelBlue(q,GetPixelRed(q));
                  q++;
                }
              if (x++ < (ssize_t) number_pixels)
                {
                  SetPixelRed(q,ScaleAnyToQuantum((pixel >> 12) & 0x3ff,range));
                  SetPixelGreen(q,GetPixelRed(q));
                  SetPixelBlue(q,GetPixelRed(q));
                  q++;
                }
              break;
            }
          for (x=0; x < (ssize_t) (number_pixels-2); x+=3)
          {
            p=PushLongPixel(quantum_info->endian,p,&pixel);
            SetPixelRed(q,ScaleAnyToQuantum((pixel >> 2) & 0x3ff,range));
            SetPixelGreen(q,GetPixelRed(q));
            SetPixelBlue(q,GetPixelRed(q));
            q++;
            SetPixelRed(q,ScaleAnyToQuantum((pixel >> 12) & 0x3ff,range));
            SetPixelGreen(q,GetPixelRed(q));
            SetPixelBlue(q,GetPixelRed(q));
            q++;
            SetPixelRed(q,ScaleAnyToQuantum((pixel >> 22) & 0x3ff,range));
            SetPixelGreen(q,GetPixelRed(q));
            SetPixelBlue(q,GetPixelRed(q));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          if (x++ < (ssize_t) (number_pixels-1))
            {
              p=PushLongPixel(quantum_info->endian,p,&pixel);
              SetPixelRed(q,ScaleAnyToQuantum((pixel >> 2) & 0x3ff,range));
              SetPixelGreen(q,GetPixelRed(q));
              SetPixelBlue(q,GetPixelRed(q));
              q++;
            }
          if (x++ < (ssize_t) number_pixels)
            {
              SetPixelRed(q,ScaleAnyToQuantum((pixel >> 12) & 0x3ff,range));
              SetPixelGreen(q,GetPixelRed(q));
              SetPixelBlue(q,GetPixelRed(q));
              q++;
            }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
        SetPixelGreen(q,GetPixelRed(q));
        SetPixelBlue(q,GetPixelRed(q));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 12:
    {
      range=GetQuantumRange(quantum_info->depth);
      if (quantum_info->pack == MagickFalse)
        {
          unsigned short
            pixel;

          for (x=0; x < (ssize_t) (number_pixels-1); x+=2)
          {
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelRed(q,ScaleAnyToQuantum((QuantumAny) (pixel >> 4),range));
            SetPixelGreen(q,GetPixelRed(q));
            SetPixelBlue(q,GetPixelRed(q));
            q++;
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelRed(q,ScaleAnyToQuantum((QuantumAny) (pixel >> 4),range));
            SetPixelGreen(q,GetPixelRed(q));
            SetPixelBlue(q,GetPixelRed(q));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          for (bit=0; bit < (ssize_t) (number_pixels % 2); bit++)
          {
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelRed(q,ScaleAnyToQuantum((QuantumAny)
              (pixel >> 4),range));
            SetPixelGreen(q,GetPixelRed(q));
            SetPixelBlue(q,GetPixelRed(q));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          if (bit != 0)
            p++;
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
        SetPixelGreen(q,GetPixelRed(q));
        SetPixelBlue(q,GetPixelRed(q));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 16:
    {
      unsigned short
        pixel;

      if (quantum_info->min_is_white != MagickFalse)
        {
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelRed(q,QuantumRange-ScaleShortToQuantum(pixel));
            SetPixelGreen(q,GetPixelRed(q));
            SetPixelBlue(q,GetPixelRed(q));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelRed(q,ClampToQuantum((double) QuantumRange*
              (double) HalfToSinglePrecision(pixel)));
            SetPixelGreen(q,GetPixelRed(q));
            SetPixelBlue(q,GetPixelRed(q));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      if (quantum_info->format == SignedQuantumFormat)
        {
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            pixel=(unsigned short) (((unsigned int) pixel+32768) % 65536);
            SetPixelRed(q,ScaleShortToQuantum(pixel));
            SetPixelGreen(q,GetPixelRed(q));
            SetPixelBlue(q,GetPixelRed(q));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelRed(q,ScaleShortToQuantum(pixel));
        SetPixelGreen(q,GetPixelRed(q));
        SetPixelBlue(q,GetPixelRed(q));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 32:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            SetPixelGreen(q,GetPixelRed(q));
            SetPixelBlue(q,GetPixelRed(q));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelRed(q,ScaleLongToQuantum(pixel));
        SetPixelGreen(q,GetPixelRed(q));
        SetPixelBlue(q,GetPixelRed(q));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 24:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            SetPixelGreen(q,GetPixelRed(q));
            SetPixelBlue(q,GetPixelRed(q));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    case 64:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          double
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            SetPixelGreen(q,GetPixelRed(q));
            SetPixelBlue(q,GetPixelRed(q));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    default:
    {
      range=GetQuantumRange(quantum_info->depth);
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
        SetPixelGreen(q,GetPixelRed(q));
        SetPixelBlue(q,GetPixelRed(q));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
  }
}

static void ImportGrayAlphaQuantum(QuantumInfo *quantum_info,
  const MagickSizeType number_pixels,const unsigned char *magick_restrict p,
  PixelPacket *magick_restrict q)
{
  QuantumAny
    range;

  ssize_t
    x;

  ssize_t
    bit;

  unsigned int
    pixel;

  switch (quantum_info->depth)
  {
    case 1:
    {
      unsigned char
        pixel;

      bit=0;
      for (x=((ssize_t) number_pixels-3); x > 0; x-=4)
      {
        for (bit=0; bit < 8; bit+=2)
        {
          pixel=(unsigned char) (((*p) & (1 << (7-bit))) != 0 ? 0x00 : 0x01);
          SetPixelRed(q,pixel == 0 ? 0 : QuantumRange);
          SetPixelGreen(q,GetPixelRed(q));
          SetPixelBlue(q,GetPixelRed(q));
          SetPixelOpacity(q,((*p) & (1UL << (unsigned char) (6-bit))) == 0 ?
            TransparentOpacity : OpaqueOpacity);
          q++;
        }
        p++;
      }
      if ((number_pixels % 4) != 0)
        for (bit=3; bit >= (ssize_t) (4-(number_pixels % 4)); bit-=2)
        {
          pixel=(unsigned char) (((*p) & (1 << (7-bit))) != 0 ? 0x00 : 0x01);
          SetPixelRed(q,pixel != 0 ? 0 : QuantumRange);
          SetPixelGreen(q,GetPixelRed(q));
          SetPixelBlue(q,GetPixelRed(q));
          SetPixelOpacity(q,((*p) & (1UL << (unsigned char) (6-bit))) == 0 ?
            TransparentOpacity : OpaqueOpacity);
          q++;
        }
      if (bit != 0)
        p++;
      break;
    }
    case 4:
    {
      unsigned char
        pixel;

      range=GetQuantumRange(quantum_info->depth);
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        pixel=(unsigned char) ((*p >> 4) & 0xf);
        SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
        SetPixelGreen(q,GetPixelRed(q));
        SetPixelBlue(q,GetPixelRed(q));
        pixel=(unsigned char) ((*p) & 0xf);
        SetPixelAlpha(q,ScaleAnyToQuantum(pixel,range));
        p++;
        q++;
      }
      break;
    }
    case 8:
    {
      unsigned char
        pixel;

      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushCharPixel(p,&pixel);
        SetPixelRed(q,ScaleCharToQuantum(pixel));
        SetPixelGreen(q,GetPixelRed(q));
        SetPixelBlue(q,GetPixelRed(q));
        p=PushCharPixel(p,&pixel);
        SetPixelAlpha(q,ScaleCharToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 10:
    {
      range=GetQuantumRange(quantum_info->depth);
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
        SetPixelGreen(q,GetPixelRed(q));
        SetPixelBlue(q,GetPixelRed(q));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelOpacity(q,ScaleAnyToQuantum(pixel,range));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 12:
    {
      range=GetQuantumRange(quantum_info->depth);
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
        SetPixelGreen(q,GetPixelRed(q));
        SetPixelBlue(q,GetPixelRed(q));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelOpacity(q,ScaleAnyToQuantum(pixel,range));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 16:
    {
      unsigned short
        pixel;

      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelRed(q,ClampToQuantum((double) QuantumRange*
              (double) HalfToSinglePrecision(pixel)));
            SetPixelGreen(q,GetPixelRed(q));
            SetPixelBlue(q,GetPixelRed(q));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelAlpha(q,ClampToQuantum((double) QuantumRange*
              (double) HalfToSinglePrecision(pixel)));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelRed(q,ScaleShortToQuantum(pixel));
        SetPixelGreen(q,GetPixelRed(q));
        SetPixelBlue(q,GetPixelRed(q));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelAlpha(q,ScaleShortToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 32:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            SetPixelGreen(q,GetPixelRed(q));
            SetPixelBlue(q,GetPixelRed(q));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelAlpha(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelRed(q,ScaleLongToQuantum(pixel));
        SetPixelGreen(q,GetPixelRed(q));
        SetPixelBlue(q,GetPixelRed(q));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelAlpha(q,ScaleLongToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 24:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            SetPixelGreen(q,GetPixelRed(q));
            SetPixelBlue(q,GetPixelRed(q));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelAlpha(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    case 64:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          double
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            SetPixelGreen(q,GetPixelRed(q));
            SetPixelBlue(q,GetPixelRed(q));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelAlpha(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    default:
    {
      QuantumAny
        range;

      range=GetQuantumRange(quantum_info->depth);
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
        SetPixelGreen(q,GetPixelRed(q));
        SetPixelBlue(q,GetPixelRed(q));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelAlpha(q,ScaleAnyToQuantum(pixel,range));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
  }
}

static void ImportGreenQuantum(QuantumInfo *quantum_info,
  const MagickSizeType number_pixels,const unsigned char *magick_restrict p,
  PixelPacket *magick_restrict q)
{
  ssize_t
    x;

  unsigned int
    pixel;

  switch (quantum_info->depth)
  {
    case 8:
    {
      unsigned char
        pixel;

      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushCharPixel(p,&pixel);
        SetPixelGreen(q,ScaleCharToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 16:
    {
      unsigned short
        pixel;

      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelGreen(q,ClampToQuantum((double) QuantumRange*(double)
              HalfToSinglePrecision(pixel)));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelGreen(q,ScaleShortToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 32:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelGreen(q,ScaleLongToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 24:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    case 64:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          double
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    default:
    {
      QuantumAny
        range;

      range=GetQuantumRange(quantum_info->depth);
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelGreen(q,ScaleAnyToQuantum(pixel,range));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
  }
}

static void ImportIndexQuantum(const Image *image,QuantumInfo *quantum_info,
  const MagickSizeType number_pixels,const unsigned char *magick_restrict p,
  PixelPacket *magick_restrict q,IndexPacket *magick_restrict indexes,
  ExceptionInfo *exception)
{
  MagickBooleanType
    range_exception;

  ssize_t
    x;

  ssize_t
    bit;

  unsigned int
    pixel;

  if (image->storage_class != PseudoClass)
    {
      (void) ThrowMagickException(exception,GetMagickModule(),ImageError,
        "ColormappedImageRequired","`%s'",image->filename);
      return;
    }
  range_exception=MagickFalse;
  switch (quantum_info->depth)
  {
    case 1:
    {
      unsigned char
        pixel;

      for (x=0; x < ((ssize_t) number_pixels-7); x+=8)
      {
        for (bit=0; bit < 8; bit++)
        {
          if (quantum_info->min_is_white == MagickFalse)
            pixel=(unsigned char) (((*p) & (1 << (7-bit))) == 0 ? 0x00 : 0x01);
          else
            pixel=(unsigned char) (((*p) & (1 << (7-bit))) != 0 ? 0x00 : 0x01);
          SetPixelIndex(indexes+x+bit,PushColormapIndex(image,pixel,
            &range_exception));
          SetPixelRGBO(q,image->colormap+(ssize_t) GetPixelIndex(
            indexes+x+bit));
          q++;
        }
        p++;
      }
      for (bit=0; bit < (ssize_t) (number_pixels % 8); bit++)
      {
        if (quantum_info->min_is_white == MagickFalse)
          pixel=(unsigned char) (((*p) & (1 << (7-bit))) == 0 ? 0x00 : 0x01);
        else
          pixel=(unsigned char) (((*p) & (1 << (7-bit))) != 0 ? 0x00 : 0x01);
        SetPixelIndex(indexes+x+bit,PushColormapIndex(image,pixel,
          &range_exception));
        SetPixelRGBO(q,image->colormap+(ssize_t) GetPixelIndex(indexes+x+bit));
        q++;
      }
      break;
    }
    case 4:
    {
      unsigned char
        pixel;

      for (x=0; x < ((ssize_t) number_pixels-1); x+=2)
      {
        pixel=(unsigned char) ((*p >> 4) & 0xf);
        SetPixelIndex(indexes+x,PushColormapIndex(image,pixel,
          &range_exception));
        SetPixelRGBO(q,image->colormap+(ssize_t) GetPixelIndex(indexes+x));
        q++;
        pixel=(unsigned char) ((*p) & 0xf);
        SetPixelIndex(indexes+x+1,PushColormapIndex(image,pixel,
          &range_exception));
        SetPixelRGBO(q,image->colormap+(ssize_t) GetPixelIndex(indexes+x+1));
        p++;
        q++;
      }
      for (bit=0; bit < (ssize_t) (number_pixels % 2); bit++)
      {
        pixel=(unsigned char) ((*p++ >> 4) & 0xf);
        SetPixelIndex(indexes+x+bit,PushColormapIndex(image,pixel,
          &range_exception));
        SetPixelRGBO(q,image->colormap+(ssize_t) GetPixelIndex(indexes+x+bit));
        q++;
      }
      break;
    }
    case 8:
    {
      unsigned char
        pixel;

      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushCharPixel(p,&pixel);
        SetPixelIndex(indexes+x,PushColormapIndex(image,pixel,
          &range_exception));
        SetPixelRGBO(q,image->colormap+(ssize_t) GetPixelIndex(indexes+x));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 16:
    {
      unsigned short
        pixel;

      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelIndex(indexes+x,PushColormapIndex(image,(size_t)
              ClampToQuantum((double) QuantumRange* (double)
              HalfToSinglePrecision(pixel)),&range_exception));
            SetPixelRGBO(q,image->colormap+(ssize_t) GetPixelIndex(indexes+x));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelIndex(indexes+x,PushColormapIndex(image,pixel,
          &range_exception));
        SetPixelRGBO(q,image->colormap+(ssize_t) GetPixelIndex(indexes+x));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 32:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelIndex(indexes+x,PushColormapIndex(image,(size_t)
              ClampToQuantum(pixel),&range_exception));
            SetPixelRGBO(q,image->colormap+(ssize_t) GetPixelIndex(indexes+x));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelIndex(indexes+x,PushColormapIndex(image,pixel,
          &range_exception));
        SetPixelRGBO(q,image->colormap+(ssize_t) GetPixelIndex(indexes+x));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 24:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelIndex(indexes+x,PushColormapIndex(image,(size_t)
              ClampToQuantum(pixel),&range_exception));
            SetPixelRGBO(q,image->colormap+(ssize_t) GetPixelIndex(indexes+x));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    case 64:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          double
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelIndex(indexes+x,PushColormapIndex(image,(size_t)
              ClampToQuantum(pixel),&range_exception));
            SetPixelRGBO(q,image->colormap+(ssize_t) GetPixelIndex(indexes+x));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    default:
    {
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelIndex(indexes+x,PushColormapIndex(image,pixel,
          &range_exception));
        SetPixelRGBO(q,image->colormap+(ssize_t) GetPixelIndex(indexes+x));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
  }
  if (range_exception != MagickFalse)
    (void) ThrowMagickException(exception,GetMagickModule(),CorruptImageError,
      "InvalidColormapIndex","`%s'",image->filename);
}

static void ImportIndexAlphaQuantum(const Image *image,
  QuantumInfo *quantum_info,const MagickSizeType number_pixels,
  const unsigned char *magick_restrict p,PixelPacket *magick_restrict q,
  IndexPacket *magick_restrict indexes,ExceptionInfo *exception)
{
  MagickBooleanType
    range_exception;

  QuantumAny
    range;

  ssize_t
    x;

  ssize_t
    bit;

  unsigned int
    pixel;

  if (image->storage_class != PseudoClass)
    {
      (void) ThrowMagickException(exception,GetMagickModule(),ImageError,
        "ColormappedImageRequired","`%s'",image->filename);
      return;
    }
  range_exception=MagickFalse;
  switch (quantum_info->depth)
  {
    case 1:
    {
      unsigned char
        pixel;

      for (x=((ssize_t) number_pixels-3); x > 0; x-=4)
      {
        for (bit=0; bit < 8; bit+=2)
        {
          if (quantum_info->min_is_white == MagickFalse)
            pixel=(unsigned char) (((*p) & (1 << (7-bit))) == 0 ? 0x00 : 0x01);
          else
            pixel=(unsigned char) (((*p) & (1 << (7-bit))) != 0 ? 0x00 : 0x01);
          SetPixelIndex(indexes+x+bit/2,pixel == 0 ? 0 : 1);
          SetPixelRed(q,pixel == 0 ? 0 : QuantumRange);
          SetPixelGreen(q,GetPixelRed(q));
          SetPixelBlue(q,GetPixelRed(q));
          SetPixelOpacity(q,((*p) & (1UL << (unsigned char) (6-bit))) == 0 ?
            TransparentOpacity : OpaqueOpacity);
          q++;
        }
      }
      if ((number_pixels % 4) != 0)
        for (bit=0; bit < (ssize_t) (number_pixels % 4); bit+=2)
        {
          if (quantum_info->min_is_white == MagickFalse)
            pixel=(unsigned char) (((*p) & (1 << (7-bit))) == 0 ? 0x00 : 0x01);
          else
            pixel=(unsigned char) (((*p) & (1 << (7-bit))) != 0 ? 0x00 : 0x01);
          SetPixelIndex(indexes+x+bit/2,pixel == 0 ? 0 : 1);
          SetPixelRed(q,pixel == 0 ? 0 : QuantumRange);
          SetPixelGreen(q,GetPixelRed(q));
          SetPixelBlue(q,GetPixelRed(q));
          SetPixelOpacity(q,((*p) & (1UL << (unsigned char) (6-bit))) == 0 ?
            TransparentOpacity : OpaqueOpacity);
          q++;
        }
      break;
    }
    case 4:
    {
      unsigned char
        pixel;

      range=GetQuantumRange(quantum_info->depth);
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        pixel=(unsigned char) ((*p >> 4) & 0xf);
        SetPixelIndex(indexes+x,PushColormapIndex(image,pixel,
          &range_exception));
        SetPixelRGBO(q,image->colormap+(ssize_t) GetPixelIndex(indexes+x));
        pixel=(unsigned char) ((*p) & 0xf);
        SetPixelAlpha(q,ScaleAnyToQuantum(pixel,range));
        p++;
        q++;
      }
      break;
    }
    case 8:
    {
      unsigned char
        pixel;

      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushCharPixel(p,&pixel);
        SetPixelIndex(indexes+x,PushColormapIndex(image,pixel,
          &range_exception));
        SetPixelRGBO(q,image->colormap+(ssize_t) GetPixelIndex(indexes+x));
        p=PushCharPixel(p,&pixel);
        SetPixelAlpha(q,ScaleCharToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 16:
    {
      unsigned short
        pixel;

      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelIndex(indexes+x,PushColormapIndex(image,(size_t)
              ClampToQuantum((double) QuantumRange*(double) 
              HalfToSinglePrecision(pixel)),&range_exception));
            SetPixelRGBO(q,image->colormap+(ssize_t) GetPixelIndex(indexes+x));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelAlpha(q,ClampToQuantum((double) QuantumRange*(double)
              HalfToSinglePrecision(pixel)));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelIndex(indexes+x,PushColormapIndex(image,pixel,
          &range_exception));
        SetPixelRGBO(q,image->colormap+(ssize_t) GetPixelIndex(indexes+x));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelAlpha(q,ScaleShortToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 32:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelIndex(indexes+x,PushColormapIndex(image,(size_t)
              ClampToQuantum(pixel),&range_exception));
            SetPixelRGBO(q,image->colormap+(ssize_t) GetPixelIndex(indexes+x));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelAlpha(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelIndex(indexes+x,PushColormapIndex(image,pixel,
          &range_exception));
        SetPixelRGBO(q,image->colormap+(ssize_t) GetPixelIndex(indexes+x));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelAlpha(q,ScaleLongToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 24:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelIndex(indexes+x,PushColormapIndex(image,(size_t)
              ClampToQuantum(pixel),&range_exception));
            SetPixelRGBO(q,image->colormap+(ssize_t) GetPixelIndex(indexes+x));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelAlpha(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    case 64:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          double
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelIndex(indexes+x,PushColormapIndex(image,(size_t)
              ClampToQuantum(pixel),&range_exception));
            SetPixelRGBO(q,image->colormap+(ssize_t) GetPixelIndex(indexes+x));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelAlpha(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    default:
    {
      range=GetQuantumRange(quantum_info->depth);
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelIndex(indexes+x,PushColormapIndex(image,pixel,
          &range_exception));
        SetPixelRGBO(q,image->colormap+(ssize_t) GetPixelIndex(indexes+x));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelAlpha(q,ScaleAnyToQuantum(pixel,range));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
  }
  if (range_exception != MagickFalse)
    (void) ThrowMagickException(exception,GetMagickModule(),CorruptImageError,
      "InvalidColormapIndex","`%s'",image->filename);
}

static void ImportRedQuantum(QuantumInfo *quantum_info,
  const MagickSizeType number_pixels,const unsigned char *magick_restrict p,
  PixelPacket *magick_restrict q)
{
  ssize_t
    x;

  unsigned int
    pixel;

  switch (quantum_info->depth)
  {
    case 8:
    {
      unsigned char
        pixel;

      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushCharPixel(p,&pixel);
        SetPixelRed(q,ScaleCharToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 16:
    {
      unsigned short
        pixel;

      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelRed(q,ClampToQuantum((double) QuantumRange*
              (double) HalfToSinglePrecision(pixel)));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelRed(q,ScaleShortToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 32:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelRed(q,ScaleLongToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 24:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    case 64:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          double
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    default:
    {
      QuantumAny
        range;

      range=GetQuantumRange(quantum_info->depth);
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
  }
}

static void ImportRGBQuantum(QuantumInfo *quantum_info,
  const MagickSizeType number_pixels,const unsigned char *magick_restrict p,
  PixelPacket *magick_restrict q)
{
  QuantumAny
    range;

  ssize_t
    x;

  ssize_t
    bit;

  unsigned int
    pixel;

  switch (quantum_info->depth)
  {
    case 8:
    {
      unsigned char
        pixel;

      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushCharPixel(p,&pixel);
        SetPixelRed(q,ScaleCharToQuantum(pixel));
        p=PushCharPixel(p,&pixel);
        SetPixelGreen(q,ScaleCharToQuantum(pixel));
        p=PushCharPixel(p,&pixel);
        SetPixelBlue(q,ScaleCharToQuantum(pixel));
        SetPixelOpacity(q,OpaqueOpacity);
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 10:
    {
      range=GetQuantumRange(quantum_info->depth);
      if (quantum_info->pack == MagickFalse)
        {
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushLongPixel(quantum_info->endian,p,&pixel);
            SetPixelRed(q,ScaleAnyToQuantum((pixel >> 22) & 0x3ff,range));
            SetPixelGreen(q,ScaleAnyToQuantum((pixel >> 12) & 0x3ff,range));
            SetPixelBlue(q,ScaleAnyToQuantum((pixel >> 2) & 0x3ff,range));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      if (quantum_info->quantum == 32U)
        {
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumLongPixel(quantum_info,p,&pixel);
            SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
            p=PushQuantumLongPixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ScaleAnyToQuantum(pixel,range));
            p=PushQuantumLongPixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ScaleAnyToQuantum(pixel,range));
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelGreen(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelBlue(q,ScaleAnyToQuantum(pixel,range));
        q++;
      }
      break;
    }
    case 12:
    {
      range=GetQuantumRange(quantum_info->depth);
      if (quantum_info->pack == MagickFalse)
        {
          unsigned short
            pixel;

          for (x=0; x < (ssize_t) (3*number_pixels-1); x+=2)
          {
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            switch (x % 3)
            {
              default:
              case 0:
              {
                SetPixelRed(q,ScaleAnyToQuantum((QuantumAny) (pixel >> 4),
                  range));
                break;
              }
              case 1:
              {
                SetPixelGreen(q,ScaleAnyToQuantum((QuantumAny) (pixel >> 4),
                  range));
                break;
              }
              case 2:
              {
                SetPixelBlue(q,ScaleAnyToQuantum((QuantumAny) (pixel >> 4),
                  range));
                q++;
                break;
              }
            }
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            switch ((x+1) % 3)
            {
              default:
              case 0:
              {
                SetPixelRed(q,ScaleAnyToQuantum((QuantumAny) (pixel >> 4),
                  range));
                break;
              }
              case 1:
              {
                SetPixelGreen(q,ScaleAnyToQuantum((QuantumAny) (pixel >> 4),
                  range));
                break;
              }
              case 2:
              {
                SetPixelBlue(q,ScaleAnyToQuantum((QuantumAny) (pixel >> 4),
                  range));
                q++;
                break;
              }
            }
            p+=(ptrdiff_t) quantum_info->pad;
          }
          for (bit=0; bit < (ssize_t) (3*number_pixels % 2); bit++)
          {
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            switch ((x+bit) % 3)
            {
              default:
              case 0:
              {
                SetPixelRed(q,ScaleAnyToQuantum((QuantumAny) (pixel >> 4),
                  range));
                break;
              }
              case 1:
              {
                SetPixelGreen(q,ScaleAnyToQuantum((QuantumAny) (pixel >> 4),
                  range));
                break;
              }
              case 2:
              {
                SetPixelBlue(q,ScaleAnyToQuantum((QuantumAny) (pixel >> 4),
                  range));
                q++;
                break;
              }
            }
            p+=(ptrdiff_t) quantum_info->pad;
          }
          if (bit != 0)
            p++;
          break;
        }
      if (quantum_info->quantum == 32U)
        {
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumLongPixel(quantum_info,p,&pixel);
            SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
            p=PushQuantumLongPixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ScaleAnyToQuantum(pixel,range));
            p=PushQuantumLongPixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ScaleAnyToQuantum(pixel,range));
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelGreen(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelBlue(q,ScaleAnyToQuantum(pixel,range));
        q++;
      }
      break;
    }
    case 16:
    {
      unsigned short
        pixel;

      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelRed(q,ClampToQuantum((double) QuantumRange*
              (double) HalfToSinglePrecision(pixel)));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelGreen(q,ClampToQuantum((double) QuantumRange*
              (double) HalfToSinglePrecision(pixel)));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelBlue(q,ClampToQuantum((double) QuantumRange*
              (double) HalfToSinglePrecision(pixel)));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelRed(q,ScaleShortToQuantum(pixel));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelGreen(q,ScaleShortToQuantum(pixel));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelBlue(q,ScaleShortToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 32:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelRed(q,ScaleLongToQuantum(pixel));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelGreen(q,ScaleLongToQuantum(pixel));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelBlue(q,ScaleLongToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 24:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    case 64:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          double
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    default:
    {
      range=GetQuantumRange(quantum_info->depth);
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelGreen(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelBlue(q,ScaleAnyToQuantum(pixel,range));
        q++;
      }
      break;
    }
  }
}

static void ImportRGBAQuantum(QuantumInfo *quantum_info,
  const MagickSizeType number_pixels,const unsigned char *magick_restrict p,
  PixelPacket *magick_restrict q)
{
  QuantumAny
    range;

  ssize_t
    x;

  unsigned int
    pixel;

  switch (quantum_info->depth)
  {
    case 8:
    {
      unsigned char
        pixel;

      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushCharPixel(p,&pixel);
        SetPixelRed(q,ScaleCharToQuantum(pixel));
        p=PushCharPixel(p,&pixel);
        SetPixelGreen(q,ScaleCharToQuantum(pixel));
        p=PushCharPixel(p,&pixel);
        SetPixelBlue(q,ScaleCharToQuantum(pixel));
        p=PushCharPixel(p,&pixel);
        SetPixelAlpha(q,ScaleCharToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 10:
    {
      pixel=0;
      if (quantum_info->pack == MagickFalse)
        {
          ssize_t
            i;

          size_t
            quantum;

          ssize_t
            n;

          n=0;
          quantum=0;
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            for (i=0; i < 4; i++)
            {
              switch (n % 3)
              {
                case 0:
                {
                  p=PushLongPixel(quantum_info->endian,p,&pixel);
                  quantum=(size_t) (ScaleShortToQuantum((unsigned short)
                    (((pixel >> 22) & 0x3ff) << 6)));
                  break;
                }
                case 1:
                {
                  quantum=(size_t) (ScaleShortToQuantum((unsigned short)
                    (((pixel >> 12) & 0x3ff) << 6)));
                  break;
                }
                case 2:
                {
                  quantum=(size_t) (ScaleShortToQuantum((unsigned short)
                    (((pixel >> 2) & 0x3ff) << 6)));
                  break;
                }
              }
              switch (i)
              {
                case 0: SetPixelRed(q,quantum); break;
                case 1: SetPixelGreen(q,quantum); break;
                case 2: SetPixelBlue(q,quantum); break;
                case 3: SetPixelAlpha(q,quantum); break;
              }
              n++;
            }
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelRed(q,ScaleShortToQuantum((unsigned short) (pixel << 6)));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelGreen(q,ScaleShortToQuantum((unsigned short) (pixel << 6)));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelBlue(q,ScaleShortToQuantum((unsigned short) (pixel << 6)));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelAlpha(q,ScaleShortToQuantum((unsigned short) (pixel << 6)));
        q++;
      }
      break;
    }
    case 16:
    {
      unsigned short
        pixel;

      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelRed(q,ClampToQuantum((double) QuantumRange*(double)
              HalfToSinglePrecision(pixel)));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelGreen(q,ClampToQuantum((double) QuantumRange*(double)
              HalfToSinglePrecision(pixel)));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelBlue(q,ClampToQuantum((double) QuantumRange*(double)
              HalfToSinglePrecision(pixel)));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelAlpha(q,ClampToQuantum((double) QuantumRange* (double)
              HalfToSinglePrecision(pixel)));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelRed(q,ScaleShortToQuantum(pixel));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelGreen(q,ScaleShortToQuantum(pixel));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelBlue(q,ScaleShortToQuantum(pixel));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelAlpha(q,ScaleShortToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 32:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelAlpha(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelRed(q,ScaleLongToQuantum(pixel));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelGreen(q,ScaleLongToQuantum(pixel));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelBlue(q,ScaleLongToQuantum(pixel));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelAlpha(q,ScaleLongToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 24:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelAlpha(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    case 64:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          double
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelAlpha(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    default:
    {
      range=GetQuantumRange(quantum_info->depth);
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelGreen(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelBlue(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelAlpha(q,ScaleAnyToQuantum(pixel,range));
        q++;
      }
      break;
    }
  }
}

static void ImportRGBOQuantum(QuantumInfo *quantum_info,
  const MagickSizeType number_pixels,const unsigned char *magick_restrict p,
  PixelPacket *magick_restrict q)
{
  QuantumAny
    range;

  ssize_t
    x;

  unsigned int
    pixel;

  switch (quantum_info->depth)
  {
    case 8:
    {
      unsigned char
        pixel;

      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushCharPixel(p,&pixel);
        SetPixelRed(q,ScaleCharToQuantum(pixel));
        p=PushCharPixel(p,&pixel);
        SetPixelGreen(q,ScaleCharToQuantum(pixel));
        p=PushCharPixel(p,&pixel);
        SetPixelBlue(q,ScaleCharToQuantum(pixel));
        p=PushCharPixel(p,&pixel);
        SetPixelOpacity(q,ScaleCharToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 10:
    {
      pixel=0;
      if (quantum_info->pack == MagickFalse)
        {
          ssize_t
            i;

          size_t
            quantum;

          ssize_t
            n;

          n=0;
          quantum=0;
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            for (i=0; i < 4; i++)
            {
              switch (n % 3)
              {
                case 0:
                {
                  p=PushLongPixel(quantum_info->endian,p,&pixel);
                  quantum=(size_t) (ScaleShortToQuantum((unsigned short)
                    (((pixel >> 22) & 0x3ff) << 6)));
                  break;
                }
                case 1:
                {
                  quantum=(size_t) (ScaleShortToQuantum((unsigned short)
                    (((pixel >> 12) & 0x3ff) << 6)));
                  break;
                }
                case 2:
                {
                  quantum=(size_t) (ScaleShortToQuantum((unsigned short)
                    (((pixel >> 2) & 0x3ff) << 6)));
                  break;
                }
              }
              switch (i)
              {
                case 0: SetPixelRed(q,quantum); break;
                case 1: SetPixelGreen(q,quantum); break;
                case 2: SetPixelBlue(q,quantum); break;
                case 3: SetPixelOpacity(q,quantum); break;
              }
              n++;
            }
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelRed(q,ScaleShortToQuantum((unsigned short) (pixel << 6)));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelGreen(q,ScaleShortToQuantum((unsigned short) (pixel << 6)));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelBlue(q,ScaleShortToQuantum((unsigned short) (pixel << 6)));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelOpacity(q,ScaleShortToQuantum((unsigned short) (pixel << 6)));
        q++;
      }
      break;
    }
    case 16:
    {
      unsigned short
        pixel;

      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelRed(q,ClampToQuantum((double) QuantumRange*(double)
              HalfToSinglePrecision(pixel)));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelGreen(q,ClampToQuantum((double) QuantumRange*(double)
              HalfToSinglePrecision(pixel)));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelBlue(q,ClampToQuantum((double) QuantumRange*(double)
              HalfToSinglePrecision(pixel)));
            p=PushShortPixel(quantum_info->endian,p,&pixel);
            SetPixelOpacity(q,ClampToQuantum((double) QuantumRange*(double)
              HalfToSinglePrecision(pixel)));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelRed(q,ScaleShortToQuantum(pixel));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelGreen(q,ScaleShortToQuantum(pixel));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelBlue(q,ScaleShortToQuantum(pixel));
        p=PushShortPixel(quantum_info->endian,p,&pixel);
        SetPixelOpacity(q,ScaleShortToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 32:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p=PushQuantumFloatPixel(quantum_info,p,&pixel);
            SetPixelOpacity(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelRed(q,ScaleLongToQuantum(pixel));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelGreen(q,ScaleLongToQuantum(pixel));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelBlue(q,ScaleLongToQuantum(pixel));
        p=PushLongPixel(quantum_info->endian,p,&pixel);
        SetPixelOpacity(q,ScaleLongToQuantum(pixel));
        p+=(ptrdiff_t) quantum_info->pad;
        q++;
      }
      break;
    }
    case 24:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          float
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p=PushQuantumFloat24Pixel(quantum_info,p,&pixel);
            SetPixelOpacity(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    case 64:
    {
      if (quantum_info->format == FloatingPointQuantumFormat)
        {
          double
            pixel;

          for (x=0; x < (ssize_t) number_pixels; x++)
          {
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelRed(q,ClampToQuantum(pixel));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelGreen(q,ClampToQuantum(pixel));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelBlue(q,ClampToQuantum(pixel));
            p=PushDoublePixel(quantum_info,p,&pixel);
            SetPixelOpacity(q,ClampToQuantum(pixel));
            p+=(ptrdiff_t) quantum_info->pad;
            q++;
          }
          break;
        }
      magick_fallthrough;
    }
    default:
    {
      range=GetQuantumRange(quantum_info->depth);
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelRed(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelGreen(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelBlue(q,ScaleAnyToQuantum(pixel,range));
        p=PushQuantumPixel(quantum_info,p,&pixel);
        SetPixelOpacity(q,ScaleAnyToQuantum(pixel,range));
        q++;
      }
      break;
    }
  }
}

MagickExport size_t ImportQuantumPixels(Image *image,CacheView *image_view,
  const QuantumInfo *quantum_info,const QuantumType quantum_type,
  const unsigned char *magick_restrict pixels,ExceptionInfo *exception)
{
  MagickSizeType
    number_pixels;

  const unsigned char
    *magick_restrict p;

  IndexPacket
    *magick_restrict indexes;

  ssize_t
    x;

  PixelPacket
    *magick_restrict q;

  size_t
    extent;

  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(quantum_info != (QuantumInfo *) NULL);
  assert(quantum_info->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  if (pixels == (const unsigned char *) NULL)
    pixels=GetQuantumPixels(quantum_info);
  x=0;
  p=pixels;
  if (image_view == (CacheView *) NULL)
    {
      number_pixels=GetImageExtent(image);
      q=GetAuthenticPixelQueue(image);
      indexes=GetAuthenticIndexQueue(image);
    }
  else
    {
      number_pixels=GetCacheViewExtent(image_view);
      q=GetCacheViewAuthenticPixelQueue(image_view);
      indexes=GetCacheViewAuthenticIndexQueue(image_view);
    }
  ResetQuantumState((QuantumInfo *) quantum_info);
  extent=GetQuantumExtent(image,quantum_info,quantum_type);
  switch (quantum_type)
  {
    case AlphaQuantum:
    {
      ImportAlphaQuantum((QuantumInfo *) quantum_info,number_pixels,p,q);
      break;
    }
    case BGRQuantum:
    {
      ImportBGRQuantum((QuantumInfo *) quantum_info,number_pixels,p,q);
      break;
    }
    case BGRAQuantum:
    {
      ImportBGRAQuantum((QuantumInfo *) quantum_info,number_pixels,p,q);
      break;
    }
    case BGROQuantum:
    {
      ImportBGROQuantum((QuantumInfo *) quantum_info,number_pixels,p,q);
      break;
    }
    case BlackQuantum:
    {
      ImportBlackQuantum(image,(QuantumInfo *) quantum_info,number_pixels,p,q,
        indexes,exception);
      break;
    }
    case BlueQuantum:
    case YellowQuantum:
    {
      ImportBlueQuantum((QuantumInfo *) quantum_info,number_pixels,p,q);
      break;
    }
    case CbYCrYQuantum:
    {
      ImportCbYCrYQuantum(image,(QuantumInfo *) quantum_info,number_pixels,p,
        q);
      break;
    }
    case CMYKQuantum:
    {
      ImportCMYKQuantum(image,(QuantumInfo *) quantum_info,number_pixels,p,q,
        indexes,exception);
      break;
    }
    case CMYKAQuantum:
    {
      ImportCMYKAQuantum(image,(QuantumInfo *) quantum_info,number_pixels,p,q,
        indexes,exception);
      break;
    }
    case CMYKOQuantum:
    {
      ImportCMYKOQuantum(image,(QuantumInfo *) quantum_info,number_pixels,p,q,
        indexes,exception);
      break;
    }
    case GrayQuantum:
    {
      ImportGrayQuantum(image,(QuantumInfo *) quantum_info,number_pixels,p,q);
      break;
    }
    case GrayAlphaQuantum:
    {
      ImportGrayAlphaQuantum((QuantumInfo *) quantum_info,number_pixels,p,q);
      break;
    }
    case GreenQuantum:
    case MagentaQuantum:
    {
      ImportGreenQuantum((QuantumInfo *) quantum_info,number_pixels,p,q);
      break;
    }
    case IndexQuantum:
    {
      ImportIndexQuantum(image,(QuantumInfo *) quantum_info,number_pixels,p,q,
        indexes,exception);
      break;
    }
    case IndexAlphaQuantum:
    {
      ImportIndexAlphaQuantum(image,(QuantumInfo *) quantum_info,number_pixels,
        p,q,indexes,exception);
      break;
    }
    case RedQuantum:
    case CyanQuantum:
    {
      ImportRedQuantum((QuantumInfo *) quantum_info,number_pixels,p,q);
      break;
    }
    case RGBQuantum:
    case CbYCrQuantum:
    {
      ImportRGBQuantum((QuantumInfo *) quantum_info,number_pixels,p,q);
      break;
    }
    case RGBAQuantum:
    case CbYCrAQuantum:
    {
      ImportRGBAQuantum((QuantumInfo *) quantum_info,number_pixels,p,q);
      break;
    }
    case RGBOQuantum:
    {
      ImportRGBOQuantum((QuantumInfo *) quantum_info,number_pixels,p,q);
      break;
    }
    default:
      break;
  }
  if ((quantum_type == CbYCrQuantum) || (quantum_type == CbYCrAQuantum))
    {
      Quantum
        quantum;

      PixelPacket
        *magick_restrict q;

      q=GetAuthenticPixelQueue(image);
      if (image_view != (CacheView *) NULL)
        q=GetCacheViewAuthenticPixelQueue(image_view);
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        quantum=GetPixelRed(q);
        SetPixelRed(q,GetPixelGreen(q));
        SetPixelGreen(q,quantum);
        q++;
      }
    }
  if (quantum_info->alpha_type == AssociatedQuantumAlpha)
    {
      MagickRealType
        alpha;

      PixelPacket
        *magick_restrict q;

      /*
        Disassociate alpha.
      */
      q=GetAuthenticPixelQueue(image);
      if (image_view != (CacheView *) NULL)
        q=GetCacheViewAuthenticPixelQueue(image_view);
      indexes=GetAuthenticIndexQueue(image);
      for (x=0; x < (ssize_t) number_pixels; x++)
      {
        alpha=QuantumScale*(double) GetPixelAlpha(q);
        alpha=MagickSafeReciprocal(alpha);
        SetPixelRed(q,ClampToQuantum(alpha*(double) GetPixelRed(q)));
        SetPixelGreen(q,ClampToQuantum(alpha*(double) GetPixelGreen(q)));
        SetPixelBlue(q,ClampToQuantum(alpha*(double) GetPixelBlue(q)));
        if (image->colorspace == CMYKColorspace)
          SetPixelBlack(indexes+x,ClampToQuantum(alpha*(double) GetPixelBlack(
            indexes+x)));
        q++;
      }
    }
  return(extent);
}
