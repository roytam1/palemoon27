# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

// Simple gestures support
//
// As per bug #412486, web content must not be allowed to receive any
// simple gesture events.  Multi-touch gesture APIs are in their
// infancy and we do NOT want to be forced into supporting an API that
// will probably have to change in the future.  (The current Mac OS X
// API is undocumented and was reverse-engineered.)  Until support is
// implemented in the event dispatcher to keep these events as
// chrome-only, we must listen for the simple gesture events during
// the capturing phase and call stopPropagation on every event.

let gGestureSupport = {
  _currentRotation: 0,
  _lastRotateDelta: 0,
  _rotateMomentumThreshold: .75,

  /**
   * Add or remove mouse gesture event listeners
   *
   * @param aAddListener
   *        True to add/init listeners and false to remove/uninit
   */
  init: function GS_init(aAddListener) {
    // Bug 863514 - Make gesture support work in electrolysis
    if (gMultiProcessBrowser)
      return;

    const gestureEvents = ["SwipeGestureStart",
      "SwipeGestureUpdate", "SwipeGestureEnd", "SwipeGesture",
      "MagnifyGestureStart", "MagnifyGestureUpdate", "MagnifyGesture",
      "RotateGestureStart", "RotateGestureUpdate", "RotateGesture",
      "TapGesture", "PressTapGesture"];

    let addRemove = aAddListener ? window.addEventListener :
      window.removeEventListener;

    gestureEvents.forEach(function (event) addRemove("Moz" + event, this, true),
                          this);
  },

  /**
   * Dispatch events based on the type of mouse gesture event. For now, make
   * sure to stop propagation of every gesture event so that web content cannot
   * receive gesture events.
   *
   * @param aEvent
   *        The gesture event to handle
   */
  handleEvent: function GS_handleEvent(aEvent) {
    if (!Services.prefs.getBoolPref(
           "dom.debug.propagate_gesture_events_through_content")) {
      aEvent.stopPropagation();
    }

    // Create a preference object with some defaults
    let def = (aThreshold, aLatched) =>
      ({ threshold: aThreshold, latched: !!aLatched });

    switch (aEvent.type) {
      case "MozSwipeGestureStart":
        aEvent.preventDefault();
        this._setupSwipeGesture(aEvent);
        break;
      case "MozSwipeGestureUpdate":
        aEvent.preventDefault();
        this._doUpdate(aEvent);
        break;
      case "MozSwipeGestureEnd":
        aEvent.preventDefault();
        this._doEnd(aEvent);
        break;
      case "MozSwipeGesture":
        aEvent.preventDefault();
        this.onSwipe(aEvent);
        break;
      case "MozMagnifyGestureStart":
        aEvent.preventDefault();
#ifdef XP_WIN
        this._setupGesture(aEvent, "pinch", def(25, 0), "out", "in");
#else
        this._setupGesture(aEvent, "pinch", def(150, 1), "out", "in");
#endif
        break;
      case "MozRotateGestureStart":
        aEvent.preventDefault();
        this._setupGesture(aEvent, "twist", def(25, 0), "right", "left");
        break;
      case "MozMagnifyGestureUpdate":
      case "MozRotateGestureUpdate":
        aEvent.preventDefault();
        this._doUpdate(aEvent);
        break;
      case "MozTapGesture":
        aEvent.preventDefault();
        this._doAction(aEvent, ["tap"]);
        break;
      case "MozRotateGesture":
        aEvent.preventDefault();
        this._doAction(aEvent, ["twist", "end"]);
        break;
      /* case "MozPressTapGesture":
        break; */
    }
  },

  /**
   * Called at the start of "pinch" and "twist" gestures to setup all of the
   * information needed to process the gesture
   *
   * @param aEvent
   *        The continual motion start event to handle
   * @param aGesture
   *        Name of the gesture to handle
   * @param aPref
   *        Preference object with the names of preferences and defaults
   * @param aInc
   *        Command to trigger for increasing motion (without gesture name)
   * @param aDec
   *        Command to trigger for decreasing motion (without gesture name)
   */
  _setupGesture: function GS__setupGesture(aEvent, aGesture, aPref, aInc, aDec) {
    // Try to load user-set values from preferences
    for (let [pref, def] in Iterator(aPref))
      aPref[pref] = this._getPref(aGesture + "." + pref, def);

    // Keep track of the total deltas and latching behavior
    let offset = 0;
    let latchDir = aEvent.delta > 0 ? 1 : -1;
    let isLatched = false;

    // Create the update function here to capture closure state
    this._doUpdate = function GS__doUpdate(aEvent) {
      // Update the offset with new event data
      offset += aEvent.delta;

      // Check if the cumulative deltas exceed the threshold
      if (Math.abs(offset) > aPref["threshold"]) {
        // Trigger the action if we don't care about latching; otherwise, make
        // sure either we're not latched and going the same direction of the
        // initial motion; or we're latched and going the opposite way
        let sameDir = (latchDir ^ offset) >= 0;
        if (!aPref["latched"] || (isLatched ^ sameDir)) {
          this._doAction(aEvent, [aGesture, offset > 0 ? aInc : aDec]);

          // We must be getting latched or leaving it, so just toggle
          isLatched = !isLatched;
        }

        // Reset motion counter to prepare for more of the same gesture
        offset = 0;
      }
    };

    // The start event also contains deltas, so handle an update right away
    this._doUpdate(aEvent);
  },

  /**
   * Checks whether a swipe gesture event can navigate the browser history or
   * not.
   *
   * @param aEvent
   *        The swipe gesture event.
   * @return true if the swipe event may navigate the history, false othwerwise.
   */
  _swipeNavigatesHistory: function GS__swipeNavigatesHistory(aEvent) {
    return this._getCommand(aEvent, ["swipe", "left"])
              == "Browser:BackOrBackDuplicate" &&
           this._getCommand(aEvent, ["swipe", "right"])
              == "Browser:ForwardOrForwardDuplicate";
  },

  /**
   * Sets up the history swipe animations for a swipe gesture event, if enabled.
   *
   * @param aEvent
   *        The swipe gesture start event.
   */
  _setupSwipeGesture: function GS__setupSwipeGesture(aEvent) {
    if (!this._swipeNavigatesHistory(aEvent))
      return;

    let canGoBack = gHistorySwipeAnimation.canGoBack();
    let canGoForward = gHistorySwipeAnimation.canGoForward();
    let isLTR = gHistorySwipeAnimation.isLTR;

    if (canGoBack)
      aEvent.allowedDirections |= isLTR ? aEvent.DIRECTION_LEFT :
                                          aEvent.DIRECTION_RIGHT;
    if (canGoForward)
      aEvent.allowedDirections |= isLTR ? aEvent.DIRECTION_RIGHT :
                                          aEvent.DIRECTION_LEFT;

    gHistorySwipeAnimation.startAnimation();

    this._doUpdate = function GS__doUpdate(aEvent) {
      gHistorySwipeAnimation.updateAnimation(aEvent.delta);
    };

    this._doEnd = function GS__doEnd(aEvent) {
      gHistorySwipeAnimation.swipeEndEventReceived();

      this._doUpdate = function (aEvent) {};
      this._doEnd = function (aEvent) {};
    }
  },

  /**
   * Generator producing the powerset of the input array where the first result
   * is the complete set and the last result (before StopIteration) is empty.
   *
   * @param aArray
   *        Source array containing any number of elements
   * @yield Array that is a subset of the input array from full set to empty
   */
  _power: function* GS__power(aArray) {
    // Create a bitmask based on the length of the array
    let num = 1 << aArray.length;
    while (--num >= 0) {
      // Only select array elements where the current bit is set
      yield aArray.reduce(function (aPrev, aCurr, aIndex) {
        if (num & 1 << aIndex)
          aPrev.push(aCurr);
        return aPrev;
      }, []);
    }
  },

  /**
   * Determine what action to do for the gesture based on which keys are
   * pressed and which commands are set, and execute the command.
   *
   * @param aEvent
   *        The original gesture event to convert into a fake click event
   * @param aGesture
   *        Array of gesture name parts (to be joined by periods)
   * @return Name of the executed command. Returns null if no command is
   *         found.
   */
  _doAction: function GS__doAction(aEvent, aGesture) {
    let command = this._getCommand(aEvent, aGesture);
    return command && this._doCommand(aEvent, command);
  },

  /**
   * Determine what action to do for the gesture based on which keys are
   * pressed and which commands are set
   *
   * @param aEvent
   *        The original gesture event to convert into a fake click event
   * @param aGesture
   *        Array of gesture name parts (to be joined by periods)
   */
  _getCommand: function GS__getCommand(aEvent, aGesture) {
    // Create an array of pressed keys in a fixed order so that a command for
    // "meta" is preferred over "ctrl" when both buttons are pressed (and a
    // command for both don't exist)
    let keyCombos = [];
    ["shift", "alt", "ctrl", "meta"].forEach(function (key) {
      if (aEvent[key + "Key"])
        keyCombos.push(key);
    });

    // Try each combination of key presses in decreasing order for commands
    for (let subCombo of this._power(keyCombos)) {
      // Convert a gesture and pressed keys into the corresponding command
      // action where the preference has the gesture before "shift" before
      // "alt" before "ctrl" before "meta" all separated by periods
      let command;
      try {
        command = this._getPref(aGesture.concat(subCombo).join("."));
      } catch (e) {}

      if (command)
        return command;
    }
    return null;
  },

  /**
   * Execute the specified command.
   *
   * @param aEvent
   *        The original gesture event to convert into a fake click event
   * @param aCommand
   *        Name of the command found for the event's keys and gesture.
   */
  _doCommand: function GS__doCommand(aEvent, aCommand) {
    let node = document.getElementById(aCommand);
    if (node) {
      if (node.getAttribute("disabled") != "true") {
        let cmdEvent = document.createEvent("xulcommandevent");
        cmdEvent.initCommandEvent("command", true, true, window, 0,
                                  aEvent.ctrlKey, aEvent.altKey,
                                  aEvent.shiftKey, aEvent.metaKey, aEvent);
        node.dispatchEvent(cmdEvent);
      }

    }
    else {
      goDoCommand(aCommand);
    }
  },

  /**
   * Handle continual motion events.  This function will be set by
   * _setupGesture or _setupSwipe.
   *
   * @param aEvent
   *        The continual motion update event to handle
   */
  _doUpdate: function(aEvent) {},

  /**
   * Handle gesture end events.  This function will be set by _setupSwipe.
   *
   * @param aEvent
   *        The gesture end event to handle
   */
  _doEnd: function(aEvent) {},

  /**
   * Convert the swipe gesture into a browser action based on the direction.
   *
   * @param aEvent
   *        The swipe event to handle
   */
  onSwipe: function GS_onSwipe(aEvent) {
    // Figure out which one (and only one) direction was triggered
    for (let dir of ["UP", "RIGHT", "DOWN", "LEFT"]) {
      if (aEvent.direction == aEvent["DIRECTION_" + dir]) {
        this._coordinateSwipeEventWithAnimation(aEvent, dir);
        break;
      }
    }
  },

  /**
   * Process a swipe event based on the given direction.
   *
   * @param aEvent
   *        The swipe event to handle
   * @param aDir
   *        The direction for the swipe event
   */
  processSwipeEvent: function GS_processSwipeEvent(aEvent, aDir) {
    this._doAction(aEvent, ["swipe", aDir.toLowerCase()]);
  },

  /**
   * Coordinates the swipe event with the swipe animation, if any.
   * If an animation is currently running, the swipe event will be
   * processed once the animation stops. This will guarantee a fluid
   * motion of the animation.
   *
   * @param aEvent
   *        The swipe event to handle
   * @param aDir
   *        The direction for the swipe event
   */
  _coordinateSwipeEventWithAnimation:
  function GS__coordinateSwipeEventWithAnimation(aEvent, aDir) {
    if ((gHistorySwipeAnimation.isAnimationRunning()) &&
        (aDir == "RIGHT" || aDir == "LEFT")) {
      gHistorySwipeAnimation.processSwipeEvent(aEvent, aDir);
    }
    else {
      this.processSwipeEvent(aEvent, aDir);
    }
  },

  /**
   * Get a gesture preference or use a default if it doesn't exist
   *
   * @param aPref
   *        Name of the preference to load under the gesture branch
   * @param aDef
   *        Default value if the preference doesn't exist
   */
  _getPref: function GS__getPref(aPref, aDef) {
    // Preferences branch under which all gestures preferences are stored
    const branch = "browser.gesture.";

    try {
      // Determine what type of data to load based on default value's type
      let type = typeof aDef;
      let getFunc = "get" + (type == "boolean" ? "Bool" :
                             type == "number" ? "Int" : "Char") + "Pref";
      return gPrefService[getFunc](branch + aPref);
    }
    catch (e) {
      return aDef;
    }
  },

  /**
   * Perform rotation for ImageDocuments
   *
   * @param aEvent
   *        The MozRotateGestureUpdate event triggering this call
   */
  rotate: function(aEvent) {
    if (!(content.document instanceof ImageDocument))
      return;

    let contentElement = content.document.body.firstElementChild;
    if (!contentElement)
      return;
    // If we're currently snapping, cancel that snap
    if (contentElement.classList.contains("completeRotation"))
      this._clearCompleteRotation();

    this.rotation = Math.round(this.rotation + aEvent.delta);
    contentElement.style.transform = "rotate(" + this.rotation + "deg)";
    this._lastRotateDelta = aEvent.delta;
  },

  /**
   * Perform a rotation end for ImageDocuments
   */
  rotateEnd: function() {
    if (!(content.document instanceof ImageDocument))
      return;

    let contentElement = content.document.body.firstElementChild;
    if (!contentElement)
      return;

    let transitionRotation = 0;

    // The reason that 360 is allowed here is because when rotating between
    // 315 and 360, setting rotate(0deg) will cause it to rotate the wrong
    // direction around--spinning wildly.
    if (this.rotation <= 45)
      transitionRotation = 0;
    else if (this.rotation > 45 && this.rotation <= 135)
      transitionRotation = 90;
    else if (this.rotation > 135 && this.rotation <= 225)
      transitionRotation = 180;
    else if (this.rotation > 225 && this.rotation <= 315)
      transitionRotation = 270;
    else
      transitionRotation = 360;

    // If we're going fast enough, and we didn't already snap ahead of rotation,
    // then snap ahead of rotation to simulate momentum
    if (this._lastRotateDelta > this._rotateMomentumThreshold &&
        this.rotation > transitionRotation)
      transitionRotation += 90;
    else if (this._lastRotateDelta < -1 * this._rotateMomentumThreshold &&
             this.rotation < transitionRotation)
      transitionRotation -= 90;

    // Only add the completeRotation class if it is is necessary
    if (transitionRotation != this.rotation) {
      contentElement.classList.add("completeRotation");
      contentElement.addEventListener("transitionend", this._clearCompleteRotation);
    }

    contentElement.style.transform = "rotate(" + transitionRotation + "deg)";
    this.rotation = transitionRotation;
  },

  /**
   * Gets the current rotation for the ImageDocument
   */
  get rotation() {
    return this._currentRotation;
  },

  /**
   * Sets the current rotation for the ImageDocument
   *
   * @param aVal
   *        The new value to take.  Can be any value, but it will be bounded to
   *        0 inclusive to 360 exclusive.
   */
  set rotation(aVal) {
    this._currentRotation = aVal % 360;
    if (this._currentRotation < 0)
      this._currentRotation += 360;
    return this._currentRotation;
  },

  /**
   * When the location/tab changes, need to reload the current rotation for the
   * image
   */
  restoreRotationState: function() {
    // Bug 863514 - Make gesture support work in electrolysis
    if (gMultiProcessBrowser)
      return;

    if (!(content.document instanceof ImageDocument))
      return;

    let contentElement = content.document.body.firstElementChild;
    let transformValue = content.window.getComputedStyle(contentElement, null)
                                       .transform;

    if (transformValue == "none") {
      this.rotation = 0;
      return;
    }

    // transformValue is a rotation matrix--split it and do mathemagic to
    // obtain the real rotation value
    transformValue = transformValue.split("(")[1]
                                   .split(")")[0]
                                   .split(",");
    this.rotation = Math.round(Math.atan2(transformValue[1], transformValue[0]) *
                               (180 / Math.PI));
  },

  /**
   * Removes the transition rule by removing the completeRotation class
   */
  _clearCompleteRotation: function() {
    let contentElement = content.document &&
                         content.document instanceof ImageDocument &&
                         content.document.body &&
                         content.document.body.firstElementChild;
    if (!contentElement)
      return;
    contentElement.classList.remove("completeRotation");
    contentElement.removeEventListener("transitionend", this._clearCompleteRotation);
  },
};

// History Swipe Animation Support (bug 678392)
let gHistorySwipeAnimation = {

  active: false,
  isLTR: false,

  /**
   * Initializes the support for history swipe animations, if it is supported
   * by the platform/configuration.
   */
  init: function HSA_init() {
    if (!this._isSupported())
      return;

    this.active = false;
    this.isLTR = document.documentElement.mozMatchesSelector(
                                            ":-moz-locale-dir(ltr)");
    this._trackedSnapshots = [];
    this._historyIndex = -1;
    this._boxWidth = -1;
    this._maxSnapshots = this._getMaxSnapshots();
    this._lastSwipeDir = "";

    // We only want to activate history swipe animations if we store snapshots.
    // If we don't store any, we handle horizontal swipes without animations.
    if (this._maxSnapshots > 0) {
      this.active = true;
      gBrowser.addEventListener("pagehide", this, false);
      gBrowser.addEventListener("pageshow", this, false);
      gBrowser.addEventListener("popstate", this, false);
      gBrowser.tabContainer.addEventListener("TabClose", this, false);
    }
  },

  /**
   * Uninitializes the support for history swipe animations.
   */
  uninit: function HSA_uninit() {
    gBrowser.removeEventListener("pagehide", this, false);
    gBrowser.removeEventListener("pageshow", this, false);
    gBrowser.removeEventListener("popstate", this, false);
    gBrowser.tabContainer.removeEventListener("TabClose", this, false);

    this.active = false;
    this.isLTR = false;
  },

  /**
   * Starts the swipe animation and handles fast swiping (i.e. a swipe animation
   * is already in progress when a new one is initiated).
   */
  startAnimation: function HSA_startAnimation() {
    if (this.isAnimationRunning()) {
      gBrowser.stop();
      this._lastSwipeDir = "RELOAD"; // just ensure that != ""
      this._canGoBack = this.canGoBack();
      this._canGoForward = this.canGoForward();
      this._handleFastSwiping();
    }
    else {
      this._historyIndex = gBrowser.webNavigation.sessionHistory.index;
      this._canGoBack = this.canGoBack();
      this._canGoForward = this.canGoForward();
      if (this.active) {
        this._takeSnapshot();
        this._installPrevAndNextSnapshots();
        this._addBoxes();
        this._lastSwipeDir = "";
      }
    }
    this.updateAnimation(0);
  },

  /**
   * Stops the swipe animation.
   */
  stopAnimation: function HSA_stopAnimation() {
    gHistorySwipeAnimation._removeBoxes();
  },

  /**
   * Updates the animation between two pages in history.
   *
   * @param aVal
   *        A floating point value that represents the progress of the
   *        swipe gesture.
   */
  updateAnimation: function HSA_updateAnimation(aVal) {
    if (!this.isAnimationRunning())
      return;

    if ((aVal >= 0 && this.isLTR) ||
        (aVal <= 0 && !this.isLTR)) {
      if (aVal > 1)
        aVal = 1; // Cap value to avoid sliding the page further than allowed.

      if (this._canGoBack)
        this._prevBox.collapsed = false;
      else
        this._prevBox.collapsed = true;

      // The current page is pushed to the right (LTR) or left (RTL),
      // the intention is to go back.
      // If there is a page to go back to, it should show in the background.
      this._positionBox(this._curBox, aVal);

      // The forward page should be pushed offscreen all the way to the right.
      this._positionBox(this._nextBox, 1);
    }
    else {
      if (aVal < -1)
        aVal = -1; // Cap value to avoid sliding the page further than allowed.

      // The intention is to go forward. If there is a page to go forward to,
      // it should slide in from the right (LTR) or left (RTL).
      // Otherwise, the current page should slide to the left (LTR) or
      // right (RTL) and the backdrop should appear in the background.
      // For the backdrop to be visible in that case, the previous page needs
      // to be hidden (if it exists).
      if (this._canGoForward) {
        let offset = this.isLTR ? 1 : -1;
        this._positionBox(this._curBox, 0);
        this._positionBox(this._nextBox, offset + aVal); // aVal is negative
      }
      else {
        this._prevBox.collapsed = true;
        this._positionBox(this._curBox, aVal);
      }
    }
  },

  /**
   * Event handler for events relevant to the history swipe animation.
   *
   * @param aEvent
   *        An event to process.
   */
  handleEvent: function HSA_handleEvent(aEvent) {
    switch (aEvent.type) {
      case "TabClose":
        let browser = gBrowser.getBrowserForTab(aEvent.target);
        this._removeTrackedSnapshot(-1, browser);
        break;
      case "pageshow":
      case "popstate":
        if (this.isAnimationRunning()) {
          if (aEvent.target != gBrowser.selectedBrowser.contentDocument)
            break;
          this.stopAnimation();
        }
        this._historyIndex = gBrowser.webNavigation.sessionHistory.index;
        break;
      case "pagehide":
        if (aEvent.target == gBrowser.selectedBrowser.contentDocument) {
          // Take a snapshot of a page whenever it's about to be navigated away
          // from.
          this._takeSnapshot();
        }
        break;
    }
  },

  /**
   * Checks whether the history swipe animation is currently running or not.
   *
   * @return true if the animation is currently running, false otherwise.
   */
  isAnimationRunning: function HSA_isAnimationRunning() {
    return !!this._container;
  },

  /**
   * Process a swipe event based on the given direction.
   *
   * @param aEvent
   *        The swipe event to handle
   * @param aDir
   *        The direction for the swipe event
   */
  processSwipeEvent: function HSA_processSwipeEvent(aEvent, aDir) {
    if (aDir == "RIGHT")
      this._historyIndex += this.isLTR ? 1 : -1;
    else if (aDir == "LEFT")
      this._historyIndex += this.isLTR ? -1 : 1;
    else
      return;
    this._lastSwipeDir = aDir;
  },

  /**
   * Checks if there is a page in the browser history to go back to.
   *
   * @return true if there is a previous page in history, false otherwise.
   */
  canGoBack: function HSA_canGoBack() {
    if (this.isAnimationRunning())
      return this._doesIndexExistInHistory(this._historyIndex - 1);
    return gBrowser.webNavigation.canGoBack;
  },

  /**
   * Checks if there is a page in the browser history to go forward to.
   *
   * @return true if there is a next page in history, false otherwise.
   */
  canGoForward: function HSA_canGoForward() {
    if (this.isAnimationRunning())
      return this._doesIndexExistInHistory(this._historyIndex + 1);
    return gBrowser.webNavigation.canGoForward;
  },

  /**
   * Used to notify the history swipe animation that the OS sent a swipe end
   * event and that we should navigate to the page that the user swiped to, if
   * any. This will also result in the animation overlay to be torn down.
   */
  swipeEndEventReceived: function HSA_swipeEndEventReceived() {
    if (this._lastSwipeDir != "")
      this._navigateToHistoryIndex();
    else
      this.stopAnimation();
  },

  /**
   * Checks whether a particular index exists in the browser history or not.
   *
   * @param aIndex
   *        The index to check for availability for in the history.
   * @return true if the index exists in the browser history, false otherwise.
   */
  _doesIndexExistInHistory: function HSA__doesIndexExistInHistory(aIndex) {
    try {
      gBrowser.webNavigation.sessionHistory.getEntryAtIndex(aIndex, false);
    }
    catch(ex) {
      return false;
    }
    return true;
  },

  /**
   * Navigates to the index in history that is currently being tracked by
   * |this|.
   */
  _navigateToHistoryIndex: function HSA__navigateToHistoryIndex() {
    if (this._doesIndexExistInHistory(this._historyIndex)) {
      gBrowser.webNavigation.gotoIndex(this._historyIndex);
    }
  },

  /**
   * Checks to see if history swipe animations are supported by this
   * platform/configuration.
   *
   * return true if supported, false otherwise.
   */
  _isSupported: function HSA__isSupported() {
    return window.matchMedia("(-moz-swipe-animation-enabled)").matches;
  },

  /**
   * Handle fast swiping (i.e. a swipe animation is already in
   * progress when a new one is initiated). This will swap out the snapshots
   * used in the previous animation with the appropriate new ones.
   */
  _handleFastSwiping: function HSA__handleFastSwiping() {
    this._installCurrentPageSnapshot(null);
    this._installPrevAndNextSnapshots();
  },

  /**
   * Adds the boxes that contain the snapshots used during the swipe animation.
   */
  _addBoxes: function HSA__addBoxes() {
    let browserStack =
      document.getAnonymousElementByAttribute(gBrowser.getNotificationBox(),
                                              "class", "browserStack");
    this._container = this._createElement("historySwipeAnimationContainer",
                                          "stack");
    browserStack.appendChild(this._container);

    this._prevBox = this._createElement("historySwipeAnimationPreviousPage",
                                        "box");
    this._container.appendChild(this._prevBox);

    this._curBox = this._createElement("historySwipeAnimationCurrentPage",
                                       "box");
    this._container.appendChild(this._curBox);

    this._nextBox = this._createElement("historySwipeAnimationNextPage",
                                        "box");
    this._container.appendChild(this._nextBox);

    this._boxWidth = this._curBox.getBoundingClientRect().width; // cache width
  },

  /**
   * Removes the boxes.
   */
  _removeBoxes: function HSA__removeBoxes() {
    this._curBox = null;
    this._prevBox = null;
    this._nextBox = null;
    if (this._container)
      this._container.parentNode.removeChild(this._container);
    this._container = null;
    this._boxWidth = -1;
  },

  /**
   * Creates an element with a given identifier and tag name.
   *
   * @param aID
   *        An identifier to create the element with.
   * @param aTagName
   *        The name of the tag to create the element for.
   * @return the newly created element.
   */
  _createElement: function HSA__createElement(aID, aTagName) {
    let XULNS = "http://www.mozilla.org/keymaster/gatekeeper/there.is.only.xul";
    let element = document.createElementNS(XULNS, aTagName);
    element.id = aID;
    return element;
  },

  /**
   * Moves a given box to a given X coordinate position.
   *
   * @param aBox
   *        The box element to position.
   * @param aPosition
   *        The position (in X coordinates) to move the box element to.
   */
  _positionBox: function HSA__positionBox(aBox, aPosition) {
    aBox.style.transform = "translateX(" + this._boxWidth * aPosition + "px)";
  },

  /**
   * Takes a snapshot of the page the browser is currently on.
   */
  _takeSnapshot: function HSA__takeSnapshot() {
    if ((this._maxSnapshots < 1) ||
        (gBrowser.webNavigation.sessionHistory.index < 0))
      return;

    let browser = gBrowser.selectedBrowser;
    let r = browser.getBoundingClientRect();
    let canvas = document.createElementNS("http://www.w3.org/1999/xhtml",
                                          "canvas");
    canvas.mozOpaque = true;
    canvas.width = r.width;
    canvas.height = r.height;
    let ctx = canvas.getContext("2d");
    let zoom = browser.markupDocumentViewer.fullZoom;
    ctx.scale(zoom, zoom);
    ctx.drawWindow(browser.contentWindow, 0, 0, r.width, r.height, "white",
                   ctx.DRAWWINDOW_DO_NOT_FLUSH | ctx.DRAWWINDOW_DRAW_VIEW |
                   ctx.DRAWWINDOW_ASYNC_DECODE_IMAGES |
                   ctx.DRAWWINDOW_USE_WIDGET_LAYERS);

    this._installCurrentPageSnapshot(canvas);
    this._assignSnapshotToCurrentBrowser(canvas);
  },

  /**
   * Retrieves the maximum number of snapshots that should be kept in memory.
   * This limit is a global limit and is valid across all open tabs.
   */
  _getMaxSnapshots: function HSA__getMaxSnapshots() {
    return gPrefService.getIntPref("browser.snapshots.limit");
  },

  /**
   * Adds a snapshot to the list and initiates the compression of said snapshot.
   * Once the compression is completed, it will replace the uncompressed
   * snapshot in the list.
   *
   * @param aCanvas
   *        The snapshot to add to the list and compress.
   */
  _assignSnapshotToCurrentBrowser:
  function HSA__assignSnapshotToCurrentBrowser(aCanvas) {
    let browser = gBrowser.selectedBrowser;
    let currIndex = browser.webNavigation.sessionHistory.index;

    this._removeTrackedSnapshot(currIndex, browser);
    this._addSnapshotRefToArray(currIndex, browser);

    if (!("snapshots" in browser))
      browser.snapshots = [];
    let snapshots = browser.snapshots;
    // Temporarily store the canvas as the compressed snapshot.
    // This avoids a blank page if the user swipes quickly
    // between pages before the compression could complete.
    snapshots[currIndex] = aCanvas;

    // Kick off snapshot compression.
    aCanvas.toBlob(function(aBlob) {
        snapshots[currIndex] = aBlob;
      }, "image/png"
    );
  },

  /**
   * Removes a snapshot identified by the browser and index in the array of
   * snapshots for that browser, if present. If no snapshot could be identified
   * the method simply returns without taking any action. If aIndex is negative,
   * all snapshots for a particular browser will be removed.
   *
   * @param aIndex
   *        The index in history of the new snapshot, or negative value if all
   *        snapshots for a browser should be removed.
   * @param aBrowser
   *        The browser the new snapshot was taken in.
   */
  _removeTrackedSnapshot: function HSA__removeTrackedSnapshot(aIndex, aBrowser) {
    let arr = this._trackedSnapshots;
    let requiresExactIndexMatch = aIndex >= 0;
    for (let i = 0; i < arr.length; i++) {
      if ((arr[i].browser == aBrowser) &&
          (aIndex < 0 || aIndex == arr[i].index)) {
        delete aBrowser.snapshots[arr[i].index];
        arr.splice(i, 1);
        if (requiresExactIndexMatch)
          return; // Found and removed the only element.
        i--; // Make sure to revisit the index that we just removed an
             // element at.
      }
    }
  },

  /**
   * Adds a new snapshot reference for a given index and browser to the array
   * of references to tracked snapshots.
   *
   * @param aIndex
   *        The index in history of the new snapshot.
   * @param aBrowser
   *        The browser the new snapshot was taken in.
   */
  _addSnapshotRefToArray:
  function HSA__addSnapshotRefToArray(aIndex, aBrowser) {
    let id = { index: aIndex,
               browser: aBrowser };
    let arr = this._trackedSnapshots;
    arr.unshift(id);

    while (arr.length > this._maxSnapshots) {
      let lastElem = arr[arr.length - 1];
      delete lastElem.browser.snapshots[lastElem.index];
      arr.splice(-1, 1);
    }
  },

  /**
   * Converts a compressed blob to an Image object. In some situations
   * (especially during fast swiping) aBlob may still be a canvas, not a
   * compressed blob. In this case, we simply return the canvas.
   *
   * @param aBlob
   *        The compressed blob to convert, or a canvas if a blob compression
   *        couldn't complete before this method was called.
   * @return A new Image object representing the converted blob.
   */
  _convertToImg: function HSA__convertToImg(aBlob) {
    if (!aBlob)
      return null;

    // Return aBlob if it's still a canvas and not a compressed blob yet.
    if (aBlob instanceof HTMLCanvasElement)
      return aBlob;

    let img = new Image();
    let url = URL.createObjectURL(aBlob);
    img.onload = function() {
      URL.revokeObjectURL(url);
    };
    img.src = url;
    return img;
  },

  /**
   * Sets the snapshot of the current page to the snapshot passed as parameter,
   * or to the one previously stored for the current index in history if the
   * parameter is null.
   *
   * @param aCanvas
   *        The snapshot to set the current page to. If this parameter is null,
   *        the previously stored snapshot for this index (if any) will be used.
   */
  _installCurrentPageSnapshot:
  function HSA__installCurrentPageSnapshot(aCanvas) {
    let currSnapshot = aCanvas;
    if (!currSnapshot) {
      let snapshots = gBrowser.selectedBrowser.snapshots || {};
      let currIndex = this._historyIndex;
      if (currIndex in snapshots)
        currSnapshot = this._convertToImg(snapshots[currIndex]);
    }
    document.mozSetImageElement("historySwipeAnimationCurrentPageSnapshot",
                                  currSnapshot);
  },

  /**
   * Sets the snapshots of the previous and next pages to the snapshots
   * previously stored for their respective indeces.
   */
  _installPrevAndNextSnapshots:
  function HSA__installPrevAndNextSnapshots() {
    let snapshots = gBrowser.selectedBrowser.snapshots || [];
    let currIndex = this._historyIndex;
    let prevIndex = currIndex - 1;
    let prevSnapshot = null;
    if (prevIndex in snapshots)
      prevSnapshot = this._convertToImg(snapshots[prevIndex]);
    document.mozSetImageElement("historySwipeAnimationPreviousPageSnapshot",
                                prevSnapshot);

    let nextIndex = currIndex + 1;
    let nextSnapshot = null;
    if (nextIndex in snapshots)
      nextSnapshot = this._convertToImg(snapshots[nextIndex]);
    document.mozSetImageElement("historySwipeAnimationNextPageSnapshot",
                                nextSnapshot);
  },
};
