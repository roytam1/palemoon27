/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/
*/
/* A testcase to make sure reading the failed profile lock count works.  */

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;
const Cr = Components.results;

Cu.import("resource://gre/modules/Services.jsm", this);

const Telemetry = Cc["@mozilla.org/base/telemetry;1"].getService(Ci.nsITelemetry);

const LOCK_FILE_NAME = "Telemetry.FailedProfileLocks.txt";
const N_FAILED_LOCKS = 10;

// Constants from prio.h for nsIFileOutputStream.init
const PR_WRONLY = 0x2;
const PR_CREATE_FILE = 0x8;
const PR_TRUNCATE = 0x20;
const RW_OWNER = 0600;

function write_string_to_file(file, contents) {
  let ostream = Cc["@mozilla.org/network/safe-file-output-stream;1"]
                .createInstance(Ci.nsIFileOutputStream);
  ostream.init(file, PR_WRONLY | PR_CREATE_FILE | PR_TRUNCATE,
	       RW_OWNER, ostream.DEFER_OPEN);
  ostream.write(contents, contents.length);
  ostream.QueryInterface(Ci.nsISafeOutputStream).finish();
  ostream.close();
}

function construct_file() {
  let profileDirectory = Services.dirsvc.get("ProfD", Ci.nsIFile);
  let file = profileDirectory.clone();
  file.append(LOCK_FILE_NAME);
  return file;
}

function run_test() {
  do_get_profile();

  do_check_eq(Telemetry.failedProfileLockCount, 0);

  write_string_to_file(construct_file(), N_FAILED_LOCKS.toString());

  // Make sure that we're not eagerly reading the count now that the
  // file exists.
  do_check_eq(Telemetry.failedProfileLockCount, 0);

  do_test_pending();
  Telemetry.asyncFetchTelemetryData(actual_test);
}

function actual_test() {
  do_check_eq(Telemetry.failedProfileLockCount, N_FAILED_LOCKS);
  do_check_false(construct_file().exists());
  do_test_finished();
}
