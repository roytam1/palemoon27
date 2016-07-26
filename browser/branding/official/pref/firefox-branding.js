#filter substitution

// ****************** App/Update/General ******************

pref("startup.homepage_override_url","http://www.palemoon.org/releasenotes.shtml");
pref("startup.homepage_welcome_url","http://www.palemoon.org/firstrun.shtml");
// Interval: Time between checks for a new version (in seconds) -- 2 days for Pale Moon
pref("app.update.interval", 172800);
pref("app.update.auto", false);
pref("app.update.enabled", true);
// URL for update checks, re-enabled on palemoon.org (369)
pref("app.update.url", "https://www.palemoon.org/update/%VERSION%/%BUILD_TARGET%/update.xml");
pref("app.update.promptWaitTime", 86400); 
// The time interval between the downloading of mar file chunks in the
// background (in seconds)
pref("app.update.download.backgroundInterval", 600);
// Give the user x seconds to react before showing the big UI. default=48 hours
pref("app.update.promptWaitTime", 172800);
// URL user can browse to manually if for some reason all update installation
// attempts fail.
pref("app.update.url.manual", "http://www.palemoon.org/");
// A default value for the "More information about this update" link
// supplied in the "An update is available" page of the update wizard. 
pref("app.update.url.details", "http://www.palemoon.org/releasenotes.shtml");
// Additional Update fixes - no SSL damnit, I don't have a cert (4.0)
pref("app.update.cert.checkAttributes", false);
pref("app.update.cert.requireBuiltIn", false);
// Make sure we shortcut out of a11y to save walking unnecessary code
pref("accessibility.force_disabled", 1);

// ****************** Release notes and vendor URLs ******************

pref("app.releaseNotesURL", "http://www.palemoon.org/releasenotes.shtml");
pref("app.vendorURL", "http://www.palemoon.org/");
pref("app.support.baseURL", "http://www.palemoon.org/support/");
pref("browser.mixedcontent.warning.infoURL", "http://support.mozilla.org/1/firefox/%VERSION%/%OS%/%LOCALE%/mixed-content/");
//Add-on window fixes
pref("extensions.getAddons.browseAddons", "https://addons.mozilla.org/%LOCALE%/firefox");
pref("extensions.getAddons.maxResults", 10);
pref("extensions.getAddons.recommended.browseURL", "https://addons.palemoon.org/integration/addon-manager/external/recommended");
pref("extensions.getAddons.recommended.url", "https://addons.palemoon.org/integration/addon-manager/internal/recommended?locale=%LOCALE%&os=%OS%");
pref("extensions.getAddons.search.browseURL", "https://addons.palemoon.org/integration/addon-manager/external/search?q=%TERMS%");
pref("extensions.getAddons.search.url", "https://addons.palemoon.org/integration/addon-manager/internal/search?q=%TERMS%&locale=%LOCALE%&os=%OS%&version=%VERSION%");
pref("extensions.getMoreThemesURL", "https://addons.palemoon.org/integration/addon-manager/external/themes");
pref("extensions.blocklist.url", "https://addons.mozilla.org/blocklist/3/firefox/%APP_VERSION%/%PRODUCT%/%BUILD_ID%/%BUILD_TARGET%/%LOCALE%/%CHANNEL%/%OS_VERSION%/%DISTRIBUTION%/%DISTRIBUTION_VERSION%/");
pref("extensions.blocklist.itemURL", "https://addons.mozilla.org/%LOCALE%/firefox/blocked/%blockID%");
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

// ****************** domain-specific UAs ******************
#include ../../common/uaoverrides.inc

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

// ****************** Security ******************

// Known embedding domains that do NOT do things properly and should provide
// a filter-disabling header, but don't.
pref("security.xssfilter.srcwhitelist", "googleusercontent.com,embed.ly,embedly.com,msads.net");

// Known embedded target domains that do things properly (no XSS vulnerability)
pref("security.xssfilter.whitelist", "paypalobjects.com,yimg.com");

// ****************** Misc. config ******************

// Download manager
pref("browser.download.manager.flashCount", 10);
pref("browser.download.manager.scanWhenDone", false); //NIB, make sure to disable to prevent hangups
pref("browser.altClickSave", true); //SBaD,M! (#2)

//plugin kill timeout
pref("dom.ipc.plugins.timeoutSecs", 20);

//Automatically update extensions by default
pref("extensions.update.autoUpdateDefault", true);

//Improve memory handling for js
pref("javascript.options.mem.gc_per_compartment", true);
pref("javascript.options.mem.high_water_mark", 64);
pref("javascript.options.mem.max", -1);
pref("javascript.options.gc_on_memory_pressure", true);
pref("javascript.options.mem.disable_explicit_compartment_gc", true);
//add IGC and adjust time slice
pref("javascript.options.mem.gc_incremental",true);
pref("javascript.options.mem.gc_incremental_slice_ms",20);

//DOM
pref("dom.disable_window_status_change", false); //Allow status feedback by default
//Set max script runtimes to sane values
pref("dom.max_chrome_script_run_time", 90); //Some addons need ample time!
pref("dom.max_script_run_time", 20); //Should be plenty for a page script to do what it needs

//Media components

//Image decoding tweaks
pref("image.mem.max_ms_before_yield", 50);
pref("image.mem.decode_bytes_at_a_time", 65536); //larger chunks

//store sessions less frequently to prevent redundant mem usage by storing too much
pref("browser.sessionstore.interval",60000); //every minute instead of every 10 seconds
pref("browser.sessionstore.privacy_level",1); //don't store session data (forms, etc) for secure sites
