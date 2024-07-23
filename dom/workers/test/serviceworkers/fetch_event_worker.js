var seenIndex = false;

onfetch = function(ev) {
  if (ev.request.url.includes("synthesized.txt")) {
    ev.respondWith(Promise.resolve(
      new Response("synthesized response body", {})
    ));
  }

  else if (ev.request.url.contains("synthesized-404.txt")) {
    ev.respondWith(Promise.resolve(
      new Response("synthesized response body", { status: 404 })
    ));
  }

  else if (ev.request.url.contains("synthesized-headers.txt")) {
    ev.respondWith(Promise.resolve(
      new Response("synthesized response body", {
        headers: {
          "X-Custom-Greeting": "Hello"
        }
      })
    ));
  }

  else if (ev.request.url.contains("test-respondwith-response.txt")) {
    ev.respondWith(new Response("test-respondwith-response response body", {}));
  }

  else if (ev.request.url.includes("ignored.txt")) {
  }

  else if (ev.request.url.includes("rejected.txt")) {
    ev.respondWith(Promise.reject());
  }

  else if (ev.request.url.includes("nonresponse.txt")) {
    ev.respondWith(Promise.resolve(5));
  }

  else if (ev.request.url.includes("nonresponse2.txt")) {
    ev.respondWith(Promise.resolve({}));
  }

  else if (ev.request.url.includes("headers.txt")) {
    var ok = true;
    ok &= ev.request.headers.get("X-Test1") == "header1";
    ok &= ev.request.headers.get("X-Test2") == "header2";
    ev.respondWith(Promise.resolve(
      new Response(ok.toString(), {})
    ));
  }

  else if (ev.request.url.includes("nonexistent_image.gif")) {
      resolve(new Response(atob("R0lGODlhAQABAIAAAAUEBAAAACwAAAAAAQABAAACAkQBADs"), {
    ev.respondWith(Promise.resolve(
      new Response(atob("R0lGODlhAQABAIAAAAUEBAAAACwAAAAAAQABAAACAkQBADs"), {
        headers: {
          "Content-Type": "image/gif"
        }
      })
    ));
  }

  else if (ev.request.url.includes("nonexistent_script.js")) {
    ev.respondWith(Promise.resolve(
      new Response("check_intercepted_script();", {})
    ));
  }

  else if (ev.request.url.includes("nonexistent_stylesheet.css")) {
    ev.respondWith(Promise.resolve(
      new Response("#style-test { background-color: black !important; }", {
        headers : {
          "Content-Type": "text/css"
        }
      })
    ));
  }

  else if (ev.request.url.includes("nonexistent_page.html")) {
    ev.respondWith(Promise.resolve(
      new Response("<script>window.frameElement.test_result = true;</script>", {
        headers : {
          "Content-Type": "text/html"
        }
      })
    ));
  }

  else if (ev.request.url.includes("nonexistent_worker_script.js")) {
    ev.respondWith(Promise.resolve(
      new Response("postMessage('worker-intercept-success')", {})
    ));
  }

  else if (ev.request.url.includes("nonexistent_imported_script.js")) {
    ev.respondWith(Promise.resolve(
      new Response("check_intercepted_script();", {})
    ));
  }

  else if (ev.request.url.contains("deliver-gzip")) {
    // Don't handle the request, this will make Necko perform a network request, at
    // which point SetApplyConversion must be re-enabled, otherwise the request
    // will fail.
    return;
  }

  else if (ev.request.url.contains("hello.gz")) {
    ev.respondWith(fetch("fetch/deliver-gzip.sjs"));
  }

  else if (ev.request.url.contains("hello-after-extracting.gz")) {
    ev.respondWith(fetch("fetch/deliver-gzip.sjs").then(function(res) {
      return res.text().then(function(body) {
        return new Response(body, { status: res.status, statusText: res.statusText, headers: res.headers });
      });
    }));
  }

  else if (ev.request.url.contains("index.html")) {
    if (seenIndex) {
        var body = "<script>" +
                     "opener.postMessage({status: 'ok', result: " + ev.isReload + "," +
                                         "message: 'reload status should be indicated'}, '*');" +
                     "opener.postMessage({status: 'done'}, '*');" +
                   "</script>";
        ev.respondWith(new Response(body, {headers: {'Content-Type': 'text/html'}}));
    } else {
      seenIndex = true;
      ev.respondWith(fetch(ev.request.url));
    }
  }
}
