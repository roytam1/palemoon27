/* vim:set ts=2 sw=2 sts=2 et: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Tests that network log messages bring up the network panel.

"use strict";

const TEST_NETWORK_REQUEST_URI =
  "http://example.com/browser/devtools/client/webconsole/test/" +
  "test-network-request.html";

add_task(function* () {
  let finishedRequest = waitForFinishedRequest();
  const hud = yield loadPageAndGetHud(TEST_NETWORK_REQUEST_URI);
  let request = yield finishedRequest;

  yield hud.ui.openNetworkPanel(request.actor);
  let toolbox = gDevTools.getToolbox(hud.target);
  is(toolbox.currentToolId, "netmonitor", "Network panel was opened");
  let panel = toolbox.getCurrentPanel();
  let selected = panel.panelWin.NetMonitorView.RequestsMenu.selectedItem;
  is(selected.attachment.method, request.request.method,
     "The correct request is selected");
  is(selected.attachment.url, request.request.url,
     "The correct request is definitely selected");
});
