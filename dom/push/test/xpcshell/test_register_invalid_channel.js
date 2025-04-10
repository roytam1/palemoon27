/* Any copyright is dedicated to the Public Domain.
   http://creativecommons.org/publicdomain/zero/1.0/ */

'use strict';

const {PushDB, PushService, PushServiceWebSocket} = serviceExports;

const userAgentID = '52b2b04c-b6cc-42c6-abdf-bef9cbdbea00';
const channelID = 'cafed00d';

function run_test() {
  do_get_profile();
  setPrefs();
  run_next_test();
}

add_task(function* test_register_invalid_channel() {
  let db = PushServiceWebSocket.newPushDB();
  do_register_cleanup(() => {return db.drop().then(_ => db.close());});

  PushServiceWebSocket._generateID = () => channelID;
  PushService.init({
    serverURI: "wss://push.example.org/",
    db,
    makeWebSocket(uri) {
      return new MockWebSocket(uri, {
        onHello(request) {
          this.serverSendMsg(JSON.stringify({
            messageType: 'hello',
            uaid: userAgentID,
            status: 200
          }));
        },
        onRegister(request) {
          this.serverSendMsg(JSON.stringify({
            messageType: 'register',
            status: 403,
            channelID,
            error: 'Invalid channel ID'
          }));
        }
      });
    }
  });

  yield rejects(
    PushService.register({
      scope: 'https://example.com/invalid-channel',
      originAttributes: ChromeUtils.originAttributesToSuffix(
        { appId: Ci.nsIScriptSecurityManager.NO_APP_ID, inBrowser: false }),
    }),
    'Expected error for invalid channel ID'
  );

  let record = yield db.getByKeyID(channelID);
  ok(!record, 'Should not store records for error responses');
});
