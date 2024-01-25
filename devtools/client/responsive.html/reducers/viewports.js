/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const { ADD_VIEWPORT } = require("../actions/index");

const INITIAL_VIEWPORTS = [];
const INITIAL_VIEWPORT = {
  width: 320,
  height: 480,
};

let reducers = {

  [ADD_VIEWPORT](viewports) {
    // For the moment, there can be at most one viewport.
    if (viewports.length === 1) {
      return viewports;
    }
    return [...viewports, INITIAL_VIEWPORT];
  },

};

module.exports = function(viewports = INITIAL_VIEWPORTS, action) {
  let reducer = reducers[action.type];
  if (!reducer) {
    return viewports;
  }
  return reducer(viewports, action);
};
