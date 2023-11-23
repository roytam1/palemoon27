/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/
 */

function test() {
  waitForExplicitFinish();
  gBrowser.selectedTab = gBrowser.addTab("data:text/html,<iframe width='700' height='700' src='about:certerror'></iframe>");
  // Open a html page with about:certerror in an iframe
  BrowserTestUtils.browserLoaded(gBrowser.selectedBrowser).then(testIframeCert);
}

function testIframeCert(e) {
  // Confirm that the expert section is hidden
  var doc = gBrowser.contentDocument.getElementsByTagName('iframe')[0].contentDocument;
  var aP = doc.getElementById("advancedPanel");
  ok(aP, "Advanced content should exist");
  is_element_hidden(aP, "Advanced content should not be visible by default")

  // Clean up
  gBrowser.removeCurrentTab();

  finish();
}
