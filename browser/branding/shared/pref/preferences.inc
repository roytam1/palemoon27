#define BRANDING_DIRECTORY_OFFICIAL browser/branding/official
#define BRANDING_DIRECTORY_UNSTABLE browser/branding/unstable

#define APO_AM_URL addons.palemoon.org/integration/addon-manager
#define APO_AUS_ARGS reqVersion=%REQ_VERSION%&id=%ITEM_ID%&version=%ITEM_VERSION%&maxAppVersion=%ITEM_MAXAPPVERSION%&status=%ITEM_STATUS%&appID=%APP_ID%&appVersion=%APP_VERSION%&appOS=%APP_OS%&appABI=%APP_ABI%&locale=%APP_LOCALE%&currentAppVersion=%CURRENT_APP_VERSION%&updateType=%UPDATE_TYPE%&compatMode=%COMPATIBILITY_MODE%

// ===| General |==============================================================

pref("startup.homepage_welcome_url","http://www.palemoon.org/firstrun.shtml");

pref("app.vendorURL", "http://www.palemoon.org/");
pref("app.support.baseURL", "http://www.palemoon.org/support/");
pref("browser.getdevtools.url","https://@APO_AM_URL@/external/devtools");

// User Interface
pref("browser.tabs.insertRelatedAfterCurrent", false); //use old method of tabbed browsing instead of "Chrome" style
pref("browser.download.useDownloadDir", false); //don not use default download location as standard. ASK.
pref("browser.search.context.loadInBackground", true); //don not swap focus to the context search tab.
pref("browser.ctrlTab.previews", true);
pref("browser.allTabs.previews", true);
pref("browser.urlbar.trimURLs", false); //stop being a derp, Mozilla!
pref("browser.identity.ssl_domain_display", 1); //show domain verified SSL (blue)
pref("browser.urlbar.autoFill", true);
pref("browser.urlbar.autoFill.typed", true);

//Set tabs NOT on top
pref("browser.tabs.onTop",false);

// Download manager
pref("browser.download.manager.flashCount", 10);
pref("browser.download.manager.scanWhenDone", false); //NIB, make sure to disable to prevent hangups
pref("browser.altClickSave", true); //SBaD,M! (#2)

// ============================================================================

// ===| Application Update Service |===========================================

pref("app.update.auto", false);

#if MOZ_BRANDING_DIRECTORY == BRANDING_DIRECTORY_OFFICIAL && defined(XP_WIN)
// Updates enabled
pref("app.update.enabled", true);
pref("app.update.url", "https://www.palemoon.org/update/%VERSION%/%BUILD_TARGET%/update.xml");
pref("app.update.cert.checkAttributes", true);

// Interval: Time between checks for a new version (in seconds) -- 2 days for Pale Moon
pref("app.update.interval", 172800);
pref("app.update.promptWaitTime", 86400);

// URL user can browse to manually if for some reason all update
// installation attempts fail.
pref("app.update.url.manual", "http://www.palemoon.org/");

// A default value for the "More information about this update" link
// supplied in the "An update is available" page of the update wizard. 
pref("app.update.url.details", "http://www.palemoon.org/releasenotes.shtml");
#elif MOZ_BRANDING_DIRECTORY == BRANDING_DIRECTORY_UNSTABLE && defined(XP_WIN)
// Updates enabled
pref("app.update.enabled", true);
pref("app.update.url", "https://www.palemoon.org/unstable/update.xml");
pref("app.update.cert.checkAttributes", true);

// Interval: Time between checks for a new version (in seconds) -- 6 hours for unstable
pref("app.update.interval", 21600);
pref("app.update.promptWaitTime", 86400);

// URL user can browse to manually if for some reason all update installation
// attempts fail.
pref("app.update.url.manual", "http://www.palemoon.org/unstable/");

// A default value for the "More information about this update" link
// supplied in the "An update is available" page of the update wizard. 
pref("app.update.url.details", "http://www.palemoon.org/unstable/");
#else
// Updates disabled
pref("app.update.enabled", false);
pref("app.update.url", "");
#endif

// The time interval between the downloading of mar file chunks in the
// background (in seconds)
pref("app.update.download.backgroundInterval", 600);

// Give the user x seconds to react before showing the big UI. default=48 hours
pref("app.update.promptWaitTime", 172800);

// Additional Update fixes
pref("app.update.cert.requireBuiltIn", false);

// ============================================================================

// ===| Add-ons Manager |======================================================

// Add-on window fixes
pref("extensions.getAddons.browseAddons", "https://addons.mozilla.org/%LOCALE%/firefox");
pref("extensions.getAddons.recommended.browseURL", "https://@APO_AM_URL@/external/recommended");
pref("extensions.getAddons.recommended.url", "https://@APO_AM_URL@/internal/recommended?locale=%LOCALE%&os=%OS%");
pref("extensions.getAddons.search.browseURL", "https://@APO_AM_URL@/external/search?q=%TERMS%");
pref("extensions.getAddons.search.url", "https://@APO_AM_URL@/internal/search?q=%TERMS%&locale=%LOCALE%&os=%OS%&version=%VERSION%");
pref("extensions.getMoreThemesURL", "https://@APO_AM_URL@/external/themes");
pref("extensions.webservice.discoverURL","http://@APO_AM_URL@/internal/discover/");
pref("extensions.getAddons.get.url","https://@APO_AM_URL@/internal/get?addonguid=%IDS%&os=%OS%&version=%VERSION%");
pref("extensions.getAddons.getWithPerformance.url","https://@APO_AM_URL@/internal/get?addonguid=%IDS%&os=%OS%&version=%VERSION%");

// Add-on updates: hard-code base Firefox version number.
pref("extensions.update.background.url","https://@APO_AM_URL@/internal/update?@APO_AUS_ARGS@");
pref("extensions.update.url","https://@APO_AM_URL@/internal/update?@APO_AUS_ARGS@");

// Extensions Blocklist
pref("extensions.blocklist.url","http://blocklist.palemoon.org/%VERSION%/blocklist.xml");
pref("extensions.blocklist.itemURL", "http://blocklist.palemoon.org/info/?id=%blockID%");

// Search engine fixes
pref("browser.search.searchEnginesURL", "https://@APO_AM_URL@/external/searchplugins");

// Dictionary URL
pref("browser.dictionaries.download.url", "https://addons.mozilla.org/%LOCALE%/firefox/dictionaries/");

pref("extensions.update.autoUpdateDefault", true); // Automatically update extensions by default
pref("extensions.getAddons.maxResults", 10);
pref("extensions.getAddons.cache.enabled", false);

// ============================================================================

// ===| DOM |==================================================================

pref("dom.disable_window_status_change", false); //Allow status feedback by default
// Set max script runtimes to sane values
pref("dom.max_chrome_script_run_time", 90); //Some addons need ample time!
pref("dom.max_script_run_time", 20); //Should be plenty for a page script to do what it needs

// ============================================================================

// ===| Plugins |==============================================================

pref("plugin.default.state", 2); //Allow plugins to run by default
pref("plugin.expose_full_path", true); //Security: expose the full path to the plugin
pref("dom.ipc.plugins.timeoutSecs", 20);

// ============================================================================

// ===| Graphics |=============================================================

pref("nglayout.initialpaint.delay", 300);

// ============================================================================

// ===| Image |================================================================

pref("image.mem.max_ms_before_yield", 50);
pref("image.mem.decode_bytes_at_a_time", 65536); //larger chunks

// ============================================================================

// ===| Misc. |================================================================

// store sessions less frequently to prevent redundant mem usage by storing too much
pref("browser.sessionstore.interval",60000); //every minute instead of every 10 seconds

// Make sure we shortcut out of a11y to save walking unnecessary code
pref("accessibility.force_disabled", 1);

// ============================================================================
