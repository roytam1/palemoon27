#filter substitution
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

"use strict";

Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");
XPCOMUtils.defineLazyModuleGetter(this, "Services", "resource://gre/modules/Services.jsm");

this.EXPORTED_SYMBOLS = ["AppConstants"];

// Immutable for export.
let AppConstants = Object.freeze({
  // See this wiki page for more details about channel specific build
  // defines: https://wiki.mozilla.org/Platform/Channel-specific_build_defines
  NIGHTLY_BUILD:
#ifdef NIGHTLY_BUILD
  true,
#else
  false,
#endif

  RELEASE_BUILD:
#ifdef RELEASE_BUILD
  true,
#else
  false,
#endif

  ACCESSIBILITY:
#ifdef ACCESSIBILITY
  true,
#else
  false,
#endif

  // Official corresponds, roughly, to whether this build is performed
  // on Mozilla's continuous integration infrastructure. You should
  // disable developer-only functionality when this flag is set.
  MOZILLA_OFFICIAL:
#ifdef MOZILLA_OFFICIAL
  true,
#else
  false,
#endif

  MOZ_OFFICIAL_BRANDING:
#ifdef MOZ_OFFICIAL_BRANDING
  true,
#else
  false,
#endif

  MOZ_DEV_EDITION:
#ifdef MOZ_DEV_EDITION
  true,
#else
  false,
#endif

  MOZ_DEV_EDITION:
#ifdef MOZ_DEV_EDITION
  true,
#else
  false,
#endif

  MOZ_SERVICES_HEALTHREPORT:
#ifdef MOZ_SERVICES_HEALTHREPORT
  true,
#else
  false,
#endif

  MOZ_DATA_REPORTING:
#ifdef MOZ_DATA_REPORTING
  true,
#else
  false,
#endif

  MOZ_DEVICES:
#ifdef MOZ_DEVICES
  true,
#else
  false,
#endif

  MOZ_TELEMETRY_REPORTING:
#ifdef MOZ_TELEMETRY_REPORTING
  true,
#else
  false,
#endif

  MOZ_WEBRTC:
#ifdef MOZ_WEBRTC
  true,
#else
  false,
#endif

  MOZ_B2G_RIL:
#ifdef MOZ_B2G_RIL
  true,
#else
  false,
#endif

  platform:
#ifdef MOZ_WIDGET_GTK
  "linux",
#elif MOZ_WIDGET_QT
  "linux",
#elif XP_WIN
  "win",
#elif XP_MACOSX
  "macosx",
#elif MOZ_WIDGET_ANDROID
  "android",
#elif MOZ_WIDGET_GONK
  "gonk",
#else
  "other",
#endif

  isPlatformAndVersionAtLeast(platform, version) {
    let platformVersion = Services.sysinfo.getProperty("version");
    return platform == this.platform &&
           Services.vc.compare(platformVersion, version) >= 0;
  },

  isPlatformAndVersionAtMost(platform, version) {
    let platformVersion = Services.sysinfo.getProperty("version");
    return platform == this.platform &&
           Services.vc.compare(platformVersion, version) <= 0;
  },

  MOZ_CRASHREPORTER:
#ifdef MOZ_CRASHREPORTER
  true,
#else
  false,
#endif

  MOZ_APP_VERSION: "@MOZ_APP_VERSION@",

  ANDROID_PACKAGE_NAME: "@ANDROID_PACKAGE_NAME@",
});
