/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-*/
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DOMMediaStream.h"
#include "nsContentUtils.h"
#include "nsServiceManagerUtils.h"
#include "nsIUUIDGenerator.h"
#include "mozilla/dom/MediaStreamBinding.h"
#include "mozilla/dom/LocalMediaStreamBinding.h"
#include "mozilla/dom/AudioNode.h"
#include "mozilla/dom/AudioTrack.h"
#include "mozilla/dom/AudioTrackList.h"
#include "mozilla/dom/VideoTrack.h"
#include "mozilla/dom/VideoTrackList.h"
#include "MediaStreamGraph.h"
#include "AudioStreamTrack.h"
#include "VideoStreamTrack.h"

using namespace mozilla;
using namespace mozilla::dom;

class DOMMediaStream::StreamListener : public MediaStreamListener {
public:
  explicit StreamListener(DOMMediaStream* aStream)
    : mStream(aStream)
  {}

  // Main thread only
  void Forget() { mStream = nullptr; }
  DOMMediaStream* GetStream() { return mStream; }

  class TrackChange : public nsRunnable {
  public:
    TrackChange(StreamListener* aListener,
                TrackID aID, StreamTime aTrackOffset,
                uint32_t aEvents, MediaSegment::Type aType)
      : mListener(aListener), mID(aID), mEvents(aEvents), mType(aType)
    {
    }

    NS_IMETHOD Run()
    {
      NS_ASSERTION(NS_IsMainThread(), "main thread only");

      DOMMediaStream* stream = mListener->GetStream();
      if (!stream) {
        return NS_OK;
      }

      nsRefPtr<MediaStreamTrack> track;
      if (mEvents & MediaStreamListener::TRACK_EVENT_CREATED) {
        track = stream->BindDOMTrack(mID, mType);
        if (!track) {
          stream->CreateDOMTrack(mID, mType);
          track = stream->BindDOMTrack(mID, mType);
        }
        stream->NotifyMediaStreamTrackCreated(track);
      } else {
        track = stream->GetDOMTrackFor(mID);
      }
      if (mEvents & MediaStreamListener::TRACK_EVENT_ENDED) {
        if (track) {
          track->NotifyEnded();
          stream->NotifyMediaStreamTrackEnded(track);
        } else {
          NS_ERROR("track ended but not found");
        }
      }
      return NS_OK;
    }

    StreamTime mEndTime;
    nsRefPtr<StreamListener> mListener;
    TrackID mID;
    uint32_t mEvents;
    MediaSegment::Type mType;
  };

  /**
   * Notify that changes to one of the stream tracks have been queued.
   * aTrackEvents can be any combination of TRACK_EVENT_CREATED and
   * TRACK_EVENT_ENDED. aQueuedMedia is the data being added to the track
   * at aTrackOffset (relative to the start of the stream).
   * aQueuedMedia can be null if there is no output.
   */
  virtual void NotifyQueuedTrackChanges(MediaStreamGraph* aGraph, TrackID aID,
                                        StreamTime aTrackOffset,
                                        uint32_t aTrackEvents,
                                        const MediaSegment& aQueuedMedia) override
  {
    if (aTrackEvents & (TRACK_EVENT_CREATED | TRACK_EVENT_ENDED)) {
      nsRefPtr<TrackChange> runnable =
        new TrackChange(this, aID, aTrackOffset, aTrackEvents,
                        aQueuedMedia.GetType());
      aGraph->DispatchToMainThreadAfterStreamStateUpdate(runnable.forget());
    }
  }

  class TracksCreatedRunnable : public nsRunnable {
  public:
    explicit TracksCreatedRunnable(StreamListener* aListener)
      : mListener(aListener)
    {
    }

    NS_IMETHOD Run()
    {
      MOZ_ASSERT(NS_IsMainThread());

      DOMMediaStream* stream = mListener->GetStream();
      if (!stream) {
        return NS_OK;
      }

      stream->TracksCreated();
      return NS_OK;
    }

    nsRefPtr<StreamListener> mListener;
  };

  virtual void NotifyFinishedTrackCreation(MediaStreamGraph* aGraph) override
  {
    nsRefPtr<TracksCreatedRunnable> runnable = new TracksCreatedRunnable(this);
    aGraph->DispatchToMainThreadAfterStreamStateUpdate(runnable.forget());
  }

private:
  // These fields may only be accessed on the main thread
  DOMMediaStream* mStream;
};

NS_IMPL_CYCLE_COLLECTION_CLASS(DOMMediaStream)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(DOMMediaStream,
                                                DOMEventTargetHelper)
  tmp->Destroy();
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mWindow)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mTracks)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mConsumersToKeepAlive)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(DOMMediaStream,
                                                  DOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mWindow)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mTracks)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mConsumersToKeepAlive)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_ADDREF_INHERITED(DOMMediaStream, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(DOMMediaStream, DOMEventTargetHelper)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(DOMMediaStream)
  NS_INTERFACE_MAP_ENTRY(DOMMediaStream)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

NS_IMPL_ADDREF_INHERITED(DOMLocalMediaStream, DOMMediaStream)
NS_IMPL_RELEASE_INHERITED(DOMLocalMediaStream, DOMMediaStream)

NS_INTERFACE_MAP_BEGIN(DOMLocalMediaStream)
  NS_INTERFACE_MAP_ENTRY(DOMLocalMediaStream)
NS_INTERFACE_MAP_END_INHERITING(DOMMediaStream)

NS_IMPL_CYCLE_COLLECTION_INHERITED(DOMAudioNodeMediaStream, DOMMediaStream,
                                   mStreamNode)

NS_IMPL_ADDREF_INHERITED(DOMAudioNodeMediaStream, DOMMediaStream)
NS_IMPL_RELEASE_INHERITED(DOMAudioNodeMediaStream, DOMMediaStream)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(DOMAudioNodeMediaStream)
NS_INTERFACE_MAP_END_INHERITING(DOMMediaStream)

DOMMediaStream::DOMMediaStream()
  : mLogicalStreamStartTime(0),
    mStream(nullptr), mTracksCreated(false),
    mNotifiedOfMediaStreamGraphShutdown(false), mCORSMode(CORS_NONE)
{
  nsresult rv;
  nsCOMPtr<nsIUUIDGenerator> uuidgen =
    do_GetService("@mozilla.org/uuid-generator;1", &rv);

  if (NS_SUCCEEDED(rv) && uuidgen) {
    nsID uuid;
    memset(&uuid, 0, sizeof(uuid));
    rv = uuidgen->GenerateUUIDInPlace(&uuid);
    if (NS_SUCCEEDED(rv)) {
      char buffer[NSID_LENGTH];
      uuid.ToProvidedString(buffer);
      mID = NS_ConvertASCIItoUTF16(buffer);
    }
  }
}

DOMMediaStream::~DOMMediaStream()
{
  Destroy();
}

void
DOMMediaStream::Destroy()
{
  if (mListener) {
    mListener->Forget();
    mListener = nullptr;
  }
  if (mStream) {
    mStream->Destroy();
    mStream = nullptr;
  }
}

JSObject*
DOMMediaStream::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto)
{
  return dom::MediaStreamBinding::Wrap(aCx, this, aGivenProto);
}

double
DOMMediaStream::CurrentTime()
{
  if (!mStream) {
    return 0.0;
  }
  return mStream->
    StreamTimeToSeconds(mStream->GetCurrentTime() - mLogicalStreamStartTime);
}

void
DOMMediaStream::GetId(nsAString& aID) const
{
  aID = mID;
}

void
DOMMediaStream::GetAudioTracks(nsTArray<nsRefPtr<AudioStreamTrack> >& aTracks)
{
  for (uint32_t i = 0; i < mTracks.Length(); ++i) {
    AudioStreamTrack* t = mTracks[i]->AsAudioStreamTrack();
    if (t) {
      aTracks.AppendElement(t);
    }
  }
}

void
DOMMediaStream::GetVideoTracks(nsTArray<nsRefPtr<VideoStreamTrack> >& aTracks)
{
  for (uint32_t i = 0; i < mTracks.Length(); ++i) {
    VideoStreamTrack* t = mTracks[i]->AsVideoStreamTrack();
    if (t) {
      aTracks.AppendElement(t);
    }
  }
}

void
DOMMediaStream::GetTracks(nsTArray<nsRefPtr<MediaStreamTrack> >& aTracks)
{
  aTracks.AppendElements(mTracks);
}

bool
DOMMediaStream::HasTrack(const MediaStreamTrack& aTrack) const
{
  return mTracks.Contains(&aTrack);
}

bool
DOMMediaStream::IsFinished()
{
  return !mStream || mStream->IsFinished();
}

void
DOMMediaStream::InitSourceStream(nsIDOMWindow* aWindow)
{
  mWindow = aWindow;
  MediaStreamGraph* gm = MediaStreamGraph::GetInstance();
  InitStreamCommon(gm->CreateSourceStream(this));
}

void
DOMMediaStream::InitTrackUnionStream(nsIDOMWindow* aWindow)
{
  mWindow = aWindow;
  MediaStreamGraph* gm = MediaStreamGraph::GetInstance();
  InitStreamCommon(gm->CreateTrackUnionStream(this));
}

void
DOMMediaStream::InitStreamCommon(MediaStream* aStream)
{
  mStream = aStream;

  // Setup track listener
  mListener = new StreamListener(this);
  aStream->AddListener(mListener);
}

already_AddRefed<DOMMediaStream>
DOMMediaStream::CreateSourceStream(nsIDOMWindow* aWindow)
{
  nsRefPtr<DOMMediaStream> stream = new DOMMediaStream();
  stream->InitSourceStream(aWindow);
  return stream.forget();
}

already_AddRefed<DOMMediaStream>
DOMMediaStream::CreateTrackUnionStream(nsIDOMWindow* aWindow)
{
  nsRefPtr<DOMMediaStream> stream = new DOMMediaStream();
  stream->InitTrackUnionStream(aWindow);
  return stream.forget();
}

void
DOMMediaStream::SetTrackEnabled(TrackID aTrackID, bool aEnabled)
{
  if (mStream) {
    mStream->SetTrackEnabled(aTrackID, aEnabled);
  }
}

void
DOMMediaStream::StopTrack(TrackID aTrackID)
{
  if (mStream && mStream->AsSourceStream()) {
    mStream->AsSourceStream()->EndTrack(aTrackID);
  }
}

bool
DOMMediaStream::CombineWithPrincipal(nsIPrincipal* aPrincipal)
{
  bool changed =
    nsContentUtils::CombineResourcePrincipals(&mPrincipal, aPrincipal);
  if (changed) {
    NotifyPrincipalChanged();
  }
  return changed;
}

void
DOMMediaStream::SetPrincipal(nsIPrincipal* aPrincipal)
{
  mPrincipal = aPrincipal;
  NotifyPrincipalChanged();
}

void
DOMMediaStream::SetCORSMode(CORSMode aCORSMode)
{
  MOZ_ASSERT(NS_IsMainThread());
  mCORSMode = aCORSMode;
}

CORSMode
DOMMediaStream::GetCORSMode()
{
  MOZ_ASSERT(NS_IsMainThread());
  return mCORSMode;
}

void
DOMMediaStream::NotifyPrincipalChanged()
{
  for (uint32_t i = 0; i < mPrincipalChangeObservers.Length(); ++i) {
    mPrincipalChangeObservers[i]->PrincipalChanged(this);
  }
}


bool
DOMMediaStream::AddPrincipalChangeObserver(PrincipalChangeObserver* aObserver)
{
  return mPrincipalChangeObservers.AppendElement(aObserver) != nullptr;
}

bool
DOMMediaStream::RemovePrincipalChangeObserver(PrincipalChangeObserver* aObserver)
{
  return mPrincipalChangeObservers.RemoveElement(aObserver);
}

MediaStreamTrack*
DOMMediaStream::CreateDOMTrack(TrackID aTrackID, MediaSegment::Type aType)
{
  MediaStreamTrack* track;
  switch (aType) {
  case MediaSegment::AUDIO:
    track = new AudioStreamTrack(this, aTrackID);
    break;
  case MediaSegment::VIDEO:
    track = new VideoStreamTrack(this, aTrackID);
    break;
  default:
    MOZ_CRASH("Unhandled track type");
  }
  mTracks.AppendElement(track);

  return track;
}

MediaStreamTrack*
DOMMediaStream::BindDOMTrack(TrackID aTrackID, MediaSegment::Type aType)
{
  MediaStreamTrack* track = nullptr;
  bool bindSuccess = false;
  switch (aType) {
  case MediaSegment::AUDIO: {
    for (size_t i = 0; i < mTracks.Length(); ++i) {
      track = mTracks[i]->AsAudioStreamTrack();
      if (track && track->GetTrackID() == aTrackID) {
        bindSuccess = true;
        break;
      }
    }
    break;
  }
  case MediaSegment::VIDEO: {
    for (size_t i = 0; i < mTracks.Length(); ++i) {
      track = mTracks[i]->AsVideoStreamTrack();
      if (track && track->GetTrackID() == aTrackID) {
        bindSuccess = true;
        break;
      }
    }
    break;
  }
  default:
    MOZ_CRASH("Unhandled track type");
  }
  return bindSuccess ? track : nullptr;
}

MediaStreamTrack*
DOMMediaStream::GetDOMTrackFor(TrackID aTrackID)
{
  for (uint32_t i = 0; i < mTracks.Length(); ++i) {
    MediaStreamTrack* t = mTracks[i];
    // We may add streams to our track list that are actually owned by
    // a different DOMMediaStream. Ignore those.
    if (t->GetTrackID() == aTrackID && t->GetStream() == this) {
      return t;
    }
  }
  return nullptr;
}

void
DOMMediaStream::NotifyMediaStreamGraphShutdown()
{
  // No more tracks will ever be added, so just clear these callbacks now
  // to prevent leaks.
  mNotifiedOfMediaStreamGraphShutdown = true;
  mRunOnTracksAvailable.Clear();

  mConsumersToKeepAlive.Clear();
}

void
DOMMediaStream::NotifyStreamStateChanged()
{
  if (IsFinished()) {
    mConsumersToKeepAlive.Clear();
  }
}

void
DOMMediaStream::OnTracksAvailable(OnTracksAvailableCallback* aRunnable)
{
  if (mNotifiedOfMediaStreamGraphShutdown) {
    // No more tracks will ever be added, so just delete the callback now.
    delete aRunnable;
    return;
  }
  mRunOnTracksAvailable.AppendElement(aRunnable);
  CheckTracksAvailable();
}

void
DOMMediaStream::TracksCreated()
{
  MOZ_ASSERT(!mTracks.IsEmpty());
  mTracksCreated = true;
  CheckTracksAvailable();
}

void
DOMMediaStream::CheckTracksAvailable()
{
  if (!mTracksCreated) {
    return;
  }
  nsTArray<nsAutoPtr<OnTracksAvailableCallback> > callbacks;
  callbacks.SwapElements(mRunOnTracksAvailable);

  for (uint32_t i = 0; i < callbacks.Length(); ++i) {
    callbacks[i]->NotifyTracksAvailable(this);
  }
}

already_AddRefed<AudioTrack>
DOMMediaStream::CreateAudioTrack(AudioStreamTrack* aStreamTrack)
{
  nsAutoString id;
  nsAutoString label;
  aStreamTrack->GetId(id);
  aStreamTrack->GetLabel(label);

  return MediaTrackList::CreateAudioTrack(id, NS_LITERAL_STRING("main"),
                                          label, EmptyString(),
                                          aStreamTrack->Enabled());
}

already_AddRefed<VideoTrack>
DOMMediaStream::CreateVideoTrack(VideoStreamTrack* aStreamTrack)
{
  nsAutoString id;
  nsAutoString label;
  aStreamTrack->GetId(id);
  aStreamTrack->GetLabel(label);

  return MediaTrackList::CreateVideoTrack(id, NS_LITERAL_STRING("main"),
                                          label, EmptyString());
}

void
DOMMediaStream::ConstructMediaTracks(AudioTrackList* aAudioTrackList,
                                     VideoTrackList* aVideoTrackList)
{
  MediaTrackListListener audioListener(aAudioTrackList);
  mMediaTrackListListeners.AppendElement(audioListener);
  MediaTrackListListener videoListener(aVideoTrackList);
  mMediaTrackListListeners.AppendElement(videoListener);

  int firstEnabledVideo = -1;
  for (uint32_t i = 0; i < mTracks.Length(); ++i) {
    if (AudioStreamTrack* t = mTracks[i]->AsAudioStreamTrack()) {
      nsRefPtr<AudioTrack> track = CreateAudioTrack(t);
      aAudioTrackList->AddTrack(track);
    } else if (VideoStreamTrack* t = mTracks[i]->AsVideoStreamTrack()) {
      nsRefPtr<VideoTrack> track = CreateVideoTrack(t);
      aVideoTrackList->AddTrack(track);
      firstEnabledVideo = (t->Enabled() && firstEnabledVideo < 0)
                          ? (aVideoTrackList->Length() - 1)
                          : firstEnabledVideo;
    }
  }

  if (aVideoTrackList->Length() > 0) {
    // If media resource does not indicate a particular set of video tracks to
    // enable, the one that is listed first in the element's videoTracks object
    // must be selected.
    int index = firstEnabledVideo >= 0 ? firstEnabledVideo : 0;
    (*aVideoTrackList)[index]->SetEnabledInternal(true, MediaTrack::FIRE_NO_EVENTS);
  }
}

void
DOMMediaStream::DisconnectTrackListListeners(const AudioTrackList* aAudioTrackList,
                                             const VideoTrackList* aVideoTrackList)
{
  for (auto i = mMediaTrackListListeners.Length(); i > 0; ) { // unsigned!
    --i; // 0 ... Length()-1 range
    if (mMediaTrackListListeners[i].mMediaTrackList == aAudioTrackList ||
        mMediaTrackListListeners[i].mMediaTrackList == aVideoTrackList) {
      mMediaTrackListListeners.RemoveElementAt(i);
    }
  }
}

void
DOMMediaStream::NotifyMediaStreamTrackCreated(MediaStreamTrack* aTrack)
{
  MOZ_ASSERT(aTrack);

  for (uint32_t i = 0; i < mMediaTrackListListeners.Length(); ++i) {
    if (AudioStreamTrack* t = aTrack->AsAudioStreamTrack()) {
      nsRefPtr<AudioTrack> track = CreateAudioTrack(t);
      mMediaTrackListListeners[i].NotifyMediaTrackCreated(track);
    } else if (VideoStreamTrack* t = aTrack->AsVideoStreamTrack()) {
      nsRefPtr<VideoTrack> track = CreateVideoTrack(t);
      mMediaTrackListListeners[i].NotifyMediaTrackCreated(track);
    }
  }
}

void
DOMMediaStream::NotifyMediaStreamTrackEnded(MediaStreamTrack* aTrack)
{
  MOZ_ASSERT(aTrack);

  nsAutoString id;
  aTrack->GetId(id);
  for (uint32_t i = 0; i < mMediaTrackListListeners.Length(); ++i) {
    mMediaTrackListListeners[i].NotifyMediaTrackEnded(id);
  }
}

DOMLocalMediaStream::~DOMLocalMediaStream()
{
  if (mStream) {
    // Make sure Listeners of this stream know it's going away
    Stop();
  }
}

JSObject*
DOMLocalMediaStream::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto)
{
  return dom::LocalMediaStreamBinding::Wrap(aCx, this, aGivenProto);
}

void
DOMLocalMediaStream::Stop()
{
  if (mStream && mStream->AsSourceStream()) {
    mStream->AsSourceStream()->EndAllTrackAndFinish();
  }
}

already_AddRefed<DOMLocalMediaStream>
DOMLocalMediaStream::CreateSourceStream(nsIDOMWindow* aWindow)
{
  nsRefPtr<DOMLocalMediaStream> stream = new DOMLocalMediaStream();
  stream->InitSourceStream(aWindow);
  return stream.forget();
}

already_AddRefed<DOMLocalMediaStream>
DOMLocalMediaStream::CreateTrackUnionStream(nsIDOMWindow* aWindow)
{
  nsRefPtr<DOMLocalMediaStream> stream = new DOMLocalMediaStream();
  stream->InitTrackUnionStream(aWindow);
  return stream.forget();
}

DOMAudioNodeMediaStream::DOMAudioNodeMediaStream(AudioNode* aNode)
: mStreamNode(aNode)
{
}

DOMAudioNodeMediaStream::~DOMAudioNodeMediaStream()
{
}

already_AddRefed<DOMAudioNodeMediaStream>
DOMAudioNodeMediaStream::CreateTrackUnionStream(nsIDOMWindow* aWindow,
                                                AudioNode* aNode)
{
  nsRefPtr<DOMAudioNodeMediaStream> stream = new DOMAudioNodeMediaStream(aNode);
  stream->InitTrackUnionStream(aWindow);
  return stream.forget();
}
