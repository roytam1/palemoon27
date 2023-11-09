/* -*- Mode: IDL; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * The origin of this IDL file is
 * http://w3c.github.io/web-animations/#the-keyframeeffect-interfaces
 *
 * Copyright © 2015 W3C® (MIT, ERCIM, Keio), All Rights Reserved. W3C
 * liability, trademark and document use rules apply.
 */

enum IterationCompositeOperation {
  "replace",
  "accumulate"
};

dictionary KeyframeEffectOptions : AnimationEffectTimingProperties {
  IterationCompositeOperation iterationComposite = "replace";
  CompositeOperation          composite = "replace";
  DOMString                   spacing = "distribute";
};

// For the constructor we use Element? for the first argument since we
// don't support Animatable for pseudo-elements yet.
[HeaderFile="mozilla/dom/KeyframeEffect.h",
 Func="nsDocument::IsWebAnimationsEnabled",
 Constructor(Element? target,
             object? frames,
             optional (unrestricted double or KeyframeEffectOptions) options)]
interface KeyframeEffectReadOnly : AnimationEffectReadOnly {
  readonly attribute Element?  target;
  readonly attribute IterationCompositeOperation iterationComposite;
  readonly attribute CompositeOperation          composite;
  readonly attribute DOMString                   spacing;

  // Not yet implemented:
  // KeyframeEffect             clone();

  // We use object instead of ComputedKeyframe so that we can put the
  // property-value pairs on the object.
  [Throws] sequence<object> getFrames();
};
