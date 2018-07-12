#filter substitution
#filter emptyLines
#include ../../shared/pref/preferences.inc
#include ../../shared/pref/uaoverrides.inc

pref("startup.homepage_override_url","http://www.palemoon.org/unofficial.shtml");
pref("app.releaseNotesURL", "https://github.com/Feodor2/MyPal");

// Enable Firefox compatmode by default.
pref("general.useragent.compatMode", 2);

// Updates disabled
pref("app.update.enabled", false);
pref("app.update.url", "");
