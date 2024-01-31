/* -*- indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et: */

const { classes: Cc, interfaces: Ci, utils: Cu, results: Cr } = Components;

Cu.import("resource://gre/modules/FileUtils.jsm");
Cu.import("resource://gre/modules/osfile.jsm");
Cu.import("resource://gre/modules/NetUtil.jsm");
Cu.import("resource://gre/modules/Promise.jsm");
Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/Task.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");
Cu.import("resource://testing-common/AppInfo.jsm");
Cu.import("resource://testing-common/httpd.js");

const BROWSER_SEARCH_PREF = "browser.search.";
const NS_APP_SEARCH_DIR = "SrchPlugns";

const MODE_RDONLY = FileUtils.MODE_RDONLY;
const MODE_WRONLY = FileUtils.MODE_WRONLY;
const MODE_CREATE = FileUtils.MODE_CREATE;
const MODE_TRUNCATE = FileUtils.MODE_TRUNCATE;

// nsSearchService.js uses Services.appinfo.name to build a salt for a hash.
var XULRuntime = Components.classesByID["{95d89e3e-a169-41a3-8e56-719978e15b12}"]
                           .getService(Ci.nsIXULRuntime);

var isChild = XULRuntime.processType == XULRuntime.PROCESS_TYPE_CONTENT;

updateAppInfo({
  name: "XPCShell",
  ID: "xpcshell@test.mozilla.org",
  version: "5",
  platformVersion: "1.9",
  // mirror OS from the base impl as some of the "location" tests rely on it
  OS: XULRuntime.OS,
  // mirror processType from the base implementation
  extraProps: {
    processType: XULRuntime.processType,
  },
});

var gProfD;
if (!isChild) {
  // Need to create and register a profile folder.
  gProfD = do_get_profile();
}

function dumpn(text)
{
  dump("search test: " + text + "\n");
}

/**
 * Configure preferences to load engines from
 * chrome://testsearchplugin/locale/searchplugins/
 * unless the loadFromJars parameter is set to false.
 */
function configureToLoadJarEngines(loadFromJars = true)
{
  let defaultBranch = Services.prefs.getDefaultBranch(null);

  let url = "chrome://testsearchplugin/locale/searchplugins/";
  defaultBranch.setCharPref("browser.search.jarURIs", url);

  defaultBranch.setBoolPref("browser.search.loadFromJars", loadFromJars);

  // Give the pref a user set value that is the opposite of the default,
  // to ensure user set values are ignored.
  Services.prefs.setBoolPref("browser.search.loadFromJars", !loadFromJars)

  // Ensure a test engine exists in the app dir anyway.
  let dir = Services.dirsvc.get(NS_APP_SEARCH_DIR, Ci.nsIFile);
  if (!dir.exists())
    dir.create(dir.DIRECTORY_TYPE, FileUtils.PERMS_DIRECTORY);
  do_get_file("data/engine-app.xml").copyTo(dir, "app.xml");
}

/**
 * Clean the profile of any metadata files left from a previous run.
 */
function removeMetadata()
{
  let file = gProfD.clone();
  file.append("search-metadata.json");
  if (file.exists()) {
    file.remove(false);
  }

  file = gProfD.clone();
  file.append("search.sqlite");
  if (file.exists()) {
    file.remove(false);
  }
}

function getSearchMetadata()
{
  // Check that search-metadata.json has been created
  let metadata = gProfD.clone();
  metadata.append("search-metadata.json");
  do_check_true(metadata.exists());

  do_print("Parsing metadata");
  return readJSONFile(metadata);
}

function removeCacheFile()
{
  let file = gProfD.clone();
  file.append("search.json");
  if (file.exists()) {
    file.remove(false);
  }
}

/**
 * Clean the profile of any cache file left from a previous run.
 */
function removeCache()
{
  let file = gProfD.clone();
  file.append("search.json");
  if (file.exists()) {
    file.remove(false);
  }

}

/**
 * isUSTimezone taken from nsSearchService.js
 */
function isUSTimezone() {
  // Timezone assumptions! We assume that if the system clock's timezone is
  // between Newfoundland and Hawaii, that the user is in North America.

  // This includes all of South America as well, but we have relatively few
  // en-US users there, so that's OK.

  // 150 minutes = 2.5 hours (UTC-2.5), which is
  // Newfoundland Daylight Time (http://www.timeanddate.com/time/zones/ndt)

  // 600 minutes = 10 hours (UTC-10), which is
  // Hawaii-Aleutian Standard Time (http://www.timeanddate.com/time/zones/hast)

  let UTCOffset = (new Date()).getTimezoneOffset();
  return UTCOffset >= 150 && UTCOffset <= 600;
}

/**
 * Run some callback once metadata has been committed to disk.
 */
function afterCommit(callback)
{
  let obs = function(result, topic, verb) {
    if (verb == "write-metadata-to-disk-complete") {
      Services.obs.removeObserver(obs, topic);
      callback(result);
    } else {
      dump("TOPIC: " + topic+ "\n");
    }
  }
  Services.obs.addObserver(obs, "browser-search-service", false);
}

/**
 * Run some callback once cache has been built.
 */
function afterCache(callback)
{
  let obs = function(result, topic, verb) {
    do_print("afterCache: " + verb);
    if (verb == "write-cache-to-disk-complete") {
      Services.obs.removeObserver(obs, topic);
      callback(result);
    } else {
      dump("TOPIC: " + topic+ "\n");
    }
  }
  Services.obs.addObserver(obs, "browser-search-service", false);
}

function parseJsonFromStream(aInputStream) {
  const json = Cc["@mozilla.org/dom/json;1"].createInstance(Components.interfaces.nsIJSON);
  const data = json.decodeFromStream(aInputStream, aInputStream.available());
  return data;
}

/**
 * Read a JSON file and return the JS object
 */
function readJSONFile(aFile) {
  let stream = Cc["@mozilla.org/network/file-input-stream;1"].
               createInstance(Ci.nsIFileInputStream);
  try {
    stream.init(aFile, MODE_RDONLY, FileUtils.PERMS_FILE, 0);
    return parseJsonFromStream(stream, stream.available());
  } catch(ex) {
    dumpn("readJSONFile: Error reading JSON file: " + ex);
  } finally {
    stream.close();
  }
  return false;
}

/**
 * Recursively compare two objects and check that every property of expectedObj has the same value
 * on actualObj.
 */
function isSubObjectOf(expectedObj, actualObj) {
  for (let prop in expectedObj) {
    if (expectedObj[prop] instanceof Object) {
      do_check_eq(expectedObj[prop].length, actualObj[prop].length);
      isSubObjectOf(expectedObj[prop], actualObj[prop]);
    } else {
      do_check_eq(expectedObj[prop], actualObj[prop]);
    }
  }
}

// Can't set prefs if we're running in a child process, but the search  service
// doesn't run in child processes anyways.
if (!isChild) {
  // Expand the amount of information available in error logs
  Services.prefs.setBoolPref("browser.search.log", true);

  // The geo-specific search tests assume certain prefs are already setup, which
  // might not be true when run in comm-central etc.  So create them here.
  Services.prefs.setBoolPref("browser.search.geoSpecificDefaults", true);
  Services.prefs.setIntPref("browser.search.geoip.timeout", 2000);
  // But still disable geoip lookups - tests that need it will re-configure this.
  Services.prefs.setCharPref("browser.search.geoip.url", "");
}

/**
 * After useHttpServer() is called, this string contains the URL of the "data"
 * directory, including the final slash.
 */
let gDataUrl;

/**
 * Initializes the HTTP server and ensures that it is terminated when tests end.
 *
 * @return The HttpServer object in case further customization is needed.
 */
function useHttpServer() {
  let httpServer = new HttpServer();
  httpServer.start(-1);
  httpServer.registerDirectory("/", do_get_cwd());
  gDataUrl = "http://localhost:" + httpServer.identity.primaryPort + "/data/";
  do_register_cleanup(() => httpServer.stop(() => {}));
  return httpServer;
}

/**
 * Adds test engines and returns a promise resolved when they are installed.
 *
 * The engines are added in the given order.
 *
 * @param aItems
 *        Array of objects with the following properties:
 *        {
 *          name: Engine name, used to wait for it to be loaded.
 *          xmlFileName: Name of the XML file in the "data" folder.
 *          details: Array containing the parameters of addEngineWithDetails,
 *                   except for the engine name.  Alternative to xmlFileName.
 *        }
 */
let addTestEngines = Task.async(function* (aItems) {
  if (!gDataUrl) {
    do_throw("useHttpServer must be called before addTestEngines.");
  }

  let engines = [];

  for (let item of aItems) {
    do_print("Adding engine: " + item.name);
    yield new Promise((resolve, reject) => {
      Services.obs.addObserver(function obs(subject, topic, data) {
        try {
          let engine = subject.QueryInterface(Ci.nsISearchEngine);
          do_print("Observed " + data + " for " + engine.name);
          if (data != "engine-added" || engine.name != item.name) {
            return;
          }

          Services.obs.removeObserver(obs, "browser-search-engine-modified");
          engines.push(engine);
          resolve();
        } catch (ex) {
          reject(ex);
        }
      }, "browser-search-engine-modified", false);

      if (item.xmlFileName) {
        Services.search.addEngine(gDataUrl + item.xmlFileName,
                                  Ci.nsISearchEngine.DATA_XML, null, false);
      } else {
        Services.search.addEngineWithDetails(item.name, ...item.details);
      }
    });
  }

  return engines;
});

/**
 * Installs a test engine into the test profile.
 */
function installTestEngine() {
  removeMetadata();
  removeCacheFile();

  do_check_false(Services.search.isInitialized);

  let engineDummyFile = gProfD.clone();
  engineDummyFile.append("searchplugins");
  engineDummyFile.append("test-search-engine.xml");
  let engineDir = engineDummyFile.parent;
  engineDir.create(Ci.nsIFile.DIRECTORY_TYPE, FileUtils.PERMS_DIRECTORY);

  do_get_file("data/engine.xml").copyTo(engineDir, "engine.xml");

  do_register_cleanup(function() {
    removeMetadata();
    removeCacheFile();
  });
}

/**
 * Wrapper for nsIPrefBranch::setComplexValue.
 * @param aPrefName
 *        The name of the pref to set.
 */
function setLocalizedPref(aPrefName, aValue) {
  const nsIPLS = Ci.nsIPrefLocalizedString;
  try {
    var pls = Components.classes["@mozilla.org/pref-localizedstring;1"]
                        .createInstance(Ci.nsIPrefLocalizedString);
    pls.data = aValue;
    Services.prefs.setComplexValue(aPrefName, nsIPLS, pls);
  } catch (ex) {}
}


/**
 * Installs two test engines, sets them as default for US vs. general.
 */
function setUpGeoDefaults() {
  removeMetadata();
  removeCacheFile();

  do_check_false(Services.search.isInitialized);

  let engineDummyFile = gProfD.clone();
  engineDummyFile.append("searchplugins");
  engineDummyFile.append("test-search-engine.xml");
  let engineDir = engineDummyFile.parent;
  engineDir.create(Ci.nsIFile.DIRECTORY_TYPE, FileUtils.PERMS_DIRECTORY);

  do_get_file("data/engine.xml").copyTo(engineDir, "engine.xml");

  engineDummyFile = gProfD.clone();
  engineDummyFile.append("searchplugins");
  engineDummyFile.append("test-search-engine2.xml");

  do_get_file("data/engine2.xml").copyTo(engineDir, "engine2.xml");

  setLocalizedPref("browser.search.defaultenginename",    "Test search engine");
  setLocalizedPref("browser.search.defaultenginename.US", "A second test engine");

  do_register_cleanup(function() {
    removeMetadata();
    removeCacheFile();
  });
}

/**
 * Returns a promise that is resolved when an observer notification from the
 * search service fires with the specified data.
 *
 * @param aExpectedData
 *        The value the observer notification sends that causes us to resolve
 *        the promise.
 */
function waitForSearchNotification(aExpectedData) {
  return new Promise(resolve => {
    const SEARCH_SERVICE_TOPIC = "browser-search-service";
    Services.obs.addObserver(function observer(aSubject, aTopic, aData) {
      if (aData != aExpectedData)
        return;

      Services.obs.removeObserver(observer, SEARCH_SERVICE_TOPIC);
      resolve(aSubject);
    }, SEARCH_SERVICE_TOPIC, false);
  });
}

// This "enum" from nsSearchService.js
const TELEMETRY_RESULT_ENUM = {
  SUCCESS: 0,
  SUCCESS_WITHOUT_DATA: 1,
  XHRTIMEOUT: 2,
  ERROR: 3,
};

/**
 * Checks the value of the SEARCH_SERVICE_COUNTRY_FETCH_RESULT probe.
 *
 * @param aExpectedValue
 *        If a value from TELEMETRY_RESULT_ENUM, we expect to see this value
 *        recorded exactly once in the probe.  If |null|, we expect to see
 *        nothing recorded in the probe at all.
 */
function checkCountryResultTelemetry(aExpectedValue) {
  let histogram = Services.telemetry.getHistogramById("SEARCH_SERVICE_COUNTRY_FETCH_RESULT");
  let snapshot = histogram.snapshot();
  // The probe is declared with 8 values, but we get 9 back from .counts
  let expectedCounts = [0,0,0,0,0,0,0,0,0];
  if (aExpectedValue != null) {
    expectedCounts[aExpectedValue] = 1;
  }
  deepEqual(snapshot.counts, expectedCounts);
}
