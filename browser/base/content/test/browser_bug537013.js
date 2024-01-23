/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

/* Tests for bug 537013 to ensure proper tab-sequestration of find bar. */

let tabs = [];
let texts = [
  "This side up.",
  "The world is coming to an end. Please log off.",
  "Klein bottle for sale. Inquire within.",
  "To err is human; to forgive is not company policy."
];

function addTabWithText(aText, aCallback) {
  let newTab = gBrowser.addTab("data:text/html,<h1 id='h1'>" + aText + "</h1>");
  tabs.push(newTab);
  gBrowser.selectedTab = newTab;
}

function setFindString(aString) {
  gFindBar.open();
  gFindBar._findField.select();
  EventUtils.sendString(aString);
  is(gFindBar._findField.value, aString, "Set the field correctly!");
}

let newWindow;

function test() {
  waitForExplicitFinish();
  registerCleanupFunction(function () {
    while (tabs.length) {
      gBrowser.removeTab(tabs.pop());
    }
  });
  texts.forEach(function(aText) addTabWithText(aText));

  // Set up the first tab
  gBrowser.selectedTab = tabs[0];

  setFindString(texts[0]);

  // Make sure the second tab is correct, then set it up
  gBrowser.selectedTab = tabs[1];
  ok(gFindBar.hidden, "Second tab doesn't show find bar!");
  gFindBar.open();
  is(gFindBar._findField.value, texts[0],
     "Second tab kept old find value for new initialization!");
  setFindString(texts[1]);

  // Confirm the first tab is still correct, ensure re-hiding works as expected
  gBrowser.selectedTab = tabs[0];
  ok(!gFindBar.hidden, "First tab shows find bar!");
  is(gFindBar._findField.value, texts[0], "First tab persists find value!");
  gFindBar.close();
  ok(gFindBar.hidden, "First tab doesn't show find bar!");
  gBrowser.selectedTab = tabs[1];
  ok(!gFindBar.hidden, "Second tab shows find bar!");
  gBrowser.selectedTab = tabs[0];
  ok(gFindBar.hidden, "First tab doesn't show find bar!");

  // Set up a third tab, no tests here
  gBrowser.selectedTab = tabs[2];
  setFindString(texts[2]);

  // Now we jump to the second, then first, and then fourth
  gBrowser.selectedTab = tabs[1];
  gBrowser.selectedTab = tabs[0];
  gBrowser.selectedTab = tabs[3];
  ok(gFindBar.hidden, "Fourth tab doesn't show find bar!");
  is(gFindBar, gBrowser.getFindBar(), "Find bar is right one!");
  gFindBar.open();
  is(gFindBar._findField.value, texts[1],
     "Fourth tab has second tab's find value!");

  newWindow = gBrowser.replaceTabWithWindow(tabs.pop());
  whenDelayedStartupFinished(newWindow, checkNewWindow);
}

// Test that findbar gets restored when a tab is moved to a new window.
function checkNewWindow() {
  ok(!newWindow.gFindBar.hidden, "New window shows find bar!");
  is(newWindow.gFindBar._findField.value, texts[1],
     "New window find bar has correct find value!");
  ok(!newWindow.gFindBar.getElement("find-next").disabled,
     "New window findbar has enabled buttons!");
  newWindow.close();
  finish();
}
