/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

var Cu = Components.utils;
var Cc = Components.classes;
var Ci = Components.interfaces;
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/TelemetryController.jsm", this);
Cu.import("resource://gre/modules/TelemetrySession.jsm", this);
Cu.import('resource://gre/modules/XPCOMUtils.jsm');

// The @mozilla/xre/app-info;1 XPCOM object provided by the xpcshell test harness doesn't
// implement the nsIXULAppInfo interface, which is needed by Services.jsm and
// TelemetrySession.jsm. updateAppInfo() creates and registers a minimal mock app-info.
Cu.import("resource://testing-common/AppInfo.jsm");
updateAppInfo();

var gGlobalScope = this;
function loadAddonManager() {
  let ns = {};
  Cu.import("resource://gre/modules/Services.jsm", ns);
  let head = "../../../../mozapps/extensions/test/xpcshell/head_addons.js";
  let file = do_get_file(head);
  let uri = ns.Services.io.newFileURI(file);
  ns.Services.scriptloader.loadSubScript(uri.spec, gGlobalScope);
  createAppInfo("xpcshell@tests.mozilla.org", "XPCShell", "1", "1.9.2");
  startupManager();
}

function getSimpleMeasurementsFromTelemetryPing() {
  return TelemetrySession.getPayload().simpleMeasurements;
}

function initialiseTelemetry() {
  return TelemetryController.setup().then(TelemetrySession.setup);
}

function run_test() {
  // Telemetry needs the AddonManager.
  loadAddonManager();
  // Make profile available for |TelemetrySession.shutdown()|.
  do_get_profile();

  do_test_pending();
  const Telemetry = Services.telemetry;
  Telemetry.asyncFetchTelemetryData(run_next_test);
}

add_task(function* actualTest() {
  yield initialiseTelemetry();

  // Test the module logic
  let tmp = {};
  Cu.import("resource://gre/modules/TelemetryTimestamps.jsm", tmp);
  let TelemetryTimestamps = tmp.TelemetryTimestamps;
  let now = Date.now();
  TelemetryTimestamps.add("foo");
  do_check_true(TelemetryTimestamps.get().foo != null); // foo was added
  do_check_true(TelemetryTimestamps.get().foo >= now); // foo has a reasonable value

  // Add timestamp with value
  // Use a value far in the future since TelemetryPing substracts the time of
  // process initialization.
  const YEAR_4000_IN_MS = 64060588800000;
  TelemetryTimestamps.add("bar", YEAR_4000_IN_MS);
  do_check_eq(TelemetryTimestamps.get().bar, YEAR_4000_IN_MS); // bar has the right value

  // Can't add the same timestamp twice
  TelemetryTimestamps.add("bar", 2);
  do_check_eq(TelemetryTimestamps.get().bar, YEAR_4000_IN_MS); // bar wasn't overwritten

  let threw = false;
  try {
    TelemetryTimestamps.add("baz", "this isn't a number");
  } catch (ex) {
    threw = true;
  }
  do_check_true(threw); // adding non-number threw
  do_check_null(TelemetryTimestamps.get().baz); // no baz was added

  // Test that the data gets added to the telemetry ping properly
  let simpleMeasurements = getSimpleMeasurementsFromTelemetryPing();
  do_check_true(simpleMeasurements != null); // got simple measurements from ping data
  do_check_true(simpleMeasurements.foo > 1); // foo was included
  do_check_true(simpleMeasurements.bar > 1); // bar was included
  do_check_eq(undefined, simpleMeasurements.baz); // baz wasn't included since it wasn't added

  yield TelemetrySession.shutdown(false);

  do_test_finished();
});
