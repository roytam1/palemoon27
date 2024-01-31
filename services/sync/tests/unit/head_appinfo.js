/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

const {classes: Cc, interfaces: Ci, results: Cr, utils: Cu} = Components;

var gSyncProfile;

gSyncProfile = do_get_profile();

// Init FormHistoryStartup and pretend we opened a profile.
var fhs = Cc["@mozilla.org/satchel/form-history-startup;1"]
            .getService(Ci.nsIObserver);
fhs.observe(null, "profile-after-change", null);


Cu.import("resource://gre/modules/XPCOMUtils.jsm");

// Make sure to provide the right OS so crypto loads the right binaries
var OS = "XPCShell";
if ("@mozilla.org/windows-registry-key;1" in Cc)
  OS = "WINNT";
else if ("nsILocalFileMac" in Ci)
  OS = "Darwin";
else
  OS = "Linux";

Cu.import("resource://testing-common/AppInfo.jsm", this);
updateAppInfo({
  name: "XPCShell",
  ID: "xpcshell@tests.mozilla.org",
  version: "1",
  platformVersion: "",
  OS: OS,
});

// Register resource aliases. Normally done in SyncComponents.manifest.
function addResourceAlias() {
  Cu.import("resource://gre/modules/Services.jsm");
  const resProt = Services.io.getProtocolHandler("resource")
                          .QueryInterface(Ci.nsIResProtocolHandler);
  for (let s in ["common", "sync", "crypto"]) {
    let uri = Services.io.newURI("resource://gre/modules/services-" + s + "/", null,
                                 null);
    resProt.setSubstitution("services-" + s, uri);
  }
}
addResourceAlias();
