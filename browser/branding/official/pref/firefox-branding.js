/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

pref("startup.homepage_override_url","http://www.palemoon.org/releasenotes.shtml");
pref("startup.homepage_welcome_url","http://www.palemoon.org/firstrun.shtml");
// Interval: Time between checks for a new version (in seconds)
pref("app.update.interval", 172800);
pref("app.update.auto", false);
pref("app.update.enabled", true);
// The time interval between the downloading of mar file chunks in the
// background (in seconds)
pref("app.update.download.backgroundInterval", 600);
// Give the user x seconds to react before showing the big UI. default=48 hours
pref("app.update.url", "https://www.palemoon.org/update/%VERSION%/%BUILD_TARGET%/update.xml");
pref("app.update.promptWaitTime", 172800);
// URL user can browse to manually if for some reason all update installation
// attempts fail.
pref("app.update.url.manual", "http://www.palemoon.org/");
// A default value for the "More information about this update" link
// supplied in the "An update is available" page of the update wizard. 
pref("app.update.url.details", "http://www.palemoon.org/releasenotes.shtml");
pref("app.update.cert.checkAttributes", false);
pref("app.update.cert.requireBuiltIn", false);
// The number of days a binary is permitted to be old
// without checking for an update.  This assumes that
// app.update.checkInstallTime is true.
pref("app.update.checkInstallTime.days", 63);

// Number of usages of the web console or scratchpad.
// If this is less than 5, then pasting code into the web console or scratchpad is disabled
pref("devtools.selfxss.count", 20);

pref("app.releaseNotesURL", "http://www.palemoon.org/releasenotes.shtml");
pref("app.vendorURL", "http://www.palemoon.org/");
pref("app.support.baseURL", "http://www.palemoon.org/support/");

//Add-on window fixes
pref("extensions.getAddons.browseAddons", "https://addons.mozilla.org/%LOCALE%/firefox");
pref("extensions.getAddons.maxResults", 10);
pref("extensions.getAddons.recommended.browseURL", "https://addons.palemoon.org/integration/addon-manager/external/recommended");
pref("extensions.getAddons.recommended.url", "https://addons.palemoon.org/integration/addon-manager/internal/recommended?locale=%LOCALE%&os=%OS%");
pref("extensions.getAddons.search.browseURL", "https://addons.palemoon.org/integration/addon-manager/external/search?q=%TERMS%");
pref("extensions.getAddons.search.url", "https://addons.palemoon.org/integration/addon-manager/internal/search?q=%TERMS%&locale=%LOCALE%&os=%OS%&version=%VERSION%");
pref("extensions.getMoreThemesURL", "https://addons.palemoon.org/integration/addon-manager/external/themes");
pref("extensions.webservice.discoverURL","http://addons.palemoon.org/integration/addon-manager/internal/discover/");
pref("extensions.getAddons.cache.enabled", false);
pref("extensions.getAddons.get.url","https://addons.palemoon.org/integration/addon-manager/internal/get?addonguid=%IDS%&os=%OS%&version=%VERSION%");
pref("extensions.getAddons.getWithPerformance.url","https://addons.palemoon.org/integration/addon-manager/internal/get?addonguid=%IDS%&os=%OS%&version=%VERSION%");
//Add-on updates: hard-code base Firefox version number.
pref("extensions.update.background.url","https://addons.palemoon.org/integration/addon-manager/internal/update?reqVersion=%REQ_VERSION%&id=%ITEM_ID%&version=%ITEM_VERSION%&maxAppVersion=%ITEM_MAXAPPVERSION%&status=%ITEM_STATUS%&appID=%APP_ID%&appVersion=%APP_VERSION%&appOS=%APP_OS%&appABI=%APP_ABI%&locale=%APP_LOCALE%&currentAppVersion=%CURRENT_APP_VERSION%&updateType=%UPDATE_TYPE%&compatMode=%COMPATIBILITY_MODE%");
pref("extensions.update.url","https://addons.palemoon.org/integration/addon-manager/internal/update?reqVersion=%REQ_VERSION%&id=%ITEM_ID%&version=%ITEM_VERSION%&maxAppVersion=%ITEM_MAXAPPVERSION%&status=%ITEM_STATUS%&appID=%APP_ID%&appVersion=%APP_VERSION%&appOS=%APP_OS%&appABI=%APP_ABI%&locale=%APP_LOCALE%&currentAppVersion=%CURRENT_APP_VERSION%&updateType=%UPDATE_TYPE%&compatMode=%COMPATIBILITY_MODE%");
//Search engine fixes
pref("browser.search.searchEnginesURL", "https://addons.mozilla.org/%LOCALE%/firefox/search-engines/");
//Dictionary URL
pref("browser.dictionaries.download.url", "https://addons.mozilla.org/%LOCALE%/firefox/dictionaries/");
//Geolocation info URL
pref("browser.geolocation.warning.infoURL", "http://www.mozilla.com/%LOCALE%/firefox/geolocation/");
//add-on/plugin blocklist -> Palemoon.org
pref("extensions.blocklist.url","http://blocklist.palemoon.org/%VERSION%/blocklist.xml");
pref("extensions.blocklist.itemURL", "http://blocklist.palemoon.org/info/?id=%blockID%");

// ****************** Extensions/plugins ******************

pref("plugin.default.state", 2); //Allow plugins to run by default
pref("plugin.expose_full_path", true); //Security: expose the full path to the plugin

// ****************** Renderer config ******************

pref("gfx.color_management.mode",2); //Use CMS for images with ICC profile.
pref("gfx.color_management.enablev4", true); //Use "new" handler to prevent display issues for v4 ICC embedded profiles!

// ****************** UI config ******************

pref("browser.tabs.insertRelatedAfterCurrent", false); //use old method of tabbed browsing instead of "Chrome" style
pref("browser.download.useDownloadDir", false); //don't use default download location as standard. ASK.
pref("browser.search.context.loadInBackground", true); //don't swap focus to the context search tab.
pref("browser.ctrlTab.previews", true);
pref("browser.allTabs.previews", true);
pref("browser.urlbar.trimURLs", false); //stop being a derp, Mozilla!
pref("browser.identity.ssl_domain_display", 1); //show domain verified SSL (blue)
pref("browser.urlbar.autoFill", true);
pref("browser.urlbar.autoFill.typed", true);

//Set tabs NOT on top
pref("browser.tabs.onTop",false);

//store sessions less frequently to prevent redundant mem usage by storing too much
pref("browser.sessionstore.interval",60000); //every minute instead of every 10 seconds
pref("browser.sessionstore.privacy_level",1); //don't store session data (forms, etc) for secure sites
