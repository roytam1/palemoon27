/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
/* import-globals-from pippki.js */
"use strict";

function onLoad()
{
  let protectedAuthThread =
    window.arguments[0].QueryInterface(Components.interfaces.nsIProtectedAuthThread);

    if (!protectedAuthThread) 
    {
        window.close();
        return;
    }

    try
    {
        var tokenName = protectedAuthThread.getTokenName();

        var tag = document.getElementById("tokenName");
        tag.setAttribute("value",tokenName);

        window.setCursor("wait");
  
        var obs = {
          observe : function protectedAuthListenerObserve(subject, topic, data) {
            if (topic == "operation-completed")
              window.close();
          }
        };
        
        protectedAuthThread.login(obs);

    } catch (exception)
    {
        window.close();
        return;
    }
}

function onClose()
{
  window.setCursor("auto");
}
