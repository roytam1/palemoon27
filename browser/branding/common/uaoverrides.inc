#define GUAO_PREF general.useragent.override

#define GRE_VERSION 3.0
#define GRE_VERSION_SLICE Goanna/@GRE_VERSION@
#define GRE_DATE_SLICE Goanna/20160101
#define PM_SLICE PaleMoon/@MOZ_APP_VERSION@

#define GK_VERSION 45.9
#define GK_SLICE Gecko/20100101
#define FX_SLICE Firefox/@GK_VERSION@

#ifdef XP_UNIX
#ifndef XP_MACOSX
#define OS_SLICE X11; Linux x86_64;
#else
#define OS_SLICE Macintosh; Intel Mac OS X 10.11;
#endif
#else
#define OS_SLICE Windows NT 6.1; WOW64;
#endif

// Required for domains that have proven unresponsive to requests from users
pref("@GUAO_PREF@.live.com","Mozilla/5.0 (@OS_SLICE@ rv:@GK_VERSION@) @GK_SLICE@ @FX_SLICE@ (Pale Moon)");
pref("@GUAO_PREF@.outlook.com","Mozilla/5.0 (@OS_SLICE@ rv:@GK_VERSION@) @GK_SLICE@ @FX_SLICE@ (Pale Moon)");
pref("@GUAO_PREF@.web.de","Mozilla/5.0 (@OS_SLICE@ rv:@GK_VERSION@) @GK_SLICE@ @FX_SLICE@ (Pale Moon)");
pref("@GUAO_PREF@.aol.com","Mozilla/5.0 (@OS_SLICE@ rv:@GK_VERSION@) @GK_SLICE@ @FX_SLICE@ (Pale Moon)");
pref("@GUAO_PREF@.calendar.yahoo.com","Mozilla/5.0 (@OS_SLICE@ rv:@GK_VERSION@) @GK_SLICE@ @FX_SLICE@ (Pale Moon)");
pref("@GUAO_PREF@.google.com","Mozilla/5.0 (@OS_SLICE@ rv:@GK_VERSION@) @GK_SLICE@ @GRE_VERSION_SLICE@ @FX_SLICE@ @PM_SLICE@");
pref("@GUAO_PREF@.googlevideos.com","Mozilla/5.0 (@OS_SLICE@ rv:38.9) @GK_SLICE@ @GRE_VERSION_SLICE@ Firefox/38.9 @PM_SLICE@");
pref("@GUAO_PREF@.gstatic.com","Mozilla/5.0 (@OS_SLICE@ rv:31.9) @GK_SLICE@ @GRE_VERSION_SLICE@ Firefox/31.9 @PM_SLICE@");
pref("@GUAO_PREF@.youtube.com","Mozilla/5.0 (@OS_SLICE@ rv:42.0) @GK_SLICE@ @GRE_VERSION_SLICE@ Firefox/42.0 @PM_SLICE@");
pref("@GUAO_PREF@.netflix.com","Mozilla/5.0 (@OS_SLICE@ rv:38.9) @GK_SLICE@ Firefox/38.9");

// UA-Sniffing domains below are pending responses from their operators - temp workaround
pref("@GUAO_PREF@.facebook.com","Mozilla/5.0 (@OS_SLICE@ rv:@GK_VERSION@) @GK_SLICE@ @GRE_VERSION_SLICE@ @FX_SLICE@ @PM_SLICE@");
pref("@GUAO_PREF@.fbcdn.com","Mozilla/5.0 (@OS_SLICE@ rv:@GK_VERSION@) @GK_SLICE@ @GRE_VERSION_SLICE@ @FX_SLICE@ @PM_SLICE@");
pref("@GUAO_PREF@.chase.com","Mozilla/5.0 (@OS_SLICE@ rv:@GK_VERSION@) @GK_SLICE@ @FX_SLICE@");
// Citi requires native mode. Or it blocks.. "too old firefox"
pref("@GUAO_PREF@.citi.com","Mozilla/5.0 (@OS_SLICE@ rv:@GRE_VERSION@) @GRE_DATE_SLICE@ @PM_SLICE@");
// Yuku fora don't like the Goanna slice (result: broken mobile site)
pref("@GUAO_PREF@.yuku.com","Mozilla/5.0 (@OS_SLICE@ rv:@GK_VERSION@) @GK_SLICE@ @FX_SLICE@ @PM_SLICE@");

// UA-Sniffing domains below have indicated no interest in supporting Pale Moon (BOO!)
pref("@GUAO_PREF@.icloud.com","Mozilla/5.0 (@OS_SLICE@ rv:@GK_VERSION@) @GK_SLICE@ @FX_SLICE@ (Pale Moon)");
pref("@GUAO_PREF@.humblebundle.com","Mozilla/5.0 (@OS_SLICE@ rv:@GK_VERSION@) @GK_SLICE@ @FX_SLICE@ (Pale Moon)");
pref("@GUAO_PREF@.privat24.ua","Mozilla/5.0 (@OS_SLICE@ rv:38.0) @GK_SLICE@ Firefox/38.0");

// UA-sniffing domains that are "app/vendor-specific" and don't like Pale Moon
pref("@GUAO_PREF@.web.whatsapp.com","Mozilla/5.0 (Windows NT 6.3; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/37.0.2049.0 Safari/537.36");

// The following domains don't like the Goanna slice
pref("@GUAO_PREF@.hitbox.tv","Mozilla/5.0 (@OS_SLICE@; rv:@GK_VERSION@) @GK_SLICE@ @FX_SLICE@");

