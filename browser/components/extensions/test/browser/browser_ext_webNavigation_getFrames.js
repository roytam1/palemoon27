/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
"use strict";

add_task(function* testWebNavigationGetNonExistentTab() {
  let extension = ExtensionTestUtils.loadExtension({
    background: "(" + function() {
      let results = [
        // There is no "tabId = 0" because the id assigned by TabManager (defined in ext-utils.js)
        // starts from 1.
        browser.webNavigation.getAllFrames({tabId: 0}).then(() => {
          browser.test.fail("getAllFrames Promise should be rejected on error");
        }, (error) => {
          browser.test.assertEq("No tab found with tabId: 0", error.message,
                                "getAllFrames rejected Promise should pass the expected error");
        }),
        // There is no "tabId = 0" because the id assigned by TabManager (defined in ext-utils.js)
        // starts from 1, processId is currently marked as optional and it is ignored.
        browser.webNavigation.getFrame({tabId: 0, frameId: 15, processId: 20}).then(() => {
          browser.test.fail("getFrame Promise should be rejected on error");
        }, (error) => {
          browser.test.assertEq("No tab found with tabId: 0", error.message,
                                "getFrame rejected Promise should pass the expected error");
        }),
      ];

      Promise.all(results).then(() => {
        browser.test.sendMessage("getNonExistentTab.done");
      });
    } + ")();",
    manifest: {
      permissions: ["webNavigation"],
    },
  });
  info("load complete");

  yield extension.startup();
  info("startup complete");

  yield extension.awaitMessage("getNonExistentTab.done");

  yield extension.unload();
  info("extension unloaded");
});

add_task(function* testWebNavigationFrames() {
  let extension = ExtensionTestUtils.loadExtension({
    background: "(" + function() {
      let tabId;
      let collectedDetails = [];

      browser.webNavigation.onCompleted.addListener((details) => {
        collectedDetails.push(details);

        if (details.frameId !== 0) {
          // wait for the top level iframe to be complete
          return;
        }

        browser.webNavigation.getAllFrames({tabId}).then((getAllFramesDetails) => {
          let getFramePromises = getAllFramesDetails.map((frameDetail) => {
            let {frameId} = frameDetail;
            // processId is currently marked as optional and it is ignored.
            return browser.webNavigation.getFrame({tabId, frameId, processId: 0});
          });

          Promise.all(getFramePromises).then((getFrameResults) => {
            browser.test.sendMessage("webNavigationFrames.done", {
              collectedDetails, getAllFramesDetails, getFrameResults,
            });
          }, () => {
            browser.test.assertTrue(false, "None of the getFrame promises should have been rejected");
          });

          // Pick a random frameId.
          let nonExistentFrameId = Math.floor(Math.random() * 10000);

          // Increment the picked random nonExistentFrameId until it doesn't exists.
          while (getAllFramesDetails.filter((details) => details.frameId == nonExistentFrameId).length > 0) {
            nonExistentFrameId += 1;
          }

          // Check that getFrame Promise is rejected with the expected error message on nonexistent frameId.
          browser.webNavigation.getFrame({tabId, frameId: nonExistentFrameId, processId: 20}).then(() => {
            browser.test.fail("getFrame promise should be rejected for an unexistent frameId");
          }, (error) => {
            browser.test.assertEq(`No frame found with frameId: ${nonExistentFrameId}`, error.message,
                                  "getFrame promise should be rejected with the expected error message on unexistent frameId");
          }).then(() => {
            browser.tabs.remove(tabId);
            browser.test.sendMessage("webNavigationFrames.done");
          });
        });
      });

      browser.tabs.create({url: "tab.html"}, (tab) => {
        tabId = tab.id;
      });
    } + ")();",
    manifest: {
      permissions: ["webNavigation", "tabs"],
    },
    files: {
      "tab.html": `
        <!DOCTYPE html>
        <html>
          <head>
            <meta charset="utf-8">
          </head>
          <body>
            <iframe src="subframe.html"></iframe>
            <iframe src="subframe.html"></iframe>
          </body>
        </html>
      `,
      "subframe.html": `
        <!DOCTYPE html>
        <html>
          <head>
            <meta charset="utf-8">
          </head>
        </html>
      `,
    },
  });
  info("load complete");

  yield extension.startup();
  info("startup complete");

  let {
    collectedDetails,
    getAllFramesDetails,
    getFrameResults,
  } = yield extension.awaitMessage("webNavigationFrames.done");

  is(getAllFramesDetails.length, 3, "expected number of frames found");
  is(getAllFramesDetails.length, collectedDetails.length,
     "number of frames found should equal the number onCompleted events collected");

  // ordered by frameId
  let sortByFrameId = (el) => el ? el.frameId : -1;

  collectedDetails = collectedDetails.sort(sortByFrameId);
  getAllFramesDetails = getAllFramesDetails.sort(sortByFrameId);
  getFrameResults = getFrameResults.sort(sortByFrameId);

  info("check frame details content");

  is(getFrameResults.length, getAllFramesDetails.length,
     "getFrame and getAllFrames should return the same number of results");

  Assert.deepEqual(getFrameResults, getAllFramesDetails,
                   "getFrame and getAllFrames should return the same results");

  info(`check frame details collected and retrieved with getAllFrames`);

  for (let [i, collected] of collectedDetails.entries()) {
    let getAllFramesDetail = getAllFramesDetails[i];

    is(getAllFramesDetail.frameId, collected.frameId, "frameId");
    is(getAllFramesDetail.parentFrameId, collected.parentFrameId, "parentFrameId");
    is(getAllFramesDetail.tabId, collected.tabId, "tabId");

    // This can be uncommented once Bug 1246125 has been fixed
    // is(getAllFramesDetail.url, collected.url, "url");
  }

  info("frame details content checked");

  yield extension.awaitMessage("webNavigationFrames.done");

  yield extension.unload();
  info("extension unloaded");
});
