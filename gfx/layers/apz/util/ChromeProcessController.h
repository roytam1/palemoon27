/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_ChromeProcessController_h
#define mozilla_layers_ChromeProcessController_h

#include "mozilla/layers/GoannaContentController.h"
#include "nsCOMPtr.h"
#include "mozilla/nsRefPtr.h"

class nsIDOMWindowUtils;
class nsIDocument;
class nsIPresShell;
class nsIWidget;

class MessageLoop;

namespace mozilla {

namespace layers {

class APZEventState;
class CompositorParent;

// A ChromeProcessController is attached to the root of a compositor's layer
// tree.
class ChromeProcessController : public mozilla::layers::GoannaContentController
{
  typedef mozilla::layers::FrameMetrics FrameMetrics;
  typedef mozilla::layers::ScrollableLayerGuid ScrollableLayerGuid;

public:
  explicit ChromeProcessController(nsIWidget* aWidget, APZEventState* aAPZEventState);
  virtual void Destroy() MOZ_OVERRIDE;

  // GoannaContentController interface
  virtual void RequestContentRepaint(const FrameMetrics& aFrameMetrics) MOZ_OVERRIDE;
  virtual void PostDelayedTask(Task* aTask, int aDelayMs) MOZ_OVERRIDE;
  virtual void RequestFlingSnap(const FrameMetrics::ViewID& aScrollId,
                                const mozilla::CSSPoint& aDestination) MOZ_OVERRIDE;
  virtual void AcknowledgeScrollUpdate(const FrameMetrics::ViewID& aScrollId,
                                       const uint32_t& aScrollGeneration) MOZ_OVERRIDE;

  virtual void HandleDoubleTap(const mozilla::CSSPoint& aPoint, Modifiers aModifiers,
                               const ScrollableLayerGuid& aGuid) override {}
  virtual void HandleSingleTap(const mozilla::CSSPoint& aPoint, Modifiers aModifiers,
                               const ScrollableLayerGuid& aGuid) MOZ_OVERRIDE;
  virtual void HandleLongTap(const mozilla::CSSPoint& aPoint, Modifiers aModifiers,
                               const ScrollableLayerGuid& aGuid,
                               uint64_t aInputBlockId) MOZ_OVERRIDE;
  virtual void HandleLongTapUp(const CSSPoint& aPoint, Modifiers aModifiers,
                               const ScrollableLayerGuid& aGuid) MOZ_OVERRIDE;
  virtual void SendAsyncScrollDOMEvent(bool aIsRoot, const mozilla::CSSRect &aContentRect,
                                       const mozilla::CSSSize &aScrollableSize) override {}
  virtual void NotifyAPZStateChange(const ScrollableLayerGuid& aGuid,
                                    APZStateChange aChange,
                                    int aArg) MOZ_OVERRIDE;
private:
  nsCOMPtr<nsIWidget> mWidget;
  nsRefPtr<APZEventState> mAPZEventState;
  MessageLoop* mUILoop;

  void InitializeRoot();
  float GetPresShellResolution() const;
  nsIPresShell* GetPresShell() const;
  nsIDocument* GetDocument() const;
  already_AddRefed<nsIDOMWindowUtils> GetDOMWindowUtils() const;
};

} // namespace layers
} // namespace mozilla

#endif /* mozilla_layers_ChromeProcessController_h */
