/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCertOverrideService.h"

#include "NSSCertDBTrustDomain.h"
#include "ScopedNSSTypes.h"
#include "SharedSSLState.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsCRT.h"
#include "nsILineInputStream.h"
#include "nsIObserver.h"
#include "nsIObserverService.h"
#include "nsIX509Cert.h"
#include "nsNSSCertHelper.h"
#include "nsNSSCertificate.h"
#include "nsNSSComponent.h"
#include "nsNetUtil.h"
#include "nsISafeOutputStream.h"
#include "nsPromiseFlatString.h"
#include "nsStreamUtils.h"
#include "nsStringBuffer.h"
#include "nsThreadUtils.h"
#include "ssl.h" // For SSL_ClearSessionCache

using namespace mozilla;
using namespace mozilla::psm;

#define CERT_OVERRIDE_FILE_NAME "cert_override.txt"

void
nsCertOverride::convertBitsToString(OverrideBits ob, nsACString &str)
{
  str.Truncate();

  if (ob & ob_Mismatch)
    str.Append('M');

  if (ob & ob_Untrusted)
    str.Append('U');

  if (ob & ob_Time_error)
    str.Append('T');
}

void
nsCertOverride::convertStringToBits(const nsACString &str, OverrideBits &ob)
{
  const nsPromiseFlatCString &flat = PromiseFlatCString(str);
  const char *walk = flat.get();

  ob = ob_None;

  for ( ; *walk; ++walk)
  {
    switch (*walk)
    {
      case 'm':
      case 'M':
        ob = (OverrideBits)(ob | ob_Mismatch);
        break;

      case 'u':
      case 'U':
        ob = (OverrideBits)(ob | ob_Untrusted);
        break;

      case 't':
      case 'T':
        ob = (OverrideBits)(ob | ob_Time_error);
        break;

      default:
        break;
    }
  }
}

NS_IMPL_ISUPPORTS(nsCertOverrideService,
                  nsICertOverrideService,
                  nsIObserver,
                  nsISupportsWeakReference)

nsCertOverrideService::nsCertOverrideService()
  : monitor("nsCertOverrideService.monitor")
{
}

nsCertOverrideService::~nsCertOverrideService()
{
}

nsresult
nsCertOverrideService::Init()
{
  if (!NS_IsMainThread()) {
    NS_NOTREACHED("nsCertOverrideService initialized off main thread");
    return NS_ERROR_NOT_SAME_THREAD;
  }

  // Note that the names of these variables would seem to indicate that at one
  // point another hash algorithm was used and is still supported for backwards
  // compatibility. This is not the case. It has always been SHA256.
  mOidTagForStoringNewHashes = SEC_OID_SHA256;
  mDottedOidForStoringNewHashes.Assign("OID.2.16.840.1.101.3.4.2.1");

  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();

  // If we cannot add ourselves as a profile change observer, then we will not
  // attempt to read/write any settings file. Otherwise, we would end up
  // reading/writing the wrong settings file after a profile change.
  if (observerService) {
    observerService->AddObserver(this, "profile-before-change", true);
    observerService->AddObserver(this, "profile-do-change", true);
    // simulate a profile change so we read the current profile's settings file
    Observe(nullptr, "profile-do-change", nullptr);
  }

  SharedSSLState::NoteCertOverrideServiceInstantiated();
  return NS_OK;
}

NS_IMETHODIMP
nsCertOverrideService::Observe(nsISupports     *,
                               const char      *aTopic,
                               const char16_t *aData)
{
  // check the topic
  if (!nsCRT::strcmp(aTopic, "profile-before-change")) {
    // The profile is about to change,
    // or is going away because the application is shutting down.

    RemoveAllFromMemory();
  } else if (!nsCRT::strcmp(aTopic, "profile-do-change")) {
    // The profile has already changed.
    // Now read from the new profile location.
    // we also need to update the cached file location

    ReentrantMonitorAutoEnter lock(monitor);

    nsresult rv = NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR, getter_AddRefs(mSettingsFile));
    if (NS_SUCCEEDED(rv)) {
      mSettingsFile->AppendNative(NS_LITERAL_CSTRING(CERT_OVERRIDE_FILE_NAME));
    } else {
      mSettingsFile = nullptr;
    }
    Read();
  }

  return NS_OK;
}

void
nsCertOverrideService::RemoveAllFromMemory()
{
  ReentrantMonitorAutoEnter lock(monitor);
  mSettingsTable.Clear();
}

void
nsCertOverrideService::RemoveAllTemporaryOverrides()
{
  ReentrantMonitorAutoEnter lock(monitor);
  for (auto iter = mSettingsTable.Iter(); !iter.Done(); iter.Next()) {
    nsCertOverrideEntry *entry = iter.Get();
    if (entry->mSettings.mIsTemporary) {
      entry->mSettings.mCert = nullptr;
      iter.Remove();
    }
  }
  // no need to write, as temporaries are never written to disk
}

nsresult
nsCertOverrideService::Read()
{
  ReentrantMonitorAutoEnter lock(monitor);

  // If we don't have a profile, then we won't try to read any settings file.
  if (!mSettingsFile)
    return NS_OK;

  nsresult rv;
  nsCOMPtr<nsIInputStream> fileInputStream;
  rv = NS_NewLocalFileInputStream(getter_AddRefs(fileInputStream), mSettingsFile);
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsCOMPtr<nsILineInputStream> lineInputStream = do_QueryInterface(fileInputStream, &rv);
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsAutoCString buffer;
  bool isMore = true;
  int32_t hostIndex = 0, algoIndex, fingerprintIndex, overrideBitsIndex, dbKeyIndex;

  /* file format is:
   *
   * host:port \t fingerprint-algorithm \t fingerprint \t override-mask \t dbKey
   *
   *   where override-mask is a sequence of characters,
   *     M meaning hostname-Mismatch-override
   *     U meaning Untrusted-override
   *     T meaning Time-error-override (expired/not yet valid) 
   *
   * if this format isn't respected we move onto the next line in the file.
   */

  while (isMore && NS_SUCCEEDED(lineInputStream->ReadLine(buffer, &isMore))) {
    if (buffer.IsEmpty() || buffer.First() == '#') {
      continue;
    }

    // this is a cheap, cheesy way of parsing a tab-delimited line into
    // string indexes, which can be lopped off into substrings. just for
    // purposes of obfuscation, it also checks that each token was found.
    // todo: use iterators?
    if ((algoIndex         = buffer.FindChar('\t', hostIndex)         + 1) == 0 ||
        (fingerprintIndex  = buffer.FindChar('\t', algoIndex)         + 1) == 0 ||
        (overrideBitsIndex = buffer.FindChar('\t', fingerprintIndex)  + 1) == 0 ||
        (dbKeyIndex        = buffer.FindChar('\t', overrideBitsIndex) + 1) == 0) {
      continue;
    }

    const nsASingleFragmentCString &tmp = Substring(buffer, hostIndex, algoIndex - hostIndex - 1);
    const nsASingleFragmentCString &algo_string = Substring(buffer, algoIndex, fingerprintIndex - algoIndex - 1);
    const nsASingleFragmentCString &fingerprint = Substring(buffer, fingerprintIndex, overrideBitsIndex - fingerprintIndex - 1);
    const nsASingleFragmentCString &bits_string = Substring(buffer, overrideBitsIndex, dbKeyIndex - overrideBitsIndex - 1);
    const nsASingleFragmentCString &db_key = Substring(buffer, dbKeyIndex, buffer.Length() - dbKeyIndex);

    nsAutoCString host(tmp);
    nsCertOverride::OverrideBits bits;
    nsCertOverride::convertStringToBits(bits_string, bits);

    int32_t port;
    int32_t portIndex = host.RFindChar(':');
    if (portIndex == kNotFound)
      continue; // Ignore broken entries

    nsresult portParseError;
    nsAutoCString portString(Substring(host, portIndex+1));
    port = portString.ToInteger(&portParseError);
    if (NS_FAILED(portParseError))
      continue; // Ignore broken entries

    host.Truncate(portIndex);
    
    AddEntryToList(host, port, 
                   nullptr, // don't have the cert
                   false, // not temporary
                   algo_string, fingerprint, bits, db_key);
  }

  return NS_OK;
}

nsresult
nsCertOverrideService::Write()
{
  ReentrantMonitorAutoEnter lock(monitor);

  // If we don't have any profile, then we won't try to write any file
  if (!mSettingsFile) {
    return NS_OK;
  }

  nsresult rv;
  nsCOMPtr<nsIOutputStream> fileOutputStream;
  rv = NS_NewSafeLocalFileOutputStream(getter_AddRefs(fileOutputStream),
                                       mSettingsFile,
                                       -1,
                                       0600);
  if (NS_FAILED(rv)) {
    NS_ERROR("failed to open cert_warn_settings.txt for writing");
    return rv;
  }

  // get a buffered output stream 4096 bytes big, to optimize writes
  nsCOMPtr<nsIOutputStream> bufferedOutputStream;
  rv = NS_NewBufferedOutputStream(getter_AddRefs(bufferedOutputStream), fileOutputStream, 4096);
  if (NS_FAILED(rv)) {
    return rv;
  }

  static const char kHeader[] =
      "# PSM Certificate Override Settings file" NS_LINEBREAK
      "# This is a generated file!  Do not edit." NS_LINEBREAK;

  /* see ::Read for file format */

  uint32_t unused;
  bufferedOutputStream->Write(kHeader, sizeof(kHeader) - 1, &unused);

  static const char kTab[] = "\t";
  for (auto iter = mSettingsTable.Iter(); !iter.Done(); iter.Next()) {
    nsCertOverrideEntry *entry = iter.Get();

    const nsCertOverride &settings = entry->mSettings;
    if (settings.mIsTemporary) {
      continue;
    }

    nsAutoCString bits_string;
    nsCertOverride::convertBitsToString(settings.mOverrideBits, bits_string);

    bufferedOutputStream->Write(entry->mHostWithPort.get(),
                                entry->mHostWithPort.Length(), &unused);
    bufferedOutputStream->Write(kTab, sizeof(kTab) - 1, &unused);
    bufferedOutputStream->Write(settings.mFingerprintAlgOID.get(),
                                settings.mFingerprintAlgOID.Length(), &unused);
    bufferedOutputStream->Write(kTab, sizeof(kTab) - 1, &unused);
    bufferedOutputStream->Write(settings.mFingerprint.get(),
                                settings.mFingerprint.Length(), &unused);
    bufferedOutputStream->Write(kTab, sizeof(kTab) - 1, &unused);
    bufferedOutputStream->Write(bits_string.get(),
                                bits_string.Length(), &unused);
    bufferedOutputStream->Write(kTab, sizeof(kTab) - 1, &unused);
    bufferedOutputStream->Write(settings.mDBKey.get(),
                                settings.mDBKey.Length(), &unused);
    bufferedOutputStream->Write(NS_LINEBREAK, NS_LINEBREAK_LEN, &unused);
  }

  // All went ok. Maybe except for problems in Write(), but the stream detects
  // that for us
  nsCOMPtr<nsISafeOutputStream> safeStream = do_QueryInterface(bufferedOutputStream);
  NS_ASSERTION(safeStream, "expected a safe output stream!");
  if (safeStream) {
    rv = safeStream->Finish();
    if (NS_FAILED(rv)) {
      NS_WARNING("failed to save cert warn settings file! possible dataloss");
      return rv;
    }
  }

  return NS_OK;
}

static nsresult
GetCertFingerprintByOidTag(nsIX509Cert *aCert,
                           SECOidTag aOidTag, 
                           nsCString &fp)
{
  UniqueCERTCertificate nsscert(aCert->GetCert());
  if (!nsscert) {
    return NS_ERROR_FAILURE;
  }
  return GetCertFingerprintByOidTag(nsscert.get(), aOidTag, fp);
}

NS_IMETHODIMP
nsCertOverrideService::RememberValidityOverride(const nsACString& aHostName,
                                                int32_t aPort,
                                                nsIX509Cert* aCert,
                                                uint32_t aOverrideBits,
                                                bool aTemporary)
{
  NS_ENSURE_ARG_POINTER(aCert);
  if (aHostName.IsEmpty())
    return NS_ERROR_INVALID_ARG;
  if (aPort < -1)
    return NS_ERROR_INVALID_ARG;

  UniqueCERTCertificate nsscert(aCert->GetCert());
  if (!nsscert) {
    return NS_ERROR_FAILURE;
  }

  nsAutoCString nickname;
  nsresult rv = DefaultServerNicknameForCert(nsscert.get(), nickname);
  if (!aTemporary && NS_SUCCEEDED(rv)) {
    UniquePK11SlotInfo slot(PK11_GetInternalKeySlot());
    if (!slot) {
      return NS_ERROR_FAILURE;
    }

    SECStatus srv = PK11_ImportCert(slot.get(), nsscert.get(), CK_INVALID_HANDLE,
                                    nickname.get(), false);
    if (srv != SECSuccess) {
      return NS_ERROR_FAILURE;
    }
  }

  nsAutoCString fpStr;
  rv = GetCertFingerprintByOidTag(nsscert.get(), mOidTagForStoringNewHashes,
                                  fpStr);
  if (NS_FAILED(rv))
    return rv;

  nsAutoCString dbkey;
  rv = aCert->GetDbKey(dbkey);
  if (NS_FAILED(rv)) {
    return rv;
  }

  {
    ReentrantMonitorAutoEnter lock(monitor);
    AddEntryToList(aHostName, aPort,
                   aTemporary ? aCert : nullptr,
                     // keep a reference to the cert for temporary overrides
                   aTemporary, 
                   mDottedOidForStoringNewHashes, fpStr, 
                   (nsCertOverride::OverrideBits)aOverrideBits, 
                   dbkey);
    Write();
  }

  return NS_OK;
}

NS_IMETHODIMP
nsCertOverrideService::HasMatchingOverride(const nsACString & aHostName, int32_t aPort,
                                           nsIX509Cert *aCert, 
                                           uint32_t *aOverrideBits,
                                           bool *aIsTemporary,
                                           bool *_retval)
{
  if (aHostName.IsEmpty())
    return NS_ERROR_INVALID_ARG;
  if (aPort < -1)
    return NS_ERROR_INVALID_ARG;

  NS_ENSURE_ARG_POINTER(aCert);
  NS_ENSURE_ARG_POINTER(aOverrideBits);
  NS_ENSURE_ARG_POINTER(aIsTemporary);
  NS_ENSURE_ARG_POINTER(_retval);
  *_retval = false;
  *aOverrideBits = nsCertOverride::ob_None;

  nsAutoCString hostPort;
  GetHostWithPort(aHostName, aPort, hostPort);
  nsCertOverride settings;

  {
    ReentrantMonitorAutoEnter lock(monitor);
    nsCertOverrideEntry *entry = mSettingsTable.GetEntry(hostPort.get());
  
    if (!entry)
      return NS_OK;
  
    settings = entry->mSettings; // copy
  }

  *aOverrideBits = settings.mOverrideBits;
  *aIsTemporary = settings.mIsTemporary;

  nsAutoCString fpStr;
  nsresult rv;

  // This code was originally written in a way that suggested that other hash
  // algorithms are supported for backwards compatibility. However, this was
  // always unnecessary, because only SHA256 has ever been used here.
  if (settings.mFingerprintAlgOID.Equals(mDottedOidForStoringNewHashes)) {
    rv = GetCertFingerprintByOidTag(aCert, mOidTagForStoringNewHashes, fpStr);
    if (NS_FAILED(rv)) {
      return rv;
    }
  } else {
    return NS_ERROR_UNEXPECTED;
  }

  *_retval = settings.mFingerprint.Equals(fpStr);
  return NS_OK;
}

NS_IMETHODIMP
nsCertOverrideService::GetValidityOverride(const nsACString & aHostName, int32_t aPort,
                                           nsACString & aHashAlg, 
                                           nsACString & aFingerprint, 
                                           uint32_t *aOverrideBits,
                                           bool *aIsTemporary,
                                           bool *_found)
{
  NS_ENSURE_ARG_POINTER(_found);
  NS_ENSURE_ARG_POINTER(aIsTemporary);
  NS_ENSURE_ARG_POINTER(aOverrideBits);
  *_found = false;
  *aOverrideBits = nsCertOverride::ob_None;

  nsAutoCString hostPort;
  GetHostWithPort(aHostName, aPort, hostPort);
  nsCertOverride settings;

  {
    ReentrantMonitorAutoEnter lock(monitor);
    nsCertOverrideEntry *entry = mSettingsTable.GetEntry(hostPort.get());
  
    if (entry) {
      *_found = true;
      settings = entry->mSettings; // copy
    }
  }

  if (*_found) {
    *aOverrideBits = settings.mOverrideBits;
    *aIsTemporary = settings.mIsTemporary;
    aFingerprint = settings.mFingerprint;
    aHashAlg = settings.mFingerprintAlgOID;
  }

  return NS_OK;
}

nsresult
nsCertOverrideService::AddEntryToList(const nsACString &aHostName, int32_t aPort,
                                      nsIX509Cert *aCert,
                                      const bool aIsTemporary,
                                      const nsACString &fingerprintAlgOID, 
                                      const nsACString &fingerprint,
                                      nsCertOverride::OverrideBits ob,
                                      const nsACString &dbKey)
{
  nsAutoCString hostPort;
  GetHostWithPort(aHostName, aPort, hostPort);

  {
    ReentrantMonitorAutoEnter lock(monitor);
    nsCertOverrideEntry *entry = mSettingsTable.PutEntry(hostPort.get());

    if (!entry) {
      NS_ERROR("can't insert a null entry!");
      return NS_ERROR_OUT_OF_MEMORY;
    }

    entry->mHostWithPort = hostPort;

    nsCertOverride &settings = entry->mSettings;
    settings.mAsciiHost = aHostName;
    settings.mPort = aPort;
    settings.mIsTemporary = aIsTemporary;
    settings.mFingerprintAlgOID = fingerprintAlgOID;
    settings.mFingerprint = fingerprint;
    settings.mOverrideBits = ob;
    settings.mDBKey = dbKey;
    // remove whitespace from stored dbKey for backwards compatibility
    settings.mDBKey.StripWhitespace();
    settings.mCert = aCert;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsCertOverrideService::ClearValidityOverride(const nsACString & aHostName, int32_t aPort)
{
  if (aPort == 0 &&
      aHostName.EqualsLiteral("all:temporary-certificates")) {
    RemoveAllTemporaryOverrides();
    return NS_OK;
  }
  nsAutoCString hostPort;
  GetHostWithPort(aHostName, aPort, hostPort);
  {
    ReentrantMonitorAutoEnter lock(monitor);
    mSettingsTable.RemoveEntry(hostPort.get());
    Write();
  }

  if (EnsureNSSInitialized(nssEnsure)) {
    SSL_ClearSessionCache();
  } else {
    return NS_ERROR_NOT_AVAILABLE;
  }

  return NS_OK;
}

static bool
matchesDBKey(nsIX509Cert* cert, const nsCString& matchDbKey)
{
  nsAutoCString dbKey;
  nsresult rv = cert->GetDbKey(dbKey);
  if (NS_FAILED(rv)) {
    return false;
  }
  return dbKey.Equals(matchDbKey);
}

NS_IMETHODIMP
nsCertOverrideService::IsCertUsedForOverrides(nsIX509Cert *aCert, 
                                              bool aCheckTemporaries,
                                              bool aCheckPermanents,
                                              uint32_t *_retval)
{
  NS_ENSURE_ARG(aCert);
  NS_ENSURE_ARG(_retval);

  uint32_t counter = 0;
  {
    ReentrantMonitorAutoEnter lock(monitor);
    for (auto iter = mSettingsTable.Iter(); !iter.Done(); iter.Next()) {
      const nsCertOverride &settings = iter.Get()->mSettings;
      bool still_ok = true;

      if (( settings.mIsTemporary && !aCheckTemporaries) ||
          (!settings.mIsTemporary && !aCheckPermanents)) {
        still_ok = false;
      }

      if (still_ok && matchesDBKey(aCert, settings.mDBKey)) {
        nsAutoCString cert_fingerprint;
        nsresult rv = NS_ERROR_UNEXPECTED;
        if (settings.mFingerprintAlgOID.Equals(mDottedOidForStoringNewHashes)) {
          rv = GetCertFingerprintByOidTag(aCert,
                 mOidTagForStoringNewHashes, cert_fingerprint);
        }
        if (NS_SUCCEEDED(rv) &&
            settings.mFingerprint.Equals(cert_fingerprint)) {
          counter++;
        }
      }
    }
  }
  *_retval = counter;
  return NS_OK;
}

nsresult
nsCertOverrideService::EnumerateCertOverrides(nsIX509Cert *aCert,
                         CertOverrideEnumerator aEnumerator,
                         void *aUserData)
{
  ReentrantMonitorAutoEnter lock(monitor);
  for (auto iter = mSettingsTable.Iter(); !iter.Done(); iter.Next()) {
    const nsCertOverride &settings = iter.Get()->mSettings;

    if (!aCert) {
      aEnumerator(settings, aUserData);
    } else {
      if (matchesDBKey(aCert, settings.mDBKey)) {
        nsAutoCString cert_fingerprint;
        nsresult rv = NS_ERROR_UNEXPECTED;
        if (settings.mFingerprintAlgOID.Equals(mDottedOidForStoringNewHashes)) {
          rv = GetCertFingerprintByOidTag(aCert,
                 mOidTagForStoringNewHashes, cert_fingerprint);
        }
        if (NS_SUCCEEDED(rv) &&
            settings.mFingerprint.Equals(cert_fingerprint)) {
          aEnumerator(settings, aUserData);
        }
      }
    }
  }
  return NS_OK;
}

void
nsCertOverrideService::GetHostWithPort(const nsACString & aHostName, int32_t aPort, nsACString& _retval)
{
  nsAutoCString hostPort(aHostName);
  if (aPort == -1) {
    aPort = 443;
  }
  if (!hostPort.IsEmpty()) {
    hostPort.Append(':');
    hostPort.AppendInt(aPort);
  }
  _retval.Assign(hostPort);
}
