/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%        CCCC   OOO   M   M  PPPP    OOO   SSSSS  IIIII  TTTTT  EEEEE         %
%       C      O   O  MM MM  P   P  O   O  SS       I      T    E             %
%       C      O   O  M M M  PPPP   O   O   SSS     I      T    EEE           %
%       C      O   O  M   M  P      O   O     SS    I      T    E             %
%        CCCC   OOO   M   M  P       OOO   SSSSS  IIIII    T    EEEEE         %
%                                                                             %
%                                                                             %
%                     MagickCore Image Composite Methods                      %
%                                                                             %
%                              Software Design                                %
%                                   Cristy                                    %
%                                 July 1992                                   %
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
%
*/

/*
  Include declarations.
*/
#include "magick/studio.h"
#include "magick/accelerate-private.h"
#include "magick/artifact.h"
#include "magick/cache-view.h"
#include "magick/channel.h"
#include "magick/client.h"
#include "magick/color.h"
#include "magick/color-private.h"
#include "magick/colorspace.h"
#include "magick/colorspace-private.h"
#include "magick/composite.h"
#include "magick/composite-private.h"
#include "magick/constitute.h"
#include "magick/draw.h"
#include "magick/fx.h"
#include "magick/gem.h"
#include "magick/geometry.h"
#include "magick/image.h"
#include "magick/image-private.h"
#include "magick/list.h"
#include "magick/log.h"
#include "magick/monitor.h"
#include "magick/monitor-private.h"
#include "magick/memory_.h"
#include "magick/option.h"
#include "magick/pixel-private.h"
#include "magick/property.h"
#include "magick/quantum.h"
#include "magick/resample.h"
#include "magick/resource_.h"
#include "magick/string_.h"
#include "magick/thread-private.h"
#include "magick/threshold.h"
#include "magick/token.h"
#include "magick/utility.h"
#include "magick/version.h"

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%   C o m p o s i t e I m a g e C h a n n e l                                 %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  CompositeImageChannel() returns the second image composited onto the first
%  at the specified offset, using the specified composite method.
%
%  The format of the CompositeImageChannel method is:
%
%      MagickBooleanType CompositeImage(Image *image,
%        const CompositeOperator compose,Image *source_image,
%        const ssize_t x_offset,const ssize_t y_offset)
%      MagickBooleanType CompositeImageChannel(Image *image,
%        const ChannelType channel,const CompositeOperator compose,
%        Image *source_image,const ssize_t x_offset,const ssize_t y_offset)
%
%  A description of each parameter follows:
%
%    o image: the canvas image, modified by he composition
%
%    o channel: the channel.
%
%    o compose: This operator affects how the composite is applied to
%      the image.  The operators and how they are utilized are listed here
%      http://www.w3.org/TR/SVG12/#compositing.
%
%    o source_image: the composite (source) image.
%
%    o x_offset: the column offset of the composited image.
%
%    o y_offset: the row offset of the composited image.
%
%  Extra Controls from Image meta-data in 'source_image' (artifacts)
%
%    o "compose:args"
%        A string containing extra numerical arguments for specific compose
%        methods, generally expressed as a 'geometry' or a comma separated list
%        of numbers.
%
%        Compose methods needing such arguments include "BlendCompositeOp" and
%        "DisplaceCompositeOp".
%
%    o "compose:outside-overlay"
%        Modify how the composition is to effect areas not directly covered
%        by the 'source_image' at the offset given.  Normally this is
%        dependant on the 'compose' method, especially Duff-Porter methods.
%
%        If set to "false" then disable all normal handling of pixels not
%        covered by the source_image.  Typically used for repeated tiling
%        of the source_image by the calling API.
%
%        Previous to IM v6.5.3-3  this was called "modify-outside-overlay"
%
*/

/*
** Programmers notes on SVG specification.
**
** A Composition is defined by...
**   Color Function :  f(Sc,Dc)  where Sc and Dc are the normizalized colors
**    Blending areas :  X = 1    for area of overlap   ie: f(Sc,Dc)
**                      Y = 1    for source preserved
**                      Z = 1    for canvas preserved
**
** Conversion to transparency (then optimized)
**    Dca' = f(Sc, Dc)*Sa*Da + Y*Sca*(1-Da) + Z*Dca*(1-Sa)
**    Da'  = X*Sa*Da + Y*Sa*(1-Da) + Z*Da*(1-Sa)
**
** Where...
**   Sca = Sc*Sa     normalized Source color divided by Source alpha
**   Dca = Dc*Da     normalized Dest color divided by Dest alpha
**   Dc' = Dca'/Da'  the desired color value for this channel.
**
** Da' in in the follow formula as 'gamma'  The resulting alpla value.
**
**
** Most functions use a blending mode of over (X=1,Y=1,Z=1)
** this results in the following optimizations...
**   gamma = Sa+Da-Sa*Da;
**   gamma = 1 - QuantumScale*alpha * QuantumScale*beta;
**   opacity = QuantumScale*alpha*beta;  // over blend, optimized 1-Gamma
**
** The above SVG definitions also define that Mathematical Composition
** methods should use a 'Over' blending mode for Alpha Channel.
** It however was not applied for composition modes of 'Plus', 'Minus',
** the modulus versions of 'Add' and 'Subtract'.
**
**
** Mathematical operator changes to be applied from IM v6.7...
**
**  1/ Modulus modes 'Add' and 'Subtract' are obsoleted and renamed
**     'ModulusAdd' and 'ModulusSubtract' for clarity.
**
**  2/ All mathematical compositions work as per the SVG specification
**     with regard to blending.  This now includes 'ModulusAdd' and
**     'ModulusSubtract'.
**
**  3/ When the special channel flag 'sync' (syncronize channel updates)
**     is turned off (enabled by default) then mathematical compositions are
**     only performed on the channels specified, and are applied
**     independantally of each other.  In other words the mathematics is
**     performed as 'pure' mathematical operations, rather than as image
**     operations.
*/

static inline MagickRealType Atop(const MagickRealType p,
  const MagickRealType Sa,const MagickRealType q,
  const MagickRealType magick_unused(Da))
{
  magick_unreferenced(Da);

  return(p*Sa+q*(1.0-Sa));  /* Da optimized out,  Da/gamma => 1.0 */
}

static inline void CompositeAtop(const MagickPixelPacket *p,
  const MagickPixelPacket *q,MagickPixelPacket *composite)
{
  MagickRealType
    Sa;

  Sa=1.0-QuantumScale*p->opacity;  /* simplify and speed up equations */
  composite->opacity=q->opacity;   /* optimized  Da = 1.0-Gamma */
  composite->red=Atop(p->red,Sa,q->red,1.0);
  composite->green=Atop(p->green,Sa,q->green,1.0);
  composite->blue=Atop(p->blue,Sa,q->blue,1.0);
  if (q->colorspace == CMYKColorspace)
    composite->index=Atop(p->index,Sa,q->index,1.0);
}

/*
  What is this Composition method for? Can't find any specification!
  WARNING this is not doing correct 'over' blend handling (Anthony Thyssen).
*/
static inline void CompositeBumpmap(const MagickPixelPacket *p,
  const MagickPixelPacket *q,MagickPixelPacket *composite)
{
  MagickRealType
    intensity;

  intensity=MagickPixelIntensity(p);
  composite->red=QuantumScale*intensity*q->red;
  composite->green=QuantumScale*intensity*q->green;
  composite->blue=QuantumScale*intensity*q->blue;
  composite->opacity=(MagickRealType) QuantumScale*intensity*p->opacity;
  if (q->colorspace == CMYKColorspace)
    composite->index=QuantumScale*intensity*q->index;
}

static inline void CompositeClear(const MagickPixelPacket *q,
  MagickPixelPacket *composite)
{
  composite->opacity=(MagickRealType) TransparentOpacity;
  composite->red=0.0;
  composite->green=0.0;
  composite->blue=0.0;
  if (q->colorspace == CMYKColorspace)
    composite->index=0.0;
}

static MagickRealType ColorBurn(const MagickRealType Sca,
  const MagickRealType Sa,const MagickRealType Dca,const MagickRealType Da)
{
  double
    SaSca;

  if ((fabs((double) Sca) < MagickEpsilon) &&
      (fabs((double) (Dca-Da)) < MagickEpsilon))
    return(Sa*Da+Dca*(1.0-Sa));
  if (Sca < MagickEpsilon)
    return(Dca*(1.0-Sa));
  SaSca=Sa*MagickSafeReciprocal(Sca);
  return(Sa*Da-Sa*MagickMin(Da,(Da-Dca)*SaSca)+Sca*(1.0-Da)+Dca*(1.0-Sa));
}

static inline void CompositeColorBurn(const MagickPixelPacket *p,
  const MagickPixelPacket *q,MagickPixelPacket *composite)
{
  MagickRealType
    Da,
    gamma,
    Sa;

  Sa=1.0-QuantumScale*p->opacity;  /* simplify and speed up equations */
  Da=1.0-QuantumScale*q->opacity;
  gamma=RoundToUnity(Sa+Da-Sa*Da); /* over blend, as per SVG doc */
  composite->opacity=(MagickRealType) QuantumRange*(1.0-gamma);
  gamma=(MagickRealType) QuantumRange/(fabs((double) gamma) < MagickEpsilon ?
    MagickEpsilon : gamma);
  composite->red=gamma*ColorBurn(QuantumScale*p->red*Sa,Sa,QuantumScale*
    q->red*Da,Da);
  composite->green=gamma*ColorBurn(QuantumScale*p->green*Sa,Sa,QuantumScale*
    q->green*Da,Da);
  composite->blue=gamma*ColorBurn(QuantumScale*p->blue*Sa,Sa,QuantumScale*
    q->blue*Da,Da);
  if (q->colorspace == CMYKColorspace)
    composite->index=gamma*ColorBurn(QuantumScale*p->index*Sa,Sa,QuantumScale*
      q->index*Da,Da);
}


static MagickRealType ColorDodge(const MagickRealType Sca,
  const MagickRealType Sa,const MagickRealType Dca,const MagickRealType Da)
{
  /*
    Oct 2004 SVG specification.
  */
  if ((Sca*Da+Dca*Sa) >= Sa*Da)
    return(Sa*Da+Sca*(1.0-Da)+Dca*(1.0-Sa));
  return(Dca*Sa*Sa*MagickSafeReciprocal(Sa-Sca)+Sca*(1.0-Da)+Dca*(1.0-Sa));
#if 0
  /*
    New specification, March 2009 SVG specification.  This specification was
    also wrong of non-overlap cases.
  */
  if ((fabs(Sca-Sa) < MagickEpsilon) && (fabs(Dca) < MagickEpsilon))
    return(Sca*(1.0-Da));
  if (fabs(Sca-Sa) < MagickEpsilon)
    return(Sa*Da+Sca*(1.0-Da)+Dca*(1.0-Sa));
  return(Sa*MagickMin(Da,Dca*Sa/(Sa-Sca)));
#endif
#if 0
  /*
    Working from first principles using the original formula:

       f(Sc,Dc) = Dc/(1-Sc)

    This works correctly! Looks like the 2004 model was right but just
    required a extra condition for correct handling.
  */
  if ((fabs(Sca-Sa) < MagickEpsilon) && (fabs(Dca) < MagickEpsilon))
    return(Sca*(1.0-Da)+Dca*(1.0-Sa));
  if (fabs(Sca-Sa) < MagickEpsilon)
    return(Sa*Da+Sca*(1.0-Da)+Dca*(1.0-Sa));
  return(Dca*Sa*Sa/(Sa-Sca)+Sca*(1.0-Da)+Dca*(1.0-Sa));
#endif
}

static inline void CompositeColorDodge(const MagickPixelPacket *p,
  const MagickPixelPacket *q,MagickPixelPacket *composite)
{
  MagickRealType
    Da,
    gamma,
    Sa;

  Sa=1.0-QuantumScale*p->opacity;  /* simplify and speed up equations */
  Da=1.0-QuantumScale*q->opacity;
  gamma=RoundToUnity(Sa+Da-Sa*Da); /* over blend, as per SVG doc */
  composite->opacity=(MagickRealType) QuantumRange*(1.0-gamma);
  gamma=(MagickRealType) QuantumRange/(fabs((double) gamma) < MagickEpsilon ?
    MagickEpsilon : gamma);
  composite->red=gamma*ColorDodge(QuantumScale*p->red*Sa,Sa,QuantumScale*
    q->red*Da,Da);
  composite->green=gamma*ColorDodge(QuantumScale*p->green*Sa,Sa,QuantumScale*
    q->green*Da,Da);
  composite->blue=gamma*ColorDodge(QuantumScale*p->blue*Sa,Sa,QuantumScale*
    q->blue*Da,Da);
  if (q->colorspace == CMYKColorspace)
    composite->index=gamma*ColorDodge(QuantumScale*p->index*Sa,Sa,QuantumScale*
      q->index*Da,Da);
}

static inline MagickRealType Darken(const MagickRealType p,
  const MagickRealType alpha,const MagickRealType q,const MagickRealType beta)
{
  if (p < q)
    return(MagickOver_(p,alpha,q,beta));  /* src-over */
  return(MagickOver_(q,beta,p,alpha));    /* dst-over */
}

static inline void CompositeDarken(const MagickPixelPacket *p,
  const MagickPixelPacket *q,const ChannelType channel,
  MagickPixelPacket *composite)
{
  /*
    Darken is equivalent to a 'Minimum' method
    OR a greyscale version of a binary 'Or'
    OR the 'Intersection' of pixel sets.
  */
  double
    gamma;

  if ( (channel & SyncChannels) != 0 ) {
    composite->opacity=QuantumScale*p->opacity*q->opacity; /* Over Blend */
    gamma=1.0-QuantumScale*composite->opacity;
    gamma=MagickSafeReciprocal(gamma);
    composite->red=gamma*Darken(p->red,p->opacity,q->red,q->opacity);
    composite->green=gamma*Darken(p->green,p->opacity,q->green,q->opacity);
    composite->blue=gamma*Darken(p->blue,p->opacity,q->blue,q->opacity);
    if (q->colorspace == CMYKColorspace)
      composite->index=gamma*Darken(p->index,p->opacity,q->index,q->opacity);
  }
  else { /* handle channels as separate grayscale channels */
    if ( (channel & AlphaChannel) != 0 )
      composite->opacity=MagickMax(p->opacity,q->opacity);
    if ( (channel & RedChannel) != 0 )
      composite->red=MagickMin(p->red,q->red);
    if ( (channel & GreenChannel) != 0 )
      composite->green=MagickMin(p->green,q->green);
    if ( (channel & BlueChannel) != 0 )
      composite->blue=MagickMin(p->blue,q->blue);
    if ( (channel & IndexChannel) != 0 && q->colorspace == CMYKColorspace)
      composite->index=MagickMin(p->index,q->index);
  }
}

static inline void CompositeDarkenIntensity(const MagickPixelPacket *p,
  const MagickPixelPacket *q,const ChannelType channel,
  MagickPixelPacket *composite)
{
  /*
    Select the pixel based on the intensity level.
    If 'Sync' flag select whole pixel based on alpha weighted intensity.
    Otherwise use intensity only, but restrict copy according to channel.
  */
  if ( (channel & SyncChannels) != 0 ) {
    MagickRealType
      Da,
      Sa;

    Sa=1.0-QuantumScale*p->opacity;
    Da=1.0-QuantumScale*q->opacity;
    *composite = (Sa*MagickPixelIntensity(p) < Da*MagickPixelIntensity(q))
              ? *p : *q;
  }
  else {
    int from_p = (MagickPixelIntensity(p) < MagickPixelIntensity(q));
    if ( (channel & AlphaChannel) != 0 )
      composite->opacity = from_p ? p->opacity : q->opacity;
    if ( (channel & RedChannel) != 0 )
      composite->red = from_p ? p->red : q->red;
    if ( (channel & GreenChannel) != 0 )
      composite->green = from_p ? p->green : q->green;
    if ( (channel & BlueChannel) != 0 )
      composite->blue = from_p ? p->blue : q->blue;
    if ( (channel & IndexChannel) != 0 && q->colorspace == CMYKColorspace)
      composite->index = from_p ? p->index : q->index;
  }
}

static inline MagickRealType Difference(const MagickRealType p,
  const MagickRealType Sa,const MagickRealType q,const MagickRealType Da)
{
  /* Optimized by Multipling by QuantumRange (taken from gamma).  */
  return(Sa*p+Da*q-Sa*Da*2.0*MagickMin(p,q));
}

static inline void CompositeDifference(const MagickPixelPacket *p,
  const MagickPixelPacket *q,const ChannelType channel,
  MagickPixelPacket *composite)
{
  double
    gamma;

  MagickRealType
    Da,
    Sa;

  Sa=1.0-QuantumScale*p->opacity;  /* simplify and speed up equations */
  Da=1.0-QuantumScale*q->opacity;
  if ( (channel & SyncChannels) != 0 ) {
    gamma=RoundToUnity(Sa+Da-Sa*Da); /* over blend, as per SVG doc */
    composite->opacity=(MagickRealType) QuantumRange*(1.0-gamma);
    gamma=MagickSafeReciprocal(gamma);
    /* Values are not normalized as an optimization.  */
    composite->red=gamma*Difference(p->red,Sa,q->red,Da);
    composite->green=gamma*Difference(p->green,Sa,q->green,Da);
    composite->blue=gamma*Difference(p->blue,Sa,q->blue,Da);
    if (q->colorspace == CMYKColorspace)
      composite->index=gamma*Difference(p->index,Sa,q->index,Da);
  }
  else { /* handle channels as separate grayscale channels */
    if ( (channel & AlphaChannel) != 0 )
      composite->opacity=(MagickRealType) QuantumRange-
        fabs((double) (p->opacity-q->opacity));
    if ( (channel & RedChannel) != 0 )
      composite->red=fabs((double) (p->red-q->red));
    if ( (channel & GreenChannel) != 0 )
      composite->green=fabs((double) (p->green-q->green));
    if ( (channel & BlueChannel) != 0 )
      composite->blue=fabs((double) (p->blue-q->blue));
    if ( (channel & IndexChannel) != 0 && q->colorspace == CMYKColorspace)
      composite->index=fabs((double) (p->index-q->index));
  }
}

static MagickRealType Divide(const MagickRealType Sca,const MagickRealType Sa,
  const MagickRealType Dca,const MagickRealType Da)
{
  /*
    Divide Source by Destination

      f(Sc,Dc) = Sc / Dc

    But with appropriate handling for special case of Dc == 0 specifically
    so that   f(Black,Black)=Black  and  f(non-Black,Black)=White.
    It is however also important to correctly do 'over' alpha blending which
    is why the formula becomes so complex.
  */
  if ((fabs((double) Sca) < MagickEpsilon) &&
      (fabs((double) Dca) < MagickEpsilon))
    return(Sca*(1.0-Da)+Dca*(1.0-Sa));
  if (fabs((double) Dca) < MagickEpsilon)
    return(Sa*Da+Sca*(1.0-Da)+Dca*(1.0-Sa));
  return(Sca*Da*Da*MagickSafeReciprocal(Dca)+Sca*(1.0-Da)+Dca*(1.0-Sa));
}

static inline void CompositeDivide(const MagickPixelPacket *p,
  const MagickPixelPacket *q,const ChannelType channel,
  MagickPixelPacket *composite)
{
  MagickRealType
    Da,
    gamma,
    Sa;

  Sa=1.0-QuantumScale*p->opacity;  /* simplify and speed up equations */
  Da=1.0-QuantumScale*q->opacity;
  if ( (channel & SyncChannels) != 0 ) {
    gamma=RoundToUnity(Sa+Da-Sa*Da); /* over blend, as per SVG doc */
    composite->opacity=(MagickRealType) QuantumRange*(1.0-gamma);
    gamma=(MagickRealType) QuantumRange/(fabs((double) gamma) < MagickEpsilon ?
      MagickEpsilon : gamma);
    composite->red=gamma*Divide(QuantumScale*p->red*Sa,Sa,QuantumScale*
      q->red*Da,Da);
    composite->green=gamma*Divide(QuantumScale*p->green*Sa,Sa,QuantumScale*
      q->green*Da,Da);
    composite->blue=gamma*Divide(QuantumScale*p->blue*Sa,Sa,QuantumScale*
      q->blue*Da,Da);
    if (q->colorspace == CMYKColorspace)
      composite->index=gamma*Divide(QuantumScale*p->index*Sa,Sa,QuantumScale*
        q->index*Da,Da);
  }
  else { /* handle channels as separate grayscale channels */
    if ( (channel & AlphaChannel) != 0 )
      composite->opacity=(MagickRealType) QuantumRange*(1.0-Divide(Sa,1.0,Da,1.0));
    if ( (channel & RedChannel) != 0 )
      composite->red=(MagickRealType) QuantumRange*
          Divide(QuantumScale*p->red,1.0,QuantumScale*q->red,1.0);
    if ( (channel & GreenChannel) != 0 )
      composite->green=(MagickRealType) QuantumRange*
          Divide(QuantumScale*p->green,1.0,QuantumScale*q->green,1.0);
    if ( (channel & BlueChannel) != 0 )
      composite->blue=(MagickRealType) QuantumRange*
          Divide(QuantumScale*p->blue,1.0,QuantumScale*q->blue,1.0);
    if ( (channel & IndexChannel) != 0 && q->colorspace == CMYKColorspace)
      composite->index=(MagickRealType) QuantumRange*
          Divide(QuantumScale*p->index,1.0,QuantumScale*q->index,1.0);
  }
}

static MagickRealType Exclusion(const MagickRealType Sca,
  const MagickRealType Sa,const MagickRealType Dca,const MagickRealType Da)
{
  return(Sca*Da+Dca*Sa-2.0*Sca*Dca+Sca*(1.0-Da)+Dca*(1.0-Sa));
}

static inline void CompositeExclusion(const MagickPixelPacket *p,
  const MagickPixelPacket *q,const ChannelType channel,
  MagickPixelPacket *composite)
{
  MagickRealType
    gamma,
    Sa,
    Da;

  Sa=1.0-QuantumScale*p->opacity;  /* simplify and speed up equations */
  Da=1.0-QuantumScale*q->opacity;
  if ( (channel & SyncChannels) != 0 ) {
    gamma=RoundToUnity(Sa+Da-Sa*Da); /* over blend, as per SVG doc */
    composite->opacity=(MagickRealType) QuantumRange*(1.0-gamma);
    gamma=(MagickRealType) QuantumRange/(fabs((double) gamma) < MagickEpsilon ?
      MagickEpsilon : gamma);
    composite->red=gamma*Exclusion(QuantumScale*p->red*Sa,Sa,QuantumScale*
      q->red*Da,Da);
    composite->green=gamma*Exclusion(QuantumScale*p->green*Sa,Sa,QuantumScale*
      q->green*Da,Da);
    composite->blue=gamma*Exclusion(QuantumScale*p->blue*Sa,Sa,QuantumScale*
      q->blue*Da,Da);
    if (q->colorspace == CMYKColorspace)
      composite->index=gamma*Exclusion(QuantumScale*p->index*Sa,Sa,QuantumScale*
        q->index*Da,Da);
  }
  else { /* handle channels as separate grayscale channels */
    if ((channel & AlphaChannel) != 0)
      composite->opacity=(MagickRealType) QuantumRange*(1.0-Exclusion(Sa,1.0,Da,1.0));
    if ((channel & RedChannel) != 0)
      composite->red=(MagickRealType) QuantumRange*Exclusion(QuantumScale*p->red,1.0,
        QuantumScale*q->red,1.0);
    if ((channel & GreenChannel) != 0)
      composite->green=(MagickRealType) QuantumRange*Exclusion(QuantumScale*p->green,
        1.0,QuantumScale*q->green,1.0);
    if ((channel & BlueChannel) != 0)
      composite->blue=(MagickRealType) QuantumRange*Exclusion(QuantumScale*p->blue,1.0,
        QuantumScale*q->blue,1.0);
    if (((channel & IndexChannel) != 0) && (q->colorspace == CMYKColorspace))
      composite->index=(MagickRealType) QuantumRange*Exclusion(QuantumScale*p->index,
        1.0,QuantumScale*q->index,1.0);
  }
}

static MagickRealType HardLight(const MagickRealType Sca,
  const MagickRealType Sa,const MagickRealType Dca,const MagickRealType Da)
{
  if ((2.0*Sca) < Sa)
    return(2.0*Sca*Dca+Sca*(1.0-Da)+Dca*(1.0-Sa));
  return(Sa*Da-2.0*(Da-Dca)*(Sa-Sca)+Sca*(1.0-Da)+Dca*(1.0-Sa));
}

static inline void CompositeHardLight(const MagickPixelPacket *p,
  const MagickPixelPacket *q,MagickPixelPacket *composite)
{
  MagickRealType
    Da,
    gamma,
    Sa;

  Sa=1.0-QuantumScale*p->opacity;  /* simplify and speed up equations */
  Da=1.0-QuantumScale*q->opacity;
  gamma=RoundToUnity(Sa+Da-Sa*Da); /* over blend, as per SVG doc */
  composite->opacity=(MagickRealType) QuantumRange*(1.0-gamma);
  gamma=(MagickRealType) QuantumRange/(fabs((double) gamma) < MagickEpsilon ?
    MagickEpsilon : gamma);
  composite->red=gamma*HardLight(QuantumScale*p->red*Sa,Sa,QuantumScale*
    q->red*Da,Da);
  composite->green=gamma*HardLight(QuantumScale*p->green*Sa,Sa,QuantumScale*
    q->green*Da,Da);
  composite->blue=gamma*HardLight(QuantumScale*p->blue*Sa,Sa,QuantumScale*
    q->blue*Da,Da);
  if (q->colorspace == CMYKColorspace)
    composite->index=gamma*HardLight(QuantumScale*p->index*Sa,Sa,QuantumScale*
      q->index*Da,Da);
}

static MagickRealType HardMix(const MagickRealType Sca,
  const MagickRealType Dca)
{
  if ((Sca+Dca) < (MagickRealType) QuantumRange)
    return(0.0);
  else
    return(1.0);
}

static inline void CompositeHardMix(const MagickPixelPacket *p,
  const MagickPixelPacket *q,MagickPixelPacket *composite)
{
  MagickRealType
    Da,
    gamma,
    Sa;

  Sa=1.0-QuantumScale*p->opacity;  /* simplify and speed up equations */
  Da=1.0-QuantumScale*q->opacity;
  gamma=RoundToUnity(Sa+Da-Sa*Da); /* over blend, as per SVG doc */
  composite->opacity=(MagickRealType) QuantumRange*(1.0-gamma);
  gamma=(MagickRealType) QuantumRange/(fabs((double) gamma) < MagickEpsilon ?
    MagickEpsilon : gamma);
  composite->red=gamma*HardMix(p->red*Sa,q->red*Da);
  composite->green=gamma*HardMix(p->green*Sa,q->green*Da);
  composite->blue=gamma*HardMix(p->blue*Sa,q->blue*Da);
  if (q->colorspace == CMYKColorspace)
    composite->index=gamma*HardMix(p->index*Sa,q->index*Da);
}

static void HCLComposite(const double hue,const double chroma,const double luma,
  MagickRealType *red,MagickRealType *green,MagickRealType *blue)
{
  double
    b,
    c,
    g,
    h,
    m,
    r,
    x;

  /*
    Convert HCL to RGB colorspace.
  */
  assert(red != (MagickRealType *) NULL);
  assert(green != (MagickRealType *) NULL);
  assert(blue != (MagickRealType *) NULL);
  h=6.0*hue;
  c=chroma;
  x=c*(1.0-fabs(fmod(h,2.0)-1.0));
  r=0.0;
  g=0.0;
  b=0.0;
  if ((0.0 <= h) && (h < 1.0))
    {
      r=c;
      g=x;
    }
  else
    if ((1.0 <= h) && (h < 2.0))
      {
        r=x;
        g=c;
      }
    else
      if ((2.0 <= h) && (h < 3.0))
        {
          g=c;
          b=x;
        }
      else
        if ((3.0 <= h) && (h < 4.0))
          {
            g=x;
            b=c;
          }
        else
          if ((4.0 <= h) && (h < 5.0))
            {
              r=x;
              b=c;
            }
          else
            if ((5.0 <= h) && (h < 6.0))
              {
                r=c;
                b=x;
              }
  m=luma-(0.298839*r+0.586811*g+0.114350*b);
  *red=(MagickRealType) QuantumRange*(r+m);
  *green=(MagickRealType) QuantumRange*(g+m);
  *blue=(MagickRealType) QuantumRange*(b+m);
}

static void CompositeHCL(const MagickRealType red,const MagickRealType green,
  const MagickRealType blue,double *hue,double *chroma,double *luma)
{
  double
    b,
    c,
    g,
    h,
    max,
    r;

  /*
    Convert RGB to HCL colorspace.
  */
  assert(hue != (double *) NULL);
  assert(chroma != (double *) NULL);
  assert(luma != (double *) NULL);
  r=(double) red;
  g=(double) green;
  b=(double) blue;
  max=MagickMax(r,MagickMax(g,b));
  c=max-(double) MagickMin(r,MagickMin(g,b));
  h=0.0;
  if (c == 0)
    h=0.0;
  else
    if (red == (MagickRealType) max)
      h=fmod((g-b)/c+6.0,6.0);
    else
      if (green == (MagickRealType) max)
        h=((b-r)/c)+2.0;
      else
        if (blue == (MagickRealType) max)
          h=((r-g)/c)+4.0;
  *hue=(h/6.0);
  *chroma=QuantumScale*c;
  *luma=QuantumScale*(0.298839*r+0.586811*g+0.114350*b);
}

static inline MagickRealType In(const MagickRealType p,const MagickRealType Sa,
  const MagickRealType magick_unused(q),const MagickRealType Da)
{
  magick_unreferenced(q);

  return(Sa*p*Da);
}

static inline void CompositeIn(const MagickPixelPacket *p,
  const MagickPixelPacket *q,MagickPixelPacket *composite)
{
  double
    gamma;

  MagickRealType
    Sa,
    Da;

  Sa=1.0-QuantumScale*p->opacity;  /* simplify and speed up equations */
  Da=1.0-QuantumScale*q->opacity;
  gamma=Sa*Da;
  composite->opacity=(MagickRealType) QuantumRange*(1.0-gamma);
  gamma=MagickSafeReciprocal(gamma);
  composite->red=gamma*In(p->red,Sa,q->red,Da);
  composite->green=gamma*In(p->green,Sa,q->green,Da);
  composite->blue=gamma*In(p->blue,Sa,q->blue,Da);
  if (q->colorspace == CMYKColorspace)
    composite->index=gamma*In(p->index,Sa,q->index,Da);
}

static inline MagickRealType Lighten(const MagickRealType p,
  const MagickRealType alpha,const MagickRealType q,const MagickRealType beta)
{
   if (p > q)
     return(MagickOver_(p,alpha,q,beta));  /* src-over */
   return(MagickOver_(q,beta,p,alpha));    /* dst-over */
}

static inline void CompositeLighten(const MagickPixelPacket *p,
  const MagickPixelPacket *q,const ChannelType channel,
  MagickPixelPacket *composite)
{
  /*
    Lighten is also equvalent to a 'Maximum' method
    OR a greyscale version of a binary 'And'
    OR the 'Union' of pixel sets.
  */
  double
    gamma;

  if ( (channel & SyncChannels) != 0 ) {
    composite->opacity=QuantumScale*p->opacity*q->opacity; /* Over Blend */
    gamma=1.0-QuantumScale*composite->opacity;
    gamma=MagickSafeReciprocal(gamma);
    composite->red=gamma*Lighten(p->red,p->opacity,q->red,q->opacity);
    composite->green=gamma*Lighten(p->green,p->opacity,q->green,q->opacity);
    composite->blue=gamma*Lighten(p->blue,p->opacity,q->blue,q->opacity);
    if (q->colorspace == CMYKColorspace)
      composite->index=gamma*Lighten(p->index,p->opacity,q->index,q->opacity);
  }
  else { /* handle channels as separate grayscale channels */
    if ( (channel & AlphaChannel) != 0 )
      composite->opacity=MagickMin(p->opacity,q->opacity);
    if ( (channel & RedChannel) != 0 )
      composite->red=MagickMax(p->red,q->red);
    if ( (channel & GreenChannel) != 0 )
      composite->green=MagickMax(p->green,q->green);
    if ( (channel & BlueChannel) != 0 )
      composite->blue=MagickMax(p->blue,q->blue);
    if ( (channel & IndexChannel) != 0 && q->colorspace == CMYKColorspace)
      composite->index=MagickMax(p->index,q->index);
  }
}

static inline void CompositeLightenIntensity(const MagickPixelPacket *p,
  const MagickPixelPacket *q,const ChannelType channel,
  MagickPixelPacket *composite)
{
  /*
    Select the pixel based on the intensity level.
    If 'Sync' flag select whole pixel based on alpha weighted intensity.
    Otherwise use Intenisty only, but restrict copy according to channel.
  */
  if ( (channel & SyncChannels) != 0 ) {
    MagickRealType
      Da,
      Sa;

    Sa=1.0-QuantumScale*p->opacity;
    Da=1.0-QuantumScale*q->opacity;
    *composite = (Sa*MagickPixelIntensity(p) > Da*MagickPixelIntensity(q))
               ? *p : *q;
  }
  else {
    int from_p = (MagickPixelIntensity(p) > MagickPixelIntensity(q));
    if ( (channel & AlphaChannel) != 0 )
      composite->opacity = from_p ? p->opacity : q->opacity;
    if ( (channel & RedChannel) != 0 )
      composite->red = from_p ? p->red : q->red;
    if ( (channel & GreenChannel) != 0 )
      composite->green = from_p ? p->green : q->green;
    if ( (channel & BlueChannel) != 0 )
      composite->blue = from_p ? p->blue : q->blue;
    if ( (channel & IndexChannel) != 0 && q->colorspace == CMYKColorspace)
      composite->index = from_p ? p->index : q->index;
  }
}

#if 0
static inline MagickRealType LinearDodge(const MagickRealType Sca,
  const MagickRealType Sa,const MagickRealType Dca,const MagickRealType Da)
{
  /*
    LinearDodge: simplifies to a trivial formula
    f(Sc,Dc) = Sc + Dc
    Dca' = Sca + Dca
  */
  return(Sca+Dca);
}
#endif

static inline void CompositeLinearDodge(const MagickPixelPacket *p,
  const MagickPixelPacket *q,MagickPixelPacket *composite)
{
  double
    gamma;

  MagickRealType
    Da,
    Sa;

  Sa=1.0-QuantumScale*p->opacity;  /* simplify and speed up equations */
  Da=1.0-QuantumScale*q->opacity;
  gamma=RoundToUnity(Sa+Da-Sa*Da); /* over blend, as per SVG doc */
  composite->opacity=(MagickRealType) QuantumRange*(1.0-gamma);
  gamma=MagickSafeReciprocal(gamma);
  composite->red=gamma*(p->red*Sa+q->red*Da);
  composite->green=gamma*(p->green*Sa+q->green*Da);
  composite->blue=gamma*(p->blue*Sa+q->blue*Da);
  if (q->colorspace == CMYKColorspace)
    composite->index=gamma*(p->index*Sa+q->index*Da);
}


static inline MagickRealType LinearBurn(const MagickRealType Sca,
  const MagickRealType Sa,const MagickRealType Dca,const MagickRealType Da)
{
  /*
    LinearBurn: as defined by Abode Photoshop, according to
    http://www.simplefilter.de/en/basics/mixmods.html is:

    f(Sc,Dc) = Sc + Dc - 1
  */
  return(Sca+Dca-Sa*Da);
}

static inline void CompositeLinearBurn(const MagickPixelPacket *p,
  const MagickPixelPacket *q,MagickPixelPacket *composite)
{
  MagickRealType
    Da,
    gamma,
    Sa;

  Sa=1.0-QuantumScale*p->opacity;  /* simplify and speed up equations */
  Da=1.0-QuantumScale*q->opacity;
  gamma=RoundToUnity(Sa+Da-Sa*Da); /* over blend, as per SVG doc */
  composite->opacity=(MagickRealType) QuantumRange*(1.0-gamma);
  gamma=(MagickRealType) QuantumRange/(fabs((double) gamma) < MagickEpsilon ?
    MagickEpsilon : gamma);
  composite->red=gamma*LinearBurn(QuantumScale*p->red*Sa,Sa,QuantumScale*
    q->red*Da,Da);
  composite->green=gamma*LinearBurn(QuantumScale*p->green*Sa,Sa,QuantumScale*
    q->green*Da,Da);
  composite->blue=gamma*LinearBurn(QuantumScale*p->blue*Sa,Sa,QuantumScale*
    q->blue*Da,Da);
  if (q->colorspace == CMYKColorspace)
    composite->index=gamma*LinearBurn(QuantumScale*p->index*Sa,Sa,QuantumScale*
      q->index*Da,Da);
}

static inline MagickRealType LinearLight(const MagickRealType Sca,
  const MagickRealType Sa,const MagickRealType Dca,const MagickRealType Da)
{
#if 0
  /*
    Previous formula, was only valid for fully-opaque images.
  */
  return(Dca+2*Sca-1.0);
#else
  /*
    LinearLight: as defined by Abode Photoshop, according to
    http://www.simplefilter.de/en/basics/mixmods.html is:

      f(Sc,Dc) = Dc + 2*Sc - 1
  */
  return((Sca-Sa)*Da+Sca+Dca);
#endif
}

static inline void CompositeLinearLight(const MagickPixelPacket *p,
  const MagickPixelPacket *q,MagickPixelPacket *composite)
{
  MagickRealType
    Da,
    gamma,
    Sa;

  Sa=1.0-QuantumScale*p->opacity;  /* simplify and speed up equations */
  Da=1.0-QuantumScale*q->opacity;
  gamma=RoundToUnity(Sa+Da-Sa*Da); /* over blend, as per SVG doc */
  composite->opacity=(MagickRealType) QuantumRange*(1.0-gamma);
  gamma=(MagickRealType) QuantumRange/(fabs((double) gamma) < MagickEpsilon ?
    MagickEpsilon : gamma);
  composite->red=gamma*LinearLight(QuantumScale*p->red*Sa,Sa,QuantumScale*
    q->red*Da,Da);
  composite->green=gamma*LinearLight(QuantumScale*p->green*Sa,Sa,QuantumScale*
    q->green*Da,Da);
  composite->blue=gamma*LinearLight(QuantumScale*p->blue*Sa,Sa,QuantumScale*
    q->blue*Da,Da);
  if (q->colorspace == CMYKColorspace)
    composite->index=gamma*LinearLight(QuantumScale*p->index*Sa,Sa,QuantumScale*
      q->index*Da,Da);
}

static inline MagickRealType Mathematics(const MagickRealType Sca,
  const MagickRealType Sa,const MagickRealType Dca,const MagickRealType Da,
  const GeometryInfo *geometry_info)
{
  /*
    'Mathematics' a free form user control mathematical composition is defined
    as...

       f(Sc,Dc) = A*Sc*Dc + B*Sc + C*Dc + D

    Where the arguments A,B,C,D are (currently) passed to composite as
    a command separated 'geometry' string in "compose:args" image artifact.

       A = a->rho,   B = a->sigma,  C = a->xi,  D = a->psi

    Applying the SVG transparency formula (see above), we get...

     Dca' = Sa*Da*f(Sc,Dc) + Sca*(1.0-Da) + Dca*(1.0-Sa)

     Dca' = A*Sca*Dca + B*Sca*Da + C*Dca*Sa + D*Sa*Da + Sca*(1.0-Da) +
       Dca*(1.0-Sa)
  */
  return(geometry_info->rho*Sca*Dca+geometry_info->sigma*Sca*Da+
    geometry_info->xi*Dca*Sa+geometry_info->psi*Sa*Da+Sca*(1.0-Da)+
    Dca*(1.0-Sa));
}

static inline void CompositeMathematics(const MagickPixelPacket *p,
  const MagickPixelPacket *q,const ChannelType channel, const GeometryInfo
  *args, MagickPixelPacket *composite)
{
  double
    gamma;

  MagickRealType
    Da,
    Sa;

  Sa=1.0-QuantumScale*p->opacity; /* ??? - AT */
  Da=1.0-QuantumScale*q->opacity;
  if ( (channel & SyncChannels) != 0 ) {
    gamma=RoundToUnity(Sa+Da-Sa*Da); /* over blend, as per SVG doc */
    composite->opacity=(MagickRealType) QuantumRange*(1.0-gamma);
    gamma=(MagickRealType) QuantumRange/(fabs((double) gamma) < MagickEpsilon ?
      MagickEpsilon : gamma);
    composite->red=gamma*Mathematics(QuantumScale*p->red*Sa,Sa,QuantumScale*
      q->red*Da,Da,args);
    composite->green=gamma*Mathematics(QuantumScale*p->green*Sa,Sa,QuantumScale*
      q->green*Da,Da,args);
    composite->blue=gamma*Mathematics(QuantumScale*p->blue*Sa,Sa,QuantumScale*
      q->blue*Da,Da,args);
    if (q->colorspace == CMYKColorspace)
      composite->index=gamma*Mathematics(QuantumScale*p->index*Sa,Sa,
        QuantumScale*q->index*Da,Da,args);
  }
  else { /* handle channels as separate grayscale channels */
    if ( (channel & AlphaChannel) != 0 )
      composite->opacity=(MagickRealType) QuantumRange*(1.0-
        Mathematics(Sa,1.0,Da,1.0,args));
    if ( (channel & RedChannel) != 0 )
      composite->red=(MagickRealType) QuantumRange*
          Mathematics(QuantumScale*p->red,1.0,QuantumScale*q->red,1.0,args);
    if ( (channel & GreenChannel) != 0 )
      composite->green=(MagickRealType) QuantumRange*
          Mathematics(QuantumScale*p->green,1.0,QuantumScale*q->green,1.0,args);
    if ( (channel & BlueChannel) != 0 )
      composite->blue=(MagickRealType) QuantumRange*
          Mathematics(QuantumScale*p->blue,1.0,QuantumScale*q->blue,1.0,args);
    if ( (channel & IndexChannel) != 0 && q->colorspace == CMYKColorspace)
      composite->index=(MagickRealType) QuantumRange*
          Mathematics(QuantumScale*p->index,1.0,QuantumScale*q->index,1.0,args);
  }

}

static inline void CompositePlus(const MagickPixelPacket *p,
  const MagickPixelPacket *q,const ChannelType channel,
  MagickPixelPacket *composite)
{
  if ( (channel & SyncChannels) != 0 ) {
    /*
      NOTE: "Plus" does not use 'over' alpha-blending but uses a
      special 'plus' form of alph-blending. It is the ONLY mathematical
      operator to do this. this is what makes it different to the
      otherwise equivalent "LinearDodge" composition method.

      Note however that color channels are still effected by the alpha channel
      as a result of the blending, making it just as useless for independant
      channel maths, just like all other mathematical composition methods.

      As such the removal of the 'sync' flag, is still a usful convention.

      The MagickPixelCompositePlus() function is defined in
      "composite-private.h" so it can also be used for Image Blending.
    */
    MagickPixelCompositePlus(p,p->opacity,q,q->opacity,composite);
  }
  else { /* handle channels as separate grayscale channels */
    if ( (channel & AlphaChannel) != 0 )
      composite->opacity=p->opacity+q->opacity-(MagickRealType) QuantumRange;
    if ( (channel & RedChannel) != 0 )
      composite->red=p->red+q->red;
    if ( (channel & GreenChannel) != 0 )
      composite->green=p->green+q->green;
    if ( (channel & BlueChannel) != 0 )
      composite->blue=p->blue+q->blue;
    if ( (channel & IndexChannel) != 0 && q->colorspace == CMYKColorspace)
      composite->index=p->index+q->index;
  }
}

static inline MagickRealType Minus(const MagickRealType Sca,
  const MagickRealType Sa,const MagickRealType Dca,
  const MagickRealType magick_unused(Da))
{
  /*
    Minus Source from Destination

      f(Sc,Dc) = Sc - Dc

  */
  magick_unreferenced(Da);

  return(Sca+Dca-2*Dca*Sa);
}

static inline void CompositeMinus(const MagickPixelPacket *p,
  const MagickPixelPacket *q,const ChannelType channel,
  MagickPixelPacket *composite)
{
  double
    gamma;

  MagickRealType
    Da,
    Sa;

  Sa=1.0-QuantumScale*p->opacity;  /* simplify and speed up equations */
  Da=1.0-QuantumScale*q->opacity;
  if ( (channel & SyncChannels) != 0 ) {
    gamma=RoundToUnity(Sa+Da-Sa*Da); /* over blend, as per SVG doc */
    composite->opacity=(MagickRealType) QuantumRange*(1.0-gamma);
    gamma=MagickSafeReciprocal(gamma);
    composite->red=gamma*Minus(p->red*Sa,Sa,q->red*Da,Da);
    composite->green=gamma*Minus(p->green*Sa,Sa,q->green*Da,Da);
    composite->blue=gamma*Minus(p->blue*Sa,Sa,q->blue*Da,Da);
    if (q->colorspace == CMYKColorspace)
      composite->index=gamma*Minus(p->index*Sa,Sa,q->index*Da,Da);
  }
  else { /* handle channels as separate grayscale channels */
    if ( (channel & AlphaChannel) != 0 )
      composite->opacity=(MagickRealType) QuantumRange*(1.0-(Sa-Da));
    if ( (channel & RedChannel) != 0 )
      composite->red=p->red-q->red;
    if ( (channel & GreenChannel) != 0 )
      composite->green=p->green-q->green;
    if ( (channel & BlueChannel) != 0 )
      composite->blue=p->blue-q->blue;
    if ( (channel & IndexChannel) != 0 && q->colorspace == CMYKColorspace)
      composite->index=p->index-q->index;
  }
}

static inline MagickRealType ModulusAdd(const MagickRealType Sc,
  const MagickRealType Sa,const MagickRealType Dc,const MagickRealType Da)
{
  if (((Sc*Sa)+(Dc*Da)) <= (MagickRealType) QuantumRange)
    return((Sc*Sa)+Dc*Da);
  return(((Sc*Sa)+Dc*Da)-(MagickRealType) QuantumRange);
}

static inline void CompositeModulusAdd(const MagickPixelPacket *p,
  const MagickPixelPacket *q,const ChannelType channel,
  MagickPixelPacket *composite)
{
  if ( (channel & SyncChannels) != 0 ) {
    double
      gamma;

    MagickRealType
      Sa,
      Da;

    Sa=1.0-QuantumScale*p->opacity;  /* simplify and speed up equations */
    Da=1.0-QuantumScale*q->opacity;
    gamma=RoundToUnity(Sa+Da-Sa*Da); /* over blend, as per SVG doc */
    composite->opacity=(MagickRealType) QuantumRange*(1.0-gamma);
    gamma=MagickSafeReciprocal(gamma);
    composite->red=ModulusAdd(p->red,Sa,q->red,Da);
    composite->green=ModulusAdd(p->green,Sa,q->green,Da);
    composite->blue=ModulusAdd(p->blue,Sa,q->blue,Da);
    if (q->colorspace == CMYKColorspace)
      composite->index=ModulusAdd(p->index,Sa,q->index,Da);
  }
  else { /* handle channels as separate grayscale channels */
    if ( (channel & AlphaChannel) != 0 )
      composite->opacity=(MagickRealType) QuantumRange-ModulusAdd(
        (MagickRealType) QuantumRange-p->opacity,1.0,(MagickRealType)
        QuantumRange-q->opacity,1.0);
    if ( (channel & RedChannel) != 0 )
      composite->red=ModulusAdd(p->red,1.0,q->red,1.0);
    if ( (channel & GreenChannel) != 0 )
      composite->green=ModulusAdd(p->green,1.0,q->green,1.0);
    if ( (channel & BlueChannel) != 0 )
      composite->blue=ModulusAdd(p->blue,1.0,q->blue,1.0);
    if ( (channel & IndexChannel) != 0 && q->colorspace == CMYKColorspace)
      composite->index=ModulusAdd(p->index,1.0,q->index,1.0);
  }
}

static inline MagickRealType ModulusSubtract(const MagickRealType Sc,
  const MagickRealType Sa,const MagickRealType Dc,const MagickRealType Da)
{
  if (((Sc*Sa)-(Dc*Da)) <= 0.0)
    return((Sc*Sa)-Dc*Da);
  return(((Sc*Sa)-Dc*Da)+(MagickRealType) QuantumRange);
}

static inline void CompositeModulusSubtract(const MagickPixelPacket *p,
  const MagickPixelPacket *q,const ChannelType channel,
  MagickPixelPacket *composite)
{
  if ( (channel & SyncChannels) != 0 ) {
    double
      gamma;

    MagickRealType
      Da,
      Sa;

    Sa=1.0-QuantumScale*p->opacity;  /* simplify and speed up equations */
    Da=1.0-QuantumScale*q->opacity;
    gamma = RoundToUnity(Sa+Da-Sa*Da);
    composite->opacity=(MagickRealType) QuantumRange*(1.0-gamma);
    gamma=MagickSafeReciprocal(gamma);
    composite->red=ModulusSubtract(p->red,Sa,q->red,Da);
    composite->green=ModulusSubtract(p->green,Sa,q->green,Da);
    composite->blue=ModulusSubtract(p->blue,Sa,q->blue,Da);
    if (q->colorspace == CMYKColorspace)
      composite->index=ModulusSubtract(p->index,Sa,q->index,Da);
  }
  else { /* handle channels as separate grayscale channels */
    if ( (channel & AlphaChannel) != 0 )
      composite->opacity=(MagickRealType) QuantumRange-ModulusSubtract((double)
        QuantumRange-p->opacity,1.0,(MagickRealType) QuantumRange-q->opacity,1.0);
    if ( (channel & RedChannel) != 0 )
      composite->red=ModulusSubtract(p->red,1.0,q->red,1.0);
    if ( (channel & GreenChannel) != 0 )
      composite->green=ModulusSubtract(p->green,1.0,q->green,1.0);
    if ( (channel & BlueChannel) != 0 )
      composite->blue=ModulusSubtract(p->blue,1.0,q->blue,1.0);
    if ( (channel & IndexChannel) != 0 && q->colorspace == CMYKColorspace)
      composite->index=ModulusSubtract(p->index,1.0,q->index,1.0);
  }
}

static  inline MagickRealType Multiply(const MagickRealType Sca,
  const MagickRealType Sa,const MagickRealType Dca,const MagickRealType Da)
{
  return(Sca*Dca+Sca*(1.0-Da)+Dca*(1.0-Sa));
}

static inline void CompositeMultiply(const MagickPixelPacket *p,
  const MagickPixelPacket *q,const ChannelType channel,
  MagickPixelPacket *composite)
{
  MagickRealType
    Da,
    gamma,
    Sa;

  Sa=1.0-QuantumScale*p->opacity;  /* simplify and speed up equations */
  Da=1.0-QuantumScale*q->opacity;
  if ( (channel & SyncChannels) != 0 ) {
    gamma=RoundToUnity(Sa+Da-Sa*Da); /* over blend, as per SVG doc */
    composite->opacity=(MagickRealType) QuantumRange*(1.0-gamma);
    gamma=(MagickRealType) QuantumRange/(fabs((double) gamma) < MagickEpsilon ?
      MagickEpsilon : gamma);
    composite->red=gamma*Multiply(QuantumScale*p->red*Sa,Sa,QuantumScale*
      q->red*Da,Da);
    composite->green=gamma*Multiply(QuantumScale*p->green*Sa,Sa,QuantumScale*
      q->green*Da,Da);
    composite->blue=gamma*Multiply(QuantumScale*p->blue*Sa,Sa,QuantumScale*
      q->blue*Da,Da);
    if (q->colorspace == CMYKColorspace)
      composite->index=gamma*Multiply(QuantumScale*p->index*Sa,Sa,QuantumScale*
        q->index*Da,Da);
  }
  else { /* handle channels as separate grayscale channels */
    if ( (channel & AlphaChannel) != 0 )
      composite->opacity=(MagickRealType) QuantumRange*(1.0-Sa*Da);
    if ( (channel & RedChannel) != 0 )
      composite->red=QuantumScale*p->red*q->red;
    if ( (channel & GreenChannel) != 0 )
      composite->green=QuantumScale*p->green*q->green;
    if ( (channel & BlueChannel) != 0 )
      composite->blue=QuantumScale*p->blue*q->blue;
    if ( (channel & IndexChannel) != 0 && q->colorspace == CMYKColorspace)
      composite->index=QuantumScale*p->index*q->index;
  }
}

static inline MagickRealType Out(const MagickRealType p,
  const MagickRealType Sa,const MagickRealType magick_unused(q),
  const MagickRealType Da)
{
  magick_unreferenced(q);

  return(Sa*p*(1.0-Da));
}

static inline void CompositeOut(const MagickPixelPacket *p,
  const MagickPixelPacket *q,MagickPixelPacket *composite)
{
  double
    gamma;

  MagickRealType
    Da,
    Sa;

  Sa=1.0-QuantumScale*p->opacity;  /* simplify and speed up equations */
  Da=1.0-QuantumScale*q->opacity;
  gamma=Sa*(1.0-Da);
  composite->opacity=(MagickRealType) QuantumRange*(1.0-gamma);
  gamma=MagickSafeReciprocal(gamma);
  composite->red=gamma*Out(p->red,Sa,q->red,Da);
  composite->green=gamma*Out(p->green,Sa,q->green,Da);
  composite->blue=gamma*Out(p->blue,Sa,q->blue,Da);
  if (q->colorspace == CMYKColorspace)
    composite->index=gamma*Out(p->index,Sa,q->index,Da);
}

static MagickRealType PegtopLight(const MagickRealType Sca,
  const MagickRealType Sa,const MagickRealType Dca,const MagickRealType Da)
{
  /*
    PegTop: A Soft-Light alternative: A continuous version of the Softlight
    function, producing very similar results.

    f(Sc,Dc) = Dc^2*(1-2*Sc) + 2*Sc*Dc

    See http://www.pegtop.net/delphi/articles/blendmodes/softlight.htm.
  */
  if (fabs((double) Da) < MagickEpsilon)
    return(Sca);
  return(Dca*Dca*(Sa-2.0*Sca)*MagickSafeReciprocal(Da)+Sca*(2.0*Dca+1.0-Da)+Dca*(1.0-Sa));
}

static inline void CompositePegtopLight(const MagickPixelPacket *p,
  const MagickPixelPacket *q,MagickPixelPacket *composite)
{
  MagickRealType
    Da,
    gamma,
    Sa;

  Sa=1.0-QuantumScale*p->opacity;  /* simplify and speed up equations */
  Da=1.0-QuantumScale*q->opacity;
  gamma=RoundToUnity(Sa+Da-Sa*Da); /* over blend, as per SVG doc */
  composite->opacity=(MagickRealType) QuantumRange*(1.0-gamma);
  gamma=(MagickRealType) QuantumRange/(fabs((double) gamma) < MagickEpsilon ?
    MagickEpsilon : gamma);
  composite->red=gamma*PegtopLight(QuantumScale*p->red*Sa,Sa,QuantumScale*
    q->red*Da,Da);
  composite->green=gamma*PegtopLight(QuantumScale*p->green*Sa,Sa,QuantumScale*
    q->green*Da,Da);
  composite->blue=gamma*PegtopLight(QuantumScale*p->blue*Sa,Sa,QuantumScale*
    q->blue*Da,Da);
  if (q->colorspace == CMYKColorspace)
    composite->index=gamma*PegtopLight(QuantumScale*p->index*Sa,Sa,QuantumScale*
      q->index*Da,Da);
}

static MagickRealType PinLight(const MagickRealType Sca,
  const MagickRealType Sa,const MagickRealType Dca,const MagickRealType Da)
{
  /*
    PinLight: A Photoshop 7 composition method
    http://www.simplefilter.de/en/basics/mixmods.html

    f(Sc,Dc) = Dc<2*Sc-1 ? 2*Sc-1 : Dc>2*Sc   ? 2*Sc : Dc
  */
  if (Dca*Sa < Da*(2*Sca-Sa))
    return(Sca*(Da+1.0)-Sa*Da+Dca*(1.0-Sa));
  if ((Dca*Sa) > (2*Sca*Da))
    return(Sca*Da+Sca+Dca*(1.0-Sa));
  return(Sca*(1.0-Da)+Dca);
}

static inline void CompositePinLight(const MagickPixelPacket *p,
  const MagickPixelPacket *q,MagickPixelPacket *composite)
{
  MagickRealType
    Da,
    gamma,
    Sa;

  Sa=1.0-QuantumScale*p->opacity;  /* simplify and speed up equations */
  Da=1.0-QuantumScale*q->opacity;
  gamma=RoundToUnity(Sa+Da-Sa*Da); /* over blend, as per SVG doc */
  composite->opacity=(MagickRealType) QuantumRange*(1.0-gamma);
  gamma=(MagickRealType) QuantumRange/(fabs((double) gamma) < MagickEpsilon ?
    MagickEpsilon : gamma);
  composite->red=gamma*PinLight(QuantumScale*p->red*Sa,Sa,QuantumScale*
    q->red*Da,Da);
  composite->green=gamma*PinLight(QuantumScale*p->green*Sa,Sa,QuantumScale*
    q->green*Da,Da);
  composite->blue=gamma*PinLight(QuantumScale*p->blue*Sa,Sa,QuantumScale*
    q->blue*Da,Da);
  if (q->colorspace == CMYKColorspace)
    composite->index=gamma*PinLight(QuantumScale*p->index*Sa,Sa,QuantumScale*
      q->index*Da,Da);
}

static inline MagickRealType Screen(const MagickRealType Sca,
  const MagickRealType Dca)
{
  /* Screen:  A negated multiply
     f(Sc,Dc) = 1.0-(1.0-Sc)*(1.0-Dc)
  */
  return(Sca+Dca-Sca*Dca);
}

static inline void CompositeScreen(const MagickPixelPacket *p,
  const MagickPixelPacket *q,const ChannelType channel,
  MagickPixelPacket *composite)
{
  double
    gamma;

  MagickRealType
    Da,
    Sa;

  Sa=1.0-QuantumScale*p->opacity;  /* simplify and speed up equations */
  Da=1.0-QuantumScale*q->opacity;
  if ( (channel & SyncChannels) != 0 ) {
    gamma=RoundToUnity(Sa+Da-Sa*Da); /* over blend, as per SVG doc */
    composite->opacity=(MagickRealType) QuantumRange*(1.0-gamma);
    Sa*=(MagickRealType) QuantumScale;
    Da*=(MagickRealType) QuantumScale; /* optimization */
    gamma=(MagickRealType) QuantumRange/(fabs((double) gamma) < MagickEpsilon ?
      MagickEpsilon : gamma);
    composite->red=gamma*Screen(p->red*Sa,q->red*Da);
    composite->green=gamma*Screen(p->green*Sa,q->green*Da);
    composite->blue=gamma*Screen(p->blue*Sa,q->blue*Da);
    if (q->colorspace == CMYKColorspace)
      composite->index=gamma*Screen(p->index*Sa,q->index*Da);
    }
  else { /* handle channels as separate grayscale channels */
    if ( (channel & AlphaChannel) != 0 )
      composite->opacity=(MagickRealType) QuantumRange*(1.0-Screen(Sa,Da));
    if ( (channel & RedChannel) != 0 )
      composite->red=(MagickRealType) QuantumRange*Screen(QuantumScale*p->red,
           QuantumScale*q->red);
    if ( (channel & GreenChannel) != 0 )
      composite->green=(MagickRealType) QuantumRange*Screen(QuantumScale*p->green,
           QuantumScale*q->green);
    if ( (channel & BlueChannel) != 0 )
      composite->blue=(MagickRealType) QuantumRange*Screen(QuantumScale*p->blue,
           QuantumScale*q->blue);
    if ( (channel & IndexChannel) != 0 && q->colorspace == CMYKColorspace)
      composite->index=(MagickRealType) QuantumRange*Screen(QuantumScale*p->index,
           QuantumScale*q->index);
  }
}

static MagickRealType SoftLight(const MagickRealType Sca,
  const MagickRealType Sa, const MagickRealType Dca, const MagickRealType Da)
{
  MagickRealType
    alpha,
    beta;

  alpha=Dca*MagickSafeReciprocal(Da);
  if ((2.0*Sca) < Sa)
    return(Dca*(Sa+(2.0*Sca-Sa)*(1.0-alpha))+Sca*(1.0-Da)+Dca*(1.0-Sa));
  if (((2.0*Sca) > Sa) && ((4.0*Dca) <= Da))
    {
      beta=Dca*Sa+Da*(2.0*Sca-Sa)*(4.0*alpha*(4.0*alpha+1.0)*(alpha-1.0)+7.0*
        alpha)+Sca*(1.0-Da)+Dca*(1.0-Sa);
      return(beta);
    }
  beta=Dca*Sa+Da*(2.0*Sca-Sa)*(pow(alpha,0.5)-alpha)+Sca*(1.0-Da)+Dca*(1.0-Sa);
  return(beta);
}

static inline void CompositeSoftLight(const MagickPixelPacket *p,
  const MagickPixelPacket *q,MagickPixelPacket *composite)
{
  MagickRealType
    Da,
    gamma,
    Sa;

  Sa=1.0-QuantumScale*p->opacity;  /* simplify and speed up equations */
  Da=1.0-QuantumScale*q->opacity;
  gamma=RoundToUnity(Sa+Da-Sa*Da); /* over blend, as per SVG doc */
  composite->opacity=(MagickRealType) QuantumRange*(1.0-gamma);
  gamma=(MagickRealType) QuantumRange/(fabs((double) gamma) < MagickEpsilon ?
    MagickEpsilon : gamma);
  composite->red=gamma*SoftLight(QuantumScale*p->red*Sa,Sa,QuantumScale*
    q->red*Da,Da);
  composite->green=gamma*SoftLight(QuantumScale*p->green*Sa,Sa,QuantumScale*
    q->green*Da,Da);
  composite->blue=gamma*SoftLight(QuantumScale*p->blue*Sa,Sa,QuantumScale*
    q->blue*Da,Da);
  if (q->colorspace == CMYKColorspace)
    composite->index=gamma*SoftLight(QuantumScale*p->index*Sa,Sa,QuantumScale*
      q->index*Da,Da);
}

/*
  Deprecated
  Multiply difference by amount, if differance larger than threshold???
  What use this is is completely unknown
  The Opacity calculation appears to be inverted  -- Anthony Thyssen
*/
static inline MagickRealType Threshold(const MagickRealType p,
  const MagickRealType q,const MagickRealType threshold,
  const MagickRealType amount)
{
  MagickRealType
    delta;

  delta=p-q;
  if ((MagickRealType) fabs((double) (2.0*delta)) < threshold)
    return(q);
  return(q+delta*amount);
}

static inline void CompositeThreshold(const MagickPixelPacket *p,
  const MagickPixelPacket *q,const MagickRealType threshold,
  const MagickRealType amount,MagickPixelPacket *composite)
{
  composite->red=Threshold(p->red,q->red,threshold,amount);
  composite->green=Threshold(p->green,q->green,threshold,amount);
  composite->blue=Threshold(p->blue,q->blue,threshold,amount);
  composite->opacity=(MagickRealType) QuantumRange-Threshold(p->opacity,
    q->opacity,threshold,amount);
  if (q->colorspace == CMYKColorspace)
    composite->index=Threshold(p->index,q->index,threshold,amount);
}

static MagickRealType VividLight(const MagickRealType Sca,
  const MagickRealType Sa, const MagickRealType Dca, const MagickRealType Da)
{
  /*
    VividLight: A Photoshop 7 composition method.  See
    http://www.simplefilter.de/en/basics/mixmods.html.

    f(Sc,Dc) = (2*Sc < 1) ? 1-(1-Dc)/(2*Sc) : Dc/(2*(1-Sc))
  */
  if ((fabs((double) Sa) < MagickEpsilon) ||
      (fabs((double) (Sca-Sa)) < MagickEpsilon))
    return(Sa*Da+Sca*(1.0-Da)+Dca*(1.0-Sa));
  if ((2*Sca) <= Sa)
    return(Sa*(Da+Sa*(Dca-Da)*MagickSafeReciprocal(2.0*Sca))+Sca*(1.0-Da)+
      Dca*(1.0-Sa));
  return(Dca*Sa*Sa*MagickSafeReciprocal(2.0*(Sa-Sca))+Sca*(1.0-Da)+Dca*
    (1.0-Sa));
}

static inline void CompositeVividLight(const MagickPixelPacket *p,
  const MagickPixelPacket *q,MagickPixelPacket *composite)
{
  MagickRealType
    Da,
    gamma,
    Sa;

  Sa=1.0-QuantumScale*p->opacity;  /* simplify and speed up equations */
  Da=1.0-QuantumScale*q->opacity;
  gamma=RoundToUnity(Sa+Da-Sa*Da); /* over blend, as per SVG doc */
  composite->opacity=(MagickRealType) QuantumRange*(1.0-gamma);
  gamma=(MagickRealType) QuantumRange/(fabs((double) gamma) < MagickEpsilon ?
    MagickEpsilon : gamma);
  composite->red=gamma*VividLight(QuantumScale*p->red*Sa,Sa,QuantumScale*
    q->red*Da,Da);
  composite->green=gamma*VividLight(QuantumScale*p->green*Sa,Sa,QuantumScale*
    q->green*Da,Da);
  composite->blue=gamma*VividLight(QuantumScale*p->blue*Sa,Sa,QuantumScale*
    q->blue*Da,Da);
  if (q->colorspace == CMYKColorspace)
    composite->index=gamma*VividLight(QuantumScale*p->index*Sa,Sa,QuantumScale*
      q->index*Da,Da);
}

static MagickRealType Xor(const MagickRealType Sca,const MagickRealType Sa,
  const MagickRealType Dca,const MagickRealType Da)
{
  return(Sca*(1.0-Da)+Dca*(1.0-Sa));
}

static inline void CompositeXor(const MagickPixelPacket *p,
  const MagickPixelPacket *q,MagickPixelPacket *composite)
{
  MagickRealType
    Da,
    gamma,
    Sa;

  Sa=1.0-QuantumScale*p->opacity;  /* simplify and speed up equations */
  Da=1.0-QuantumScale*q->opacity;
  gamma=Sa+Da-2*Sa*Da;        /* Xor blend mode X=0,Y=1,Z=1 */
  composite->opacity=(MagickRealType) QuantumRange*(1.0-gamma);
  gamma=MagickSafeReciprocal(gamma);
  composite->red=gamma*Xor(p->red*Sa,Sa,q->red*Da,Da);
  composite->green=gamma*Xor(p->green*Sa,Sa,q->green*Da,Da);
  composite->blue=gamma*Xor(p->blue*Sa,Sa,q->blue*Da,Da);
  if (q->colorspace == CMYKColorspace)
    composite->index=gamma*Xor(p->index*Sa,Sa,q->index*Da,Da);
}

MagickExport MagickBooleanType CompositeImage(Image *image,
  const CompositeOperator compose,const Image *source_image,
  const ssize_t x_offset,const ssize_t y_offset)
{
  MagickBooleanType
    status;

  status=CompositeImageChannel(image,DefaultChannels,compose,source_image,
    x_offset,y_offset);
  return(status);
}

MagickExport MagickBooleanType CompositeImageChannel(Image *image,
  const ChannelType channel,const CompositeOperator compose,
  const Image *composite,const ssize_t x_offset,const ssize_t y_offset)
{
#define CompositeImageTag  "Composite/Image"

  CacheView
    *source_view,
    *image_view;

  const char
    *value;

  ExceptionInfo
    *exception;

  GeometryInfo
    geometry_info;

  Image
    *canvas_image,
    *source_image;

  MagickBooleanType
    clamp,
    clip_to_self,
    status;

  MagickOffsetType
    progress;

  MagickPixelPacket
    zero;

  MagickRealType
    amount,
    canvas_dissolve,
    midpoint,
    percent_luma,
    percent_chroma,
    source_dissolve,
    threshold;

  MagickStatusType
    flags;

  ssize_t
    y;

  /*
    Prepare composite image.
  */
  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  assert(composite != (Image *) NULL);
  assert(composite->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"%s",image->filename);
  if (SetImageStorageClass(image,DirectClass) == MagickFalse)
    return(MagickFalse);
  exception=(&image->exception);
  source_image=CloneImage(composite,0,0,MagickTrue,exception);
  if (source_image == (const Image *) NULL)
    return(MagickFalse);
  (void) SetImageColorspace(source_image,image->colorspace);
  GetMagickPixelPacket(image,&zero);
  canvas_image=(Image *) NULL;
  amount=0.5;
  canvas_dissolve=1.0;
  clip_to_self=MagickTrue;
  percent_luma=100.0;
  percent_chroma=100.0;
  source_dissolve=1.0;
  threshold=0.05f;
  switch (compose)
  {
    case ClearCompositeOp:
    case SrcCompositeOp:
    case InCompositeOp:
    case SrcInCompositeOp:
    case OutCompositeOp:
    case SrcOutCompositeOp:
    case DstInCompositeOp:
    case DstAtopCompositeOp:
    {
      /*
        Modify canvas outside the overlaid region.
      */
      clip_to_self=MagickFalse;
      break;
    }
    case OverCompositeOp:
    {
      if (image->matte != MagickFalse)
        break;
      if (source_image->matte != MagickFalse)
        break;
      magick_fallthrough;
    }
    case CopyCompositeOp:
    {
      if ((x_offset < 0) || (y_offset < 0))
        break;
      if ((x_offset+(ssize_t) source_image->columns) >= (ssize_t) image->columns)
        break;
      if ((y_offset+(ssize_t) source_image->rows) >= (ssize_t) image->rows)
        break;
      status=MagickTrue;
      source_view=AcquireVirtualCacheView(source_image,exception);
      image_view=AcquireAuthenticCacheView(image,exception);
#if defined(MAGICKCORE_OPENMP_SUPPORT)
      #pragma omp parallel for schedule(static) shared(status) \
        magick_number_threads(source_image,image,source_image->rows,4)
#endif
      for (y=0; y < (ssize_t) source_image->rows; y++)
      {
        MagickBooleanType
          sync;

        const IndexPacket
          *source_indexes;

        const PixelPacket
          *p;

        IndexPacket
          *indexes;

        PixelPacket
          *q;

        if (status == MagickFalse)
          continue;
        p=GetCacheViewVirtualPixels(source_view,0,y,source_image->columns,
          1,exception);
        q=GetCacheViewAuthenticPixels(image_view,x_offset,y+y_offset,
          source_image->columns,1,exception);
        if ((p == (const PixelPacket *) NULL) || (q == (PixelPacket *) NULL))
          {
            status=MagickFalse;
            continue;
          }
        source_indexes=GetCacheViewVirtualIndexQueue(source_view);
        indexes=GetCacheViewAuthenticIndexQueue(image_view);
        (void) memcpy(q,p,source_image->columns*sizeof(*p));
        if ((indexes != (IndexPacket *) NULL) &&
            (source_indexes != (const IndexPacket *) NULL))
          (void) memcpy(indexes,source_indexes,
            source_image->columns*sizeof(*indexes));
        sync=SyncCacheViewAuthenticPixels(image_view,exception);
        if (sync == MagickFalse)
          status=MagickFalse;
        if (image->progress_monitor != (MagickProgressMonitor) NULL)
          {
            MagickBooleanType
              proceed;

            proceed=SetImageProgress(image,CompositeImageTag,(MagickOffsetType)
              y,image->rows);
            if (proceed == MagickFalse)
              status=MagickFalse;
          }
      }
      source_view=DestroyCacheView(source_view);
      image_view=DestroyCacheView(image_view);
      source_image=DestroyImage(source_image);
      return(status);
    }
    case CopyOpacityCompositeOp:
    case ChangeMaskCompositeOp:
    {
      /*
        Modify canvas outside the overlaid region and require an alpha
        channel to exist, to add transparency.
      */
      if (image->matte == MagickFalse)
        (void) SetImageAlphaChannel(image,OpaqueAlphaChannel);
      clip_to_self=MagickFalse;
      break;
    }
    case BlurCompositeOp:
    {
      CacheView
        *canvas_view,
        *source_view;

      MagickPixelPacket
        pixel;

      MagickRealType
        angle_range,
        angle_start,
        height,
        width;

      ResampleFilter
        *resample_filter;

      SegmentInfo
        blur;

      /*
        Blur Image by resampling.

        Blur Image dictated by an overlay gradient map: X = red_channel;
          Y = green_channel; compose:args =  x_scale[,y_scale[,angle]].
      */
      canvas_image=CloneImage(image,0,0,MagickTrue,exception);
      if (canvas_image == (Image *) NULL)
        {
          source_image=DestroyImage(source_image);
          return(MagickFalse);
        }
      /*
        Gather the maximum blur sigma values from user.
      */
      SetGeometryInfo(&geometry_info);
      flags=NoValue;
      value=GetImageArtifact(image,"compose:args");
      if (value != (char *) NULL)
        flags=ParseGeometry(value,&geometry_info);
      if ((flags & WidthValue) == 0)
        {
          (void) ThrowMagickException(exception,GetMagickModule(),
            OptionWarning,"InvalidGeometry","'%s' '%s'","compose:args",value);
          source_image=DestroyImage(source_image);
          canvas_image=DestroyImage(canvas_image);
          return(MagickFalse);
        }
      /*
        Users input sigma now needs to be converted to the EWA ellipse size.
        The filter defaults to a sigma of 0.5 so to make this match the
        users input the ellipse size needs to be doubled.
      */
      width=height=geometry_info.rho*2.0;
      if ((flags & HeightValue) != 0 )
        height=geometry_info.sigma*2.0;

      /* default the unrotated ellipse width and height axis vectors */
      blur.x1=width;
      blur.x2=0.0;
      blur.y1=0.0;
      blur.y2=height;
      /* rotate vectors if a rotation angle is given */
      if ((flags & XValue) != 0 )
        {
          MagickRealType
            angle;

          angle=DegreesToRadians(geometry_info.xi);
          blur.x1=width*cos(angle);
          blur.x2=width*sin(angle);
          blur.y1=(-height*sin(angle));
          blur.y2=height*cos(angle);
        }
      /* Otherwise lets set a angle range and calculate in the loop */
      angle_start=0.0;
      angle_range=0.0;
      if ((flags & YValue) != 0 )
        {
          angle_start=DegreesToRadians(geometry_info.xi);
          angle_range=DegreesToRadians(geometry_info.psi)-angle_start;
        }
      /*
        Set up a gaussian cylindrical filter for EWA Blurring.

        As the minimum ellipse radius of support*1.0 the EWA algorithm
        can only produce a minimum blur of 0.5 for Gaussian (support=2.0)
        This means that even 'No Blur' will be still a little blurry!

        The solution (as well as the problem of preventing any user
        expert filter settings, is to set our own user settings, then
        restore them afterwards.
      */
      resample_filter=AcquireResampleFilter(image,exception);
      SetResampleFilter(resample_filter,GaussianFilter,1.0);

      /* do the variable blurring of each pixel in image */
      pixel=zero;
      source_view=AcquireVirtualCacheView(source_image,exception);
      canvas_view=AcquireAuthenticCacheView(canvas_image,exception);
      for (y=0; y < (ssize_t) source_image->rows; y++)
      {
        MagickBooleanType
          sync;

        const PixelPacket
          *magick_restrict p;

        PixelPacket
          *magick_restrict r;

        IndexPacket
          *magick_restrict canvas_indexes;

        ssize_t
          x;

        if (((y+y_offset) < 0) || ((y+y_offset) >= (ssize_t) image->rows))
          continue;
        p=GetCacheViewVirtualPixels(source_view,0,y,source_image->columns,
          1,exception);
        r=QueueCacheViewAuthenticPixels(canvas_view,0,y,canvas_image->columns,
          1,exception);
        if ((p == (const PixelPacket *) NULL) || (r == (PixelPacket *) NULL))
          break;
        canvas_indexes=GetCacheViewAuthenticIndexQueue(canvas_view);
        for (x=0; x < (ssize_t) source_image->columns; x++)
        {
          if (((x_offset+x) < 0) || ((x_offset+x) >= (ssize_t) image->columns))
            {
              p++;
              continue;
            }
          if (fabs((double) angle_range) > MagickEpsilon)
            {
              MagickRealType
                angle;

              angle=angle_start+angle_range*QuantumScale*(double)
                GetPixelBlue(p);
              blur.x1=width*cos(angle);
              blur.x2=width*sin(angle);
              blur.y1=(-height*sin(angle));
              blur.y2=height*cos(angle);
            }
#if 0
          if ( x == 10 && y == 60 ) {
            fprintf(stderr, "blur.x=%lf,%lf, blur.y=%lf,%lf\n",
                blur.x1, blur.x2, blur.y1, blur.y2);
            fprintf(stderr, "scaled by=%lf,%lf\n",
                QuantumScale*GetPixelRed(p), QuantumScale*GetPixelGreen(p));
          }
#endif
          ScaleResampleFilter(resample_filter,
            blur.x1*QuantumScale*(double) GetPixelRed(p),
            blur.y1*QuantumScale*(double) GetPixelGreen(p),
            blur.x2*QuantumScale*(double) GetPixelRed(p),
            blur.y2*QuantumScale*(double) GetPixelGreen(p));
          (void) ResamplePixelColor(resample_filter,(double) x_offset+x,(double)
            y_offset+y,&pixel);
          SetPixelPacket(canvas_image,&pixel,r,canvas_indexes+x);
          p++;
          r++;
        }
        sync=SyncCacheViewAuthenticPixels(canvas_view,exception);
        if (sync == MagickFalse)
          break;
      }
      resample_filter=DestroyResampleFilter(resample_filter);
      source_view=DestroyCacheView(source_view);
      canvas_view=DestroyCacheView(canvas_view);
      source_image=DestroyImage(source_image);
      source_image=canvas_image;
      break;
    }
    case DisplaceCompositeOp:
    case DistortCompositeOp:
    {
      CacheView
        *canvas_view,
        *source_view,
        *image_view;

      MagickPixelPacket
        pixel;

      MagickRealType
        horizontal_scale,
        vertical_scale;

      PointInfo
        center,
        offset;

      IndexPacket
        *magick_restrict canvas_indexes;

      PixelPacket
        *magick_restrict r;

      /*
        Displace/Distort based on overlay gradient map:
          X = red_channel;  Y = green_channel;
          compose:args = x_scale[,y_scale[,center.x,center.y]]
      */
      canvas_image=CloneImage(image,0,0,MagickTrue,exception);
      if (canvas_image == (Image *) NULL)
        {
          source_image=DestroyImage(source_image);
          return(MagickFalse);
        }
      SetGeometryInfo(&geometry_info);
      flags=NoValue;
      value=GetImageArtifact(image,"compose:args");
      if (value != (char *) NULL)
        flags=ParseGeometry(value,&geometry_info);
      if ((flags & (WidthValue | HeightValue)) == 0 )
        {
          if ((flags & AspectValue) == 0)
            {
              horizontal_scale=(MagickRealType) (source_image->columns-1)/2.0;
              vertical_scale=(MagickRealType) (source_image->rows-1)/2.0;
            }
          else
            {
              horizontal_scale=(MagickRealType) (image->columns-1)/2.0;
              vertical_scale=(MagickRealType) (image->rows-1)/2.0;
            }
        }
      else
        {
          horizontal_scale=geometry_info.rho;
          vertical_scale=geometry_info.sigma;
          if ((flags & PercentValue) != 0)
            {
              if ((flags & AspectValue) == 0)
                {
                  horizontal_scale*=(source_image->columns-1)/200.0;
                  vertical_scale*=(source_image->rows-1)/200.0;
                }
              else
                {
                  horizontal_scale*=(image->columns-1)/200.0;
                  vertical_scale*=(image->rows-1)/200.0;
                }
            }
          if ((flags & HeightValue) == 0)
            vertical_scale=horizontal_scale;
        }
      /*
        Determine fixed center point for absolute distortion map
         Absolute distort ==
           Displace offset relative to a fixed absolute point
           Select that point according to +X+Y user inputs.
           default = center of overlay image
           arg flag '!' = locations/percentage relative to background image
      */
      center.x=(MagickRealType) x_offset;
      center.y=(MagickRealType) y_offset;
      if (compose == DistortCompositeOp)
        {
          if ((flags & XValue) == 0)
            if ((flags & AspectValue) != 0)
              center.x=((MagickRealType) image->columns-1)/2.0;
            else
              center.x=(MagickRealType) (x_offset+(source_image->columns-1)/
                2.0);
          else
            if ((flags & AspectValue) == 0)
              center.x=(MagickRealType) (x_offset+geometry_info.xi);
            else
              center.x=geometry_info.xi;
          if ((flags & YValue) == 0)
            if ((flags & AspectValue) != 0)
              center.y=((MagickRealType) image->rows-1)/2.0;
            else
              center.y=(MagickRealType) (y_offset+(source_image->rows-1)/2.0);
          else
            if ((flags & AspectValue) != 0)
              center.y=geometry_info.psi;
            else
              center.y=(MagickRealType) (y_offset+geometry_info.psi);
        }
      /*
        Shift the pixel offset point as defined by the provided,
        displacement/distortion map.  -- Like a lens...
      */
      pixel=zero;
      image_view=AcquireVirtualCacheView(image,exception);
      source_view=AcquireVirtualCacheView(source_image,exception);
      canvas_view=AcquireAuthenticCacheView(canvas_image,exception);
      for (y=0; y < (ssize_t) source_image->rows; y++)
      {
        MagickBooleanType
          sync;

        const PixelPacket
          *magick_restrict p;

        ssize_t
          x;

        if (((y+y_offset) < 0) || ((y+y_offset) >= (ssize_t) image->rows))
          continue;
        p=GetCacheViewVirtualPixels(source_view,0,y,source_image->columns,
          1,exception);
        r=QueueCacheViewAuthenticPixels(canvas_view,0,y,canvas_image->columns,
          1,exception);
        if ((p == (const PixelPacket *) NULL) || (r == (PixelPacket *) NULL))
          break;
        canvas_indexes=GetCacheViewAuthenticIndexQueue(canvas_view);
        for (x=0; x < (ssize_t) source_image->columns; x++)
        {
          if (((x_offset+x) < 0) || ((x_offset+x) >= (ssize_t) image->columns))
            {
              p++;
              continue;
            }
          /*
            Displace the offset.
          */
          offset.x=(double) ((horizontal_scale*((MagickRealType) GetPixelRed(p)-
            (((MagickRealType) QuantumRange+1.0)/2.0)))/(((MagickRealType)
            QuantumRange+1.0)/2.0)+center.x+((compose == DisplaceCompositeOp) ?
            x : 0));
          offset.y=(double) ((vertical_scale*((MagickRealType) GetPixelGreen(p)-
            (((MagickRealType) QuantumRange+1.0)/2.0)))/(((MagickRealType)
            QuantumRange+1.0)/2.0)+center.y+((compose == DisplaceCompositeOp) ?
            y : 0));
          status=InterpolateMagickPixelPacket(image,image_view,
            UndefinedInterpolatePixel,(double) offset.x,(double) offset.y,
            &pixel,exception);
          if (status == MagickFalse)
            break;
          /*
            Mask with the 'invalid pixel mask' in alpha channel.
          */
          pixel.opacity=(MagickRealType) QuantumRange*(1.0-(1.0-QuantumScale*
            pixel.opacity)*(1.0-QuantumScale*(double) GetPixelOpacity(p)));
          SetPixelPacket(canvas_image,&pixel,r,canvas_indexes+x);
          p++;
          r++;
        }
        if (x < (ssize_t) source_image->columns)
          break;
        sync=SyncCacheViewAuthenticPixels(canvas_view,exception);
        if (sync == MagickFalse)
          break;
      }
      canvas_view=DestroyCacheView(canvas_view);
      source_view=DestroyCacheView(source_view);
      image_view=DestroyCacheView(image_view);
      source_image=DestroyImage(source_image);
      source_image=canvas_image;
      break;
    }
    case DissolveCompositeOp:
    {
      /*
        Geometry arguments to dissolve factors.
      */
      value=GetImageArtifact(image,"compose:args");
      if (value != (char *) NULL)
        {
          flags=ParseGeometry(value,&geometry_info);
          source_dissolve=geometry_info.rho/100.0;
          canvas_dissolve=1.0;
          if ((source_dissolve-MagickEpsilon) < 0.0)
            source_dissolve=0.0;
          if ((source_dissolve+MagickEpsilon) > 1.0)
            {
              canvas_dissolve=2.0-source_dissolve;
              source_dissolve=1.0;
            }
          if ((flags & SigmaValue) != 0)
            canvas_dissolve=geometry_info.sigma/100.0;
          if ((canvas_dissolve-MagickEpsilon) < 0.0)
            canvas_dissolve=0.0;
          clip_to_self=MagickFalse;
          if ((canvas_dissolve+MagickEpsilon) > 1.0 )
            {
              canvas_dissolve=1.0;
              clip_to_self=MagickTrue;
            }
        }
      break;
    }
    case BlendCompositeOp:
    {
      value=GetImageArtifact(image,"compose:args");
      if (value != (char *) NULL)
        {
          flags=ParseGeometry(value,&geometry_info);
          source_dissolve=geometry_info.rho/100.0;
          canvas_dissolve=1.0-source_dissolve;
          if ((flags & SigmaValue) != 0)
            canvas_dissolve=geometry_info.sigma/100.0;
          clip_to_self=MagickFalse;
          if ((canvas_dissolve+MagickEpsilon) > 1.0)
            clip_to_self=MagickTrue;
        }
      break;
    }
    case MathematicsCompositeOp:
    {
      /*
        Just collect the values from "compose:args", setting.
        Unused values are set to zero automagically.

        Arguments are normally a comma separated list, so this probably should
        be changed to some 'general comma list' parser, (with a minimum
        number of values)
      */
      SetGeometryInfo(&geometry_info);
      value=GetImageArtifact(image,"compose:args");
      if (value != (char *) NULL)
        {
          flags=ParseGeometry(value,&geometry_info);
          if (flags == NoValue)
            (void) ThrowMagickException(exception,GetMagickModule(),OptionError,
              "InvalidGeometry","`%s'",value);
        }
      break;
    }
    case ModulateCompositeOp:
    {
      /*
        Determine the luma and chroma scale.
      */
      value=GetImageArtifact(image,"compose:args");
      if (value != (char *) NULL)
        {
          flags=ParseGeometry(value,&geometry_info);
          percent_luma=geometry_info.rho;
          if ((flags & SigmaValue) != 0)
            percent_chroma=geometry_info.sigma;
        }
      break;
    }
    case ThresholdCompositeOp:
    {
      /*
        Determine the amount and threshold.
        This Composition method is deprecated
      */
      value=GetImageArtifact(image,"compose:args");
      if (value != (char *) NULL)
        {
          flags=ParseGeometry(value,&geometry_info);
          amount=geometry_info.rho;
          threshold=geometry_info.sigma;
          if ((flags & SigmaValue) == 0)
            threshold=0.05f;
        }
      threshold*=(double) QuantumRange;
      break;
    }
    default:
      break;
  }
  value=GetImageArtifact(image,"compose:outside-overlay");
  if (value != (const char *) NULL)
    clip_to_self=IsMagickTrue(value) == MagickFalse ? MagickTrue : MagickFalse;
  value=GetImageArtifact(image,"compose:clip-to-self");
  if (value != (const char *) NULL)
    clip_to_self=IsMagickTrue(value) != MagickFalse ? MagickTrue : MagickFalse;
  clamp=MagickTrue;
  value=GetImageArtifact(image,"compose:clamp");
  if (value != (const char *) NULL)
    clamp=IsMagickTrue(value);
  /*
    Composite image.
  */
#if defined(MAGICKCORE_OPENCL_SUPPORT)
  status=AccelerateCompositeImage(image,channel,compose,source_image,
    x_offset,y_offset,canvas_dissolve,source_dissolve,exception);
  if (status != MagickFalse)
    return(status);
#endif
  status=MagickTrue;
  progress=0;
  midpoint=((MagickRealType) QuantumRange+1.0)/2;
  GetMagickPixelPacket(source_image,&zero);
  source_view=AcquireVirtualCacheView(source_image,exception);
  image_view=AcquireAuthenticCacheView(image,exception);
#if defined(MAGICKCORE_OPENMP_SUPPORT)
  #pragma omp parallel for schedule(static) shared(progress,status) \
    magick_number_threads(source_image,image,image->rows,1)
#endif
  for (y=0; y < (ssize_t) image->rows; y++)
  {
    const PixelPacket
      *pixels;

    double
      luma,
      hue,
      chroma,
      sans;

    MagickPixelPacket
      composite,
      canvas,
      source;

    const IndexPacket
      *magick_restrict source_indexes;

    const PixelPacket
      *magick_restrict p;

    IndexPacket
      *magick_restrict indexes;

    ssize_t
      x;

    PixelPacket
      *magick_restrict q;

    if (status == MagickFalse)
      continue;
    if (clip_to_self != MagickFalse)
      {
        if (y < y_offset)
          continue;
        if ((y-(double) y_offset) >= (double) source_image->rows)
          continue;
      }
    /*
      If pixels is NULL, y is outside overlay region.
    */
    pixels=(PixelPacket *) NULL;
    p=(PixelPacket *) NULL;
    if ((y >= y_offset) &&
        ((y-(double) y_offset) < (double) source_image->rows))
      {
        p=GetCacheViewVirtualPixels(source_view,0,
          CastDoubleToLong(y-(double) y_offset),source_image->columns,1,
          exception);
        if (p == (const PixelPacket *) NULL)
          {
            status=MagickFalse;
            continue;
          }
        pixels=p;
        if (x_offset < 0)
          p-=(ptrdiff_t)x_offset;
      }
    q=GetCacheViewAuthenticPixels(image_view,0,y,image->columns,1,exception);
    if (q == (PixelPacket *) NULL)
      {
        status=MagickFalse;
        continue;
      }
    indexes=GetCacheViewAuthenticIndexQueue(image_view);
    source_indexes=GetCacheViewVirtualIndexQueue(source_view);
    GetMagickPixelPacket(source_image,&source);
    GetMagickPixelPacket(image,&canvas);
    hue=0.0;
    chroma=0.0;
    luma=0.0;
    for (x=0; x < (ssize_t) image->columns; x++)
    {
      if (clip_to_self != MagickFalse)
        {
          if (x < x_offset)
            {
              q++;
              continue;
            }
          if ((x-(double) x_offset) >= (double) source_image->columns)
            break;
        }
      canvas.red=(MagickRealType) GetPixelRed(q);
      canvas.green=(MagickRealType) GetPixelGreen(q);
      canvas.blue=(MagickRealType) GetPixelBlue(q);
      if (image->matte != MagickFalse)
        canvas.opacity=(MagickRealType) GetPixelOpacity(q);
      if (image->colorspace == CMYKColorspace)
        canvas.index=(MagickRealType) GetPixelIndex(indexes+x);
      if (image->colorspace == CMYKColorspace)
        {
          canvas.red=(MagickRealType) QuantumRange-canvas.red;
          canvas.green=(MagickRealType) QuantumRange-canvas.green;
          canvas.blue=(MagickRealType) QuantumRange-canvas.blue;
          canvas.index=(MagickRealType) QuantumRange-canvas.index;
        }
      /*
        Handle canvas modifications outside overlaid region.
      */
      composite=canvas;
      if ((pixels == (PixelPacket *) NULL) || (x < x_offset) ||
          ((x-(double) x_offset) >= (double) source_image->columns))
        {
          switch (compose)
          {
            case DissolveCompositeOp:
            case BlendCompositeOp:
            {
              composite.opacity=(MagickRealType) ((MagickRealType) QuantumRange-
                canvas_dissolve*((MagickRealType) QuantumRange-
                composite.opacity));
              break;
            }
            case ClearCompositeOp:
            case SrcCompositeOp:
            {
              CompositeClear(&canvas,&composite);
              break;
            }
            case InCompositeOp:
            case SrcInCompositeOp:
            case OutCompositeOp:
            case SrcOutCompositeOp:
            case DstInCompositeOp:
            case DstAtopCompositeOp:
            case CopyOpacityCompositeOp:
            case ChangeMaskCompositeOp:
            {
              composite.opacity=(MagickRealType) TransparentOpacity;
              break;
            }
            default:
            {
              (void) GetOneVirtualMagickPixel(source_image,
                CastDoubleToLong(x-(double) x_offset),
                CastDoubleToLong(y-(double) y_offset),&composite,exception);
              break;
            }
          }
          if (image->colorspace == CMYKColorspace)
            {
              composite.red=(MagickRealType) QuantumRange-composite.red;
              composite.green=(MagickRealType) QuantumRange-composite.green;
              composite.blue=(MagickRealType) QuantumRange-composite.blue;
              composite.index=(MagickRealType) QuantumRange-composite.index;
            }
          SetPixelRed(q,clamp != MagickFalse ?
            ClampPixel(composite.red) : ClampToQuantum(composite.red));
          SetPixelGreen(q,clamp != MagickFalse ?
            ClampPixel(composite.green) : ClampToQuantum(composite.green));
          SetPixelBlue(q,clamp != MagickFalse ?
            ClampPixel(composite.blue) : ClampToQuantum(composite.blue));
          if (image->matte != MagickFalse)
            SetPixelOpacity(q,clamp != MagickFalse ?
              ClampPixel(composite.opacity) :
              ClampToQuantum(composite.opacity));
          if (image->colorspace == CMYKColorspace)
            SetPixelIndex(indexes+x,clamp != MagickFalse ?
              ClampPixel(composite.index) : ClampToQuantum(composite.index));
          q++;
          continue;
        }
      /*
        Handle normal overlay of source onto canvas.
      */
      source.red=(MagickRealType) GetPixelRed(p);
      source.green=(MagickRealType) GetPixelGreen(p);
      source.blue=(MagickRealType) GetPixelBlue(p);
      if (source_image->matte != MagickFalse)
        source.opacity=(MagickRealType) GetPixelOpacity(p);
      if (source_image->colorspace == CMYKColorspace)
        source.index=(MagickRealType) GetPixelIndex(source_indexes+
          CastDoubleToLong(x-(double) x_offset));
      if (source_image->colorspace == CMYKColorspace)
        {
          source.red=(MagickRealType) QuantumRange-source.red;
          source.green=(MagickRealType) QuantumRange-source.green;
          source.blue=(MagickRealType) QuantumRange-source.blue;
          source.index=(MagickRealType) QuantumRange-source.index;
        }
      switch (compose)
      {
        /* Duff-Porter Compositions */
        case ClearCompositeOp:
        {
          CompositeClear(&canvas,&composite);
          break;
        }
        case SrcCompositeOp:
        case CopyCompositeOp:
        case ReplaceCompositeOp:
        {
          composite=source;
          break;
        }
        case NoCompositeOp:
        case DstCompositeOp:
          break;
        case OverCompositeOp:
        case SrcOverCompositeOp:
        {
          MagickPixelCompositeOver(&source,source.opacity,&canvas,
            canvas.opacity,&composite);
          break;
        }
        case DstOverCompositeOp:
        {
          MagickPixelCompositeOver(&canvas,canvas.opacity,&source,
            source.opacity,&composite);
          break;
        }
        case SrcInCompositeOp:
        case InCompositeOp:
        {
          CompositeIn(&source,&canvas,&composite);
          break;
        }
        case DstInCompositeOp:
        {
          CompositeIn(&canvas,&source,&composite);
          break;
        }
        case OutCompositeOp:
        case SrcOutCompositeOp:
        {
          CompositeOut(&source,&canvas,&composite);
          break;
        }
        case DstOutCompositeOp:
        {
          CompositeOut(&canvas,&source,&composite);
          break;
        }
        case AtopCompositeOp:
        case SrcAtopCompositeOp:
        {
          CompositeAtop(&source,&canvas,&composite);
          break;
        }
        case DstAtopCompositeOp:
        {
          CompositeAtop(&canvas,&source,&composite);
          break;
        }
        case XorCompositeOp:
        {
          CompositeXor(&source,&canvas,&composite);
          break;
        }
        /* Mathematical Compositions */
        case PlusCompositeOp:
        {
          CompositePlus(&source,&canvas,channel,&composite);
          break;
        }
        case MinusDstCompositeOp:
        {
          CompositeMinus(&source,&canvas,channel,&composite);
          break;
        }
        case MinusSrcCompositeOp:
        {
          CompositeMinus(&canvas,&source,channel,&composite);
          break;
        }
        case ModulusAddCompositeOp:
        {
          CompositeModulusAdd(&source,&canvas,channel,&composite);
          break;
        }
        case ModulusSubtractCompositeOp:
        {
          CompositeModulusSubtract(&source,&canvas,channel,&composite);
          break;
        }
        case DifferenceCompositeOp:
        {
          CompositeDifference(&source,&canvas,channel,&composite);
          break;
        }
        case ExclusionCompositeOp:
        {
          CompositeExclusion(&source,&canvas,channel,&composite);
          break;
        }
        case MultiplyCompositeOp:
        {
          CompositeMultiply(&source,&canvas,channel,&composite);
          break;
        }
        case ScreenCompositeOp:
        {
          CompositeScreen(&source,&canvas,channel,&composite);
          break;
        }
        case DivideDstCompositeOp:
        {
          CompositeDivide(&source,&canvas,channel,&composite);
          break;
        }
        case DivideSrcCompositeOp:
        {
          CompositeDivide(&canvas,&source,channel,&composite);
          break;
        }
        case DarkenCompositeOp:
        {
          CompositeDarken(&source,&canvas,channel,&composite);
          break;
        }
        case LightenCompositeOp:
        {
          CompositeLighten(&source,&canvas,channel,&composite);
          break;
        }
        case DarkenIntensityCompositeOp:
        {
          CompositeDarkenIntensity(&source,&canvas,channel,&composite);
          break;
        }
        case LightenIntensityCompositeOp:
        {
          CompositeLightenIntensity(&source,&canvas,channel,&composite);
          break;
        }
        case MathematicsCompositeOp:
        {
          CompositeMathematics(&source,&canvas,channel,&geometry_info,
            &composite);
          break;
        }
        /* Lighting Compositions */
        case ColorDodgeCompositeOp:
        {
          CompositeColorDodge(&source,&canvas,&composite);
          break;
        }
        case ColorBurnCompositeOp:
        {
          CompositeColorBurn(&source,&canvas,&composite);
          break;
        }
        case LinearDodgeCompositeOp:
        {
          CompositeLinearDodge(&source,&canvas,&composite);
          break;
        }
        case LinearBurnCompositeOp:
        {
          CompositeLinearBurn(&source,&canvas,&composite);
          break;
        }
        case HardLightCompositeOp:
        {
          CompositeHardLight(&source,&canvas,&composite);
          break;
        }
        case HardMixCompositeOp:
        {
          CompositeHardMix(&source,&canvas,&composite);
          break;
        }
        case OverlayCompositeOp:
        {
          /* Overlay = Reversed HardLight. */
          CompositeHardLight(&canvas,&source,&composite);
          break;
        }
        case SoftLightCompositeOp:
        {
          CompositeSoftLight(&source,&canvas,&composite);
          break;
        }
        case LinearLightCompositeOp:
        {
          CompositeLinearLight(&source,&canvas,&composite);
          break;
        }
        case PegtopLightCompositeOp:
        {
          CompositePegtopLight(&source,&canvas,&composite);
          break;
        }
        case VividLightCompositeOp:
        {
          CompositeVividLight(&source,&canvas,&composite);
          break;
        }
        case PinLightCompositeOp:
        {
          CompositePinLight(&source,&canvas,&composite);
          break;
        }
        /* Other Composition */
        case ChangeMaskCompositeOp:
        {
          if ((composite.opacity > ((MagickRealType) QuantumRange/2.0)) ||
              (IsMagickColorSimilar(&source,&canvas) != MagickFalse))
            composite.opacity=(MagickRealType) TransparentOpacity;
          else
            composite.opacity=(MagickRealType) OpaqueOpacity;
          break;
        }
        case BumpmapCompositeOp:
        {
          if (source.opacity == (MagickRealType) TransparentOpacity)
            break;
          CompositeBumpmap(&source,&canvas,&composite);
          break;
        }
        case DissolveCompositeOp:
        {
          MagickPixelCompositeOver(&source,(MagickRealType) QuantumRange-
            source_dissolve*((MagickRealType) QuantumRange-source.opacity),
            &canvas,(MagickRealType) QuantumRange-canvas_dissolve*
            ((MagickRealType) QuantumRange-canvas.opacity),&composite);
          break;
        }
        case BlendCompositeOp:
        {
          MagickPixelCompositeBlend(&source,source_dissolve,&canvas,
            canvas_dissolve,&composite);
          break;
        }
        case StereoCompositeOp:
        {
          composite.red=(MagickRealType) GetPixelRed(p);
          composite.opacity=(composite.opacity+canvas.opacity/2);
          break;
        }
        case ThresholdCompositeOp:
        {
          CompositeThreshold(&source,&canvas,threshold,amount,&composite);
          break;
        }
        case ModulateCompositeOp:
        {
          ssize_t
            offset;

          if (source.opacity == (MagickRealType) TransparentOpacity)
            break;
          offset=(ssize_t) ((MagickRealType) MagickPixelIntensityToQuantum(
            &source)-(MagickRealType) midpoint);
          if (offset == 0)
            break;
          CompositeHCL(canvas.red,canvas.green,canvas.blue,&hue,
            &chroma,&luma);
          luma+=(0.01*percent_luma*offset)/midpoint;
          chroma*=0.01*percent_chroma;
          HCLComposite(hue,chroma,luma,&composite.red,&composite.green,
            &composite.blue);
          break;
        }
        case HueCompositeOp:
        {
          if (source.opacity == (MagickRealType) TransparentOpacity)
            break;
          if (canvas.opacity == (MagickRealType) TransparentOpacity)
            {
              composite=source;
              break;
            }
          CompositeHCL(canvas.red,canvas.green,canvas.blue,&hue,
            &chroma,&luma);
          CompositeHCL(source.red,source.green,source.blue,&hue,&sans,&sans);
          HCLComposite(hue,chroma,luma,&composite.red,
            &composite.green,&composite.blue);
          if (source.opacity < canvas.opacity)
            composite.opacity=source.opacity;
          break;
        }
        case SaturateCompositeOp:
        {
          if (source.opacity == (MagickRealType) TransparentOpacity)
            break;
          if (canvas.opacity == (MagickRealType) TransparentOpacity)
            {
              composite=source;
              break;
            }
          CompositeHCL(canvas.red,canvas.green,canvas.blue,&hue,
            &chroma,&luma);
          CompositeHCL(source.red,source.green,source.blue,&sans,&chroma,
            &sans);
          HCLComposite(hue,chroma,luma,&composite.red,
            &composite.green,&composite.blue);
          if (source.opacity < canvas.opacity)
            composite.opacity=source.opacity;
          break;
        }
        case LuminizeCompositeOp:
        {
          if (source.opacity == (MagickRealType) TransparentOpacity)
            break;
          if (canvas.opacity == (MagickRealType) TransparentOpacity)
            {
              composite=source;
              break;
            }
          CompositeHCL(canvas.red,canvas.green,canvas.blue,&hue,
            &chroma,&luma);
          CompositeHCL(source.red,source.green,source.blue,&sans,&sans,
            &luma);
          HCLComposite(hue,chroma,luma,&composite.red,
            &composite.green,&composite.blue);
          if (source.opacity < canvas.opacity)
            composite.opacity=source.opacity;
          break;
        }
        case ColorizeCompositeOp:
        {
          if (source.opacity == (MagickRealType) TransparentOpacity)
            break;
          if (canvas.opacity == (MagickRealType) TransparentOpacity)
            {
              composite=source;
              break;
            }
          CompositeHCL(canvas.red,canvas.green,canvas.blue,&sans,
            &sans,&luma);
          CompositeHCL(source.red,source.green,source.blue,&hue,&chroma,&sans);
          HCLComposite(hue,chroma,luma,&composite.red,
            &composite.green,&composite.blue);
          if (source.opacity < canvas.opacity)
            composite.opacity=source.opacity;
          break;
        }
        case CopyRedCompositeOp:
        case CopyCyanCompositeOp:
        {
          composite.red=source.red;
          break;
        }
        case CopyGreenCompositeOp:
        case CopyMagentaCompositeOp:
        {
          composite.green=source.green;
          break;
        }
        case CopyBlueCompositeOp:
        case CopyYellowCompositeOp:
        {
          composite.blue=source.blue;
          break;
        }
        case CopyOpacityCompositeOp:
        {
          if (source.matte == MagickFalse)
            composite.opacity=(MagickRealType) (QuantumRange-
              MagickPixelIntensityToQuantum(&source));
          else
            composite.opacity=source.opacity;
          break;
        }
        case CopyBlackCompositeOp:
        {
          if (source.colorspace != CMYKColorspace)
            ConvertRGBToCMYK(&source);
          composite.index=source.index;
          break;
        }
        /* compose methods that are already handled */
        case BlurCompositeOp:
        case DisplaceCompositeOp:
        case DistortCompositeOp:
        {
          composite=source;
          break;
        }
        default:
          break;
      }
      if (image->colorspace == CMYKColorspace)
        {
          composite.red=(MagickRealType) QuantumRange-composite.red;
          composite.green=(MagickRealType) QuantumRange-composite.green;
          composite.blue=(MagickRealType) QuantumRange-composite.blue;
          composite.index=(MagickRealType) QuantumRange-composite.index;
        }
      SetPixelRed(q,clamp != MagickFalse ?
        ClampPixel(composite.red) : ClampToQuantum(composite.red));
      SetPixelGreen(q,clamp != MagickFalse ?
        ClampPixel(composite.green) : ClampToQuantum(composite.green));
      SetPixelBlue(q,clamp != MagickFalse ?
        ClampPixel(composite.blue) : ClampToQuantum(composite.blue));
      SetPixelOpacity(q,clamp != MagickFalse ?
        ClampPixel(composite.opacity) : ClampToQuantum(composite.opacity));
      if (image->colorspace == CMYKColorspace)
        SetPixelIndex(indexes+x,clamp != MagickFalse ?
          ClampPixel(composite.index) : ClampToQuantum(composite.index));
      p++;
      if (p >= (pixels+source_image->columns))
        p=pixels;
      q++;
    }
    if (SyncCacheViewAuthenticPixels(image_view,exception) == MagickFalse)
      status=MagickFalse;
    if (image->progress_monitor != (MagickProgressMonitor) NULL)
      {
        MagickBooleanType
          proceed;

#if defined(MAGICKCORE_OPENMP_SUPPORT)
        #pragma omp atomic
#endif
        progress++;
        proceed=SetImageProgress(image,CompositeImageTag,progress,image->rows);
        if (proceed == MagickFalse)
          status=MagickFalse;
      }
  }
  source_view=DestroyCacheView(source_view);
  image_view=DestroyCacheView(image_view);
  if (canvas_image != (Image * ) NULL)
    canvas_image=DestroyImage(canvas_image);
  else
    source_image=DestroyImage(source_image);
  return(status);
}

/*
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%                                                                             %
%                                                                             %
%                                                                             %
%     T e x t u r e I m a g e                                                 %
%                                                                             %
%                                                                             %
%                                                                             %
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
%
%  TextureImage() repeatedly tiles the texture image across and down the image
%  canvas.
%
%  The format of the TextureImage method is:
%
%      MagickBooleanType TextureImage(Image *image,const Image *texture)
%
%  A description of each parameter follows:
%
%    o image: the image.
%
%    o texture: This image is the texture to layer on the background.
%
*/
MagickExport MagickBooleanType TextureImage(Image *image,const Image *texture)
{
#define TextureImageTag  "Texture/Image"

  CacheView
    *image_view,
    *texture_view;

  ExceptionInfo
    *exception;

  Image
    *texture_image;

  MagickBooleanType
    status;

  ssize_t
    y;

  assert(image != (Image *) NULL);
  assert(image->signature == MagickCoreSignature);
  if (IsEventLogging() != MagickFalse)
    (void) LogMagickEvent(TraceEvent,GetMagickModule(),"...");
  if (texture == (const Image *) NULL)
    return(MagickFalse);
  if (SetImageStorageClass(image,DirectClass) == MagickFalse)
    return(MagickFalse);
  exception=(&image->exception);
  texture_image=CloneImage(texture,0,0,MagickTrue,exception);
  if (texture_image == (const Image *) NULL)
    return(MagickFalse);
  (void) TransformImageColorspace(texture_image,image->colorspace);
  (void) SetImageVirtualPixelMethod(texture_image,TileVirtualPixelMethod);
  status=MagickTrue;
  if ((image->compose != CopyCompositeOp) &&
      ((image->compose != OverCompositeOp) || (image->matte != MagickFalse) ||
       (texture_image->matte != MagickFalse)))
    {
      /*
        Tile texture onto the image background.
      */
      for (y=0; y < (ssize_t) image->rows; y+=(ssize_t) texture_image->rows)
      {
        ssize_t
          x;

        if (status == MagickFalse)
          continue;
        for (x=0; x < (ssize_t) image->columns; x+=(ssize_t) texture_image->columns)
        {
          MagickBooleanType
            thread_status;

          thread_status=CompositeImage(image,image->compose,texture_image,x+
            texture_image->tile_offset.x,y+texture_image->tile_offset.y);
          if (thread_status == MagickFalse)
            {
              status=thread_status;
              break;
            }
        }
        if (image->progress_monitor != (MagickProgressMonitor) NULL)
          {
            MagickBooleanType
              proceed;

            proceed=SetImageProgress(image,TextureImageTag,(MagickOffsetType)
              y,image->rows);
            if (proceed == MagickFalse)
              status=MagickFalse;
          }
      }
      (void) SetImageProgress(image,TextureImageTag,(MagickOffsetType)
        image->rows,image->rows);
      texture_image=DestroyImage(texture_image);
      return(status);
    }
  /*
    Tile texture onto the image background (optimized).
  */
  status=MagickTrue;
  texture_view=AcquireVirtualCacheView(texture_image,exception);
  image_view=AcquireAuthenticCacheView(image,exception);
#if defined(MAGICKCORE_OPENMP_SUPPORT)
  #pragma omp parallel for schedule(static) shared(status) \
    magick_number_threads(image,texture_image,image->rows,2)
#endif
  for (y=0; y < (ssize_t) image->rows; y++)
  {
    MagickBooleanType
      sync;

    const IndexPacket
      *texture_indexes;

    const PixelPacket
      *p;

    IndexPacket
      *indexes;

    ssize_t
      x;

    PixelPacket
      *q;

    size_t
      width;

    if (status == MagickFalse)
      continue;
    p=GetCacheViewVirtualPixels(texture_view,texture_image->tile_offset.x,(y+
      texture_image->tile_offset.y) % texture_image->rows,
      texture_image->columns,1,exception);
    q=QueueCacheViewAuthenticPixels(image_view,0,y,image->columns,1,
      exception);
    if ((p == (const PixelPacket *) NULL) || (q == (PixelPacket *) NULL))
      {
        status=MagickFalse;
        continue;
      }
    texture_indexes=GetCacheViewVirtualIndexQueue(texture_view);
    indexes=GetCacheViewAuthenticIndexQueue(image_view);
    for (x=0; x < (ssize_t) image->columns; x+=(ssize_t) texture_image->columns)
    {
      width=texture_image->columns;
      if ((x+(ssize_t) width) > (ssize_t) image->columns)
        width=image->columns-x;
      (void) memcpy(q,p,width*sizeof(*p));
      if ((image->colorspace == CMYKColorspace) &&
          (texture_image->colorspace == CMYKColorspace))
        {
          (void) memcpy(indexes,texture_indexes,width*
            sizeof(*indexes));
          indexes+=width;
        }
      q+=(ptrdiff_t) width;
    }
    sync=SyncCacheViewAuthenticPixels(image_view,exception);
    if (sync == MagickFalse)
      status=MagickFalse;
    if (image->progress_monitor != (MagickProgressMonitor) NULL)
      {
        MagickBooleanType
          proceed;

        proceed=SetImageProgress(image,TextureImageTag,(MagickOffsetType) y,
          image->rows);
        if (proceed == MagickFalse)
          status=MagickFalse;
      }
  }
  texture_view=DestroyCacheView(texture_view);
  image_view=DestroyCacheView(image_view);
  texture_image=DestroyImage(texture_image);
  return(status);
}
