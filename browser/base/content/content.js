/* -*- Mode: javascript; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

let Cc = Components.classes;
let Ci = Components.interfaces;
let Cu = Components.utils;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "BrowserUtils",
  "resource://gre/modules/BrowserUtils.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "LoginManagerContent",
  "resource://gre/modules/LoginManagerContent.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "InsecurePasswordUtils",
  "resource://gre/modules/InsecurePasswordUtils.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "PrivateBrowsingUtils",
  "resource://gre/modules/PrivateBrowsingUtils.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "FormSubmitObserver",
  "resource:///modules/FormSubmitObserver.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "Feeds",
  "resource:///modules/Feeds.jsm");

// Bug 671101 - directly using webNavigation in this context
// causes docshells to leak
this.__defineGetter__("webNavigation", function () {
  return docShell.QueryInterface(Ci.nsIWebNavigation);
});

addMessageListener("WebNavigation:LoadURI", function (message) {
  let flags = message.json.flags || webNavigation.LOAD_FLAGS_NONE;

  webNavigation.loadURI(message.json.uri, flags, null, null, null);
});

// TabChildGlobal
var global = this;

// Load the form validation popup handler
var formSubmitObserver = new FormSubmitObserver(content, this);

addMessageListener("Browser:HideSessionRestoreButton", function (message) {
  // Hide session restore button on about:home
  let doc = content.document;
  let container;
  if (doc.documentURI.toLowerCase() == "about:home" &&
      (container = doc.getElementById("sessionRestoreContainer"))){
    container.hidden = true;
  }
});

addEventListener("DOMFormHasPassword", function(event) {
  InsecurePasswordUtils.checkForInsecurePasswords(event.target);
  LoginManagerContent.onFormPassword(event);
});
addEventListener("DOMAutoComplete", function(event) {
  LoginManagerContent.onUsernameInput(event);
});
addEventListener("blur", function(event) {
  LoginManagerContent.onUsernameInput(event);
});

let AboutHomeListener = {
  init: function(chromeGlobal) {
    let self = this;
    chromeGlobal.addEventListener('AboutHomeLoad', function(e) { self.onPageLoad(); }, false, true);
  },

  handleEvent: function(aEvent) {
    switch (aEvent.type) {
      case "AboutHomeLoad":
        this.onPageLoad();
        break;
    }
  },

  receiveMessage: function(aMessage) {
    switch (aMessage.name) {
      case "AboutHome:Update":
        this.onUpdate(aMessage.data);
        break;
    }
  },

  onUpdate: function(aData) {
    let doc = content.document;
    if (doc.documentURI.toLowerCase() != "about:home")
      return;

    if (aData.showRestoreLastSession && !PrivateBrowsingUtils.isWindowPrivate(content))
      doc.getElementById("launcher").setAttribute("session", "true");

    // Inject search engine and snippets URL.
    let docElt = doc.documentElement;
    // set the following attributes BEFORE searchEngineName, which triggers to
    // show the snippets when it's set.
    docElt.setAttribute("snippetsURL", aData.snippetsURL);
    if (aData.showKnowYourRights)
      docElt.setAttribute("showKnowYourRights", "true");
    docElt.setAttribute("snippetsVersion", aData.snippetsVersion);
    docElt.setAttribute("searchEngineName", aData.defaultEngineName);
  },

  onPageLoad: function() {
    let doc = content.document;
    if (doc.documentURI.toLowerCase() != "about:home" ||
        doc.documentElement.hasAttribute("hasBrowserHandlers")) {
      return;
    }

    doc.documentElement.setAttribute("hasBrowserHandlers", "true");
    let updateListener = this;
    addMessageListener("AboutHome:Update", updateListener);
    addEventListener("click", this.onClick, true);
    addEventListener("pagehide", function onPageHide(event) {
      if (event.target.defaultView.frameElement)
        return;
      removeMessageListener("AboutHome:Update", updateListener);
      removeEventListener("click", this.onClick, true);
      removeEventListener("pagehide", onPageHide, true);
      if (event.target.documentElement)
        event.target.documentElement.removeAttribute("hasBrowserHandlers");
    }, true);

    // XXX bug 738646 - when Marketplace is launched, remove this statement and
    // the hidden attribute set on the apps button in aboutHome.xhtml
    if (Services.prefs.getPrefType("browser.aboutHome.apps") == Services.prefs.PREF_BOOL &&
        Services.prefs.getBoolPref("browser.aboutHome.apps"))
      doc.getElementById("apps").removeAttribute("hidden");

    sendAsyncMessage("AboutHome:RequestUpdate");

    doc.addEventListener("AboutHomeSearchEvent", function onSearch(e) {
      sendAsyncMessage("AboutHome:Search", { searchData: e.detail });
    }, true, true);
  },

  onClick: function(aEvent) {
    if (!aEvent.isTrusted || // Don't trust synthetic events
        aEvent.button == 2 || aEvent.target.localName != "button") {
      return;
    }

    let originalTarget = aEvent.originalTarget;
    let ownerDoc = originalTarget.ownerDocument;
    let elmId = originalTarget.getAttribute("id");

    switch (elmId) {
      case "restorePreviousSession":
        sendAsyncMessage("AboutHome:RestorePreviousSession");
        ownerDoc.getElementById("launcher").removeAttribute("session");
        break;

      case "downloads":
        sendAsyncMessage("AboutHome:Downloads");
        break;

      case "bookmarks":
        sendAsyncMessage("AboutHome:Bookmarks");
        break;

      case "history":
        sendAsyncMessage("AboutHome:History");
        break;

      case "apps":
        sendAsyncMessage("AboutHome:Apps");
        break;

      case "addons":
        sendAsyncMessage("AboutHome:Addons");
        break;

      case "sync":
        sendAsyncMessage("AboutHome:Sync");
        break;

      case "settings":
        sendAsyncMessage("AboutHome:Settings");
        break;
    }
  },
};
AboutHomeListener.init(this);

var global = this;

var ClickEventHandler = {
  init: function init() {
    Cc["@mozilla.org/eventlistenerservice;1"]
      .getService(Ci.nsIEventListenerService)
      .addSystemEventListener(global, "click", this, true);
  },

  handleEvent: function(event) {
    // Bug 903016: Most of this code is an unfortunate duplication from
    // contentAreaClick in browser.js.
    if (!event.isTrusted || event.defaultPrevented || event.button == 2)
      return;

    let [href, node] = this._hrefAndLinkNodeForClickEvent(event);

    let json = { button: event.button, shiftKey: event.shiftKey,
                 ctrlKey: event.ctrlKey, metaKey: event.metaKey,
                 altKey: event.altKey, href: null, title: null,
                 bookmark: false };

    if (href) {
      json.href = href;
      if (node) {
        json.title = node.getAttribute("title");

        if (event.button == 0 && !event.ctrlKey && !event.shiftKey &&
            !event.altKey && !event.metaKey) {
          json.bookmark = node.getAttribute("rel") == "sidebar";
          if (json.bookmark)
            event.preventDefault(); // Need to prevent the pageload.
        }
      }

      sendAsyncMessage("Content:Click", json);
      return;
    }

    // This might be middle mouse navigation.
    if (event.button == 1)
      sendAsyncMessage("Content:Click", json);
  },

  /**
   * Extracts linkNode and href for the current click target.
   *
   * @param event
   *        The click event.
   * @return [href, linkNode].
   *
   * @note linkNode will be null if the click wasn't on an anchor
   *       element (or XLink).
   */
  _hrefAndLinkNodeForClickEvent: function(event) {
    function isHTMLLink(aNode) {
      // Be consistent with what nsContextMenu.js does.
      return ((aNode instanceof content.HTMLAnchorElement && aNode.href) ||
              (aNode instanceof content.HTMLAreaElement && aNode.href) ||
              aNode instanceof content.HTMLLinkElement);
    }

    function makeURLAbsolute(aBase, aUrl) {
      // Note:  makeURI() will throw if aUri is not a valid URI
      return makeURI(aUrl, null, makeURI(aBase)).spec;
    }

    let node = event.target;
    while (node && !isHTMLLink(node)) {
      node = node.parentNode;
    }

    if (node)
      return [node.href, node];

    // If there is no linkNode, try simple XLink.
    let href, baseURI;
    node = event.target;
    while (node && !href) {
      if (node.nodeType == content.Node.ELEMENT_NODE) {
        href = node.getAttributeNS("http://www.w3.org/1999/xlink", "href");
        if (href)
          baseURI = node.baseURI;
      }
      node = node.parentNode;
    }

    // In case of XLink, we don't return the node we got href from since
    // callers expect <a>-like elements.
    return [href ? makeURLAbsolute(baseURI, href) : null, null];
  }
};
ClickEventHandler.init();

// Lazily load the finder code
addMessageListener("Finder:Initialize", function () {
  let {RemoteFinderListener} = Cu.import("resource://gre/modules/RemoteFinder.jsm", {});
  new RemoteFinderListener(global);
});

addEventListener("DOMWebNotificationClicked", function(event) {
  sendAsyncMessage("DOMWebNotificationClicked", {});
}, false);

addEventListener("DOMServiceWorkerFocusClient", function(event) {
  sendAsyncMessage("DOMServiceWorkerFocusClient", {});
}, false);

let pageInfoListener = {

  init: function(chromeGlobal) {
    chromeGlobal.addMessageListener("PageInfo:getData", this, false, true);
  },

  receiveMessage: function(message) {
    this.imageViewRows = [];
    this.frameList = [];
    this.strings = message.data.strings;

    let frameOuterWindowID = message.data.frameOuterWindowID;

    // If inside frame then get the frame's window and document.
    if (frameOuterWindowID) {
      this.window = Services.wm.getOuterWindowWithId(frameOuterWindowID);
      this.document = this.window.document;
    }
    else {
      this.document = content.document;
      this.window = content.window;
    }

    let pageInfoData = {metaViewRows: this.getMetaInfo(), docInfo: this.getDocumentInfo(),
                        feeds: this.getFeedsInfo(), windowInfo: this.getWindowInfo()};
    sendAsyncMessage("PageInfo:data", pageInfoData);

    // Separate step so page info dialog isn't blank while waiting for this to finish.
    this.getMediaInfo();

    // Send the message after all the media elements have been walked through.
    let pageInfoMediaData = {imageViewRows: this.imageViewRows};

    this.imageViewRows = null;
    this.frameList = null;
    this.strings = null;
    this.window = null;
    this.document = null;

    sendAsyncMessage("PageInfo:mediaData", pageInfoMediaData);
  },

  getMetaInfo: function() {
    let metaViewRows = [];

    // Get the meta tags from the page.
    let metaNodes = this.document.getElementsByTagName("meta");

    for (let metaNode of metaNodes) {
      metaViewRows.push([metaNode.name || metaNode.httpEquiv || metaNode.getAttribute("property"),
                        metaNode.content]);
    }

    return metaViewRows;
  },

  getWindowInfo: function() {
    let windowInfo = {};
    windowInfo.isTopWindow = this.window == this.window.top;

    let hostName = null;
    try {
      hostName = this.window.location.host;
    }
    catch (exception) { }

    windowInfo.hostName = hostName;
    return windowInfo;
  },

  getDocumentInfo: function() {
    let docInfo = {};
    docInfo.title = this.document.title;
    docInfo.location = this.document.location.toString();
    docInfo.referrer = this.document.referrer;
    docInfo.compatMode = this.document.compatMode;
    docInfo.contentType = this.document.contentType;
    docInfo.characterSet = this.document.characterSet;
    docInfo.lastModified = this.document.lastModified;

    let documentURIObject = {};
    documentURIObject.spec = this.document.documentURIObject.spec;
    documentURIObject.originCharset = this.document.documentURIObject.originCharset;
    docInfo.documentURIObject = documentURIObject;

    docInfo.isContentWindowPrivate = PrivateBrowsingUtils.isContentWindowPrivate(content);

    return docInfo;
  },

  getFeedsInfo: function() {
    let feeds = [];
    // Get the feeds from the page.
    let linkNodes = this.document.getElementsByTagName("link");
    let length = linkNodes.length;
    for (let i = 0; i < length; i++) {
      let link = linkNodes[i];
      if (!link.href) {
        continue;
      }
      let rel = link.rel && link.rel.toLowerCase();
      let rels = {};

      if (rel) {
        for each (let relVal in rel.split(/\s+/)) {
          rels[relVal] = true;
        }
      }

      if (rels.feed || (link.type && rels.alternate && !rels.stylesheet)) {
        let type = Feeds.isValidFeed(link, this.document.nodePrincipal, "feed" in rels);
        if (type) {
          type = this.strings[type] || this.strings["application/rss+xml"];
          feeds.push([link.title, type, link.href]);
        }
      }
    }
    return feeds;
  },

  // Only called once to get the media tab's media elements from the content page.
  // The actual work is done with a TreeWalker that calls doGrab() once for
  // each element node in the document.
  getMediaInfo: function()
  {
    this.goThroughFrames(this.document, this.window);
    this.processFrames();
  },

  goThroughFrames: function(aDocument, aWindow)
  {
    this.frameList.push(aDocument);
    if (aWindow && aWindow.frames.length > 0) {
      let num = aWindow.frames.length;
      for (let i = 0; i < num; i++) {
        this.goThroughFrames(aWindow.frames[i].document, aWindow.frames[i]);  // recurse through the frames
      }
    }
  },

  processFrames: function()
  {
    if (this.frameList.length) {
      let doc = this.frameList[0];
      let iterator = doc.createTreeWalker(doc, content.NodeFilter.SHOW_ELEMENT, elem => this.grabAll(elem));
      this.frameList.shift();
      this.doGrab(iterator);
    }
  },

  /**
   * This function's previous purpose in pageInfo.js was to get loop through 500 elements at a time.
   * The iterator filter will filter for media elements.
   * #TODO Bug 1175794: refactor pageInfo.js to receive a media element at a time
   * from messages and continually update UI.
   */
  doGrab: function(iterator)
  {
    while (true)
    {
      if (!iterator.nextNode()) {
        this.processFrames();
        return;
      }
    }
  },

  grabAll: function(elem)
  {
    // Check for images defined in CSS (e.g. background, borders), any node may have multiple.
    let computedStyle = elem.ownerDocument.defaultView.getComputedStyle(elem, "");

    let addImage = (url, type, alt, elem, isBg) => {
      let element = this.serializeElementInfo(url, type, alt, elem, isBg);
      this.imageViewRows.push([url, type, alt, element, isBg]);
    };

    if (computedStyle) {
      let addImgFunc = (label, val) => {
        if (val.primitiveType == content.CSSPrimitiveValue.CSS_URI) {
          addImage(val.getStringValue(), label, this.strings.notSet, elem, true);
        }
        else if (val.primitiveType == content.CSSPrimitiveValue.CSS_STRING) {
          // This is for -moz-image-rect.
          // TODO: Reimplement once bug 714757 is fixed.
          let strVal = val.getStringValue();
          if (strVal.search(/^.*url\(\"?/) > -1) {
            let url = strVal.replace(/^.*url\(\"?/,"").replace(/\"?\).*$/,"");
            addImage(url, label, this.strings.notSet, elem, true);
          }
        }
        else if (val.cssValueType == content.CSSValue.CSS_VALUE_LIST) {
          // Recursively resolve multiple nested CSS value lists.
          for (let i = 0; i < val.length; i++) {
            addImgFunc(label, val.item(i));
          }
        }
      };

      addImgFunc(this.strings.mediaBGImg, computedStyle.getPropertyCSSValue("background-image"));
      addImgFunc(this.strings.mediaBorderImg, computedStyle.getPropertyCSSValue("border-image-source"));
      addImgFunc(this.strings.mediaListImg, computedStyle.getPropertyCSSValue("list-style-image"));
      addImgFunc(this.strings.mediaCursor, computedStyle.getPropertyCSSValue("cursor"));
    }

    // One swi^H^H^Hif-else to rule them all.
    if (elem instanceof content.HTMLImageElement) {
      addImage(elem.src, this.strings.mediaImg,
               (elem.hasAttribute("alt")) ? elem.alt : this.strings.notSet, elem, false);
    }
    else if (elem instanceof content.SVGImageElement) {
      try {
        // Note: makeURLAbsolute will throw if either the baseURI is not a valid URI
        //       or the URI formed from the baseURI and the URL is not a valid URI.
        let href = makeURLAbsolute(elem.baseURI, elem.href.baseVal);
        addImage(href, this.strings.mediaImg, "", elem, false);
      } catch (e) { }
    }
    else if (elem instanceof content.HTMLVideoElement) {
      addImage(elem.currentSrc, this.strings.mediaVideo, "", elem, false);
    }
    else if (elem instanceof content.HTMLAudioElement) {
      addImage(elem.currentSrc, this.strings.mediaAudio, "", elem, false);
    }
    else if (elem instanceof content.HTMLLinkElement) {
      if (elem.rel && /\bicon\b/i.test(elem.rel)) {
        addImage(elem.href, this.strings.mediaLink, "", elem, false);
      }
    }
    else if (elem instanceof content.HTMLInputElement || elem instanceof content.HTMLButtonElement) {
      if (elem.type.toLowerCase() == "image") {
        addImage(elem.src, this.strings.mediaInput,
                 (elem.hasAttribute("alt")) ? elem.alt : this.strings.notSet, elem, false);
      }
    }
    else if (elem instanceof content.HTMLObjectElement) {
      addImage(elem.data, this.strings.mediaObject, this.getValueText(elem), elem, false);
    }
    else if (elem instanceof content.HTMLEmbedElement) {
      addImage(elem.src, this.strings.mediaEmbed, "", elem, false);
    }

    return content.NodeFilter.FILTER_ACCEPT;
  },

  /**
   * Set up a JSON element object with all the instanceOf and other infomation that
   * makePreview in pageInfo.js uses to figure out how to display the preview.
   */

  serializeElementInfo: function(url, type, alt, item, isBG)
  {
    // Interface for image loading content.
    const nsIImageLoadingContent = Components.interfaces.nsIImageLoadingContent;

    let result = {};

    let imageText;
    if (!isBG &&
        !(item instanceof content.SVGImageElement) &&
        !(this.document instanceof content.ImageDocument)) {
      imageText = item.title || item.alt;

      if (!imageText && !(item instanceof content.HTMLImageElement)) {
        imageText = this.getValueText(item);
      }
    }

    result.imageText = imageText;
    result.longDesc = item.longDesc;
    result.numFrames = 1;

    if (item instanceof content.HTMLObjectElement ||
      item instanceof content.HTMLEmbedElement ||
      item instanceof content.HTMLLinkElement) {
      result.mimeType = item.type;
    }

    if (!result.mimeType && !isBG && item instanceof nsIImageLoadingContent) {
      // Interface for image loading content.
      const nsIImageLoadingContent = Components.interfaces.nsIImageLoadingContent;
      let imageRequest = item.getRequest(nsIImageLoadingContent.CURRENT_REQUEST);
      if (imageRequest) {
        result.mimeType = imageRequest.mimeType;
        let image = !(imageRequest.imageStatus & imageRequest.STATUS_ERROR) && imageRequest.image;
        if (image) {
          result.numFrames = image.numFrames;
        }
      }
    }

    // if we have a data url, get the MIME type from the url
    if (!result.mimeType && url.startsWith("data:")) {
      let dataMimeType = /^data:(image\/[^;,]+)/i.exec(url);
      if (dataMimeType)
        result.mimeType = dataMimeType[1].toLowerCase();
    }

    result.HTMLLinkElement = item instanceof content.HTMLLinkElement;
    result.HTMLInputElement = item instanceof content.HTMLInputElement;
    result.HTMLImageElement = item instanceof content.HTMLImageElement;
    result.HTMLObjectElement = item instanceof content.HTMLObjectElement;
    result.SVGImageElement = item instanceof content.SVGImageElement;
    result.HTMLVideoElement = item instanceof content.HTMLVideoElement;
    result.HTMLAudioElement = item instanceof content.HTMLAudioElement;

    if (!isBG) {
      result.width = item.width;
      result.height = item.height;
    }

    if (item instanceof content.SVGImageElement) {
      result.SVGImageElementWidth = item.width.baseVal.value;
      result.SVGImageElementHeight = item.height.baseVal.value;
    }

    result.baseURI = item.baseURI;

    return result;
  },

  //******** Other Misc Stuff
  // Modified from the Links Panel v2.3, http://segment7.net/mozilla/links/links.html
  // parse a node to extract the contents of the node
  getValueText: function(node)
  {

    let valueText = "";

    // Form input elements don't generally contain information that is useful to our callers, so return nothing.
    if (node instanceof content.HTMLInputElement ||
        node instanceof content.HTMLSelectElement ||
        node instanceof content.HTMLTextAreaElement) {
      return valueText;
    }

    // Otherwise recurse for each child.
    let length = node.childNodes.length;

    for (let i = 0; i < length; i++) {
      let childNode = node.childNodes[i];
      let nodeType = childNode.nodeType;

      // Text nodes are where the goods are.
      if (nodeType == content.Node.TEXT_NODE) {
        valueText += " " + childNode.nodeValue;
      }
      // And elements can have more text inside them.
      else if (nodeType == content.Node.ELEMENT_NODE) {
        // Images are special, we want to capture the alt text as if the image weren't there.
        if (childNode instanceof content.HTMLImageElement) {
          valueText += " " + this.getAltText(childNode);
        }
        else {
          valueText += " " + this.getValueText(childNode);
        }
      }
    }

    return this.stripWS(valueText);
  },

  // Copied from the Links Panel v2.3, http://segment7.net/mozilla/links/links.html.
  // Traverse the tree in search of an img or area element and grab its alt tag.
  getAltText: function(node)
  {
    let altText = "";

    if (node.alt) {
      return node.alt;
    }
    let length = node.childNodes.length;
    for (let i = 0; i < length; i++) {
      if ((altText = this.getAltText(node.childNodes[i]) != undefined)) { // stupid js warning...
        return altText;
      }
    }
    return "";
  },

  // Copied from the Links Panel v2.3, http://segment7.net/mozilla/links/links.html.
  // Strip leading and trailing whitespace, and replace multiple consecutive whitespace characters with a single space.
  stripWS: function(text)
  {
    let middleRE = /\s+/g;
    let endRE = /(^\s+)|(\s+$)/g;

    text = text.replace(middleRE, " ");
    return text.replace(endRE, "");
  }
};
pageInfoListener.init(this);
