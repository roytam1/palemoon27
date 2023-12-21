/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var Ci = Components.interfaces;

add_task(function* () {
  const url = "data:text/html;base64," +
              btoa("<body><iframe srcdoc=\"content\"/></iframe>" +
                   "<a href=\"http://test.com\">test link</a>");
  let tab = yield BrowserTestUtils.openNewForegroundTab(gBrowser, url);

  let finder = tab.linkedBrowser.finder;
  let listener = {
    onFindResult: function () {
      ok(false, "callback wasn't replaced");
    }
  };
  finder.addResultListener(listener);

  function waitForFind() {
    return new Promise(resolve => {
      listener.onFindResult = resolve;
    })
  }

  let promiseFind = waitForFind();
  finder.highlight(true, "content");
  let findResult = yield promiseFind;
  is(findResult.result, Ci.nsITypeAheadFind.FIND_FOUND, "should find string");

  promiseFind = waitForFind();
  finder.highlight(true, "Bla");
  findResult = yield promiseFind;
  is(findResult.result, Ci.nsITypeAheadFind.FIND_NOTFOUND, "should not find string");

  // Search only for links and draw outlines.
  promiseFind = waitForFind();
  finder.fastFind("test link", true, true);
  findResult = yield promiseFind;
  is(findResult.result, Ci.nsITypeAheadFind.FIND_FOUND, "should find link");

  yield ContentTask.spawn(tab.linkedBrowser, {}, function* (arg) {
    ok(!!content.document.getElementsByTagName("a")[0].style.outline, "outline set");
  });

  // Just a simple search for "test link".
  promiseFind = waitForFind();
  finder.fastFind("test link", false, false);
  findResult = yield promiseFind;
  is(findResult.result, Ci.nsITypeAheadFind.FIND_FOUND, "should find link again");

  yield ContentTask.spawn(tab.linkedBrowser, {}, function* (arg) {
    ok(!content.document.getElementsByTagName("a")[0].style.outline, "outline not set");
  });

  finder.removeResultListener(listener);
  gBrowser.removeTab(tab);
});
