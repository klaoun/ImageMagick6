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

  MagickCore image composite private methods.
*/
#ifndef MAGICKCORE_COMPOSITE_PRIVATE_H
#define MAGICKCORE_COMPOSITE_PRIVATE_H

#include "magick/artifact.h"
#include "magick/color.h"
#include "magick/image.h"
#include "magick/image-private.h"
#include "magick/pixel-private.h"

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

/*
  ImageMagick Alpha Composite Inline Methods (special export)
*/
static inline MagickRealType RoundToUnity(const MagickRealType value)
{
  return(value < 0.0 ? 0.0 : (value > 1.0) ? 1.0 : value);
}

static inline MagickRealType MagickOver_(const MagickRealType p,
  const MagickRealType alpha,const MagickRealType q,const MagickRealType beta)
{
  MagickRealType
    Da,
    Sa;

  Sa=1.0-QuantumScale*alpha;
  Da=1.0-QuantumScale*beta;
  return(Sa*p+Da*q*(1.0-Sa));
}

static inline void MagickCompositeOver(const PixelPacket *p,
  const MagickRealType alpha,const PixelPacket *q,const MagickRealType beta,
  PixelPacket *composite)
{
  MagickRealType
    Da,
    gamma,
    Sa;

  /*
    Compose pixel p over pixel q with the given opacities.
  */
  Sa=1.0-QuantumScale*alpha;
  Da=1.0-QuantumScale*beta;
  gamma=Sa+Da-Sa*Da;
#if !defined(MAGICKCORE_HDRI_SUPPORT)
  SetPixelOpacity(composite,ClampToQuantum(QuantumRange*(1.0-
    RoundToUnity(gamma))));
  gamma=MagickSafeReciprocal(gamma);
  SetPixelRed(composite,ClampToQuantum(gamma*MagickOver_((MagickRealType)
    GetPixelRed(p),alpha,(MagickRealType) GetPixelRed(q),beta)));
  SetPixelGreen(composite,ClampToQuantum(gamma*MagickOver_((MagickRealType)
    GetPixelGreen(p),alpha,(MagickRealType) GetPixelGreen(q),beta)));
  SetPixelBlue(composite,ClampToQuantum(gamma*MagickOver_((MagickRealType)
    GetPixelBlue(p),alpha,(MagickRealType) GetPixelBlue(q),beta)));
#else
  SetPixelOpacity(composite,(MagickRealType) QuantumRange*(1.0-
    RoundToUnity(gamma)));
  gamma=MagickSafeReciprocal(gamma);
  SetPixelRed(composite,gamma*MagickOver_((MagickRealType)
    GetPixelRed(p),alpha,(MagickRealType) GetPixelRed(q),beta));
  SetPixelGreen(composite,gamma*MagickOver_((MagickRealType)
    GetPixelGreen(p),alpha,(MagickRealType) GetPixelGreen(q),beta));
  SetPixelBlue(composite,gamma*MagickOver_((MagickRealType)
    GetPixelBlue(p),alpha,(MagickRealType) GetPixelBlue(q),beta));
#endif
}

static inline void MagickPixelCompositeOver(const MagickPixelPacket *p,
  const MagickRealType alpha,const MagickPixelPacket *q,
  const MagickRealType beta,MagickPixelPacket *composite)
{
  MagickRealType
    Da,
    gamma,
    Sa;

  /*
    Compose pixel p over pixel q with the given opacities.
  */
  Sa=1.0-QuantumScale*alpha;
  Da=1.0-QuantumScale*beta;
  gamma=Sa+Da-Sa*Da;
  composite->opacity=(MagickRealType) QuantumRange*(1.0-RoundToUnity(gamma));
  gamma=MagickSafeReciprocal(gamma);
  composite->red=gamma*MagickOver_(p->red,alpha,q->red,beta);
  composite->green=gamma*MagickOver_(p->green,alpha,q->green,beta);
  composite->blue=gamma*MagickOver_(p->blue,alpha,q->blue,beta);
  if (q->colorspace == CMYKColorspace)
    composite->index=gamma*MagickOver_(p->index,alpha,q->index,beta);
}

static inline void MagickPixelCompositePlus(const MagickPixelPacket *p,
  const MagickRealType alpha,const MagickPixelPacket *q,
  const MagickRealType beta,MagickPixelPacket *composite)
{
  MagickRealType
    Da,
    gamma,
    Sa;

  /*
    Add two pixels with the given opacities.
  */
  Sa=1.0-QuantumScale*alpha;
  Da=1.0-QuantumScale*beta;
  gamma=RoundToUnity(Sa+Da);  /* 'Plus' blending -- not 'Over' blending */
  composite->opacity=(MagickRealType) QuantumRange*(1.0-RoundToUnity(gamma));
  gamma=MagickSafeReciprocal(gamma);
  composite->red=gamma*(Sa*p->red+Da*q->red);
  composite->green=gamma*(Sa*p->green+Da*q->green);
  composite->blue=gamma*(Sa*p->blue+Da*q->blue);
  if (q->colorspace == CMYKColorspace)
    composite->index=gamma*(Sa*p->index+Da*q->index);
}

/*
  Blend pixel colors p and q by the amount given.
*/
static inline void MagickPixelCompositeBlend(const MagickPixelPacket *p,
  const MagickRealType alpha,const MagickPixelPacket *q,
  const MagickRealType beta,MagickPixelPacket *composite)
{
  MagickPixelCompositePlus(p,(MagickRealType) QuantumRange-alpha*
    ((MagickRealType) QuantumRange-p->opacity),q,(MagickRealType)
    QuantumRange-beta*((MagickRealType) QuantumRange-q->opacity),composite);
}

/*
  Blend pixel colors p and q by the amount given and area.
*/
static inline void MagickPixelCompositeAreaBlend(const MagickPixelPacket *p,
  const MagickRealType alpha,const MagickPixelPacket *q,
  const MagickRealType beta,const MagickRealType area,
  MagickPixelPacket *composite)
{
  MagickPixelCompositePlus(p,(MagickRealType) QuantumRange-(1.0-area)*
    ((MagickRealType) QuantumRange-alpha),q,(MagickRealType) QuantumRange-
    area*((MagickRealType) QuantumRange-beta),composite);
}

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
