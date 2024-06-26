/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

const modules = [
  "async.js",
  "bagheeraclient.js",
  "logmanager.js",
  "rest.js",
  "storageservice.js",
  "stringbundle.js",
  "tokenserverclient.js",
  "utils.js",
];

const test_modules = [
  "bagheeraserver.js",
  "logging.js",
  "storageserver.js",
];

function run_test() {
  for (let m in modules) {
    let resource = "resource://services-common/" + m;
    Components.utils.import(resource, {});
  }

  for (let m in test_modules) {
    let resource = "resource://testing-common/services/common/" + m;
    Components.utils.import(resource, {});
  }
}
