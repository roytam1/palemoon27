/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

const { classes: Cc, interfaces: Ci } = Components;

function gzipCompressString(string, obs) {

  let scs = Cc["@mozilla.org/streamConverters;1"]
           .getService(Ci.nsIStreamConverterService);
  let listener = Cc["@mozilla.org/network/stream-loader;1"]
                .createInstance(Ci.nsIStreamLoader);
  listener.init(obs);
  let converter = scs.asyncConvertData("uncompressed", "gzip",
                                        listener, null);
  let stringStream = Cc["@mozilla.org/io/string-input-stream;1"]
                    .createInstance(Ci.nsIStringInputStream);
  stringStream.data = string;
  converter.onStartRequest(null, null);
  converter.onDataAvailable(null, null, stringStream, 0, string.length);
  converter.onStopRequest(null, null, null);
}

function doubleGzipCompressString(string, observer) {
  let observer2 = {
    onStreamComplete: function(loader, context, status, length, result) {
      let buffer = String.fromCharCode.apply(this, result);
      gzipCompressString(buffer, observer);
    }
  };
  gzipCompressString(string, observer2);
}

function handleRequest(request, response) {
  response.processAsync();

  let params = request.queryString.split("&");
  let format = (params.filter((s) => s.contains("fmt="))[0] || "").split("=")[1];
  let status = (params.filter((s) => s.contains("sts="))[0] || "").split("=")[1] || 200;

  let cachedCount = 0;
  let cacheExpire = 60; // seconds

  function setCacheHeaders() {
    if (status != 304) {
      response.setHeader("Cache-Control", "no-cache, no-store, must-revalidate");
      response.setHeader("Pragma", "no-cache");
      response.setHeader("Expires", "0");
      return;
    }
    // Spice things up a little!
    if (cachedCount % 2) {
      response.setHeader("Cache-Control", "max-age=" + cacheExpire, false);
    } else {
      response.setHeader("Expires", Date(Date.now() + cacheExpire * 1000), false);
    }
    cachedCount++;
  }

  let timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
  timer.initWithCallback(() => {
    // to avoid garbage collection
    timer = null;
    switch (format) {
      case "txt": {
        response.setStatusLine(request.httpVersion, status, "DA DA DA");
        response.setHeader("Content-Type", "text/plain", false);
        setCacheHeaders();
        response.write("Братан, ты вообще качаешься?");
        response.finish();
        break;
      }
      case "xml": {
        response.setStatusLine(request.httpVersion, status, "OK");
        response.setHeader("Content-Type", "text/xml; charset=utf-8", false);
        setCacheHeaders();
        response.write("<label value='greeting'>Hello XML!</label>");
        response.finish();
        break;
      }
      case "html": {
        let content = params.filter((s) => s.contains("res="))[0].split("=")[1];
        response.setStatusLine(request.httpVersion, status, "OK");
        response.setHeader("Content-Type", "text/html; charset=utf-8", false);
        setCacheHeaders();
        response.write(content || "<p>Hello HTML!</p>");
        response.finish();
        break;
      }
      case "html-long": {
        let str = new Array(102400 /* 100 KB in bytes */).join(".");
        response.setStatusLine(request.httpVersion, status, "OK");
        response.setHeader("Content-Type", "text/html; charset=utf-8", false);
        setCacheHeaders();
        response.write("<p>" + str + "</p>");
        response.finish();
        break;
      }
      case "css": {
        response.setStatusLine(request.httpVersion, status, "OK");
        response.setHeader("Content-Type", "text/css; charset=utf-8", false);
        setCacheHeaders();
        response.write("body:pre { content: 'Hello CSS!' }");
        response.finish();
        break;
      }
      case "js": {
        response.setStatusLine(request.httpVersion, status, "OK");
        response.setHeader("Content-Type", "application/javascript; charset=utf-8", false);
        setCacheHeaders();
        response.write("function() { return 'Hello JS!'; }");
        response.finish();
        break;
      }
      case "json": {
        response.setStatusLine(request.httpVersion, status, "OK");
        response.setHeader("Content-Type", "application/json; charset=utf-8", false);
        setCacheHeaders();
        response.write("{ \"greeting\": \"Hello JSON!\" }");
        response.finish();
        break;
      }
      case "jsonp": {
        let fun = params.filter((s) => s.contains("jsonp="))[0].split("=")[1];
        response.setStatusLine(request.httpVersion, status, "OK");
        response.setHeader("Content-Type", "text/json; charset=utf-8", false);
        setCacheHeaders();
        response.write(fun + "({ \"greeting\": \"Hello JSONP!\" })");
        response.finish();
        break;
      }
      case "jsonp2": {
        let fun = params.filter((s) => s.contains("jsonp="))[0].split("=")[1];
        response.setStatusLine(request.httpVersion, status, "OK");
        response.setHeader("Content-Type", "text/json; charset=utf-8", false);
        setCacheHeaders();
        response.write(" " + fun + " ( { \"greeting\": \"Hello weird JSONP!\" } ) ; ");
        response.finish();
        break;
      }
      case "json-long": {
        let str = "{ \"greeting\": \"Hello long string JSON!\" },";
        response.setStatusLine(request.httpVersion, status, "OK");
        response.setHeader("Content-Type", "text/json; charset=utf-8", false);
        setCacheHeaders();
        response.write("[" + new Array(2048).join(str).slice(0, -1) + "]");
        response.finish();
        break;
      }
      case "json-malformed": {
        response.setStatusLine(request.httpVersion, status, "OK");
        response.setHeader("Content-Type", "text/json; charset=utf-8", false);
        setCacheHeaders();
        response.write("{ \"greeting\": \"Hello malformed JSON!\" },");
        response.finish();
        break;
      }
      case "json-text-mime": {
        response.setStatusLine(request.httpVersion, status, "OK");
        response.setHeader("Content-Type", "text/plain; charset=utf-8", false);
        setCacheHeaders();
        response.write("{ \"greeting\": \"Hello third-party JSON!\" }");
        response.finish();
        break;
      }
      case "json-custom-mime": {
        response.setStatusLine(request.httpVersion, status, "OK");
        response.setHeader("Content-Type", "text/x-bigcorp-json; charset=utf-8", false);
        setCacheHeaders();
        response.write("{ \"greeting\": \"Hello oddly-named JSON!\" }");
        response.finish();
        break;
      }
      case "font": {
        response.setStatusLine(request.httpVersion, status, "OK");
        response.setHeader("Content-Type", "font/woff", false);
        setCacheHeaders();
        response.finish();
        break;
      }
      case "image": {
        response.setStatusLine(request.httpVersion, status, "OK");
        response.setHeader("Content-Type", "image/png", false);
        setCacheHeaders();
        response.finish();
        break;
      }
      case "audio": {
        response.setStatusLine(request.httpVersion, status, "OK");
        response.setHeader("Content-Type", "audio/ogg", false);
        setCacheHeaders();
        response.finish();
        break;
      }
      case "video": {
        response.setStatusLine(request.httpVersion, status, "OK");
        response.setHeader("Content-Type", "video/webm", false);
        setCacheHeaders();
        response.finish();
        break;
      }
      case "flash": {
        response.setStatusLine(request.httpVersion, status, "OK");
        response.setHeader("Content-Type", "application/x-shockwave-flash", false);
        setCacheHeaders();
        response.finish();
        break;
      }
      case "gzip": {
        // Note: we're doing a double gzip encoding to test multiple
        // converters in network monitor.
        response.setStatusLine(request.httpVersion, status, "OK");
        response.setHeader("Content-Type", "text/plain", false);
        response.setHeader("Content-Encoding", "gzip\t ,gzip", false);
        setCacheHeaders();
        let observer = {
          onStreamComplete: function(loader, context, status, length, result) {
            let buffer = String.fromCharCode.apply(this, result);
            response.setHeader("Content-Length", "" + buffer.length, false);
            response.write(buffer);
            response.finish();
          }
        };
        let data = new Array(1000).join("Hello gzip!");
        doubleGzipCompressString(data, observer);
        break;
      }
      default: {
        response.setStatusLine(request.httpVersion, 404, "Not Found");
        response.setHeader("Content-Type", "text/html; charset=utf-8", false);
        setCacheHeaders();
        response.write("<blink>Not Found</blink>");
        response.finish();
        break;
      }
    }
  }, 10, Ci.nsITimer.TYPE_ONE_SHOT); // Make sure this request takes a few ms.
}
