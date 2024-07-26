/* -*- js-indent-level: 2; indent-tabs-mode: nil -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cr = Components.results;
const Cu = Components.utils;

Cu.import("resource://gre/modules/debug.js", this);
Cu.import("resource://gre/modules/Log.jsm");
Cu.import("resource://gre/modules/osfile.jsm", this);
Cu.import("resource://gre/modules/Services.jsm", this);
Cu.import("resource://gre/modules/XPCOMUtils.jsm", this);
Cu.import("resource://gre/modules/Promise.jsm", this);
Cu.import("resource://gre/modules/DeferredTask.jsm", this);
Cu.import("resource://gre/modules/Preferences.jsm");
Cu.import("resource://gre/modules/Task.jsm");
Cu.import("resource://gre/modules/Timer.jsm");

const IS_CONTENT_PROCESS = (function() {
  // We cannot use Services.appinfo here because in telemetry xpcshell tests,
  // appinfo is initially unavailable, and becomes available only later on.
  let runtime = Cc["@mozilla.org/xre/app-info;1"].getService(Ci.nsIXULRuntime);
  return runtime.processType == Ci.nsIXULRuntime.PROCESS_TYPE_CONTENT;
})();

// When modifying the payload in incompatible ways, please bump this version number
const PAYLOAD_VERSION = 4;
const PING_TYPE_MAIN = "main";
const RETENTION_DAYS = 14;

const REASON_DAILY = "daily";
const REASON_SAVED_SESSION = "saved-session";
const REASON_IDLE_DAILY = "idle-daily";
const REASON_GATHER_PAYLOAD = "gather-payload";
const REASON_TEST_PING = "test-ping";
const REASON_ENVIRONMENT_CHANGE = "environment-change";

const ENVIRONMENT_CHANGE_LISTENER = "TelemetrySession::onEnvironmentChange";

const SEC_IN_ONE_DAY  = 24 * 60 * 60;
const MS_IN_ONE_DAY   = SEC_IN_ONE_DAY * 1000;

const MIN_SUBSESSION_LENGTH_MS = 10 * 60 * 1000;

// This is the HG changeset of the Histogram.json file, used to associate
// submitted ping data with its histogram definition (bug 832007)
#expand const HISTOGRAMS_FILE_VERSION = "__HISTOGRAMS_FILE_VERSION__";

const LOGGER_NAME = "Toolkit.Telemetry";
const LOGGER_PREFIX = "TelemetrySession::";

const PREF_BRANCH = "toolkit.telemetry.";
const PREF_SERVER = PREF_BRANCH + "server";
const PREF_ENABLED = PREF_BRANCH + "enabled";
const PREF_PREVIOUS_BUILDID = PREF_BRANCH + "previousBuildID";
const PREF_CACHED_CLIENTID = PREF_BRANCH + "cachedClientID"
const PREF_FHR_UPLOAD_ENABLED = "datareporting.healthreport.uploadEnabled";
const PREF_ASYNC_PLUGIN_INIT = "dom.ipc.plugins.asyncInit";

const MESSAGE_TELEMETRY_PAYLOAD = "Telemetry:Payload";
const MESSAGE_TELEMETRY_GET_CHILD_PAYLOAD = "Telemetry:GetChildPayload";

const SESSION_STATE_FILE_NAME = "session-state.json";

// Maximum number of content payloads that we are willing to store.
const MAX_NUM_CONTENT_PAYLOADS = 10;

// Do not gather data more than once a minute
const TELEMETRY_INTERVAL = 60000;
// Delay before intializing telemetry (ms)
const TELEMETRY_DELAY = 60000;
// Delay before initializing telemetry if we're testing (ms)
const TELEMETRY_TEST_DELAY = 100;

// Seconds of idle time before pinging.
// On idle-daily a gather-telemetry notification is fired, during it probes can
// start asynchronous tasks to gather data.  On the next idle the data is sent.
const IDLE_TIMEOUT_SECONDS = 5 * 60;

var gLastMemoryPoll = null;

let gWasDebuggerAttached = false;

function getLocale() {
  return Cc["@mozilla.org/chrome/chrome-registry;1"].
         getService(Ci.nsIXULChromeRegistry).
         getSelectedLocale('global');
}

XPCOMUtils.defineLazyServiceGetter(this, "Telemetry",
                                   "@mozilla.org/base/telemetry;1",
                                   "nsITelemetry");
XPCOMUtils.defineLazyServiceGetter(this, "idleService",
                                   "@mozilla.org/widget/idleservice;1",
                                   "nsIIdleService");
XPCOMUtils.defineLazyServiceGetter(this, "cpmm",
                                   "@mozilla.org/childprocessmessagemanager;1",
                                   "nsIMessageSender");
XPCOMUtils.defineLazyServiceGetter(this, "cpml",
                                   "@mozilla.org/childprocessmessagemanager;1",
                                   "nsIMessageListenerManager");
XPCOMUtils.defineLazyServiceGetter(this, "ppmm",
                                   "@mozilla.org/parentprocessmessagemanager;1",
                                   "nsIMessageBroadcaster");
XPCOMUtils.defineLazyServiceGetter(this, "ppml",
                                   "@mozilla.org/parentprocessmessagemanager;1",
                                   "nsIMessageListenerManager");

XPCOMUtils.defineLazyModuleGetter(this, "AddonManagerPrivate",
                                  "resource://gre/modules/AddonManager.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "AsyncShutdown",
                                  "resource://gre/modules/AsyncShutdown.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "TelemetryPing",
                                  "resource://gre/modules/TelemetryPing.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "TelemetryFile",
                                  "resource://gre/modules/TelemetryFile.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "TelemetryLog",
                                  "resource://gre/modules/TelemetryLog.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "ThirdPartyCookieProbe",
                                  "resource://gre/modules/ThirdPartyCookieProbe.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "UITelemetry",
                                  "resource://gre/modules/UITelemetry.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "UpdateChannel",
                                  "resource://gre/modules/UpdateChannel.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "TelemetryEnvironment",
                                  "resource://gre/modules/TelemetryEnvironment.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "CommonUtils",
                                  "resource://services-common/utils.js");

function generateUUID() {
  let str = Cc["@mozilla.org/uuid-generator;1"].getService(Ci.nsIUUIDGenerator).generateUUID().toString();
  // strip {}
  return str.substring(1, str.length - 1);
}

/**
 * This is a policy object used to override behavior for testing.
 */
let Policy = {
  now: () => new Date(),
  setDailyTimeout: (callback, delayMs) => setTimeout(callback, delayMs),
  clearDailyTimeout: (id) => clearTimeout(id),
};

/**
 * Takes a date and returns it trunctated to a date with daily precision.
 */
function truncateToDays(date) {
  return new Date(date.getFullYear(),
                  date.getMonth(),
                  date.getDate(),
                  0, 0, 0, 0);
}

/**
 * Date.toISOString() gives us UTC times, this gives us local times in the ISO date format.
 * http://www.w3.org/TR/NOTE-datetime
 */
function toLocalTimeISOString(date) {
  function padNumber(number, places) {
    number = number.toString();
    while (number.length < places) {
      number = "0" + number;
    }
    return number;
  }

  let sign = (n) => n >= 0 ? "+" : "-";
  let tzOffset = date.getTimezoneOffset();

  // YYYY-MM-DDThh:mm:ss.sTZD (eg 1997-07-16T19:20:30.45+01:00)
  return    padNumber(date.getFullYear(), 4)
    + "-" + padNumber(date.getMonth() + 1, 2)
    + "-" + padNumber(date.getDate(), 2)
    + "T" + padNumber(date.getHours(), 2)
    + ":" + padNumber(date.getMinutes(), 2)
    + ":" + padNumber(date.getSeconds(), 2);
    + "." + date.getMilliseconds()
    + sign(tzOffset) + Math.abs(Math.floor(tzOffset / 60))
    + ":" + Math.abs(tzOffset % 60);
}

/**
 * Read current process I/O counters.
 */
let processInfo = {
  _initialized: false,
  _IO_COUNTERS: null,
  _kernel32: null,
  _GetProcessIoCounters: null,
  _GetCurrentProcess: null,
  getCounters: function() {
    let isWindows = ("@mozilla.org/windows-registry-key;1" in Components.classes);
    if (isWindows)
      return this.getCounters_Windows();
    return null;
  },
  getCounters_Windows: function() {
    if (!this._initialized){
      Cu.import("resource://gre/modules/ctypes.jsm");
      this._IO_COUNTERS = new ctypes.StructType("IO_COUNTERS", [
        {'readOps': ctypes.unsigned_long_long},
        {'writeOps': ctypes.unsigned_long_long},
        {'otherOps': ctypes.unsigned_long_long},
        {'readBytes': ctypes.unsigned_long_long},
        {'writeBytes': ctypes.unsigned_long_long},
        {'otherBytes': ctypes.unsigned_long_long} ]);
      try {
        this._kernel32 = ctypes.open("Kernel32.dll");
        this._GetProcessIoCounters = this._kernel32.declare("GetProcessIoCounters",
          ctypes.winapi_abi,
          ctypes.bool, // return
          ctypes.voidptr_t, // hProcess
          this._IO_COUNTERS.ptr); // lpIoCounters
        this._GetCurrentProcess = this._kernel32.declare("GetCurrentProcess",
          ctypes.winapi_abi,
          ctypes.voidptr_t); // return
        this._initialized = true;
      } catch (err) {
        return null;
      }
    }
    let io = new this._IO_COUNTERS();
    if(!this._GetProcessIoCounters(this._GetCurrentProcess(), io.address()))
      return null;
    return [parseInt(io.readBytes), parseInt(io.writeBytes)];
  }
};

/**
 * This object allows the serialisation of asynchronous tasks. This is particularly
 * useful to serialise write access to the disk in order to prevent race conditions
 * to corrupt the data being written.
 * We are using this to synchronize saving to the file that TelemetrySession persists
 * its state in.
 */
let gStateSaveSerializer = {
  _queuedOperations: [],
  _queuedInProgress: false,
  _log: Log.repository.getLoggerWithMessagePrefix(LOGGER_NAME, LOGGER_PREFIX),

  /**
   * Enqueues an operation to a list to serialise their execution in order to prevent race
   * conditions. Useful to serialise access to disk.
   *
   * @param {Function} aFunction The task function to enqueue. It must return a promise.
   * @return {Promise} A promise resolved when the enqueued task completes.
   */
  enqueueTask: function (aFunction) {
    let promise = new Promise((resolve, reject) =>
      this._queuedOperations.push([aFunction, resolve, reject]));

    if (this._queuedOperations.length == 1) {
      this._popAndPerformQueuedOperation();
    }
    return promise;
  },

  /**
   * Make sure to flush all the pending operations.
   * @return {Promise} A promise resolved when all the pending operations have completed.
   */
  flushTasks: function () {
    let dummyTask = () => new Promise(resolve => resolve());
    return this.enqueueTask(dummyTask);
  },

  /**
   * Pop a task from the queue, executes it and continue to the next one.
   * This function recursively pops all the tasks.
   */
  _popAndPerformQueuedOperation: function () {
    if (!this._queuedOperations.length || this._queuedInProgress) {
      return;
    }

    this._log.trace("_popAndPerformQueuedOperation - Performing queued operation.");
    let [func, resolve, reject] = this._queuedOperations.shift();
    let promise;

    try {
      this._queuedInProgress = true;
      promise = func();
    } catch (ex) {
      this._log.warn("_popAndPerformQueuedOperation - Queued operation threw during execution. ",
                     ex);
      this._queuedInProgress = false;
      reject(ex);
      this._popAndPerformQueuedOperation();
      return;
    }

    if (!promise || typeof(promise.then) != "function") {
      let msg = "Queued operation did not return a promise: " + func;
      this._log.warn("_popAndPerformQueuedOperation - " + msg);

      this._queuedInProgress = false;
      reject(new Error(msg));
      this._popAndPerformQueuedOperation();
      return;
    }

    promise.then(result => {
        this._log.trace("_popAndPerformQueuedOperation - Queued operation completed.");
        this._queuedInProgress = false;
        resolve(result);
        this._popAndPerformQueuedOperation();
      },
      error => {
        this._log.warn("_popAndPerformQueuedOperation - Failure when performing queued operation.",
                       error);
        this._queuedInProgress = false;
        reject(error);
        this._popAndPerformQueuedOperation();
      });
  },
};

this.EXPORTED_SYMBOLS = ["TelemetrySession"];

this.TelemetrySession = Object.freeze({
  Constants: Object.freeze({
    PREF_ENABLED: PREF_ENABLED,
    PREF_SERVER: PREF_SERVER,
    PREF_PREVIOUS_BUILDID: PREF_PREVIOUS_BUILDID,
  }),
  /**
   * Send a ping to a test server. Used only for testing.
   */
  testPing: function() {
    return Impl.testPing();
  },
  /**
   * Returns the current telemetry payload.
   * @param reason Optional, the reason to trigger the payload.
   * @param clearSubsession Optional, whether to clear subsession specific data.
   * @returns Object
   */
  getPayload: function(reason, clearSubsession = false) {
    return Impl.getPayload(reason, clearSubsession);
  },
  /**
   * Asks the content processes to send their payloads.
   * @returns Object
   */
  requestChildPayloads: function() {
    return Impl.requestChildPayloads();
  },
  /**
   * Save histograms to a file.
   * Used only for testing purposes.
   *
   * @param {nsIFile} aFile The file to load from.
   */
  testSaveHistograms: function(aFile) {
    return Impl.testSaveHistograms(aFile);
  },
  /**
   * Collect and store information about startup.
   */
  gatherStartup: function() {
    return Impl.gatherStartup();
  },
  /**
   * Inform the ping which AddOns are installed.
   *
   * @param aAddOns - The AddOns.
   */
  setAddOns: function(aAddOns) {
    return Impl.setAddOns(aAddOns);
  },
  /**
   * Descriptive metadata
   *
   * @param  reason
   *         The reason for the telemetry ping, this will be included in the
   *         returned metadata,
   * @return The metadata as a JS object
   */
  getMetadata: function(reason) {
    return Impl.getMetadata(reason);
  },
  /**
   * Used only for testing purposes.
   */
  reset: function() {
    Impl._subsessionCounter = 0;
    Impl._profileSubsessionCounter = 0;
    this.uninstall();
    return this.setup();
  },
  /**
   * Used only for testing purposes.
   * @param {Boolean} [aForceSavePending=true] If true, always saves the ping whether Telemetry
   *        can send pings or not, which is used for testing.
   */
  shutdown: function(aForceSavePending = true) {
    return Impl.shutdownChromeProcess(aForceSavePending);
  },
  /**
   * Used only for testing purposes.
   */
  setup: function() {
    return Impl.setupChromeProcess(true);
  },
  /**
   * Used only for testing purposes.
   */
  uninstall: function() {
    try {
      Impl.uninstall();
    } catch (ex) {
      // Ignore errors
    }
  },
  /**
   * Send a notification.
   */
  observe: function (aSubject, aTopic, aData) {
    return Impl.observe(aSubject, aTopic, aData);
  },
  /**
   * The client id send with the telemetry ping.
   *
   * @return The client id as string, or null.
   */
   get clientID() {
    return Impl.clientID;
   },
});

let Impl = {
  _histograms: {},
  _initialized: false,
  _log: null,
  _prevValues: {},
  // Regex that matches histograms we care about during startup.
  // Keep this in sync with gen-histogram-bucket-ranges.py.
  _startupHistogramRegex: /SQLITE|HTTP|SPDY|CACHE|DNS/,
  _slowSQLStartup: {},
  _hasWindowRestoredObserver: false,
  _hasXulWindowVisibleObserver: false,
  _startupIO : {},
  // The previous build ID, if this is the first run with a new build.
  // Null if this is the first run, or the previous build ID is unknown.
  _previousBuildId: null,
  // Telemetry payloads sent by child processes.
  // Each element is in the format {source: <weak-ref>, payload: <object>},
  // where source is a weak reference to the child process,
  // and payload is the telemetry payload from that child process.
  _childTelemetry: [],
  // Generate a unique id once per session so the server can cope with duplicate
  // submissions, orphaning and other oddities. The id is shared across subsessions.
  _sessionId: generateUUID(),
  // Random subsession id.
  _subsessionId: null,
  // Subsession id of the previous subsession (even if it was in a different session),
  // null on first run.
  _previousSubsessionId: null,
  // The running no. of subsessions since the start of the browser session
  _subsessionCounter: 0,
  // The running no. of all subsessions for the whole profile life time
  _profileSubsessionCounter: 0,
  // Date of the last session split
  _subsessionStartDate: null,
  // The timer used for daily collections.
  _dailyTimerId: null,
  // A task performing delayed initialization of the chrome process
  _delayedInitTask: null,

  /**
   * Gets a series of simple measurements (counters). At the moment, this
   * only returns startup data from nsIAppStartup.getStartupInfo().
   *
   * @return simple measurements as a dictionary.
   */
  getSimpleMeasurements: function getSimpleMeasurements(forSavedSession) {
    this._log.trace("getSimpleMeasurements");

    let si = Services.startup.getStartupInfo();

    // Measurements common to chrome and content processes.
    var ret = {
      // uptime in minutes
      uptime: Math.round((new Date() - si.process) / 60000)
    }

    // Look for app-specific timestamps
    var appTimestamps = {};
    try {
      let o = {};
      Cu.import("resource://gre/modules/TelemetryTimestamps.jsm", o);
      appTimestamps = o.TelemetryTimestamps.get();
    } catch (ex) {}
    try {
      if (!IS_CONTENT_PROCESS) {
        ret.addonManager = AddonManagerPrivate.getSimpleMeasures();
      }
    } catch (ex) {}
    try {
      if (!IS_CONTENT_PROCESS) {
        ret.UITelemetry = UITelemetry.getSimpleMeasures();
      }
    } catch (ex) {}

    if (si.process) {
      for each (let field in Object.keys(si)) {
        if (field == "process")
          continue;
        ret[field] = si[field] - si.process
      }

      for (let p in appTimestamps) {
        if (!(p in ret) && appTimestamps[p])
          ret[p] = appTimestamps[p] - si.process;
      }
    }

    ret.startupInterrupted = Number(Services.startup.interrupted);

    ret.js = Cu.getJSEngineTelemetryValue();

    let maximalNumberOfConcurrentThreads = Telemetry.maximalNumberOfConcurrentThreads;
    if (maximalNumberOfConcurrentThreads) {
      ret.maximalNumberOfConcurrentThreads = maximalNumberOfConcurrentThreads;
    }

    if (IS_CONTENT_PROCESS) {
      return ret;
    }

    // Measurements specific to chrome process

    // Update debuggerAttached flag
    let debugService = Cc["@mozilla.org/xpcom/debug;1"].getService(Ci.nsIDebug2);
    let isDebuggerAttached = debugService.isDebuggerAttached;
    gWasDebuggerAttached = gWasDebuggerAttached || isDebuggerAttached;
    ret.debuggerAttached = Number(gWasDebuggerAttached);

    let shutdownDuration = Telemetry.lastShutdownDuration;
    if (shutdownDuration)
      ret.shutdownDuration = shutdownDuration;

    let failedProfileLockCount = Telemetry.failedProfileLockCount;
    if (failedProfileLockCount)
      ret.failedProfileLockCount = failedProfileLockCount;

    for (let ioCounter in this._startupIO)
      ret[ioCounter] = this._startupIO[ioCounter];

    let hasPingBeenSent = false;
    try {
      hasPingBeenSent = Telemetry.getHistogramById("TELEMETRY_SUCCESS").snapshot().sum > 0;
    } catch(e) {
    }
    if (!forSavedSession || hasPingBeenSent) {
      ret.savedPings = TelemetryFile.pingsLoaded;
    }

    ret.activeTicks = -1;
    if ("@mozilla.org/datareporting/service;1" in Cc) {
      let drs = Cc["@mozilla.org/datareporting/service;1"]
                  .getService(Ci.nsISupports)
                  .wrappedJSObject;

      let sr = drs.getSessionRecorder();
      if (sr) {
        ret.activeTicks = sr.activeTicks;
      }
    }

    ret.pingsOverdue = TelemetryFile.pingsOverdue;
    ret.pingsDiscarded = TelemetryFile.pingsDiscarded;

    return ret;
  },

  /**
   * When reflecting a histogram into JS, Telemetry hands us an object
   * with the following properties:
   *
   * - min, max, histogram_type, sum, sum_squares_{lo,hi}: simple integers;
   * - log_sum, log_sum_squares: doubles;
   * - counts: array of counts for histogram buckets;
   * - ranges: array of calculated bucket sizes.
   *
   * This format is not straightforward to read and potentially bulky
   * with lots of zeros in the counts array.  Packing histograms makes
   * raw histograms easier to read and compresses the data a little bit.
   *
   * Returns an object:
   * { range: [min, max], bucket_count: <number of buckets>,
   *   histogram_type: <histogram_type>, sum: <sum>,
   *   sum_squares_lo: <sum_squares_lo>,
   *   sum_squares_hi: <sum_squares_hi>,
   *   log_sum: <log_sum>, log_sum_squares: <log_sum_squares>,
   *   values: { bucket1: count1, bucket2: count2, ... } }
   */
  packHistogram: function packHistogram(hgram) {
    let r = hgram.ranges;;
    let c = hgram.counts;
    let retgram = {
      range: [r[1], r[r.length - 1]],
      bucket_count: r.length,
      histogram_type: hgram.histogram_type,
      values: {},
      sum: hgram.sum
    };

    if (hgram.histogram_type == Telemetry.HISTOGRAM_EXPONENTIAL) {
      retgram.log_sum = hgram.log_sum;
      retgram.log_sum_squares = hgram.log_sum_squares;
    } else {
      retgram.sum_squares_lo = hgram.sum_squares_lo;
      retgram.sum_squares_hi = hgram.sum_squares_hi;
    }

    let first = true;
    let last = 0;

    for (let i = 0; i < c.length; i++) {
      let value = c[i];
      if (!value)
        continue;

      // add a lower bound
      if (i && first) {
        retgram.values[r[i - 1]] = 0;
      }
      first = false;
      last = i + 1;
      retgram.values[r[i]] = value;
    }

    // add an upper bound
    if (last && last < c.length)
      retgram.values[r[last]] = 0;
    return retgram;
  },

  getHistograms: function getHistograms(subsession, clearSubsession) {
    this._log.trace("getHistograms - subsession: " + subsession + ", clearSubsession: " + clearSubsession);

    let registered =
      Telemetry.registeredHistograms(Ci.nsITelemetry.DATASET_RELEASE_CHANNEL_OPTIN, []);
    let hls = subsession ? Telemetry.snapshotSubsessionHistograms(clearSubsession)
                         : Telemetry.histogramSnapshots;
    let ret = {};

    for (let name of registered) {
      for (let n of [name, "STARTUP_" + name]) {
        if (n in hls) {
          ret[n] = this.packHistogram(hls[n]);
        }
      }
    }

    return ret;
  },

  getAddonHistograms: function getAddonHistograms() {
    this._log.trace("getAddonHistograms");

    let ahs = Telemetry.addonHistogramSnapshots;
    let ret = {};

    for (let addonName in ahs) {
      let addonHistograms = ahs[addonName];
      let packedHistograms = {};
      for (let name in addonHistograms) {
        packedHistograms[name] = this.packHistogram(addonHistograms[name]);
      }
      if (Object.keys(packedHistograms).length != 0)
        ret[addonName] = packedHistograms;
    }

    return ret;
  },

  getKeyedHistograms: function(subsession, clearSubsession) {
    this._log.trace("getKeyedHistograms - subsession: " + subsession + ", clearSubsession: " + clearSubsession);

    let registered =
      Telemetry.registeredKeyedHistograms(Ci.nsITelemetry.DATASET_RELEASE_CHANNEL_OPTIN, []);
    let ret = {};

    for (let id of registered) {
      ret[id] = {};
      let keyed = Telemetry.getKeyedHistogramById(id);
      let snapshot = null;
      if (subsession) {
        snapshot = clearSubsession ? keyed.snapshotSubsessionAndClear()
                                   : keyed.subsessionSnapshot();
      } else {
        snapshot = keyed.snapshot();
      }
      for (let key of Object.keys(snapshot)) {
        ret[id][key] = this.packHistogram(snapshot[key]);
      }
    }

    return ret;
  },

  /**
   * Descriptive metadata
   *
   * @param  reason
   *         The reason for the telemetry ping, this will be included in the
   *         returned metadata,
   * @return The metadata as a JS object
   */
  getMetadata: function getMetadata(reason) {
    this._log.trace("getMetadata - Reason " + reason);

    let sessionStartDate = toLocalTimeISOString(truncateToDays(this._sessionStartDate));
    let subsessionStartDate = toLocalTimeISOString(truncateToDays(this._subsessionStartDate));
    // Compute the subsession length in milliseconds, then convert to seconds.
    let subsessionLength =
      Math.floor((Policy.now() - this._subsessionStartDate.getTime()) / 1000);

    let ret = {
      reason: reason,
      revision: HISTOGRAMS_FILE_VERSION,
      asyncPluginInit: Preferences.get(PREF_ASYNC_PLUGIN_INIT, false),

      // Date.getTimezoneOffset() unintuitively returns negative values if we are ahead of
      // UTC and vice versa (e.g. -60 for UTC+1). We invert the sign here.
      timezoneOffset: -this._subsessionStartDate.getTimezoneOffset(),
      previousBuildId: this._previousBuildId,

      sessionId: this._sessionId,
      subsessionId: this._subsessionId,
      previousSubsessionId: this._previousSubsessionId,

      subsessionCounter: this._subsessionCounter,
      profileSubsessionCounter: this._profileSubsessionCounter,

      sessionStartDate: sessionStartDate,
      subsessionStartDate: subsessionStartDate,
      subsessionLength: subsessionLength,
    };

    // TODO: Remove this when bug 1124128 lands.
    if (this._addons)
      ret.addons = this._addons;

    // TODO: Remove this when bug 1124128 lands.
    let flashVersion = this.getFlashVersion();
    if (flashVersion)
      ret.flashVersion = flashVersion;

    return ret;
  },

  /**
   * Pull values from about:memory into corresponding histograms
   */
  gatherMemory: function gatherMemory() {
    this._log.trace("gatherMemory");

    let mgr;
    try {
      mgr = Cc["@mozilla.org/memory-reporter-manager;1"].
            getService(Ci.nsIMemoryReporterManager);
    } catch (e) {
      // OK to skip memory reporters in xpcshell
      return;
    }

    let histogram = Telemetry.getHistogramById("TELEMETRY_MEMORY_REPORTER_MS");
    let startTime = new Date();

    // Get memory measurements from distinguished amount attributes.  We used
    // to measure "explicit" too, but it could cause hangs, and the data was
    // always really noisy anyway.  See bug 859657.
    //
    // test_TelemetryPing.js relies on some of these histograms being
    // here.  If you remove any of the following histograms from here, you'll
    // have to modify test_TelemetryPing.js:
    //
    //   * MEMORY_JS_GC_HEAP, and
    //   * MEMORY_JS_COMPARTMENTS_SYSTEM.
    //
    // The distinguished amount attribute names don't match the telemetry id
    // names in some cases due to a combination of (a) historical reasons, and
    // (b) the fact that we can't change telemetry id names without breaking
    // data continuity.
    //
    let boundHandleMemoryReport = this.handleMemoryReport.bind(this);
    function h(id, units, amountName) {
      try {
        // If mgr[amountName] throws an exception, just move on -- some amounts
        // aren't available on all platforms.  But if the attribute simply
        // isn't present, that indicates the distinguished amounts have changed
        // and this file hasn't been updated appropriately.
        let amount = mgr[amountName];
        NS_ASSERT(amount !== undefined,
                  "telemetry accessed an unknown distinguished amount");
        boundHandleMemoryReport(id, units, amount);
      } catch (e) {
      };
    }
    let b = (id, n) => h(id, Ci.nsIMemoryReporter.UNITS_BYTES, n);
    let c = (id, n) => h(id, Ci.nsIMemoryReporter.UNITS_COUNT, n);
    let cc= (id, n) => h(id, Ci.nsIMemoryReporter.UNITS_COUNT_CUMULATIVE, n);
    let p = (id, n) => h(id, Ci.nsIMemoryReporter.UNITS_PERCENTAGE, n);

    b("MEMORY_VSIZE", "vsize");
    b("MEMORY_VSIZE_MAX_CONTIGUOUS", "vsizeMaxContiguous");
    b("MEMORY_RESIDENT", "residentFast");
    b("MEMORY_HEAP_ALLOCATED", "heapAllocated");
    p("MEMORY_HEAP_COMMITTED_UNUSED_RATIO", "heapOverheadRatio");
    b("MEMORY_JS_GC_HEAP", "JSMainRuntimeGCHeap");
    b("MEMORY_JS_MAIN_RUNTIME_TEMPORARY_PEAK", "JSMainRuntimeTemporaryPeak");
    c("MEMORY_JS_COMPARTMENTS_SYSTEM", "JSMainRuntimeCompartmentsSystem");
    c("MEMORY_JS_COMPARTMENTS_USER", "JSMainRuntimeCompartmentsUser");
    b("MEMORY_IMAGES_CONTENT_USED_UNCOMPRESSED", "imagesContentUsedUncompressed");
    b("MEMORY_STORAGE_SQLITE", "storageSQLite");
    cc("MEMORY_EVENTS_VIRTUAL", "lowMemoryEventsVirtual");
    cc("MEMORY_EVENTS_PHYSICAL", "lowMemoryEventsPhysical");
    c("GHOST_WINDOWS", "ghostWindows");
    cc("PAGE_FAULTS_HARD", "pageFaultsHard");

    histogram.add(new Date() - startTime);
  },

  handleMemoryReport: function(id, units, amount) {
    let val;
    if (units == Ci.nsIMemoryReporter.UNITS_BYTES) {
      val = Math.floor(amount / 1024);
    }
    else if (units == Ci.nsIMemoryReporter.UNITS_PERCENTAGE) {
      // UNITS_PERCENTAGE amounts are 100x greater than their raw value.
      val = Math.floor(amount / 100);
    }
    else if (units == Ci.nsIMemoryReporter.UNITS_COUNT) {
      val = amount;
    }
    else if (units == Ci.nsIMemoryReporter.UNITS_COUNT_CUMULATIVE) {
      // If the reporter gives us a cumulative count, we'll report the
      // difference in its value between now and our previous ping.

      if (!(id in this._prevValues)) {
        // If this is the first time we're reading this reporter, store its
        // current value but don't report it in the telemetry ping, so we
        // ignore the effect startup had on the reporter.
        this._prevValues[id] = amount;
        return;
      }

      val = amount - this._prevValues[id];
      this._prevValues[id] = amount;
    }
    else {
      NS_ASSERT(false, "Can't handle memory reporter with units " + units);
      return;
    }

    let h = this._histograms[id];
    if (!h) {
      h = Telemetry.getHistogramById(id);
      this._histograms[id] = h;
    }
    h.add(val);
  },

  /**
   * Return true if we're interested in having a STARTUP_* histogram for
   * the given histogram name.
   */
  isInterestingStartupHistogram: function isInterestingStartupHistogram(name) {
    return this._startupHistogramRegex.test(name);
  },

  getChildPayloads: function getChildPayloads() {
    return [for (child of this._childTelemetry) child.payload];
  },

  /**
   * Make a copy of interesting histograms at startup.
   */
  gatherStartupHistograms: function gatherStartupHistograms() {
    this._log.trace("gatherStartupHistograms");

    let info =
      Telemetry.registeredHistograms(Ci.nsITelemetry.DATASET_RELEASE_CHANNEL_OPTIN, []);
    let snapshots = Telemetry.histogramSnapshots;
    for (let name of info) {
      // Only duplicate histograms with actual data.
      if (this.isInterestingStartupHistogram(name) && name in snapshots) {
        Telemetry.histogramFrom("STARTUP_" + name, name);
      }
    }
  },

  /**
   * Get the current session's payload using the provided
   * simpleMeasurements and info, which are typically obtained by a call
   * to |this.getSimpleMeasurements| and |this.getMetadata|,
   * respectively.
   */
  assemblePayloadWithMeasurements: function(simpleMeasurements, info, reason, clearSubsession) {
    const isSubsession = !this._isClassicReason(reason);
    this._log.trace("assemblePayloadWithMeasurements - reason: " + reason +
                    ", submitting subsession data: " + isSubsession);

    // Payload common to chrome and content processes.
    let payloadObj = {
      ver: PAYLOAD_VERSION,
      simpleMeasurements: simpleMeasurements,
      histograms: this.getHistograms(isSubsession, clearSubsession),
      keyedHistograms: this.getKeyedHistograms(isSubsession, clearSubsession),
      chromeHangs: Telemetry.chromeHangs,
      log: TelemetryLog.entries(),
    };

    if (IS_CONTENT_PROCESS) {
      return payloadObj;
    }

    // Additional payload for chrome process.
    payloadObj.info = info;
    payloadObj.slowSQL = Telemetry.slowSQL;
    payloadObj.fileIOReports = Telemetry.fileIOReports;
    payloadObj.lateWrites = Telemetry.lateWrites;
    payloadObj.addonHistograms = this.getAddonHistograms();
    payloadObj.addonDetails = AddonManagerPrivate.getTelemetryDetails();
    payloadObj.UIMeasurements = UITelemetry.getUIMeasurements();

    if (Object.keys(this._slowSQLStartup).length != 0 &&
        (Object.keys(this._slowSQLStartup.mainThread).length ||
         Object.keys(this._slowSQLStartup.otherThreads).length)) {
      payloadObj.slowSQLStartup = this._slowSQLStartup;
    }

    if (this._childTelemetry.length) {
      payloadObj.childPayloads = this.getChildPayloads();
    }

    return payloadObj;
  },

  /**
   * Start a new subsession.
   */
  startNewSubsession: function () {
    this._subsessionStartDate = Policy.now();
    this._previousSubsessionId = this._subsessionId;
    this._subsessionId = generateUUID();
    this._subsessionCounter++;
    this._profileSubsessionCounter++;
  },

  getSessionPayload: function getSessionPayload(reason, clearSubsession) {
    this._log.trace("getSessionPayload - reason: " + reason + ", clearSubsession: " + clearSubsession);

    let measurements = this.getSimpleMeasurements(reason == REASON_SAVED_SESSION);
    let info = !IS_CONTENT_PROCESS ? this.getMetadata(reason) : null;
    let payload = this.assemblePayloadWithMeasurements(measurements, info, reason, clearSubsession);

    if (!IS_CONTENT_PROCESS && clearSubsession) {
      this.startNewSubsession();
      // Persist session data to disk (don't wait until it completes).
      let sessionData = this._getSessionDataObject();
      gStateSaveSerializer.enqueueTask(() => this._saveSessionData(sessionData));
      this._rescheduleDailyTimer();
    }

    return payload;
  },

  /**
   * Send data to the server. Record success/send-time in histograms
   */
  send: function send(reason) {
    this._log.trace("send - Reason " + reason);
    // populate histograms one last time
    this.gatherMemory();

    const isSubsession = !this._isClassicReason(reason);
    let payload = this.getSessionPayload(reason, isSubsession);
    let options = {
      retentionDays: RETENTION_DAYS,
      addClientId: true,
      addEnvironment: true,
    };
    return TelemetryPing.send(PING_TYPE_MAIN, payload, options);
  },

  attachObservers: function attachObservers() {
    if (!this._initialized)
      return;
    Services.obs.addObserver(this, "cycle-collector-begin", false);
    Services.obs.addObserver(this, "idle-daily", false);
  },

  detachObservers: function detachObservers() {
    if (!this._initialized)
      return;
    Services.obs.removeObserver(this, "idle-daily");
    Services.obs.removeObserver(this, "cycle-collector-begin");
    if (this._isIdleObserver) {
      idleService.removeIdleObserver(this, IDLE_TIMEOUT_SECONDS);
      this._isIdleObserver = false;
    }
  },

  /**
   * Perform telemetry initialization for either chrome or content process.
   */
  enableTelemetryRecording: function enableTelemetryRecording(testing) {

#ifdef MOZILLA_OFFICIAL
    if (!Telemetry.isOfficialTelemetry && !testing) {
      this._log.config("enableTelemetryRecording - Can't send data, disabling Telemetry recording.");
      return false;
    }
#endif

    let enabled = Preferences.get(PREF_ENABLED, false);
    if (!enabled) {
      this._log.config("enableTelemetryRecording - Telemetry is disabled, turning off Telemetry recording.");
      return false;
    }

    return true;
  },

  /**
   * Initializes telemetry within a timer. If there is no PREF_SERVER set, don't turn on telemetry.
   */
  setupChromeProcess: function setupChromeProcess(testing) {
    this._initStarted = true;
    if (testing && !this._log) {
      this._log = Log.repository.getLoggerWithMessagePrefix(LOGGER_NAME, LOGGER_PREFIX);
    }

    this._log.trace("setupChromeProcess");

    if (this._delayedInitTask) {
      this._log.error("setupTelemetry - init task already running");
      return this._delayedInitTask;
    }

    if (this._initialized && !testing) {
      this._log.error("setupTelemetry - already initialized");
      return Promise.resolve();
    }

    this.startNewSubsession();
    // startNewSubsession sets |_subsessionStartDate| to the current date/time. Use
    // the very same value for |_sessionStartDate|.
    this._sessionStartDate = this._subsessionStartDate;

    // Initialize some probes that are kept in their own modules
    this._thirdPartyCookies = new ThirdPartyCookieProbe();
    this._thirdPartyCookies.init();

    // Record old value and update build ID preference if this is the first
    // run with a new build ID.
    let previousBuildId = Preferences.get(PREF_PREVIOUS_BUILDID, null);
    let thisBuildID = Services.appinfo.appBuildID;
    // If there is no previousBuildId preference, we send null to the server.
    if (previousBuildId != thisBuildID) {
      this._previousBuildId = previousBuildId;
      Preferences.set(PREF_PREVIOUS_BUILDID, thisBuildID);
    }

    if (!this.enableTelemetryRecording(testing)) {
      this._log.config("setupChromeProcess - Telemetry recording is disabled, skipping Chrome process setup.");
      return Promise.resolve();
    }

    TelemetryPing.shutdown.addBlocker("TelemetrySession: shutting down",
                                      () => this.shutdownChromeProcess(),
                                      () => this._getState());

    Services.obs.addObserver(this, "sessionstore-windows-restored", false);
#ifdef MOZ_WIDGET_ANDROID
    Services.obs.addObserver(this, "application-background", false);
#endif
    Services.obs.addObserver(this, "xul-window-visible", false);
    this._hasWindowRestoredObserver = true;
    this._hasXulWindowVisibleObserver = true;

    ppml.addMessageListener(MESSAGE_TELEMETRY_PAYLOAD, this);

    // Delay full telemetry initialization to give the browser time to
    // run various late initializers. Otherwise our gathered memory
    // footprint and other numbers would be too optimistic.
    let deferred = Promise.defer();
    this._delayedInitTask = new DeferredTask(function* () {
      try {
        this._initialized = true;

        let hasLoaded = yield this._loadSessionData();
        if (!hasLoaded) {
          // We could not load a valid session data file. Create one.
          yield this._saveSessionData(this._getSessionDataObject()).catch(() =>
            this._log.error("setupChromeProcess - Could not write session data to disk."));
        }
        this.attachObservers();
        this.gatherMemory();

        Telemetry.asyncFetchTelemetryData(function () {});
        this._rescheduleDailyTimer();

        TelemetryEnvironment.registerChangeListener(ENVIRONMENT_CHANGE_LISTENER,
                                                    () => this._onEnvironmentChange());

        deferred.resolve();
      } catch (e) {
        deferred.reject();
      } finally {
        this._delayedInitTask = null;
      }
    }.bind(this), testing ? TELEMETRY_TEST_DELAY : TELEMETRY_DELAY);

    this._delayedInitTask.arm();
    return deferred.promise;
  },

  /**
   * Initializes telemetry for a content process.
   */
  setupContentProcess: function setupContentProcess() {
    this._log.trace("setupContentProcess");

    if (!this.enableTelemetryRecording()) {
      return;
    }

    Services.obs.addObserver(this, "content-child-shutdown", false);
    cpml.addMessageListener(MESSAGE_TELEMETRY_GET_CHILD_PAYLOAD, this);

    this.gatherStartupHistograms();

    let delayedTask = new DeferredTask(function* () {
      this._initialized = true;

      this.attachObservers();
      this.gatherMemory();
    }.bind(this), TELEMETRY_DELAY);

    delayedTask.arm();
  },

  getFlashVersion: function getFlashVersion() {
    this._log.trace("getFlashVersion");
    let host = Cc["@mozilla.org/plugin/host;1"].getService(Ci.nsIPluginHost);
    let tags = host.getPluginTags();

    for (let i = 0; i < tags.length; i++) {
      if (tags[i].name == "Shockwave Flash")
        return tags[i].version;
    }

    return null;
  },

  receiveMessage: function receiveMessage(message) {
    this._log.trace("receiveMessage - Message name " + message.name);
    switch (message.name) {
    case MESSAGE_TELEMETRY_PAYLOAD:
    {
      let source = message.data.childUUID;
      delete message.data.childUUID;

      for (let child of this._childTelemetry) {
        if (child.source === source) {
          // Update existing telemetry data.
          child.payload = message.data;
          return;
        }
      }
      // Did not find existing child in this._childTelemetry.
      this._childTelemetry.push({
        source: source,
        payload: message.data,
      });

      if (this._childTelemetry.length == MAX_NUM_CONTENT_PAYLOADS + 1) {
        this._childTelemetry.shift();
        Telemetry.getHistogramById("TELEMETRY_DISCARDED_CONTENT_PINGS_COUNT").add();
      }

      break;
    }
    case MESSAGE_TELEMETRY_GET_CHILD_PAYLOAD:
    {
      this.sendContentProcessPing("saved-session");
      break;
    }
    default:
      throw new Error("Telemetry.receiveMessage: bad message name");
    }
  },

  _processUUID: generateUUID(),

  sendContentProcessPing: function sendContentProcessPing(reason) {
    this._log.trace("sendContentProcessPing - Reason " + reason);
    const isSubsession = !this._isClassicReason(reason);
    let payload = this.getSessionPayload(reason, isSubsession);
    payload.childUUID = this._processUUID;
    cpmm.sendAsyncMessage(MESSAGE_TELEMETRY_PAYLOAD, payload);
  },

  savePendingPings: function savePendingPings() {
    this._log.trace("savePendingPings");
    let payload = this.getSessionPayload(REASON_SAVED_SESSION, false);
    let options = {
      retentionDays: RETENTION_DAYS,
      addClientId: true,
      addEnvironment: true,
    };
    return TelemetryPing.savePendingPings(PING_TYPE_MAIN, payload, options);
  },

  testSaveHistograms: function testSaveHistograms(file) {
    this._log.trace("testSaveHistograms - Path: " + file.path);
    let payload = this.getSessionPayload(REASON_SAVED_SESSION, false);
    let options = {
      retentionDays: RETENTION_DAYS,
      addClientId: true,
      addEnvironment: true,
      overwrite: true,
      filePath: file.path,
    };
    return TelemetryPing.testSavePingToFile(PING_TYPE_MAIN, payload, options);
  },

  /**
   * Remove observers to avoid leaks
   */
  uninstall: function uninstall() {
    this.detachObservers();
    if (this._hasWindowRestoredObserver) {
      Services.obs.removeObserver(this, "sessionstore-windows-restored");
      this._hasWindowRestoredObserver = false;
    }
    if (this._hasXulWindowVisibleObserver) {
      Services.obs.removeObserver(this, "xul-window-visible");
      this._hasXulWindowVisibleObserver = false;
    }
#ifdef MOZ_WIDGET_ANDROID
    Services.obs.removeObserver(this, "application-background", false);
#endif
  },

  getPayload: function getPayload(reason, clearSubsession) {
    this._log.trace("getPayload - clearSubsession: " + clearSubsession);
    reason = reason || REASON_GATHER_PAYLOAD;
    // This function returns the current Telemetry payload to the caller.
    // We only gather startup info once.
    if (Object.keys(this._slowSQLStartup).length == 0) {
      this.gatherStartupHistograms();
      this._slowSQLStartup = Telemetry.slowSQL;
    }
    this.gatherMemory();
    return this.getSessionPayload(reason, clearSubsession);
  },

  requestChildPayloads: function() {
    this._log.trace("requestChildPayloads");
    ppmm.broadcastAsyncMessage(MESSAGE_TELEMETRY_GET_CHILD_PAYLOAD, {});
  },

  gatherStartup: function gatherStartup() {
    this._log.trace("gatherStartup");
    let counters = processInfo.getCounters();
    if (counters) {
      [this._startupIO.startupSessionRestoreReadBytes,
        this._startupIO.startupSessionRestoreWriteBytes] = counters;
    }
    this.gatherStartupHistograms();
    this._slowSQLStartup = Telemetry.slowSQL;
  },

  setAddOns: function setAddOns(aAddOns) {
    this._addons = aAddOns;
  },

  sendIdlePing: function sendIdlePing(aTest) {
    this._log.trace("sendIdlePing");
    if (this._isIdleObserver) {
      idleService.removeIdleObserver(this, IDLE_TIMEOUT_SECONDS);
      this._isIdleObserver = false;
    }
    if (aTest) {
      return this.send(REASON_TEST_PING);
    } else if (Telemetry.isOfficialTelemetry) {
      return this.send(REASON_IDLE_DAILY);
    }
  },

  testPing: function testPing() {
    return this.sendIdlePing(true);
  },

  /**
   * This observer drives telemetry.
   */
  observe: function (aSubject, aTopic, aData) {
    if (!this._log) {
      this._log = Log.repository.getLoggerWithMessagePrefix(LOGGER_NAME, LOGGER_PREFIX);
    }

    this._log.trace("observe - " + aTopic + " notified.");

    switch (aTopic) {
    case "profile-after-change":
      // profile-after-change is only registered for chrome processes.
      return this.setupChromeProcess();
    case "app-startup":
      // app-startup is only registered for content processes.
      return this.setupContentProcess();
    case "content-child-shutdown":
      // content-child-shutdown is only registered for content processes.
      Services.obs.removeObserver(this, "content-child-shutdown");
      this.uninstall();

      if (Telemetry.isOfficialTelemetry) {
        this.sendContentProcessPing(REASON_SAVED_SESSION);
      }
      break;
    case "cycle-collector-begin":
      let now = new Date();
      if (!gLastMemoryPoll
          || (TELEMETRY_INTERVAL <= now - gLastMemoryPoll)) {
        gLastMemoryPoll = now;
        this.gatherMemory();
      }
      break;
    case "xul-window-visible":
      Services.obs.removeObserver(this, "xul-window-visible");
      this._hasXulWindowVisibleObserver = false;
      var counters = processInfo.getCounters();
      if (counters) {
        [this._startupIO.startupWindowVisibleReadBytes,
          this._startupIO.startupWindowVisibleWriteBytes] = counters;
      }
      break;
    case "sessionstore-windows-restored":
      Services.obs.removeObserver(this, "sessionstore-windows-restored");
      this._hasWindowRestoredObserver = false;
      // Check whether debugger was attached during startup
      let debugService = Cc["@mozilla.org/xpcom/debug;1"].getService(Ci.nsIDebug2);
      gWasDebuggerAttached = debugService.isDebuggerAttached;
      this.gatherStartup();
      break;
    case REASON_IDLE_DAILY:
      // Enqueue to main-thread, otherwise components may be inited by the
      // idle-daily category and miss the gather-telemetry notification.
      Services.tm.mainThread.dispatch((function() {
        // Notify that data should be gathered now, since ping will happen soon.
        Services.obs.notifyObservers(null, "gather-telemetry", null);
        // The ping happens at the first idle of length IDLE_TIMEOUT_SECONDS.
        idleService.addIdleObserver(this, IDLE_TIMEOUT_SECONDS);
        this._isIdleObserver = true;
      }).bind(this), Ci.nsIThread.DISPATCH_NORMAL);
      break;
    case "idle":
      this.sendIdlePing(false, this._server);
      break;

#ifdef MOZ_WIDGET_ANDROID
    // On Android, we can get killed without warning once we are in the background,
    // but we may also submit data and/or come back into the foreground without getting
    // killed. To deal with this, we save the current session data to file when we are
    // put into the background. This handles the following post-backgrounding scenarios:
    // 1) We are killed immediately. In this case the current session data (which we
    //    save to a file) will be loaded and submitted on a future run.
    // 2) We submit the data while in the background, and then are killed. In this case
    //    the file that we saved will be deleted by the usual process in
    //    finishPingRequest after it is submitted.
    // 3) We submit the data, and then come back into the foreground. Same as case (2).
    // 4) We do not submit the data, but come back into the foreground. In this case
    //    we have the option of either deleting the file that we saved (since we will either
    //    send the live data while in the foreground, or create the file again on the next
    //    backgrounding), or not (in which case we will delete it on submit, or overwrite
    //    it on the next backgrounding). Not deleting it is faster, so that's what we do.
    case "application-background":
      if (Telemetry.isOfficialTelemetry) {
        let payload = this.getSessionPayload(REASON_SAVED_SESSION, false);
        let options = {
          retentionDays: RETENTION_DAYS,
          addClientId: true,
          addEnvironment: true,
          overwrite: true,
        };
        TelemetryPing.savePing(PING_TYPE_MAIN, payload, options);
      }
      break;
#endif
    }
  },

  /**
   * This tells TelemetrySession to uninitialize and save any pending pings.
   * @param testing Optional. If true, always saves the ping whether Telemetry
   *                can send pings or not, which is used for testing.
   */
  shutdownChromeProcess: function(testing = false) {
    this._log.trace("shutdownChromeProcess - testing: " + testing);
    TelemetryEnvironment.unregisterChangeListener(ENVIRONMENT_CHANGE_LISTENER);

    let cleanup = () => {
      TelemetryEnvironment.unregisterChangeListener(ENVIRONMENT_CHANGE_LISTENER);
      if (this._dailyTimerId) {
        Policy.clearDailyTimeout(this._dailyTimerId);
        this._dailyTimerId = null;
      }
      this.uninstall();

      let reset = () => {
        this._initStarted = false;
        this._initialized = false;
      };

      if (Telemetry.isOfficialTelemetry || testing) {
        return this.savePendingPings()
                .then(() => gStateSaveSerializer.flushTasks())
                .then(reset);
      }

      reset();
      return Promise.resolve();
    };

    // We can be in one the following states here:
    // 1) setupChromeProcess was never called
    // or it was called and
    //   2) _delayedInitTask was scheduled, but didn't run yet.
    //   3) _delayedInitTask is running now.
    //   4) _delayedInitTask finished running already.

    // This handles 1).
    if (!this._initStarted) {
      return Promise.resolve();
     }

    // This handles 4).
    if (!this._delayedInitTask) {
      // We already ran the delayed initialization.
      return cleanup();
     }

    // This handles 2) and 3).
    this._delayedInitTask.disarm();
    return this._delayedInitTask.finalize().then(cleanup);
   },

  _rescheduleDailyTimer: function() {
    if (this._dailyTimerId) {
      this._log.trace("_rescheduleDailyTimer - clearing existing timeout");
      Policy.clearDailyTimeout(this._dailyTimerId);
    }

    let now = Policy.now();
    let midnight = truncateToDays(now).getTime() + MS_IN_ONE_DAY;
    let msUntilCollection = midnight - now.getTime();
    if (msUntilCollection < MIN_SUBSESSION_LENGTH_MS) {
      msUntilCollection += MS_IN_ONE_DAY;
    }

    this._log.trace("_rescheduleDailyTimer - now: " + now
                    + ", scheduled: " + new Date(now.getTime() + msUntilCollection));
    this._dailyTimerId = Policy.setDailyTimeout(() => this._onDailyTimer(), msUntilCollection);
  },

  _onDailyTimer: function() {
    if (!this._initStarted) {
      if (this._log) {
        this._log.warn("_onDailyTimer - not initialized");
      } else {
        Cu.reportError("TelemetrySession._onDailyTimer - not initialized");
      }
      return;
    }

    this._log.trace("_onDailyTimer");
    let payload = this.getSessionPayload(REASON_DAILY, true);

    let options = {
      retentionDays: RETENTION_DAYS,
      addClientId: true,
      addEnvironment: true,
    };
    let promise = TelemetryPing.send(PING_TYPE_MAIN, payload, options);

    this._rescheduleDailyTimer();
    // Return the promise so tests can wait on the ping submission.
    return promise;
  },

  /**
   * Loads session data from the session data file.
   * @return {Promise<boolean>} A promise which is resolved with a true argument when
   *                            loading has completed, with false otherwise.
   */
  _loadSessionData: Task.async(function* () {
    let dataFile = OS.Path.join(OS.Constants.Path.profileDir, "datareporting",
                                SESSION_STATE_FILE_NAME);

    // Try to load the "profileSubsessionCounter" from the state file.
    try {
      let data = yield CommonUtils.readJSON(dataFile);
      if (data &&
          "profileSubsessionCounter" in data &&
          typeof(data.profileSubsessionCounter) == "number" &&
          "previousSubsessionId" in data) {
        this._previousSubsessionId = data.previousSubsessionId;
        // Add |_subsessionCounter| to the |_profileSubsessionCounter| to account for
        // new subsession while loading still takes place. This will always be exactly
        // 1 - the current subsessions.
        this._profileSubsessionCounter = data.profileSubsessionCounter +
                                         this._subsessionCounter;
        return true;
      }
    } catch (e) {
      this._log.info("_loadSessionData - Cannot load session data file " + dataFile, e);
    }
    return false;
  }),

  /**
   * Get the session data object to serialise to disk.
   */
  _getSessionDataObject: function() {
    return {
      previousSubsessionId: this._previousSubsessionId,
      profileSubsessionCounter: this._profileSubsessionCounter,
    };
  },

  /**
   * Saves session data to disk.
   */
  _saveSessionData: Task.async(function* (sessionData) {
    let dataDir = OS.Path.join(OS.Constants.Path.profileDir, "datareporting");
    yield OS.File.makeDir(dataDir);

    let filePath = OS.Path.join(dataDir, SESSION_STATE_FILE_NAME);
    try {
      yield CommonUtils.writeJSON(sessionData, filePath);
    } catch(e) {
      this._log.error("_saveSessionData - Failed to write session data to " + filePath, e);
    }
  }),

  _onEnvironmentChange: function() {
    this._log.trace("_onEnvironmentChange");
    let payload = this.getSessionPayload(REASON_ENVIRONMENT_CHANGE, true);

    let options = {
      retentionDays: RETENTION_DAYS,
      addClientId: true,
      addEnvironment: true,
    };
    let promise = TelemetryPing.send(PING_TYPE_MAIN, payload, options);
  },

  _isClassicReason: function(reason) {
    const classicReasons = [
      REASON_SAVED_SESSION,
      REASON_IDLE_DAILY,
      REASON_GATHER_PAYLOAD,
      REASON_TEST_PING,
    ];
    return classicReasons.indexOf(reason) != -1;
  },

  /**
   * Get an object describing the current state of this module for AsyncShutdown diagnostics.
   */
  _getState: function() {
    return {
      initialized: this._initialized,
      initStarted: this._initStarted,
      haveDelayedInitTask: !!this._delayedInitTask,
      dailyTimerScheduled: !!this._dailyTimerId,
    };
  },
};
