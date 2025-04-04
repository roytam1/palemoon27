/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/DebugOnly.h"
#include "mozilla/dom/FetchDriver.h"

#include "nsIAsyncVerifyRedirectCallback.h"
#include "nsIDocument.h"
#include "nsIInputStream.h"
#include "nsIOutputStream.h"
#include "nsIHttpChannel.h"
#include "nsIHttpChannelInternal.h"
#include "nsIHttpHeaderVisitor.h"
#include "nsIScriptSecurityManager.h"
#include "nsIThreadRetargetableRequest.h"
#include "nsIUploadChannel2.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIPipe.h"

#include "nsContentPolicyUtils.h"
#include "nsCORSListenerProxy.h"
#include "nsDataHandler.h"
#include "nsHostObjectProtocolHandler.h"
#include "nsNetUtil.h"
#include "nsPrintfCString.h"
#include "nsStreamUtils.h"
#include "nsStringStream.h"
#include "nsHttpChannel.h"

#include "mozilla/dom/File.h"
#include "mozilla/dom/workers/Workers.h"
#include "mozilla/unused.h"

#include "Fetch.h"
#include "InternalRequest.h"
#include "InternalResponse.h"

namespace mozilla {
namespace dom {

NS_IMPL_ISUPPORTS(FetchDriver,
                  nsIStreamListener, nsIChannelEventSink, nsIInterfaceRequestor,
                  nsIThreadRetargetableStreamListener)

FetchDriver::FetchDriver(InternalRequest* aRequest, nsIPrincipal* aPrincipal,
                         nsILoadGroup* aLoadGroup)
  : mPrincipal(aPrincipal)
  , mLoadGroup(aLoadGroup)
  , mRequest(aRequest)
  , mResponseAvailableCalled(false)
  , mFetchCalled(false)
{
}

FetchDriver::~FetchDriver()
{
  // We assert this since even on failures, we should call
  // FailWithNetworkError().
  MOZ_ASSERT(mResponseAvailableCalled);
}

nsresult
FetchDriver::Fetch(FetchDriverObserver* aObserver)
{
  workers::AssertIsOnMainThread();
  MOZ_ASSERT(!mFetchCalled);
  mFetchCalled = true;

  mObserver = aObserver;

  Telemetry::Accumulate(Telemetry::SERVICE_WORKER_REQUEST_PASSTHROUGH,
                        mRequest->WasCreatedByFetchEvent());

  // FIXME(nsm): Deal with HSTS.

  MOZ_RELEASE_ASSERT(!mRequest->IsSynchronous(),
                     "Synchronous fetch not supported");

  return NS_DispatchToCurrentThread(NewRunnableMethod(this, &FetchDriver::ContinueFetch));
}

nsresult
FetchDriver::ContinueFetch()
{
  workers::AssertIsOnMainThread();

  nsresult rv = HttpFetch();
  if (NS_FAILED(rv)) {
    FailWithNetworkError();
  }
 
  return rv;
}

// This function implements the "HTTP Fetch" algorithm from the Fetch spec.
// Functionality is often split between here, the CORS listener proxy and the
// Necko HTTP implementation.
nsresult
FetchDriver::HttpFetch()
{
  // Step 1. "Let response be null."
  mResponse = nullptr;
  nsresult rv;

  nsCOMPtr<nsIIOService> ios = do_GetIOService(&rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString url;
  mRequest->GetURL(url);
  nsCOMPtr<nsIURI> uri;
  rv = NS_NewURI(getter_AddRefs(uri),
                          url,
                          nullptr,
                          nullptr,
                          ios);
  NS_ENSURE_SUCCESS(rv, rv);

  // Unsafe requests aren't allowed with when using no-core mode.
  if (mRequest->Mode() == RequestMode::No_cors &&
      mRequest->UnsafeRequest() &&
      (!mRequest->HasSimpleMethod() ||
       !mRequest->Headers()->HasOnlySimpleHeaders())) {
    MOZ_ASSERT(false, "The API should have caught this");
    return NS_ERROR_DOM_BAD_URI;
  }

  // non-GET requests aren't allowed for blob.
  if (IsBlobURI(uri)) {
    nsAutoCString method;
    mRequest->GetMethod(method);
    if (!method.EqualsLiteral("GET")) {
      return NS_ERROR_DOM_NETWORK_ERR;
    }
  }

  // Step 2 deals with letting ServiceWorkers intercept requests. This is
  // handled by Necko after the channel is opened.
  // FIXME(nsm): Bug 1119026: The channel's skip service worker flag should be
  // set based on the Request's flag.

  // Step 3.1 "If the CORS preflight flag is set and one of these conditions is
  // true..." is handled by the CORS proxy.
  //
  // Step 3.2 "Set request's skip service worker flag." This isn't required
  // since Necko will fall back to the network if the ServiceWorker does not
  // respond with a valid Response.
  //
  // NS_StartCORSPreflight() will automatically kick off the original request
  // if it succeeds, so we need to have everything setup for the original
  // request too.

  // Step 3.3 "Let credentials flag be set if one of
  //  - request's credentials mode is "include"
  //  - request's credentials mode is "same-origin" and either the CORS flag
  //    is unset or response tainting is "opaque"
  // is true, and unset otherwise."

  // Set skip serviceworker flag.
  // While the spec also gates on the client being a ServiceWorker, we can't
  // infer that here. Instead we rely on callers to set the flag correctly.
  const nsLoadFlags bypassFlag = mRequest->SkipServiceWorker() ?
                                 nsIChannel::LOAD_BYPASS_SERVICE_WORKER : 0;

  nsSecurityFlags secFlags = nsILoadInfo::SEC_ABOUT_BLANK_INHERITS;
  if (mRequest->Mode() == RequestMode::Cors) {
    secFlags |= nsILoadInfo::SEC_REQUIRE_CORS_DATA_INHERITS;
  } else if (mRequest->Mode() == RequestMode::Same_origin ||
             mRequest->Mode() == RequestMode::Navigate) {
    secFlags |= nsILoadInfo::SEC_REQUIRE_SAME_ORIGIN_DATA_INHERITS;
  } else if (mRequest->Mode() == RequestMode::No_cors) {
    secFlags |= nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_DATA_INHERITS;
  } else {
    MOZ_ASSERT_UNREACHABLE("Unexpected request mode!");
    return NS_ERROR_UNEXPECTED;
  }

  if (mRequest->GetRedirectMode() != RequestRedirect::Follow) {
    secFlags |= nsILoadInfo::SEC_DONT_FOLLOW_REDIRECTS;
  }

  // This is handles the use credentials flag in "HTTP
  // network or cache fetch" in the spec and decides whether to transmit
  // cookies and other identifying information.
  if (mRequest->GetCredentialsMode() == RequestCredentials::Include) {
    secFlags |= nsILoadInfo::SEC_COOKIES_INCLUDE;
  } else if (mRequest->GetCredentialsMode() == RequestCredentials::Omit) {
    secFlags |= nsILoadInfo::SEC_COOKIES_OMIT;
  } else if (mRequest->GetCredentialsMode() == RequestCredentials::Same_origin) {
    secFlags |= nsILoadInfo::SEC_COOKIES_SAME_ORIGIN;
  } else {
    MOZ_ASSERT_UNREACHABLE("Unexpected credentials mode!");
    return NS_ERROR_UNEXPECTED;
  }

  // From here on we create a channel and set its properties with the
  // information from the InternalRequest. This is an implementation detail.
  MOZ_ASSERT(mLoadGroup);
  nsCOMPtr<nsIChannel> chan;

  nsLoadFlags loadFlags = nsIRequest::LOAD_NORMAL |
    bypassFlag | nsIChannel::LOAD_CLASSIFY_URI;
  if (mDocument) {
    MOZ_ASSERT(mDocument->NodePrincipal() == mPrincipal);
    rv = NS_NewChannel(getter_AddRefs(chan),
                       uri,
                       mDocument,
                       secFlags,
                       mRequest->ContentPolicyType(),
                       mLoadGroup,
                       nullptr, /* aCallbacks */
                       loadFlags,
                       ios);
  } else {
    rv = NS_NewChannel(getter_AddRefs(chan),
                       uri,
                       mPrincipal,
                       secFlags,
                       mRequest->ContentPolicyType(),
                       mLoadGroup,
                       nullptr, /* aCallbacks */
                       loadFlags,
                       ios);
  }
  NS_ENSURE_SUCCESS(rv, rv);

  mLoadGroup = nullptr;

  // Insert ourselves into the notification callbacks chain so we can set
  // headers on redirects.
#ifdef DEBUG
  {
    nsCOMPtr<nsIInterfaceRequestor> notificationCallbacks;
    chan->GetNotificationCallbacks(getter_AddRefs(notificationCallbacks));
    MOZ_ASSERT(!notificationCallbacks);
  }
#endif
  chan->SetNotificationCallbacks(this);

  // Step 3.5 begins "HTTP network or cache fetch".
  // HTTP network or cache fetch
  // ---------------------------
  // Step 1 "Let HTTPRequest..." The channel is the HTTPRequest.
  nsCOMPtr<nsIHttpChannel> httpChan = do_QueryInterface(chan);
  if (httpChan) {
    // Copy the method.
    nsAutoCString method;
    mRequest->GetMethod(method);
    rv = httpChan->SetRequestMethod(method);
    NS_ENSURE_SUCCESS(rv, rv);

    // Set the same headers.
    SetRequestHeaders(httpChan);

    // Step 2. Set the referrer.
    nsAutoString referrer;
    mRequest->GetReferrer(referrer);
    ReferrerPolicy referrerPolicy = mRequest->ReferrerPolicy_();
    net::ReferrerPolicy net_referrerPolicy = net::RP_Unset;
    switch (referrerPolicy) {
    case ReferrerPolicy::_empty:
      net_referrerPolicy = net::RP_Default;
      break;
    case ReferrerPolicy::No_referrer:
      net_referrerPolicy = net::RP_No_Referrer;
      break;
    case ReferrerPolicy::No_referrer_when_downgrade:
      net_referrerPolicy = net::RP_No_Referrer_When_Downgrade;
      break;
    case ReferrerPolicy::Origin:
      net_referrerPolicy = net::RP_Origin;
      break;
    case ReferrerPolicy::Origin_when_cross_origin:
      net_referrerPolicy = net::RP_Origin_When_Crossorigin;
      break;
    case ReferrerPolicy::Unsafe_url:
      net_referrerPolicy = net::RP_Unsafe_URL;
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("Invalid ReferrerPolicy enum value?");
      break;
    }
    if (referrer.EqualsLiteral(kFETCH_CLIENT_REFERRER_STR)) {
      rv = nsContentUtils::SetFetchReferrerURIWithPolicy(mPrincipal,
                                                         mDocument,
                                                         httpChan,
                                                         net_referrerPolicy);
      NS_ENSURE_SUCCESS(rv, rv);
    } else if (referrer.IsEmpty()) {
      rv = httpChan->SetReferrerWithPolicy(nullptr, net::RP_No_Referrer);
      NS_ENSURE_SUCCESS(rv, rv);
    } else {
      // From "Determine request's Referrer" step 3
      // "If request's referrer is a URL, let referrerSource be request's
      // referrer."
      nsCOMPtr<nsIURI> referrerURI;
      rv = NS_NewURI(getter_AddRefs(referrerURI), referrer, nullptr, nullptr);
      NS_ENSURE_SUCCESS(rv, rv);

      uint32_t documentReferrerPolicy = mDocument ? mDocument->GetReferrerPolicy() :
                                                    net::RP_Default;
      rv =
        httpChan->SetReferrerWithPolicy(referrerURI,
                                        referrerPolicy == ReferrerPolicy::_empty ?
                                          documentReferrerPolicy :
                                          net_referrerPolicy);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    // Bug 1120722 - Authorization will be handled later.
    // Auth may require prompting, we don't support it yet.
    // The next patch in this same bug prevents this from aborting the request.
    // Credentials checks for CORS are handled by nsCORSListenerProxy,

    nsCOMPtr<nsIHttpChannelInternal> internalChan = do_QueryInterface(httpChan);

    // Conversion between enumerations is safe due to static asserts in
    // dom/workers/ServiceWorkerManager.cpp
    internalChan->SetCorsMode(static_cast<uint32_t>(mRequest->Mode()));
    internalChan->SetRedirectMode(static_cast<uint32_t>(mRequest->GetRedirectMode()));
    mRequest->MaybeSkipCacheIfPerformingRevalidation();
    internalChan->SetFetchCacheMode(static_cast<uint32_t>(mRequest->GetCacheMode()));
  }

  // Step 5. Proxy authentication will be handled by Necko.

  // Continue setting up 'HTTPRequest'. Content-Type and body data.
  nsCOMPtr<nsIUploadChannel2> uploadChan = do_QueryInterface(chan);
  if (uploadChan) {
    nsAutoCString contentType;
    ErrorResult result;
    mRequest->Headers()->Get(NS_LITERAL_CSTRING("content-type"), contentType, result);
    // This is an error because the Request constructor explicitly extracts and
    // sets a content-type per spec.
    if (result.Failed()) {
      return result.StealNSResult();
    }

    nsCOMPtr<nsIInputStream> bodyStream;
    mRequest->GetBody(getter_AddRefs(bodyStream));
    if (bodyStream) {
      nsAutoCString method;
      mRequest->GetMethod(method);
      rv = uploadChan->ExplicitSetUploadStream(bodyStream, contentType, -1, method, false /* aStreamHasHeaders */);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  // If preflight is required, start a "CORS preflight fetch"
  // https://fetch.spec.whatwg.org/#cors-preflight-fetch-0. All the
  // implementation is handled by the http channel calling into
  // nsCORSListenerProxy. We just inform it which unsafe headers are included
  // in the request.
  if (mRequest->Mode() == RequestMode::Cors) {
    AutoTArray<nsCString, 5> unsafeHeaders;
    mRequest->Headers()->GetUnsafeHeaders(unsafeHeaders);
    nsCOMPtr<nsILoadInfo> loadInfo = chan->GetLoadInfo();
    loadInfo->SetCorsPreflightInfo(unsafeHeaders, false);
  }

  rv = chan->AsyncOpen2(this);
  NS_ENSURE_SUCCESS(rv, rv);

  // Step 4 onwards of "HTTP Fetch" is handled internally by Necko.
  return NS_OK;
}

already_AddRefed<InternalResponse>
FetchDriver::BeginAndGetFilteredResponse(InternalResponse* aResponse,
                                         nsIURI* aFinalURI,
                                         bool aFoundOpaqueRedirect)
{
  MOZ_ASSERT(aResponse);
  nsAutoCString reqURL;
  if (aFinalURI) {
    aFinalURI->GetSpec(reqURL);
  } else {
    mRequest->GetURL(reqURL);
  }
  DebugOnly<nsresult> rv = aResponse->StripFragmentAndSetUrl(reqURL);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  RefPtr<InternalResponse> filteredResponse;
  if (aFoundOpaqueRedirect) {
    filteredResponse = aResponse->OpaqueRedirectResponse();
  } else {
    switch (mRequest->GetResponseTainting()) {
      case LoadTainting::Basic:
        filteredResponse = aResponse->BasicResponse();
        break;
      case LoadTainting::CORS:
        filteredResponse = aResponse->CORSResponse();
        break;
      case LoadTainting::Opaque:
        filteredResponse = aResponse->OpaqueResponse();
        break;
      default:
        MOZ_CRASH("Unexpected case");
    }
  }

  MOZ_ASSERT(filteredResponse);
  MOZ_ASSERT(mObserver);
  mObserver->OnResponseAvailable(filteredResponse);
  mResponseAvailableCalled = true;
  return filteredResponse.forget();
}

void
FetchDriver::FailWithNetworkError()
{
  workers::AssertIsOnMainThread();
  RefPtr<InternalResponse> error = InternalResponse::NetworkError();
  if (mObserver) {
    mObserver->OnResponseAvailable(error);
    mResponseAvailableCalled = true;
    mObserver->OnResponseEnd();
    mObserver = nullptr;
  }
}

namespace {
class FillResponseHeaders final : public nsIHttpHeaderVisitor {
  InternalResponse* mResponse;

  ~FillResponseHeaders()
  { }
public:
  NS_DECL_ISUPPORTS

  explicit FillResponseHeaders(InternalResponse* aResponse)
    : mResponse(aResponse)
  {
  }

  NS_IMETHOD
  VisitHeader(const nsACString & aHeader, const nsACString & aValue) override
  {
    ErrorResult result;
    mResponse->Headers()->Append(aHeader, aValue, result);
    if (result.Failed()) {
      NS_WARNING(nsPrintfCString("Fetch ignoring illegal header - '%s': '%s'",
                                 PromiseFlatCString(aHeader).get(),
                                 PromiseFlatCString(aValue).get()).get());
      result.SuppressException();
    }
    return NS_OK;
  }
};

NS_IMPL_ISUPPORTS(FillResponseHeaders, nsIHttpHeaderVisitor)
} // namespace

NS_IMETHODIMP
FetchDriver::OnStartRequest(nsIRequest* aRequest,
                            nsISupports* aContext)
{
  workers::AssertIsOnMainThread();

  // Note, this can be called multiple times if we are doing an opaqueredirect.
  // In that case we will get a simulated OnStartRequest() and then the real
  // channel will call in with an errored OnStartRequest().

  nsresult rv;
  aRequest->GetStatus(&rv);
  if (NS_FAILED(rv)) {
    FailWithNetworkError();
    return rv;
  }

  // We should only get to the following code once.
  MOZ_ASSERT(!mPipeOutputStream);
  MOZ_ASSERT(mObserver);

  RefPtr<InternalResponse> response;
  nsCOMPtr<nsIChannel> channel = do_QueryInterface(aRequest);
  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aRequest);

  // On a successful redirect we perform the following substeps of HTTP Fetch,
  // step 5, "redirect status", step 11.

  // Step 11.5 "Append locationURL to request's url list." so that when we set the
  // Response's URL from the Request's URL in Main Fetch, step 15, we get the
  // final value. Note, we still use a single URL value instead of a list.
  // Because of that we only need to do this after the request finishes.
  nsCOMPtr<nsIURI> newURI;
  rv = NS_GetFinalChannelURI(channel, getter_AddRefs(newURI));
  if (NS_FAILED(rv)) {
    FailWithNetworkError();
    return rv;
  }
  nsAutoCString newUrl;
  newURI->GetSpec(newUrl);
  mRequest->SetURL(newUrl);

  bool foundOpaqueRedirect = false;

  if (httpChannel) {
    uint32_t responseStatus;
    httpChannel->GetResponseStatus(&responseStatus);

    if (mozilla::net::nsHttpChannel::IsRedirectStatus(responseStatus)) {
      if (mRequest->GetRedirectMode() == RequestRedirect::Error) {
        FailWithNetworkError();
        return NS_BINDING_FAILED;
      }
      if (mRequest->GetRedirectMode() == RequestRedirect::Manual) {
        foundOpaqueRedirect = true;
      }
    }

    nsAutoCString statusText;
    httpChannel->GetResponseStatusText(statusText);

    response = new InternalResponse(responseStatus, statusText);

    RefPtr<FillResponseHeaders> visitor = new FillResponseHeaders(response);
    rv = httpChannel->VisitResponseHeaders(visitor);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      NS_WARNING("Failed to visit all headers.");
    }
  } else {
    response = new InternalResponse(200, NS_LITERAL_CSTRING("OK"));

    ErrorResult result;
    nsAutoCString contentType;
    rv = channel->GetContentType(contentType);
    if (NS_SUCCEEDED(rv) && !contentType.IsEmpty()) {
      nsAutoCString contentCharset;
      channel->GetContentCharset(contentCharset);
      if (NS_SUCCEEDED(rv) && !contentCharset.IsEmpty()) {
        contentType += NS_LITERAL_CSTRING(";charset=") + contentCharset;
      }

      response->Headers()->Append(NS_LITERAL_CSTRING("Content-Type"),
                                  contentType,
                                  result);
      MOZ_ASSERT(!result.Failed());
    }

    int64_t contentLength;
    rv = channel->GetContentLength(&contentLength);
    if (NS_SUCCEEDED(rv) && contentLength) {
      nsAutoCString contentLenStr;
      contentLenStr.AppendInt(contentLength);
      response->Headers()->Append(NS_LITERAL_CSTRING("Content-Length"),
                                  contentLenStr,
                                  result);
      MOZ_ASSERT(!result.Failed());
    }
  }

  // We open a pipe so that we can immediately set the pipe's read end as the
  // response's body. Setting the segment size to UINT32_MAX means that the
  // pipe has infinite space. The nsIChannel will continue to buffer data in
  // xpcom events even if we block on a fixed size pipe.  It might be possible
  // to suspend the channel and then resume when there is space available, but
  // for now use an infinite pipe to avoid blocking.
  nsCOMPtr<nsIInputStream> pipeInputStream;
  rv = NS_NewPipe(getter_AddRefs(pipeInputStream),
                  getter_AddRefs(mPipeOutputStream),
                  0, /* default segment size */
                  UINT32_MAX /* infinite pipe */,
                  true /* non-blocking input, otherwise you deadlock */,
                  false /* blocking output, since the pipe is 'in'finite */ );
  if (NS_WARN_IF(NS_FAILED(rv))) {
    FailWithNetworkError();
    // Cancel request.
    return rv;
  }
  response->SetBody(pipeInputStream);

  response->InitChannelInfo(channel);

  nsCOMPtr<nsIURI> channelURI;
  rv = channel->GetURI(getter_AddRefs(channelURI));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    FailWithNetworkError();
    // Cancel request.
    return rv;
  }

  nsCOMPtr<nsILoadInfo> loadInfo;
  rv = channel->GetLoadInfo(getter_AddRefs(loadInfo));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    FailWithNetworkError();
    return rv;
  }

  // Propagate any tainting from the channel back to our response here.  This
  // step is not reflected in the spec because the spec is written such that
  // FetchEvent.respondWith() just passes the already-tainted Response back to
  // the outer fetch().  In gecko, however, we serialize the Response through
  // the channel and must regenerate the tainting from the channel in the
  // interception case.
  mRequest->MaybeIncreaseResponseTainting(loadInfo->GetTainting());

  // Resolves fetch() promise which may trigger code running in a worker.  Make
  // sure the Response is fully initialized before calling this.
  mResponse = BeginAndGetFilteredResponse(response, channelURI,
                                          foundOpaqueRedirect);

  nsCOMPtr<nsIEventTarget> sts = do_GetService(NS_STREAMTRANSPORTSERVICE_CONTRACTID, &rv);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    FailWithNetworkError();
    // Cancel request.
    return rv;
  }

  // Try to retarget off main thread.
  if (nsCOMPtr<nsIThreadRetargetableRequest> rr = do_QueryInterface(aRequest)) {
    NS_WARN_IF(NS_FAILED(rr->RetargetDeliveryTo(sts)));
  }
  return NS_OK;
}

NS_IMETHODIMP
FetchDriver::OnDataAvailable(nsIRequest* aRequest,
                             nsISupports* aContext,
                             nsIInputStream* aInputStream,
                             uint64_t aOffset,
                             uint32_t aCount)
{
  // NB: This can be called on any thread!  But we're guaranteed that it is
  // called between OnStartRequest and OnStopRequest, so we don't need to worry
  // about races.

  uint32_t aRead;
  MOZ_ASSERT(mResponse);
  MOZ_ASSERT(mPipeOutputStream);

  nsresult rv = aInputStream->ReadSegments(NS_CopySegmentToStream,
                                           mPipeOutputStream,
                                           aCount, &aRead);
  return rv;
}

NS_IMETHODIMP
FetchDriver::OnStopRequest(nsIRequest* aRequest,
                           nsISupports* aContext,
                           nsresult aStatusCode)
{
  workers::AssertIsOnMainThread();
  if (NS_FAILED(aStatusCode)) {
    nsCOMPtr<nsIAsyncOutputStream> outputStream = do_QueryInterface(mPipeOutputStream);
    if (outputStream) {
      outputStream->CloseWithStatus(NS_BINDING_FAILED);
    }

    // We proceed as usual here, since we've already created a successful response
    // from OnStartRequest.
  } else {
    MOZ_ASSERT(mResponse);
    MOZ_ASSERT(!mResponse->IsError());

    if (mPipeOutputStream) {
      mPipeOutputStream->Close();
    }
  }

  if (mObserver) {
    mObserver->OnResponseEnd();
    mObserver = nullptr;
  }

  return NS_OK;
}

NS_IMETHODIMP
FetchDriver::AsyncOnChannelRedirect(nsIChannel* aOldChannel,
                                    nsIChannel* aNewChannel,
                                    uint32_t aFlags,
                                    nsIAsyncVerifyRedirectCallback *aCallback)
{
  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aNewChannel);
  if (httpChannel) {
    SetRequestHeaders(httpChannel);
  }

  aCallback->OnRedirectVerifyCallback(NS_OK);
  return NS_OK;
}

NS_IMETHODIMP
FetchDriver::CheckListenerChain()
{
  return NS_OK;
}

NS_IMETHODIMP
FetchDriver::GetInterface(const nsIID& aIID, void **aResult)
{
  if (aIID.Equals(NS_GET_IID(nsIChannelEventSink))) {
    *aResult = static_cast<nsIChannelEventSink*>(this);
    NS_ADDREF_THIS();
    return NS_OK;
  }
  if (aIID.Equals(NS_GET_IID(nsIStreamListener))) {
    *aResult = static_cast<nsIStreamListener*>(this);
    NS_ADDREF_THIS();
    return NS_OK;
  }
  if (aIID.Equals(NS_GET_IID(nsIRequestObserver))) {
    *aResult = static_cast<nsIRequestObserver*>(this);
    NS_ADDREF_THIS();
    return NS_OK;
  }

  return QueryInterface(aIID, aResult);
}

void
FetchDriver::SetDocument(nsIDocument* aDocument)
{
  // Cannot set document after Fetch() has been called.
  MOZ_ASSERT(!mFetchCalled);
  mDocument = aDocument;
}

void
FetchDriver::SetRequestHeaders(nsIHttpChannel* aChannel) const
{
  MOZ_ASSERT(aChannel);

  AutoTArray<InternalHeaders::Entry, 5> headers;
  mRequest->Headers()->GetEntries(headers);
  bool hasAccept = false;
  for (uint32_t i = 0; i < headers.Length(); ++i) {
    if (!hasAccept && headers[i].mName.EqualsLiteral("accept")) {
      hasAccept = true;
    }
    if (headers[i].mValue.IsEmpty()) {
      aChannel->SetEmptyRequestHeader(headers[i].mName);
    } else {
      aChannel->SetRequestHeader(headers[i].mName, headers[i].mValue, false /* merge */);
    }
  }

  if (!hasAccept) {
    aChannel->SetRequestHeader(NS_LITERAL_CSTRING("accept"),
                               NS_LITERAL_CSTRING("*/*"),
                               false /* merge */);
  }

  if (mRequest->ForceOriginHeader()) {
    nsAutoString origin;
    if (NS_SUCCEEDED(nsContentUtils::GetUTFOrigin(mPrincipal, origin))) {
      aChannel->SetRequestHeader(NS_LITERAL_CSTRING("origin"),
                                 NS_ConvertUTF16toUTF8(origin),
                                 false /* merge */);
    }
  }
}

} // namespace dom
} // namespace mozilla
