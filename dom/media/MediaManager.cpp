/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MediaManager.h"

#include "MediaStreamGraph.h"
#include "mozilla/dom/MediaStreamTrack.h"
#include "GetUserMediaRequest.h"
#include "nsContentUtils.h"
#include "nsHashPropertyBag.h"
#ifdef MOZ_WIDGET_GONK
#include "nsIAudioManager.h"
#endif
#include "nsIEventTarget.h"
#include "nsIUUIDGenerator.h"
#include "nsIScriptGlobalObject.h"
#include "nsIPermissionManager.h"
#include "nsIPopupWindowManager.h"
#include "nsISupportsArray.h"
#include "nsIDocShell.h"
#include "nsIDocument.h"
#include "nsISupportsPrimitives.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIIDNService.h"
#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsPrincipal.h"
#include "nsICryptoHash.h"
#include "nsICryptoHMAC.h"
#include "nsIKeyModule.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsIInputStream.h"
#include "nsILineInputStream.h"
#include "mozilla/Telemetry.h"
#include "mozilla/Types.h"
#include "mozilla/PeerIdentity.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/File.h"
#include "mozilla/dom/MediaStreamBinding.h"
#include "mozilla/dom/MediaStreamTrackBinding.h"
#include "mozilla/dom/GetUserMediaRequestBinding.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/Base64.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/media/MediaChild.h"
#include "MediaTrackConstraints.h"
#include "VideoUtils.h"
#include "Latency.h"
#include "nsProxyRelease.h"
#include "nsNullPrincipal.h"
#include "nsVariant.h"

// For snprintf
#include "mozilla/Snprintf.h"

#include "nsJSUtils.h"
#include "nsGlobalWindow.h"
#include "nsIUUIDGenerator.h"
#include "nspr.h"
#include "nss.h"
#include "pk11pub.h"

/* Using WebRTC backend on Desktops (Mac, Windows, Linux), otherwise default */
#include "MediaEngineDefault.h"
#if defined(MOZ_WEBRTC)
#include "MediaEngineWebRTC.h"
#include "browser_logging/WebRtcLog.h"
#endif

#ifdef MOZ_B2G
#include "MediaPermissionGonk.h"
#endif

#if defined(XP_MACOSX)
#include "nsCocoaFeatures.h"
#endif
#if defined (XP_WIN)
#include "mozilla/WindowsVersion.h"
#include <winsock2.h>
#include <iphlpapi.h>
#include <tchar.h>
#endif

// GetCurrentTime is defined in winbase.h as zero argument macro forwarding to
// GetTickCount() and conflicts with MediaStream::GetCurrentTime.
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

// XXX Workaround for bug 986974 to maintain the existing broken semantics
template<>
struct nsIMediaDevice::COMTypeInfo<mozilla::VideoDevice, void> {
  static const nsIID kIID;
};
const nsIID nsIMediaDevice::COMTypeInfo<mozilla::VideoDevice, void>::kIID = NS_IMEDIADEVICE_IID;
template<>
struct nsIMediaDevice::COMTypeInfo<mozilla::AudioDevice, void> {
  static const nsIID kIID;
};
const nsIID nsIMediaDevice::COMTypeInfo<mozilla::AudioDevice, void>::kIID = NS_IMEDIADEVICE_IID;

namespace {
already_AddRefed<nsIAsyncShutdownClient> GetShutdownPhase() {
  nsCOMPtr<nsIAsyncShutdownService> svc = mozilla::services::GetAsyncShutdown();
  MOZ_RELEASE_ASSERT(svc);

  nsCOMPtr<nsIAsyncShutdownClient> shutdownPhase;
  nsresult rv = svc->GetProfileBeforeChange(getter_AddRefs(shutdownPhase));
  if (!shutdownPhase) {
    // We are probably in a content process.
    rv = svc->GetContentChildShutdown(getter_AddRefs(shutdownPhase));
  }
  MOZ_RELEASE_ASSERT(shutdownPhase);
  MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv));
  return shutdownPhase.forget();
}
}

namespace mozilla {

#ifdef LOG
#undef LOG
#endif

LogModule*
GetMediaManagerLog()
{
  static LazyLogModule sLog("MediaManager");
  return sLog;
}
#define LOG(msg) MOZ_LOG(GetMediaManagerLog(), mozilla::LogLevel::Debug, msg)

using dom::BasicUnstoppableTrackSource;
using dom::ConstrainDOMStringParameters;
using dom::File;
using dom::GetUserMediaRequest;
using dom::MediaSourceEnum;
using dom::MediaStreamConstraints;
using dom::MediaStreamError;
using dom::MediaStreamTrack;
using dom::MediaStreamTrackSource;
using dom::MediaTrackConstraints;
using dom::MediaTrackConstraintSet;
using dom::OwningBooleanOrMediaTrackConstraints;
using dom::OwningStringOrStringSequence;
using dom::OwningStringOrStringSequenceOrConstrainDOMStringParameters;
using dom::Promise;
using dom::Sequence;
using media::NewRunnableFrom;
using media::NewTaskFrom;
using media::Pledge;
using media::Refcountable;

static Atomic<bool> sInShutdown;

static bool
HostInDomain(const nsCString &aHost, const nsCString &aPattern)
{
  int32_t patternOffset = 0;
  int32_t hostOffset = 0;

  // Act on '*.' wildcard in the left-most position in a domain pattern.
  if (aPattern.Length() > 2 && aPattern[0] == '*' && aPattern[1] == '.') {
    patternOffset = 2;

    // Ignore the lowest level sub-domain for the hostname.
    hostOffset = aHost.FindChar('.') + 1;

    if (hostOffset <= 1) {
      // Reject a match between a wildcard and a TLD or '.foo' form.
      return false;
    }
  }

  nsDependentCString hostRoot(aHost, hostOffset);
  return hostRoot.EqualsIgnoreCase(aPattern.BeginReading() + patternOffset);
}

static bool
HostIsHttps(nsIURI &docURI)
{
  bool isHttps;
  nsresult rv = docURI.SchemeIs("https", &isHttps);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }
  return isHttps;
}

static bool
HostHasPermission(nsIURI &docURI)
{
  nsAdoptingCString hostName;
  docURI.GetAsciiHost(hostName); //normalize UTF8 to ASCII equivalent
  nsAdoptingCString domainWhiteList =
    Preferences::GetCString("media.getusermedia.screensharing.allowed_domains");
  domainWhiteList.StripWhitespace();

  if (domainWhiteList.IsEmpty() || hostName.IsEmpty()) {
    return false;
  }

  // Get UTF8 to ASCII domain name normalization service
  nsresult rv;
  nsCOMPtr<nsIIDNService> idnService
    = do_GetService("@mozilla.org/network/idn-service;1", &rv);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }

  uint32_t begin = 0;
  uint32_t end = 0;
  nsCString domainName;
  /*
     Test each domain name in the comma separated list
     after converting from UTF8 to ASCII. Each domain
     must match exactly or have a single leading '*.' wildcard
  */
  do {
    end = domainWhiteList.FindChar(',', begin);
    if (end == (uint32_t)-1) {
      // Last or only domain name in the comma separated list
      end = domainWhiteList.Length();
    }

    rv = idnService->ConvertUTF8toACE(Substring(domainWhiteList, begin, end - begin),
                                      domainName);
    if (NS_SUCCEEDED(rv)) {
      if (HostInDomain(hostName, domainName)) {
        return true;
      }
    } else {
      NS_WARNING("Failed to convert UTF-8 host to ASCII");
    }

    begin = end + 1;
  } while (end < domainWhiteList.Length());

  return false;
}

// Generic class for running long media operations like Start off the main
// thread, and then (because nsDOMMediaStreams aren't threadsafe),
// ProxyReleases mStream since it's cycle collected.
class MediaOperationTask : public Runnable
{
public:
  // so we can send Stop without AddRef()ing from the MSG thread
  MediaOperationTask(MediaOperation aType,
    GetUserMediaCallbackMediaStreamListener* aListener,
    DOMMediaStream* aStream,
    OnTracksAvailableCallback* aOnTracksAvailableCallback,
    AudioDevice* aAudioDevice,
    VideoDevice* aVideoDevice,
    bool aBool,
    uint64_t aWindowID,
    already_AddRefed<nsIDOMGetUserMediaErrorCallback> aError,
    const dom::MediaTrackConstraints& aConstraints = dom::MediaTrackConstraints())
    : mType(aType)
    , mStream(aStream)
    , mOnTracksAvailableCallback(aOnTracksAvailableCallback)
    , mAudioDevice(aAudioDevice)
    , mVideoDevice(aVideoDevice)
    , mListener(aListener)
    , mBool(aBool)
    , mWindowID(aWindowID)
    , mOnFailure(aError)
    , mConstraints(aConstraints)
  {}

  ~MediaOperationTask()
  {
    // MediaStreams can be released on any thread.
  }

  void
  ReturnCallbackError(nsresult rv, const char* errorLog);

  NS_IMETHOD
  Run() override
  {
    SourceMediaStream *source = mListener->GetSourceStream();
    // No locking between these is required as all the callbacks for the
    // same MediaStream will occur on the same thread.
    if (!source) // means the stream was never Activated()
      return NS_OK;

    switch (mType) {
      case MEDIA_START:
        {
          NS_ASSERTION(!NS_IsMainThread(), "Never call on main thread");
          nsresult rv;

          if (mAudioDevice) {
            rv = mAudioDevice->GetSource()->Start(source, kAudioTrack,
                                                  mListener->GetPrincipalHandle());
            if (NS_FAILED(rv)) {
              ReturnCallbackError(rv, "Starting audio failed");
              return NS_OK;
            }
          }
          if (mVideoDevice) {
            rv = mVideoDevice->GetSource()->Start(source, kVideoTrack,
                                                  mListener->GetPrincipalHandle());
            if (NS_FAILED(rv)) {
              ReturnCallbackError(rv, "Starting video failed");
              return NS_OK;
            }
          }
          // Start() queued the tracks to be added synchronously to avoid races
          source->FinishAddTracks();

          source->SetPullEnabled(true);
          source->AdvanceKnownTracksTime(STREAM_TIME_MAX);

          MM_LOG(("started all sources"));
          // Forward mOnTracksAvailableCallback to GetUserMediaNotificationEvent,
          // because mOnTracksAvailableCallback needs to be added to mStream
          // on the main thread.
          nsIRunnable *event =
            new GetUserMediaNotificationEvent(GetUserMediaNotificationEvent::STARTING,
                                              mStream.forget(),
                                              mOnTracksAvailableCallback.forget(),
                                              mAudioDevice != nullptr,
                                              mVideoDevice != nullptr,
                                              mWindowID, mOnFailure.forget());
          // event must always be released on mainthread due to the JS callbacks
          // in the TracksAvailableCallback
          NS_DispatchToMainThread(event);
        }
        break;

      case MEDIA_STOP:
      case MEDIA_STOP_TRACK:
        {
          NS_ASSERTION(!NS_IsMainThread(), "Never call on main thread");
          if (mAudioDevice) {
            mAudioDevice->GetSource()->Stop(source, kAudioTrack);
            mAudioDevice->GetSource()->Deallocate();
          }
          if (mVideoDevice) {
            mVideoDevice->GetSource()->Stop(source, kVideoTrack);
            mVideoDevice->GetSource()->Deallocate();
          }
          if (mType == MEDIA_STOP) {
            source->EndAllTrackAndFinish();
          }

          nsIRunnable *event =
            new GetUserMediaNotificationEvent(mListener,
                                              mType == MEDIA_STOP ?
                                              GetUserMediaNotificationEvent::STOPPING :
                                              GetUserMediaNotificationEvent::STOPPED_TRACK,
                                              mAudioDevice != nullptr,
                                              mVideoDevice != nullptr,
                                              mWindowID);
          // event must always be released on mainthread due to the JS callbacks
          // in the TracksAvailableCallback
          NS_DispatchToMainThread(event);
        }
        break;

      case MEDIA_DIRECT_LISTENERS:
        {
          NS_ASSERTION(!NS_IsMainThread(), "Never call on main thread");
          if (mVideoDevice) {
            mVideoDevice->GetSource()->SetDirectListeners(mBool);
          }
        }
        break;

      default:
        MOZ_ASSERT(false,"invalid MediaManager operation");
        break;
    }

    return NS_OK;
  }

private:
  MediaOperation mType;
  RefPtr<DOMMediaStream> mStream;
  nsAutoPtr<OnTracksAvailableCallback> mOnTracksAvailableCallback;
  RefPtr<AudioDevice> mAudioDevice; // threadsafe
  RefPtr<VideoDevice> mVideoDevice; // threadsafe
  RefPtr<GetUserMediaCallbackMediaStreamListener> mListener; // threadsafe
  bool mBool;
  uint64_t mWindowID;
  nsCOMPtr<nsIDOMGetUserMediaErrorCallback> mOnFailure;
  dom::MediaTrackConstraints mConstraints;
};

/**
 * Send an error back to content.
 * Do this only on the main thread. The onSuccess callback is also passed here
 * so it can be released correctly.
 */
template<class SuccessCallbackType>
class ErrorCallbackRunnable : public Runnable
{
public:
  ErrorCallbackRunnable(
    nsCOMPtr<SuccessCallbackType>& aOnSuccess,
    nsCOMPtr<nsIDOMGetUserMediaErrorCallback>& aOnFailure,
    MediaMgrError& aError,
    uint64_t aWindowID)
    : mError(&aError)
    , mWindowID(aWindowID)
    , mManager(MediaManager::GetInstance())
  {
    mOnSuccess.swap(aOnSuccess);
    mOnFailure.swap(aOnFailure);
  }

  NS_IMETHODIMP
  Run()
  {
    MOZ_ASSERT(NS_IsMainThread());

    nsCOMPtr<SuccessCallbackType> onSuccess = mOnSuccess.forget();
    nsCOMPtr<nsIDOMGetUserMediaErrorCallback> onFailure = mOnFailure.forget();

    // Only run if the window is still active.
    if (!(mManager->IsWindowStillActive(mWindowID))) {
      return NS_OK;
    }
    // This is safe since we're on main-thread, and the windowlist can only
    // be invalidated from the main-thread (see OnNavigation)
    nsGlobalWindow* window = nsGlobalWindow::GetInnerWindowWithId(mWindowID);
    if (window) {
      RefPtr<MediaStreamError> error = new MediaStreamError(window, *mError);
      onFailure->OnError(error);
    }
    return NS_OK;
  }
private:
  ~ErrorCallbackRunnable()
  {
    MOZ_ASSERT(!mOnSuccess && !mOnFailure);
  }

  nsCOMPtr<SuccessCallbackType> mOnSuccess;
  nsCOMPtr<nsIDOMGetUserMediaErrorCallback> mOnFailure;
  RefPtr<MediaMgrError> mError;
  uint64_t mWindowID;
  RefPtr<MediaManager> mManager; // get ref to this when creating the runnable
};

// Handle removing GetUserMediaCallbackMediaStreamListener from main thread
class GetUserMediaListenerRemove: public Runnable
{
public:
  GetUserMediaListenerRemove(uint64_t aWindowID,
    GetUserMediaCallbackMediaStreamListener *aListener)
    : mWindowID(aWindowID)
    , mListener(aListener) {}

  NS_IMETHOD
  Run()
  {
    MOZ_ASSERT(NS_IsMainThread());
    RefPtr<MediaManager> manager(MediaManager::GetInstance());
    manager->RemoveFromWindowList(mWindowID, mListener);
    return NS_OK;
  }

protected:
  uint64_t mWindowID;
  RefPtr<GetUserMediaCallbackMediaStreamListener> mListener;
};

/**
 * nsIMediaDevice implementation.
 */
NS_IMPL_ISUPPORTS(MediaDevice, nsIMediaDevice)

MediaDevice::MediaDevice(MediaEngineSource* aSource, bool aIsVideo)
  : mMediaSource(aSource->GetMediaSource())
  , mSource(aSource)
  , mIsVideo(aIsVideo)
{
  mSource->GetName(mName);
  nsCString id;
  mSource->GetUUID(id);
  CopyUTF8toUTF16(id, mID);
}

VideoDevice::VideoDevice(MediaEngineVideoSource* aSource)
  : MediaDevice(aSource, true)
{}

/**
 * Helper functions that implement the constraints algorithm from
 * http://dev.w3.org/2011/webrtc/editor/getusermedia.html#methods-5
 */

bool
MediaDevice::StringsContain(const OwningStringOrStringSequence& aStrings,
                            nsString aN)
{
  return aStrings.IsString() ? aStrings.GetAsString() == aN
                             : aStrings.GetAsStringSequence().Contains(aN);
}

/* static */ uint32_t
MediaDevice::FitnessDistance(nsString aN,
                             const ConstrainDOMStringParameters& aParams)
{
  if (aParams.mExact.WasPassed() && !StringsContain(aParams.mExact.Value(), aN)) {
    return UINT32_MAX;
  }
  if (aParams.mIdeal.WasPassed() && !StringsContain(aParams.mIdeal.Value(), aN)) {
    return 1;
  }
  return 0;
}

// Binding code doesn't templatize well...

/* static */ uint32_t
MediaDevice::FitnessDistance(nsString aN,
    const OwningStringOrStringSequenceOrConstrainDOMStringParameters& aConstraint)
{
  if (aConstraint.IsString()) {
    ConstrainDOMStringParameters params;
    params.mIdeal.Construct();
    params.mIdeal.Value().SetAsString() = aConstraint.GetAsString();
    return FitnessDistance(aN, params);
  } else if (aConstraint.IsStringSequence()) {
    ConstrainDOMStringParameters params;
    params.mIdeal.Construct();
    params.mIdeal.Value().SetAsStringSequence() = aConstraint.GetAsStringSequence();
    return FitnessDistance(aN, params);
  } else {
    return FitnessDistance(aN, aConstraint.GetAsConstrainDOMStringParameters());
  }
}

// Reminder: add handling for new constraints both here and in GetSources below!

uint32_t
MediaDevice::GetBestFitnessDistance(
    const nsTArray<const MediaTrackConstraintSet*>& aConstraintSets)
{
  nsString mediaSource;
  GetMediaSource(mediaSource);

  // This code is reused for audio, where the mediaSource constraint does
  // not currently have a function, but because it defaults to "camera" in
  // webidl, we ignore it for audio here.
  if (!mediaSource.EqualsASCII("microphone")) {
    for (const auto& constraint : aConstraintSets) {
      if (mediaSource != constraint->mMediaSource) {
        return UINT32_MAX;
      }
    }
  }
  // Forward request to underlying object to interrogate per-mode capabilities.
  // Pass in device's origin-specific id for deviceId constraint comparison.
  nsString id;
  GetId(id);
  return mSource->GetBestFitnessDistance(aConstraintSets, id);
}

AudioDevice::AudioDevice(MediaEngineAudioSource* aSource)
  : MediaDevice(aSource, false)
{
  mMediaSource = aSource->GetMediaSource();
}

NS_IMETHODIMP
MediaDevice::GetName(nsAString& aName)
{
  aName.Assign(mName);
  return NS_OK;
}

NS_IMETHODIMP
MediaDevice::GetType(nsAString& aType)
{
  return NS_OK;
}

NS_IMETHODIMP
VideoDevice::GetType(nsAString& aType)
{
  aType.AssignLiteral(MOZ_UTF16("video"));
  return NS_OK;
}

NS_IMETHODIMP
AudioDevice::GetType(nsAString& aType)
{
  aType.AssignLiteral(MOZ_UTF16("audio"));
  return NS_OK;
}

NS_IMETHODIMP
MediaDevice::GetId(nsAString& aID)
{
  aID.Assign(mID);
  return NS_OK;
}

void
MediaDevice::SetId(const nsAString& aID)
{
  mID.Assign(aID);
}

NS_IMETHODIMP
MediaDevice::GetMediaSource(nsAString& aMediaSource)
{
  if (mMediaSource == MediaSourceEnum::Microphone) {
    aMediaSource.Assign(NS_LITERAL_STRING("microphone"));
  } else if (mMediaSource == MediaSourceEnum::AudioCapture) {
    aMediaSource.Assign(NS_LITERAL_STRING("audioCapture"));
  } else if (mMediaSource == MediaSourceEnum::Window) { // this will go away
    aMediaSource.Assign(NS_LITERAL_STRING("window"));
  } else { // all the rest are shared
    aMediaSource.Assign(NS_ConvertUTF8toUTF16(
      dom::MediaSourceEnumValues::strings[uint32_t(mMediaSource)].value));
  }
  return NS_OK;
}

VideoDevice::Source*
VideoDevice::GetSource()
{
  return static_cast<Source*>(&*mSource);
}

AudioDevice::Source*
AudioDevice::GetSource()
{
  return static_cast<Source*>(&*mSource);
}

nsresult VideoDevice::Allocate(const dom::MediaTrackConstraints &aConstraints,
                               const MediaEnginePrefs &aPrefs,
                               const nsACString& aOrigin) {
  return GetSource()->Allocate(aConstraints, aPrefs, mID, aOrigin);
}

nsresult AudioDevice::Allocate(const dom::MediaTrackConstraints &aConstraints,
                               const MediaEnginePrefs &aPrefs,
                               const nsACString& aOrigin) {
  return GetSource()->Allocate(aConstraints, aPrefs, mID, aOrigin);
}

nsresult VideoDevice::Restart(const dom::MediaTrackConstraints &aConstraints,
                              const MediaEnginePrefs &aPrefs) {
  return GetSource()->Restart(aConstraints, aPrefs, mID);
}

nsresult AudioDevice::Restart(const dom::MediaTrackConstraints &aConstraints,
                              const MediaEnginePrefs &aPrefs) {
  return GetSource()->Restart(aConstraints, aPrefs, mID);
}

void
MediaOperationTask::ReturnCallbackError(nsresult rv, const char* errorLog)
{
  MM_LOG(("%s , rv=%d", errorLog, rv));
  NS_DispatchToMainThread(do_AddRef(new ReleaseMediaOperationResource(mStream.forget(),
    mOnTracksAvailableCallback.forget())));
  nsString log;

  log.AssignASCII(errorLog);
  nsCOMPtr<nsIDOMGetUserMediaSuccessCallback> onSuccess;
  RefPtr<MediaMgrError> error = new MediaMgrError(
    NS_LITERAL_STRING("InternalError"), log);
  NS_DispatchToMainThread(do_AddRef(
    new ErrorCallbackRunnable<nsIDOMGetUserMediaSuccessCallback>(onSuccess,
                                                                 mOnFailure,
                                                                 *error,
                                                                 mWindowID)));
}

/**
 * This class is only needed since fake tracks are added dynamically.
 * Instead of refactoring to add them explicitly we let the DOMMediaStream
 * query us for the source as they become available.
 * Since they are used only for testing the API surface, we make them very
 * simple.
 */
class FakeTrackSourceGetter : public MediaStreamTrackSourceGetter
{
public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(FakeTrackSourceGetter,
                                           MediaStreamTrackSourceGetter)

  explicit FakeTrackSourceGetter(nsIPrincipal* aPrincipal)
    : mPrincipal(aPrincipal) {}

  already_AddRefed<dom::MediaStreamTrackSource>
  GetMediaStreamTrackSource(TrackID aInputTrackID) override
  {
    NS_ASSERTION(kAudioTrack != aInputTrackID,
                 "Only fake tracks should appear dynamically");
    NS_ASSERTION(kVideoTrack != aInputTrackID,
                 "Only fake tracks should appear dynamically");
    return do_AddRef(new BasicUnstoppableTrackSource(mPrincipal));
  }

protected:
  virtual ~FakeTrackSourceGetter() {}

  nsCOMPtr<nsIPrincipal> mPrincipal;
};

NS_IMPL_ADDREF_INHERITED(FakeTrackSourceGetter, MediaStreamTrackSourceGetter)
NS_IMPL_RELEASE_INHERITED(FakeTrackSourceGetter, MediaStreamTrackSourceGetter)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(FakeTrackSourceGetter)
NS_INTERFACE_MAP_END_INHERITING(MediaStreamTrackSourceGetter)
NS_IMPL_CYCLE_COLLECTION_INHERITED(FakeTrackSourceGetter,
                                   MediaStreamTrackSourceGetter,
                                   mPrincipal)

/**
 * Creates a MediaStream, attaches a listener and fires off a success callback
 * to the DOM with the stream. We also pass in the error callback so it can
 * be released correctly.
 *
 * All of this must be done on the main thread!
 *
 * Note that the various GetUserMedia Runnable classes currently allow for
 * two streams.  If we ever need to support getting more than two streams
 * at once, we could convert everything to nsTArray<RefPtr<blah> >'s,
 * though that would complicate the constructors some.  Currently the
 * GetUserMedia spec does not allow for more than 2 streams to be obtained in
 * one call, to simplify handling of constraints.
 */
class GetUserMediaStreamRunnable : public Runnable
{
public:
  GetUserMediaStreamRunnable(
    nsCOMPtr<nsIDOMGetUserMediaSuccessCallback>& aOnSuccess,
    nsCOMPtr<nsIDOMGetUserMediaErrorCallback>& aOnFailure,
    uint64_t aWindowID,
    GetUserMediaCallbackMediaStreamListener* aListener,
    const nsCString& aOrigin,
    AudioDevice* aAudioDevice,
    VideoDevice* aVideoDevice,
    PeerIdentity* aPeerIdentity)
    : mAudioDevice(aAudioDevice)
    , mVideoDevice(aVideoDevice)
    , mWindowID(aWindowID)
    , mListener(aListener)
    , mOrigin(aOrigin)
    , mPeerIdentity(aPeerIdentity)
    , mManager(MediaManager::GetInstance())
  {
    mOnSuccess.swap(aOnSuccess);
    mOnFailure.swap(aOnFailure);
  }

  ~GetUserMediaStreamRunnable() {}

  class TracksAvailableCallback : public OnTracksAvailableCallback
  {
  public:
    TracksAvailableCallback(MediaManager* aManager,
                            nsIDOMGetUserMediaSuccessCallback* aSuccess,
                            uint64_t aWindowID,
                            DOMMediaStream* aStream)
      : mWindowID(aWindowID), mOnSuccess(aSuccess), mManager(aManager),
        mStream(aStream) {}
    void NotifyTracksAvailable(DOMMediaStream* aStream) override
    {
      // We're in the main thread, so no worries here.
      if (!(mManager->IsWindowStillActive(mWindowID))) {
        return;
      }

      // Start currentTime from the point where this stream was successfully
      // returned.
      aStream->SetLogicalStreamStartTime(aStream->GetPlaybackStream()->GetCurrentTime());

      // This is safe since we're on main-thread, and the windowlist can only
      // be invalidated from the main-thread (see OnNavigation)
      LOG(("Returning success for getUserMedia()"));
      mOnSuccess->OnSuccess(aStream);
    }
    uint64_t mWindowID;
    nsCOMPtr<nsIDOMGetUserMediaSuccessCallback> mOnSuccess;
    RefPtr<MediaManager> mManager;
    // Keep the DOMMediaStream alive until the NotifyTracksAvailable callback
    // has fired, otherwise we might immediately destroy the DOMMediaStream and
    // shut down the underlying MediaStream prematurely.
    // This creates a cycle which is broken when NotifyTracksAvailable
    // is fired (which will happen unless the browser shuts down,
    // since we only add this callback when we've successfully appended
    // the desired tracks in the MediaStreamGraph) or when
    // DOMMediaStream::NotifyMediaStreamGraphShutdown is called.
    RefPtr<DOMMediaStream> mStream;
  };

  NS_IMETHOD
  Run()
  {
    MOZ_ASSERT(NS_IsMainThread());
    nsPIDOMWindow *window = static_cast<nsPIDOMWindow*>
      (nsGlobalWindow::GetInnerWindowWithId(mWindowID));

    // We're on main-thread, and the windowlist can only
    // be invalidated from the main-thread (see OnNavigation)
    StreamListeners* listeners = mManager->GetWindowListeners(mWindowID);
    if (!listeners || !window || !window->GetExtantDoc()) {
      // This window is no longer live.  mListener has already been removed
      return NS_OK;
    }

    MediaStreamGraph::GraphDriverType graphDriverType =
      mAudioDevice ? MediaStreamGraph::AUDIO_THREAD_DRIVER
                   : MediaStreamGraph::SYSTEM_THREAD_DRIVER;
    MediaStreamGraph* msg =
      MediaStreamGraph::GetInstance(graphDriverType,
                                    dom::AudioChannel::Normal);

    RefPtr<DOMMediaStream> domStream;
    RefPtr<SourceMediaStream> stream;
    // AudioCapture is a special case, here, in the sense that we're not really
    // using the audio source and the SourceMediaStream, which acts as
    // placeholders. We re-route a number of stream internaly in the MSG and mix
    // them down instead.
    if (mAudioDevice &&
        mAudioDevice->GetMediaSource() == MediaSourceEnum::AudioCapture) {
      // It should be possible to pipe the capture stream to anything. CORS is
      // not a problem here, we got explicit user content.
      nsCOMPtr<nsIPrincipal> principal = window->GetExtantDoc()->NodePrincipal();
      domStream =
        DOMMediaStream::CreateAudioCaptureStreamAsInput(window, principal, msg);

      stream = msg->CreateSourceStream(nullptr); // Placeholder
      msg->RegisterCaptureStreamForWindow(
            mWindowID, domStream->GetInputStream()->AsProcessedStream());
      window->SetAudioCapture(true);
    } else {
      class LocalTrackSource : public MediaStreamTrackSource
      {
      public:
        LocalTrackSource(nsIPrincipal* aPrincipal,
                         const nsString& aLabel,
                         GetUserMediaCallbackMediaStreamListener* aListener,
                         const MediaSourceEnum aSource,
                         const TrackID aTrackID,
                         const PeerIdentity* aPeerIdentity)
          : MediaStreamTrackSource(aPrincipal, false, aLabel), mListener(aListener),
            mSource(aSource), mTrackID(aTrackID), mPeerIdentity(aPeerIdentity) {}

        MediaSourceEnum GetMediaSource() const override
        {
          return mSource;
        }

        const PeerIdentity* GetPeerIdentity() const override
        {
          return mPeerIdentity;
        }

        already_AddRefed<Promise>
        ApplyConstraints(nsPIDOMWindow* aWindow,
                         const MediaTrackConstraints& aConstraints,
                         ErrorResult &aRv) override
        {
          nsCOMPtr<nsIGlobalObject> go = do_QueryInterface(aWindow);
          RefPtr<Promise> promise = Promise::Create(go, aRv);

          if (sInShutdown) {
            RefPtr<MediaStreamError> error = new MediaStreamError(aWindow,
                NS_LITERAL_STRING("AbortError"),
                NS_LITERAL_STRING("In shutdown"));
            promise->MaybeReject(error);
            return promise.forget();
          }

          typedef media::Pledge<bool, MediaStreamError*> PledgeVoid;

          RefPtr<PledgeVoid> p =
            mListener->ApplyConstraintsToTrack(aWindow, mTrackID, aConstraints);
          p->Then([promise](bool& aDummy) mutable {
            promise->MaybeResolve(false);
          }, [promise](MediaStreamError*& reason) mutable {
            promise->MaybeReject(reason);
          });
          return promise.forget();
        }


        void Stop() override
        {
          if (mListener) {
            mListener->StopTrack(mTrackID);
            mListener = nullptr;
          }
        }

      protected:
        ~LocalTrackSource() {}

        RefPtr<GetUserMediaCallbackMediaStreamListener> mListener;
        const MediaSourceEnum mSource;
        const TrackID mTrackID;
        const RefPtr<const PeerIdentity> mPeerIdentity;
      };

      nsCOMPtr<nsIPrincipal> principal;
      if (mPeerIdentity) {
        principal = nsNullPrincipal::Create();
      } else {
        principal = window->GetExtantDoc()->NodePrincipal();
      }

      // Normal case, connect the source stream to the track union stream to
      // avoid us blocking. Pass a simple TrackSourceGetter for potential
      // fake tracks. Apart from them gUM never adds tracks dynamically.
      domStream =
        DOMLocalMediaStream::CreateSourceStreamAsInput(window, msg,
                                                       new FakeTrackSourceGetter(principal));

      if (mAudioDevice) {
        nsString audioDeviceName;
        mAudioDevice->GetName(audioDeviceName);
        const MediaSourceEnum source =
          mAudioDevice->GetSource()->GetMediaSource();
        RefPtr<MediaStreamTrackSource> audioSource =
          new LocalTrackSource(principal, audioDeviceName, mListener, source,
                               kAudioTrack, mPeerIdentity);
        domStream->CreateDOMTrack(kAudioTrack, MediaSegment::AUDIO, audioSource);
      }
      if (mVideoDevice) {
        nsString videoDeviceName;
        mVideoDevice->GetName(videoDeviceName);
        const MediaSourceEnum source =
          mVideoDevice->GetSource()->GetMediaSource();
        RefPtr<MediaStreamTrackSource> videoSource =
          new LocalTrackSource(principal, videoDeviceName, mListener, source,
                               kVideoTrack, mPeerIdentity);
        domStream->CreateDOMTrack(kVideoTrack, MediaSegment::VIDEO, videoSource);
      }
      stream = domStream->GetInputStream()->AsSourceStream();
    }

    if (!domStream || sInShutdown) {
      nsCOMPtr<nsIDOMGetUserMediaErrorCallback> onFailure = mOnFailure.forget();
      LOG(("Returning error for getUserMedia() - no stream"));

      nsGlobalWindow* window = nsGlobalWindow::GetInnerWindowWithId(mWindowID);
      if (window) {
        RefPtr<MediaStreamError> error = new MediaStreamError(window,
            NS_LITERAL_STRING("InternalError"),
            sInShutdown ? NS_LITERAL_STRING("In shutdown") :
                          NS_LITERAL_STRING("No stream."));
        onFailure->OnError(error);
      }
      return NS_OK;
    }

    // The listener was added at the beginning in an inactive state.
    // Activate our listener. We'll call Start() on the source when get a callback
    // that the MediaStream has started consuming. The listener is freed
    // when the page is invalidated (on navigation or close).
    MOZ_ASSERT(stream);
    mListener->Activate(stream.forget(), mAudioDevice, mVideoDevice);

    // Note: includes JS callbacks; must be released on MainThread
    TracksAvailableCallback* tracksAvailableCallback =
      new TracksAvailableCallback(mManager, mOnSuccess, mWindowID, domStream);

    // Dispatch to the media thread to ask it to start the sources,
    // because that can take a while.
    // Pass ownership of domStream to the MediaOperationTask
    // to ensure it's kept alive until the MediaOperationTask runs (at least).
    RefPtr<Runnable> mediaOperation =
        new MediaOperationTask(MEDIA_START, mListener, domStream,
                               tracksAvailableCallback,
                               mAudioDevice, mVideoDevice,
                               false, mWindowID, mOnFailure.forget());
    MediaManager::PostTask(mediaOperation.forget());
    // We won't need mOnFailure now.
    mOnFailure = nullptr;

    if (!MediaManager::IsPrivateBrowsing(window)) {
      // Call GetOriginKey again, this time w/persist = true, to promote
      // deviceIds to persistent, in case they're not already. Fire'n'forget.
      RefPtr<Pledge<nsCString>> p = media::GetOriginKey(mOrigin, false, true);
    }
    return NS_OK;
  }

private:
  nsCOMPtr<nsIDOMGetUserMediaSuccessCallback> mOnSuccess;
  nsCOMPtr<nsIDOMGetUserMediaErrorCallback> mOnFailure;
  RefPtr<AudioDevice> mAudioDevice;
  RefPtr<VideoDevice> mVideoDevice;
  uint64_t mWindowID;
  RefPtr<GetUserMediaCallbackMediaStreamListener> mListener;
  nsCString mOrigin;
  RefPtr<PeerIdentity> mPeerIdentity;
  RefPtr<MediaManager> mManager; // get ref to this when creating the runnable
};

static bool
IsOn(const OwningBooleanOrMediaTrackConstraints &aUnion) {
  return !aUnion.IsBoolean() || aUnion.GetAsBoolean();
}

static const MediaTrackConstraints&
GetInvariant(const OwningBooleanOrMediaTrackConstraints &aUnion) {
  static const MediaTrackConstraints empty;
  return aUnion.IsMediaTrackConstraints() ?
      aUnion.GetAsMediaTrackConstraints() : empty;
}

// Source getter returning full list

template<class DeviceType>
static void
GetSources(MediaEngine *engine, MediaSourceEnum aSrcType,
           void (MediaEngine::* aEnumerate)(MediaSourceEnum,
               nsTArray<RefPtr<typename DeviceType::Source> >*),
           nsTArray<RefPtr<DeviceType>>& aResult,
           const char* media_device_name = nullptr)
{
  nsTArray<RefPtr<typename DeviceType::Source>> sources;

  (engine->*aEnumerate)(aSrcType, &sources);
  /**
    * We're allowing multiple tabs to access the same camera for parity
    * with Chrome.  See bug 811757 for some of the issues surrounding
    * this decision.  To disallow, we'd filter by IsAvailable() as we used
    * to.
    */
  if (media_device_name && *media_device_name)  {
    for (auto& source : sources) {
      nsString deviceName;
      source->GetName(deviceName);
      if (deviceName.EqualsASCII(media_device_name)) {
        aResult.AppendElement(new DeviceType(source));
        break;
      }
    }
  } else {
    for (auto& source : sources) {
      aResult.AppendElement(new DeviceType(source));
    }
  }
}

// TODO: Remove once upgraded to GCC 4.8+ on linux. Bogus error on static func:
// error: 'this' was not captured for this lambda function

static auto& MediaManager_GetInstance = MediaManager::GetInstance;
static auto& MediaManager_ToJSArray = MediaManager::ToJSArray;
static auto& MediaManager_AnonymizeDevices = MediaManager::AnonymizeDevices;

already_AddRefed<MediaManager::PledgeChar>
MediaManager::SelectSettings(
    MediaStreamConstraints& aConstraints,
    RefPtr<Refcountable<UniquePtr<SourceSet>>>& aSources)
{
  MOZ_ASSERT(NS_IsMainThread());
  RefPtr<PledgeChar> p = new PledgeChar();
  uint32_t id = mOutstandingCharPledges.Append(*p);

  // Algorithm accesses device capabilities code and must run on media thread.
  // Modifies passed-in aSources.

  MediaManager::PostTask(NewTaskFrom([id, aConstraints, aSources]() mutable {
    auto& sources = **aSources;

    // Since the advanced part of the constraints algorithm needs to know when
    // a candidate set is overconstrained (zero members), we must split up the
    // list into videos and audios, and put it back together again at the end.

    nsTArray<RefPtr<VideoDevice>> videos;
    nsTArray<RefPtr<AudioDevice>> audios;

    for (auto& source : sources) {
      if (source->mIsVideo) {
        RefPtr<VideoDevice> video = static_cast<VideoDevice*>(source.get());
        videos.AppendElement(video);
      } else {
        RefPtr<AudioDevice> audio = static_cast<AudioDevice*>(source.get());
        audios.AppendElement(audio);
      }
    }
    sources.Clear();
    const char* badConstraint = nullptr;

    if (videos.Length() && IsOn(aConstraints.mVideo)) {
      badConstraint = MediaConstraintsHelper::SelectSettings(
          GetInvariant(aConstraints.mVideo), videos);
      for (auto& video : videos) {
        sources.AppendElement(video);
      }
    }
    if (audios.Length() && IsOn(aConstraints.mAudio)) {
      badConstraint = MediaConstraintsHelper::SelectSettings(
          GetInvariant(aConstraints.mAudio), audios);
      for (auto& audio : audios) {
        sources.AppendElement(audio);
      }
    }
    NS_DispatchToMainThread(NewRunnableFrom([id, badConstraint]() mutable {
      RefPtr<MediaManager> mgr = MediaManager_GetInstance();
      RefPtr<PledgeChar> p = mgr->mOutstandingCharPledges.Remove(id);
      if (p) {
        p->Resolve(badConstraint);
      }
      return NS_OK;
    }));
  }));
  return p.forget();
}

/**
 * Runs on a seperate thread and is responsible for enumerating devices.
 * Depending on whether a picture or stream was asked for, either
 * ProcessGetUserMedia is called, and the results are sent back to the DOM.
 *
 * Do not run this on the main thread. The success and error callbacks *MUST*
 * be dispatched on the main thread!
 */
class GetUserMediaTask : public Runnable
{
public:
  GetUserMediaTask(
    const MediaStreamConstraints& aConstraints,
    already_AddRefed<nsIDOMGetUserMediaSuccessCallback> aOnSuccess,
    already_AddRefed<nsIDOMGetUserMediaErrorCallback> aOnFailure,
    uint64_t aWindowID, GetUserMediaCallbackMediaStreamListener *aListener,
    MediaEnginePrefs &aPrefs,
    const nsCString& aOrigin,
    MediaManager::SourceSet* aSourceSet)
    : mConstraints(aConstraints)
    , mOnSuccess(aOnSuccess)
    , mOnFailure(aOnFailure)
    , mWindowID(aWindowID)
    , mListener(aListener)
    , mPrefs(aPrefs)
    , mOrigin(aOrigin)
    , mDeviceChosen(false)
    , mSourceSet(aSourceSet)
    , mManager(MediaManager::GetInstance())
  {}

  ~GetUserMediaTask() {
  }

  void
  Fail(const nsAString& aName, const nsAString& aMessage = EmptyString()) {
    RefPtr<MediaMgrError> error = new MediaMgrError(aName, aMessage);
    RefPtr<ErrorCallbackRunnable<nsIDOMGetUserMediaSuccessCallback>> runnable =
      new ErrorCallbackRunnable<nsIDOMGetUserMediaSuccessCallback>(mOnSuccess,
                                                                   mOnFailure,
                                                                   *error,
                                                                   mWindowID);
    // These should be empty now
    MOZ_ASSERT(!mOnSuccess);
    MOZ_ASSERT(!mOnFailure);

    NS_DispatchToMainThread(runnable.forget());
    // Do after ErrorCallbackRunnable Run()s, as it checks active window list
    NS_DispatchToMainThread(do_AddRef(new GetUserMediaListenerRemove(mWindowID, mListener)));
  }

  NS_IMETHOD
  Run() override
  {
    MOZ_ASSERT(!NS_IsMainThread());
    MOZ_ASSERT(mOnSuccess);
    MOZ_ASSERT(mOnFailure);
    MOZ_ASSERT(mDeviceChosen);

    // Allocate a video or audio device and return a MediaStream via
    // a GetUserMediaStreamRunnable.

    nsresult rv;

    if (mAudioDevice) {
      rv = mAudioDevice->Allocate(GetInvariant(mConstraints.mAudio),
                                  mPrefs, mOrigin);
      if (NS_FAILED(rv)) {
        LOG(("Failed to allocate audiosource %d",rv));
        Fail(NS_LITERAL_STRING("SourceUnavailableError"),
             NS_LITERAL_STRING("Failed to allocate audiosource"));
        return NS_OK;
      }
    }
    if (mVideoDevice) {
      rv = mVideoDevice->Allocate(GetInvariant(mConstraints.mVideo),
                                  mPrefs, mOrigin);
      if (NS_FAILED(rv)) {
        LOG(("Failed to allocate videosource %d\n",rv));
        if (mAudioDevice) {
          mAudioDevice->GetSource()->Deallocate();
        }
        Fail(NS_LITERAL_STRING("SourceUnavailableError"),
             NS_LITERAL_STRING("Failed to allocate videosource"));
        return NS_OK;
      }
    }
    PeerIdentity* peerIdentity = nullptr;
    if (!mConstraints.mPeerIdentity.IsEmpty()) {
      peerIdentity = new PeerIdentity(mConstraints.mPeerIdentity);
    }

    NS_DispatchToMainThread(do_AddRef(
        new GetUserMediaStreamRunnable(mOnSuccess, mOnFailure, mWindowID,
                                       mListener, mOrigin, mAudioDevice,
                                       mVideoDevice, peerIdentity)));
    MOZ_ASSERT(!mOnSuccess);
    MOZ_ASSERT(!mOnFailure);
    return NS_OK;
  }

  nsresult
  Denied(const nsAString& aName,
         const nsAString& aMessage = EmptyString())
  {
    MOZ_ASSERT(mOnSuccess);
    MOZ_ASSERT(mOnFailure);

    // We add a disabled listener to the StreamListeners array until accepted
    // If this was the only active MediaStream, remove the window from the list.
    if (NS_IsMainThread()) {
      // This is safe since we're on main-thread, and the window can only
      // be invalidated from the main-thread (see OnNavigation)
      nsCOMPtr<nsIDOMGetUserMediaSuccessCallback> onSuccess = mOnSuccess.forget();
      nsCOMPtr<nsIDOMGetUserMediaErrorCallback> onFailure = mOnFailure.forget();

      nsGlobalWindow* window = nsGlobalWindow::GetInnerWindowWithId(mWindowID);
      if (window) {
        RefPtr<MediaStreamError> error = new MediaStreamError(window,
                                                                aName, aMessage);
        onFailure->OnError(error);
      }
      // Should happen *after* error runs for consistency, but may not matter
      RefPtr<MediaManager> manager(MediaManager::GetInstance());
      manager->RemoveFromWindowList(mWindowID, mListener);
    } else {
      // This will re-check the window being alive on main-thread
      // and remove the listener on MainThread as well
      Fail(aName, aMessage);
    }

    MOZ_ASSERT(!mOnSuccess);
    MOZ_ASSERT(!mOnFailure);

    return NS_OK;
  }

  nsresult
  SetContraints(const MediaStreamConstraints& aConstraints)
  {
    mConstraints = aConstraints;
    return NS_OK;
  }

  nsresult
  SetAudioDevice(AudioDevice* aAudioDevice)
  {
    mAudioDevice = aAudioDevice;
    mDeviceChosen = true;
    return NS_OK;
  }

  nsresult
  SetVideoDevice(VideoDevice* aVideoDevice)
  {
    mVideoDevice = aVideoDevice;
    mDeviceChosen = true;
    return NS_OK;
  }

private:
  MediaStreamConstraints mConstraints;

  nsCOMPtr<nsIDOMGetUserMediaSuccessCallback> mOnSuccess;
  nsCOMPtr<nsIDOMGetUserMediaErrorCallback> mOnFailure;
  uint64_t mWindowID;
  RefPtr<GetUserMediaCallbackMediaStreamListener> mListener;
  RefPtr<AudioDevice> mAudioDevice;
  RefPtr<VideoDevice> mVideoDevice;
  MediaEnginePrefs mPrefs;
  nsCString mOrigin;

  bool mDeviceChosen;
public:
  nsAutoPtr<MediaManager::SourceSet> mSourceSet;
private:
  RefPtr<MediaManager> mManager; // get ref to this when creating the runnable
};

#if defined(ANDROID) && !defined(MOZ_WIDGET_GONK)
class GetUserMediaRunnableWrapper : public Runnable
{
public:
  // This object must take ownership of task
  GetUserMediaRunnableWrapper(GetUserMediaTask* task) :
    mTask(task) {
  }

  ~GetUserMediaRunnableWrapper() {
  }

  NS_IMETHOD Run() override {
    mTask->Run();
    return NS_OK;
  }

private:
  nsAutoPtr<GetUserMediaTask> mTask;
};
#endif

/**
 * EnumerateRawDevices - Enumerate a list of audio & video devices that
 * satisfy passed-in constraints. List contains raw id's.
 */

already_AddRefed<MediaManager::PledgeSourceSet>
MediaManager::EnumerateRawDevices(uint64_t aWindowId,
                                  MediaSourceEnum aVideoType,
                                  MediaSourceEnum aAudioType,
                                  bool aFake, bool aFakeTracks)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aVideoType != MediaSourceEnum::Other ||
             aAudioType != MediaSourceEnum::Other);
  RefPtr<PledgeSourceSet> p = new PledgeSourceSet();
  uint32_t id = mOutstandingPledges.Append(*p);

  nsAdoptingCString audioLoopDev, videoLoopDev;
  if (!aFake) {
    // Fake stream not requested. The entire device stack is available.
    // Loop in loopback devices if they are set, and their respective type is
    // requested. This is currently used for automated media tests only.
    if (aVideoType == MediaSourceEnum::Camera) {
      videoLoopDev = Preferences::GetCString("media.video_loopback_dev");
    }
    if (aAudioType == MediaSourceEnum::Microphone) {
      audioLoopDev = Preferences::GetCString("media.audio_loopback_dev");
    }
  }

  if (!aFake) {
    // Fake tracks only make sense when we have a fake stream.
    aFakeTracks = false;
  }

  MediaManager::PostTask(NewTaskFrom([id, aWindowId, audioLoopDev,
                                      videoLoopDev, aVideoType,
                                      aAudioType, aFake,
                                      aFakeTracks]() mutable {
    // Only enumerate what's asked for, and only fake cams and mics.
    bool hasVideo = aVideoType != MediaSourceEnum::Other;
    bool hasAudio = aAudioType != MediaSourceEnum::Other;
    bool fakeCams = aFake && aVideoType == MediaSourceEnum::Camera;
    bool fakeMics = aFake && aAudioType == MediaSourceEnum::Microphone;

    RefPtr<MediaEngine> fakeBackend, realBackend;
    if (fakeCams || fakeMics) {
      fakeBackend = new MediaEngineDefault(aFakeTracks);
    }
    if ((!fakeCams && hasVideo) || (!fakeMics && hasAudio)) {
      RefPtr<MediaManager> manager = MediaManager_GetInstance();
      realBackend = manager->GetBackend(aWindowId);
    }

    auto result = MakeUnique<SourceSet>();

    if (hasVideo) {
      nsTArray<RefPtr<VideoDevice>> videos;
      GetSources(fakeCams? fakeBackend : realBackend, aVideoType,
                 &MediaEngine::EnumerateVideoDevices, videos, videoLoopDev);
      for (auto& source : videos) {
        result->AppendElement(source);
      }
    }
    if (hasAudio) {
      nsTArray<RefPtr<AudioDevice>> audios;
      GetSources(fakeMics? fakeBackend : realBackend, aAudioType,
                 &MediaEngine::EnumerateAudioDevices, audios, audioLoopDev);
      for (auto& source : audios) {
        result->AppendElement(source);
      }
    }
    SourceSet* handoff = result.release();
    NS_DispatchToMainThread(NewRunnableFrom([id, handoff]() mutable {
      UniquePtr<SourceSet> result(handoff); // grab result
      RefPtr<MediaManager> mgr = MediaManager_GetInstance();
      if (!mgr) {
        return NS_OK;
      }
      RefPtr<PledgeSourceSet> p = mgr->mOutstandingPledges.Remove(id);
      if (p) {
        p->Resolve(result.release());
      }
      return NS_OK;
    }));
  }));
  return p.forget();
}

MediaManager::MediaManager()
  : mMediaThread(nullptr)
  , mBackend(nullptr) {
  mPrefs.mFreq         = 1000; // 1KHz test tone
  mPrefs.mWidth        = 0; // adaptive default
  mPrefs.mHeight       = 0; // adaptive default
  mPrefs.mFPS          = MediaEngine::DEFAULT_VIDEO_FPS;
  mPrefs.mMinFPS       = MediaEngine::DEFAULT_VIDEO_MIN_FPS;
  mPrefs.mAecOn        = false;
  mPrefs.mAgcOn        = false;
  mPrefs.mNoiseOn      = false;
  mPrefs.mExtendedFilter = true;
  mPrefs.mDelayAgnostic = true;
#ifdef MOZ_WEBRTC
  mPrefs.mAec          = webrtc::kEcUnchanged;
  mPrefs.mAgc          = webrtc::kAgcUnchanged;
  mPrefs.mNoise        = webrtc::kNsUnchanged;
#else
  mPrefs.mAec          = 0;
  mPrefs.mAgc          = 0;
  mPrefs.mNoise        = 0;
#endif
  mPrefs.mPlayoutDelay = 0;
  mPrefs.mFullDuplex = false;
  nsresult rv;
  nsCOMPtr<nsIPrefService> prefs = do_GetService("@mozilla.org/preferences-service;1", &rv);
  if (NS_SUCCEEDED(rv)) {
    nsCOMPtr<nsIPrefBranch> branch = do_QueryInterface(prefs);
    if (branch) {
      GetPrefs(branch, nullptr);
    }
  }
  LOG(("%s: default prefs: %dx%d @%dfps (min %d), %dHz test tones, aec: %s,"
       "agc: %s, noise: %s, aec level: %d, agc level: %d, noise level: %d,"
       "playout delay: %d, %sfull_duplex, extended aec %s, delay_agnostic %s",
       __FUNCTION__, mPrefs.mWidth, mPrefs.mHeight,
       mPrefs.mFPS, mPrefs.mMinFPS, mPrefs.mFreq, mPrefs.mAecOn ? "on" : "off",
       mPrefs.mAgcOn ? "on": "off", mPrefs.mNoiseOn ? "on": "off", mPrefs.mAec,
       mPrefs.mAgc, mPrefs.mNoise, mPrefs.mPlayoutDelay, mPrefs.mFullDuplex ? "" : "not ",
       mPrefs.mExtendedFilter ? "on" : "off", mPrefs.mDelayAgnostic ? "on" : "off"));
}

NS_IMPL_ISUPPORTS(MediaManager, nsIMediaManagerService, nsIObserver)

/* static */ StaticRefPtr<MediaManager> MediaManager::sSingleton;

#ifdef DEBUG
/* static */ bool
MediaManager::IsInMediaThread()
{
  return sSingleton?
      (sSingleton->mMediaThread->thread_id() == PlatformThread::CurrentId()) :
      false;
}
#endif

// NOTE: never Dispatch(....,NS_DISPATCH_SYNC) to the MediaManager
// thread from the MainThread, as we NS_DISPATCH_SYNC to MainThread
// from MediaManager thread.

// Guaranteed never to return nullptr.
/* static */  MediaManager*
MediaManager::Get() {
  if (!sSingleton) {
    MOZ_ASSERT(NS_IsMainThread());

    static int timesCreated = 0;
    timesCreated++;
    MOZ_RELEASE_ASSERT(timesCreated == 1);

    sSingleton = new MediaManager();

    sSingleton->mMediaThread = new base::Thread("MediaManager");
    base::Thread::Options options;
#if defined(_WIN32)
    options.message_loop_type = MessageLoop::TYPE_MOZILLA_NONMAINUITHREAD;
#else
    options.message_loop_type = MessageLoop::TYPE_MOZILLA_NONMAINTHREAD;
#endif
    if (!sSingleton->mMediaThread->StartWithOptions(options)) {
      MOZ_CRASH();
    }

    LOG(("New Media thread for gum"));

    nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
    if (obs) {
      obs->AddObserver(sSingleton, "xpcom-will-shutdown", false);
      obs->AddObserver(sSingleton, "last-pb-context-exited", false);
      obs->AddObserver(sSingleton, "getUserMedia:privileged:allow", false);
      obs->AddObserver(sSingleton, "getUserMedia:response:allow", false);
      obs->AddObserver(sSingleton, "getUserMedia:response:deny", false);
      obs->AddObserver(sSingleton, "getUserMedia:revoke", false);
      obs->AddObserver(sSingleton, "phone-state-changed", false);
    }
    // else MediaManager won't work properly and will leak (see bug 837874)
    nsCOMPtr<nsIPrefBranch> prefs = do_GetService(NS_PREFSERVICE_CONTRACTID);
    if (prefs) {
      prefs->AddObserver("media.navigator.video.default_width", sSingleton, false);
      prefs->AddObserver("media.navigator.video.default_height", sSingleton, false);
      prefs->AddObserver("media.navigator.video.default_fps", sSingleton, false);
      prefs->AddObserver("media.navigator.video.default_minfps", sSingleton, false);
      prefs->AddObserver("media.navigator.audio.fake_frequency", sSingleton, false);
      prefs->AddObserver("media.navigator.audio.full_duplex", sSingleton, false);
#ifdef MOZ_WEBRTC
      prefs->AddObserver("media.getusermedia.aec_enabled", sSingleton, false);
      prefs->AddObserver("media.getusermedia.aec", sSingleton, false);
      prefs->AddObserver("media.getusermedia.agc_enabled", sSingleton, false);
      prefs->AddObserver("media.getusermedia.agc", sSingleton, false);
      prefs->AddObserver("media.getusermedia.noise_enabled", sSingleton, false);
      prefs->AddObserver("media.getusermedia.noise", sSingleton, false);
      prefs->AddObserver("media.getusermedia.playout_delay", sSingleton, false);
#endif
    }

    // Prepare async shutdown

    nsCOMPtr<nsIAsyncShutdownClient> shutdownPhase = GetShutdownPhase();

    class Blocker : public media::ShutdownBlocker
    {
    public:
      Blocker()
      : media::ShutdownBlocker(NS_LITERAL_STRING(
          "Media shutdown: blocking on media thread")) {}

      NS_IMETHOD BlockShutdown(nsIAsyncShutdownClient*) override
      {
        MOZ_RELEASE_ASSERT(MediaManager::GetIfExists());
        MediaManager::GetIfExists()->Shutdown();
        return NS_OK;
      }
    };

    sSingleton->mShutdownBlocker = new Blocker();
    nsresult rv = shutdownPhase->AddBlocker(sSingleton->mShutdownBlocker,
                                            NS_LITERAL_STRING(__FILE__),
                                            __LINE__,
                                            NS_LITERAL_STRING("Media shutdown"));
    MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv));
#ifdef MOZ_B2G
    // Init MediaPermissionManager before sending out any permission requests.
    (void) MediaPermissionManager::GetInstance();
#endif //MOZ_B2G
  }
  return sSingleton;
}

/* static */  MediaManager*
MediaManager::GetIfExists() {
  return sSingleton;
}

/* static */ already_AddRefed<MediaManager>
MediaManager::GetInstance()
{
  // so we can have non-refcounted getters
  RefPtr<MediaManager> service = MediaManager::Get();
  return service.forget();
}

media::Parent<media::NonE10s>*
MediaManager::GetNonE10sParent()
{
  if (!mNonE10sParent) {
    mNonE10sParent = MakeUnique<media::Parent<media::NonE10s>>(true);
  }
  return mNonE10sParent.get();
}

/* static */ void
MediaManager::StartupInit()
{
#ifdef WIN32
  if (IsVistaOrLater() && !IsWin8OrLater()) {
    // Bug 1107702 - Older Windows fail in GetAdaptersInfo (and others) if the
    // first(?) call occurs after the process size is over 2GB (kb/2588507).
    // Attempt to 'prime' the pump by making a call at startup.
    unsigned long out_buf_len = sizeof(IP_ADAPTER_INFO);
    PIP_ADAPTER_INFO pAdapterInfo = (IP_ADAPTER_INFO *) moz_xmalloc(out_buf_len);
    if (GetAdaptersInfo(pAdapterInfo, &out_buf_len) == ERROR_BUFFER_OVERFLOW) {
      free(pAdapterInfo);
      pAdapterInfo = (IP_ADAPTER_INFO *) moz_xmalloc(out_buf_len);
      GetAdaptersInfo(pAdapterInfo, &out_buf_len);
    }
    if (pAdapterInfo) {
      free(pAdapterInfo);
    }
  }
#endif
}

/* static */
void
MediaManager::PostTask(already_AddRefed<Runnable> task)
{
  if (sInShutdown) {
    // Can't safely delete task here since it may have items with specific
    // thread-release requirements.
    // XXXkhuey well then who is supposed to delete it?! We don't signal
    // that we failed ...
    MOZ_CRASH();
    return;
  }
  NS_ASSERTION(Get(), "MediaManager singleton?");
  NS_ASSERTION(Get()->mMediaThread, "No thread yet");
  Get()->mMediaThread->message_loop()->PostTask(Move(task));
}

/* static */ nsresult
MediaManager::NotifyRecordingStatusChange(nsPIDOMWindow* aWindow,
                                          const nsString& aMsg,
                                          const bool& aIsAudio,
                                          const bool& aIsVideo)
{
  NS_ENSURE_ARG(aWindow);

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (!obs) {
    NS_WARNING("Could not get the Observer service for GetUserMedia recording notification.");
    return NS_ERROR_FAILURE;
  }

  RefPtr<nsHashPropertyBag> props = new nsHashPropertyBag();
  props->SetPropertyAsBool(NS_LITERAL_STRING("isAudio"), aIsAudio);
  props->SetPropertyAsBool(NS_LITERAL_STRING("isVideo"), aIsVideo);

  bool isApp = false;
  nsString requestURL;

  if (nsCOMPtr<nsIDocShell> docShell = aWindow->GetDocShell()) {
    nsresult rv = docShell->GetIsApp(&isApp);
    NS_ENSURE_SUCCESS(rv, rv);

    if (isApp) {
      rv = docShell->GetAppManifestURL(requestURL);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  if (!isApp) {
    nsCString pageURL;
    nsCOMPtr<nsIURI> docURI = aWindow->GetDocumentURI();
    NS_ENSURE_TRUE(docURI, NS_ERROR_FAILURE);

    nsresult rv = docURI->GetSpec(pageURL);
    NS_ENSURE_SUCCESS(rv, rv);

    requestURL = NS_ConvertUTF8toUTF16(pageURL);
  }

  props->SetPropertyAsBool(NS_LITERAL_STRING("isApp"), isApp);
  props->SetPropertyAsAString(NS_LITERAL_STRING("requestURL"), requestURL);

  obs->NotifyObservers(static_cast<nsIPropertyBag2*>(props),
                       "recording-device-events",
                       aMsg.get());

  // Forward recording events to parent process.
  // The events are gathered in chrome process and used for recording indicator
  if (!XRE_IsParentProcess()) {
    Unused <<
      dom::ContentChild::GetSingleton()->SendRecordingDeviceEvents(aMsg,
                                                                   requestURL,
                                                                   aIsAudio,
                                                                   aIsVideo);
  }

  return NS_OK;
}

bool MediaManager::IsPrivileged()
{
  bool permission = nsContentUtils::IsCallerChrome();

  // Developer preference for turning off permission check.
  if (Preferences::GetBool("media.navigator.permission.disabled", false)) {
    permission = true;
  }
  return permission;
}

bool MediaManager::IsLoop(nsIURI* aDocURI)
{
  MOZ_ASSERT(aDocURI);

  nsCOMPtr<nsIURI> loopURI;
  nsresult rv = NS_NewURI(getter_AddRefs(loopURI), "about:loopconversation");
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }
  bool result = false;
  rv = aDocURI->EqualsExceptRef(loopURI, &result);
  NS_ENSURE_SUCCESS(rv, false);
  return result;
}

bool MediaManager::IsPrivateBrowsing(nsPIDOMWindow *window)
{
  nsCOMPtr<nsIDocument> doc = window->GetDoc();
  nsCOMPtr<nsILoadContext> loadContext = doc->GetLoadContext();
  return loadContext && loadContext->UsePrivateBrowsing();
}

nsresult MediaManager::GenerateUUID(nsAString& aResult)
{
  nsresult rv;
  nsCOMPtr<nsIUUIDGenerator> uuidgen =
      do_GetService("@mozilla.org/uuid-generator;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  // Generate a call ID.
  nsID id;
  rv = uuidgen->GenerateUUIDInPlace(&id);
  NS_ENSURE_SUCCESS(rv, rv);

  char buffer[NSID_LENGTH];
  id.ToProvidedString(buffer);
  aResult.Assign(NS_ConvertUTF8toUTF16(buffer));
  return NS_OK;
}

enum class GetUserMediaSecurityState {
  Other = 0,
  HTTPS = 1,
  File = 2,
  App = 3,
  Localhost = 4,
  Loop = 5,
  Privileged = 6
};

/**
 * The entry point for this file. A call from Navigator::mozGetUserMedia
 * will end up here. MediaManager is a singleton that is responsible
 * for handling all incoming getUserMedia calls from every window.
 */
nsresult
MediaManager::GetUserMedia(nsPIDOMWindow* aWindow,
                           const MediaStreamConstraints& aConstraintsPassedIn,
                           nsIDOMGetUserMediaSuccessCallback* aOnSuccess,
                           nsIDOMGetUserMediaErrorCallback* aOnFailure)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aWindow);
  MOZ_ASSERT(aOnFailure);
  MOZ_ASSERT(aOnSuccess);
  nsCOMPtr<nsIDOMGetUserMediaSuccessCallback> onSuccess(aOnSuccess);
  nsCOMPtr<nsIDOMGetUserMediaErrorCallback> onFailure(aOnFailure);
  uint64_t windowID = aWindow->WindowID();

  MediaStreamConstraints c(aConstraintsPassedIn); // use a modifiable copy

  // Do all the validation we can while we're sync (to return an
  // already-rejected promise on failure).

  if (!IsOn(c.mVideo) && !IsOn(c.mAudio)) {
    RefPtr<MediaStreamError> error =
        new MediaStreamError(aWindow,
                             NS_LITERAL_STRING("NotSupportedError"),
                             NS_LITERAL_STRING("audio and/or video is required"));
    onFailure->OnError(error);
    return NS_OK;
  }
  if (sInShutdown) {
    RefPtr<MediaStreamError> error =
        new MediaStreamError(aWindow,
                             NS_LITERAL_STRING("AbortError"),
                             NS_LITERAL_STRING("In shutdown"));
    onFailure->OnError(error);
    return NS_OK;
  }

  // Determine permissions early (while we still have a stack).

  nsIURI* docURI = aWindow->GetDocumentURI();
  if (!docURI) {
    return NS_ERROR_UNEXPECTED;
  }
  bool loop = IsLoop(docURI);
  bool privileged = loop || IsPrivileged();
  bool isHTTPS = false;
  docURI->SchemeIs("https", &isHTTPS);
  nsCString host;
  nsresult rv = docURI->GetHost(host);
  // Test for some other schemes that ServiceWorker recognizes
  bool isFile;
  docURI->SchemeIs("file", &isFile);
  bool isApp;
  docURI->SchemeIs("app", &isApp);
  // Same localhost check as ServiceWorkers uses
  // (see IsOriginPotentiallyTrustworthy())
  bool isLocalhost = NS_SUCCEEDED(rv) &&
                     (host.LowerCaseEqualsLiteral("localhost") ||
                      host.LowerCaseEqualsLiteral("127.0.0.1") ||
                      host.LowerCaseEqualsLiteral("::1"));

  nsCString origin;
  rv = nsPrincipal::GetOriginForURI(docURI, origin);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (!Preferences::GetBool("media.navigator.video.enabled", true)) {
    c.mVideo.SetAsBoolean() = false;
  }

  MediaSourceEnum videoType = MediaSourceEnum::Other; // none
  MediaSourceEnum audioType = MediaSourceEnum::Other; // none

  if (c.mVideo.IsMediaTrackConstraints()) {
    auto& vc = c.mVideo.GetAsMediaTrackConstraints();
    videoType = StringToEnum(dom::MediaSourceEnumValues::strings,
                             vc.mMediaSource,
                             MediaSourceEnum::Other);
    switch (videoType) {
      case MediaSourceEnum::Camera:
        break;

      case MediaSourceEnum::Browser:
        // If no window id is passed in then default to the caller's window.
        // Functional defaults are helpful in tests, but also a natural outcome
        // of the constraints API's limited semantics for requiring input.
        if (!vc.mBrowserWindow.WasPassed()) {
          nsPIDOMWindow *outer = aWindow->GetOuterWindow();
          vc.mBrowserWindow.Construct(outer->WindowID());
        }
        MOZ_FALLTHROUGH;
      case MediaSourceEnum::Screen:
      case MediaSourceEnum::Application:
      case MediaSourceEnum::Window:
        // Deny screensharing request if support is disabled, or
        // the requesting document is not from a host on the whitelist, or
        // we're on Mac OSX 10.6 and WinXP until proved that they work
        if (!Preferences::GetBool(((videoType == MediaSourceEnum::Browser)?
                                   "media.getusermedia.browser.enabled" :
                                   "media.getusermedia.screensharing.enabled"),
                                  false) ||
#if defined(XP_MACOSX) || defined(XP_WIN)
            (
              // Allow tab sharing for all platforms including XP and OSX 10.6
              (videoType != MediaSourceEnum::Browser) &&
              !Preferences::GetBool("media.getusermedia.screensharing.allow_on_old_platforms",
                                    false) &&
#if defined(XP_MACOSX)
              !nsCocoaFeatures::OnLionOrLater()
#endif
#if defined (XP_WIN)
              !IsVistaOrLater()
#endif
              ) ||
#endif
            (!privileged && !HostIsHttps(*docURI)) ||
            !(loop || HostHasPermission(*docURI))) {
          RefPtr<MediaStreamError> error =
              new MediaStreamError(aWindow,
                                   NS_LITERAL_STRING("SecurityError"));
          onFailure->OnError(error);
          return NS_OK;
        }
        break;

      case MediaSourceEnum::Microphone:
      case MediaSourceEnum::Other:
      default: {
        RefPtr<MediaStreamError> error =
            new MediaStreamError(aWindow,
                                 NS_LITERAL_STRING("OverconstrainedError"),
                                 NS_LITERAL_STRING(""),
                                 NS_LITERAL_STRING("mediaSource"));
        onFailure->OnError(error);
        return NS_OK;
      }
    }

    if (vc.mAdvanced.WasPassed() && videoType != MediaSourceEnum::Camera) {
      // iterate through advanced, forcing all unset mediaSources to match "root"
      const char *unset = EnumToASCII(dom::MediaSourceEnumValues::strings,
                                      MediaSourceEnum::Camera);
      for (MediaTrackConstraintSet& cs : vc.mAdvanced.Value()) {
        if (cs.mMediaSource.EqualsASCII(unset)) {
          cs.mMediaSource = vc.mMediaSource;
        }
      }
    }
    if (!privileged) {
      // only allow privileged content to set the window id
      if (vc.mBrowserWindow.WasPassed()) {
        vc.mBrowserWindow.Value() = -1;
      }
      if (vc.mAdvanced.WasPassed()) {
        for (MediaTrackConstraintSet& cs : vc.mAdvanced.Value()) {
          if (cs.mBrowserWindow.WasPassed()) {
            cs.mBrowserWindow.Value() = -1;
          }
        }
      }
    }

    // For all but tab sharing, Loop needs to prompt as we are using the
    // permission menu for selection of the device currently. For tab sharing,
    // Loop has implicit permissions within Firefox, as it is built-in,
    // and will manage the active tab and provide appropriate UI.
    if (loop && (videoType == MediaSourceEnum::Window ||
                 videoType == MediaSourceEnum::Application ||
                 videoType == MediaSourceEnum::Screen)) {
       privileged = false;
    }
  } else if (IsOn(c.mVideo)) {
    videoType = MediaSourceEnum::Camera;
  }

  if (c.mAudio.IsMediaTrackConstraints()) {
    auto& ac = c.mAudio.GetAsMediaTrackConstraints();
    audioType = StringToEnum(dom::MediaSourceEnumValues::strings,
                             ac.mMediaSource,
                             MediaSourceEnum::Other);
    // Work around WebIDL default since spec uses same dictionary w/audio & video.
    if (audioType == MediaSourceEnum::Camera) {
      audioType = MediaSourceEnum::Microphone;
      ac.mMediaSource.AssignASCII(EnumToASCII(dom::MediaSourceEnumValues::strings,
                                              audioType));
    }
    switch (audioType) {
      case MediaSourceEnum::Microphone:
        break;

      case MediaSourceEnum::AudioCapture:
        // Only enable AudioCapture if the pref is enabled. If it's not, we can
        // deny right away.
        if (!Preferences::GetBool("media.getusermedia.audiocapture.enabled")) {
          RefPtr<MediaStreamError> error =
            new MediaStreamError(aWindow,
                                 NS_LITERAL_STRING("SecurityError"));
          onFailure->OnError(error);
          return NS_OK;
        }
        break;

      case MediaSourceEnum::Other:
      default: {
        RefPtr<MediaStreamError> error =
            new MediaStreamError(aWindow,
                                 NS_LITERAL_STRING("OverconstrainedError"),
                                 NS_LITERAL_STRING(""),
                                 NS_LITERAL_STRING("mediaSource"));
        onFailure->OnError(error);
        return NS_OK;
      }
    }
    if (ac.mAdvanced.WasPassed()) {
      // iterate through advanced, forcing all unset mediaSources to match "root"
      const char *unset = EnumToASCII(dom::MediaSourceEnumValues::strings,
                                      MediaSourceEnum::Camera);
      for (MediaTrackConstraintSet& cs : ac.mAdvanced.Value()) {
        if (cs.mMediaSource.EqualsASCII(unset)) {
          cs.mMediaSource = ac.mMediaSource;
        }
      }
    }
  } else if (IsOn(c.mAudio)) {
   audioType = MediaSourceEnum::Microphone;
  }

  StreamListeners* listeners = AddWindowID(windowID);

  // Create a disabled listener to act as a placeholder
  nsIPrincipal* principal = aWindow->GetExtantDoc()->NodePrincipal();
  RefPtr<GetUserMediaCallbackMediaStreamListener> listener =
    new GetUserMediaCallbackMediaStreamListener(mMediaThread, windowID,
                                                MakePrincipalHandle(principal));

  // No need for locking because we always do this in the main thread.
  listeners->AppendElement(listener);

  if (!privileged) {
    // Check if this site has had persistent permissions denied.
    nsCOMPtr<nsIPermissionManager> permManager =
      do_GetService(NS_PERMISSIONMANAGER_CONTRACTID, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    uint32_t audioPerm = nsIPermissionManager::UNKNOWN_ACTION;
    if (IsOn(c.mAudio)) {
      rv = permManager->TestExactPermissionFromPrincipal(
        principal, "microphone", &audioPerm);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    uint32_t videoPerm = nsIPermissionManager::UNKNOWN_ACTION;
    if (IsOn(c.mVideo)) {
      rv = permManager->TestExactPermissionFromPrincipal(
        principal, "camera", &videoPerm);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    if ((!IsOn(c.mAudio) || audioPerm == nsIPermissionManager::DENY_ACTION) &&
        (!IsOn(c.mVideo) || videoPerm == nsIPermissionManager::DENY_ACTION)) {
      RefPtr<MediaStreamError> error =
          new MediaStreamError(aWindow, NS_LITERAL_STRING("SecurityError"));
      onFailure->OnError(error);
      RemoveFromWindowList(windowID, listener);
      return NS_OK;
    }
  }

#if defined(MOZ_B2G_CAMERA) && defined(MOZ_WIDGET_GONK)
  if (mCameraManager == nullptr) {
    mCameraManager = nsDOMCameraManager::CreateInstance(aWindow);
  }
#endif

  // Get list of all devices, with origin-specific device ids.

  MediaEnginePrefs prefs = mPrefs;

  nsString callID;
  rv = GenerateUUID(callID);
  NS_ENSURE_SUCCESS(rv, rv);

  bool fake = c.mFake.WasPassed()? c.mFake.Value() :
      Preferences::GetBool("media.navigator.streams.fake");

  bool fakeTracks = c.mFakeTracks.WasPassed()? c.mFakeTracks.Value() : false;

  bool askPermission = !privileged &&
      (!fake || Preferences::GetBool("media.navigator.permission.fake"));

  RefPtr<PledgeSourceSet> p = EnumerateDevicesImpl(windowID, videoType,
                                                   audioType, fake,
                                                   fakeTracks);
  p->Then([this, onSuccess, onFailure, windowID, c, listener, askPermission,
           prefs, isHTTPS, callID, origin](SourceSet*& aDevices) mutable {

    RefPtr<Refcountable<UniquePtr<SourceSet>>> devices(
         new Refcountable<UniquePtr<SourceSet>>(aDevices)); // grab result

    // Ensure that the captured 'this' pointer and our windowID are still good.
    if (!MediaManager::Exists() ||
        !nsGlobalWindow::GetInnerWindowWithId(windowID)) {
      return;
    }

    // Apply any constraints. This modifies the passed-in list.
    RefPtr<PledgeChar> p2 = SelectSettings(c, devices);

    p2->Then([this, onSuccess, onFailure, windowID, c,
              listener, askPermission, prefs, isHTTPS,
              callID, origin, devices](const char*& badConstraint) mutable {

      // Ensure that the captured 'this' pointer and our windowID are still good.
      RefPtr<nsPIDOMWindow> window = static_cast<nsPIDOMWindow*>
          (nsGlobalWindow::GetInnerWindowWithId(windowID));
      if (!MediaManager::Exists() || !window) {
        return;
      }

      if (badConstraint) {
        nsString constraint;
        constraint.AssignASCII(badConstraint);
        RefPtr<MediaStreamError> error =
            new MediaStreamError(window,
                                 NS_LITERAL_STRING("OverconstrainedError"),
                                 NS_LITERAL_STRING(""),
                                 constraint);
        onFailure->OnError(error);
        return;
      }
      if (!(*devices)->Length()) {
        RefPtr<MediaStreamError> error =
            new MediaStreamError(window, NS_LITERAL_STRING("NotFoundError"));
        onFailure->OnError(error);
        return;
      }

      nsCOMPtr<nsISupportsArray> devicesCopy; // before we give up devices below
      if (!askPermission) {
        nsresult rv = NS_NewISupportsArray(getter_AddRefs(devicesCopy));
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return;
        }
        for (auto& device : **devices) {
          rv = devicesCopy->AppendElement(device);
          if (NS_WARN_IF(NS_FAILED(rv))) {
            return;
          }
        }
      }

      // Pass callbacks and MediaStreamListener along to GetUserMediaTask.
      RefPtr<GetUserMediaTask> task (new GetUserMediaTask(c, onSuccess.forget(),
                                                          onFailure.forget(),
                                                          windowID, listener,
                                                          prefs, origin,
                                                          devices->release()));
      // Store the task w/callbacks.
      mActiveCallbacks.Put(callID, task.forget());

      // Add a WindowID cross-reference so OnNavigation can tear things down
      nsTArray<nsString>* array;
      if (!mCallIds.Get(windowID, &array)) {
        array = new nsTArray<nsString>();
        mCallIds.Put(windowID, array);
      }
      array->AppendElement(callID);

      nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
      if (!askPermission) {
        obs->NotifyObservers(devicesCopy, "getUserMedia:privileged:allow",
                             callID.BeginReading());
      } else {
        RefPtr<GetUserMediaRequest> req =
            new GetUserMediaRequest(window, callID, c, isHTTPS);
        obs->NotifyObservers(req, "getUserMedia:request", nullptr);
      }

#ifdef MOZ_WEBRTC
      EnableWebRtcLog();
#endif
    }, [onFailure](MediaStreamError*& reason) mutable {
      onFailure->OnError(reason);
    });
  }, [onFailure](MediaStreamError*& reason) mutable {
    onFailure->OnError(reason);
  });
  return NS_OK;
}

/* static */ void
MediaManager::AnonymizeDevices(SourceSet& aDevices, const nsACString& aOriginKey)
{
  if (!aOriginKey.IsEmpty()) {
    for (auto& device : aDevices) {
      nsString id;
      device->GetId(id);
      AnonymizeId(id, aOriginKey);
      device->SetId(id);
    }
  }
}

/* static */ nsresult
MediaManager::AnonymizeId(nsAString& aId, const nsACString& aOriginKey)
{
  MOZ_ASSERT(NS_IsMainThread());

  nsresult rv;
  nsCOMPtr<nsIKeyObjectFactory> factory =
    do_GetService("@mozilla.org/security/keyobjectfactory;1", &rv);
  if (NS_FAILED(rv)) {
    return rv;
  }
  nsCString rawKey;
  rv = Base64Decode(aOriginKey, rawKey);
  if (NS_FAILED(rv)) {
    return rv;
  }
  nsCOMPtr<nsIKeyObject> key;
  rv = factory->KeyFromString(nsIKeyObject::HMAC, rawKey, getter_AddRefs(key));
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsCOMPtr<nsICryptoHMAC> hasher =
    do_CreateInstance(NS_CRYPTO_HMAC_CONTRACTID, &rv);
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = hasher->Init(nsICryptoHMAC::SHA256, key);
  if (NS_FAILED(rv)) {
    return rv;
  }
  NS_ConvertUTF16toUTF8 id(aId);
  rv = hasher->Update(reinterpret_cast<const uint8_t*> (id.get()), id.Length());
  if (NS_FAILED(rv)) {
    return rv;
  }
  nsCString mac;
  rv = hasher->Finish(true, mac);
  if (NS_FAILED(rv)) {
    return rv;
  }

  aId = NS_ConvertUTF8toUTF16(mac);
  return NS_OK;
}

/* static */
already_AddRefed<nsIWritableVariant>
MediaManager::ToJSArray(SourceSet& aDevices)
{
  MOZ_ASSERT(NS_IsMainThread());
  RefPtr<nsVariantCC> var = new nsVariantCC();
  size_t len = aDevices.Length();
  if (len) {
    nsTArray<nsIMediaDevice*> tmp(len);
    for (auto& device : aDevices) {
      tmp.AppendElement(device);
    }
    auto* elements = static_cast<const void*>(tmp.Elements());
    nsresult rv = var->SetAsArray(nsIDataType::VTYPE_INTERFACE,
                                  &NS_GET_IID(nsIMediaDevice), len,
                                  const_cast<void*>(elements));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return nullptr;
    }
  } else {
    var->SetAsEmptyArray(); // because SetAsArray() fails on zero length arrays.
  }
  return var.forget();
}

already_AddRefed<MediaManager::PledgeSourceSet>
MediaManager::EnumerateDevicesImpl(uint64_t aWindowId,
                                   MediaSourceEnum aVideoType,
                                   MediaSourceEnum aAudioType,
                                   bool aFake, bool aFakeTracks)
{
  MOZ_ASSERT(NS_IsMainThread());
  nsPIDOMWindow *window = static_cast<nsPIDOMWindow*>
      (nsGlobalWindow::GetInnerWindowWithId(aWindowId));

  // This function returns a pledge, a promise-like object with the future result
  RefPtr<PledgeSourceSet> pledge = new PledgeSourceSet();
  uint32_t id = mOutstandingPledges.Append(*pledge);

  // To get a device list anonymized for a particular origin, we must:
  // 1. Get an origin-key (for either regular or private browsing)
  // 2. Get the raw devices list
  // 3. Anonymize the raw list with the origin-key.

  bool privateBrowsing = IsPrivateBrowsing(window);
  nsCString origin;
  nsPrincipal::GetOriginForURI(window->GetDocumentURI(), origin);

  bool persist = IsActivelyCapturingOrHasAPermission(aWindowId);

  // GetOriginKey is an async API that returns a pledge (a promise-like
  // pattern). We use .Then() to pass in a lambda to run back on this same
  // thread later once GetOriginKey resolves. Needed variables are "captured"
  // (passed by value) safely into the lambda.

  RefPtr<Pledge<nsCString>> p = media::GetOriginKey(origin, privateBrowsing,
                                                      persist);
  p->Then([id, aWindowId, aVideoType, aAudioType,
           aFake, aFakeTracks](const nsCString& aOriginKey) mutable {
    MOZ_ASSERT(NS_IsMainThread());
    RefPtr<MediaManager> mgr = MediaManager_GetInstance();

    RefPtr<PledgeSourceSet> p = mgr->EnumerateRawDevices(aWindowId,
                                                         aVideoType, aAudioType,
                                                         aFake, aFakeTracks);
    p->Then([id, aWindowId, aOriginKey](SourceSet*& aDevices) mutable {
      UniquePtr<SourceSet> devices(aDevices); // secondary result

      // Only run if window is still on our active list.
      RefPtr<MediaManager> mgr = MediaManager_GetInstance();
      if (!mgr) {
        return NS_OK;
      }
      RefPtr<PledgeSourceSet> p = mgr->mOutstandingPledges.Remove(id);
      if (!p || !mgr->IsWindowStillActive(aWindowId)) {
        return NS_OK;
      }
      MediaManager_AnonymizeDevices(*devices, aOriginKey);
      p->Resolve(devices.release());
      return NS_OK;
    });
  });
  return pledge.forget();
}

nsresult
MediaManager::EnumerateDevices(nsPIDOMWindow* aWindow,
                               nsIGetUserMediaDevicesSuccessCallback* aOnSuccess,
                               nsIDOMGetUserMediaErrorCallback* aOnFailure)
{
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_TRUE(!sInShutdown, NS_ERROR_FAILURE);
  nsCOMPtr<nsIGetUserMediaDevicesSuccessCallback> onSuccess(aOnSuccess);
  nsCOMPtr<nsIDOMGetUserMediaErrorCallback> onFailure(aOnFailure);
  uint64_t windowId = aWindow->WindowID();

  StreamListeners* listeners = AddWindowID(windowId);

  nsIPrincipal* principal = aWindow->GetExtantDoc()->NodePrincipal();

  // Create a disabled listener to act as a placeholder
  RefPtr<GetUserMediaCallbackMediaStreamListener> listener =
    new GetUserMediaCallbackMediaStreamListener(mMediaThread, windowId,
                                                MakePrincipalHandle(principal));

  // No need for locking because we always do this in the main thread.
  listeners->AppendElement(listener);

  bool fake = Preferences::GetBool("media.navigator.streams.fake");

  RefPtr<PledgeSourceSet> p = EnumerateDevicesImpl(windowId,
                                                     MediaSourceEnum::Camera,
                                                     MediaSourceEnum::Microphone,
                                                     fake);
  p->Then([onSuccess, windowId, listener](SourceSet*& aDevices) mutable {
    UniquePtr<SourceSet> devices(aDevices); // grab result
    RefPtr<MediaManager> mgr = MediaManager_GetInstance();
    mgr->RemoveFromWindowList(windowId, listener);
    nsCOMPtr<nsIWritableVariant> array = MediaManager_ToJSArray(*devices);
    onSuccess->OnSuccess(array);
  }, [onFailure, windowId, listener](MediaStreamError*& reason) mutable {
    RefPtr<MediaManager> mgr = MediaManager_GetInstance();
    mgr->RemoveFromWindowList(windowId, listener);
    onFailure->OnError(reason);
  });
  return NS_OK;
}

/*
 * GetUserMediaDevices - called by the UI-part of getUserMedia from chrome JS.
 */

nsresult
MediaManager::GetUserMediaDevices(nsPIDOMWindow* aWindow,
                                  const MediaStreamConstraints& aConstraints,
                                  nsIGetUserMediaDevicesSuccessCallback* aOnSuccess,
                                  nsIDOMGetUserMediaErrorCallback* aOnFailure,
                                  uint64_t aWindowId,
                                  const nsAString& aCallID)
{
  MOZ_ASSERT(NS_IsMainThread());
  nsCOMPtr<nsIGetUserMediaDevicesSuccessCallback> onSuccess(aOnSuccess);
  nsCOMPtr<nsIDOMGetUserMediaErrorCallback> onFailure(aOnFailure);
  if (!aWindowId) {
    aWindowId = aWindow->WindowID();
  }

  // Ignore passed-in constraints, instead locate + return already-constrained list.

  nsTArray<nsString>* callIDs;
  if (!mCallIds.Get(aWindowId, &callIDs)) {
    return NS_ERROR_UNEXPECTED;
  }

  for (auto& callID : *callIDs) {
    RefPtr<GetUserMediaTask> task;
    if (!aCallID.Length() || aCallID == callID) {
      if (mActiveCallbacks.Get(callID, getter_AddRefs(task))) {
        nsCOMPtr<nsIWritableVariant> array = MediaManager_ToJSArray(*task->mSourceSet);
        onSuccess->OnSuccess(array);
        return NS_OK;
      }
    }
  }
  return NS_ERROR_UNEXPECTED;
}

MediaEngine*
MediaManager::GetBackend(uint64_t aWindowId)
{
  MOZ_ASSERT(MediaManager::IsInMediaThread());
  // Plugin backends as appropriate. The default engine also currently
  // includes picture support for Android.
  // This IS called off main-thread.
  if (!mBackend) {
    MOZ_RELEASE_ASSERT(!sInShutdown);  // we should never create a new backend in shutdown
#if defined(MOZ_WEBRTC)
    mBackend = new MediaEngineWebRTC(mPrefs);
#else
    mBackend = new MediaEngineDefault();
#endif
  }
  return mBackend;
}

static void
StopSharingCallback(MediaManager *aThis,
                    uint64_t aWindowID,
                    StreamListeners *aListeners,
                    void *aData)
{
  MOZ_ASSERT(NS_IsMainThread());
  if (aListeners) {
    auto length = aListeners->Length();
    for (size_t i = 0; i < length; ++i) {
      GetUserMediaCallbackMediaStreamListener *listener = aListeners->ElementAt(i);

      if (listener->Stream()) { // aka HasBeenActivate()ed
        listener->Stop();
      }
      listener->Remove();
      listener->StopSharing();
    }
    aListeners->Clear();
    aThis->RemoveWindowID(aWindowID);
  }
}


void
MediaManager::OnNavigation(uint64_t aWindowID)
{
  MOZ_ASSERT(NS_IsMainThread());
  LOG(("OnNavigation for %llu", aWindowID));

  // Stop the streams for this window. The runnables check this value before
  // making a call to content.

  nsTArray<nsString>* callIDs;
  if (mCallIds.Get(aWindowID, &callIDs)) {
    for (auto& callID : *callIDs) {
      mActiveCallbacks.Remove(callID);
    }
    mCallIds.Remove(aWindowID);
  }

  // This is safe since we're on main-thread, and the windowlist can only
  // be added to from the main-thread
  nsPIDOMWindow *window = static_cast<nsPIDOMWindow*>
      (nsGlobalWindow::GetInnerWindowWithId(aWindowID));
  if (window) {
    IterateWindowListeners(window, StopSharingCallback, nullptr);
  } else {
    RemoveWindowID(aWindowID);
  }
}

StreamListeners*
MediaManager::AddWindowID(uint64_t aWindowId)
{
  MOZ_ASSERT(NS_IsMainThread());
  // Store the WindowID in a hash table and mark as active. The entry is removed
  // when this window is closed or navigated away from.
  // This is safe since we're on main-thread, and the windowlist can only
  // be invalidated from the main-thread (see OnNavigation)
  StreamListeners* listeners = GetActiveWindows()->Get(aWindowId);
  if (!listeners) {
    listeners = new StreamListeners;
    GetActiveWindows()->Put(aWindowId, listeners);
  }
  return listeners;
}

void
MediaManager::RemoveWindowID(uint64_t aWindowId)
{
  mActiveWindows.Remove(aWindowId);

  // get outer windowID
  nsPIDOMWindow *window = static_cast<nsPIDOMWindow*>
    (nsGlobalWindow::GetInnerWindowWithId(aWindowId));
  if (!window) {
    LOG(("No inner window for %llu", aWindowId));
    return;
  }

  nsPIDOMWindow *outer = window->GetOuterWindow();
  if (!outer) {
    LOG(("No outer window for inner %llu", aWindowId));
    return;
  }

  uint64_t outerID = outer->WindowID();

  // Notify the UI that this window no longer has gUM active
  char windowBuffer[32];
  snprintf_literal(windowBuffer, "%" PRIu64, outerID);
  nsString data = NS_ConvertUTF8toUTF16(windowBuffer);

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  obs->NotifyObservers(nullptr, "recording-window-ended", data.get());
  LOG(("Sent recording-window-ended for window %llu (outer %llu)",
       aWindowId, outerID));
}

void
MediaManager::RemoveFromWindowList(uint64_t aWindowID,
  GetUserMediaCallbackMediaStreamListener *aListener)
{
  MOZ_ASSERT(NS_IsMainThread());

  // This is defined as safe on an inactive GUMCMSListener
  aListener->Remove(); // really queues the remove

  StreamListeners* listeners = GetWindowListeners(aWindowID);
  if (!listeners) {
    return;
  }
  listeners->RemoveElement(aListener);
  if (listeners->Length() == 0) {
    RemoveWindowID(aWindowID);
    // listeners has been deleted here
  }
}

void
MediaManager::GetPref(nsIPrefBranch *aBranch, const char *aPref,
                      const char *aData, int32_t *aVal)
{
  int32_t temp;
  if (aData == nullptr || strcmp(aPref,aData) == 0) {
    if (NS_SUCCEEDED(aBranch->GetIntPref(aPref, &temp))) {
      *aVal = temp;
    }
  }
}

void
MediaManager::GetPrefBool(nsIPrefBranch *aBranch, const char *aPref,
                          const char *aData, bool *aVal)
{
  bool temp;
  if (aData == nullptr || strcmp(aPref,aData) == 0) {
    if (NS_SUCCEEDED(aBranch->GetBoolPref(aPref, &temp))) {
      *aVal = temp;
    }
  }
}

void
MediaManager::GetPrefs(nsIPrefBranch *aBranch, const char *aData)
{
  GetPref(aBranch, "media.navigator.video.default_width", aData, &mPrefs.mWidth);
  GetPref(aBranch, "media.navigator.video.default_height", aData, &mPrefs.mHeight);
  GetPref(aBranch, "media.navigator.video.default_fps", aData, &mPrefs.mFPS);
  GetPref(aBranch, "media.navigator.video.default_minfps", aData, &mPrefs.mMinFPS);
  GetPref(aBranch, "media.navigator.audio.fake_frequency", aData, &mPrefs.mFreq);
#ifdef MOZ_WEBRTC
  GetPrefBool(aBranch, "media.getusermedia.aec_enabled", aData, &mPrefs.mAecOn);
  GetPrefBool(aBranch, "media.getusermedia.agc_enabled", aData, &mPrefs.mAgcOn);
  GetPrefBool(aBranch, "media.getusermedia.noise_enabled", aData, &mPrefs.mNoiseOn);
  GetPref(aBranch, "media.getusermedia.aec", aData, &mPrefs.mAec);
  GetPref(aBranch, "media.getusermedia.agc", aData, &mPrefs.mAgc);
  GetPref(aBranch, "media.getusermedia.noise", aData, &mPrefs.mNoise);
  GetPref(aBranch, "media.getusermedia.playout_delay", aData, &mPrefs.mPlayoutDelay);
  GetPrefBool(aBranch, "media.getusermedia.aec_extended_filter", aData, &mPrefs.mExtendedFilter);
  GetPrefBool(aBranch, "media.getusermedia.aec_aec_delay_agnostic", aData, &mPrefs.mDelayAgnostic);
#endif
  GetPrefBool(aBranch, "media.navigator.audio.full_duplex", aData, &mPrefs.mFullDuplex);
}

void
MediaManager::Shutdown()
{
  MOZ_ASSERT(NS_IsMainThread());
  if (sInShutdown) {
    return;
  }
  sInShutdown = true;

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();

  obs->RemoveObserver(this, "xpcom-will-shutdown");
  obs->RemoveObserver(this, "last-pb-context-exited");
  obs->RemoveObserver(this, "getUserMedia:privileged:allow");
  obs->RemoveObserver(this, "getUserMedia:response:allow");
  obs->RemoveObserver(this, "getUserMedia:response:deny");
  obs->RemoveObserver(this, "getUserMedia:revoke");

  nsCOMPtr<nsIPrefBranch> prefs = do_GetService(NS_PREFSERVICE_CONTRACTID);
  if (prefs) {
    prefs->RemoveObserver("media.navigator.video.default_width", this);
    prefs->RemoveObserver("media.navigator.video.default_height", this);
    prefs->RemoveObserver("media.navigator.video.default_fps", this);
    prefs->RemoveObserver("media.navigator.video.default_minfps", this);
    prefs->RemoveObserver("media.navigator.audio.fake_frequency", this);
#ifdef MOZ_WEBRTC
    prefs->RemoveObserver("media.getusermedia.aec_enabled", this);
    prefs->RemoveObserver("media.getusermedia.aec", this);
    prefs->RemoveObserver("media.getusermedia.agc_enabled", this);
    prefs->RemoveObserver("media.getusermedia.agc", this);
    prefs->RemoveObserver("media.getusermedia.noise_enabled", this);
    prefs->RemoveObserver("media.getusermedia.noise", this);
    prefs->RemoveObserver("media.getusermedia.playout_delay", this);
#endif
    prefs->RemoveObserver("media.navigator.audio.full_duplex", this);
  }

  // Close off any remaining active windows.
  GetActiveWindows()->Clear();
  mActiveCallbacks.Clear();
  mCallIds.Clear();
#ifdef MOZ_WEBRTC
  StopWebRtcLog();
#endif

  // Because mMediaThread is not an nsThread, we must dispatch to it so it can
  // clean up BackgroundChild. Continue stopping thread once this is done.

  class ShutdownTask : public Runnable
  {
  public:
    ShutdownTask(MediaManager* aManager,
                 already_AddRefed<Runnable> aReply)
      : mManager(aManager)
      , mReply(aReply) {}
  private:
    NS_IMETHOD
    Run() override
    {
      LOG(("MediaManager Thread Shutdown"));
      MOZ_ASSERT(MediaManager::IsInMediaThread());
      // Must shutdown backend on MediaManager thread, since that's where we started it from!
      {
        if (mManager->mBackend) {
          mManager->mBackend->Shutdown(); // ok to invoke multiple times
        }
      }
      mozilla::ipc::BackgroundChild::CloseForCurrentThread();
      // must explicitly do this before dispatching the reply, since the reply may kill us with Stop()
      mManager->mBackend = nullptr; // last reference, will invoke Shutdown() again

      if (NS_FAILED(NS_DispatchToMainThread(mReply.forget()))) {
        LOG(("Will leak thread: DispatchToMainthread of reply runnable failed in MediaManager shutdown"));
      }

      return NS_OK;
    }
    RefPtr<MediaManager> mManager;
    RefPtr<Runnable> mReply;
  };

  // Post ShutdownTask to execute on mMediaThread and pass in a lambda
  // callback to be executed back on this thread once it is done.
  //
  // The lambda callback "captures" the 'this' pointer for member access.
  // This is safe since this is guaranteed to be here since sSingleton isn't
  // cleared until the lambda function clears it.

  // note that this == sSingleton
  RefPtr<MediaManager> that(sSingleton);
  // Release the backend (and call Shutdown()) from within the MediaManager thread
  // Don't use MediaManager::PostTask() because we're sInShutdown=true here!
  RefPtr<ShutdownTask> shutdown = new ShutdownTask(this,
      media::NewRunnableFrom([this, that]() mutable {
    LOG(("MediaManager shutdown lambda running, releasing MediaManager singleton and thread"));
    if (mMediaThread) {
      mMediaThread->Stop();
    }

    // Remove async shutdown blocker

    nsCOMPtr<nsIAsyncShutdownClient> shutdownPhase = GetShutdownPhase();
    shutdownPhase->RemoveBlocker(sSingleton->mShutdownBlocker);

    // we hold a ref to 'that' which is the same as sSingleton
    sSingleton = nullptr;

    return NS_OK;
  }));
  mMediaThread->message_loop()->PostTask(shutdown.forget());
}

nsresult
MediaManager::Observe(nsISupports* aSubject, const char* aTopic,
  const char16_t* aData)
{
  MOZ_ASSERT(NS_IsMainThread());

  if (!strcmp(aTopic, NS_PREFBRANCH_PREFCHANGE_TOPIC_ID)) {
    nsCOMPtr<nsIPrefBranch> branch( do_QueryInterface(aSubject) );
    if (branch) {
      GetPrefs(branch,NS_ConvertUTF16toUTF8(aData).get());
      LOG(("%s: %dx%d @%dfps (min %d)", __FUNCTION__,
           mPrefs.mWidth, mPrefs.mHeight, mPrefs.mFPS, mPrefs.mMinFPS));
    }
  } else if (!strcmp(aTopic, "xpcom-will-shutdown")) {
    Shutdown();
    return NS_OK;
  } else if (!strcmp(aTopic, "last-pb-context-exited")) {
    // Clear memory of private-browsing-specific deviceIds. Fire and forget.
    media::SanitizeOriginKeys(0, true);
    return NS_OK;
  } else if (!strcmp(aTopic, "getUserMedia:privileged:allow") ||
             !strcmp(aTopic, "getUserMedia:response:allow")) {
    nsString key(aData);
    RefPtr<GetUserMediaTask> task;
    mActiveCallbacks.Remove(key, getter_AddRefs(task));
    if (!task) {
      return NS_OK;
    }

    if (aSubject) {
      // A particular device or devices were chosen by the user.
      // NOTE: does not allow setting a device to null; assumes nullptr
      nsCOMPtr<nsISupportsArray> array(do_QueryInterface(aSubject));
      MOZ_ASSERT(array);
      uint32_t len = 0;
      array->Count(&len);
      if (!len) {
        // neither audio nor video were selected
        task->Denied(NS_LITERAL_STRING("SecurityError"));
        return NS_OK;
      }
      bool videoFound = false, audioFound = false;
      for (uint32_t i = 0; i < len; i++) {
        nsCOMPtr<nsISupports> supports;
        array->GetElementAt(i,getter_AddRefs(supports));
        nsCOMPtr<nsIMediaDevice> device(do_QueryInterface(supports));
        MOZ_ASSERT(device); // shouldn't be returning anything else...
        if (device) {
          nsString type;
          device->GetType(type);
          if (type.EqualsLiteral("video")) {
            if (!videoFound) {
              task->SetVideoDevice(static_cast<VideoDevice*>(device.get()));
              videoFound = true;
            }
          } else if (type.EqualsLiteral("audio")) {
            if (!audioFound) {
              task->SetAudioDevice(static_cast<AudioDevice*>(device.get()));
              audioFound = true;
            }
          } else {
            NS_WARNING("Unknown device type in getUserMedia");
          }
        }
      }
    }

    if (sInShutdown) {
      return task->Denied(NS_LITERAL_STRING("In shutdown"));
    }
    // Reuse the same thread to save memory.
    MediaManager::PostTask(task.forget());
    return NS_OK;

  } else if (!strcmp(aTopic, "getUserMedia:response:deny")) {
    nsString errorMessage(NS_LITERAL_STRING("SecurityError"));

    if (aSubject) {
      nsCOMPtr<nsISupportsString> msg(do_QueryInterface(aSubject));
      MOZ_ASSERT(msg);
      msg->GetData(errorMessage);
      if (errorMessage.IsEmpty())
        errorMessage.AssignLiteral(MOZ_UTF16("InternalError"));
    }

    nsString key(aData);
    RefPtr<GetUserMediaTask> task;
    mActiveCallbacks.Remove(key, getter_AddRefs(task));
    if (task) {
      task->Denied(errorMessage);
    }
    return NS_OK;

  } else if (!strcmp(aTopic, "getUserMedia:revoke")) {
    nsresult rv;
    // may be windowid or screen:windowid
    nsDependentString data(aData);
    if (Substring(data, 0, strlen("screen:")).EqualsLiteral("screen:")) {
      uint64_t windowID = PromiseFlatString(Substring(data, strlen("screen:"))).ToInteger64(&rv);
      MOZ_ASSERT(NS_SUCCEEDED(rv));
      if (NS_SUCCEEDED(rv)) {
        LOG(("Revoking Screen/windowCapture access for window %llu", windowID));
        StopScreensharing(windowID);
      }
    } else {
      uint64_t windowID = nsString(aData).ToInteger64(&rv);
      MOZ_ASSERT(NS_SUCCEEDED(rv));
      if (NS_SUCCEEDED(rv)) {
        LOG(("Revoking MediaCapture access for window %llu", windowID));
        OnNavigation(windowID);
      }
    }
    return NS_OK;
  }
#ifdef MOZ_WIDGET_GONK
  else if (!strcmp(aTopic, "phone-state-changed")) {
    nsString state(aData);
    nsresult rv;
    uint32_t phoneState = state.ToInteger(&rv);

    if (NS_SUCCEEDED(rv) && phoneState == nsIAudioManager::PHONE_STATE_IN_CALL) {
      StopMediaStreams();
    }
    return NS_OK;
  }
#endif

  return NS_OK;
}

nsresult
MediaManager::GetActiveMediaCaptureWindows(nsISupportsArray** aArray)
{
  MOZ_ASSERT(aArray);
  nsISupportsArray* array;
  nsresult rv = NS_NewISupportsArray(&array); // AddRefs
  if (NS_FAILED(rv)) {
    return rv;
  }

  for (auto iter = mActiveWindows.Iter(); !iter.Done(); iter.Next()) {
    const uint64_t& id = iter.Key();
    StreamListeners* listeners = iter.UserData();

    nsPIDOMWindow* window = static_cast<nsPIDOMWindow*>
      (nsGlobalWindow::GetInnerWindowWithId(id));
    MOZ_ASSERT(window);
    if (!window) {
      continue;
    }
    // mActiveWindows contains both windows that have requested device
    // access and windows that are currently capturing media. We want
    // to return only the latter. See bug 975177.
    bool capturing = false;
    if (listeners) {
      uint32_t length = listeners->Length();
      for (uint32_t i = 0; i < length; ++i) {
        RefPtr<GetUserMediaCallbackMediaStreamListener> listener =
          listeners->ElementAt(i);
        if (listener->CapturingVideo() || listener->CapturingAudio() ||
            listener->CapturingScreen() || listener->CapturingWindow() ||
            listener->CapturingApplication()) {
          capturing = true;
          break;
        }
      }
    }
    if (capturing) {
      array->AppendElement(window);
    }
  }

  *aArray = array;
  return NS_OK;
}

// XXX flags might be better...
struct CaptureWindowStateData {
  bool *mVideo;
  bool *mAudio;
  bool *mScreenShare;
  bool *mWindowShare;
  bool *mAppShare;
  bool *mBrowserShare;
};

static void
CaptureWindowStateCallback(MediaManager *aThis,
                           uint64_t aWindowID,
                           StreamListeners *aListeners,
                           void *aData)
{
  struct CaptureWindowStateData *data = (struct CaptureWindowStateData *) aData;

  if (aListeners) {
    auto length = aListeners->Length();
    for (size_t i = 0; i < length; ++i) {
      GetUserMediaCallbackMediaStreamListener *listener = aListeners->ElementAt(i);

      if (listener->CapturingVideo()) {
        *data->mVideo = true;
      }
      if (listener->CapturingAudio()) {
        *data->mAudio = true;
      }
      if (listener->CapturingScreen()) {
        *data->mScreenShare = true;
      }
      if (listener->CapturingWindow()) {
        *data->mWindowShare = true;
      }
      if (listener->CapturingApplication()) {
        *data->mAppShare = true;
      }
      if (listener->CapturingBrowser()) {
        *data->mBrowserShare = true;
      }
    }
  }
}


NS_IMETHODIMP
MediaManager::MediaCaptureWindowState(nsIDOMWindow* aWindow, bool* aVideo,
                                      bool* aAudio, bool *aScreenShare,
                                      bool* aWindowShare, bool *aAppShare,
                                      bool *aBrowserShare)
{
  MOZ_ASSERT(NS_IsMainThread());
  struct CaptureWindowStateData data;
  data.mVideo = aVideo;
  data.mAudio = aAudio;
  data.mScreenShare = aScreenShare;
  data.mWindowShare = aWindowShare;
  data.mAppShare = aAppShare;
  data.mBrowserShare = aBrowserShare;

  *aVideo = false;
  *aAudio = false;
  *aScreenShare = false;
  *aWindowShare = false;
  *aAppShare = false;
  *aBrowserShare = false;

  nsCOMPtr<nsPIDOMWindow> piWin = do_QueryInterface(aWindow);
  if (piWin) {
    IterateWindowListeners(piWin, CaptureWindowStateCallback, &data);
  }
#ifdef DEBUG
  LOG(("%s: window %lld capturing %s %s %s %s %s %s", __FUNCTION__, piWin ? piWin->WindowID() : -1,
       *aVideo ? "video" : "", *aAudio ? "audio" : "",
       *aScreenShare ? "screenshare" : "",  *aWindowShare ? "windowshare" : "",
       *aAppShare ? "appshare" : "", *aBrowserShare ? "browsershare" : ""));
#endif
  return NS_OK;
}

NS_IMETHODIMP
MediaManager::SanitizeDeviceIds(int64_t aSinceWhen)
{
  MOZ_ASSERT(NS_IsMainThread());
  LOG(("%s: sinceWhen = %llu", __FUNCTION__, aSinceWhen));

  media::SanitizeOriginKeys(aSinceWhen, false); // we fire and forget
  return NS_OK;
}

static void
StopScreensharingCallback(MediaManager *aThis,
                          uint64_t aWindowID,
                          StreamListeners *aListeners,
                          void *aData)
{
  if (aListeners) {
    auto length = aListeners->Length();
    for (size_t i = 0; i < length; ++i) {
      aListeners->ElementAt(i)->StopSharing();
    }
  }
}

void
MediaManager::StopScreensharing(uint64_t aWindowID)
{
  // We need to stop window/screensharing for all streams in all innerwindows that
  // correspond to that outerwindow.

  nsPIDOMWindow *window = static_cast<nsPIDOMWindow*>
      (nsGlobalWindow::GetInnerWindowWithId(aWindowID));
  if (!window) {
    return;
  }
  IterateWindowListeners(window, &StopScreensharingCallback, nullptr);
}

// lets us do all sorts of things to the listeners
void
MediaManager::IterateWindowListeners(nsPIDOMWindow *aWindow,
                                     WindowListenerCallback aCallback,
                                     void *aData)
{
  // Iterate the docshell tree to find all the child windows, and for each
  // invoke the callback
  nsCOMPtr<nsPIDOMWindow> piWin = do_QueryInterface(aWindow);
  if (piWin) {
    if (piWin->IsInnerWindow() || piWin->GetCurrentInnerWindow()) {
      uint64_t windowID;
      if (piWin->IsInnerWindow()) {
        windowID = piWin->WindowID();
      } else {
        windowID = piWin->GetCurrentInnerWindow()->WindowID();
      }
      StreamListeners* listeners = GetActiveWindows()->Get(windowID);
      // pass listeners so it can modify/delete the list
      (*aCallback)(this, windowID, listeners, aData);
    }

    // iterate any children of *this* window (iframes, etc)
    nsCOMPtr<nsIDocShell> docShell = piWin->GetDocShell();
    if (docShell) {
      int32_t i, count;
      docShell->GetChildCount(&count);
      for (i = 0; i < count; ++i) {
        nsCOMPtr<nsIDocShellTreeItem> item;
        docShell->GetChildAt(i, getter_AddRefs(item));
        nsCOMPtr<nsPIDOMWindow> win = item ? item->GetWindow() : nullptr;

        if (win) {
          IterateWindowListeners(win, aCallback, aData);
        }
      }
    }
  }
}


void
MediaManager::StopMediaStreams()
{
  nsCOMPtr<nsISupportsArray> array;
  GetActiveMediaCaptureWindows(getter_AddRefs(array));
  uint32_t len;
  array->Count(&len);
  for (uint32_t i = 0; i < len; i++) {
    nsCOMPtr<nsISupports> window;
    array->GetElementAt(i, getter_AddRefs(window));
    nsCOMPtr<nsPIDOMWindow> win(do_QueryInterface(window));
    if (win) {
      OnNavigation(win->WindowID());
    }
  }
}

bool
MediaManager::IsActivelyCapturingOrHasAPermission(uint64_t aWindowId)
{
  // Does page currently have a gUM stream active?

  nsCOMPtr<nsISupportsArray> array;
  GetActiveMediaCaptureWindows(getter_AddRefs(array));
  uint32_t len;
  array->Count(&len);
  for (uint32_t i = 0; i < len; i++) {
    nsCOMPtr<nsISupports> window;
    array->GetElementAt(i, getter_AddRefs(window));
    nsCOMPtr<nsPIDOMWindow> win(do_QueryInterface(window));
    if (win && win->WindowID() == aWindowId) {
      return true;
    }
  }

  // Or are persistent permissions (audio or video) granted?

  nsPIDOMWindow *window = static_cast<nsPIDOMWindow*>
      (nsGlobalWindow::GetInnerWindowWithId(aWindowId));
  if (NS_WARN_IF(!window)) {
    return false;
  }
  // Check if this site has persistent permissions.
  nsresult rv;
  nsCOMPtr<nsIPermissionManager> mgr =
    do_GetService(NS_PERMISSIONMANAGER_CONTRACTID, &rv);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false; // no permission manager no permissions!
  }

  uint32_t audio = nsIPermissionManager::UNKNOWN_ACTION;
  uint32_t video = nsIPermissionManager::UNKNOWN_ACTION;
  {
    auto* principal = window->GetExtantDoc()->NodePrincipal();
    rv = mgr->TestExactPermissionFromPrincipal(principal, "microphone", &audio);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return false;
    }
    rv = mgr->TestExactPermissionFromPrincipal(principal, "camera", &video);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return false;
    }
  }
  return audio == nsIPermissionManager::ALLOW_ACTION ||
         video == nsIPermissionManager::ALLOW_ACTION;
}

void
GetUserMediaCallbackMediaStreamListener::Stop()
{
  MOZ_ASSERT(NS_IsMainThread(), "Only call on main thread");
  if (mStopped) {
    return;
  }

  // We can't take a chance on blocking here, so proxy this to another
  // thread.
  // Pass a ref to us (which is threadsafe) so it can query us for the
  // source stream info.
  RefPtr<MediaOperationTask> mediaOperation =
    new MediaOperationTask(MEDIA_STOP,
                           this, nullptr, nullptr,
                           !mAudioStopped ? mAudioDevice.get() : nullptr,
                           !mVideoStopped ? mVideoDevice.get() : nullptr,
                           false, mWindowID, nullptr);
  MediaManager::PostTask(mediaOperation.forget());
  mStopped = mAudioStopped = mVideoStopped = true;
}

// Doesn't kill audio
void
GetUserMediaCallbackMediaStreamListener::StopSharing()
{
  MOZ_ASSERT(NS_IsMainThread());
  if (mVideoDevice &&
      (mVideoDevice->GetMediaSource() == MediaSourceEnum::Screen ||
       mVideoDevice->GetMediaSource() == MediaSourceEnum::Application ||
       mVideoDevice->GetMediaSource() == MediaSourceEnum::Window)) {
    // We want to stop the whole stream if there's no audio;
    // just the video track if we have both.
    // StopTrack figures this out for us.
    StopTrack(kVideoTrack);
  } else if (mAudioDevice &&
             mAudioDevice->GetMediaSource() == MediaSourceEnum::AudioCapture) {
    nsCOMPtr<nsPIDOMWindow> window = nsGlobalWindow::GetInnerWindowWithId(mWindowID);
    MOZ_ASSERT(window);
    window->SetAudioCapture(false);
    MediaStreamGraph* graph =
      MediaStreamGraph::GetInstance(MediaStreamGraph::AUDIO_THREAD_DRIVER,
                                    dom::AudioChannel::Normal);
    graph->UnregisterCaptureStreamForWindow(mWindowID);
    mStream->Destroy();
  }
}

// ApplyConstraints for track

already_AddRefed<GetUserMediaCallbackMediaStreamListener::PledgeVoid>
GetUserMediaCallbackMediaStreamListener::ApplyConstraintsToTrack(
    nsPIDOMWindow* aWindow,
    TrackID aTrackID,
    const MediaTrackConstraints& aConstraints)
{
  MOZ_ASSERT(NS_IsMainThread());
  RefPtr<PledgeVoid> p = new PledgeVoid();

  // XXX to support multiple tracks of a type in a stream, this should key off
  // the TrackID and not just the type
  RefPtr<AudioDevice> audioDevice =
    aTrackID == kAudioTrack ? mAudioDevice.get() : nullptr;
  RefPtr<VideoDevice> videoDevice =
    aTrackID == kVideoTrack ? mVideoDevice.get() : nullptr;

  if (mStopped || (!audioDevice && !videoDevice))
  {
    LOG(("gUM track %d applyConstraints, but we don't have type %s",
         aTrackID, aTrackID == kAudioTrack ? "audio" : "video"));
    p->Resolve(false);
    return p.forget();
  }

  RefPtr<MediaManager> mgr = MediaManager::GetInstance();
  uint32_t id = mgr->mOutstandingVoidPledges.Append(*p);
  uint64_t windowId = aWindow->WindowID();

  MediaManager::PostTask(NewTaskFrom([id, windowId,
                                      audioDevice, videoDevice,
                                      aConstraints]() mutable {
    MOZ_ASSERT(MediaManager::IsInMediaThread());
    RefPtr<MediaManager> mgr = MediaManager::GetInstance();
    const char* badConstraint = nullptr;
    nsresult rv = NS_OK;

    if (audioDevice) {
      rv = audioDevice->Restart(aConstraints, mgr->mPrefs);
      if (rv == NS_ERROR_NOT_AVAILABLE) {
        nsTArray<RefPtr<AudioDevice>> audios;
        audios.AppendElement(audioDevice);
        badConstraint = MediaConstraintsHelper::SelectSettings(aConstraints,
                                                               audios);
      }
    } else {
      rv = videoDevice->Restart(aConstraints, mgr->mPrefs);
      if (rv == NS_ERROR_NOT_AVAILABLE) {
        nsTArray<RefPtr<VideoDevice>> videos;
        videos.AppendElement(videoDevice);
        badConstraint = MediaConstraintsHelper::SelectSettings(aConstraints,
                                                               videos);
      }
    }
    NS_DispatchToMainThread(NewRunnableFrom([id, windowId, rv,
                                             badConstraint]() mutable {
      MOZ_ASSERT(NS_IsMainThread());
      RefPtr<MediaManager> mgr = MediaManager_GetInstance();
      if (!mgr) {
        return NS_OK;
      }
      RefPtr<PledgeVoid> p = mgr->mOutstandingVoidPledges.Remove(id);
      if (p) {
        if (NS_SUCCEEDED(rv)) {
          p->Resolve(false);
        } else {
          nsPIDOMWindow *window = static_cast<nsPIDOMWindow*>
              (nsGlobalWindow::GetInnerWindowWithId(windowId));
          if (window) {
            if (rv == NS_ERROR_NOT_AVAILABLE) {
              nsString constraint;
              constraint.AssignASCII(badConstraint);
              RefPtr<MediaStreamError> error =
                  new MediaStreamError(window,
                                       NS_LITERAL_STRING("OverconstrainedError"),
                                       NS_LITERAL_STRING(""),
                                       constraint);
              p->Reject(error);
            } else {
              RefPtr<MediaStreamError> error =
                  new MediaStreamError(window,
                                       NS_LITERAL_STRING("InternalError"));
              p->Reject(error);
            }
          }
        }
      }
      return NS_OK;
    }));
  }));
  return p.forget();
}

// Stop backend for track

void
GetUserMediaCallbackMediaStreamListener::StopTrack(TrackID aTrackID)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aTrackID == kAudioTrack || aTrackID == kVideoTrack);

  // XXX to support multiple tracks of a type in a stream, this should key off
  // the TrackID and not just hard coded values.

  bool stopAudio = aTrackID == kAudioTrack;
  bool stopVideo = aTrackID == kVideoTrack;

  if (mStopped ||
      (stopAudio && (mAudioStopped || !mAudioDevice)) ||
      (stopVideo && (mVideoStopped || !mVideoDevice)))
  {
    LOG(("Can't stop gUM track %d (%s), exists=%d, stopped=%d",
         aTrackID,
         aTrackID == kAudioTrack ? "audio" : "video",
         aTrackID == kAudioTrack ? !!mAudioDevice : !!mVideoDevice,
         aTrackID == kAudioTrack ? mAudioStopped : mVideoStopped));
    return;
  }

  if ((stopAudio || mAudioStopped || !mAudioDevice) &&
      (stopVideo || mVideoStopped || !mVideoDevice)) {
    Stop();
    return;
  }

  RefPtr<MediaOperationTask> mediaOperation =
    new MediaOperationTask(MEDIA_STOP_TRACK,
                           this, nullptr, nullptr,
                           stopAudio ? mAudioDevice.get() : nullptr,
                           stopVideo ? mVideoDevice.get() : nullptr,
                           false , mWindowID, nullptr);
  MediaManager::PostTask(mediaOperation.forget());
  mAudioStopped |= stopAudio;
  mVideoStopped |= stopVideo;
}

void
GetUserMediaCallbackMediaStreamListener::NotifyFinished()
{
  MOZ_ASSERT(NS_IsMainThread());
  mFinished = true;
  Stop(); // we know it's been activated

  RefPtr<MediaManager> manager(MediaManager::GetIfExists());
  if (manager) {
    manager->RemoveFromWindowList(mWindowID, this);
  } else {
    NS_WARNING("Late NotifyFinished after MediaManager shutdown");
  }
}

// Called from the MediaStreamGraph thread
void
GetUserMediaCallbackMediaStreamListener::NotifyDirectListeners(MediaStreamGraph* aGraph,
                                                               bool aHasListeners)
{
  RefPtr<MediaOperationTask> mediaOperation =
    new MediaOperationTask(MEDIA_DIRECT_LISTENERS,
                           this, nullptr, nullptr,
                           mAudioDevice, mVideoDevice,
                           aHasListeners, mWindowID, nullptr);
  MediaManager::PostTask(mediaOperation.forget());
}

// this can be in response to our own RemoveListener() (via ::Remove()), or
// because the DOM GC'd the DOMLocalMediaStream/etc we're attached to.
void
GetUserMediaCallbackMediaStreamListener::NotifyRemoved()
{
  MOZ_ASSERT(NS_IsMainThread());
  MM_LOG(("Listener removed by DOM Destroy(), mFinished = %d", (int) mFinished));
  mRemoved = true;

  if (!mFinished) {
    NotifyFinished();
  }
}

NS_IMETHODIMP
GetUserMediaNotificationEvent::Run()
{
  MOZ_ASSERT(NS_IsMainThread());
  // Make sure mStream is cleared and our reference to the DOMMediaStream
  // is dropped on the main thread, no matter what happens in this method.
  // Otherwise this object might be destroyed off the main thread,
  // releasing DOMMediaStream off the main thread, which is not allowed.
  RefPtr<DOMMediaStream> stream = mStream.forget();

  nsString msg;
  switch (mStatus) {
  case STARTING:
    msg = NS_LITERAL_STRING("starting");
    stream->OnTracksAvailable(mOnTracksAvailableCallback.forget());
    break;
  case STOPPING:
  case STOPPED_TRACK:
    msg = NS_LITERAL_STRING("shutdown");
    break;
  }

  nsCOMPtr<nsPIDOMWindow> window = nsGlobalWindow::GetInnerWindowWithId(mWindowID);
  NS_ENSURE_TRUE(window, NS_ERROR_FAILURE);

  return MediaManager::NotifyRecordingStatusChange(window, msg, mIsAudio, mIsVideo);
}

} // namespace mozilla
