/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_APZCCallbackHelper_h
#define mozilla_layers_APZCCallbackHelper_h

#include "FrameMetrics.h"
#include "mozilla/EventForwards.h"
#include "nsIDOMWindowUtils.h"
#include "nsCOMPtr.h"
#include "nsAutoPtr.h"

class nsIContent;
class nsIDocument;
class nsIWidget;
template<class T> struct already_AddRefed;

namespace mozilla {
namespace layers {

/* A base class for callbacks to be passed to APZCCallbackHelper::SendSetTargetAPZCNotification.
 * If we had something like std::function, we could just use
 * std::function<void(uint64_t, const nsTArray<ScrollableLayerGuid>&)>. */
struct SetTargetAPZCCallback {
public:
  NS_INLINE_DECL_REFCOUNTING(SetTargetAPZCCallback)
  virtual void Run(uint64_t aInputBlockId, const nsTArray<ScrollableLayerGuid>& aTargets) const = 0;
protected:
  virtual ~SetTargetAPZCCallback() {}
};

/* This class contains some helper methods that facilitate implementing the
   GoannaContentController callback interface required by the AsyncPanZoomController.
   Since different platforms need to implement this interface in similar-but-
   not-quite-the-same ways, this utility class provides some helpful methods
   to hold code that can be shared across the different platform implementations.
 */
class APZCCallbackHelper
{
    typedef mozilla::layers::FrameMetrics FrameMetrics;
    typedef mozilla::layers::ScrollableLayerGuid ScrollableLayerGuid;

public:
    /* Checks to see if the pres shell that the given FrameMetrics object refers
       to is still the valid pres shell for the DOMWindowUtils. This can help
       guard against apply stale updates (updates meant for a pres shell that has
       since been torn down and replaced). */
    static bool HasValidPresShellId(nsIDOMWindowUtils* aUtils,
                                    const FrameMetrics& aMetrics);

    /* Applies the scroll and zoom parameters from the given FrameMetrics object to
       the root frame corresponding to the given DOMWindowUtils. If tiled thebes
       layers are enabled, this will align the displayport to tile boundaries.
       Setting the scroll position can cause some small adjustments to be made
       to the actual scroll position. aMetrics' display port and scroll position
       will be updated with any modifications made. */
    static void UpdateRootFrame(nsIDOMWindowUtils* aUtils,
                                FrameMetrics& aMetrics);

    /* Applies the scroll parameters from the given FrameMetrics object to the subframe
       corresponding to the given content object. If tiled thebes
       layers are enabled, this will align the displayport to tile boundaries.
       Setting the scroll position can cause some small adjustments to be made
       to the actual scroll position. aMetrics' display port and scroll position
       will be updated with any modifications made. */
    static void UpdateSubFrame(nsIContent* aContent,
                               FrameMetrics& aMetrics);

    /* Get the DOMWindowUtils for the window corresponding to the given document. */
    static already_AddRefed<nsIDOMWindowUtils> GetDOMWindowUtils(const nsIDocument* aDoc);

    /* Get the DOMWindowUtils for the window corresponding to the givent content
       element. This might be an iframe inside the tab, for instance. */
    static already_AddRefed<nsIDOMWindowUtils> GetDOMWindowUtils(const nsIContent* aContent);

    /* Get the presShellId and view ID for the given content element.
     * If the view ID does not exist, one is created.
     * The pres shell ID should generally already exist; if it doesn't for some
     * reason, false is returned. */
    static bool GetOrCreateScrollIdentifiers(nsIContent* aContent,
                                             uint32_t* aPresShellIdOut,
                                             FrameMetrics::ViewID* aViewIdOut);

    /* Tell layout to perform scroll snapping for the scrollable frame with the
     * given scroll id. aDestination specifies the expected landing position of
     * a current fling or scrolling animation that should be used to select
     * the scroll snap point.
     */
    static void RequestFlingSnap(const FrameMetrics::ViewID& aScrollId,
                                 const mozilla::CSSPoint& aDestination);

    /* Tell layout that we received the scroll offset update for the given view ID, so
       that it accepts future scroll offset updates from APZ. */
    static void AcknowledgeScrollUpdate(const FrameMetrics::ViewID& aScrollId,
                                        const uint32_t& aScrollGeneration);

    /* Apply an "input transform" to the given |aInput| and return the transformed value.
       The input transform applied is the one for the content element corresponding to
       |aGuid|; this is populated in a previous call to UpdateCallbackTransform. See that
       method's documentations for details.
       This method additionally adjusts |aInput| by inversely scaling by the provided
       pres shell resolution, to cancel out a compositor-side transform (added in
       bug 1076241) that APZ doesn't unapply. */
    static CSSPoint ApplyCallbackTransform(const CSSPoint& aInput,
                                           const ScrollableLayerGuid& aGuid,
                                           float aPresShellResolution);

    /* Same as above, but operates on LayoutDeviceIntPoint.
       Requires an additonal |aScale| parameter to convert between CSS and
       LayoutDevice space. */
    static mozilla::LayoutDeviceIntPoint
    ApplyCallbackTransform(const LayoutDeviceIntPoint& aPoint,
                           const ScrollableLayerGuid& aGuid,
                           const CSSToLayoutDeviceScale& aScale,
                           float aPresShellResolution);

    /* Convenience function for applying a callback transform to all touch
     * points of a touch event. */
    static void ApplyCallbackTransform(WidgetTouchEvent& aEvent,
                                       const ScrollableLayerGuid& aGuid,
                                       const CSSToLayoutDeviceScale& aScale,
                                       float aPresShellResolution);

    /* Dispatch a widget event via the widget stored in the event, if any.
     * In a child process, allows the TabParent event-capture mechanism to
     * intercept the event. */
    static nsEventStatus DispatchWidgetEvent(WidgetGUIEvent& aEvent);

    /* Synthesize a mouse event with the given parameters, and dispatch it
     * via the given widget. */
    static nsEventStatus DispatchSynthesizedMouseEvent(uint32_t aMsg,
                                                       uint64_t aTime,
                                                       const LayoutDevicePoint& aRefPoint,
                                                       Modifiers aModifiers,
                                                       nsIWidget* aWidget);

    /* Dispatch a mouse event with the given parameters.
     * Return whether or not any listeners have called preventDefault on the event. */
    static bool DispatchMouseEvent(const nsCOMPtr<nsIDOMWindowUtils>& aUtils,
                                   const nsString& aType,
                                   const CSSPoint& aPoint,
                                   int32_t aButton,
                                   int32_t aClickCount,
                                   int32_t aModifiers,
                                   bool aIgnoreRootScrollFrame,
                                   unsigned short aInputSourceArg);

    /* Fire a single-tap event at the given point. The event is dispatched
     * via the given widget. */
    static void FireSingleTapEvent(const LayoutDevicePoint& aPoint,
                                   Modifiers aModifiers,
                                   nsIWidget* aWidget);

    /* Perform hit-testing on the touch points of |aEvent| to determine
     * which scrollable frames they target. If any of these frames don't have
     * a displayport, set one. Finally, invoke the provided callback with
     * the guids of the target frames. If any displayports needed to be set,
     * the callback is invoked after the next refresh, otherwise it's invoked
     * right away. */
    static void SendSetTargetAPZCNotification(nsIWidget* aWidget,
                                              nsIDocument* aDocument,
                                              const WidgetGUIEvent& aEvent,
                                              const ScrollableLayerGuid& aGuid,
                                              uint64_t aInputBlockId,
                                              const nsRefPtr<SetTargetAPZCCallback>& aCallback);
};

}
}

#endif /* mozilla_layers_APZCCallbackHelper_h */
