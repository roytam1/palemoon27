/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cu = Components.utils;

Cu.import("resource://gre/modules/XPCOMUtils.jsm");

XPCOMUtils.defineLazyModuleGetter(this, "BrowserUtils",
                                  "resource://gre/modules/BrowserUtils.jsm");
XPCOMUtils.defineLazyServiceGetter(this, "DOMUtils",
                                   "@mozilla.org/inspector/dom-utils;1", "inIDOMUtils");

const kStateHover = 0x00000004; // NS_EVENT_STATE_HOVER

this.EXPORTED_SYMBOLS = [
  "SelectContentHelper"
];

this.SelectContentHelper = function (aElement, aGlobal) {
  this.element = aElement;
  this.initialSelection = aElement[aElement.selectedIndex] || null;
  this.global = aGlobal;
  this.init();
  this.showDropDown();
}

this.SelectContentHelper.prototype = {
  init: function() {
    this.global.addMessageListener("Forms:SelectDropDownItem", this);
    this.global.addMessageListener("Forms:DismissedDropDown", this);
    this.global.addMessageListener("Forms:MouseOver", this);
    this.global.addMessageListener("Forms:MouseOut", this);
    this.global.addEventListener("pagehide", this);
    this.global.addEventListener("mozhidedropdown", this);
  },

  uninit: function() {
    this.global.removeMessageListener("Forms:SelectDropDownItem", this);
    this.global.removeMessageListener("Forms:DismissedDropDown", this);
    this.global.removeMessageListener("Forms:MouseOver", this);
    this.global.removeMessageListener("Forms:MouseOut", this);
    this.global.removeEventListener("pagehide", this);
    this.global.removeEventListener("mozhidedropdown", this);
    this.element = null;
    this.global = null;
  },

  showDropDown: function() {
    let rect = this._getBoundingContentRect();

    this.global.sendAsyncMessage("Forms:ShowDropDown", {
      rect: rect,
      options: this._buildOptionList(),
      selectedIndex: this.element.selectedIndex,
    });
  },

  _getBoundingContentRect: function() {
    return BrowserUtils.getElementBoundingScreenRect(this.element);
  },

  _buildOptionList: function() {
    return buildOptionListForChildren(this.element);
  },

  receiveMessage: function(message) {
    switch (message.name) {
      case "Forms:SelectDropDownItem":
        this.element.selectedIndex = message.data.value;
        break;

      case "Forms:DismissedDropDown":
        if (this.initialSelection != this.element.item[this.element.selectedIndex]) {
          let event = this.element.ownerDocument.createEvent("Events");
          event.initEvent("change", true, true);
          this.element.dispatchEvent(event);
        }

        this.uninit();
        break;

      case "Forms:MouseOver":
        DOMUtils.setContentState(this.element, kStateHover);
        break;

      case "Forms:MouseOut":
        DOMUtils.removeContentState(this.element, kStateHover);
        break;

    }
  },

  handleEvent: function(event) {
    switch (event.type) {
      case "pagehide":
        if (this.element.ownerDocument === event.target) {
          this.global.sendAsyncMessage("Forms:HideDropDown", {});
          this.uninit();
        }
        break;
      case "mozhidedropdown":
        if (this.element === event.target) {
          this.global.sendAsyncMessage("Forms:HideDropDown", {});
          this.uninit();
        }
        break;
    }
  }

}

function buildOptionListForChildren(node) {
  let result = [];
  for (let child = node.firstChild; child; child = child.nextSibling) {
    if (child.tagName == 'OPTION' || child.tagName == 'OPTGROUP') {
      let textContent =
        child.tagName == 'OPTGROUP' ? child.getAttribute("label")
                                    : child.textContent;

      if (textContent != null) {
        textContent = textContent.trim();
      } else {
        textContent = ""
      }

      let info = {
        tagName: child.tagName,
        textContent: textContent,
        // XXX this uses a highlight color when this is the selected element.
        // We need to suppress such highlighting in the content process to get
        // the option's correct unhighlighted color here.
        // We also need to detect default color vs. custom so that a standard
        // color does not override color: menutext in the parent.
        // backgroundColor: computedStyle.backgroundColor,
        // color: computedStyle.color,
        children: child.tagName == 'OPTGROUP' ? buildOptionListForChildren(child) : []
      };
      result.push(info);
    }
  }
  return result;
}
