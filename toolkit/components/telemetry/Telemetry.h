/* -*-  Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2; -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef Telemetry_h__
#define Telemetry_h__

#include "mozilla/GuardObjects.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/StartupTimeline.h"
#include "nsTArray.h"
#include "nsStringGlue.h"

#include "mozilla/TelemetryHistogramEnums.h"

/******************************************************************************
 * This implements the Telemetry system.
 * It allows recording into histograms as well some more specialized data
 * points and gives access to the data.
 *
 * For documentation on how to add and use new Telemetry probes, see:
 * https://developer.mozilla.org/en-US/docs/Mozilla/Performance/Adding_a_new_Telemetry_probe
 *
 * For more general information on Telemetry see:
 * https://wiki.mozilla.org/Telemetry
 *****************************************************************************/

namespace mozilla {
namespace HangMonitor {
  class HangAnnotations;
} // namespace HangMonitor
namespace Telemetry {

enum TimerResolution {
  Millisecond,
  Microsecond
};

/**
 * Initialize the Telemetry service on the main thread at startup.
 */
void Init();

/**
 * Adds sample to a histogram defined in TelemetryHistograms.h
 *
 * @param id - histogram id
 * @param sample - value to record.
 */
void Accumulate(ID id, uint32_t sample);

/**
 * Adds sample to a keyed histogram defined in TelemetryHistograms.h
 *
 * @param id - keyed histogram id
 * @param key - the string key
 * @param sample - (optional) value to record, defaults to 1.
 */
void Accumulate(ID id, const nsCString& key, uint32_t sample = 1);

/**
 * Adds a sample to a histogram defined in TelemetryHistograms.h.
 * This function is here to support telemetry measurements from Java,
 * where we have only names and not numeric IDs.  You should almost
 * certainly be using the by-enum-id version instead of this one.
 *
 * @param name - histogram name
 * @param sample - value to record
 */
void Accumulate(const char* name, uint32_t sample);

/**
 * Adds a sample to a histogram defined in TelemetryHistograms.h.
 * This function is here to support telemetry measurements from Java,
 * where we have only names and not numeric IDs.  You should almost
 * certainly be using the by-enum-id version instead of this one.
 *
 * @param name - histogram name
 * @param key - the string key
 * @param sample - sample - (optional) value to record, defaults to 1.
 */
void Accumulate(const char *name, const nsCString& key, uint32_t sample = 1);

/**
 * Adds time delta in milliseconds to a histogram defined in TelemetryHistograms.h
 *
 * @param id - histogram id
 * @param start - start time
 * @param end - end time
 */
void AccumulateTimeDelta(ID id, TimeStamp start, TimeStamp end = TimeStamp::Now());

/**
 * This clears the data for a histogram in TelemetryHistograms.h.
 *
 * @param id - histogram id
 */
void ClearHistogram(ID id);

/**
 * Enable/disable recording for this histogram at runtime.
 * Recording is enabled by default, unless listed at kRecordingInitiallyDisabledIDs[].
 * id must be a valid telemetry enum, otherwise an assertion is triggered.
 *
 * @param id - histogram id
 * @param enabled - whether or not to enable recording from now on.
 */
void SetHistogramRecordingEnabled(ID id, bool enabled);

const char* GetHistogramName(ID id);

/**
 * Those wrappers are needed because the VS versions we use do not support free
 * functions with default template arguments.
 */
template<TimerResolution res>
struct AccumulateDelta_impl
{
  static void compute(ID id, TimeStamp start, TimeStamp end = TimeStamp::Now());
  static void compute(ID id, const nsCString& key, TimeStamp start, TimeStamp end = TimeStamp::Now());
};

template<>
struct AccumulateDelta_impl<Millisecond>
{
  static void compute(ID id, TimeStamp start, TimeStamp end = TimeStamp::Now()) {
    Accumulate(id, static_cast<uint32_t>((end - start).ToMilliseconds()));
  }
  static void compute(ID id, const nsCString& key, TimeStamp start, TimeStamp end = TimeStamp::Now()) {
    Accumulate(id, key, static_cast<uint32_t>((end - start).ToMilliseconds()));
  }
};

template<>
struct AccumulateDelta_impl<Microsecond>
{
  static void compute(ID id, TimeStamp start, TimeStamp end = TimeStamp::Now()) {
    Accumulate(id, static_cast<uint32_t>((end - start).ToMicroseconds()));
  }
  static void compute(ID id, const nsCString& key, TimeStamp start, TimeStamp end = TimeStamp::Now()) {
    Accumulate(id, key, static_cast<uint32_t>((end - start).ToMicroseconds()));
  }
};


template<ID id, TimerResolution res = Millisecond>
class MOZ_RAII AutoTimer {
public:
  explicit AutoTimer(TimeStamp aStart = TimeStamp::Now() MOZ_GUARD_OBJECT_NOTIFIER_PARAM)
     : start(aStart)
  {
    MOZ_GUARD_OBJECT_NOTIFIER_INIT;
  }

  explicit AutoTimer(const nsCString& aKey, TimeStamp aStart = TimeStamp::Now() MOZ_GUARD_OBJECT_NOTIFIER_PARAM)
    : start(aStart)
    , key(aKey)
  {
    MOZ_GUARD_OBJECT_NOTIFIER_INIT;
  }

  ~AutoTimer() {
    if (key.IsEmpty()) {
      AccumulateDelta_impl<res>::compute(id, start);
    } else {
      AccumulateDelta_impl<res>::compute(id, key, start);
    }
  }

private:
  const TimeStamp start;
  const nsCString key;
  MOZ_DECL_USE_GUARD_OBJECT_NOTIFIER
};

template<ID id>
class MOZ_RAII AutoCounter {
public:
  explicit AutoCounter(uint32_t counterStart = 0 MOZ_GUARD_OBJECT_NOTIFIER_PARAM)
    : counter(counterStart)
  {
    MOZ_GUARD_OBJECT_NOTIFIER_INIT;
  }

  ~AutoCounter() {
    Accumulate(id, counter);
  }

  // Prefix increment only, to encourage good habits.
  void operator++() {
    ++counter;
  }

  // Chaining doesn't make any sense, don't return anything.
  void operator+=(int increment) {
    counter += increment;
  }

private:
  uint32_t counter;
  MOZ_DECL_USE_GUARD_OBJECT_NOTIFIER
};

/**
 * Indicates whether Telemetry base data recording is turned on. Added for future uses.
 */
bool CanRecordBase();

/**
 * Indicates whether Telemetry extended data recording is turned on.  This is intended
 * to guard calls to Accumulate when the statistic being recorded is expensive to compute.
 */
bool CanRecordExtended();

/**
 * Records slow SQL statements for Telemetry reporting.
 *
 * @param statement - offending SQL statement to record
 * @param dbName - DB filename
 * @param delay - execution time in milliseconds
 */
void RecordSlowSQLStatement(const nsACString &statement,
                            const nsACString &dbName,
                            uint32_t delay);

/**
 * Record Webrtc ICE candidate type combinations in a 17bit bitmask
 *
 * @param iceCandidateBitmask - the bitmask representing local and remote ICE
 *                              candidate types present for the connection
 * @param success - did the peer connection connected
 * @param loop - was this a Firefox Hello AKA Loop call
 */
void
RecordWebrtcIceCandidates(const uint32_t iceCandidateBitmask,
                          const bool success,
                          const bool loop);
/**
 * Initialize I/O Reporting
 * Initially this only records I/O for files in the binary directory.
 *
 * @param aXreDir - XRE directory
 */
void InitIOReporting(nsIFile* aXreDir);

/**
 * Set the profile directory. Once called, files in the profile directory will
 * be included in I/O reporting. We can't use the directory
 * service to obtain this information because it isn't running yet.
 */
void SetProfileDir(nsIFile* aProfD);

/**
 * Called to inform Telemetry that startup has completed.
 */
void LeavingStartupStage();

/**
 * Called to inform Telemetry that shutdown is commencing.
 */
void EnteringShutdownStage();

/**
 * Thresholds for a statement to be considered slow, in milliseconds
 */
const uint32_t kSlowSQLThresholdForMainThread = 50;
const uint32_t kSlowSQLThresholdForHelperThreads = 100;

class ProcessedStack;

/**
 * Record the main thread's call stack after it hangs.
 *
 * @param aDuration - Approximate duration of main thread hang, in seconds
 * @param aStack - Array of PCs from the hung call stack
 * @param aSystemUptime - System uptime at the time of the hang, in minutes
 * @param aFirefoxUptime - Firefox uptime at the time of the hang, in minutes
 * @param aAnnotations - Any annotations to be added to the report
 */
#if defined(MOZ_ENABLE_PROFILER_SPS) && !defined(MOZILLA_XPCOMRT_API)
void RecordChromeHang(uint32_t aDuration,
                      ProcessedStack &aStack,
                      int32_t aSystemUptime,
                      int32_t aFirefoxUptime,
                      mozilla::UniquePtr<mozilla::HangMonitor::HangAnnotations>
                              aAnnotations);
#endif

class ThreadHangStats;

/**
 * Move a ThreadHangStats to Telemetry storage. Normally Telemetry queries
 * for active ThreadHangStats through BackgroundHangMonitor, but once a
 * thread exits, the thread's copy of ThreadHangStats needs to be moved to
 * inside Telemetry using this function.
 *
 * @param aStats ThreadHangStats to save; the data inside aStats
 *               will be moved and aStats should be treated as
 *               invalid after this function returns
 */
void RecordThreadHangStats(ThreadHangStats& aStats);

/**
 * Record a failed attempt at locking the user's profile.
 *
 * @param aProfileDir The profile directory whose lock attempt failed
 */
void WriteFailedProfileLock(nsIFile* aProfileDir);

} // namespace Telemetry
} // namespace mozilla

#endif // Telemetry_h__
