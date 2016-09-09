/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

const EXPORTED_SYMBOLS = ["S4EToolbars"];

const CI = Components.interfaces;
const CU = Components.utils;

CU.import("resource://gre/modules/Services.jsm");

function S4EToolbars(window, gBrowser, toolbox, service, getters)
{
	this._window = window;
	this._toolbox = toolbox;
	this._service = service;
	this._getters = getters;

	if(Services.vc.compare("28.*", Services.appinfo.version) < 0)
	{
		this._handler = new AustralisS4EToolbars(this._window, gBrowser, this._getters);
		Services.console.logStringMessage("S4EToolbars using AustralisS4EToolbars backend");
	}
	else
	{
		this._handler = new ClassicS4EToolbars(this._window, this._toolbox);
		Services.console.logStringMessage("S4EToolbars using ClassicS4EToolbars backend");
	}
}

S4EToolbars.prototype =
{
	_window:  null,
	_toolbox: null,
	_service: null,
	_getters: null,

	_handler: null,

	setup: function()
	{
		this.updateSplitters(false);
		this.updateWindowGripper(false);
		this._handler.setup(this._service.firstRun, this._service.firstRunAustralis);
	},

	destroy: function()
	{
		this._handler.destroy();

		["_window", "_toolbox",  "_service", "_getters", "_handler"].forEach(function(prop)
		{
			delete this[prop];
		}, this);
	},

	updateSplitters: function(action)
	{
		let document = this._window.document;

		let splitter_before = document.getElementById("status4evar-status-splitter-before");
		if(splitter_before)
		{
			splitter_before.parentNode.removeChild(splitter_before);
		}

		let splitter_after = document.getElementById("status4evar-status-splitter-after");
		if(splitter_after)
		{
			splitter_after.parentNode.removeChild(splitter_after);
		}

		let status = this._getters.statusWidget;
		if(!action || !status)
		{
			return;
		}

		let urlbar = document.getElementById("urlbar-container");
		let stop = document.getElementById("stop-button");
		let fullscreenflex = document.getElementById("fullscreenflex");

		let nextSibling = status.nextSibling;
		let previousSibling = status.previousSibling;

		function getSplitter(splitter, suffix)
		{
			if(!splitter)
			{
				splitter = document.createElement("splitter");
				splitter.id = "status4evar-status-splitter-" + suffix;
				splitter.setAttribute("resizebefore", "flex");
				splitter.setAttribute("resizeafter", "flex");
				splitter.className = "chromeclass-toolbar-additional status4evar-status-splitter";
			}
			return splitter;
		}

		if((previousSibling && previousSibling.flex > 0)
		|| (urlbar && stop && urlbar.getAttribute("combined") && stop == previousSibling))
		{
			status.parentNode.insertBefore(getSplitter(splitter_before, "before"), status);
		}

		if(nextSibling && nextSibling.flex > 0 && nextSibling != fullscreenflex)
		{
			status.parentNode.insertBefore(getSplitter(splitter_after, "after"), nextSibling);
		}
	},

	updateWindowGripper: function(action)
	{
		let document = this._window.document;

		let gripper = document.getElementById("status4evar-window-gripper");
		let toolbar = this._getters.statusBar || this._getters.addonbar;

		if(!action || !toolbar || !this._service.addonbarWindowGripper
		|| this._window.windowState != CI.nsIDOMChromeWindow.STATE_NORMAL || toolbar.toolbox.customizing)
		{
			if(gripper)
			{
				gripper.parentNode.removeChild(gripper);
			}
			return;
		}

		gripper = this._handler.buildGripper(toolbar, gripper, "status4evar-window-gripper");

		toolbar.appendChild(gripper);
	}
};

function ClassicS4EToolbars(window, toolbox)
{
	this._window = window;
	this._toolbox = toolbox;
}

ClassicS4EToolbars.prototype =
{
	_window:  null,
	_toolbox: null,

	setup: function(firstRun, firstRunAustralis)
	{
		let document = this._window.document;

		let addon_bar = document.getElementById("addon-bar");
		if(addon_bar)
		{
			let baseSet = "addonbar-closebutton"
				    + ",status4evar-status-widget"
				    + ",status4evar-download-button"
				    + ",status4evar-progress-widget";

			// Update the defaultSet
			let defaultSet = baseSet;
			let defaultSetIgnore = ["addonbar-closebutton", "spring", "status-bar"];
			addon_bar.getAttribute("defaultset").split(",").forEach(function(item)
			{
				if(defaultSetIgnore.indexOf(item) == -1)
				{
					defaultSet += "," + item;
				}
			});
			defaultSet += ",status-bar"
			addon_bar.setAttribute("defaultset", defaultSet);

			// Update the currentSet
			if(firstRun)
			{
				let isCustomizableToolbar = function(aElt)
				{
					return aElt.localName == "toolbar" && aElt.getAttribute("customizable") == "true";
				}

				let isCustomizedAlready = false;
				let toolbars = Array.filter(this._toolbox.childNodes, isCustomizableToolbar).concat(
					       Array.filter(this._toolbox.externalToolbars, isCustomizableToolbar));
				toolbars.forEach(function(toolbar)
				{
					if(toolbar.currentSet.indexOf("status4evar") > -1)
					{
						isCustomizedAlready = true;
					}
				});

				if(!isCustomizedAlready)
				{
					let currentSet = baseSet;
					let currentSetIgnore = ["addonbar-closebutton", "spring"];
					addon_bar.currentSet.split(",").forEach(function(item)
					{
						if(currentSetIgnore.indexOf(item) == -1)
						{
							currentSet += "," + item;
						}
					});
					addon_bar.currentSet = currentSet;
					addon_bar.setAttribute("currentset", currentSet);
					document.persist(addon_bar.id, "currentset");
					this._window.setToolbarVisibility(addon_bar, true);
				}
			}
		}
	},

	destroy: function()
	{
		["_window", "_toolbox"].forEach(function(prop)
		{
			delete this[prop];
		}, this);
	},

	buildGripper: function(toolbar, gripper, id)
	{
		if(!gripper)
		{
			let document = this._window.document;

			gripper = document.createElement("resizer");
			gripper.id = id;
			gripper.dir = "bottomend";
		}

		return gripper;
	}
};

function AustralisS4EToolbars(window, gBrowser, getters)
{
	this._window = window;
	this._gBrowser = gBrowser;
	this._getters = getters;

	this.__bound_updateWindowResizers = this.updateWindowResizers.bind(this);

	this._api = CU.import("resource:///modules/statusbar/Australis.jsm", {}).AustralisTools;
}

AustralisS4EToolbars.prototype =
{
	_window:   null,
	_gBrowser: null,
	_getters:  null,

	__bound_updateWindowResizers: null,
	__old_updateWindowResizers: null,

	_api: null,

	setup: function(firstRun, firstRunAustralis)
	{
		this.__old_updateWindowResizers = this._gBrowser.updateWindowResizers;
		this._gBrowser.updateWindowResizers = this.__bound_updateWindowResizers;

		if(!firstRun && firstRunAustralis)
		{
			this._api.migrate();
		}
	},

	destroy: function()
	{
		this._gBrowser.updateWindowResizers = this.__old_updateWindowResizers;

		["_window", "_gBrowser", "_getters", "_api", "__bound_updateWindowResizers",
		"__old_updateWindowResizers"].forEach(function(prop)
		{
			delete this[prop];
		}, this);
	},

	updateWindowResizers: function()
	{
		if(!this._window.gShowPageResizers)
		{
			return;
		}

		let toolbar = this._getters.statusBar;
		let show = this._window.windowState == this._window.STATE_NORMAL && (!toolbar || toolbar.collapsed);
		this._gBrowser.browsers.forEach(function(browser)
		{
			browser.showWindowResizer = show;
		});
	},

	buildGripper: function(toolbar, container, id)
	{
		if(!container)
		{
			let document = this._window.document;

			let gripper = document.createElement("resizer");
			gripper.dir = "bottomend";

			container = document.createElement("hbox");
			container.id = id;
			container.pack = "end";
			container.ordinal = 1000;
			container.appendChild(gripper);
		}

		let needFlex = 1;
		for(let node of toolbar.childNodes)
		{
			if(node.hasAttribute("flex") || node.flex)
			{
				needFlex = 0;
				break;
			}
		}
		container.flex = needFlex;

		return container;
	}
};

