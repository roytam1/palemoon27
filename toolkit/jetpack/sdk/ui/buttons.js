/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 * PMkit shim for 'sdk/ui/button', (c) JustOff, 2017 */
"use strict";

module.metadata = {
  "stability": "experimental",
  "engines": {
    "Firefox": "> 27"
  }
};

const { Ci, Cc } = require('chrome');
const prefs = require('../preferences/service');
const blist = new Map();

const INSERTION_PREF_ROOT = "extensions.sdk-button-inserted.";

let gWindowListener;

function saveInserted(id) {
  prefs.set(INSERTION_PREF_ROOT + id, true);
}

function haveInserted(id) {
  return prefs.has(INSERTION_PREF_ROOT + id);
}

function insertButton(aWindow, id, onBuild) {
  // Build button and save reference to it
  let doc = aWindow.document;
  let b = onBuild(doc, id);
  aWindow[id] = b;

  // Add to the customization palette
  let toolbox = doc.getElementById("navigator-toolbox");
  toolbox.palette.appendChild(b);

  // Search for widget toolbar by reading toolbar's currentset attribute
  let container = null;
  let toolbars = doc.getElementsByTagName("toolbar");
  for (let i = 0, l = toolbars.length; i < l; i++) {
    let toolbar = toolbars[i];
    if (toolbar.getAttribute("currentset").indexOf(id) == -1)
      continue;
    container = toolbar;
  }

  // if widget isn't in any toolbar, add it to the nav-bar
  if (!container) {
    if (haveInserted(id)) {
      return;
    }
    container = doc.getElementById("nav-bar");
    saveInserted(id);
  }

  // Now retrieve a reference to the next toolbar item
  // by reading currentset attribute on the toolbar
  let nextNode = null;
  let currentSet = container.getAttribute("currentset");
  let ids = (currentSet == "__empty") ? [] : currentSet.split(",");
  let idx = ids.indexOf(id);
  if (idx != -1) {
    for (let i = idx; i < ids.length; i++) {
      nextNode = doc.getElementById(ids[i]);
      if (nextNode)
        break;
    }
  }

  // Finally insert our widget in the right toolbar and in the right position
  container.insertItem(id, nextNode, null, false);

  // Update DOM in order to save position: which toolbar, and which position
  // in this toolbar. But only do this the first time we add it to the toolbar
  // Otherwise, this code may collide with other instance of Widget module
  // during Firefox startup. See bug 685929.
  if (ids.indexOf(id) == -1) {
    let set = container.currentSet;
    container.setAttribute("currentset", set);
    // Save DOM attribute in order to save position on new window opened
    aWindow.document.persist(container.id, "currentset");
  }
}

function removeButton(aWindow, id) {
  let b = aWindow[id];
  b.parentNode.removeChild(b);
  delete aWindow[id];
}

// Global window observer
function browserWindowObserver(handlers) {
  this.handlers = handlers;
}

browserWindowObserver.prototype = {
  observe: function(aSubject, aTopic, aData) {
    if (aTopic == "domwindowopened") {
      aSubject.QueryInterface(Ci.nsIDOMWindow).addEventListener("load", this, false);
    } else if (aTopic == "domwindowclosed") {
      if (aSubject.document.documentElement.getAttribute("windowtype") == "navigator:browser") {
        this.handlers.onShutdown(aSubject);
      }
    }
  },

  handleEvent: function(aEvent) {
    let aWindow = aEvent.currentTarget;
    aWindow.removeEventListener(aEvent.type, this, false);

    if (aWindow.document.documentElement.getAttribute("windowtype") == "navigator:browser") {
      this.handlers.onStartup(aWindow);
    }
  }
};

// Run on every window startup
function browserWindowStartup(aWindow) {
  for (let [id, onBuild] of blist) {
    insertButton(aWindow, id, onBuild);
  }
};

// Run on every window shutdown
function browserWindowShutdown(aWindow) {
  for (let [id, onBuild] of blist) {
    removeButton(aWindow, id);
  }
}

// Main object
const buttons = {
  createButton: function(aProperties) {
    // If no buttons were inserted, setup global window observer
    if (blist.size == 0) {
      let ww = Cc["@mozilla.org/embedcomp/window-watcher;1"].getService(Ci.nsIWindowWatcher);
      gWindowListener = new browserWindowObserver({
        onStartup: browserWindowStartup,
        onShutdown: browserWindowShutdown
      });
      ww.registerNotification(gWindowListener);
    }

    // Add button to list and insert to all open windows
    blist.set(aProperties.id, aProperties.onBuild);
    let wm = Cc["@mozilla.org/appshell/window-mediator;1"].getService(Ci.nsIWindowMediator);
    let winenu = wm.getEnumerator("navigator:browser");
    while (winenu.hasMoreElements()) {
      insertButton(winenu.getNext(), aProperties.id, aProperties.onBuild);
    }
  },

  destroyButton: function(id) {
    // Remove button from list
    blist.delete(id);

    // If list is empty, remove global window observer
    if (blist.size == 0) {
      let ww = Cc["@mozilla.org/embedcomp/window-watcher;1"].getService(Ci.nsIWindowWatcher);
      ww.unregisterNotification(gWindowListener);
      gWindowListener = null;
    }

    // Remove button from all open windows
    let wm = Cc["@mozilla.org/appshell/window-mediator;1"].getService(Ci.nsIWindowMediator);
    let winenu = wm.getEnumerator("navigator:browser");
    while (winenu.hasMoreElements()) {
      removeButton(winenu.getNext(), id);
    }
  },

  getNode: function(id, window) {
    return window[id];
  }
};

exports.buttons = buttons;
