/*
  Copyright 1999 ImageMagick Studio LLC, a non-profit organization
  dedicated to making software imaging solutions freely available.

  You may not use this file except in compliance with the License.  You may
  obtain a copy of the License at

    https://imagemagick.org/script/license.php

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  MagickCore image colorspace private methods.
*/
#ifndef MAGICKCORE_COLORSPACE_PRIVATE_H
#define MAGICKCORE_COLORSPACE_PRIVATE_H

#include "magick/image.h"
#include "magick/image-private.h"
#include "magick/pixel.h"
#include "magick/pixel-accessor.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

static inline void ConvertCMYKToRGB(MagickPixelPacket *pixel)
{
  pixel->red=(((double) QuantumRange-(QuantumScale*pixel->red*((double)
    QuantumRange-pixel->index)+pixel->index)));
  pixel->green=(((double) QuantumRange-(QuantumScale*pixel->green*((double)
    QuantumRange-pixel->index)+pixel->index)));
  pixel->blue=(((double) QuantumRange-(QuantumScale*pixel->blue*((double)
    QuantumRange-pixel->index)+pixel->index)));
}

static inline void ConvertRGBToCMYK(MagickPixelPacket *pixel)
{
  MagickRealType
    black,
    blue,
    cyan,
    green,
    magenta,
    red,
    yellow;

  if (pixel->colorspace != sRGBColorspace)
    {
      red=QuantumScale*pixel->red;
      green=QuantumScale*pixel->green;
      blue=QuantumScale*pixel->blue;
    }
  else
    {
      red=QuantumScale*DecodePixelGamma(pixel->red);
      green=QuantumScale*DecodePixelGamma(pixel->green);
      blue=QuantumScale*DecodePixelGamma(pixel->blue);
    }
  if ((fabs((double) red) < MagickEpsilon) &&
      (fabs((double) green) < MagickEpsilon) &&
      (fabs((double) blue) < MagickEpsilon))
    {
      pixel->index=(MagickRealType) QuantumRange;
      return;
    }
  cyan=(MagickRealType) (1.0-red);
  magenta=(MagickRealType) (1.0-green);
  yellow=(MagickRealType) (1.0-blue);
  black=cyan;
  if (magenta < black)
    black=magenta;
  if (yellow < black)
    black=yellow;
  cyan=(MagickRealType) (MagickSafeReciprocal(1.0-black)*(cyan-black));
  magenta=(MagickRealType) (MagickSafeReciprocal(1.0-black)*(magenta-black));
  yellow=(MagickRealType) (MagickSafeReciprocal(1.0-black)*(yellow-black));
  pixel->colorspace=CMYKColorspace;
  pixel->red=(double) QuantumRange*cyan;
  pixel->green=(double) QuantumRange*magenta;
  pixel->blue=(double) QuantumRange*yellow;
  pixel->index=(double) QuantumRange*black;
}

static inline MagickBooleanType IsCMYKColorspace(
  const ColorspaceType colorspace)
{
  if (colorspace == CMYKColorspace)
    return(MagickTrue);
  return(MagickFalse);
}

static inline MagickBooleanType IsGrayColorspace(
  const ColorspaceType colorspace)
{
  if ((colorspace == LinearGRAYColorspace) || (colorspace == GRAYColorspace) ||
      (colorspace == Rec601LumaColorspace) ||
      (colorspace == Rec709LumaColorspace))
    return(MagickTrue);
  return(MagickFalse);
}

static inline MagickBooleanType IsHueCompatibleColorspace(
  const ColorspaceType colorspace)
{
  if ((colorspace == HCLColorspace) || (colorspace == HCLpColorspace) ||
      (colorspace == HSBColorspace) || (colorspace == HSIColorspace) ||
      (colorspace == HSLColorspace) || (colorspace == HSVColorspace))
    return(MagickTrue);
  return(MagickFalse);
}

static inline MagickBooleanType IsLabCompatibleColorspace(
  const ColorspaceType colorspace)
{
  if ((colorspace == LabColorspace) || (colorspace == LCHColorspace) ||
      (colorspace == LCHabColorspace) || (colorspace == LCHuvColorspace))
    return(MagickTrue);
  return(MagickFalse);
}

static inline MagickBooleanType IsRGBColorspace(const ColorspaceType colorspace)
{
  if ((colorspace == RGBColorspace) || (colorspace == scRGBColorspace) ||
      (colorspace == LinearGRAYColorspace))
    return(MagickTrue);
  return(MagickFalse);
}

static inline MagickBooleanType IssRGBColorspace(
  const ColorspaceType colorspace)
{
  if ((colorspace == sRGBColorspace) || (colorspace == TransparentColorspace))
    return(MagickTrue);
  return(MagickFalse);
}

static inline MagickBooleanType IssRGBCompatibleColorspace(
  const ColorspaceType colorspace)
{
  if ((colorspace == sRGBColorspace) || (colorspace == RGBColorspace) ||
      (colorspace == scRGBColorspace) ||
      (colorspace == TransparentColorspace) || (colorspace == GRAYColorspace) ||
      (colorspace == LinearGRAYColorspace))
    return(MagickTrue);
  return(MagickFalse);
}

static inline MagickBooleanType IsYCbCrCompatibleColorspace(
  const ColorspaceType colorspace)
{
  if ((colorspace == YCbCrColorspace) ||
      (colorspace == Rec709YCbCrColorspace) ||
      (colorspace == Rec601YCbCrColorspace))
    return(MagickTrue);
  return(MagickFalse);
}

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
