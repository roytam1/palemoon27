/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-*/
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef VIDEOSTREAMTRACK_H_
#define VIDEOSTREAMTRACK_H_

#include "MediaStreamTrack.h"
#include "DOMMediaStream.h"

namespace mozilla {
namespace dom {

class VideoStreamTrack : public MediaStreamTrack {
public:
  VideoStreamTrack(DOMMediaStream* aStream, TrackID aTrackID, const nsString& aLabel)
    : MediaStreamTrack(aStream, aTrackID, aLabel) {}

  JSObject* WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto) override;

  VideoStreamTrack* AsVideoStreamTrack() override { return this; }

  // WebIDL
  void GetKind(nsAString& aKind) override { aKind.AssignLiteral("video"); }
};

} // namespace dom
} // namespace mozilla

#endif /* VIDEOSTREAMTRACK_H_ */
