function testScript(script) {
  function workerTest() {
    return new Promise(function(resolve, reject) {
      var worker = new Worker("worker_wrapper.js");
      worker.onmessage = function(event) {
        if (event.data.type == 'finish') {
          resolve();
        } else if (event.data.type == 'status') {
          ok(event.data.status, "Worker fetch test: " + event.data.msg);
        }
      }
      worker.onerror = function(event) {
        reject("Worker error: " + event.message);
      };

      worker.postMessage({ "script": script });
    });
  }

  function windowTest() {
    return new Promise(function(resolve, reject) {
      var scriptEl = document.createElement("script");
      scriptEl.setAttribute("src", script);
      scriptEl.onload = function() {
        runTest().then(resolve, reject);
      };
      document.body.appendChild(scriptEl);
    });
  }

  SimpleTest.waitForExplicitFinish();
  // We have to run the window and worker tests sequentially since some tests
  // set and compare cookies and running in parallel can lead to conflicting
  // values.
  windowTest()
    .then(function() {
      return workerTest();
    })
    .catch(function(e) {
      ok(false, "Some test failed in " + script);
      info(e);
      info(e.message);
      return Promise.resolve();
    })
    .then(function() {
      SimpleTest.finish();
    });
}

// Utilities
// =========

// Helper that uses FileReader or FileReaderSync based on context and returns
// a Promise that resolves with the text or rejects with error.
function readAsText(blob) {
  if (typeof FileReader !== "undefined") {
    return new Promise(function(resolve, reject) {
      var fs = new FileReader();
      fs.onload = function() {
        resolve(fs.result);
      }
      fs.onerror = reject;
      fs.readAsText(blob);
    });
  } else {
    var fs = new FileReaderSync();
    return Promise.resolve(fs.readAsText(blob));
  }
}

function readAsArrayBuffer(blob) {
  if (typeof FileReader !== "undefined") {
    return new Promise(function(resolve, reject) {
      var fs = new FileReader();
      fs.onload = function() {
        resolve(fs.result);
      }
      fs.onerror = reject;
      fs.readAsArrayBuffer(blob);
    });
  } else {
    var fs = new FileReaderSync();
    return Promise.resolve(fs.readAsArrayBuffer(blob));
  }
}

