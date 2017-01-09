#filter substitution
#filter emptyLines
#include ../../shared/pref/preferences.inc
#include ../../shared/pref/uaoverrides.inc

pref("startup.homepage_override_url","http://www.palemoon.org/unstable/releasenotes.shtml");
pref("app.releaseNotesURL", "http://www.palemoon.org/unstable/releasenotes.shtml");

// Enable Firefox compatmode by default.
pref("general.useragent.compatMode", 2);

// Enable auto-updates for this channel
pref("app.update.auto", true);
pref("app.update.interval", 86400);
pref("app.update.promptWaitTime", 10800);
