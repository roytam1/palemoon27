/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/AnimationEffectTiming.h"

#include "mozilla/dom/AnimatableBinding.h"
#include "mozilla/dom/AnimationEffectTimingBinding.h"

namespace mozilla {
namespace dom {

JSObject*
AnimationEffectTiming::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto)
{
  return AnimationEffectTimingBinding::Wrap(aCx, this, aGivenProto);
}

void
AnimationEffectTiming::NotifyTimingUpdate()
{
  if (mEffect) {
    mEffect->NotifySpecifiedTimingUpdated();
  }
}

void
AnimationEffectTiming::SetEndDelay(double aEndDelay)
{
  TimeDuration endDelay = TimeDuration::FromMilliseconds(aEndDelay);
  if (mTiming.mEndDelay == endDelay) {
    return;
  }
  mTiming.mEndDelay = endDelay;

  NotifyTimingUpdate();
}

void
AnimationEffectTiming::SetDuration(const UnrestrictedDoubleOrString& aDuration)
{
  if (mTiming.mDuration.IsUnrestrictedDouble() &&
      aDuration.IsUnrestrictedDouble() &&
      mTiming.mDuration.GetAsUnrestrictedDouble() ==
        aDuration.GetAsUnrestrictedDouble()) {
    return;
  }

  if (mTiming.mDuration.IsString() && aDuration.IsString() &&
      mTiming.mDuration.GetAsString() == aDuration.GetAsString()) {
    return;
  }

  if (aDuration.IsUnrestrictedDouble()) {
    mTiming.mDuration.SetAsUnrestrictedDouble() =
      aDuration.GetAsUnrestrictedDouble();
  } else {
    mTiming.mDuration.SetAsString() = aDuration.GetAsString();
  }

  NotifyTimingUpdate();
}

} // namespace dom
} // namespace mozilla
