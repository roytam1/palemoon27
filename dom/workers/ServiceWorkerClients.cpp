/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ServiceWorkerClients.h"

#include "mozilla/dom/Promise.h"
#include "mozilla/dom/PromiseWorkerProxy.h"

#include "ServiceWorkerClient.h"
#include "ServiceWorkerManager.h"
#include "ServiceWorkerPrivate.h"
#include "ServiceWorkerWindowClient.h"

#include "WorkerPrivate.h"
#include "WorkerRunnable.h"
#include "WorkerScope.h"

#include "nsContentUtils.h"
#include "nsIBrowserDOMWindow.h"
#include "nsIDocShell.h"
#include "nsIDOMChromeWindow.h"
#include "nsIDOMWindow.h"
#include "nsIWebNavigation.h"
#include "nsIWebProgress.h"
#include "nsIWebProgressListener.h"
#include "nsIWindowMediator.h"
#include "nsIWindowWatcher.h"
#include "nsNetUtil.h"
#include "nsPIWindowWatcher.h"
#include "nsWindowWatcher.h"
#include "nsWeakReference.h"

using namespace mozilla;
using namespace mozilla::dom;
using namespace mozilla::dom::workers;

NS_IMPL_CYCLE_COLLECTING_ADDREF(ServiceWorkerClients)
NS_IMPL_CYCLE_COLLECTING_RELEASE(ServiceWorkerClients)
NS_IMPL_CYCLE_COLLECTION_WRAPPERCACHE(ServiceWorkerClients, mWorkerScope)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(ServiceWorkerClients)
  NS_WRAPPERCACHE_INTERFACE_MAP_ENTRY
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

ServiceWorkerClients::ServiceWorkerClients(ServiceWorkerGlobalScope* aWorkerScope)
  : mWorkerScope(aWorkerScope)
{
  MOZ_ASSERT(mWorkerScope);
}

JSObject*
ServiceWorkerClients::WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto)
{
  return ClientsBinding::Wrap(aCx, this, aGivenProto);
}

namespace {

class GetRunnable final : public Runnable
{
  class ResolvePromiseWorkerRunnable final : public WorkerRunnable
  {
    RefPtr<PromiseWorkerProxy> mPromiseProxy;
    UniquePtr<ServiceWorkerClientInfo> mValue;
    nsresult mRv;

  public:
    ResolvePromiseWorkerRunnable(WorkerPrivate* aWorkerPrivate,
                                 PromiseWorkerProxy* aPromiseProxy,
                                 UniquePtr<ServiceWorkerClientInfo>&& aValue,
                                 nsresult aRv)
      : WorkerRunnable(aWorkerPrivate, WorkerThreadModifyBusyCount),
        mPromiseProxy(aPromiseProxy),
        mValue(Move(aValue)),
        mRv(Move(aRv))
    {
      AssertIsOnMainThread();
    }

    bool
    WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate)
    {
      MOZ_ASSERT(aWorkerPrivate);
      aWorkerPrivate->AssertIsOnWorkerThread();

      Promise* promise = mPromiseProxy->WorkerPromise();
      MOZ_ASSERT(promise);

      if (NS_FAILED(mRv)) {
        promise->MaybeReject(mRv);
      } else if (mValue) {
        RefPtr<ServiceWorkerWindowClient> windowClient =
          new ServiceWorkerWindowClient(promise->GetParentObject(), *mValue);
        promise->MaybeResolve(windowClient.get());
      } else {
        promise->MaybeResolve(JS::UndefinedHandleValue);
      }
      mPromiseProxy->CleanUp();
      return true;
    }
  };

  RefPtr<PromiseWorkerProxy> mPromiseProxy;
  nsString mClientId;
public:
  GetRunnable(PromiseWorkerProxy* aPromiseProxy,
              const nsAString& aClientId)
    : mPromiseProxy(aPromiseProxy),
      mClientId(aClientId)
  {
  }

  NS_IMETHOD
  Run() override
  {
    AssertIsOnMainThread();

    MutexAutoLock lock(mPromiseProxy->Lock());
    if (mPromiseProxy->CleanedUp()) {
      return NS_OK;
    }

    WorkerPrivate* workerPrivate = mPromiseProxy->GetWorkerPrivate();
    MOZ_ASSERT(workerPrivate);

    RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
    ErrorResult rv;
    UniquePtr<ServiceWorkerClientInfo> result = swm->GetClient(workerPrivate->GetPrincipal(),
                                                               mClientId, rv);
    RefPtr<ResolvePromiseWorkerRunnable> r =
      new ResolvePromiseWorkerRunnable(mPromiseProxy->GetWorkerPrivate(),
                                       mPromiseProxy, Move(result),
                                       rv.StealNSResult());
    rv.SuppressException();

    r->Dispatch();
    return NS_OK;
  }
};

class MatchAllRunnable final : public Runnable
{
  class ResolvePromiseWorkerRunnable final : public WorkerRunnable
  {
    RefPtr<PromiseWorkerProxy> mPromiseProxy;
    nsTArray<ServiceWorkerClientInfo> mValue;

  public:
    ResolvePromiseWorkerRunnable(WorkerPrivate* aWorkerPrivate,
                                 PromiseWorkerProxy* aPromiseProxy,
                                 nsTArray<ServiceWorkerClientInfo>& aValue)
      : WorkerRunnable(aWorkerPrivate, WorkerThreadModifyBusyCount),
        mPromiseProxy(aPromiseProxy)
    {
      AssertIsOnMainThread();
      mValue.SwapElements(aValue);
    }

    bool
    WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate)
    {
      MOZ_ASSERT(aWorkerPrivate);
      aWorkerPrivate->AssertIsOnWorkerThread();

      Promise* promise = mPromiseProxy->WorkerPromise();
      MOZ_ASSERT(promise);

      nsTArray<RefPtr<ServiceWorkerClient>> ret;
      for (size_t i = 0; i < mValue.Length(); i++) {
        ret.AppendElement(RefPtr<ServiceWorkerClient>(
              new ServiceWorkerWindowClient(promise->GetParentObject(),
                                            mValue.ElementAt(i))));
      }

      promise->MaybeResolve(ret);
      mPromiseProxy->CleanUp();
      return true;
    }
  };

  RefPtr<PromiseWorkerProxy> mPromiseProxy;
  nsCString mScope;
  bool mIncludeUncontrolled;
public:
  MatchAllRunnable(PromiseWorkerProxy* aPromiseProxy,
                   const nsCString& aScope,
                   bool aIncludeUncontrolled)
    : mPromiseProxy(aPromiseProxy),
      mScope(aScope),
      mIncludeUncontrolled(aIncludeUncontrolled)
  {
    MOZ_ASSERT(mPromiseProxy);
  }

  NS_IMETHOD
  Run() override
  {
    AssertIsOnMainThread();

    MutexAutoLock lock(mPromiseProxy->Lock());
    if (mPromiseProxy->CleanedUp()) {
      return NS_OK;
    }

    RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
    nsTArray<ServiceWorkerClientInfo> result;

    swm->GetAllClients(mPromiseProxy->GetWorkerPrivate()->GetPrincipal(), mScope,
                       mIncludeUncontrolled, result);
    RefPtr<ResolvePromiseWorkerRunnable> r =
      new ResolvePromiseWorkerRunnable(mPromiseProxy->GetWorkerPrivate(),
                                       mPromiseProxy, result);

    r->Dispatch();
    return NS_OK;
  }
};

class ResolveClaimRunnable final : public WorkerRunnable
{
  RefPtr<PromiseWorkerProxy> mPromiseProxy;
  nsresult mResult;

public:
  ResolveClaimRunnable(WorkerPrivate* aWorkerPrivate,
                       PromiseWorkerProxy* aPromiseProxy,
                       nsresult aResult)
    : WorkerRunnable(aWorkerPrivate, WorkerThreadModifyBusyCount)
    , mPromiseProxy(aPromiseProxy)
    , mResult(aResult)
  {
    AssertIsOnMainThread();
  }

  bool
  WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override
  {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();

    RefPtr<Promise> promise = mPromiseProxy->WorkerPromise();
    MOZ_ASSERT(promise);

    if (NS_SUCCEEDED(mResult)) {
      promise->MaybeResolve(JS::UndefinedHandleValue);
    } else {
      promise->MaybeReject(NS_ERROR_DOM_INVALID_STATE_ERR);
    }

    mPromiseProxy->CleanUp();
    return true;
  }
};

class ClaimRunnable final : public Runnable
{
  RefPtr<PromiseWorkerProxy> mPromiseProxy;
  nsCString mScope;
  uint64_t mServiceWorkerID;

public:
  ClaimRunnable(PromiseWorkerProxy* aPromiseProxy, const nsCString& aScope)
    : mPromiseProxy(aPromiseProxy)
    , mScope(aScope)
    // Safe to call GetWorkerPrivate() since we are being called on the worker
    // thread via script (so no clean up has occured yet).
    , mServiceWorkerID(aPromiseProxy->GetWorkerPrivate()->ServiceWorkerID())
  {
    MOZ_ASSERT(aPromiseProxy);
  }

  NS_IMETHOD
  Run() override
  {
    MutexAutoLock lock(mPromiseProxy->Lock());
    if (mPromiseProxy->CleanedUp()) {
      return NS_OK;
    }

    WorkerPrivate* workerPrivate = mPromiseProxy->GetWorkerPrivate();
    MOZ_ASSERT(workerPrivate);

    RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
    MOZ_ASSERT(swm);

    nsresult rv = swm->ClaimClients(workerPrivate->GetPrincipal(),
                                    mScope, mServiceWorkerID);

    RefPtr<ResolveClaimRunnable> r =
      new ResolveClaimRunnable(workerPrivate, mPromiseProxy, rv);

    r->Dispatch();
    return NS_OK;
  }
};

class ResolveOpenWindowRunnable final : public WorkerRunnable
{
public:
  ResolveOpenWindowRunnable(PromiseWorkerProxy* aPromiseProxy,
                            UniquePtr<ServiceWorkerClientInfo>&& aClientInfo,
                            const nsresult aStatus)
  : WorkerRunnable(aPromiseProxy->GetWorkerPrivate(), WorkerThreadModifyBusyCount)
  , mPromiseProxy(aPromiseProxy)
  , mClientInfo(Move(aClientInfo))
  , mStatus(aStatus)
  {
    AssertIsOnMainThread();
    MOZ_ASSERT(aPromiseProxy);
  }

  bool
  WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate)
  {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();

    Promise* promise = mPromiseProxy->WorkerPromise();
    if (NS_WARN_IF(NS_FAILED(mStatus))) {
      promise->MaybeReject(mStatus);
    } else if (mClientInfo) {
      RefPtr<ServiceWorkerWindowClient> client =
        new ServiceWorkerWindowClient(promise->GetParentObject(),
                                      *mClientInfo);
      promise->MaybeResolve(client);
    } else {
      promise->MaybeResolve(JS::NullHandleValue);
    }

    mPromiseProxy->CleanUp();
    return true;
  }

private:
  RefPtr<PromiseWorkerProxy> mPromiseProxy;
  UniquePtr<ServiceWorkerClientInfo> mClientInfo;
  const nsresult mStatus;
};

class WebProgressListener final : public nsIWebProgressListener,
                                  public nsSupportsWeakReference
{
public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(WebProgressListener, nsIWebProgressListener)

  WebProgressListener(PromiseWorkerProxy* aPromiseProxy,
                      ServiceWorkerPrivate* aServiceWorkerPrivate,
                      nsPIDOMWindow* aWindow,
                      nsIURI* aBaseURI)
  : mPromiseProxy(aPromiseProxy)
  , mServiceWorkerPrivate(aServiceWorkerPrivate)
  , mWindow(aWindow)
  , mBaseURI(aBaseURI)
  {
    MOZ_ASSERT(aPromiseProxy);
    MOZ_ASSERT(aServiceWorkerPrivate);
    MOZ_ASSERT(aWindow);
    MOZ_ASSERT(aWindow->IsOuterWindow());
    MOZ_ASSERT(aBaseURI);
    AssertIsOnMainThread();

    mServiceWorkerPrivate->StoreISupports(static_cast<nsIWebProgressListener*>(this));
  }

  NS_IMETHOD
  OnStateChange(nsIWebProgress* aWebProgress,
                nsIRequest* aRequest,
                uint32_t aStateFlags, nsresult aStatus) override
  {
    if (!(aStateFlags & STATE_IS_DOCUMENT) ||
         !(aStateFlags & (STATE_STOP | STATE_TRANSFERRING))) {
      return NS_OK;
    }

    // Our caller keeps a strong reference, so it is safe to remove the listener
    // from ServiceWorkerPrivate.
    mServiceWorkerPrivate->RemoveISupports(static_cast<nsIWebProgressListener*>(this));
    aWebProgress->RemoveProgressListener(this);

    MutexAutoLock lock(mPromiseProxy->Lock());
    if (mPromiseProxy->CleanedUp()) {
      return NS_OK;
    }

    nsCOMPtr<nsIDocument> doc = mWindow->GetExtantDoc();
    UniquePtr<ServiceWorkerClientInfo> clientInfo;
    if (doc) {
      // Check same origin.
      nsCOMPtr<nsIScriptSecurityManager> securityManager =
        nsContentUtils::GetSecurityManager();
      nsresult rv = securityManager->CheckSameOriginURI(doc->GetOriginalURI(),
                                                        mBaseURI, false);
      if (NS_SUCCEEDED(rv)) {
        clientInfo.reset(new ServiceWorkerClientInfo(doc));
      }
    }

    RefPtr<ResolveOpenWindowRunnable> r =
      new ResolveOpenWindowRunnable(mPromiseProxy,
                                    Move(clientInfo),
                                    NS_OK);
    r->Dispatch();

    return NS_OK;
  }

  NS_IMETHOD
  OnProgressChange(nsIWebProgress* aWebProgress,
                   nsIRequest* aRequest,
                   int32_t aCurSelfProgress,
                   int32_t aMaxSelfProgress,
                   int32_t aCurTotalProgress,
                   int32_t aMaxTotalProgress) override
  {
    MOZ_ASSERT(false, "Unexpected notification.");
    return NS_OK;
  }

  NS_IMETHOD
  OnLocationChange(nsIWebProgress* aWebProgress,
                   nsIRequest* aRequest,
                   nsIURI* aLocation,
                   uint32_t aFlags) override
  {
    MOZ_ASSERT(false, "Unexpected notification.");
    return NS_OK;
  }

  NS_IMETHOD
  OnStatusChange(nsIWebProgress* aWebProgress,
                 nsIRequest* aRequest,
                 nsresult aStatus, const char16_t* aMessage) override
  {
    MOZ_ASSERT(false, "Unexpected notification.");
    return NS_OK;
  }

  NS_IMETHOD
  OnSecurityChange(nsIWebProgress* aWebProgress,
                   nsIRequest* aRequest,
                   uint32_t aState) override
  {
    MOZ_ASSERT(false, "Unexpected notification.");
    return NS_OK;
  }

private:
  ~WebProgressListener()
  { }

  RefPtr<PromiseWorkerProxy> mPromiseProxy;
  RefPtr<ServiceWorkerPrivate> mServiceWorkerPrivate;
  nsCOMPtr<nsPIDOMWindow> mWindow;
  nsCOMPtr<nsIURI> mBaseURI;
};

NS_IMPL_CYCLE_COLLECTING_ADDREF(WebProgressListener)
NS_IMPL_CYCLE_COLLECTING_RELEASE(WebProgressListener)
NS_IMPL_CYCLE_COLLECTION(WebProgressListener, mPromiseProxy,
                         mServiceWorkerPrivate, mWindow)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(WebProgressListener)
  NS_INTERFACE_MAP_ENTRY(nsIWebProgressListener)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
NS_INTERFACE_MAP_END

class OpenWindowRunnable final : public Runnable
{
  RefPtr<PromiseWorkerProxy> mPromiseProxy;
  nsString mUrl;
  nsString mScope;

public:
  OpenWindowRunnable(PromiseWorkerProxy* aPromiseProxy,
                     const nsAString& aUrl,
                     const nsAString& aScope)
    : mPromiseProxy(aPromiseProxy)
    , mUrl(aUrl)
    , mScope(aScope)
  {
    MOZ_ASSERT(aPromiseProxy);
    MOZ_ASSERT(aPromiseProxy->GetWorkerPrivate());
    aPromiseProxy->GetWorkerPrivate()->AssertIsOnWorkerThread();
  }

  NS_IMETHOD
  Run() override
  {
    AssertIsOnMainThread();

    MutexAutoLock lock(mPromiseProxy->Lock());
    if (mPromiseProxy->CleanedUp()) {
      return NS_OK;
    }

    nsCOMPtr<nsPIDOMWindow> window;
    nsresult rv = OpenWindow(getter_AddRefs(window));
    if (NS_SUCCEEDED(rv)) {
      MOZ_ASSERT(window);

      rv = nsContentUtils::DispatchFocusChromeEvent(window);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }

      WorkerPrivate* workerPrivate = mPromiseProxy->GetWorkerPrivate();
      MOZ_ASSERT(workerPrivate);

      WorkerPrivate::LocationInfo& info = workerPrivate->GetLocationInfo();
      nsCOMPtr<nsIURI> baseURI;
      nsresult rv = NS_NewURI(getter_AddRefs(baseURI), info.mHref);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return NS_ERROR_FAILURE;
      }

      nsCOMPtr<nsIDocShell> docShell = window->GetDocShell();
      nsCOMPtr<nsIWebProgress> webProgress = do_GetInterface(docShell);

      if (!webProgress) {
        return NS_ERROR_FAILURE;
      }

      RefPtr<ServiceWorkerManager> swm = ServiceWorkerManager::GetInstance();
      MOZ_ASSERT(swm);

      nsCOMPtr<nsIPrincipal> principal = workerPrivate->GetPrincipal();
      MOZ_ASSERT(principal);
      RefPtr<ServiceWorkerRegistrationInfo> registration =
        swm->GetRegistration(principal, NS_ConvertUTF16toUTF8(mScope));
      if (NS_WARN_IF(!registration)) {
        return NS_ERROR_FAILURE;
      }
      RefPtr<ServiceWorkerInfo> serviceWorkerInfo =
        registration->GetServiceWorkerInfoById(workerPrivate->ServiceWorkerID());
      if (NS_WARN_IF(!serviceWorkerInfo)) {
        return NS_ERROR_FAILURE;
      }

      nsCOMPtr<nsIWebProgressListener> listener =
        new WebProgressListener(mPromiseProxy, serviceWorkerInfo->WorkerPrivate(),
                                window, baseURI);

      rv = webProgress->AddProgressListener(listener,
                                            nsIWebProgress::NOTIFY_STATE_DOCUMENT);
      MOZ_ASSERT(NS_SUCCEEDED(rv));
      return NS_OK;
    }

    RefPtr<ResolveOpenWindowRunnable> resolveRunnable =
      new ResolveOpenWindowRunnable(mPromiseProxy, nullptr, rv);

    NS_WARN_IF(!resolveRunnable->Dispatch());

    return NS_OK;
  }

private:
  nsresult
  OpenWindow(nsPIDOMWindow** aWindow)
  {
    WorkerPrivate* workerPrivate = mPromiseProxy->GetWorkerPrivate();

    // [[1. Let url be the result of parsing url with entry settings object's API
    //   base URL.]]
    nsCOMPtr<nsIURI> uri;
    WorkerPrivate::LocationInfo& info = workerPrivate->GetLocationInfo();

    nsCOMPtr<nsIURI> baseURI;
    nsresult rv = NS_NewURI(getter_AddRefs(baseURI), info.mHref);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return NS_ERROR_TYPE_ERR;
    }

    rv = NS_NewURI(getter_AddRefs(uri), mUrl, nullptr, baseURI);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return NS_ERROR_TYPE_ERR;
    }

    // [[6.1 Open Window]]
    nsCOMPtr<nsIWindowMediator> wm = do_GetService(NS_WINDOWMEDIATOR_CONTRACTID,
                                                   &rv);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    if (XRE_IsContentProcess()) {
      // ContentProcess
      nsCOMPtr<nsIWindowWatcher> wwatch =
        do_GetService(NS_WINDOWWATCHER_CONTRACTID, &rv);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
      nsCOMPtr<nsPIWindowWatcher> pwwatch(do_QueryInterface(wwatch));
      NS_ENSURE_STATE(pwwatch);

      nsCString spec;
      uri->GetSpec(spec);

      nsCOMPtr<nsIDOMWindow> newWindow;
      pwwatch->OpenWindow2(nullptr,
                           spec.get(),
                           nullptr,
                           nullptr,
                           false, false, true, nullptr, nullptr, nullptr, 1.0f, 0,
                           getter_AddRefs(newWindow));
      nsCOMPtr<nsPIDOMWindow> pwindow = do_QueryInterface(newWindow);
      pwindow.forget(aWindow);
      return NS_OK;
    }

    // Find the most recent browser window and open a new tab in it.
    nsCOMPtr<nsPIDOMWindow> browserWindow =
      nsContentUtils::GetMostRecentNonPBWindow();
    if (!browserWindow) {
      // It is possible to be running without a browser window on Mac OS, so
      // we need to open a new chrome window.
      // TODO(catalinb): open new chrome window. Bug 1218080
      return NS_ERROR_FAILURE;
    }

    nsCOMPtr<nsIDOMChromeWindow> chromeWin = do_QueryInterface(browserWindow);
    if (NS_WARN_IF(!chromeWin)) {
      return NS_ERROR_FAILURE;
    }

    nsCOMPtr<nsIBrowserDOMWindow> bwin;
    chromeWin->GetBrowserDOMWindow(getter_AddRefs(bwin));

    if (NS_WARN_IF(!bwin)) {
      return NS_ERROR_FAILURE;
    }

    nsCOMPtr<nsIDOMWindow> win;
    rv = bwin->OpenURI(uri, nullptr,
                       nsIBrowserDOMWindow::OPEN_DEFAULTWINDOW,
                       nsIBrowserDOMWindow::OPEN_NEW,
                       getter_AddRefs(win));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
    NS_ENSURE_STATE(win);

    nsCOMPtr<nsPIDOMWindow> pWin = do_QueryInterface(win);
    pWin = pWin->GetOuterWindow();
    pWin.forget(aWindow);

    return NS_OK;
  }
};

} // namespace

already_AddRefed<Promise>
ServiceWorkerClients::Get(const nsAString& aClientId, ErrorResult& aRv)
{
  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
  MOZ_ASSERT(workerPrivate);
  workerPrivate->AssertIsOnWorkerThread();

  RefPtr<Promise> promise = Promise::Create(mWorkerScope, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  RefPtr<PromiseWorkerProxy> promiseProxy =
    PromiseWorkerProxy::Create(workerPrivate, promise);
  if (!promiseProxy) {
    promise->MaybeReject(NS_ERROR_DOM_ABORT_ERR);
    return promise.forget();
  }

  RefPtr<GetRunnable> r =
    new GetRunnable(promiseProxy, aClientId);
  MOZ_ALWAYS_SUCCEEDS(NS_DispatchToMainThread(r));
  return promise.forget();
}

already_AddRefed<Promise>
ServiceWorkerClients::MatchAll(const ClientQueryOptions& aOptions,
                               ErrorResult& aRv)
{
  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
  MOZ_ASSERT(workerPrivate);
  workerPrivate->AssertIsOnWorkerThread();

  nsString scope;
  mWorkerScope->GetScope(scope);

  if (aOptions.mType != ClientType::Window) {
    aRv.Throw(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
    return nullptr;
  }

  RefPtr<Promise> promise = Promise::Create(mWorkerScope, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  RefPtr<PromiseWorkerProxy> promiseProxy =
    PromiseWorkerProxy::Create(workerPrivate, promise);
  if (!promiseProxy) {
    promise->MaybeReject(NS_ERROR_DOM_ABORT_ERR);
    return promise.forget();
  }

  RefPtr<MatchAllRunnable> r =
    new MatchAllRunnable(promiseProxy,
                         NS_ConvertUTF16toUTF8(scope),
                         aOptions.mIncludeUncontrolled);
  MOZ_ALWAYS_SUCCEEDS(NS_DispatchToMainThread(r));
  return promise.forget();
}

already_AddRefed<Promise>
ServiceWorkerClients::OpenWindow(const nsAString& aUrl,
                                 ErrorResult& aRv)
{
  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
  MOZ_ASSERT(workerPrivate);

  RefPtr<Promise> promise = Promise::Create(mWorkerScope, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  if (aUrl.EqualsLiteral("about:blank")) {
    promise->MaybeReject(NS_ERROR_TYPE_ERR);
    return promise.forget();
  }

  // [[4. If this algorithm is not allowed to show a popup ..]]
  // In Gecko the service worker is allowed to show a popup only if the user
  // just clicked on a notification.
  if (!workerPrivate->GlobalScope()->WindowInteractionAllowed()) {
    promise->MaybeReject(NS_ERROR_DOM_INVALID_ACCESS_ERR);
    return promise.forget();
  }

  RefPtr<PromiseWorkerProxy> promiseProxy =
    PromiseWorkerProxy::Create(workerPrivate, promise);

  if (!promiseProxy) {
    return nullptr;
  }

  nsString scope;
  mWorkerScope->GetScope(scope);

  RefPtr<OpenWindowRunnable> r = new OpenWindowRunnable(promiseProxy,
                                                          aUrl, scope);
  MOZ_ALWAYS_SUCCEEDS(NS_DispatchToMainThread(r));

  return promise.forget();
}

already_AddRefed<Promise>
ServiceWorkerClients::Claim(ErrorResult& aRv)
{
  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
  MOZ_ASSERT(workerPrivate);

  RefPtr<Promise> promise = Promise::Create(mWorkerScope, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  RefPtr<PromiseWorkerProxy> promiseProxy =
    PromiseWorkerProxy::Create(workerPrivate, promise);
  if (!promiseProxy) {
    promise->MaybeReject(NS_ERROR_DOM_ABORT_ERR);
    return promise.forget();
  }

  nsString scope;
  mWorkerScope->GetScope(scope);

  RefPtr<ClaimRunnable> runnable =
    new ClaimRunnable(promiseProxy, NS_ConvertUTF16toUTF8(scope));

  MOZ_ALWAYS_SUCCEEDS(NS_DispatchToMainThread(runnable));
  return promise.forget();
}
