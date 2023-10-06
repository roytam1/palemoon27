/* vim: set ft=javascript ts=2 et sw=2 tw=80: */
/* Any copyright is dedicated to the Public Domain.
 http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

// Computed view color cycling test.

const TEST_URI = `
  <style type="text/css">
    .matches {
      color: #F00;
    }
  </style>
  <span id="matches" class="matches">Some styled text</span>
`;

add_task(function*() {
  yield addTab("data:text/html;charset=utf-8," + encodeURIComponent(TEST_URI));
  let {inspector, view} = yield openComputedView();
  yield selectNode("#matches", inspector);

  info("Checking the property itself");
  let container = getComputedViewPropertyView(view, "color").valueNode;
  checkColorCycling(container, view);

  info("Checking matched selectors");
  container = yield getComputedViewMatchedRules(view, "color");
  checkColorCycling(container, view);
});

function checkColorCycling(container, view) {
  let swatch = container.querySelector(".computedview-colorswatch");
  let valueNode = container.querySelector(".computedview-color");
  let win = view.styleWindow;

  // "Authored" (default; currently the computed value)
  is(valueNode.textContent, "rgb(255, 0, 0)",
                            "Color displayed as an RGB value.");

  // Hex
  EventUtils.synthesizeMouseAtCenter(swatch,
                                     {type: "mousedown", shiftKey: true}, win);
  is(valueNode.textContent, "#F00", "Color displayed as a hex value.");

  // HSL
  EventUtils.synthesizeMouseAtCenter(swatch,
                                     {type: "mousedown", shiftKey: true}, win);
  is(valueNode.textContent, "hsl(0, 100%, 50%)",
                            "Color displayed as an HSL value.");

  // RGB
  EventUtils.synthesizeMouseAtCenter(swatch,
                                     {type: "mousedown", shiftKey: true}, win);
  is(valueNode.textContent, "rgb(255, 0, 0)",
                            "Color displayed as an RGB value.");

  // Color name
  EventUtils.synthesizeMouseAtCenter(swatch,
                                     {type: "mousedown", shiftKey: true}, win);
  is(valueNode.textContent, "red",
                            "Color displayed as a color name.");

  // Back to "Authored"
  EventUtils.synthesizeMouseAtCenter(swatch,
                                     {type: "mousedown", shiftKey: true}, win);
  is(valueNode.textContent, "rgb(255, 0, 0)",
                            "Color displayed as an RGB value.");
}
