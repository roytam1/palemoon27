/* -*- Mode: c++; tab-width: 40; indent-tabs-mode: nil; c-basic-offset: 4; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsAppShell.h"

#include "base/basictypes.h"
#include "base/message_loop.h"
#include "base/task.h"
#include "mozilla/Hal.h"
#include "nsIScreen.h"
#include "nsIScreenManager.h"
#include "nsWindow.h"
#include "nsThreadUtils.h"
#include "nsICommandLineRunner.h"
#include "nsIObserverService.h"
#include "nsIAppStartup.h"
#include "nsIGeolocationProvider.h"
#include "nsCacheService.h"
#include "nsIDOMEventListener.h"
#include "nsIDOMClientRectList.h"
#include "nsIDOMClientRect.h"
#include "nsIDOMWakeLockListener.h"
#include "nsIPowerManagerService.h"
#include "nsINetworkLinkService.h"
#include "nsCategoryManagerUtils.h"

#include "mozilla/BackgroundHangMonitor.h"
#include "mozilla/Services.h"
#include "mozilla/unused.h"
#include "mozilla/Preferences.h"
#include "mozilla/Hal.h"
#include "prenv.h"

#include "AndroidBridge.h"
#include "AndroidBridgeUtilities.h"
#include <android/log.h>
#include <pthread.h>
#include <wchar.h>

#include "mozilla/dom/ScreenOrientation.h"
#ifdef MOZ_GAMEPAD
#include "mozilla/dom/GamepadService.h"
#endif

#include "GoannaProfiler.h"
#ifdef MOZ_ANDROID_HISTORY
#include "nsNetUtil.h"
#include "IHistory.h"
#endif

#ifdef MOZ_LOGGING
#include "prlog.h"
#endif

#ifdef DEBUG_ANDROID_EVENTS
#define EVLOG(args...)  ALOG(args)
#else
#define EVLOG(args...) do { } while (0)
#endif

using namespace mozilla;

#ifdef PR_LOGGING
PRLogModuleInfo *gWidgetLog = nullptr;
#endif

nsIGeolocationUpdate *gLocationCallback = nullptr;
nsAutoPtr<mozilla::AndroidGoannaEvent> gLastSizeChange;

nsAppShell *nsAppShell::gAppShell = nullptr;

NS_IMPL_ISUPPORTS_INHERITED(nsAppShell, nsBaseAppShell, nsIObserver)

class ThumbnailRunnable : public nsRunnable {
public:
    ThumbnailRunnable(nsIAndroidBrowserApp* aBrowserApp, int aTabId,
                       const nsTArray<nsIntPoint>& aPoints, RefCountedJavaObject* aBuffer):
        mBrowserApp(aBrowserApp), mPoints(aPoints), mTabId(aTabId), mBuffer(aBuffer) {}

    virtual nsresult Run() {
        const auto& buffer = jni::Object::Ref::From(mBuffer->GetObject());
        nsCOMPtr<nsIDOMWindow> domWindow;
        nsCOMPtr<nsIBrowserTab> tab;
        mBrowserApp->GetBrowserTab(mTabId, getter_AddRefs(tab));
        if (!tab) {
            widget::ThumbnailHelper::SendThumbnail(buffer, mTabId, false, false);
            return NS_ERROR_FAILURE;
        }

        tab->GetWindow(getter_AddRefs(domWindow));
        if (!domWindow) {
            widget::ThumbnailHelper::SendThumbnail(buffer, mTabId, false, false);
            return NS_ERROR_FAILURE;
        }

        NS_ASSERTION(mPoints.Length() == 1, "Thumbnail event does not have enough coordinates");

        bool shouldStore = true;
        nsresult rv = AndroidBridge::Bridge()->CaptureThumbnail(domWindow, mPoints[0].x, mPoints[0].y, mTabId, buffer, shouldStore);
        widget::ThumbnailHelper::SendThumbnail(buffer, mTabId, NS_SUCCEEDED(rv), shouldStore);
        return rv;
    }
private:
    nsCOMPtr<nsIAndroidBrowserApp> mBrowserApp;
    nsTArray<nsIntPoint> mPoints;
    int mTabId;
    nsRefPtr<RefCountedJavaObject> mBuffer;
};

class WakeLockListener final : public nsIDOMMozWakeLockListener {
private:
  ~WakeLockListener() {}

public:
  NS_DECL_ISUPPORTS;

  nsresult Callback(const nsAString& topic, const nsAString& state) {
    widget::GoannaAppShell::NotifyWakeLockChanged(topic, state);
    return NS_OK;
  }
};

NS_IMPL_ISUPPORTS(WakeLockListener, nsIDOMMozWakeLockListener)
nsCOMPtr<nsIPowerManagerService> sPowerManagerService = nullptr;
StaticRefPtr<WakeLockListener> sWakeLockListener;

nsAppShell::nsAppShell()
    : mQueueLock("nsAppShell.mQueueLock"),
      mCondLock("nsAppShell.mCondLock"),
      mQueueCond(mCondLock, "nsAppShell.mQueueCond"),
      mQueuedViewportEvent(nullptr)
{
    gAppShell = this;

    if (XRE_GetProcessType() != GoannaProcessType_Default) {
        return;
    }

    sPowerManagerService = do_GetService(POWERMANAGERSERVICE_CONTRACTID);

    if (sPowerManagerService) {
        sWakeLockListener = new WakeLockListener();
    } else {
        NS_WARNING("Failed to retrieve PowerManagerService, wakelocks will be broken!");
    }

}

nsAppShell::~nsAppShell()
{
    gAppShell = nullptr;

    if (sPowerManagerService) {
        sPowerManagerService->RemoveWakeLockListener(sWakeLockListener);

        sPowerManagerService = nullptr;
        sWakeLockListener = nullptr;
    }
}

void
nsAppShell::NotifyNativeEvent()
{
    MutexAutoLock lock(mCondLock);
    mQueueCond.Notify();
}

#define PREFNAME_COALESCE_TOUCHES "dom.event.touch.coalescing.enabled"
static const char* kObservedPrefs[] = {
  PREFNAME_COALESCE_TOUCHES,
  nullptr
};

nsresult
nsAppShell::Init()
{
#ifdef PR_LOGGING
    if (!gWidgetLog)
        gWidgetLog = PR_NewLogModule("Widget");
#endif

    nsresult rv = nsBaseAppShell::Init();
    nsCOMPtr<nsIObserverService> obsServ =
        mozilla::services::GetObserverService();
    if (obsServ) {
        obsServ->AddObserver(this, "xpcom-shutdown", false);
        obsServ->AddObserver(this, "browser-delayed-startup-finished", false);
    }

    if (sPowerManagerService)
        sPowerManagerService->AddWakeLockListener(sWakeLockListener);

    Preferences::AddStrongObservers(this, kObservedPrefs);
    mAllowCoalescingTouches = Preferences::GetBool(PREFNAME_COALESCE_TOUCHES, true);
    return rv;
}

NS_IMETHODIMP
nsAppShell::Observe(nsISupports* aSubject,
                    const char* aTopic,
                    const char16_t* aData)
{
    if (!strcmp(aTopic, "xpcom-shutdown")) {
        // We need to ensure no observers stick around after XPCOM shuts down
        // or we'll see crashes, as the app shell outlives XPConnect.
        mObserversHash.Clear();
        return nsBaseAppShell::Observe(aSubject, aTopic, aData);
    } else if (!strcmp(aTopic, NS_PREFBRANCH_PREFCHANGE_TOPIC_ID) &&
               aData &&
               nsDependentString(aData).Equals(NS_LITERAL_STRING(PREFNAME_COALESCE_TOUCHES))) {
        mAllowCoalescingTouches = Preferences::GetBool(PREFNAME_COALESCE_TOUCHES, true);
        return NS_OK;
    } else if (!strcmp(aTopic, "browser-delayed-startup-finished")) {
        NS_CreateServicesFromCategory("browser-delayed-startup-finished", nullptr,
                                      "browser-delayed-startup-finished");
    }
    return NS_OK;
}

void
nsAppShell::ScheduleNativeEventCallback()
{
    EVLOG("nsAppShell::ScheduleNativeEventCallback pth: %p thread: %p main: %d", (void*) pthread_self(), (void*) NS_GetCurrentThread(), NS_IsMainThread());

    // this is valid to be called from any thread, so do so.
    PostEvent(AndroidGoannaEvent::MakeNativePoke());
}

bool
nsAppShell::ProcessNextNativeEvent(bool mayWait)
{
    EVLOG("nsAppShell::ProcessNextNativeEvent %d", mayWait);

    PROFILER_LABEL("nsAppShell", "ProcessNextNativeEvent",
        js::ProfileEntry::Category::EVENTS);

    nsAutoPtr<AndroidGoannaEvent> curEvent;
    {
        MutexAutoLock lock(mCondLock);

        curEvent = PopNextEvent();
        if (!curEvent && mayWait) {
            PROFILER_LABEL("nsAppShell", "ProcessNextNativeEvent::Wait",
                js::ProfileEntry::Category::EVENTS);

            // hmm, should we really hardcode this 10s?
#if defined(DEBUG_ANDROID_EVENTS)
            PRTime t0, t1;
            EVLOG("nsAppShell: waiting on mQueueCond");
            t0 = PR_Now();
            mQueueCond.Wait(PR_MillisecondsToInterval(10000));
            t1 = PR_Now();
            EVLOG("nsAppShell: wait done, waited %d ms", (int)(t1-t0)/1000);
#else
            mQueueCond.Wait();
#endif

            curEvent = PopNextEvent();
        }
    }

    if (!curEvent)
        return false;

    mozilla::BackgroundHangMonitor().NotifyActivity();

    EVLOG("nsAppShell: event %p %d", (void*)curEvent.get(), curEvent->Type());

    switch (curEvent->Type()) {
    case AndroidGoannaEvent::NATIVE_POKE:
        NativeEventCallback();
        break;

    case AndroidGoannaEvent::SENSOR_EVENT: {
        InfallibleTArray<float> values;
        mozilla::hal::SensorType type = (mozilla::hal::SensorType) curEvent->Flags();

        switch (type) {
          // Bug 938035, transfer HAL data for orientation sensor to meet w3c
          // spec, ex: HAL report alpha=90 means East but alpha=90 means West
          // in w3c spec
          case hal::SENSOR_ORIENTATION:
            values.AppendElement(360 -curEvent->X());
            values.AppendElement(-curEvent->Y());
            values.AppendElement(-curEvent->Z());
            break;
          case hal::SENSOR_LINEAR_ACCELERATION:
          case hal::SENSOR_ACCELERATION:
          case hal::SENSOR_GYROSCOPE:
          case hal::SENSOR_PROXIMITY:
            values.AppendElement(curEvent->X());
            values.AppendElement(curEvent->Y());
            values.AppendElement(curEvent->Z());
            break;

        case hal::SENSOR_LIGHT:
            values.AppendElement(curEvent->X());
            break;

        default:
            __android_log_print(ANDROID_LOG_ERROR,
                                "Goanna", "### SENSOR_EVENT fired, but type wasn't known %d",
                                type);
        }

        const hal::SensorAccuracyType &accuracy = (hal::SensorAccuracyType) curEvent->MetaState();
        hal::SensorData sdata(type, PR_Now(), values, accuracy);
        hal::NotifySensorChange(sdata);
      }
      break;

    case AndroidGoannaEvent::PROCESS_OBJECT: {

      switch (curEvent->Action()) {
      case AndroidGoannaEvent::ACTION_OBJECT_LAYER_CLIENT:
        AndroidBridge::Bridge()->SetLayerClient(
                widget::GoannaLayerClient::Ref::From(curEvent->Object().wrappedObject()));
        break;
      }
      break;
    }

    case AndroidGoannaEvent::LOCATION_EVENT: {
        if (!gLocationCallback)
            break;

        nsGeoPosition* p = curEvent->GeoPosition();
        if (p)
            gLocationCallback->Update(curEvent->GeoPosition());
        else
            NS_WARNING("Received location event without geoposition!");
        break;
    }

    case AndroidGoannaEvent::APP_BACKGROUNDING: {
        nsCOMPtr<nsIObserverService> obsServ =
            mozilla::services::GetObserverService();
        obsServ->NotifyObservers(nullptr, "application-background", nullptr);

        NS_NAMED_LITERAL_STRING(minimize, "heap-minimize");
        obsServ->NotifyObservers(nullptr, "memory-pressure", minimize.get());

        // If we are OOM killed with the disk cache enabled, the entire
        // cache will be cleared (bug 105843), so shut down the cache here
        // and re-init on foregrounding
        if (nsCacheService::GlobalInstance()) {
            nsCacheService::GlobalInstance()->Shutdown();
        }

        // We really want to send a notification like profile-before-change,
        // but profile-before-change ends up shutting some things down instead
        // of flushing data
        nsIPrefService* prefs = Preferences::GetService();
        if (prefs) {
            // reset the crash loop state
            nsCOMPtr<nsIPrefBranch> prefBranch;
            prefs->GetBranch("browser.sessionstore.", getter_AddRefs(prefBranch));
            if (prefBranch)
                prefBranch->SetIntPref("recent_crashes", 0);

            prefs->SavePrefFile(nullptr);
        }
        break;
    }

    case AndroidGoannaEvent::APP_FOREGROUNDING: {
        // If we are OOM killed with the disk cache enabled, the entire
        // cache will be cleared (bug 105843), so shut down cache on backgrounding
        // and re-init here
        if (nsCacheService::GlobalInstance()) {
            nsCacheService::GlobalInstance()->Init();
        }

        // We didn't return from one of our own activities, so restore
        // to foreground status
        nsCOMPtr<nsIObserverService> obsServ =
            mozilla::services::GetObserverService();
        obsServ->NotifyObservers(nullptr, "application-foreground", nullptr);
        break;
    }

    case AndroidGoannaEvent::THUMBNAIL: {
        if (!mBrowserApp)
            break;

        int32_t tabId = curEvent->MetaState();
        const nsTArray<nsIntPoint>& points = curEvent->Points();
        RefCountedJavaObject* buffer = curEvent->ByteBuffer();
        nsRefPtr<ThumbnailRunnable> sr = new ThumbnailRunnable(mBrowserApp, tabId, points, buffer);
        MessageLoop::current()->PostIdleTask(FROM_HERE, NewRunnableMethod(sr.get(), &ThumbnailRunnable::Run));
        break;
    }

    case AndroidGoannaEvent::ZOOMEDVIEW: {
        if (!mBrowserApp)
            break;
        int32_t tabId = curEvent->MetaState();
        const nsTArray<nsIntPoint>& points = curEvent->Points();
        float scaleFactor = (float) curEvent->X();
        nsRefPtr<RefCountedJavaObject> javaBuffer = curEvent->ByteBuffer();
        const auto& mBuffer = jni::Object::Ref::From(javaBuffer->GetObject());

        nsCOMPtr<nsIDOMWindow> domWindow;
        nsCOMPtr<nsIBrowserTab> tab;
        mBrowserApp->GetBrowserTab(tabId, getter_AddRefs(tab));
        if (!tab) {
            NS_ERROR("Can't find tab!");
            break;
        }
        tab->GetWindow(getter_AddRefs(domWindow));
        if (!domWindow) {
            NS_ERROR("Can't find dom window!");
            break;
        }
        NS_ASSERTION(points.Length() == 2, "ZoomedView event does not have enough coordinates");
        nsIntRect r(points[0].x, points[0].y, points[1].x, points[1].y);
        AndroidBridge::Bridge()->CaptureZoomedView(domWindow, r, mBuffer, scaleFactor);
        break;
    }

    case AndroidGoannaEvent::VIEWPORT:
    case AndroidGoannaEvent::BROADCAST: {
        if (curEvent->Characters().Length() == 0)
            break;

        nsCOMPtr<nsIObserverService> obsServ =
            mozilla::services::GetObserverService();

        const NS_ConvertUTF16toUTF8 topic(curEvent->Characters());
        const nsPromiseFlatString& data = PromiseFlatString(curEvent->CharactersExtra());

        obsServ->NotifyObservers(nullptr, topic.get(), data.get());
        break;
    }

    case AndroidGoannaEvent::TELEMETRY_UI_SESSION_STOP: {
        if (curEvent->Characters().Length() == 0)
            break;

        nsCOMPtr<nsIUITelemetryObserver> obs;
        mBrowserApp->GetUITelemetryObserver(getter_AddRefs(obs));
        if (!obs)
            break;

        obs->StopSession(
                nsString(curEvent->Characters()).get(),
                nsString(curEvent->CharactersExtra()).get(),
                curEvent->Time()
                );
        break;
    }

    case AndroidGoannaEvent::TELEMETRY_UI_SESSION_START: {
        if (curEvent->Characters().Length() == 0)
            break;

        nsCOMPtr<nsIUITelemetryObserver> obs;
        mBrowserApp->GetUITelemetryObserver(getter_AddRefs(obs));
        if (!obs)
            break;

        obs->StartSession(
                nsString(curEvent->Characters()).get(),
                curEvent->Time()
                );
        break;
    }

    case AndroidGoannaEvent::TELEMETRY_UI_EVENT: {
        if (curEvent->Data().Length() == 0)
            break;

        nsCOMPtr<nsIUITelemetryObserver> obs;
        mBrowserApp->GetUITelemetryObserver(getter_AddRefs(obs));
        if (!obs)
            break;

        obs->AddEvent(
                nsString(curEvent->Data()).get(),
                nsString(curEvent->Characters()).get(),
                curEvent->Time(),
                nsString(curEvent->CharactersExtra()).get()
                );
        break;
    }

    case AndroidGoannaEvent::LOAD_URI: {
        nsCOMPtr<nsICommandLineRunner> cmdline
            (do_CreateInstance("@mozilla.org/toolkit/command-line;1"));
        if (!cmdline)
            break;

        if (curEvent->Characters().Length() == 0)
            break;

        char *uri = ToNewUTF8String(curEvent->Characters());
        if (!uri)
            break;

        char *flag = ToNewUTF8String(curEvent->CharactersExtra());

        const char *argv[4] = {
            "dummyappname",
            "-url",
            uri,
            flag ? flag : ""
        };
        nsresult rv = cmdline->Init(4, argv, nullptr, nsICommandLine::STATE_REMOTE_AUTO);
        if (NS_SUCCEEDED(rv))
            cmdline->Run();
        free(uri);
        if (flag)
            free(flag);
        break;
    }

    case AndroidGoannaEvent::SIZE_CHANGED: {
        // store the last resize event to dispatch it to new windows with a FORCED_RESIZE event
        if (curEvent != gLastSizeChange) {
            gLastSizeChange = AndroidGoannaEvent::CopyResizeEvent(curEvent);
        }
        nsWindow::OnGlobalAndroidEvent(curEvent);
        break;
    }

    case AndroidGoannaEvent::VISITED: {
#ifdef MOZ_ANDROID_HISTORY
        nsCOMPtr<IHistory> history = services::GetHistoryService();
        nsCOMPtr<nsIURI> visitedURI;
        if (history &&
            NS_SUCCEEDED(NS_NewURI(getter_AddRefs(visitedURI),
                                   nsString(curEvent->Characters())))) {
            history->NotifyVisited(visitedURI);
        }
#endif
        break;
    }

    case AndroidGoannaEvent::NETWORK_CHANGED: {
        hal::NotifyNetworkChange(hal::NetworkInformation(curEvent->ConnectionType(),
                                                         curEvent->IsWifi(),
                                                         curEvent->DHCPGateway()));
        break;
    }

    case AndroidGoannaEvent::SCREENORIENTATION_CHANGED: {
        nsresult rv;
        nsCOMPtr<nsIScreenManager> screenMgr =
            do_GetService("@mozilla.org/gfx/screenmanager;1", &rv);
        if (NS_FAILED(rv)) {
            NS_ERROR("Can't find nsIScreenManager!");
            break;
        }

        nsIntRect rect;
        int32_t colorDepth, pixelDepth;
        dom::ScreenOrientation orientation;
        nsCOMPtr<nsIScreen> screen;

        screenMgr->GetPrimaryScreen(getter_AddRefs(screen));
        screen->GetRect(&rect.x, &rect.y, &rect.width, &rect.height);
        screen->GetColorDepth(&colorDepth);
        screen->GetPixelDepth(&pixelDepth);
        orientation =
            static_cast<dom::ScreenOrientation>(curEvent->ScreenOrientation());

        hal::NotifyScreenConfigurationChange(
            hal::ScreenConfiguration(rect, orientation, colorDepth, pixelDepth));
        break;
    }

    case AndroidGoannaEvent::CALL_OBSERVER:
    {
        nsCOMPtr<nsIObserver> observer;
        mObserversHash.Get(curEvent->Characters(), getter_AddRefs(observer));

        if (observer) {
            observer->Observe(nullptr, NS_ConvertUTF16toUTF8(curEvent->CharactersExtra()).get(),
                              nsString(curEvent->Data()).get());
        } else {
            ALOG("Call_Observer event: Observer was not found!");
        }

        break;
    }

    case AndroidGoannaEvent::REMOVE_OBSERVER:
        mObserversHash.Remove(curEvent->Characters());
        break;

    case AndroidGoannaEvent::ADD_OBSERVER:
        AddObserver(curEvent->Characters(), curEvent->Observer());
        break;

    case AndroidGoannaEvent::PREFERENCES_GET:
    case AndroidGoannaEvent::PREFERENCES_OBSERVE: {
        const nsTArray<nsString> &prefNames = curEvent->PrefNames();
        size_t count = prefNames.Length();
        nsAutoArrayPtr<const char16_t*> prefNamePtrs(new const char16_t*[count]);
        for (size_t i = 0; i < count; ++i) {
            prefNamePtrs[i] = prefNames[i].get();
        }

        if (curEvent->Type() == AndroidGoannaEvent::PREFERENCES_GET) {
            mBrowserApp->GetPreferences(curEvent->RequestId(), prefNamePtrs, count);
        } else {
            mBrowserApp->ObservePreferences(curEvent->RequestId(), prefNamePtrs, count);
        }
        break;
    }

    case AndroidGoannaEvent::PREFERENCES_REMOVE_OBSERVERS:
        mBrowserApp->RemovePreferenceObservers(curEvent->RequestId());
        break;

    case AndroidGoannaEvent::LOW_MEMORY:
        // TODO hook in memory-reduction stuff for different levels here
        if (curEvent->MetaState() >= AndroidGoannaEvent::MEMORY_PRESSURE_MEDIUM) {
            nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
            if (os) {
                os->NotifyObservers(nullptr,
                                    "memory-pressure",
                                    MOZ_UTF16("low-memory"));
            }
        }
        break;

    case AndroidGoannaEvent::NETWORK_LINK_CHANGE:
    {
        nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
        if (os) {
            os->NotifyObservers(nullptr,
                                NS_NETWORK_LINK_TOPIC,
                                nsString(curEvent->Characters()).get());
        }
        break;
    }

    case AndroidGoannaEvent::TELEMETRY_HISTOGRAM_ADD:
        Telemetry::Accumulate(NS_ConvertUTF16toUTF8(curEvent->Characters()).get(),
                              curEvent->Count());
        break;

    case AndroidGoannaEvent::GAMEPAD_ADDREMOVE: {
#ifdef MOZ_GAMEPAD
        nsRefPtr<mozilla::dom::GamepadService> svc =
            mozilla::dom::GamepadService::GetService();
        if (svc) {
            if (curEvent->Action() == AndroidGoannaEvent::ACTION_GAMEPAD_ADDED) {
                int svc_id = svc->AddGamepad("android",
                                             mozilla::dom::GamepadMappingType::Standard,
                                             mozilla::dom::kStandardGamepadButtons,
                                             mozilla::dom::kStandardGamepadAxes);
                widget::GoannaAppShell::GamepadAdded(curEvent->ID(),
                                                    svc_id);
            } else if (curEvent->Action() == AndroidGoannaEvent::ACTION_GAMEPAD_REMOVED) {
                svc->RemoveGamepad(curEvent->ID());
            }
        }
#endif
        break;
    }

    case AndroidGoannaEvent::GAMEPAD_DATA: {
#ifdef MOZ_GAMEPAD
        nsRefPtr<mozilla::dom::GamepadService> svc =
            mozilla::dom::GamepadService::GetService();
        if (svc) {
            int id = curEvent->ID();
            if (curEvent->Action() == AndroidGoannaEvent::ACTION_GAMEPAD_BUTTON) {
                 svc->NewButtonEvent(id, curEvent->GamepadButton(),
                                     curEvent->GamepadButtonPressed(),
                                     curEvent->GamepadButtonValue());
            } else if (curEvent->Action() == AndroidGoannaEvent::ACTION_GAMEPAD_AXES) {
                int valid = curEvent->Flags();
                const nsTArray<float>& values = curEvent->GamepadValues();
                for (unsigned i = 0; i < values.Length(); i++) {
                    if (valid & (1<<i)) {
                        svc->NewAxisMoveEvent(id, i, values[i]);
                    }
                }
            }
        }
#endif
        break;
    }
    case AndroidGoannaEvent::NOOP:
        break;

    default:
        nsWindow::OnGlobalAndroidEvent(curEvent);
        break;
    }

    if (curEvent->AckNeeded()) {
        widget::GoannaAppShell::AcknowledgeEvent();
    }

    EVLOG("nsAppShell: -- done event %p %d", (void*)curEvent.get(), curEvent->Type());

    return true;
}

void
nsAppShell::ResendLastResizeEvent(nsWindow* aDest) {
    if (gLastSizeChange) {
        nsWindow::OnGlobalAndroidEvent(gLastSizeChange);
    }
}

AndroidGoannaEvent*
nsAppShell::PopNextEvent()
{
    AndroidGoannaEvent *ae = nullptr;
    MutexAutoLock lock(mQueueLock);
    if (mEventQueue.Length()) {
        ae = mEventQueue[0];
        mEventQueue.RemoveElementAt(0);
        if (mQueuedViewportEvent == ae) {
            mQueuedViewportEvent = nullptr;
        }
    }

    return ae;
}

AndroidGoannaEvent*
nsAppShell::PeekNextEvent()
{
    AndroidGoannaEvent *ae = nullptr;
    MutexAutoLock lock(mQueueLock);
    if (mEventQueue.Length()) {
        ae = mEventQueue[0];
    }

    return ae;
}

void
nsAppShell::PostEvent(AndroidGoannaEvent *ae)
{
    {
        // set this to true when inserting events that we can coalesce
        // viewport events across. this is effectively maintaining a whitelist
        // of events that are unaffected by viewport changes.
        bool allowCoalescingNextViewport = false;

        MutexAutoLock lock(mQueueLock);
        EVLOG("nsAppShell::PostEvent %p %d", ae, ae->Type());
        switch (ae->Type()) {
        case AndroidGoannaEvent::COMPOSITOR_CREATE:
        case AndroidGoannaEvent::COMPOSITOR_PAUSE:
        case AndroidGoannaEvent::COMPOSITOR_RESUME:
            // Give priority to these events, but maintain their order wrt each other.
            {
                uint32_t i = 0;
                while (i < mEventQueue.Length() &&
                       (mEventQueue[i]->Type() == AndroidGoannaEvent::COMPOSITOR_CREATE ||
                        mEventQueue[i]->Type() == AndroidGoannaEvent::COMPOSITOR_PAUSE ||
                        mEventQueue[i]->Type() == AndroidGoannaEvent::COMPOSITOR_RESUME)) {
                    i++;
                }
                EVLOG("nsAppShell: Inserting compositor event %d at position %d to maintain priority order", ae->Type(), i);
                mEventQueue.InsertElementAt(i, ae);
            }
            break;

        case AndroidGoannaEvent::VIEWPORT:
            if (mQueuedViewportEvent) {
                // drop the previous viewport event now that we have a new one
                EVLOG("nsAppShell: Dropping old viewport event at %p in favour of new VIEWPORT event %p", mQueuedViewportEvent, ae);
                mEventQueue.RemoveElement(mQueuedViewportEvent);
                delete mQueuedViewportEvent;
            }
            mQueuedViewportEvent = ae;
            allowCoalescingNextViewport = true;

            mEventQueue.AppendElement(ae);
            break;

        case AndroidGoannaEvent::MOTION_EVENT:
        case AndroidGoannaEvent::APZ_INPUT_EVENT:
            if (mAllowCoalescingTouches && mEventQueue.Length() > 0) {
                int len = mEventQueue.Length();
                AndroidGoannaEvent* event = mEventQueue[len - 1];
                if (ae->CanCoalesceWith(event)) {
                    // consecutive motion-move events; drop the last one before adding the new one
                    EVLOG("nsAppShell: Dropping old move event at %p in favour of new move event %p", event, ae);
                    mEventQueue.RemoveElementAt(len - 1);
                    delete event;
                }
            }
            mEventQueue.AppendElement(ae);
            break;

        case AndroidGoannaEvent::NATIVE_POKE:
            allowCoalescingNextViewport = true;
            // fall through

        default:
            mEventQueue.AppendElement(ae);
            break;
        }

        // if the event wasn't on our whitelist then reset mQueuedViewportEvent
        // so that we don't coalesce future viewport events into the last viewport
        // event we added
        if (!allowCoalescingNextViewport)
            mQueuedViewportEvent = nullptr;
    }
    NotifyNativeEvent();
}

void
nsAppShell::OnResume()
{
}

nsresult
nsAppShell::AddObserver(const nsAString &aObserverKey, nsIObserver *aObserver)
{
    NS_ASSERTION(aObserver != nullptr, "nsAppShell::AddObserver: aObserver is null!");
    mObserversHash.Put(aObserverKey, aObserver);
    return NS_OK;
}

// Used by IPC code
namespace mozilla {

bool ProcessNextEvent()
{
    if (!nsAppShell::gAppShell) {
        return false;
    }

    return nsAppShell::gAppShell->ProcessNextNativeEvent(true) ? true : false;
}

void NotifyEvent()
{
    if (nsAppShell::gAppShell) {
        nsAppShell::gAppShell->NotifyNativeEvent();
    }
}

}
