#! /bin/sh
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

MOZ_APP_BASENAME=PaleMoon
MOZ_APP_VENDOR=Moonchild
MOZ_UPDATER=1
MOZ_PHOENIX=1

if test "$OS_ARCH" = "WINNT"; then
  MOZ_MAINTENANCE_SERVICE=
  MOZ_VERIFY_MAR_SIGNATURE=
  if ! test "$HAVE_64BIT_BUILD"; then
    if test "$MOZ_UPDATE_CHANNEL" = "nightly" -o \
            "$MOZ_UPDATE_CHANNEL" = "aurora" -o \
            "$MOZ_UPDATE_CHANNEL" = "beta" -o \
            "$MOZ_UPDATE_CHANNEL" = "release"; then
      if ! test "$MOZ_DEBUG"; then
        MOZ_STUB_INSTALLER=
      fi
    fi
  fi
fi

MOZ_CHROME_FILE_FORMAT=omni
MOZ_DISABLE_EXPORT_JS=
MOZ_SAFE_BROWSING=
MOZ_SERVICES_COMMON=1
MOZ_SERVICES_CRYPTO=1
MOZ_SERVICES_HEALTHREPORT=
MOZ_SERVICES_METRICS=
MOZ_SERVICES_SYNC=1
MOZ_SERVICES_CLOUDSYNC=
MOZ_APP_VERSION=$FIREFOX_VERSION
MOZ_EXTENSIONS_DEFAULT=" gio"
# MOZ_APP_DISPLAYNAME will be set by branding/configure.sh
# Changing MOZ_*BRANDING_DIRECTORY requires a clobber to ensure correct results,
# because branding dependencies are broken.
# MOZ_BRANDING_DIRECTORY is the default branding directory used when none is
# specified. It should never point to the "official" branding directory.
# For mozilla-beta, mozilla-release, or mozilla-central repositories, use
# "nightly" branding (until bug 659568 is fixed).
# For the mozilla-aurora repository, use "aurora".
MOZ_BRANDING_DIRECTORY=browser/branding/nightly
MOZ_OFFICIAL_BRANDING_DIRECTORY=browser/branding/official
MOZ_APP_ID={8de7fcbb-c55c-4fbe-bfc5-fc555c87dbc4}
# This should usually be the same as the value MAR_CHANNEL_ID.
# If more than one ID is needed, then you should use a comma separated list
# of values.
ACCEPTED_MAR_CHANNEL_IDS=palemoon-tycho-alpha
# The MAR_CHANNEL_ID must not contain the following 3 characters: ",\t "
MAR_CHANNEL_ID=palemoon-tycho-alpha
MOZ_PROFILE_MIGRATOR=1
MOZ_APP_STATIC_INI=1
MOZ_WEBAPP_RUNTIME=
MOZ_MEDIA_NAVIGATOR=1
MOZ_WEBGL_CONFORMANT=1
# Enable navigator.mozPay
MOZ_PAY=
# Enable activities. These are used for FxOS developers currently.
MOZ_ACTIVITIES=
MOZ_JSDOWNLOADS=1
MOZ_WEBM_ENCODER=1
