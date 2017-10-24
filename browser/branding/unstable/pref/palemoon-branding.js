#filter substitution
#filter emptyLines
#include ../../shared/pref/preferences.inc
#include ../../shared/pref/uaoverrides.inc

pref("startup.homepage_override_url","http://www.palemoon.org/unstable/releasenotes.shtml");
pref("app.releaseNotesURL", "http://www.palemoon.org/unstable/releasenotes.shtml");

// Enable Firefox compatmode by default.
pref("general.useragent.compatMode", 2);

// ========================= updates ========================
#if defined(XP_WIN)
// Enable auto-updates for this channel
pref("app.update.auto", true);

// Updates enabled
pref("app.update.enabled", true);
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
// Updates disabled (Linux, etc.)
pref("app.update.enabled", false);
pref("app.update.url", "");
#endif
