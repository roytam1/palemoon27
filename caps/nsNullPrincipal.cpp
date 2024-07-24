/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set sw=2 sts=2 ts=2 et tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This is the principal that has no rights and can't be accessed by
 * anything other than itself and chrome; null principals are not
 * same-origin with anything but themselves.
 */

#include "mozilla/ArrayUtils.h"

#include "nsNullPrincipal.h"
#include "nsNullPrincipalURI.h"
#include "nsMemory.h"
#include "nsIUUIDGenerator.h"
#include "nsID.h"
#include "nsNetUtil.h"
#include "nsIClassInfoImpl.h"
#include "nsIObjectInputStream.h"
#include "nsIObjectOutputStream.h"
#include "nsNetCID.h"
#include "nsError.h"
#include "nsIScriptSecurityManager.h"
#include "nsPrincipal.h"
#include "nsScriptSecurityManager.h"
#include "pratom.h"

using namespace mozilla;

NS_IMPL_CLASSINFO(nsNullPrincipal, nullptr, nsIClassInfo::MAIN_THREAD_ONLY,
                  NS_NULLPRINCIPAL_CID)
NS_IMPL_QUERY_INTERFACE_CI(nsNullPrincipal,
                           nsIPrincipal,
                           nsISerializable)
NS_IMPL_CI_INTERFACE_GETTER(nsNullPrincipal,
                            nsIPrincipal,
                            nsISerializable)

NS_IMETHODIMP_(MozExternalRefCountType)
nsNullPrincipal::AddRef()
{
  NS_PRECONDITION(int32_t(refcount) >= 0, "illegal refcnt");
  nsrefcnt count = ++refcount;
  NS_LOG_ADDREF(this, count, "nsNullPrincipal", sizeof(*this));
  return count;
}

NS_IMETHODIMP_(MozExternalRefCountType)
nsNullPrincipal::Release()
{
  NS_PRECONDITION(0 != refcount, "dup release");
  nsrefcnt count = --refcount;
  NS_LOG_RELEASE(this, count, "nsNullPrincipal");
  if (count == 0) {
    delete this;
  }

  return count;
}

nsNullPrincipal::nsNullPrincipal()
{
}

nsNullPrincipal::~nsNullPrincipal()
{
}

/* static */ already_AddRefed<nsNullPrincipal>
nsNullPrincipal::CreateWithInheritedAttributes(nsIPrincipal* aInheritFrom)
{
  nsRefPtr<nsNullPrincipal> nullPrin = new nsNullPrincipal();
  nsresult rv = nullPrin->Init(aInheritFrom->GetAppId(),
                               aInheritFrom->GetIsInBrowserElement());
  return NS_SUCCEEDED(rv) ? nullPrin.forget() : nullptr;
}

#define NS_NULLPRINCIPAL_PREFIX NS_NULLPRINCIPAL_SCHEME ":"

nsresult
nsNullPrincipal::Init(uint32_t aAppId, bool aInMozBrowser)
{
  MOZ_ASSERT(aAppId != nsIScriptSecurityManager::UNKNOWN_APP_ID);
  mAppId = aAppId;
  mInMozBrowser = aInMozBrowser;

  nsCString str;
  nsresult rv = GenerateNullPrincipalURI(str);
  NS_ENSURE_SUCCESS(rv, rv);

  mURI = new nsNullPrincipalURI(str);

  return NS_OK;
}

void
nsNullPrincipal::GetScriptLocation(nsACString &aStr)
{
  mURI->GetSpec(aStr);
}

nsresult
nsNullPrincipal::GenerateNullPrincipalURI(nsACString &aStr)
{
  // FIXME: bug 327161 -- make sure the uuid generator is reseeding-resistant.
  nsresult rv;
  nsCOMPtr<nsIUUIDGenerator> uuidgen =
    do_GetService("@mozilla.org/uuid-generator;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsID id;
  rv = uuidgen->GenerateUUIDInPlace(&id);
  NS_ENSURE_SUCCESS(rv, rv);

  char chars[NSID_LENGTH];
  id.ToProvidedString(chars);

  uint32_t suffixLen = NSID_LENGTH - 1;
  uint32_t prefixLen = ArrayLength(NS_NULLPRINCIPAL_PREFIX) - 1;

  // Use an nsCString so we only do the allocation once here and then share
  // with nsJSPrincipals
  aStr.SetCapacity(prefixLen + suffixLen);

  aStr.Append(NS_NULLPRINCIPAL_PREFIX);
  aStr.Append(chars);

  if (aStr.Length() != prefixLen + suffixLen) {
    NS_WARNING("Out of memory allocating null-principal URI");
    return NS_ERROR_OUT_OF_MEMORY;
  }

  return NS_OK;
}

#ifdef DEBUG
void nsNullPrincipal::dumpImpl()
{
  nsAutoCString str;
  mURI->GetSpec(str);
  fprintf(stderr, "nsNullPrincipal (%p) = %s\n", this, str.get());
}
#endif 

/**
 * nsIPrincipal implementation
 */

NS_IMETHODIMP
nsNullPrincipal::Equals(nsIPrincipal *aOther, bool *aResult)
{
  // Just equal to ourselves.  Note that nsPrincipal::Equals will return false
  // for us since we have a unique domain/origin/etc.
  *aResult = (aOther == this);
  return NS_OK;
}

NS_IMETHODIMP
nsNullPrincipal::EqualsConsideringDomain(nsIPrincipal *aOther, bool *aResult)
{
  return Equals(aOther, aResult);
}

NS_IMETHODIMP
nsNullPrincipal::GetHashValue(uint32_t *aResult)
{
  *aResult = (NS_PTR_TO_INT32(this) >> 2);
  return NS_OK;
}

NS_IMETHODIMP 
nsNullPrincipal::GetURI(nsIURI** aURI)
{
  return NS_EnsureSafeToReturn(mURI, aURI);
}

NS_IMETHODIMP
nsNullPrincipal::GetCsp(nsIContentSecurityPolicy** aCsp)
{
  NS_IF_ADDREF(*aCsp = mCSP);
  return NS_OK;
}

NS_IMETHODIMP
nsNullPrincipal::SetCsp(nsIContentSecurityPolicy* aCsp)
{
  // If CSP was already set, it should not be destroyed!  Instead, it should
  // get set anew when a new principal is created.
  if (mCSP)
    return NS_ERROR_ALREADY_INITIALIZED;

  mCSP = aCsp;
  return NS_OK;
}

NS_IMETHODIMP
nsNullPrincipal::GetDomain(nsIURI** aDomain)
{
  return NS_EnsureSafeToReturn(mURI, aDomain);
}

NS_IMETHODIMP
nsNullPrincipal::SetDomain(nsIURI* aDomain)
{
  // I think the right thing to do here is to just throw...  Silently failing
  // seems counterproductive.
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP 
nsNullPrincipal::GetOrigin(char** aOrigin)
{
  *aOrigin = nullptr;
  
  nsAutoCString str;
  nsresult rv = mURI->GetSpec(str);
  NS_ENSURE_SUCCESS(rv, rv);

  *aOrigin = ToNewCString(str);
  NS_ENSURE_TRUE(*aOrigin, NS_ERROR_OUT_OF_MEMORY);

  return NS_OK;
}

NS_IMETHODIMP
nsNullPrincipal::Subsumes(nsIPrincipal *aOther, bool *aResult)
{
  // We don't subsume anything except ourselves.  Note that nsPrincipal::Equals
  // will return false for us, since we're not about:blank and not Equals to
  // reasonable nsPrincipals.
  *aResult = (aOther == this);
  return NS_OK;
}

NS_IMETHODIMP
nsNullPrincipal::SubsumesConsideringDomain(nsIPrincipal *aOther, bool *aResult)
{
  return Subsumes(aOther, aResult);
}

NS_IMETHODIMP
nsNullPrincipal::CheckMayLoad(nsIURI* aURI, bool aReport, bool aAllowIfInheritsPrincipal)
 {
  if (aAllowIfInheritsPrincipal) {
    if (nsPrincipal::IsPrincipalInherited(aURI)) {
      return NS_OK;
    }
  }

  // Also allow the load if we are the principal of the URI being checked.
  nsCOMPtr<nsIURIWithPrincipal> uriPrinc = do_QueryInterface(aURI);
  if (uriPrinc) {
    nsCOMPtr<nsIPrincipal> principal;
    uriPrinc->GetPrincipal(getter_AddRefs(principal));

    if (principal == this) {
      return NS_OK;
    }
  }

  if (aReport) {
    nsScriptSecurityManager::ReportError(
      nullptr, NS_LITERAL_STRING("CheckSameOriginError"), mURI, aURI);
  }

  return NS_ERROR_DOM_BAD_URI;
}

NS_IMETHODIMP
nsNullPrincipal::GetJarPrefix(nsACString& aJarPrefix)
{
  aJarPrefix.Truncate();
  return NS_OK;
}

NS_IMETHODIMP
nsNullPrincipal::GetAppStatus(uint16_t* aAppStatus)
{
  *aAppStatus = nsScriptSecurityManager::AppStatusForPrincipal(this);
  return NS_OK;
}

NS_IMETHODIMP
nsNullPrincipal::GetAppId(uint32_t* aAppId)
{
  *aAppId = mAppId;
  return NS_OK;
}

NS_IMETHODIMP
nsNullPrincipal::GetIsInBrowserElement(bool* aIsInBrowserElement)
{
  *aIsInBrowserElement = mInMozBrowser;
  return NS_OK;
}

NS_IMETHODIMP
nsNullPrincipal::GetUnknownAppId(bool* aUnknownAppId)
{
  *aUnknownAppId = false;
  return NS_OK;
}

NS_IMETHODIMP
nsNullPrincipal::GetIsNullPrincipal(bool* aIsNullPrincipal)
{
  *aIsNullPrincipal = true;
  return NS_OK;
}

NS_IMETHODIMP
nsNullPrincipal::GetBaseDomain(nsACString& aBaseDomain)
{
  // For a null principal, we use our unique uuid as the base domain.
  return mURI->GetPath(aBaseDomain);
}

/**
 * nsISerializable implementation
 */
NS_IMETHODIMP
nsNullPrincipal::Read(nsIObjectInputStream* aStream)
{
  // Note - nsNullPrincipal use NS_GENERIC_FACTORY_CONSTRUCTOR_INIT, which means
  // that the Init() method has already been invoked by the time we deserialize.
  // This is in contrast to nsPrincipal, which uses NS_GENERIC_FACTORY_CONSTRUCTOR,
  // in which case ::Read needs to invoke Init().
  nsresult rv = aStream->Read32(&mAppId);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aStream->ReadBoolean(&mInMozBrowser);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP
nsNullPrincipal::Write(nsIObjectOutputStream* aStream)
{
  aStream->Write32(mAppId);
  aStream->WriteBoolean(mInMozBrowser);
  return NS_OK;
}

