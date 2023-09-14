/** @jsx React.DOM */

/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

/*jshint newcap:false*/
/*global loop:true, React */

var loop = loop || {};
loop.panel = (function(_, mozL10n) {
  "use strict";

  var sharedViews = loop.shared.views;
  var sharedModels = loop.shared.models;
  var sharedMixins = loop.shared.mixins;
  var sharedActions = loop.shared.actions;
  var sharedUtils = loop.shared.utils;
  var Button = sharedViews.Button;
  var ButtonGroup = sharedViews.ButtonGroup;
  var ContactsList = loop.contacts.ContactsList;
  var ContactDetailsForm = loop.contacts.ContactDetailsForm;

  var TabView = React.createClass({displayName: "TabView",
    propTypes: {
      buttonsHidden: React.PropTypes.array,
      // The selectedTab prop is used by the UI showcase.
      selectedTab: React.PropTypes.string,
      mozLoop: React.PropTypes.object
    },

    getDefaultProps: function() {
      return {
        buttonsHidden: []
      };
    },

    shouldComponentUpdate: function(nextProps, nextState) {
      var tabChange = this.state.selectedTab !== nextState.selectedTab;
      if (tabChange) {
        this.props.mozLoop.notifyUITour("Loop:PanelTabChanged", nextState.selectedTab);
      }

      if (!tabChange && nextProps.buttonsHidden) {
        if (nextProps.buttonsHidden.length !== this.props.buttonsHidden.length) {
          tabChange = true;
        } else {
          for (var i = 0, l = nextProps.buttonsHidden.length; i < l && !tabChange; ++i) {
            if (this.props.buttonsHidden.indexOf(nextProps.buttonsHidden[i]) === -1) {
              tabChange = true;
            }
          }
        }
      }
      return tabChange;
    },

    getInitialState: function() {
      // XXX Work around props.selectedTab being undefined initially.
      // When we don't need to rely on the pref, this can move back to
      // getDefaultProps (bug 1100258).
      return {
        selectedTab: this.props.selectedTab || "rooms"
      };
    },

    handleSelectTab: function(event) {
      var tabName = event.target.dataset.tabName;
      this.setState({selectedTab: tabName});
    },

    render: function() {
      var cx = React.addons.classSet;
      var tabButtons = [];
      var tabs = [];
      React.Children.forEach(this.props.children, function(tab, i) {
        // Filter out null tabs (eg. rooms when the feature is disabled)
        if (!tab) {
          return;
        }
        var tabName = tab.props.name;
        if (this.props.buttonsHidden.indexOf(tabName) > -1) {
          return;
        }
        var isSelected = (this.state.selectedTab == tabName);
        if (!tab.props.hidden) {
          tabButtons.push(
            React.createElement("li", {className: cx({selected: isSelected}), 
                key: i, 
                "data-tab-name": tabName, 
                title: mozL10n.get(tabName + "_tab_button_tooltip"), 
                onClick: this.handleSelectTab})
          );
        }
        tabs.push(
          React.createElement("div", {key: i, className: cx({tab: true, selected: isSelected})}, 
            tab.props.children
          )
        );
      }, this);
      return (
        React.createElement("div", {className: "tab-view-container"}, 
          React.createElement("ul", {className: "tab-view"}, tabButtons), 
          tabs
        )
      );
    }
  });

  var Tab = React.createClass({displayName: "Tab",
    render: function() {
      return null;
    }
  });

  /**
   * Availability drop down menu subview.
   */
  var AvailabilityDropdown = React.createClass({displayName: "AvailabilityDropdown",
    mixins: [sharedMixins.DropdownMenuMixin],

    getInitialState: function() {
      return {
        doNotDisturb: navigator.mozLoop.doNotDisturb
      };
    },

    // XXX target event can either be the li, the span or the i tag
    // this makes it easier to figure out the target by making a
    // closure with the desired status already passed in.
    changeAvailability: function(newAvailabilty) {
      return function(event) {
        // Note: side effect!
        switch (newAvailabilty) {
          case 'available':
            this.setState({doNotDisturb: false});
            navigator.mozLoop.doNotDisturb = false;
            break;
          case 'do-not-disturb':
            this.setState({doNotDisturb: true});
            navigator.mozLoop.doNotDisturb = true;
            break;
        }
        this.hideDropdownMenu();
      }.bind(this);
    },

    render: function() {
      // XXX https://github.com/facebook/react/issues/310 for === htmlFor
      var cx = React.addons.classSet;
      var availabilityStatus = cx({
        'status': true,
        'status-dnd': this.state.doNotDisturb,
        'status-available': !this.state.doNotDisturb
      });
      var availabilityDropdown = cx({
        'dropdown-menu': true,
        'hide': !this.state.showMenu
      });
      var availabilityText = this.state.doNotDisturb ?
                              mozL10n.get("display_name_dnd_status") :
                              mozL10n.get("display_name_available_status");

      return (
        React.createElement("div", {className: "dropdown"}, 
          React.createElement("p", {className: "dnd-status", onClick: this.toggleDropdownMenu, ref: "menu-button"}, 
            React.createElement("span", null, availabilityText), 
            React.createElement("i", {className: availabilityStatus})
          ), 
          React.createElement("ul", {className: availabilityDropdown}, 
            React.createElement("li", {onClick: this.changeAvailability("available"), 
                className: "dropdown-menu-item dnd-make-available"}, 
              React.createElement("i", {className: "status status-available"}), 
              React.createElement("span", null, mozL10n.get("display_name_available_status"))
            ), 
            React.createElement("li", {onClick: this.changeAvailability("do-not-disturb"), 
                className: "dropdown-menu-item dnd-make-unavailable"}, 
              React.createElement("i", {className: "status status-dnd"}), 
              React.createElement("span", null, mozL10n.get("display_name_dnd_status"))
            )
          )
        )
      );
    }
  });

  var GettingStartedView = React.createClass({displayName: "GettingStartedView",
    mixins: [sharedMixins.WindowCloseMixin],

    handleButtonClick: function() {
      navigator.mozLoop.openGettingStartedTour("getting-started");
      navigator.mozLoop.setLoopPref("gettingStarted.seen", true);
      var event = new CustomEvent("GettingStartedSeen");
      window.dispatchEvent(event);
      this.closeWindow();
    },

    render: function() {
      if (navigator.mozLoop.getLoopPref("gettingStarted.seen")) {
        return null;
      }
      return (
        React.createElement("div", {id: "fte-getstarted"}, 
          React.createElement("header", {id: "fte-title"}, 
            mozL10n.get("first_time_experience_title", {
              "clientShortname": mozL10n.get("clientShortname2")
            })
          ), 
          React.createElement(Button, {htmlId: "fte-button", 
                  onClick: this.handleButtonClick, 
                  caption: mozL10n.get("first_time_experience_button_label")})
        )
      );
    }
  });

  var ToSView = React.createClass({displayName: "ToSView",
    mixins: [sharedMixins.WindowCloseMixin],

    getInitialState: function() {
      var getPref = navigator.mozLoop.getLoopPref.bind(navigator.mozLoop);

      return {
        seenToS: getPref("seenToS"),
        gettingStartedSeen: getPref("gettingStarted.seen"),
        showPartnerLogo: getPref("showPartnerLogo")
      };
    },

    handleLinkClick: function(event) {
      if (!event.target || !event.target.href) {
        return;
      }

      event.preventDefault();
      navigator.mozLoop.openURL(event.target.href);
      this.closeWindow();
    },

    renderPartnerLogo: function() {
      if (!this.state.showPartnerLogo) {
        return null;
      }

      var locale = mozL10n.getLanguage();
      navigator.mozLoop.setLoopPref('showPartnerLogo', false);
      return (
        React.createElement("p", {id: "powered-by", className: "powered-by"}, 
          mozL10n.get("powered_by_beforeLogo"), 
          React.createElement("img", {id: "powered-by-logo", className: locale}), 
          mozL10n.get("powered_by_afterLogo")
        )
      );
    },

    render: function() {
      if (!this.state.gettingStartedSeen || this.state.seenToS == "unseen") {
        var terms_of_use_url = navigator.mozLoop.getLoopPref('legal.ToS_url');
        var privacy_notice_url = navigator.mozLoop.getLoopPref('legal.privacy_url');
        var tosHTML = mozL10n.get("legal_text_and_links3", {
          "clientShortname": mozL10n.get("clientShortname2"),
          "terms_of_use": React.renderToStaticMarkup(
            React.createElement("a", {href: terms_of_use_url, target: "_blank"}, 
              mozL10n.get("legal_text_tos")
            )
          ),
          "privacy_notice": React.renderToStaticMarkup(
            React.createElement("a", {href: privacy_notice_url, target: "_blank"}, 
              mozL10n.get("legal_text_privacy")
            )
          )
        });
        return (
          React.createElement("div", {id: "powered-by-wrapper"}, 
            this.renderPartnerLogo(), 
            React.createElement("p", {className: "terms-service", 
               dangerouslySetInnerHTML: {__html: tosHTML}, 
               onClick: this.handleLinkClick})
           )
        );
      } else {
        return React.createElement("div", null);
      }
    }
  });

  /**
   * Panel settings (gear) menu entry.
   */
  var SettingsDropdownEntry = React.createClass({displayName: "SettingsDropdownEntry",
    propTypes: {
      onClick: React.PropTypes.func.isRequired,
      label: React.PropTypes.string.isRequired,
      icon: React.PropTypes.string,
      displayed: React.PropTypes.bool
    },

    getDefaultProps: function() {
      return {displayed: true};
    },

    render: function() {
      if (!this.props.displayed) {
        return null;
      }
      return (
        React.createElement("li", {onClick: this.props.onClick, className: "dropdown-menu-item"}, 
          this.props.icon ?
            React.createElement("i", {className: "icon icon-" + this.props.icon}) :
            null, 
          React.createElement("span", null, this.props.label)
        )
      );
    }
  });

  /**
   * Panel settings (gear) menu.
   */
  var SettingsDropdown = React.createClass({displayName: "SettingsDropdown",
    propTypes: {
      mozLoop: React.PropTypes.object.isRequired
    },

    mixins: [sharedMixins.DropdownMenuMixin, sharedMixins.WindowCloseMixin],

    handleClickSettingsEntry: function() {
      // XXX to be implemented at the same time as unhiding the entry
    },

    handleClickAccountEntry: function() {
      this.props.mozLoop.openFxASettings();
      this.closeWindow();
    },

    handleClickAuthEntry: function() {
      if (this._isSignedIn()) {
        this.props.mozLoop.logOutFromFxA();
      } else {
        this.props.mozLoop.logInToFxA();
      }
    },

    handleHelpEntry: function(event) {
      event.preventDefault();
      var helloSupportUrl = this.props.mozLoop.getLoopPref("support_url");
      this.props.mozLoop.openURL(helloSupportUrl);
      this.closeWindow();
    },

    _isSignedIn: function() {
      return !!this.props.mozLoop.userProfile;
    },

    openGettingStartedTour: function() {
      this.props.mozLoop.openGettingStartedTour("settings-menu");
      this.closeWindow();
    },

    render: function() {
      var cx = React.addons.classSet;

      return (
        React.createElement("div", {className: "settings-menu dropdown"}, 
          React.createElement("a", {className: "button-settings", 
             onClick: this.toggleDropdownMenu, 
             title: mozL10n.get("settings_menu_button_tooltip"), 
             ref: "menu-button"}), 
          React.createElement("ul", {className: cx({"dropdown-menu": true, hide: !this.state.showMenu})}, 
            React.createElement(SettingsDropdownEntry, {label: mozL10n.get("settings_menu_item_settings"), 
                                   onClick: this.handleClickSettingsEntry, 
                                   displayed: false, 
                                   icon: "settings"}), 
            React.createElement(SettingsDropdownEntry, {label: mozL10n.get("settings_menu_item_account"), 
                                   onClick: this.handleClickAccountEntry, 
                                   icon: "account", 
                                   displayed: this._isSignedIn() && this.props.mozLoop.fxAEnabled}), 
            React.createElement(SettingsDropdownEntry, {icon: "tour", 
                                   label: mozL10n.get("tour_label"), 
                                   onClick: this.openGettingStartedTour}), 
            React.createElement(SettingsDropdownEntry, {label: this._isSignedIn() ?
                                          mozL10n.get("settings_menu_item_signout") :
                                          mozL10n.get("settings_menu_item_signin"), 
                                   onClick: this.handleClickAuthEntry, 
                                   displayed: this.props.mozLoop.fxAEnabled, 
                                   icon: this._isSignedIn() ? "signout" : "signin"}), 
            React.createElement(SettingsDropdownEntry, {label: mozL10n.get("help_label"), 
                                   onClick: this.handleHelpEntry, 
                                   icon: "help"})
          )
        )
      );
    }
  });

  /**
   * FxA sign in/up link component.
   */
  var AuthLink = React.createClass({displayName: "AuthLink",
    mixins: [sharedMixins.WindowCloseMixin],

    handleSignUpLinkClick: function() {
      navigator.mozLoop.logInToFxA();
      this.closeWindow();
    },

    render: function() {
      if (!navigator.mozLoop.fxAEnabled || navigator.mozLoop.userProfile) {
        return null;
      }
      return (
        React.createElement("p", {className: "signin-link"}, 
          React.createElement("a", {href: "#", onClick: this.handleSignUpLinkClick}, 
            mozL10n.get("panel_footer_signin_or_signup_link")
          )
        )
      );
    }
  });

  /**
   * FxA user identity (guest/authenticated) component.
   */
  var UserIdentity = React.createClass({displayName: "UserIdentity",
    render: function() {
      return (
        React.createElement("p", {className: "user-identity"}, 
          this.props.displayName
        )
      );
    }
  });

  var RoomEntryContextItem = React.createClass({displayName: "RoomEntryContextItem",
    mixins: [loop.shared.mixins.WindowCloseMixin],

    propTypes: {
      mozLoop: React.PropTypes.object.isRequired,
      roomUrls: React.PropTypes.array
    },

    handleClick: function(event) {
      event.stopPropagation();
      event.preventDefault();
      this.props.mozLoop.openURL(event.currentTarget.href);
      this.closeWindow();
    },

    render: function() {
      var roomUrl = this.props.roomUrls && this.props.roomUrls[0];
      if (!roomUrl) {
        return null;
      }

      return (
        React.createElement("div", {className: "room-entry-context-item"}, 
          React.createElement("a", {href: roomUrl.location, onClick: this.handleClick}, 
            React.createElement("img", {title: roomUrl.description, 
                 src: roomUrl.thumbnail})
          )
        )
      );
    }
  });

  /**
   * Room list entry.
   */
  var RoomEntry = React.createClass({displayName: "RoomEntry",
    propTypes: {
      dispatcher: React.PropTypes.instanceOf(loop.Dispatcher).isRequired,
      mozLoop: React.PropTypes.object.isRequired,
      room: React.PropTypes.instanceOf(loop.store.Room).isRequired
    },

    mixins: [loop.shared.mixins.WindowCloseMixin],

    getInitialState: function() {
      return { urlCopied: false };
    },

    shouldComponentUpdate: function(nextProps, nextState) {
      return (nextProps.room.ctime > this.props.room.ctime) ||
        (nextState.urlCopied !== this.state.urlCopied);
    },

    handleClickEntry: function(event) {
      event.preventDefault();
      this.props.dispatcher.dispatch(new sharedActions.OpenRoom({
        roomToken: this.props.room.roomToken
      }));
      this.closeWindow();
    },

    handleCopyButtonClick: function(event) {
      event.stopPropagation();
      event.preventDefault();
      this.props.dispatcher.dispatch(new sharedActions.CopyRoomUrl({
        roomUrl: this.props.room.roomUrl
      }));
      this.setState({urlCopied: true});
    },

    handleDeleteButtonClick: function(event) {
      event.stopPropagation();
      event.preventDefault();
      this.props.mozLoop.confirm({
        message: mozL10n.get("rooms_list_deleteConfirmation_label"),
        okButton: null,
        cancelButton: null
      }, function(err, result) {
        if (err) {
          throw err;
        }

        if (!result) {
          return;
        }

        this.props.dispatcher.dispatch(new sharedActions.DeleteRoom({
          roomToken: this.props.room.roomToken
        }));
      }.bind(this));
    },

    handleMouseLeave: function(event) {
      this.setState({urlCopied: false});
    },

    _isActive: function() {
      return this.props.room.participants.length > 0;
    },

    render: function() {
      var roomClasses = React.addons.classSet({
        "room-entry": true,
        "room-active": this._isActive()
      });
      var copyButtonClasses = React.addons.classSet({
        "copy-link": true,
        "checked": this.state.urlCopied
      });

      return (
        React.createElement("div", {className: roomClasses, onMouseLeave: this.handleMouseLeave, 
             onClick: this.handleClickEntry}, 
          React.createElement("h2", null, 
            React.createElement("span", {className: "room-notification"}), 
            this.props.room.decryptedContext.roomName, 
            React.createElement("button", {className: copyButtonClasses, 
              title: mozL10n.get("rooms_list_copy_url_tooltip"), 
              onClick: this.handleCopyButtonClick}), 
            React.createElement("button", {className: "delete-link", 
              title: mozL10n.get("rooms_list_delete_tooltip"), 
              onClick: this.handleDeleteButtonClick})
          ), 
          React.createElement(RoomEntryContextItem, {mozLoop: this.props.mozLoop, 
                                roomUrls: this.props.room.decryptedContext.urls})
        )
      );
    }
  });

  /**
   * Room list.
   */
  var RoomList = React.createClass({displayName: "RoomList",
    mixins: [Backbone.Events, sharedMixins.WindowCloseMixin],

    propTypes: {
      mozLoop: React.PropTypes.object.isRequired,
      store: React.PropTypes.instanceOf(loop.store.RoomStore).isRequired,
      dispatcher: React.PropTypes.instanceOf(loop.Dispatcher).isRequired,
      userDisplayName: React.PropTypes.string.isRequired  // for room creation
    },

    getInitialState: function() {
      return this.props.store.getStoreState();
    },

    componentDidMount: function() {
      this.listenTo(this.props.store, "change", this._onStoreStateChanged);

      // XXX this should no longer be necessary once have a better mechanism
      // for updating the list (possibly as part of the content side of bug
      // 1074665.
      this.props.dispatcher.dispatch(new sharedActions.GetAllRooms());
    },

    componentWillUnmount: function() {
      this.stopListening(this.props.store);
    },

    componentWillUpdate: function(nextProps, nextState) {
      // If we've just created a room, close the panel - the store will open
      // the room.
      if (this.state.pendingCreation &&
          !nextState.pendingCreation && !nextState.error) {
        this.closeWindow();
      }
    },

    _onStoreStateChanged: function() {
      this.setState(this.props.store.getStoreState());
    },

    _getListHeading: function() {
      var numRooms = this.state.rooms.length;
      if (numRooms === 0) {
        return mozL10n.get("rooms_list_no_current_conversations");
      }
      return mozL10n.get("rooms_list_current_conversations", {num: numRooms});
    },

    render: function() {
      if (this.state.error) {
        // XXX Better end user reporting of errors.
        console.error("RoomList error", this.state.error);
      }

      return (
        React.createElement("div", {className: "rooms"}, 
          React.createElement("h1", null, this._getListHeading()), 
          React.createElement("div", {className: "room-list"}, 
            this.state.rooms.map(function(room, i) {
              return (
                React.createElement(RoomEntry, {
                  key: room.roomToken, 
                  dispatcher: this.props.dispatcher, 
                  mozLoop: this.props.mozLoop, 
                  room: room}
                )
              );
            }, this)
          ), 
          React.createElement(NewRoomView, {dispatcher: this.props.dispatcher, 
            mozLoop: this.props.mozLoop, 
            pendingOperation: this.state.pendingCreation ||
              this.state.pendingInitialRetrieval, 
            userDisplayName: this.props.userDisplayName})
        )
      );
    }
  });

  /**
   * Used for creating a new room with or without context.
   */
  var NewRoomView = React.createClass({displayName: "NewRoomView",
    propTypes: {
      dispatcher: React.PropTypes.instanceOf(loop.Dispatcher).isRequired,
      mozLoop: React.PropTypes.object.isRequired,
      pendingOperation: React.PropTypes.bool.isRequired,
      userDisplayName: React.PropTypes.string.isRequired
    },

    mixins: [sharedMixins.DocumentVisibilityMixin],

    getInitialState: function() {
      return {
        checked: false,
        previewImage: "",
        description: "",
        url: ""
      };
    },

    onDocumentVisible: function() {
      this.props.mozLoop.getSelectedTabMetadata(function callback(metadata) {
        var previewImage = metadata.previews.length ? metadata.previews[0] : "";
        var description = metadata.description || metadata.title;
        var url = metadata.url;
        this.setState({previewImage: previewImage,
                       description: description,
                       url: url});
      }.bind(this));
    },

    onDocumentHidden: function() {
      this.setState({previewImage: "",
                     description: "",
                     url: ""});
    },

    onCheckboxChange: function(event) {
      this.setState({checked: event.target.checked});
    },

    handleCreateButtonClick: function() {
      var createRoomAction = new sharedActions.CreateRoom({
        nameTemplate: mozL10n.get("rooms_default_room_name_template"),
        roomOwner: this.props.userDisplayName
      });

      if (this.state.checked) {
        createRoomAction.urls = [{
          location: this.state.url,
          description: this.state.description,
          thumbnail: this.state.previewImage
        }];
      }
      this.props.dispatcher.dispatch(createRoomAction);
    },

    render: function() {
      var hostname;

      try {
        hostname = new URL(this.state.url).hostname;
      } catch (ex) {
        // Empty catch - if there's an error, then we won't show the context.
      }

      var contextClasses = React.addons.classSet({
        context: true,
        hide: !hostname ||
          !this.props.mozLoop.getLoopPref("contextInConversations.enabled")
      });

      return (
        React.createElement("div", {className: "new-room-view"}, 
          React.createElement("div", {className: contextClasses}, 
            React.createElement("label", {className: "context-enabled"}, 
              React.createElement("input", {className: "context-checkbox", 
                     type: "checkbox", onChange: this.onCheckboxChange}), 
              mozL10n.get("context_offer_label")
            ), 
            React.createElement("img", {className: "context-preview", src: this.state.previewImage}), 
            React.createElement("span", {className: "context-description"}, this.state.description), 
            React.createElement("span", {className: "context-url"}, hostname)
          ), 
          React.createElement("button", {className: "btn btn-info new-room-button", 
                  onClick: this.handleCreateButtonClick, 
                  disabled: this.props.pendingOperation}, 
            mozL10n.get("rooms_new_room_button_label")
          )
        )
      );
    }
  });

  /**
   * Panel view.
   */
  var PanelView = React.createClass({displayName: "PanelView",
    propTypes: {
      notifications: React.PropTypes.object.isRequired,
      // Mostly used for UI components showcase and unit tests
      userProfile: React.PropTypes.object,
      // Used only for unit tests.
      showTabButtons: React.PropTypes.bool,
      selectedTab: React.PropTypes.string,
      dispatcher: React.PropTypes.instanceOf(loop.Dispatcher).isRequired,
      mozLoop: React.PropTypes.object.isRequired,
      roomStore:
        React.PropTypes.instanceOf(loop.store.RoomStore).isRequired
    },

    getInitialState: function() {
      return {
        userProfile: this.props.userProfile || this.props.mozLoop.userProfile,
        gettingStartedSeen: this.props.mozLoop.getLoopPref("gettingStarted.seen")
      };
    },

    _serviceErrorToShow: function() {
      if (!this.props.mozLoop.errors ||
          !Object.keys(this.props.mozLoop.errors).length) {
        return null;
      }
      // Just get the first error for now since more than one should be rare.
      var firstErrorKey = Object.keys(this.props.mozLoop.errors)[0];
      return {
        type: firstErrorKey,
        error: this.props.mozLoop.errors[firstErrorKey]
      };
    },

    updateServiceErrors: function() {
      var serviceError = this._serviceErrorToShow();
      if (serviceError) {
        this.props.notifications.set({
          id: "service-error",
          level: "error",
          message: serviceError.error.friendlyMessage,
          details: serviceError.error.friendlyDetails,
          detailsButtonLabel: serviceError.error.friendlyDetailsButtonLabel,
          detailsButtonCallback: serviceError.error.friendlyDetailsButtonCallback
        });
      } else {
        this.props.notifications.remove(this.props.notifications.get("service-error"));
      }
    },

    _onStatusChanged: function() {
      var profile = this.props.mozLoop.userProfile;
      var currUid = this.state.userProfile ? this.state.userProfile.uid : null;
      var newUid = profile ? profile.uid : null;
      if (currUid != newUid) {
        // On profile change (login, logout), switch back to the default tab.
        this.selectTab("rooms");
        this.setState({userProfile: profile});
      }
      this.updateServiceErrors();
    },

    _gettingStartedSeen: function() {
      this.setState({
        gettingStartedSeen: this.props.mozLoop.getLoopPref("gettingStarted.seen")
      });
    },

    _UIActionHandler: function(e) {
      switch (e.detail.action) {
        case "selectTab":
          this.selectTab(e.detail.tab);
          break;
        default:
          console.error("Invalid action", e.detail.action);
          break;
      }
    },

    startForm: function(name, contact) {
      this.refs[name].initForm(contact);
      this.selectTab(name);
    },

    selectTab: function(name) {
      this.refs.tabView.setState({ selectedTab: name });
    },

    componentWillMount: function() {
      this.updateServiceErrors();
    },

    componentDidMount: function() {
      window.addEventListener("LoopStatusChanged", this._onStatusChanged);
      window.addEventListener("GettingStartedSeen", this._gettingStartedSeen);
      window.addEventListener("UIAction", this._UIActionHandler);
    },

    componentWillUnmount: function() {
      window.removeEventListener("LoopStatusChanged", this._onStatusChanged);
      window.removeEventListener("GettingStartedSeen", this._gettingStartedSeen);
      window.removeEventListener("UIAction", this._UIActionHandler);
    },

    _getUserDisplayName: function() {
      return this.state.userProfile && this.state.userProfile.email ||
             mozL10n.get("display_name_guest");
    },

    render: function() {
      var NotificationListView = sharedViews.NotificationListView;

      if (!this.state.gettingStartedSeen) {
        return (
          React.createElement("div", null, 
            React.createElement(NotificationListView, {notifications: this.props.notifications, 
                                  clearOnDocumentHidden: true}), 
            React.createElement(GettingStartedView, null), 
            React.createElement(ToSView, null)
          )
        );
      }

      // Determine which buttons to NOT show.
      var hideButtons = [];
      if (!this.state.userProfile && !this.props.showTabButtons) {
        hideButtons.push("contacts");
      }

      return (
        React.createElement("div", null, 
          React.createElement(NotificationListView, {notifications: this.props.notifications, 
                                clearOnDocumentHidden: true}), 
          React.createElement(TabView, {ref: "tabView", selectedTab: this.props.selectedTab, 
            buttonsHidden: hideButtons, mozLoop: this.props.mozLoop}, 
            React.createElement(Tab, {name: "rooms"}, 
              React.createElement(RoomList, {dispatcher: this.props.dispatcher, 
                        store: this.props.roomStore, 
                        userDisplayName: this._getUserDisplayName(), 
                        mozLoop: this.props.mozLoop}), 
              React.createElement(ToSView, null)
            ), 
            React.createElement(Tab, {name: "contacts"}, 
              React.createElement(ContactsList, {selectTab: this.selectTab, 
                            startForm: this.startForm, 
                            notifications: this.props.notifications})
            ), 
            React.createElement(Tab, {name: "contacts_add", hidden: true}, 
              React.createElement(ContactDetailsForm, {ref: "contacts_add", mode: "add", 
                                  selectTab: this.selectTab})
            ), 
            React.createElement(Tab, {name: "contacts_edit", hidden: true}, 
              React.createElement(ContactDetailsForm, {ref: "contacts_edit", mode: "edit", 
                                  selectTab: this.selectTab})
            ), 
            React.createElement(Tab, {name: "contacts_import", hidden: true}, 
              React.createElement(ContactDetailsForm, {ref: "contacts_import", mode: "import", 
                                  selectTab: this.selectTab})
            )
          ), 
          React.createElement("div", {className: "footer"}, 
            React.createElement("div", {className: "user-details"}, 
              React.createElement(UserIdentity, {displayName: this._getUserDisplayName()}), 
              React.createElement(AvailabilityDropdown, null)
            ), 
            React.createElement("div", {className: "signin-details"}, 
              React.createElement(AuthLink, null), 
              React.createElement("div", {className: "footer-signin-separator"}), 
              React.createElement(SettingsDropdown, {mozLoop: this.props.mozLoop})
            )
          )
        )
      );
    }
  });

  /**
   * Panel initialisation.
   */
  function init() {
    // Do the initial L10n setup, we do this before anything
    // else to ensure the L10n environment is setup correctly.
    mozL10n.initialize(navigator.mozLoop);

    var notifications = new sharedModels.NotificationCollection();
    var dispatcher = new loop.Dispatcher();
    var roomStore = new loop.store.RoomStore(dispatcher, {
      mozLoop: navigator.mozLoop,
      notifications: notifications
    });

    React.render(React.createElement(PanelView, {
      notifications: notifications, 
      roomStore: roomStore, 
      mozLoop: navigator.mozLoop, 
      dispatcher: dispatcher}
    ), document.querySelector("#main"));

    document.body.setAttribute("dir", mozL10n.getDirection());

    // Notify the window that we've finished initalization and initial layout
    var evtObject = document.createEvent('Event');
    evtObject.initEvent('loopPanelInitialized', true, false);
    window.dispatchEvent(evtObject);
  }

  return {
    init: init,
    AuthLink: AuthLink,
    AvailabilityDropdown: AvailabilityDropdown,
    GettingStartedView: GettingStartedView,
    NewRoomView: NewRoomView,
    PanelView: PanelView,
    RoomEntry: RoomEntry,
    RoomList: RoomList,
    SettingsDropdown: SettingsDropdown,
    ToSView: ToSView,
    UserIdentity: UserIdentity
  };
})(_, document.mozL10n);

document.addEventListener('DOMContentLoaded', loop.panel.init);
