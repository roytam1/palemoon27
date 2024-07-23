/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>
#include <string>
#include <stdio.h>
#include <fstream>
#include <sstream>
#include "GoannaProfiler.h"
#include "SaveProfileTask.h"
#include "SyncProfile.h"
#include "platform.h"
#include "nsThreadUtils.h"
#include "prenv.h"
#include "prtime.h"
#include "shared-libraries.h"
#include "mozilla/StackWalk.h"
#include "TableTicker.h"
#include "nsXULAppAPI.h"

// JSON
#include "JSStreamWriter.h"

// Meta
#include "nsXPCOM.h"
#include "nsXPCOMCID.h"
#include "nsIHttpProtocolHandler.h"
#include "nsServiceManagerUtils.h"
#include "nsIXULRuntime.h"
#include "nsIXULAppInfo.h"
#include "nsDirectoryServiceUtils.h"
#include "nsDirectoryServiceDefs.h"
#include "nsIObserverService.h"
#include "mozilla/Services.h"
#include "nsTArray.h"

#if defined(SPS_OS_android) && !defined(MOZ_WIDGET_GONK)
  #include "AndroidBridge.h"
#endif

// JS
#include "jsfriendapi.h"
#include "js/ProfilingFrameIterator.h"

#if defined(MOZ_PROFILING) && (defined(XP_MACOSX) || defined(XP_WIN))
 #define USE_NS_STACKWALK
#endif
#ifdef USE_NS_STACKWALK
 #include "nsStackWalk.h"
#endif

#if defined(XP_WIN)
typedef CONTEXT tickcontext_t;
#elif defined(LINUX)
#include <ucontext.h>
typedef ucontext_t tickcontext_t;
#endif

#if defined(LINUX) || defined(XP_MACOSX)
#include <sys/types.h>
pid_t gettid();
#endif

using std::string;
using namespace mozilla;

#ifndef MAXPATHLEN
 #ifdef PATH_MAX
  #define MAXPATHLEN PATH_MAX
 #elif defined(MAX_PATH)
  #define MAXPATHLEN MAX_PATH
 #elif defined(_MAX_PATH)
  #define MAXPATHLEN _MAX_PATH
 #elif defined(CCHMAXPATH)
  #define MAXPATHLEN CCHMAXPATH
 #else
  #define MAXPATHLEN 1024
 #endif
#endif

///////////////////////////////////////////////////////////////////////
// BEGIN SaveProfileTask et al

std::string GetSharedLibraryInfoString();

void TableTicker::HandleSaveRequest()
{
  if (!mSaveRequested)
    return;
  mSaveRequested = false;

  // TODO: Use use the ipc/chromium Tasks here to support processes
  // without XPCOM.
  nsCOMPtr<nsIRunnable> runnable = new SaveProfileTask();
  NS_DispatchToMainThread(runnable);
}

void TableTicker::DeleteExpiredMarkers()
{
  mBuffer->deleteExpiredStoredMarkers();
}

void TableTicker::StreamTaskTracer(JSStreamWriter& b)
{
  b.BeginObject();
#ifdef MOZ_TASK_TRACER
    b.Name("data");
    b.BeginArray();
      nsAutoPtr<nsTArray<nsCString>> data(
        mozilla::tasktracer::GetLoggedData(sStartTime));
      for (uint32_t i = 0; i < data->Length(); ++i) {
        b.Value((data->ElementAt(i)).get());
      }
    b.EndArray();

    b.Name("threads");
    b.BeginArray();
      mozilla::MutexAutoLock lock(*sRegisteredThreadsMutex);
      for (size_t i = 0; i < sRegisteredThreads->size(); i++) {
        // Thread meta data
        ThreadInfo* info = sRegisteredThreads->at(i);
        b.BeginObject();
        if (XRE_GetProcessType() == GoannaProcessType_Plugin) {
          // TODO Add the proper plugin name
          b.NameValue("name", "Plugin");
        } else {
          b.NameValue("name", info->Name());
        }
        b.NameValue("tid", static_cast<int>(info->ThreadId()));
        b.EndObject();
      }
    b.EndArray();

    b.NameValue("start",
                static_cast<double>(mozilla::tasktracer::GetStartTime()));
#endif
  b.EndObject();
}


void TableTicker::StreamMetaJSCustomObject(JSStreamWriter& b)
{
  b.BeginObject();

    b.NameValue("version", 2);
    b.NameValue("interval", interval());
    b.NameValue("stackwalk", mUseStackWalk);
    b.NameValue("processType", XRE_GetProcessType());

    mozilla::TimeDuration delta = mozilla::TimeStamp::Now() - sStartTime;
    b.NameValue("startTime", static_cast<double>(PR_Now()/1000.0 - delta.ToMilliseconds()));

    nsresult res;
    nsCOMPtr<nsIHttpProtocolHandler> http = do_GetService(NS_NETWORK_PROTOCOL_CONTRACTID_PREFIX "http", &res);
    if (!NS_FAILED(res)) {
      nsAutoCString string;

      res = http->GetPlatform(string);
      if (!NS_FAILED(res))
        b.NameValue("platform", string.Data());

      res = http->GetOscpu(string);
      if (!NS_FAILED(res))
        b.NameValue("oscpu", string.Data());

      res = http->GetMisc(string);
      if (!NS_FAILED(res))
        b.NameValue("misc", string.Data());
    }

    nsCOMPtr<nsIXULRuntime> runtime = do_GetService("@mozilla.org/xre/runtime;1");
    if (runtime) {
      nsAutoCString string;

      res = runtime->GetXPCOMABI(string);
      if (!NS_FAILED(res))
        b.NameValue("abi", string.Data());

      res = runtime->GetWidgetToolkit(string);
      if (!NS_FAILED(res))
        b.NameValue("toolkit", string.Data());
    }

    nsCOMPtr<nsIXULAppInfo> appInfo = do_GetService("@mozilla.org/xre/app-info;1");
    if (appInfo) {
      nsAutoCString string;

      res = appInfo->GetName(string);
      if (!NS_FAILED(res))
        b.NameValue("product", string.Data());
    }

  b.EndObject();
}

void TableTicker::ToStreamAsJSON(std::ostream& stream)
{
  JSStreamWriter b(stream);
  StreamJSObject(b);
}

JSObject* TableTicker::ToJSObject(JSContext *aCx)
{
  JS::RootedValue val(aCx);
  std::stringstream ss;
  {
    // Define a scope to prevent a moving GC during ~JSStreamWriter from
    // trashing the return value.
    JSStreamWriter b(ss);
    StreamJSObject(b);
    NS_ConvertUTF8toUTF16 js_string(nsDependentCString(ss.str().c_str()));
    JS_ParseJSON(aCx, static_cast<const char16_t*>(js_string.get()),
                 js_string.Length(), &val);
  }
  return &val.toObject();
}

struct SubprocessClosure {
  explicit SubprocessClosure(JSStreamWriter *aWriter)
    : mWriter(aWriter)
  {}

  JSStreamWriter* mWriter;
};

void SubProcessCallback(const char* aProfile, void* aClosure)
{
  // Called by the observer to get their profile data included
  // as a sub profile
  SubprocessClosure* closure = (SubprocessClosure*)aClosure;

  // Add the string profile into the profile
  closure->mWriter->Value(aProfile);
}


#if defined(SPS_OS_android) && !defined(MOZ_WIDGET_GONK)
static
void BuildJavaThreadJSObject(JSStreamWriter& b)
{
  b.BeginObject();

    b.NameValue("name", "Java Main Thread");

    b.Name("samples");
    b.BeginArray();

      // for each sample
      for (int sampleId = 0; true; sampleId++) {
        bool firstRun = true;
        // for each frame
        for (int frameId = 0; true; frameId++) {
          nsCString result;
          bool hasFrame = AndroidBridge::Bridge()->GetFrameNameJavaProfiling(0, sampleId, frameId, result);
          // when we run out of frames, we stop looping
          if (!hasFrame) {
            // if we found at least one frame, we have objects to close
            if (!firstRun) {
                b.EndArray();
              b.EndObject();
            }
            break;
          }
          // the first time around, open the sample object and frames array
          if (firstRun) {
            firstRun = false;

            double sampleTime =
              mozilla::widget::GoannaJavaSampler::GetSampleTimeJavaProfiling(0, sampleId);

            b.BeginObject();
              b.NameValue("time", sampleTime);

              b.Name("frames");
              b.BeginArray();
          }
          // add a frame to the sample
          b.BeginObject();
            b.NameValue("location", result.BeginReading());
          b.EndObject();
        }
        // if we found no frames for this sample, we are done
        if (firstRun) {
          break;
        }
      }

    b.EndArray();

  b.EndObject();
}
#endif

void TableTicker::StreamJSObject(JSStreamWriter& b)
{
  b.BeginObject();
    // Put shared library info
    b.NameValue("libs", GetSharedLibraryInfoString().c_str());

    // Put meta data
    b.Name("meta");
    StreamMetaJSCustomObject(b);

    // Data of TaskTracer doesn't belong in the circular buffer.
    if (TaskTracer()) {
      b.Name("tasktracer");
      StreamTaskTracer(b);
    }

    // Lists the samples for each ThreadProfile
    b.Name("threads");
    b.BeginArray();

      SetPaused(true);

      {
        mozilla::MutexAutoLock lock(*sRegisteredThreadsMutex);

        for (size_t i = 0; i < sRegisteredThreads->size(); i++) {
          // Thread not being profiled, skip it
          if (!sRegisteredThreads->at(i)->Profile())
            continue;

          // Note that we intentionally include ThreadProfile which
          // have been marked for pending delete.

          MutexAutoLock lock(*sRegisteredThreads->at(i)->Profile()->GetMutex());

          sRegisteredThreads->at(i)->Profile()->StreamJSObject(b);
        }
      }

      if (Sampler::CanNotifyObservers()) {
        // Send a event asking any subprocesses (plugins) to
        // give us their information
        SubprocessClosure closure(&b);
        nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
        if (os) {
          nsRefPtr<ProfileSaveEvent> pse = new ProfileSaveEvent(SubProcessCallback, &closure);
          os->NotifyObservers(pse, "profiler-subprocess", nullptr);
        }
      }

  #if defined(SPS_OS_android) && !defined(MOZ_WIDGET_GONK)
      if (ProfileJava()) {
        mozilla::widget::GoannaJavaSampler::PauseJavaProfiling();

        BuildJavaThreadJSObject(b);

        mozilla::widget::GoannaJavaSampler::UnpauseJavaProfiling();
      }
  #endif

      SetPaused(false);
    b.EndArray();

  b.EndObject();
}

// END SaveProfileTask et al
////////////////////////////////////////////////////////////////////////

static
void addDynamicTag(ThreadProfile &aProfile, char aTagName, const char *aStr)
{
  aProfile.addTag(ProfileEntry(aTagName, ""));
  // Add one to store the null termination
  size_t strLen = strlen(aStr) + 1;
  for (size_t j = 0; j < strLen;) {
    // Store as many characters in the void* as the platform allows
    char text[sizeof(void*)];
    size_t len = sizeof(void*)/sizeof(char);
    if (j+len >= strLen) {
      len = strLen - j;
    }
    memcpy(text, &aStr[j], len);
    j += sizeof(void*)/sizeof(char);
    // Cast to *((void**) to pass the text data to a void*
    aProfile.addTag(ProfileEntry('d', *((void**)(&text[0]))));
  }
}

static
void addPseudoEntry(volatile StackEntry &entry, ThreadProfile &aProfile,
                    ProfileStack *stack, void *lastpc)
{
  // Pseudo-frames with the BEGIN_PSEUDO_JS flag are just annotations
  // and should not be recorded in the profile.
  if (entry.hasFlag(StackEntry::BEGIN_PSEUDO_JS))
    return;

  int lineno = -1;

  // First entry has tagName 's' (start)
  // Check for magic pointer bit 1 to indicate copy
  const char* sampleLabel = entry.label();
  if (entry.isCopyLabel()) {
    // Store the string using 1 or more 'd' (dynamic) tags
    // that will happen to the preceding tag

    addDynamicTag(aProfile, 'c', sampleLabel);
    if (entry.isJs()) {
      if (!entry.pc()) {
        // The JIT only allows the top-most entry to have a nullptr pc
        MOZ_ASSERT(&entry == &stack->mStack[stack->stackSize() - 1]);
        // If stack-walking was disabled, then that's just unfortunate
        if (lastpc) {
          jsbytecode *jspc = js::ProfilingGetPC(stack->mRuntime, entry.script(),
                                                lastpc);
          if (jspc) {
            lineno = JS_PCToLineNumber(entry.script(), jspc);
          }
        }
      } else {
        lineno = JS_PCToLineNumber(entry.script(), entry.pc());
      }
    } else {
      lineno = entry.line();
    }
  } else {
    aProfile.addTag(ProfileEntry('c', sampleLabel));

    // XXX: Bug 1010578. Don't assume a CPP entry and try to get the
    // line for js entries as well.
    if (entry.isCpp()) {
      lineno = entry.line();
    }
  }

  if (lineno != -1) {
    aProfile.addTag(ProfileEntry('n', lineno));
  }

  uint32_t category = entry.category();
  MOZ_ASSERT(!(category & StackEntry::IS_CPP_ENTRY));
  MOZ_ASSERT(!(category & StackEntry::FRAME_LABEL_COPY));

  if (category) {
    aProfile.addTag(ProfileEntry('y', (int)category));
  }
}

struct NativeStack
{
  void** pc_array;
  void** sp_array;
  size_t size;
  size_t count;
};

mozilla::Atomic<bool> WALKING_JS_STACK(false);

struct AutoWalkJSStack {
  bool walkAllowed;

  AutoWalkJSStack() : walkAllowed(false) {
    walkAllowed = WALKING_JS_STACK.compareExchange(false, true);
  }

  ~AutoWalkJSStack() {
    if (walkAllowed)
        WALKING_JS_STACK = false;
  }
};

static
void mergeStacksIntoProfile(ThreadProfile& aProfile, TickSample* aSample, NativeStack& aNativeStack)
{
  ProfileStack* profileStack = aProfile.GetStack();
  volatile StackEntry *pseudoFrames = profileStack->mStack;
  uint32_t profileCount = profileStack->stackSize();

  // Make a copy of the JS stack into a JSFrame array. This is necessary since,
  // like the native stack, the JS stack is iterated youngest-to-oldest and we
  // need to iterate oldest-to-youngest when adding entries to aProfile.

  uint32_t startBufferGen = aProfile.bufferGeneration();
  uint32_t jsCount = 0;
  JS::ProfilingFrameIterator::Frame jsFrames[1000];
  // Only walk jit stack if profiling frame iterator is turned on.
  if (pseudoStack->mRuntime && JS::IsProfilingEnabledForRuntime(pseudoStack->mRuntime)) {
    AutoWalkJSStack autoWalkJSStack;
    const uint32_t maxFrames = mozilla::ArrayLength(jsFrames);

    if (aSample && autoWalkJSStack.walkAllowed) {
      JS::ProfilingFrameIterator::RegisterState registerState;
      registerState.pc = aSample->pc;
      registerState.sp = aSample->sp;
#ifdef ENABLE_ARM_LR_SAVING
      registerState.lr = aSample->lr;
#endif

      JS::ProfilingFrameIterator jsIter(pseudoStack->mRuntime,
                                        registerState,
                                        startBufferGen);
      for (; jsCount < maxFrames && !jsIter.done(); ++jsIter) {
        uint32_t extracted = jsIter.extractStack(jsFrames, jsCount, maxFrames);
        MOZ_ASSERT(extracted <= (maxFrames - jsCount));
        jsCount += extracted;
        if (jsCount == maxFrames)
          break;
      }
    }
  }

  // Start the sample with a root entry.
  aProfile.addTag(ProfileEntry('s', "(root)"));

  // While the pseudo-stack array is ordered oldest-to-youngest, the JS and
  // native arrays are ordered youngest-to-oldest. We must add frames to
  // aProfile oldest-to-youngest. Thus, iterate over the pseudo-stack forwards
  // and JS and native arrays backwards. Note: this means the terminating
  // condition jsIndex and nativeIndex is being < 0.
  uint32_t pseudoIndex = 0;
  int32_t jsIndex = jsCount - 1;
  int32_t nativeIndex = aNativeStack.count - 1;

  uint8_t *lastPseudoCppStackAddr = nullptr;

  // Iterate as long as there is at least one frame remaining.
  while (pseudoIndex != pseudoCount || jsIndex >= 0 || nativeIndex >= 0) {
    // There are 1 to 3 frames available. Find and add the oldest.

    uint8_t *pseudoStackAddr = nullptr;
    uint8_t *jsStackAddr = nullptr;
    uint8_t *nativeStackAddr = nullptr;

    if (pseudoIndex != pseudoCount) {
      volatile StackEntry &pseudoFrame = pseudoFrames[pseudoIndex];

      if (pseudoFrame.isCpp())
        lastPseudoCppStackAddr = (uint8_t *) pseudoFrame.stackAddress();

      // Skip any pseudo-stack JS frames which are marked isOSR
      // Pseudostack frames are marked isOSR when the JS interpreter
      // enters a jit frame on a loop edge (via on-stack-replacement,
      // or OSR).  To avoid both the pseudoframe and jit frame being
      // recorded (and showing up twice), the interpreter marks the
      // interpreter pseudostack entry with the OSR flag to ensure that
      // it doesn't get counted.
      if (pseudoFrame.isJs() && pseudoFrame.isOSR()) {
          pseudoIndex++;
          continue;
      }

      MOZ_ASSERT(lastPseudoCppStackAddr);
      pseudoStackAddr = lastPseudoCppStackAddr;
    }

    if (jsIndex >= 0)
      jsStackAddr = (uint8_t *) jsFrames[jsIndex].stackAddress;

    if (nativeIndex >= 0)
      nativeStackAddr = (uint8_t *) aNativeStack.sp_array[nativeIndex];

    // Sanity checks.
    MOZ_ASSERT_IF(pseudoStackAddr, pseudoStackAddr != jsStackAddr &&
                                   pseudoStackAddr != nativeStackAddr);
    MOZ_ASSERT_IF(jsStackAddr, jsStackAddr != pseudoStackAddr &&
                               jsStackAddr != nativeStackAddr);
    MOZ_ASSERT_IF(nativeStackAddr, nativeStackAddr != pseudoStackAddr &&
                                   nativeStackAddr != jsStackAddr);

    // Check to see if pseudoStack frame is top-most.
    if (pseudoStackAddr > jsStackAddr && pseudoStackAddr > nativeStackAddr) {
      MOZ_ASSERT(pseudoIndex < pseudoCount);
      volatile StackEntry &pseudoFrame = pseudoFrames[pseudoIndex];
      addPseudoEntry(pseudoFrame, aProfile, pseudoStack, nullptr);
      pseudoIndex++;
      continue;
    }

    // Check to see if JS jit stack frame is top-most
    if (jsStackAddr > nativeStackAddr) {
      MOZ_ASSERT(jsIndex >= 0);
      addDynamicTag(aProfile, 'c', jsFrames[jsIndex].label);

      // Stringifying optimization information is delayed until streaming
      // time. To re-lookup the entry in the JitcodeGlobalTable, we need to
      // store the JIT code address ('J') in the circular buffer.
      //
      // Note that we cannot do this when we are sychronously sampling the
      // current thread; that is, when called from profiler_get_backtrace. The
      // captured backtrace is usually externally stored for an indeterminate
      // amount of time, such as in nsRefreshDriver. Problematically, the
      // stored backtrace may be alive across a GC during which the profiler
      // itself is disabled. In that case, the JS engine is free to discard
      // its JIT code. This means that if we inserted such 'J' entries into
      // the buffer, nsRefreshDriver would now be holding on to a backtrace
      // with stale JIT code return addresses.
      MOZ_ASSERT_IF(jsFrames[jsIndex].hasTrackedOptimizations,
                    jsFrames[jsIndex].kind == JS::ProfilingFrameIterator::Frame_Ion);
      if (!aSample->isSamplingCurrentThread &&
          (jsFrames[jsIndex].kind == JS::ProfilingFrameIterator::Frame_Ion ||
           jsFrames[jsIndex].kind == JS::ProfilingFrameIterator::Frame_Baseline)) {
        aProfile.addTag(ProfileEntry('J', jsFrames[jsIndex].returnAddress));
      }

      jsIndex--;
      continue;
    }

    // If we reach here, there must be a native stack entry and it must be the
    // greatest entry.
    MOZ_ASSERT(nativeStackAddr);
    MOZ_ASSERT(nativeIndex >= 0);
    aProfile.addTag(ProfileEntry('l', (void*)aNativeStack.pc_array[nativeIndex]));
    nativeIndex--;
  }

  MOZ_ASSERT(aProfile.bufferGeneration() >= startBufferGen);
  uint32_t lapCount = aProfile.bufferGeneration() - startBufferGen;

  // Update the JS runtime with the current profile sample buffer generation.
  if (pseudoStack->mRuntime) {
    JS::UpdateJSRuntimeProfilerSampleBufferGen(pseudoStack->mRuntime,
                                               aProfile.bufferGeneration(),
                                               lapCount);
  }
}

#ifdef USE_NS_STACKWALK
static
void StackWalkCallback(uint32_t aFrameNumber, void* aPC, void* aSP,
                       void* aClosure)
{
  NativeStack* nativeStack = static_cast<NativeStack*>(aClosure);
  MOZ_ASSERT(nativeStack->count < nativeStack->size);
  nativeStack->sp_array[nativeStack->count] = aSP;
  nativeStack->pc_array[nativeStack->count] = aPC;
  nativeStack->count++;
}

void TableTicker::doNativeBacktrace(ThreadProfile &aProfile, TickSample* aSample)
{
#ifndef XP_MACOSX
  uintptr_t thread = GetThreadHandle(aSample->threadProfile->GetPlatformData());
  MOZ_ASSERT(thread);
#endif
  void* pc_array[1000];
  void* sp_array[1000];
  NativeStack nativeStack = {
    pc_array,
    sp_array,
    mozilla::ArrayLength(pc_array),
    0
  };

  // Start with the current function. We use 0 as the frame number here because
  // the FramePointerStackWalk() and NS_StackWalk() calls below will use 1..N.
  // This is a bit weird but it doesn't matter because StackWalkCallback()
  // doesn't use the frame number argument.
  StackWalkCallback(/* frameNumber */ 0, aSample->pc, aSample->sp, &nativeStack);

  uint32_t maxFrames = uint32_t(nativeStack.size - nativeStack.count);
#ifdef XP_MACOSX
  pthread_t pt = GetProfiledThread(aSample->threadProfile->GetPlatformData());
  void *stackEnd = reinterpret_cast<void*>(-1);
  if (pt)
    stackEnd = static_cast<char*>(pthread_get_stackaddr_np(pt));
  nsresult rv = NS_OK;
  if (aSample->fp >= aSample->sp && aSample->fp <= stackEnd)
    rv = FramePointerStackWalk(StackWalkCallback, /* skipFrames */ 0,
                               maxFrames, &nativeStack,
                               reinterpret_cast<void**>(aSample->fp), stackEnd);
#else
  void *platformData = nullptr;
#ifdef XP_WIN
  if (aSample->isSamplingCurrentThread) {
    // In this case we want NS_StackWalk to know that it's walking the
    // current thread's stack, so we pass 0 as the thread handle.
    thread = 0;
  }
  platformData = aSample->context;
#endif // XP_WIN

  nsresult rv = NS_StackWalk(StackWalkCallback, /* skipFrames */ 0, maxFrames,
                             &nativeStack, thread, platformData);
#endif
  if (NS_SUCCEEDED(rv))
    mergeStacksIntoProfile(aProfile, aSample, nativeStack);
}
#endif

static
void doSampleStackTrace(ThreadProfile &aProfile, TickSample *aSample, bool aAddLeafAddresses)
{
  NativeStack nativeStack = { nullptr, nullptr, 0, 0 };
  mergeStacksIntoProfile(aProfile, aSample, nativeStack);

#ifdef ENABLE_SPS_LEAF_DATA
  if (aSample && aAddLeafAddresses) {
    aProfile.addTag(ProfileEntry('l', (void*)aSample->pc));
#ifdef ENABLE_ARM_LR_SAVING
    aProfile.addTag(ProfileEntry('L', (void*)aSample->lr));
#endif
  }
#endif
}

void TableTicker::Tick(TickSample* sample)
{
  // Don't allow for ticks to happen within other ticks.
  if (HasUnwinderThread()) {
    UnwinderTick(sample);
  } else {
    InplaceTick(sample);
  }
}

void TableTicker::InplaceTick(TickSample* sample)
{
  ThreadProfile& currThreadProfile = *sample->threadProfile;

  currThreadProfile.addTag(ProfileEntry('T', currThreadProfile.ThreadId()));

  PseudoStack* stack = currThreadProfile.GetPseudoStack();

#if defined(USE_NS_STACKWALK)
  if (mUseStackWalk) {
    doNativeBacktrace(currThreadProfile, sample);
  } else {
    doSampleStackTrace(currThreadProfile, sample, mAddLeafAddresses);
  }
#else
  doSampleStackTrace(currThreadProfile, sample, mAddLeafAddresses);
#endif

  // Don't process the PeudoStack's markers if we're
  // synchronously sampling the current thread.
  if (!sample->isSamplingCurrentThread) {
    ProfilerMarkerLinkedList* pendingMarkersList = stack->getPendingMarkers();
    while (pendingMarkersList && pendingMarkersList->peek()) {
      ProfilerMarker* marker = pendingMarkersList->popHead();
      currThreadProfile.addStoredMarker(marker);
      currThreadProfile.addTag(ProfileEntry('m', marker));
    }
  }

  if (sample && currThreadProfile.GetThreadResponsiveness()->HasData()) {
    mozilla::TimeDuration delta = currThreadProfile.GetThreadResponsiveness()->GetUnresponsiveDuration(sample->timestamp);
    currThreadProfile.addTag(ProfileEntry('r', static_cast<float>(delta.ToMilliseconds())));
  }

  if (sample) {
    mozilla::TimeDuration delta = sample->timestamp - sStartTime;
    currThreadProfile.addTag(ProfileEntry('t', static_cast<float>(delta.ToMilliseconds())));
  }

  // rssMemory is equal to 0 when we are not recording.
  if (sample && sample->rssMemory != 0) {
    currThreadProfile.addTag(ProfileEntry('R', static_cast<float>(sample->rssMemory)));
  }

  // ussMemory is equal to 0 when we are not recording.
  if (sample && sample->ussMemory != 0) {
    currThreadProfile.addTag(ProfileEntry('U', static_cast<float>(sample->ussMemory)));
  }

#if defined(XP_WIN)
  if (mProfilePower) {
    mIntelPowerGadget->TakeSample();
    currThreadProfile.addTag(ProfileEntry('p', static_cast<float>(mIntelPowerGadget->GetTotalPackagePowerInWatts())));
  }
#endif

  if (sLastFrameNumber != sFrameNumber) {
    currThreadProfile.addTag(ProfileEntry('f', sFrameNumber));
    sLastFrameNumber = sFrameNumber;
  }
}

namespace {

SyncProfile* NewSyncProfile()
{
  ProfileStack *stack = tlsStack.get();
  if (!stack) {
    MOZ_ASSERT(stack);
    return nullptr;
  }
  Thread::tid_t tid = Thread::GetCurrentId();

  ThreadInfo* info = new ThreadInfo("SyncProfile", tid, NS_IsMainThread(), stack, nullptr);
  SyncProfile* profile = new SyncProfile(info, GET_BACKTRACE_DEFAULT_ENTRY);
  return profile;
}

} // anonymous namespace

SyncProfile* TableTicker::GetBacktrace()
{
  SyncProfile* profile = NewSyncProfile();

  TickSample sample;
  sample.threadProfile = profile;

#if defined(HAVE_NATIVE_UNWIND)
#if defined(XP_WIN) || defined(LINUX)
  tickcontext_t context;
  sample.PopulateContext(&context);
#elif defined(XP_MACOSX)
  sample.PopulateContext(nullptr);
#endif
#endif

  sample.isSamplingCurrentThread = true;
  sample.timestamp = mozilla::TimeStamp::Now();

  if (!HasUnwinderThread()) {
    profile->BeginUnwind();
  }

  Tick(&sample);

  if (!HasUnwinderThread()) {
    profile->EndUnwind();
  }

  return profile;
}

void print_callback(const ProfileEntry& entry, const char* tagStringData) {
{
  switch (entry.getTagName()) {
    case 's':
    case 'c':
      printf_stderr("  %s\n", tagStringData);
  }
}

void mozilla_sampler_print_location1()
{
  if (!stack_key_initialized)
    profiler_init(nullptr);

  SyncProfile* syncProfile = NewSyncProfile();
  if (!syncProfile) {
    return;
  }

  syncProfile->BeginUnwind();
  doSampleStackTrace(*syncProfile, nullptr, false);
  syncProfile->EndUnwind();

  printf_stderr("Backtrace:\n");
  syncProfile->IterateTags(print_callback);
  delete syncProfile;
}


