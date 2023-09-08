/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-*/
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MEDIASTREAMTRACK_H_
#define MEDIASTREAMTRACK_H_

#include "mozilla/DOMEventTargetHelper.h"
#include "nsID.h"
#include "StreamBuffer.h"
#include "MediaTrackConstraints.h"

namespace mozilla {

class DOMMediaStream;

namespace dom {

class AudioStreamTrack;
class VideoStreamTrack;

/**
 * Class representing a track in a DOMMediaStream.
 */
class MediaStreamTrack : public DOMEventTargetHelper {
public:
  /**
   * aTrackID is the MediaStreamGraph track ID for the track in the
   * MediaStream owned by aStream.
   */
  MediaStreamTrack(DOMMediaStream* aStream, TrackID aTrackID, const nsString& aLabel);

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(MediaStreamTrack,
                                           DOMEventTargetHelper)

  DOMMediaStream* GetParentObject() const { return mOwningStream; }
  virtual JSObject* WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto) override = 0;

  /**
   * Returns the DOMMediaStream owning this track.
   */
  DOMMediaStream* GetStream() const { return mOwningStream; }

  /**
   * Returns the TrackID this stream has in its owning DOMMediaStream's Owned
   * stream.
   */
  TrackID GetTrackID() const { return mTrackID; }
  virtual AudioStreamTrack* AsAudioStreamTrack() { return nullptr; }
  virtual VideoStreamTrack* AsVideoStreamTrack() { return nullptr; }

  // WebIDL
  virtual void GetKind(nsAString& aKind) = 0;
  void GetId(nsAString& aID) const;
  void GetLabel(nsAString& aLabel) { aLabel.Assign(mLabel); }
  bool Enabled() { return mEnabled; }
  void SetEnabled(bool aEnabled);
  void Stop();
  already_AddRefed<Promise>
  ApplyConstraints(const dom::MediaTrackConstraints& aConstraints, ErrorResult &aRv);

  bool Ended() const { return mEnded; }
  // Notifications from the MediaStreamGraph
  void NotifyEnded() { mEnded = true; }

  // Webrtc allows the remote side to name tracks whatever it wants, and we
  // need to surface this to content.
  void AssignId(const nsAString& aID) { mID = aID; }

protected:
  virtual ~MediaStreamTrack();

  RefPtr<DOMMediaStream> mOwningStream;
  TrackID mTrackID;
  nsString mID;
  nsString mLabel;
  bool mEnded;
  bool mEnabled;
};

} // namespace dom
} // namespace mozilla

#endif /* MEDIASTREAMTRACK_H_ */
