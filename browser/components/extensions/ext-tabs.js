/* -*- Mode: indent-tabs-mode: nil; js-indent-level: 2 -*- */
/* vim: set sts=2 sw=2 et tw=80: */
"use strict";

XPCOMUtils.defineLazyServiceGetter(this, "aboutNewTabService",
                                   "@mozilla.org/browser/aboutnewtab-service;1",
                                   "nsIAboutNewTabService");

XPCOMUtils.defineLazyModuleGetter(this, "MatchPattern",
                                  "resource://gre/modules/MatchPattern.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "Services",
                                  "resource://gre/modules/Services.jsm");

Cu.import("resource://gre/modules/ExtensionUtils.jsm");

var {
  EventManager,
  ignoreEvent,
} = ExtensionUtils;

// This function is pretty tightly tied to Extension.jsm.
// Its job is to fill in the |tab| property of the sender.
function getSender(context, target, sender) {
  // The message was sent from a content script to a <browser> element.
  // We can just get the |tab| from |target|.
  if (target instanceof Ci.nsIDOMXULElement) {
    // The message came from a content script.
    let tabbrowser = target.ownerDocument.defaultView.gBrowser;
    if (!tabbrowser) {
      return;
    }
    let tab = tabbrowser.getTabForBrowser(target);

    sender.tab = TabManager.convert(context.extension, tab);
  } else if ("tabId" in sender) {
    // The message came from an ExtensionPage. In that case, it should
    // include a tabId property (which is filled in by the page-open
    // listener below).
    sender.tab = TabManager.convert(context.extension, TabManager.getTab(sender.tabId));
    delete sender.tabId;
  }
}

// WeakMap[ExtensionPage -> {tab, parentWindow}]
var pageDataMap = new WeakMap();

/* eslint-disable mozilla/balanced-listeners */
// This listener fires whenever an extension page opens in a tab
// (either initiated by the extension or the user). Its job is to fill
// in some tab-specific details and keep data around about the
// ExtensionPage.
extensions.on("page-load", (type, page, params, sender, delegate) => {
  if (params.type == "tab" || params.type == "popup") {
    let browser = params.docShell.chromeEventHandler;

    let parentWindow = browser.ownerDocument.defaultView;
    page.windowId = WindowManager.getId(parentWindow);

    let tab = null;
    if (params.type == "tab") {
      tab = parentWindow.gBrowser.getTabForBrowser(browser);
      sender.tabId = TabManager.getId(tab);
      page.tabId = TabManager.getId(tab);
    }

    pageDataMap.set(page, {tab, parentWindow});
  }

  delegate.getSender = getSender;
});

extensions.on("page-unload", (type, page) => {
  pageDataMap.delete(page);
});

extensions.on("page-shutdown", (type, page) => {
  if (pageDataMap.has(page)) {
    let {tab, parentWindow} = pageDataMap.get(page);
    pageDataMap.delete(page);

    if (tab) {
      parentWindow.gBrowser.removeTab(tab);
    }
  }
});

extensions.on("fill-browser-data", (type, browser, data, result) => {
  let tabId = TabManager.getBrowserId(browser);
  if (tabId == -1) {
    result.cancel = true;
    return;
  }

  data.tabId = tabId;
});
/* eslint-enable mozilla/balanced-listeners */

global.currentWindow = function(context) {
  let pageData = pageDataMap.get(context);
  if (pageData) {
    return pageData.parentWindow;
  }
  return WindowManager.topWindow;
};

// TODO: activeTab permission

extensions.registerSchemaAPI("tabs", null, (extension, context) => {
  let self = {
    tabs: {
      onActivated: new WindowEventManager(context, "tabs.onActivated", "TabSelect", (fire, event) => {
        let tab = event.originalTarget;
        let tabId = TabManager.getId(tab);
        let windowId = WindowManager.getId(tab.ownerDocument.defaultView);
        fire({tabId, windowId});
      }).api(),

      onCreated: new EventManager(context, "tabs.onCreated", fire => {
        let listener = event => {
          if (event.detail.adoptedTab) {
            // This tab is being created to adopt a tab from another window. We
            // map this event to an onAttached, rather than onCreated, event.
            return;
          }

          // We need to delay sending this event until the next tick, since the
          // tab does not have its final index when the TabOpen event is dispatched.
          let tab = event.originalTarget;
          Promise.resolve().then(() => {
            fire(TabManager.convert(extension, tab));
          });
        };

        let windowListener = window => {
          if (window.arguments[0] instanceof window.XULElement) {
            // If the first window argument is a XUL element, it means the
            // window is about to adopt a tab from another window to replace its
            // initial tab, which means we need to skip the onCreated event, and
            // fire an onAttached event instead.
            return;
          }

          for (let tab of window.gBrowser.tabs) {
            fire(TabManager.convert(extension, tab));
          }
        };

        WindowListManager.addOpenListener(windowListener);
        AllWindowEvents.addListener("TabOpen", listener);
        return () => {
          WindowListManager.removeOpenListener(windowListener);
          AllWindowEvents.removeListener("TabOpen", listener);
        };
      }).api(),

      onAttached: new EventManager(context, "tabs.onAttached", fire => {
        let fireForTab = tab => {
          let newWindowId = WindowManager.getId(tab.ownerDocument.defaultView);
          fire(TabManager.getId(tab), {newWindowId, newPosition: tab._tPos});
        };

        let listener = event => {
          if (event.detail.adoptedTab) {
            // We need to delay sending this event until the next tick, since the
            // tab does not have its final index when the TabOpen event is dispatched.
            Promise.resolve().then(() => {
              fireForTab(event.originalTarget);
            });
          }
        };

        let windowListener = window => {
          if (window.arguments[0] instanceof window.XULElement) {
            // If the first window argument is a XUL element, it means the
            // window is about to adopt a tab from another window to replace its
            // initial tab.
            //
            // Note that this event handler depends on running before the
            // delayed startup code in browser.js, which is currently triggered
            // by the first MozAfterPaint event. That code handles finally
            // adopting the tab, and clears it from the arguments list in the
            // process, so if we run later than it, we're too late.
            let tab = window.arguments[0];

            // We need to be sure to fire this event after the onDetached event
            // for the original tab.
            tab.addEventListener("TabClose", function listener(event) {
              tab.removeEventListener("TabClose", listener);
              Promise.resolve().then(() => {
                fireForTab(event.detail.adoptedBy);
              });
            });
          }
        };

        WindowListManager.addOpenListener(windowListener);
        AllWindowEvents.addListener("TabOpen", listener);
        return () => {
          WindowListManager.removeOpenListener(windowListener);
          AllWindowEvents.removeListener("TabOpen", listener);
        };
      }).api(),

      onDetached: new EventManager(context, "tabs.onDetached", fire => {
        let listener = event => {
          if (event.detail.adoptedBy) {
            let tab = event.originalTarget;
            let oldWindowId = WindowManager.getId(tab.ownerDocument.defaultView);
            fire(TabManager.getId(tab), {oldWindowId, oldPosition: tab._tPos});
          }
        };

        AllWindowEvents.addListener("TabClose", listener);
        return () => {
          AllWindowEvents.removeListener("TabClose", listener);
        };
      }).api(),

      onRemoved: new EventManager(context, "tabs.onRemoved", fire => {
        let fireForTab = (tab, isWindowClosing) => {
          let tabId = TabManager.getId(tab);
          let windowId = WindowManager.getId(tab.ownerDocument.defaultView);

          fire(tabId, {windowId, isWindowClosing});
        };

        let tabListener = event => {
          // Only fire if this tab is not being moved to another window. If it
          // is being adopted by another window, we fire an onDetached, rather
          // than an onRemoved, event.
          if (!event.detail.adoptedBy) {
            fireForTab(event.originalTarget, false);
          }
        };

        let windowListener = window => {
          for (let tab of window.gBrowser.tabs) {
            fireForTab(tab, true);
          }
        };

        WindowListManager.addCloseListener(windowListener);
        AllWindowEvents.addListener("TabClose", tabListener);
        return () => {
          WindowListManager.removeCloseListener(windowListener);
          AllWindowEvents.removeListener("TabClose", tabListener);
        };
      }).api(),

      onReplaced: ignoreEvent(context, "tabs.onReplaced"),

      onMoved: new EventManager(context, "tabs.onMoved", fire => {
        // There are certain circumstances where we need to ignore a move event.
        //
        // Namely, the first time the tab is moved after it's created, we need
        // to report the final position as the initial position in the tab's
        // onAttached or onCreated event. This is because most tabs are inserted
        // in a temporary location and then moved after the TabOpen event fires,
        // which generates a TabOpen event followed by a TabMove event, which
        // does not match the contract of our API.
        let ignoreNextMove = new WeakSet();

        let openListener = event => {
          ignoreNextMove.add(event.target);
          // Remove the tab from the set on the next tick, since it will already
          // have been moved by then.
          Promise.resolve().then(() => {
            ignoreNextMove.delete(event.target);
          });
        };

        let moveListener = event => {
          let tab = event.originalTarget;

          if (ignoreNextMove.has(tab)) {
            ignoreNextMove.delete(tab);
            return;
          }

          fire(TabManager.getId(tab), {
            windowId: WindowManager.getId(tab.ownerDocument.defaultView),
            fromIndex: event.detail,
            toIndex: tab._tPos,
          });
        };

        AllWindowEvents.addListener("TabMove", moveListener);
        AllWindowEvents.addListener("TabOpen", openListener);
        return () => {
          AllWindowEvents.removeListener("TabMove", moveListener);
          AllWindowEvents.removeListener("TabOpen", openListener);
        };
      }).api(),

      onUpdated: new EventManager(context, "tabs.onUpdated", fire => {
        function sanitize(extension, changeInfo) {
          let result = {};
          let nonempty = false;
          for (let prop in changeInfo) {
            if ((prop != "favIconUrl" && prop != "url") || extension.hasPermission("tabs")) {
              nonempty = true;
              result[prop] = changeInfo[prop];
            }
          }
          return [nonempty, result];
        }

        let fireForBrowser = (browser, changed) => {
          let [needed, changeInfo] = sanitize(extension, changed);
          if (needed) {
            let gBrowser = browser.ownerDocument.defaultView.gBrowser;
            let tabElem = gBrowser.getTabForBrowser(browser);

            let tab = TabManager.convert(extension, tabElem);
            fire(tab.id, changeInfo, tab);
          }
        };

        let listener = event => {
          let needed = [];
          if (event.type == "TabAttrModified") {
            let changed = event.detail.changed;
            if (changed.includes("image")) {
              needed.push("favIconUrl");
            }
            if (changed.includes("muted")) {
              needed.push("mutedInfo");
            }
            if (changed.includes("soundplaying")) {
              needed.push("audible");
            }
          } else if (event.type == "TabPinned") {
            needed.push("pinned");
          } else if (event.type == "TabUnpinned") {
            needed.push("pinned");
          }

          if (needed.length && !extension.hasPermission("tabs")) {
            needed = needed.filter(attr => attr != "url" && attr != "favIconUrl");
          }

          if (needed.length) {
            let tab = TabManager.convert(extension, event.originalTarget);

            let changeInfo = {};
            for (let prop of needed) {
              changeInfo[prop] = tab[prop];
            }
            fire(tab.id, changeInfo, tab);
          }
        };
        let progressListener = {
          onStateChange(browser, webProgress, request, stateFlags, statusCode) {
            if (!webProgress.isTopLevel) {
              return;
            }

            let status;
            if (stateFlags & Ci.nsIWebProgressListener.STATE_IS_WINDOW) {
              if (stateFlags & Ci.nsIWebProgressListener.STATE_START) {
                status = "loading";
              } else if (stateFlags & Ci.nsIWebProgressListener.STATE_STOP) {
                status = "complete";
              }
            } else if (stateFlags & Ci.nsIWebProgressListener.STATE_STOP &&
                       statusCode == Cr.NS_BINDING_ABORTED) {
              status = "complete";
            }

            fireForBrowser(browser, {status});
          },

          onLocationChange(browser, webProgress, request, locationURI, flags) {
            if (!webProgress.isTopLevel) {
              return;
            }

            fireForBrowser(browser, {
              status: webProgress.isLoadingDocument ? "loading" : "complete",
              url: locationURI.spec,
            });
          },
        };

        AllWindowEvents.addListener("progress", progressListener);
        AllWindowEvents.addListener("TabAttrModified", listener);
        AllWindowEvents.addListener("TabPinned", listener);
        AllWindowEvents.addListener("TabUnpinned", listener);

        return () => {
          AllWindowEvents.removeListener("progress", progressListener);
          AllWindowEvents.removeListener("TabAttrModified", listener);
          AllWindowEvents.removeListener("TabPinned", listener);
          AllWindowEvents.removeListener("TabUnpinned", listener);
        };
      }).api(),

      create: function(createProperties) {
        return new Promise(resolve => {
          function createInWindow(window) {
            let url;
            if (createProperties.url !== null) {
              url = context.uri.resolve(createProperties.url);
            } else {
              url = window.BROWSER_NEW_TAB_URL;
            }

            let tab = window.gBrowser.addTab(url);

            let active = true;
            if (createProperties.active !== null) {
              active = createProperties.active;
            }
            if (active) {
              window.gBrowser.selectedTab = tab;
            }

            if (createProperties.index !== null) {
              window.gBrowser.moveTabTo(tab, createProperties.index);
            }

            if (createProperties.pinned) {
              window.gBrowser.pinTab(tab);
            }

            resolve(TabManager.convert(extension, tab));
          }

          let window = createProperties.windowId !== null ?
            WindowManager.getWindow(createProperties.windowId) :
            WindowManager.topWindow;
          if (!window.gBrowser) {
            let obs = (finishedWindow, topic, data) => {
              if (finishedWindow != window) {
                return;
              }
              Services.obs.removeObserver(obs, "browser-delayed-startup-finished");
              createInWindow(window);
            };
            Services.obs.addObserver(obs, "browser-delayed-startup-finished", false);
          } else {
            createInWindow(window);
          }
        });
      },

      remove: function(tabs) {
        if (!Array.isArray(tabs)) {
          tabs = [tabs];
        }

        for (let tabId of tabs) {
          let tab = TabManager.getTab(tabId);
          tab.ownerDocument.defaultView.gBrowser.removeTab(tab);
        }

        return Promise.resolve();
      },

      update: function(tabId, updateProperties) {
        let tab = tabId !== null ? TabManager.getTab(tabId) : TabManager.activeTab;
        let tabbrowser = tab.ownerDocument.defaultView.gBrowser;
        if (updateProperties.url !== null) {
          tab.linkedBrowser.loadURI(updateProperties.url);
        }
        if (updateProperties.active !== null) {
          if (updateProperties.active) {
            tabbrowser.selectedTab = tab;
          } else {
            // Not sure what to do here? Which tab should we select?
          }
        }
        if (updateProperties.muted !== null) {
          if (tab.muted != updateProperties.muted) {
            tab.toggleMuteAudio(extension.uuid);
          }
        }
        if (updateProperties.pinned !== null) {
          if (updateProperties.pinned) {
            tabbrowser.pinTab(tab);
          } else {
            tabbrowser.unpinTab(tab);
          }
        }
        // FIXME: highlighted/selected, openerTabId

        return Promise.resolve(TabManager.convert(extension, tab));
      },

      reload: function(tabId, reloadProperties) {
        let tab = tabId !== null ? TabManager.getTab(tabId) : TabManager.activeTab;
        let flags = Ci.nsIWebNavigation.LOAD_FLAGS_NONE;
        if (reloadProperties && reloadProperties.bypassCache) {
          flags |= Ci.nsIWebNavigation.LOAD_FLAGS_BYPASS_CACHE;
        }
        tab.linkedBrowser.reloadWithFlags(flags);

        return Promise.resolve();
      },

      get: function(tabId) {
        let tab = TabManager.getTab(tabId);
        return Promise.resolve(TabManager.convert(extension, tab));
      },

      getCurrent() {
        let tab;
        if (context.tabId) {
          tab = TabManager.convert(extension, TabManager.getTab(context.tabId));
        }
        return Promise.resolve(tab);
      },

      getAllInWindow: function(windowId) {
        if (windowId === null) {
          windowId = WindowManager.topWindow.windowId;
        }

        return self.tabs.query({windowId});
      },

      query: function(queryInfo) {
        let pattern = null;
        if (queryInfo.url !== null) {
          pattern = new MatchPattern(queryInfo.url);
        }

        function matches(window, tab) {
          let props = ["active", "pinned", "highlighted", "status", "title", "index"];
          for (let prop of props) {
            if (queryInfo[prop] !== null && queryInfo[prop] != tab[prop]) {
              return false;
            }
          }

          let lastFocused = window == WindowManager.topWindow;
          if (queryInfo.lastFocusedWindow !== null && queryInfo.lastFocusedWindow != lastFocused) {
            return false;
          }

          let windowType = WindowManager.windowType(window);
          if (queryInfo.windowType !== null && queryInfo.windowType != windowType) {
            return false;
          }

          if (queryInfo.windowId !== null) {
            if (queryInfo.windowId == WindowManager.WINDOW_ID_CURRENT) {
              if (currentWindow(context) != window) {
                return false;
              }
            } else if (queryInfo.windowId != tab.windowId) {
              return false;
            }
          }

          if (queryInfo.audible !== null) {
            if (queryInfo.audible != tab.audible) {
              return false;
            }
          }

          if (queryInfo.muted !== null) {
            if (queryInfo.muted != tab.mutedInfo.muted) {
              return false;
            }
          }

          if (queryInfo.currentWindow !== null) {
            let eq = window == currentWindow(context);
            if (queryInfo.currentWindow != eq) {
              return false;
            }
          }

          if (pattern && !pattern.matches(Services.io.newURI(tab.url, null, null))) {
            return false;
          }

          return true;
        }

        let result = [];
        for (let window of WindowListManager.browserWindows()) {
          let tabs = TabManager.for(extension).getTabs(window);
          for (let tab of tabs) {
            if (matches(window, tab)) {
              result.push(tab);
            }
          }
        }
        return Promise.resolve(result);
      },

      captureVisibleTab: function(windowId, options) {
        if (!extension.hasPermission("<all_urls>")) {
          return Promise.reject({message: "The <all_urls> permission is required to use the captureVisibleTab API"});
        }

        let window = windowId == null ?
          WindowManager.topWindow :
          WindowManager.getWindow(windowId);

        let browser = window.gBrowser.selectedBrowser;
        let recipient = {
          innerWindowID: browser.innerWindowID,
        };

        if (!options) {
          options = {};
        }
        if (options.format == null) {
          options.format = "png";
        }
        if (options.quality == null) {
          options.quality = 92;
        }

        let message = {
          options,
          width: browser.clientWidth,
          height: browser.clientHeight,
        };

        return context.sendMessage(browser.messageManager, "Extension:Capture",
                                   message, recipient);
      },

      _execute: function(tabId, details, kind) {
        let tab = tabId !== null ? TabManager.getTab(tabId) : TabManager.activeTab;
        let mm = tab.linkedBrowser.messageManager;

        let options = {
          js: [],
          css: [],
        };

        let recipient = {
          innerWindowID: tab.linkedBrowser.innerWindowID,
        };

        if (TabManager.for(extension).hasActiveTabPermission(tab)) {
          // If we have the "activeTab" permission for this tab, ignore
          // the host whitelist.
          options.matchesHost = ["<all_urls>"];
        } else {
          options.matchesHost = extension.whiteListedHosts.serialize();
        }

        if (details.code !== null) {
          options[kind + "Code"] = details.code;
        }
        if (details.file !== null) {
          let url = context.uri.resolve(details.file);
          if (!extension.isExtensionURL(url)) {
            return Promise.reject({message: "Files to be injected must be within the extension"});
          }
          options[kind].push(url);
        }
        if (details.allFrames) {
          options.all_frames = details.allFrames;
        }
        if (details.matchAboutBlank) {
          options.match_about_blank = details.matchAboutBlank;
        }
        if (details.runAt !== null) {
          options.run_at = details.runAt;
        }

        return context.sendMessage(mm, "Extension:Execute", {options}, recipient);
      },

      executeScript: function(tabId, details) {
        return self.tabs._execute(tabId, details, "js");
      },

      insertCSS: function(tabId, details) {
        return self.tabs._execute(tabId, details, "css");
      },

      connect: function(tabId, connectInfo) {
        let tab = TabManager.getTab(tabId);
        let mm = tab.linkedBrowser.messageManager;

        let name = "";
        if (connectInfo && connectInfo.name !== null) {
          name = connectInfo.name;
        }
        let recipient = {extensionId: extension.id};
        if (connectInfo && connectInfo.frameId !== null) {
          recipient.frameId = connectInfo.frameId;
        }
        return context.messenger.connect(mm, name, recipient);
      },

      sendMessage: function(tabId, message, options, responseCallback) {
        let tab = TabManager.getTab(tabId);
        if (!tab) {
          // ignore sendMessage to non existent tab id
          return;
        }
        let mm = tab.linkedBrowser.messageManager;

        let recipient = {extensionId: extension.id};
        if (options && options.frameId !== null) {
          recipient.frameId = options.frameId;
        }
        return context.messenger.sendMessage(mm, message, recipient, responseCallback);
      },

      move: function(tabIds, moveProperties) {
        let index = moveProperties.index;
        let tabsMoved = [];
        if (!Array.isArray(tabIds)) {
          tabIds = [tabIds];
        }

        let destinationWindow = null;
        if (moveProperties.windowId !== null) {
          destinationWindow = WindowManager.getWindow(moveProperties.windowId);
          // Ignore invalid window.
          if (!destinationWindow) {
            return;
          }
        }

        /*
          Indexes are maintained on a per window basis so that a call to
            move([tabA, tabB], {index: 0})
              -> tabA to 0, tabB to 1 if tabA and tabB are in the same window
            move([tabA, tabB], {index: 0})
              -> tabA to 0, tabB to 0 if tabA and tabB are in different windows
        */
        let indexMap = new Map();

        for (let tabId of tabIds) {
          let tab = TabManager.getTab(tabId);
          // Ignore invalid tab ids.
          if (!tab) {
            continue;
          }

          // If the window is not specified, use the window from the tab.
          let window = destinationWindow || tab.ownerDocument.defaultView;
          let gBrowser = window.gBrowser;

          let insertionPoint = indexMap.get(window) || index;
          // If the index is -1 it should go to the end of the tabs.
          if (insertionPoint == -1) {
            insertionPoint = gBrowser.tabs.length;
          }

          // We can only move pinned tabs to a point within, or just after,
          // the current set of pinned tabs. Unpinned tabs, likewise, can only
          // be moved to a position after the current set of pinned tabs.
          // Attempts to move a tab to an illegal position are ignored.
          let numPinned = gBrowser._numPinnedTabs;
          let ok = tab.pinned ? insertionPoint <= numPinned : insertionPoint >= numPinned;
          if (!ok) {
            continue;
          }

          indexMap.set(window, insertionPoint + 1);

          if (tab.ownerDocument.defaultView !== window) {
            // If the window we are moving the tab in is different, then move the tab
            // to the new window.
            tab = gBrowser.adoptTab(tab, insertionPoint, false);
          } else {
            // If the window we are moving is the same, just move the tab.
            gBrowser.moveTabTo(tab, insertionPoint);
          }
          tabsMoved.push(tab);
        }

        return Promise.resolve(tabsMoved.map(tab => TabManager.convert(extension, tab)));
      },

      duplicate: function(tabId) {
        let tab = TabManager.getTab(tabId);
        if (!tab) {
          return Promise.reject({message: `Invalid tab ID: ${tabId}`});
        }

        let gBrowser = tab.ownerDocument.defaultView.gBrowser;
        let newTab = gBrowser.duplicateTab(tab);
        gBrowser.moveTabTo(newTab, tab._tPos + 1);
        gBrowser.selectTabAtIndex(newTab._tPos);

        return new Promise(resolve => {
          newTab.addEventListener("SSTabRestored", function listener() {
            newTab.removeEventListener("SSTabRestored", listener);
            return resolve(TabManager.convert(extension, newTab));
          });
        });
      },
    },
  };
  return self;
});
