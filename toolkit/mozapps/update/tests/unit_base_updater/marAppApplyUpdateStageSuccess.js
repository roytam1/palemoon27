/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

/**
 * Test applying an update by staging an update and launching an application to
 * apply it.
 */

const START_STATE = STATE_PENDING;
const END_STATE = STATE_APPLIED;

function run_test() {
  if (MOZ_APP_NAME == "xulrunner") {
    logTestInfo("Unable to run this test on xulrunner");
    return;
  }

  setupTestCommon();
  gTestFiles = gTestFilesCompleteSuccess;
  gTestDirs = gTestDirsCompleteSuccess;
  setupUpdaterTest(FILE_COMPLETE_MAR);

  createUpdaterINI(false);

  if (IS_WIN) {
    Services.prefs.setBoolPref(PREF_APP_UPDATE_SERVICE_ENABLED, false);
  }

  let channel = Services.prefs.getCharPref(PREF_APP_UPDATE_CHANNEL);
  let patches = getLocalPatchString(null, null, null, null, null, "true",
                                    START_STATE);
  let updates = getLocalUpdateString(patches, null, null, null, null, null,
                                     null, null, null, null, null, null,
                                     null, "true", channel);
  writeUpdatesToXMLFile(getLocalUpdatesXMLString(updates), true);
  writeVersionFile(getAppVersion());
  writeStatusFile(START_STATE);

  reloadUpdateManagerData();
  Assert.ok(!!gUpdateManager.activeUpdate,
            "the active update should be defined");

  setupAppFilesAsync();
}

function setupAppFilesFinished() {
  setAppBundleModTime();
  stageUpdate();
}

/**
 * Checks if the update has finished being staged.
 */
function checkUpdateApplied() {
  gTimeoutRuns++;
  // Don't proceed until the active update's state is the expected value.
  if (gUpdateManager.activeUpdate.state != END_STATE) {
    if (gTimeoutRuns > MAX_TIMEOUT_RUNS) {
      do_throw("Exceeded MAX_TIMEOUT_RUNS while waiting for the " +
               "active update status state to equal: " +
               END_STATE +
               ", current state: " + gUpdateManager.activeUpdate.state);
    }
    do_timeout(TEST_CHECK_TIMEOUT, checkUpdateApplied);
    return;
  }

  // Don't proceed until the update's status state is the expected value.
  let state = readStatusState();
  if (state != END_STATE) {
    if (gTimeoutRuns > MAX_TIMEOUT_RUNS) {
      do_throw("Exceeded MAX_TIMEOUT_RUNS while waiting for the update " +
               "status state to equal: " +
               END_STATE +
               ", current status state: " + state);
    }
    do_timeout(TEST_CHECK_TIMEOUT, checkUpdateApplied);
    return;
  }

  // Don't proceed until the last update log has been created.
  let log;
  if (IS_WIN || IS_MACOSX) {
    log = getUpdatesDir();
  } else {
    log = getStageDirFile(null, true);
    log.append(DIR_UPDATES);
  }
  log.append(FILE_LAST_LOG);
  if (!log.exists()) {
    if (gTimeoutRuns > MAX_TIMEOUT_RUNS) {
      do_throw("Exceeded MAX_TIMEOUT_RUNS while waiting for the update log " +
               "to be created. Path: " + log.path);
    } else {
      do_timeout(TEST_CHECK_TIMEOUT, checkUpdateApplied);
    }
    return;
  }

  if (IS_WIN || IS_MACOSX) {
    // Check that the post update process was not launched when staging an
    // update.
    do_check_false(getPostUpdateFile(".running").exists());
  }

  checkFilesAfterUpdateSuccess(getStageDirFile, true, false);

  log = getUpdatesPatchDir();
  log.append(FILE_UPDATE_LOG);
  logTestInfo("testing " + log.path + " shouldn't exist");
  do_check_false(log.exists());

  log = getUpdatesDir();
  log.append(FILE_LAST_LOG);
  if (IS_WIN || IS_MACOSX) {
    logTestInfo("testing " + log.path + " should exist");
    do_check_true(log.exists());
  } else {
    logTestInfo("testing " + log.path + " shouldn't exist");
    do_check_false(log.exists());
  }

  log = getUpdatesDir();
  log.append(FILE_BACKUP_LOG);
  logTestInfo("testing " + log.path + " shouldn't exist");
  do_check_false(log.exists());

  let updatesDir = getStageDirFile(DIR_UPDATES + "/0", true);
  logTestInfo("testing " + updatesDir.path + " shouldn't exist");
  do_check_false(updatesDir.exists());

  log = getStageDirFile(DIR_UPDATES + "/0/" + FILE_UPDATE_LOG, true);
  logTestInfo("testing " + log.path + " shouldn't exist");
  do_check_false(log.exists());

  log = getStageDirFile(DIR_UPDATES + "/" + FILE_LAST_LOG, true);
  if (IS_WIN || IS_MACOSX) {
    logTestInfo("testing " + log.path + " shouldn't exist");
    do_check_false(log.exists());
  } else {
    logTestInfo("testing " + log.path + " should exist");
    do_check_true(log.exists());
  }

  log = getStageDirFile(DIR_UPDATES + "/" + FILE_BACKUP_LOG, true);
  logTestInfo("testing " + log.path + " shouldn't exist");
  do_check_false(log.exists());

  // Switch the application to the staged application that was updated by
  // launching the application.
  do_timeout(TEST_CHECK_TIMEOUT, launchAppToApplyUpdate);
}

/**
 * Checks if the post update binary was properly launched for the platforms that
 * support launching post update process.
 */
function checkUpdateFinished() {
  if (IS_WIN || IS_MACOSX) {
    gCheckFunc = finishCheckUpdateApplied;
    checkPostUpdateAppLog();
  } else {
    finishCheckUpdateApplied();
  }
}

/**
 * Checks if the update has finished and if it has finished performs checks for
 * the test.
 */
function finishCheckUpdateApplied() {
  gTimeoutRuns++;
  // Don't proceed until the update's status state is the expected value.
  let state = readStatusState();
  if (state != STATE_SUCCEEDED) {
    if (gTimeoutRuns > MAX_TIMEOUT_RUNS) {
      do_throw("Exceeded MAX_TIMEOUT_RUNS while waiting for the " +
               "update status file state to equal: " +
               STATE_SUCCEEDED +
               ", current status state: " + state);
    }
    do_timeout(TEST_CHECK_TIMEOUT, checkUpdateFinished);
    return;
  }

  // Don't proceed until the application was switched out with the staged update
  // successfully.
  let updatedDir = getStageDirFile(null, true);
  if (updatedDir.exists()) {
    if (gTimeoutRuns > MAX_TIMEOUT_RUNS) {
      do_throw("Exceeded MAX_TIMEOUT_RUNS while waiting for the updated " +
               "directory to not exist. Path: " + updatedDir.path);
    }
    do_timeout(TEST_CHECK_TIMEOUT, checkUpdateFinished);
    return;
  }

  if (IS_WIN) {
    // Don't proceed until the updater binary is no longer in use.
    let updater = getUpdatesPatchDir();
    updater.append(FILE_UPDATER_BIN);
    if (updater.exists()) {
      if (gTimeoutRuns > MAX_TIMEOUT_RUNS) {
        do_throw("Exceeded while waiting for updater binary to no longer be " +
                 "in use");
      } else {
        try {
          updater.remove(false);
        } catch (e) {
          do_timeout(TEST_CHECK_TIMEOUT, checkUpdateFinished);
          return;
        }
      }
    }
  }

  if (IS_MACOSX) {
    logTestInfo("testing last modified time on the apply to directory has " +
                "changed after a successful update (bug 600098)");
    let now = Date.now();
    let applyToDir = getApplyDirFile();
    let timeDiff = Math.abs(applyToDir.lastModifiedTime - now);
    do_check_true(timeDiff < MAC_MAX_TIME_DIFFERENCE);
  }

  checkFilesAfterUpdateSuccess(getApplyDirFile, false, true);
  gSwitchApp = true;
  checkUpdateLogContents();
  gSwitchApp = false;
  checkCallbackAppLog();

  standardInit();

  let update = gUpdateManager.getUpdateAt(0);
  do_check_eq(update.state, STATE_SUCCEEDED);

  let updatesDir = getUpdatesPatchDir();
  logTestInfo("testing " + updatesDir.path + " should exist");
  do_check_true(updatesDir.exists());

  let log = getUpdatesPatchDir();
  log.append(FILE_UPDATE_LOG);
  logTestInfo("testing " + log.path + " shouldn't exist");
  do_check_false(log.exists());

  log = getUpdatesDir();
  log.append(FILE_LAST_LOG);
  logTestInfo("testing " + log.path + " should exist");
  do_check_true(log.exists());

  log = getUpdatesDir();
  log.append(FILE_BACKUP_LOG);
  if (IS_WIN || IS_MACOSX) {
    logTestInfo("testing " + log.path + " should exist");
    do_check_true(log.exists());
  } else {
    logTestInfo("testing " + log.path + " shouldn't exist");
    do_check_false(log.exists());
  }

  waitForFilesInUse();
}
