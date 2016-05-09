/* coded by Ketmar // Invisible Vector (psyc://ketmar.no-ip.org/~Ketmar)
 * Understanding is not required. Only obedience.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, 
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
////////////////////////////////////////////////////////////////////////////////
let EXPORTED_SYMBOLS = ["pij_registerWindow"];

const {utils:Cu, classes:Cc, interfaces:Ci, results:Cr} = Components;

Cu.import("resource://gre/modules/Services.jsm");
Cu.import("resource://gre/modules/XPCOMUtils.jsm");


////////////////////////////////////////////////////////////////////////////////
function loadTextContentsFromUrl (url, encoding) {
  if (typeof(encoding) !== "string") encoding = "UTF-8"; else encoding = encoding||"UTF-8";

  function toUnicode (text) {
    if (typeof(text) === "string") {
      if (text.length == 0) return "";
    } else if (text instanceof ArrayBuffer) {
      if (text.byteLength == 0) return "";
      text = new Uint8Array(text);
      return new TextDecoder(encoding).decode(text);
    } else if ((text instanceof Uint8Array) || (text instanceof Int8Array)) {
      if (text.length == 0) return "";
      return new TextDecoder(encoding).decode(text);
    } else {
      return "";
    }
    let converter = Cc["@mozilla.org/intl/scriptableunicodeconverter"].createInstance(Ci.nsIScriptableUnicodeConverter);
    converter.charset = encoding;
    return converter.ConvertToUnicode(text);
  }

  // download from URL
  let req = Cc["@mozilla.org/xmlextras/xmlhttprequest;1"].createInstance();
  req.mozBackgroundRequest = true;
  req.open("GET", url, false);
  req.timeout = 30*1000;
  let cirq = Ci.nsIRequest;
  req.channel.loadFlags |= cirq.INHIBIT_CACHING|cirq.INHIBIT_PERSISTENT_CACHING|cirq.LOAD_BYPASS_CACHE|cirq.LOAD_ANONYMOUS;
  req.responseType = "arraybuffer";
  let error = false;
  req.onerror = function () { error = true; }; // just in case
  req.ontimeout = function () { error = true; }; // just in case
  req.send(null);
  if (!error) error = (req.status != 0 && Math.floor(req.status/100) != 2);
  if (error) throw new Error("can't load URI contents: status="+req.status+"; error="+error);
  return toUnicode(req.response, encoding);
}


////////////////////////////////////////////////////////////////////////////////
// load promise library
let ppfsrc = loadTextContentsFromUrl("chrome://browser/content/promises/data/es6-shim.js");


////////////////////////////////////////////////////////////////////////////////
function createSandbox (domWin) {
  // create "unwrapped" sandbox
  let sandbox = Cu.Sandbox(domWin, {
    sandboxName: "unwrapped-polypromise",
    sameZoneAs: domWin, // help GC a little
    sandboxPrototype: domWin,
    wantXrays: false,
  });

  // nuke sandbox when this window unloaded (this helps GC)
  domWin.addEventListener("unload", function () {
    if (sandbox) {
      Cu.nukeSandbox(sandbox);
      sandbox = null;
    }
  }, true);

  return sandbox;
}


////////////////////////////////////////////////////////////////////////////////
function ContentObserver () {}


ContentObserver.prototype = {
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIObserver]),

  observe: function (aSubject, aTopic, aData) {
    if (aTopic === "document-element-inserted") {
      let doc = aSubject;
      if (!doc) return;
      if (!(doc instanceof Ci.nsIDOMHTMLDocument)) {
        //Cu.reportError("PPM: non-DOM: "+(doc.documentURI ? doc.documentURI : "<unknown>"));
        //not html document, so its likely an xul document
        return;
      }
      let win = doc.defaultView;
      if (!win) return;
      //Cu.reportError("PPM: DOM: "+(doc.documentURI ? doc.documentURI : "<unknown>"));
      let sbox = createSandbox(win);
      Cu.evalInSandbox(ppfsrc, sbox, "ECMAv5");
    }
  }
};


////////////////////////////////////////////////////////////////////////////////
let contentObserver = new ContentObserver();


////////////////////////////////////////////////////////////////////////////////
Services.obs.addObserver(contentObserver, "document-element-inserted", false);


////////////////////////////////////////////////////////////////////////////////
function pij_registerWindow (win, global) {
  /*
  win.addEventListener("unload", function () {
    Services.obs.removeObserver(contentObserver, "document-element-inserted");
  }, false);
  */
}
