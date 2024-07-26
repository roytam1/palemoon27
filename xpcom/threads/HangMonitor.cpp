/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/HangMonitor.h"

#include "mozilla/Atomics.h"
#include "mozilla/BackgroundHangMonitor.h"
#include "mozilla/Monitor.h"
#include "mozilla/Preferences.h"
#include "mozilla/ProcessedStack.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/UniquePtr.h"
#include "nsReadableUtils.h"
#include "nsStackWalk.h"
#include "nsThreadUtils.h"
#include "nsXULAppAPI.h"

#ifdef XP_WIN
#include <windows.h>
#endif

namespace mozilla {
namespace HangMonitor {

/**
 * A flag which may be set from within a debugger to disable the hang
 * monitor.
 */
volatile bool gDebugDisableHangMonitor = false;

const char kHangMonitorPrefName[] = "hangmonitor.timeout";

// Monitor protects gShutdown and gTimeout, but not gTimestamp which rely on
// being atomically set by the processor; synchronization doesn't really matter
// in this use case.
Monitor* gMonitor;

// The timeout preference, in seconds.
int32_t gTimeout;

PRThread* gThread;

// Set when shutdown begins to signal the thread to exit immediately.
bool gShutdown;

// The timestamp of the last event notification, or PR_INTERVAL_NO_WAIT if
// we're currently not processing events.
Atomic<PRIntervalTime> gTimestamp(PR_INTERVAL_NO_WAIT);

// PrefChangedFunc
void
PrefChanged(const char*, void*)
{
  int32_t newval = Preferences::GetInt(kHangMonitorPrefName);
  MonitorAutoLock lock(*gMonitor);
  if (newval != gTimeout) {
    gTimeout = newval;
    lock.Notify();
  }
}

void
Crash()
{
  if (gDebugDisableHangMonitor) {
    return;
  }

#ifdef XP_WIN
  if (::IsDebuggerPresent()) {
    return;
  }
#endif

  NS_RUNTIMEABORT("HangMonitor triggered");
}

void
ThreadMain(void*)
{
  PR_SetCurrentThreadName("Hang Monitor");

  MonitorAutoLock lock(*gMonitor);

  // In order to avoid issues with the hang monitor incorrectly triggering
  // during a general system stop such as sleeping, the monitor thread must
  // run twice to trigger hang protection.
  PRIntervalTime lastTimestamp = 0;
  int waitCount = 0;

  while (true) {
    if (gShutdown) {
      return; // Exit the thread
    }

    // avoid rereading the volatile value in this loop
    PRIntervalTime timestamp = gTimestamp;

    PRIntervalTime now = PR_IntervalNow();

    if (timestamp != PR_INTERVAL_NO_WAIT &&
        now < timestamp) {
      // 32-bit overflow, reset for another waiting period
      timestamp = 1; // lowest legal PRInterval value
    }

    if (timestamp != PR_INTERVAL_NO_WAIT &&
        timestamp == lastTimestamp &&
        gTimeout > 0) {
      ++waitCount;
      // This is the crash-on-hang feature.
      // See bug 867313 for the quirk in the waitCount comparison
      if (waitCount >= 2) {
        int32_t delay =
          int32_t(PR_IntervalToSeconds(now - timestamp));
        if (delay >= gTimeout) {
          MonitorAutoUnlock unlock(*gMonitor);
          Crash();
        }
      }
    } else {
      lastTimestamp = timestamp;
      waitCount = 0;
    }

    PRIntervalTime timeout;
    if (gTimeout <= 0) {
      timeout = PR_INTERVAL_NO_TIMEOUT;
    } else {
      timeout = PR_MillisecondsToInterval(gTimeout * 500);
    }
    lock.Wait(timeout);
  }
}

void
Startup()
{
  // The hang detector only runs in chrome processes. If you change this,
  // you must also deal with the threadsafety of AnnotateCrashReport in
  // non-chrome processes!
  if (GoannaProcessType_Default != XRE_GetProcessType()) {
    return;
  }

  MOZ_ASSERT(!gMonitor, "Hang monitor already initialized");
  gMonitor = new Monitor("HangMonitor");

  Preferences::RegisterCallback(PrefChanged, kHangMonitorPrefName, nullptr);
  PrefChanged(nullptr, nullptr);

  // Don't actually start measuring hangs until we hit the main event loop.
  // This potentially misses a small class of really early startup hangs,
  // but avoids dealing with some xpcshell tests and other situations which
  // start XPCOM but don't ever start the event loop.
  Suspend();

  gThread = PR_CreateThread(PR_USER_THREAD,
                            ThreadMain,
                            nullptr, PR_PRIORITY_LOW, PR_GLOBAL_THREAD,
                            PR_JOINABLE_THREAD, 0);
}

void
Shutdown()
{
  if (GoannaProcessType_Default != XRE_GetProcessType()) {
    return;
  }

  MOZ_ASSERT(gMonitor, "Hang monitor not started");

  {
    // Scope the lock we're going to delete later
    MonitorAutoLock lock(*gMonitor);
    gShutdown = true;
    lock.Notify();
  }

  // thread creation could theoretically fail
  if (gThread) {
    PR_JoinThread(gThread);
    gThread = nullptr;
  }

  delete gMonitor;
  gMonitor = nullptr;

}

static bool
IsUIMessageWaiting()
{
#ifndef XP_WIN
  return false;
#else
#define NS_WM_IMEFIRST WM_IME_SETCONTEXT
#define NS_WM_IMELAST  WM_IME_KEYUP
  BOOL haveUIMessageWaiting = FALSE;
  MSG msg;
  haveUIMessageWaiting |= ::PeekMessageW(&msg, nullptr, WM_KEYFIRST,
                                         WM_IME_KEYLAST, PM_NOREMOVE);
  haveUIMessageWaiting |= ::PeekMessageW(&msg, nullptr, NS_WM_IMEFIRST,
                                         NS_WM_IMELAST, PM_NOREMOVE);
  haveUIMessageWaiting |= ::PeekMessageW(&msg, nullptr, WM_MOUSEFIRST,
                                         WM_MOUSELAST, PM_NOREMOVE);
  return haveUIMessageWaiting;
#endif
}

void
NotifyActivity(ActivityType aActivityType)
{
  MOZ_ASSERT(NS_IsMainThread(),
             "HangMonitor::Notify called from off the main thread.");

  // Determine the activity type more specifically
  if (aActivityType == kGeneralActivity) {
    aActivityType = IsUIMessageWaiting() ? kActivityUIAVail :
                                           kActivityNoUIAVail;
  }

  // Calculate the cumulative amount of lag time since the last UI message
  static uint32_t cumulativeUILagMS = 0;
  switch (aActivityType) {
    case kActivityNoUIAVail:
      cumulativeUILagMS = 0;
      break;
    case kActivityUIAVail:
    case kUIActivity:
      if (gTimestamp != PR_INTERVAL_NO_WAIT) {
        cumulativeUILagMS += PR_IntervalToMilliseconds(PR_IntervalNow() -
                                                       gTimestamp);
      }
      break;
    default:
      break;
  }

  // This is not a locked activity because PRTimeStamp is a 32-bit quantity
  // which can be read/written atomically, and we don't want to pay locking
  // penalties here.
  gTimestamp = PR_IntervalNow();

  // If we have UI activity we should reset the timer and report it if it is
  // significant enough.
  if (aActivityType == kUIActivity) {
    // The minimum amount of lag time that we should report for telemetry data.
    // Mozilla's UI responsiveness goal is 50ms
    static const uint32_t kUIResponsivenessThresholdMS = 50;
    cumulativeUILagMS = 0;
  }

  if (gThread && !gShutdown) {
    mozilla::BackgroundHangMonitor().NotifyActivity();
  }
}

void
Suspend()
{
  MOZ_ASSERT(NS_IsMainThread(),
             "HangMonitor::Suspend called from off the main thread.");

  // Because gTimestamp changes this resets the wait count.
  gTimestamp = PR_INTERVAL_NO_WAIT;

  if (gThread && !gShutdown) {
    mozilla::BackgroundHangMonitor().NotifyWait();
  }
}

} // namespace HangMonitor
} // namespace mozilla
