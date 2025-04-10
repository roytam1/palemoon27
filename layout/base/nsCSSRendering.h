/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* utility functions for drawing borders and backgrounds */

#ifndef nsCSSRendering_h___
#define nsCSSRendering_h___

#include "gfxBlur.h"
#include "gfxContext.h"
#include "imgIContainer.h"
#include "mozilla/gfx/PathHelpers.h"
#include "mozilla/gfx/Rect.h"
#include "mozilla/TypedEnumBits.h"
#include "nsLayoutUtils.h"
#include "nsStyleStruct.h"
#include "nsIFrame.h"

class gfxDrawable;
class nsStyleContext;
class nsPresContext;
class nsRenderingContext;

namespace mozilla {

namespace gfx {
struct Color;
class DrawTarget;
} // namespace gfx

namespace layers {
class ImageContainer;
} // namespace layers

// A CSSSizeOrRatio represents a (possibly partially specified) size for use
// in computing image sizes. Either or both of the width and height might be
// given. A ratio of width to height may also be given. If we at least two
// of these then we can compute a concrete size, that is a width and height.
struct CSSSizeOrRatio
{
  CSSSizeOrRatio()
    : mRatio(0, 0)
    , mHasWidth(false)
    , mHasHeight(false) {}

  bool CanComputeConcreteSize() const
  {
    return mHasWidth + mHasHeight + HasRatio() >= 2;
  }
  bool IsConcrete() const { return mHasWidth && mHasHeight; }
  bool HasRatio() const { return mRatio.width > 0 && mRatio.height > 0; }
  bool IsEmpty() const
  {
    return (mHasWidth && mWidth <= 0) ||
           (mHasHeight && mHeight <= 0) ||
           mRatio.width <= 0 || mRatio.height <= 0; 
  }

  // CanComputeConcreteSize must return true when ComputeConcreteSize is
  // called.
  nsSize ComputeConcreteSize() const;

  void SetWidth(nscoord aWidth)
  {
    mWidth = aWidth;
    mHasWidth = true;
    if (mHasHeight) {
      mRatio = nsSize(mWidth, mHeight);
    }
  }
  void SetHeight(nscoord aHeight)
  {
    mHeight = aHeight;
    mHasHeight = true;
    if (mHasWidth) {
      mRatio = nsSize(mWidth, mHeight);
    }
  }
  void SetSize(const nsSize& aSize)
  {
    mWidth = aSize.width;
    mHeight = aSize.height;
    mHasWidth = true;
    mHasHeight = true;
    mRatio = aSize;    
  }
  void SetRatio(const nsSize& aRatio)
  {
    MOZ_ASSERT(!mHasWidth || !mHasHeight,
               "Probably shouldn't be setting a ratio if we have a concrete size");
    mRatio = aRatio;
  }

  nsSize mRatio;
  nscoord mWidth;
  nscoord mHeight;
  bool mHasWidth;
  bool mHasHeight;
};

enum class PaintBorderFlags : uint8_t
{
  SYNC_DECODE_IMAGES = 1 << 0
};
MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(PaintBorderFlags)

} // namespace mozilla

/**
 * This is a small wrapper class to encapsulate image drawing that can draw an
 * nsStyleImage image, which may internally be a real image, a sub image, or a
 * CSS gradient.
 *
 * @note Always call the member functions in the order of PrepareImage(),
 * SetSize(), and Draw*().
 */
class nsImageRenderer {
public:
  typedef mozilla::image::DrawResult DrawResult;
  typedef mozilla::layers::LayerManager LayerManager;
  typedef mozilla::layers::ImageContainer ImageContainer;

  enum {
    FLAG_SYNC_DECODE_IMAGES = 0x01,
    FLAG_PAINTING_TO_WINDOW = 0x02
  };
  enum FitType
  {
    CONTAIN,
    COVER
  };

  nsImageRenderer(nsIFrame* aForFrame, const nsStyleImage* aImage, uint32_t aFlags);
  ~nsImageRenderer();
  /**
   * Populates member variables to get ready for rendering.
   * @return true iff the image is ready, and there is at least a pixel to
   * draw.
   */
  bool PrepareImage();

  /**
   * The three Compute*Size functions correspond to the sizing algorthms and
   * definitions from the CSS Image Values and Replaced Content spec. See
   * http://dev.w3.org/csswg/css-images-3/#sizing .
   */
   
  /**
   * Compute the intrinsic size of the image as defined in the CSS Image Values
   * spec. The intrinsic size is the unscaled size which the image would ideally
   * like to be in app units.
   */
  mozilla::CSSSizeOrRatio ComputeIntrinsicSize();

  /**
   * Computes the placement for a background image, or for the image data
   * inside of a replaced element.
   *
   * @param aPos The CSS <position> value that specifies the image's position.
   * @param aOriginBounds The box to which the tiling position should be
   *          relative. For background images, this should correspond to
   *          'background-origin' for the frame, except when painting on the
   *          canvas, in which case the origin bounds should be the bounds
   *          of the root element's frame. For a replaced element, this should
   *          be the element's content-box.
   * @param aTopLeft [out] The top-left corner where an image tile should be
   *          drawn.
   * @param aAnchorPoint [out] A point which should be pixel-aligned by
   *          nsLayoutUtils::DrawImage. This is the same as aTopLeft, unless
   *          CSS specifies a percentage (including 'right' or 'bottom'), in
   *          which case it's that percentage within of aOriginBounds. So
   *          'right' would set aAnchorPoint.x to aOriginBounds.XMost().
   *
   * Points are returned relative to aOriginBounds.
   */
  static void ComputeObjectAnchorPoint(const nsStyleImageLayers::Position& aPos,
                                       const nsSize& aOriginBounds,
                                       const nsSize& aImageSize,
                                       nsPoint* aTopLeft,
                                       nsPoint* aAnchorPoint);

  /**
   * Compute the size of the rendered image using either the 'cover' or
   * 'contain' constraints (aFitType).
   * aIntrinsicRatio may be an invalid ratio, that is one or both of its
   * dimensions can be less than or equal to zero.
   */
  static nsSize ComputeConstrainedSize(const nsSize& aConstrainingSize,
                                       const nsSize& aIntrinsicRatio,
                                       FitType aFitType);
  /**
   * Compute the size of the rendered image (the concrete size) where no cover/
   * contain constraints are given. The 'default algorithm' from the CSS Image
   * Values spec.
   */
  static nsSize ComputeConcreteSize(const mozilla::CSSSizeOrRatio& aSpecifiedSize,
                                    const mozilla::CSSSizeOrRatio& aIntrinsicSize,
                                    const nsSize& aDefaultSize);

  /**
   * Set this image's preferred size. This will be its intrinsic size where
   * specified and the default size where it is not. Used as the unscaled size
   * when rendering the image.
   */
  void SetPreferredSize(const mozilla::CSSSizeOrRatio& aIntrinsicSize,
                        const nsSize& aDefaultSize);

  /**
   * Draws the image to the target rendering context using background-specific
   * arguments.
   * @see nsLayoutUtils::DrawImage() for parameters.
   */
  DrawResult DrawBackground(nsPresContext*       aPresContext,
                            nsRenderingContext&  aRenderingContext,
                            const nsRect&        aDest,
                            const nsRect&        aFill,
                            const nsPoint&       aAnchor,
                            const nsRect&        aDirty,
                            const nsSize&        aRepeatSize);

  /**
   * Draw the image to a single component of a border-image style rendering.
   * aFill The destination rect to be drawn into
   * aSrc is the part of the image to be rendered into a tile (aUnitSize in
   * aFill), if aSrc and the dest tile are different sizes, the image will be
   * scaled to map aSrc onto the dest tile.
   * aHFill and aVFill are the repeat patterns for the component -
   * NS_STYLE_BORDER_IMAGE_REPEAT_*
   * aUnitSize The scaled size of a single source rect (in destination coords)
   * aIndex identifies the component: 0 1 2
   *                                  3 4 5
   *                                  6 7 8
   * aSVGViewportSize The image size evaluated by default sizing algorithm.
   * Pass Nothing() if we can read a valid viewport size or aspect-ratio from
   * the drawing image directly, otherwise, pass Some() with viewport size
   * evaluated from default sizing algorithm.
   */
  DrawResult
  DrawBorderImageComponent(nsPresContext*       aPresContext,
                           nsRenderingContext&  aRenderingContext,
                           const nsRect&        aDirtyRect,
                           const nsRect&        aFill,
                           const mozilla::CSSIntRect& aSrc,
                           uint8_t              aHFill,
                           uint8_t              aVFill,
                           const nsSize&        aUnitSize,
                           uint8_t              aIndex,
                           const mozilla::Maybe<nsSize>& aSVGViewportSize);

  bool IsRasterImage();
  bool IsAnimatedImage();

  /// Retrieves the image associated with this nsImageRenderer, if there is one.
  already_AddRefed<imgIContainer> GetImage();

  bool IsReady() const { return mPrepareResult == DrawResult::SUCCESS; }
  DrawResult PrepareResult() const { return mPrepareResult; }
  void SetExtendMode(mozilla::gfx::ExtendMode aMode) { mExtendMode = aMode; }
  void SetMaskOp(uint8_t aMaskOp) { mMaskOp = aMaskOp; }

private:
  /**
   * Draws the image to the target rendering context.
   * aSrc is a rect on the source image which will be mapped to aDest; it's
   * currently only used for gradients.
   *
   * @see nsLayoutUtils::DrawImage() for other parameters.
   */
  DrawResult Draw(nsPresContext*       aPresContext,
                  nsRenderingContext&  aRenderingContext,
                  const nsRect&        aDirtyRect,
                  const nsRect&        aDest,
                  const nsRect&        aFill,
                  const nsPoint&       aAnchor,
                  const nsSize&        aRepeatSize,
                  const mozilla::CSSIntRect& aSrc);

  /**
   * Helper method for creating a gfxDrawable from mPaintServerFrame or 
   * mImageElementSurface.
   * Requires mType is eStyleImageType_Element.
   * Returns null if we cannot create the drawable.
   */
  already_AddRefed<gfxDrawable> DrawableForElement(const nsRect& aImageRect,
                                                   nsRenderingContext&  aRenderingContext);

  nsIFrame*                 mForFrame;
  const nsStyleImage*       mImage;
  nsStyleImageType          mType;
  nsCOMPtr<imgIContainer>   mImageContainer;
  RefPtr<nsStyleGradient> mGradientData;
  nsIFrame*                 mPaintServerFrame;
  nsLayoutUtils::SurfaceFromElementResult mImageElementSurface;
  DrawResult                mPrepareResult;
  nsSize                    mSize; // unscaled size of the image, in app units
  uint32_t                  mFlags;
  mozilla::gfx::ExtendMode  mExtendMode;
  uint8_t                   mMaskOp;
};

/**
 * A struct representing all the information needed to paint a background
 * image to some target, taking into account all CSS background-* properties.
 * See PrepareImageLayer.
 */
struct nsBackgroundLayerState {
  typedef mozilla::gfx::CompositionOp CompositionOp;

  /**
   * @param aFlags some combination of nsCSSRendering::PAINTBG_* flags
   */
  nsBackgroundLayerState(nsIFrame* aForFrame, const nsStyleImage* aImage,
                         uint32_t aFlags)
    : mImageRenderer(aForFrame, aImage, aFlags)
  {}

  /**
   * The nsImageRenderer that will be used to draw the background.
   */
  nsImageRenderer mImageRenderer;
  /**
   * A rectangle that one copy of the image tile is mapped onto. Same
   * coordinate system as aBorderArea/aBGClipRect passed into
   * PrepareImageLayer.
   */
  nsRect mDestArea;
  /**
   * The actual rectangle that should be filled with (complete or partial)
   * image tiles. Same coordinate system as aBorderArea/aBGClipRect passed into
   * PrepareImageLayer.
   */
  nsRect mFillArea;
  /**
   * The anchor point that should be snapped to a pixel corner. Same
   * coordinate system as aBorderArea/aBGClipRect passed into
   * PrepareImageLayer.
   */
  nsPoint mAnchor;
  /**
   * The repeat size computed as a result of using the background-repeat property
   * 'space' keyword, which is the image size plus spacing.
   */
  nsSize mRepeatSize;
};

struct nsCSSRendering {
  typedef mozilla::gfx::CompositionOp CompositionOp;
  typedef mozilla::gfx::DrawTarget DrawTarget;
  typedef mozilla::gfx::Float Float;
  typedef mozilla::gfx::Point Point;
  typedef mozilla::gfx::Rect Rect;
  typedef mozilla::gfx::Size Size;
  typedef mozilla::gfx::RectCornerRadii RectCornerRadii;
  typedef mozilla::image::DrawResult DrawResult;
  typedef nsIFrame::Sides Sides;

  /**
   * Initialize any static variables used by nsCSSRendering.
   */
  static void Init();
  
  /**
   * Clean up any static variables used by nsCSSRendering.
   */
  static void Shutdown();
  
  static void PaintBoxShadowInner(nsPresContext* aPresContext,
                                  nsRenderingContext& aRenderingContext,
                                  nsIFrame* aForFrame,
                                  const nsRect& aFrameArea);

  static void PaintBoxShadowOuter(nsPresContext* aPresContext,
                                  nsRenderingContext& aRenderingContext,
                                  nsIFrame* aForFrame,
                                  const nsRect& aFrameArea,
                                  const nsRect& aDirtyRect,
                                  float aOpacity = 1.0);

  static void ComputePixelRadii(const nscoord *aAppUnitsRadii,
                                nscoord aAppUnitsPerPixel,
                                RectCornerRadii *oBorderRadii);

  /**
   * Render the border for an element using css rendering rules
   * for borders. aSkipSides says which sides to skip
   * when rendering, the default is to skip none.
   */
  static DrawResult PaintBorder(nsPresContext* aPresContext,
                                nsRenderingContext& aRenderingContext,
                                nsIFrame* aForFrame,
                                const nsRect& aDirtyRect,
                                const nsRect& aBorderArea,
                                nsStyleContext* aStyleContext,
                                mozilla::PaintBorderFlags aFlags,
                                Sides aSkipSides = Sides());

  /**
   * Like PaintBorder, but taking an nsStyleBorder argument instead of
   * getting it from aStyleContext. aSkipSides says which sides to skip
   * when rendering, the default is to skip none.
   */
  static DrawResult PaintBorderWithStyleBorder(nsPresContext* aPresContext,
                                               nsRenderingContext& aRenderingContext,
                                               nsIFrame* aForFrame,
                                               const nsRect& aDirtyRect,
                                               const nsRect& aBorderArea,
                                               const nsStyleBorder& aBorderStyle,
                                               nsStyleContext* aStyleContext,
                                               mozilla::PaintBorderFlags aFlags,
                                               Sides aSkipSides = Sides());


  /**
   * Render the outline for an element using css rendering rules
   * for borders.
   */
  static void PaintOutline(nsPresContext* aPresContext,
                          nsRenderingContext& aRenderingContext,
                          nsIFrame* aForFrame,
                          const nsRect& aDirtyRect,
                          const nsRect& aBorderArea,
                          nsStyleContext* aStyleContext);

  /**
   * Render keyboard focus on an element.
   * |aFocusRect| is the outer rectangle of the focused element.
   * Uses a fixed style equivalent to "1px dotted |aColor|".
   * Not used for controls, because the native theme may differ.
   */
  static void PaintFocus(nsPresContext* aPresContext,
                         DrawTarget* aDrawTarget,
                         const nsRect& aFocusRect,
                         nscolor aColor);

  /**
   * Render a gradient for an element.
   * aDest is the rect for a single tile of the gradient on the destination.
   * aFill is the rect on the destination to be covered by repeated tiling of
   * the gradient.
   * aSrc is the part of the gradient to be rendered into a tile (aDest), if
   * aSrc and aDest are different sizes, the image will be scaled to map aSrc
   * onto aDest.
   * aIntrinsicSize is the size of the source gradient.
   */
  static void PaintGradient(nsPresContext* aPresContext,
                            nsRenderingContext& aRenderingContext,
                            nsStyleGradient* aGradient,
                            const nsRect& aDirtyRect,
                            const nsRect& aDest,
                            const nsRect& aFill,
                            const mozilla::CSSIntRect& aSrc,
                            const nsSize& aIntrinsiceSize);

  /**
   * Find the frame whose background style should be used to draw the
   * canvas background. aForFrame must be the frame for the root element
   * whose background style should be used. This function will return
   * aForFrame unless the <body> background should be propagated, in
   * which case we return the frame associated with the <body>'s background.
   */
  static nsIFrame* FindBackgroundStyleFrame(nsIFrame* aForFrame);

  /**
   * @return true if |aFrame| is a canvas frame, in the CSS sense.
   */
  static bool IsCanvasFrame(nsIFrame* aFrame);

  /**
   * Fill in an aBackgroundSC to be used to paint the background
   * for an element.  This applies the rules for propagating
   * backgrounds between BODY, the root element, and the canvas.
   * @return true if there is some meaningful background.
   */
  static bool FindBackground(nsIFrame* aForFrame,
                             nsStyleContext** aBackgroundSC);

  /**
   * As FindBackground, but the passed-in frame is known to be a root frame
   * (returned from nsCSSFrameConstructor::GetRootElementStyleFrame())
   * and there is always some meaningful background returned.
   */
  static nsStyleContext* FindRootFrameBackground(nsIFrame* aForFrame);

  /**
   * Returns background style information for the canvas.
   *
   * @param aForFrame
   *   the frame used to represent the canvas, in the CSS sense (i.e.
   *   nsCSSRendering::IsCanvasFrame(aForFrame) must be true)
   * @param aRootElementFrame
   *   the frame representing the root element of the document
   * @param aBackground
   *   contains background style information for the canvas on return
   */
  static nsStyleContext*
  FindCanvasBackground(nsIFrame* aForFrame, nsIFrame* aRootElementFrame)
  {
    MOZ_ASSERT(IsCanvasFrame(aForFrame), "not a canvas frame");
    if (aRootElementFrame)
      return FindRootFrameBackground(aRootElementFrame);

    // This should always give transparent, so we'll fill it in with the
    // default color if needed.  This seems to happen a bit while a page is
    // being loaded.
    return aForFrame->StyleContext();
  }

  /**
   * Find a frame which draws a non-transparent background,
   * for various table-related and HR-related backwards-compatibility hacks.
   * This function will also stop if it finds themed frame which might draw
   * background.
   *
   * Be very hesitant if you're considering calling this function -- it's
   * usually not what you want.
   */
  static nsIFrame*
  FindNonTransparentBackgroundFrame(nsIFrame* aFrame,
                                    bool aStartAtParent = false);

  /**
   * Determine the background color to draw taking into account print settings.
   */
  static nscolor
  DetermineBackgroundColor(nsPresContext* aPresContext,
                           nsStyleContext* aStyleContext,
                           nsIFrame* aFrame,
                           bool& aDrawBackgroundImage,
                           bool& aDrawBackgroundColor);

  static nsRect
  ComputeImageLayerPositioningArea(nsPresContext* aPresContext,
                                   nsIFrame* aForFrame,
                                   const nsRect& aBorderArea,
                                   const nsStyleImageLayers::Layer& aLayer,
                                   nsIFrame** aAttachedToFrame,
                                   bool* aOutTransformedFixed);

  static nsBackgroundLayerState
  PrepareImageLayer(nsPresContext* aPresContext,
                    nsIFrame* aForFrame,
                    uint32_t aFlags,
                    const nsRect& aBorderArea,
                    const nsRect& aBGClipRect,
                    const nsStyleImageLayers::Layer& aLayer,
                    bool* aOutIsTransformedFixed = nullptr,
                    CompositionOp aCompositionOp = CompositionOp::OP_OVER);

  struct ImageLayerClipState {
    nsRect mBGClipArea;  // Affected by mClippedRadii
    nsRect mAdditionalBGClipArea;  // Not affected by mClippedRadii
    nsRect mDirtyRect;
    gfxRect mDirtyRectGfx;

    nscoord mRadii[8];
    RectCornerRadii mClippedRadii;
    bool mHasRoundedCorners;
    bool mHasAdditionalBGClipArea;

    // Whether we are being asked to draw with a caller provided background
    // clipping area. If this is true we also disable rounded corners.
    bool mCustomClip;
  };

  static void
  GetImageLayerClip(const nsStyleImageLayers::Layer& aLayer,
                    nsIFrame* aForFrame, const nsStyleBorder& aBorder,
                    const nsRect& aBorderArea, const nsRect& aCallerDirtyRect,
                    bool aWillPaintBorder, nscoord aAppUnitsPerPixel,
                    /* out */ ImageLayerClipState* aClipState);

  /**
   * Render the background for an element using css rendering rules
   * for backgrounds or mask.
   */
  enum {
    /**
     * When this flag is passed, the element's nsDisplayBorder will be
     * painted immediately on top of this background.
     */
    PAINTBG_WILL_PAINT_BORDER = 0x01,
    /**
     * When this flag is passed, images are synchronously decoded.
     */
    PAINTBG_SYNC_DECODE_IMAGES = 0x02,
    /**
     * When this flag is passed, painting will go to the screen so we can
     * take advantage of the fact that it will be clipped to the viewport.
     */
    PAINTBG_TO_WINDOW = 0x04,
    /**
     * When this flag is passed, painting will read properties of mask-image
     * style, instead of background-image.
     */
    PAINTBG_MASK_IMAGE = 0x08
  };
  static DrawResult PaintBackground(nsPresContext* aPresContext,
                                    nsRenderingContext& aRenderingContext,
                                    nsIFrame* aForFrame,
                                    const nsRect& aDirtyRect,
                                    const nsRect& aBorderArea,
                                    uint32_t aFlags,
                                    nsRect* aBGClipRect = nullptr,
                                    int32_t aLayer = -1,
                                    CompositionOp aCompositionOp = CompositionOp::OP_OVER);


  /**
   * Same as |PaintBackground|, except using the provided style structs.
   * This short-circuits the code that ensures that the root element's
   * background is drawn on the canvas.
   * The aLayer parameter allows you to paint a single layer of the background.
   * The default value for aLayer, -1, means that all layers will be painted.
   * The background color will only be painted if the back-most layer is also
   * being painted.
   * aCompositionOp is only respected if a single layer is specified (aLayer != -1).
   * If all layers are painted, the image layer's blend mode (or the mask
   * layer's composition mode) will be used.
   */
  static DrawResult PaintBackgroundWithSC(nsPresContext* aPresContext,
                                          nsRenderingContext& aRenderingContext,
                                          nsIFrame* aForFrame,
                                          const nsRect& aDirtyRect,
                                          const nsRect& aBorderArea,
                                          nsStyleContext *aStyleContext,
                                          const nsStyleBorder& aBorder,
                                          uint32_t aFlags,
                                          nsRect* aBGClipRect = nullptr,
                                          int32_t aLayer = -1,
                                          CompositionOp aCompositionOp = CompositionOp::OP_OVER);

  /**
   * Returns the rectangle covered by the given background layer image, taking
   * into account background positioning, sizing, and repetition, but not
   * clipping.
   */
  static nsRect GetBackgroundLayerRect(nsPresContext* aPresContext,
                                       nsIFrame* aForFrame,
                                       const nsRect& aBorderArea,
                                       const nsRect& aClipRect,
                                       const nsStyleImageLayers::Layer& aLayer,
                                       uint32_t aFlags);

  /**
   * Called when we start creating a display list. The frame tree will not
   * change until a matching EndFrameTreeLocked is called.
   */
  static void BeginFrameTreesLocked();
  /**
   * Called when we've finished using a display list. When all
   * BeginFrameTreeLocked calls have been balanced by an EndFrameTreeLocked,
   * the frame tree may start changing again.
   */
  static void EndFrameTreesLocked();

  // Draw a border segment in the table collapsing border model without
  // beveling corners
  static void DrawTableBorderSegment(DrawTarget&          aDrawTarget,
                                     uint8_t              aBorderStyle,
                                     nscolor              aBorderColor,
                                     const nsStyleBackground* aBGColor,
                                     const nsRect&        aBorderRect,
                                     int32_t              aAppUnitsPerDevPixel,
                                     int32_t              aAppUnitsPerCSSPixel,
                                     uint8_t              aStartBevelSide = 0,
                                     nscoord              aStartBevelOffset = 0,
                                     uint8_t              aEndBevelSide = 0,
                                     nscoord              aEndBevelOffset = 0);

  // NOTE: pt, dirtyRect, lineSize, ascent, offset in the following
  //       structs are non-rounded device pixels, not app units.
  struct DecorationRectParams
  {
    // The width [length] and the height [thickness] of the decoration
    // line. This is a "logical" size in textRun orientation, so that
    // for a vertical textrun, width will actually be a physical height;
    // and conversely, height will be a physical width.
    Size lineSize;
    // The ascent of the text.
    Float ascent = 0.0f;
    // The offset of the decoration line from the baseline of the text
    // (if the value is positive, the line is lifted up).
    Float offset = 0.0f;
    // If descentLimit is zero or larger and the underline overflows
    // from the descent space, the underline should be lifted up as far
    // as possible.  Note that this does not mean the underline never
    // overflows from this limitation, because if the underline is
    // positioned to the baseline or upper, it causes unreadability.
    // Note that if this is zero or larger, the underline rect may be
    // shrunken if it's possible.  Therefore, this value is used for
    // strikeout line and overline too.
    Float descentLimit = -1.0f;
    // Which line will be painted. The value can be
    // NS_STYLE_TEXT_DECORATION_LINE_UNDERLINE or
    // NS_STYLE_TEXT_DECORATION_LINE_OVERLINE or
    // NS_STYLE_TEXT_DECORATION_LINE_LINE_THROUGH.
    uint8_t decoration = NS_STYLE_TEXT_DECORATION_LINE_UNDERLINE;
    // The style of the decoration line such as
    // NS_STYLE_TEXT_DECORATION_STYLE_*.
    uint8_t style = NS_STYLE_TEXT_DECORATION_STYLE_NONE;
    bool vertical = false;
  };
  struct PaintDecorationLineParams : DecorationRectParams
  {
    // No need to paint outside this rect.
    Rect dirtyRect;
    // The top/left edge of the text.
    Point pt;
    // The color of the decoration line.
    nscolor color = NS_RGBA(0, 0, 0, 0);
    // The distance between the left edge of the given frame and the
    // position of the text as positioned without offset of the shadow.
    Float icoordInFrame = 0.0f;
  };

  /**
   * Function for painting the decoration lines for the text.
   *
   *   input:
   *     @param aFrame            the frame which needs the decoration line
   *     @param aGfxContext
   */
  static void PaintDecorationLine(nsIFrame* aFrame, DrawTarget& aDrawTarget,
                                  const PaintDecorationLineParams& aParams);

  /**
   * Returns a Rect corresponding to the outline of the decoration line for the
   * given text metrics.  Arguments have the same meaning as for
   * PaintDecorationLine.  Currently this only works for solid
   * decorations; for other decoration styles the returned Rect will be empty.
   */
  static Rect DecorationLineToPath(const PaintDecorationLineParams& aParams);

  /**
   * Function for getting the decoration line rect for the text.
   * NOTE: aLineSize, aAscent and aOffset are non-rounded device pixels,
   *       not app units.
   *   input:
   *     @param aPresContext
   *   output:
   *     @return                  the decoration line rect for the input,
   *                              the each values are app units.
   */
  static nsRect GetTextDecorationRect(nsPresContext* aPresContext,
                                      const DecorationRectParams& aParams);

  static CompositionOp GetGFXBlendMode(uint8_t mBlendMode) {
    switch (mBlendMode) {
      case NS_STYLE_BLEND_NORMAL:      return CompositionOp::OP_OVER;
      case NS_STYLE_BLEND_MULTIPLY:    return CompositionOp::OP_MULTIPLY;
      case NS_STYLE_BLEND_SCREEN:      return CompositionOp::OP_SCREEN;
      case NS_STYLE_BLEND_OVERLAY:     return CompositionOp::OP_OVERLAY;
      case NS_STYLE_BLEND_DARKEN:      return CompositionOp::OP_DARKEN;
      case NS_STYLE_BLEND_LIGHTEN:     return CompositionOp::OP_LIGHTEN;
      case NS_STYLE_BLEND_COLOR_DODGE: return CompositionOp::OP_COLOR_DODGE;
      case NS_STYLE_BLEND_COLOR_BURN:  return CompositionOp::OP_COLOR_BURN;
      case NS_STYLE_BLEND_HARD_LIGHT:  return CompositionOp::OP_HARD_LIGHT;
      case NS_STYLE_BLEND_SOFT_LIGHT:  return CompositionOp::OP_SOFT_LIGHT;
      case NS_STYLE_BLEND_DIFFERENCE:  return CompositionOp::OP_DIFFERENCE;
      case NS_STYLE_BLEND_EXCLUSION:   return CompositionOp::OP_EXCLUSION;
      case NS_STYLE_BLEND_HUE:         return CompositionOp::OP_HUE;
      case NS_STYLE_BLEND_SATURATION:  return CompositionOp::OP_SATURATION;
      case NS_STYLE_BLEND_COLOR:       return CompositionOp::OP_COLOR;
      case NS_STYLE_BLEND_LUMINOSITY:  return CompositionOp::OP_LUMINOSITY;
      default:      MOZ_ASSERT(false); return CompositionOp::OP_OVER;
    }
  }

  static CompositionOp GetGFXCompositeMode(uint8_t aCompositeMode) {
    switch (aCompositeMode) {
      case NS_STYLE_MASK_COMPOSITE_ADD:        return CompositionOp::OP_OVER;
      case NS_STYLE_MASK_COMPOSITE_SUBSTRACT:  return CompositionOp::OP_OUT;
      case NS_STYLE_MASK_COMPOSITE_INTERSECT:  return CompositionOp::OP_IN;
      case NS_STYLE_MASK_COMPOSITE_EXCLUDE:    return CompositionOp::OP_XOR;
      default:              MOZ_ASSERT(false); return CompositionOp::OP_OVER;
    }

  }
protected:
  static gfxRect GetTextDecorationRectInternal(
      const Point& aPt, const DecorationRectParams& aParams);

  /**
   * Returns inflated rect for painting a decoration line.
   * Complex style decoration lines should be painted from leftmost of nearest
   * ancestor block box because that makes better look of connection of lines
   * for different nodes.  ExpandPaintingRectForDecorationLine() returns
   * a rect for actual painting rect for the clipped rect.
   *
   * input:
   *     @param aFrame            the frame which needs the decoration line.
   *     @param aStyle            the style of the complex decoration line
   *                              NS_STYLE_TEXT_DECORATION_STYLE_DOTTED or
   *                              NS_STYLE_TEXT_DECORATION_STYLE_DASHED or
   *                              NS_STYLE_TEXT_DECORATION_STYLE_WAVY.
   *     @param aClippedRect      the clipped rect for the decoration line.
   *                              in other words, visible area of the line.
   *     @param aICoordInFrame  the distance between inline-start edge of aFrame
   *                              and aClippedRect.pos.
   *     @param aCycleLength      the width of one cycle of the line style.
   */
  static Rect ExpandPaintingRectForDecorationLine(
                   nsIFrame* aFrame,
                   const uint8_t aStyle,
                   const Rect &aClippedRect,
                   const Float aICoordInFrame,
                   const Float aCycleLength,
                   bool aVertical);
};

/*
 * nsContextBoxBlur
 * Creates an 8-bit alpha channel context for callers to draw in, blurs the
 * contents of that context and applies it as a 1-color mask on a
 * different existing context. Uses gfxAlphaBoxBlur as its back end.
 *
 * You must call Init() first to create a suitable temporary surface to draw
 * on.  You must then draw any desired content onto the given context, then
 * call DoPaint() to apply the blurred content as a single-color mask. You
 * can only call Init() once, so objects cannot be reused.
 *
 * This is very useful for creating drop shadows or silhouettes.
 */
class nsContextBoxBlur {
  typedef mozilla::gfx::Color Color;
  typedef mozilla::gfx::DrawTarget DrawTarget;
  typedef mozilla::gfx::RectCornerRadii RectCornerRadii;

public:
  enum {
    FORCE_MASK = 0x01
  };
  /**
   * Prepares a gfxContext to draw on. Do not call this twice; if you want
   * to get the gfxContext again use GetContext().
   *
   * @param aRect                The coordinates of the surface to create.
   *                             All coordinates must be in app units.
   *                             This must not include the blur radius, pass
   *                             it as the second parameter and everything
   *                             is taken care of.
   *
   * @param aBlurRadius          The blur radius in app units.
   *
   * @param aAppUnitsPerDevPixel The number of app units in a device pixel,
   *                             for conversion.  Most of the time you'll
   *                             pass this from the current PresContext if
   *                             available.
   *
   * @param aDestinationCtx      The graphics context to apply the blurred
   *                             mask to when you call DoPaint(). Make sure
   *                             it is not destroyed before you call
   *                             DoPaint(). To set the color of the
   *                             resulting blurred graphic mask, you must
   *                             set the color on this context before
   *                             calling Init().
   *
   * @param aDirtyRect           The absolute dirty rect in app units. Used to
   *                             optimize the temporary surface size and speed up blur.
   *
   * @param aSkipRect            An area in device pixels (NOT app units!) to avoid
   *                             blurring over, to prevent unnecessary work.
   *                             
   * @param aFlags               FORCE_MASK to ensure that the content drawn to the
   *                             returned gfxContext is used as a mask, and not
   *                             drawn directly to aDestinationCtx.
   *
   * @return            A blank 8-bit alpha-channel-only graphics context to
   *                    draw on, or null on error. Must not be freed. The
   *                    context has a device offset applied to it given by
   *                    aRect. This means you can use coordinates as if it
   *                    were at the desired position at aRect and you don't
   *                    need to worry about translating any coordinates to
   *                    draw on this temporary surface.
   *
   * If aBlurRadius is 0, the returned context is aDestinationCtx and
   * DoPaint() does nothing, because no blurring is required. Therefore, you
   * should prepare the destination context as if you were going to draw
   * directly on it instead of any temporary surface created in this class.
   */
  gfxContext* Init(const nsRect& aRect, nscoord aSpreadRadius,
                   nscoord aBlurRadius,
                   int32_t aAppUnitsPerDevPixel, gfxContext* aDestinationCtx,
                   const nsRect& aDirtyRect, const gfxRect* aSkipRect,
                   uint32_t aFlags = 0);

  /**
   * Does the actual blurring and mask applying. Users of this object *must*
   * have called Init() first, then have drawn whatever they want to be
   * blurred onto the internal gfxContext before calling this.
   */
  void DoPaint();

  /**
   * Gets the internal gfxContext at any time. Must not be freed. Avoid
   * calling this before calling Init() since the context would not be
   * constructed at that point.
   */
  gfxContext* GetContext();


  /**
   * Get the margin associated with the given blur radius, i.e., the
   * additional area that might be painted as a result of it.  (The
   * margin for a spread radius is itself, on all sides.)
   */
  static nsMargin GetBlurRadiusMargin(nscoord aBlurRadius,
                                      int32_t aAppUnitsPerDevPixel);

  /**
   * Blurs a coloured rectangle onto aDestinationCtx. This is equivalent
   * to calling Init(), drawing a rectangle onto the returned surface
   * and then calling DoPaint, but may let us optimize better in the
   * backend.
   *
   * @param aDestinationCtx      The destination to blur to.
   * @param aRect                The rectangle to blur in app units.
   * @param aAppUnitsPerDevPixel The number of app units in a device pixel,
   *                             for conversion.  Most of the time you'll
   *                             pass this from the current PresContext if
   *                             available.
   * @param aCornerRadii         Corner radii for aRect, if it is a rounded
   *                             rectangle.
   * @param aBlurRadius          The blur radius in app units.
   * @param aShadowColor         The color to draw the blurred shadow.
   * @param aDirtyRect           The absolute dirty rect in app units. Used to
   *                             optimize the temporary surface size and speed up blur.
   * @param aSkipRect            An area in device pixels (NOT app units!) to avoid
   *                             blurring over, to prevent unnecessary work.
   */
  static void BlurRectangle(gfxContext* aDestinationCtx,
                            const nsRect& aRect,
                            int32_t aAppUnitsPerDevPixel,
                            RectCornerRadii* aCornerRadii,
                            nscoord aBlurRadius,
                            const Color& aShadowColor,
                            const nsRect& aDirtyRect,
                            const gfxRect& aSkipRect);

  /**
   * Draws a blurred inset box shadow shape onto the destination surface.
   * Like BlurRectangle, this is equivalent to calling Init(),
   * drawing a rectangle onto the returned surface
   * and then calling DoPaint, but may let us optimize better in the
   * backend.
   *
   * @param aDestinationCtx      The destination to blur to.
   * @param aDestinationRect     The rectangle to blur in app units.
   * @param aShadowClipRect      The inside clip rect that creates the path.
   * @param aShadowColor         The color of the blur
   * @param aBlurRadiusAppUnits  The blur radius in app units
   * @param aSpreadRadiusAppUnits The spread radius in app units.
   * @param aAppUnitsPerDevPixel The number of app units in a device pixel,
   *                             for conversion.  Most of the time you'll
   *                             pass this from the current PresContext if
   *                             available.
   * @param aHasBorderRadius     If this inset box blur has a border radius
   * @param aInnerClipRectRadii  The clip rect radii used for the inside rect's path.
   * @param aSkipRect            An area in device pixels (NOT app units!) to avoid
   *                             blurring over, to prevent unnecessary work.
   */
  bool InsetBoxBlur(gfxContext* aDestinationCtx,
                    mozilla::gfx::Rect aDestinationRect,
                    mozilla::gfx::Rect aShadowClipRect,
                    mozilla::gfx::Color& aShadowColor,
                    nscoord aBlurRadiusAppUnits,
                    nscoord aSpreadRadiusAppUnits,
                    int32_t aAppUnitsPerDevPixel,
                    bool aHasBorderRadius,
                    RectCornerRadii& aInnerClipRectRadii,
                    mozilla::gfx::Rect aSkipRect,
                    mozilla::gfx::Point aShadowOffset);

protected:
  static void GetBlurAndSpreadRadius(DrawTarget* aDestDrawTarget,
                                     int32_t aAppUnitsPerDevPixel,
                                     nscoord aBlurRadius,
                                     nscoord aSpreadRadius,
                                     mozilla::gfx::IntSize& aOutBlurRadius,
                                     mozilla::gfx::IntSize& aOutSpreadRadius,
                                     bool aConstrainSpreadRadius = true);

  gfxAlphaBoxBlur mAlphaBoxBlur;
  RefPtr<gfxContext> mContext;
  gfxContext* mDestinationCtx;

  /* This is true if the blur already has it's content transformed
   * by mDestinationCtx's transform */
  bool mPreTransformed;
};

#endif /* nsCSSRendering_h___ */
