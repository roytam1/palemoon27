/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
"use strict";

function promisePopupShown(popup) {
  return new Promise(resolve => {
    if (popup.popupOpen) {
      resolve();
    } else {
      let onPopupShown = event => {
        popup.removeEventListener("popupshown", onPopupShown);
        resolve();
      };
      popup.addEventListener("popupshown", onPopupShown);
    }
  });
}

add_task(function* testPageActionPopup() {
  let scriptPage = url => `<html><head><meta charset="utf-8"><script src="${url}"></script></head></html>`;

  let extension = ExtensionTestUtils.loadExtension({
    manifest: {
      "background": {
        "page": "data/background.html",
      },
      "page_action": {
        "default_popup": "popup-a.html",
      },
    },

    files: {
      "popup-a.html": scriptPage("popup-a.js"),
      "popup-a.js": function() {
        browser.runtime.sendMessage("from-popup-a");
      },

      "data/popup-b.html": scriptPage("popup-b.js"),
      "data/popup-b.js": function() {
        browser.runtime.sendMessage("from-popup-b");
      },

      "data/background.html": scriptPage("background.js"),

      "data/background.js": function() {
        let tabId;

        let sendClick;
        let tests = [
          () => {
            sendClick({expectEvent: false, expectPopup: "a"});
          },
          () => {
            sendClick({expectEvent: false, expectPopup: "a"});
          },
          () => {
            browser.pageAction.setPopup({tabId, popup: "popup-b.html"});
            sendClick({expectEvent: false, expectPopup: "b"});
          },
          () => {
            sendClick({expectEvent: false, expectPopup: "b"});
          },
          () => {
            browser.pageAction.setPopup({tabId, popup: ""});
            sendClick({expectEvent: true, expectPopup: null});
          },
          () => {
            sendClick({expectEvent: true, expectPopup: null});
          },
          () => {
            browser.pageAction.setPopup({tabId, popup: "/popup-a.html"});
            sendClick({expectEvent: false, expectPopup: "a"});
          },
        ];

        let expect = {};
        sendClick = ({expectEvent, expectPopup}) => {
          expect = {event: expectEvent, popup: expectPopup};
          browser.test.sendMessage("send-click");
        };

        browser.runtime.onMessage.addListener(msg => {
          if (expect.popup) {
            browser.test.assertEq(msg, `from-popup-${expect.popup}`,
                                  "expected popup opened");
          } else {
            browser.test.fail("unexpected popup");
          }

          expect.popup = null;
          browser.test.sendMessage("next-test");
        });

        browser.pageAction.onClicked.addListener(() => {
          if (expect.event) {
            browser.test.succeed("expected click event received");
          } else {
            browser.test.fail("unexpected click event");
          }

          expect.event = false;
          browser.test.sendMessage("next-test");
        });

        browser.test.onMessage.addListener((msg) => {
          if (msg != "next-test") {
            browser.test.fail("Expecting 'next-test' message");
          }

          if (tests.length) {
            let test = tests.shift();
            test();
          } else {
            browser.test.notifyPass("pageaction-tests-done");
          }
        });

        browser.tabs.query({active: true, currentWindow: true}, tabs => {
          tabId = tabs[0].id;

          browser.pageAction.show(tabId);
          browser.test.sendMessage("next-test");
        });
      },
    },
  });

  let pageActionId = makeWidgetId(extension.id) + "-page-action";
  let panelId = makeWidgetId(extension.id) + "-panel";

  extension.onMessage("send-click", () => {
    clickPageAction(extension);
  });

  extension.onMessage("next-test", Task.async(function* () {
    let panel = document.getElementById(panelId);
    if (panel) {
      yield promisePopupShown(panel);
      panel.hidePopup();

      panel = document.getElementById(panelId);
      is(panel, null, "panel successfully removed from document after hiding");
    }

    extension.sendMessage("next-test");
  }));


  yield extension.startup();
  yield extension.awaitFinish("pageaction-tests-done");

  yield extension.unload();

  let node = document.getElementById(pageActionId);
  is(node, null, "pageAction image removed from document");

  let panel = document.getElementById(panelId);
  is(panel, null, "pageAction panel removed from document");
});


add_task(function* testPageActionSecurity() {
  const URL = "chrome://browser/content/browser.xul";

  let apis = ["browser_action", "page_action"];

  for (let api of apis) {
    info(`TEST ${api} icon url: ${URL}`);

    let messages = [/Access to restricted URI denied/];

    let waitForConsole = new Promise(resolve => {
      // Not necessary in browser-chrome tests, but monitorConsole gripes
      // if we don't call it.
      SimpleTest.waitForExplicitFinish();

      SimpleTest.monitorConsole(resolve, messages);
    });

    let extension = ExtensionTestUtils.loadExtension({
      manifest: {
        [api]: {"default_popup": URL},
      },
    });

    yield Assert.rejects(extension.startup(),
                         null,
                         "Manifest rejected");

    SimpleTest.endMonitorConsole();
    yield waitForConsole;
  }
});

add_task(forceGC);
