/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=4 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_GoannaContentController_h
#define mozilla_layers_GoannaContentController_h

#include "FrameMetrics.h"               // for FrameMetrics, etc
#include "Units.h"                      // for CSSPoint, CSSRect, etc
#include "mozilla/Assertions.h"         // for MOZ_ASSERT_HELPER2
#include "mozilla/EventForwards.h"      // for Modifiers
#include "nsISupportsImpl.h"

class Task;

namespace mozilla {
namespace layers {

class GoannaContentController
{
public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(GoannaContentController)

  /**
   * Requests a paint of the given FrameMetrics |aFrameMetrics| from Goanna.
   * Implementations per-platform are responsible for actually handling this.
   * This method will always be called on the Goanna main thread.
   */
  virtual void RequestContentRepaint(const FrameMetrics& aFrameMetrics) = 0;

  /**
   * Requests handling of a scroll snapping at the end of a fling gesture for
   * the scrollable frame with the given scroll id. aDestination specifies the
   * expected landing position of the fling if no snapping were to be performed.
   */
  virtual void RequestFlingSnap(const FrameMetrics::ViewID& aScrollId,
                                const mozilla::CSSPoint& aDestination) = 0;

  /**
   * Acknowledges the recipt of a scroll offset update for the scrollable
   * frame with the given scroll id. This is used to maintain consistency
   * between APZ and other sources of scroll changes.
   */
  virtual void AcknowledgeScrollUpdate(const FrameMetrics::ViewID& aScrollId,
                                       const uint32_t& aScrollGeneration) = 0;

  /**
   * Requests handling of a double tap. |aPoint| is in CSS pixels, relative to
   * the current scroll offset. This should eventually round-trip back to
   * AsyncPanZoomController::ZoomToRect with the dimensions that we want to zoom
   * to.
   */
  virtual void HandleDoubleTap(const CSSPoint& aPoint,
                               Modifiers aModifiers,
                               const ScrollableLayerGuid& aGuid) = 0;

  /**
   * Requests handling a single tap. |aPoint| is in CSS pixels, relative to the
   * current scroll offset. This should simulate and send to content a mouse
   * button down, then mouse button up at |aPoint|.
   */
  virtual void HandleSingleTap(const CSSPoint& aPoint,
                               Modifiers aModifiers,
                               const ScrollableLayerGuid& aGuid) = 0;

  /**
   * Requests handling a long tap. |aPoint| is in CSS pixels, relative to the
   * current scroll offset.
   */
  virtual void HandleLongTap(const CSSPoint& aPoint,
                             Modifiers aModifiers,
                             const ScrollableLayerGuid& aGuid,
                             uint64_t aInputBlockId) = 0;

  /**
   * Requests handling of releasing a long tap. |aPoint| is in CSS pixels,
   * relative to the current scroll offset. HandleLongTapUp will always be
   * preceeded by HandleLongTap. However not all calls to HandleLongTap will
   * be followed by a HandleLongTapUp (for example, if the user drags
   * around between the long-tap and lifting their finger, or if content
   * notifies the APZ that the long-tap event was prevent-defaulted).
   */
  virtual void HandleLongTapUp(const CSSPoint& aPoint,
                               Modifiers aModifiers,
                               const ScrollableLayerGuid& aGuid) = 0;

  /**
   * Requests sending a mozbrowserasyncscroll domevent to embedder.
   * |aContentRect| is in CSS pixels, relative to the current cssPage.
   * |aScrollableSize| is the current content width/height in CSS pixels.
   */
  virtual void SendAsyncScrollDOMEvent(bool aIsRoot,
                                       const CSSRect &aContentRect,
                                       const CSSSize &aScrollableSize) = 0;

  /**
   * Schedules a runnable to run on the controller/UI thread at some time
   * in the future.
   * This method must always be called on the controller thread.
   */
  virtual void PostDelayedTask(Task* aTask, int aDelayMs) = 0;

  /**
   * Retrieves the last known zoom constraints for the root scrollable layer
   * for this layers tree. This function should return false if there are no
   * last known zoom constraints.
   */
  virtual bool GetRootZoomConstraints(ZoomConstraints* aOutConstraints)
  {
    return false;
  }

  /**
   * APZ uses |FrameMetrics::mCompositionBounds| for hit testing. Sometimes,
   * widget code has knowledge of a touch-sensitive region that should
   * additionally constrain hit testing for all frames associated with the
   * controller. This method allows APZ to query the controller for such a
   * region. A return value of true indicates that the controller has such a
   * region, and it is returned in |aOutRegion|.
   * TODO: once bug 928833 is implemented, this should be removed, as
   * APZ can then get the correct touch-sensitive region for each frame
   * directly from the layer.
   */
  virtual bool GetTouchSensitiveRegion(CSSRect* aOutRegion)
  {
    return false;
  }

  enum APZStateChange {
    /**
     * APZ started modifying the view (including panning, zooming, and fling).
     */
    TransformBegin,
    /**
     * APZ finished modifying the view.
     */
    TransformEnd,
    /**
     * APZ started a touch.
     * |aArg| is 1 if touch can be a pan, 0 otherwise.
     */
    StartTouch,
    /**
     * APZ started a pan.
     */
    StartPanning,
    /**
     * APZ finished processing a touch.
     * |aArg| is 1 if touch was a click, 0 otherwise.
     */
    EndTouch,
    APZStateChangeSentinel
  };

  /**
   * General notices of APZ state changes for consumers.
   * |aGuid| identifies the APZC originating the state change.
   * |aChange| identifies the type of state change
   * |aArg| is used by some state changes to pass extra information (see
   *        the documentation for each state change above)
   */
  virtual void NotifyAPZStateChange(const ScrollableLayerGuid& aGuid,
                                    APZStateChange aChange,
                                    int aArg = 0) {}

  GoannaContentController() {}
  virtual void Destroy() {}

protected:
  // Protected destructor, to discourage deletion outside of Release():
  virtual ~GoannaContentController() {}
};

}
}

#endif // mozilla_layers_GoannaContentController_h
