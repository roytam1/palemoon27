function fetch(name, onload, onerror, headers) {
  expectAsyncResult();

  onload = onload || function() {
    my_ok(false, "XHR load should not complete successfully");
    finish();
  };
  onerror = onerror || function() {
    my_ok(false, "XHR load should be intercepted successfully");
    finish();
  };

  var x = new XMLHttpRequest();
  x.open('GET', name, true);
  x.onload = function() { onload(x) };
  x.onerror = function() { onerror(x) };
  headers = headers || [];
  headers.forEach(function(header) {
    x.setRequestHeader(header[0], header[1]);
  });
  x.send();
}

fetch('synthesized.txt', function(xhr) {
  my_ok(xhr.status == 200, "load should be successful");
  my_ok(xhr.responseText == "synthesized response body", "load should have synthesized response");
  finish();
});

fetch('test-respondwith-response.txt', function(xhr) {
  my_ok(xhr.status == 200, "test-respondwith-response load should be successful");
  my_ok(xhr.responseText == "test-respondwith-response response body", "load should have response");
  finish();
});

fetch('synthesized-404.txt', function(xhr) {
  my_ok(xhr.status == 404, "load should 404");
  my_ok(xhr.responseText == "synthesized response body", "404 load should have synthesized response");
  finish();
});

fetch('synthesized-headers.txt', function(xhr) {
  my_ok(xhr.status == 200, "load should be successful");
  my_ok(xhr.getResponseHeader("X-Custom-Greeting") === "Hello", "custom header should be set");
  my_ok(xhr.responseText == "synthesized response body", "custom header load should have synthesized response");
  finish();
});

fetch('ignored.txt', function(xhr) {
  my_ok(xhr.status == 404, "load should be uninterrupted");
  finish();
});

fetch('rejected.txt', null, function(xhr) {
  my_ok(xhr.status == 0, "load should not complete");
  finish();
});

fetch('nonresponse.txt', null, function(xhr) {
  my_ok(xhr.status == 0, "load should not complete");
  finish();
});

fetch('nonresponse2.txt', null, function(xhr) {
  my_ok(xhr.status == 0, "load should not complete");
  finish();
});

fetch('headers.txt', function(xhr) {
  my_ok(xhr.status == 200, "load should be successful");
  my_ok(xhr.responseText == "1", "request header checks should have passed");
  finish();
}, null, [["X-Test1", "header1"], ["X-Test2", "header2"]]);

var expectedUncompressedResponse = "";
for (var i = 0; i < 10; ++i) {
  expectedUncompressedResponse += "hello";
}
expectedUncompressedResponse += "\n";

// ServiceWorker does not intercept, at which point the network request should
// be correctly decoded.
fetch('deliver-gzip.sjs', function(xhr) {
  my_ok(xhr.status == 200, "network gzip load should be successful");
  my_ok(xhr.responseText == expectedUncompressedResponse, "network gzip load should have synthesized response.");
  my_ok(xhr.getResponseHeader("Content-Encoding") == "gzip", "network Content-Encoding should be gzip.");
  my_ok(xhr.getResponseHeader("Content-Length") == "35", "network Content-Length should be of original gzipped file.");
  finish();
});

fetch('hello.gz', function(xhr) {
  my_ok(xhr.status == 200, "gzip load should be successful");
  my_ok(xhr.responseText == expectedUncompressedResponse, "gzip load should have synthesized response.");
  my_ok(xhr.getResponseHeader("Content-Encoding") == "gzip", "Content-Encoding should be gzip.");
  my_ok(xhr.getResponseHeader("Content-Length") == "35", "Content-Length should be of original gzipped file.");
  finish();
});

fetch('hello-after-extracting.gz', function(xhr) {
  my_ok(xhr.status == 200, "gzip load should be successful");
  my_ok(xhr.responseText == expectedUncompressedResponse, "gzip load should have synthesized response.");
  my_ok(xhr.getResponseHeader("Content-Encoding") == "gzip", "Content-Encoding should be gzip.");
  my_ok(xhr.getResponseHeader("Content-Length") == "35", "Content-Length should be of original gzipped file.");
  finish();
});
