/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_workers_serviceworkermanager_h
#define mozilla_dom_workers_serviceworkermanager_h

#include "nsIServiceWorkerManager.h"
#include "nsCOMPtr.h"

#include "ipc/IPCMessageUtils.h"
#include "mozilla/Attributes.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/LinkedList.h"
#include "mozilla/Preferences.h"
#include "mozilla/TypedEnumBits.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/ServiceWorkerCommon.h"
#include "mozilla/dom/ServiceWorkerRegistrar.h"
#include "mozilla/dom/ServiceWorkerRegistrarTypes.h"
#include "mozilla/dom/workers/ServiceWorkerRegistrationInfo.h"
#include "mozilla/ipc/BackgroundUtils.h"
#include "nsClassHashtable.h"
#include "nsDataHashtable.h"
#include "nsIIPCBackgroundChildCreateCallback.h"
#include "nsRefPtrHashtable.h"
#include "nsTArrayForwardDeclare.h"
#include "nsTObserverArray.h"

class mozIApplicationClearPrivateDataParams;

namespace mozilla {

class OriginAttributes;

namespace dom {

class ServiceWorkerRegistrationListener;

namespace workers {

class ServiceWorkerClientInfo;
class ServiceWorkerInfo;
class ServiceWorkerJobQueue;
class ServiceWorkerManagerChild;
class ServiceWorkerPrivate;

class ServiceWorkerUpdateFinishCallback
{
protected:
  virtual ~ServiceWorkerUpdateFinishCallback()
  {}

public:
  NS_INLINE_DECL_REFCOUNTING(ServiceWorkerUpdateFinishCallback)

  virtual
  void UpdateSucceeded(ServiceWorkerRegistrationInfo* aInfo) = 0;

  virtual
  void UpdateFailed(ErrorResult& aStatus) = 0;
};

#define NS_SERVICEWORKERMANAGER_IMPL_IID                 \
{ /* f4f8755a-69ca-46e8-a65d-775745535990 */             \
  0xf4f8755a,                                            \
  0x69ca,                                                \
  0x46e8,                                                \
  { 0xa6, 0x5d, 0x77, 0x57, 0x45, 0x53, 0x59, 0x90 }     \
}

/*
 * The ServiceWorkerManager is a per-process global that deals with the
 * installation, querying and event dispatch of ServiceWorkers for all the
 * origins in the process.
 */
class ServiceWorkerManager final
  : public nsIServiceWorkerManager
  , public nsIIPCBackgroundChildCreateCallback
  , public nsIObserver
{
  friend class GetReadyPromiseRunnable;
  friend class GetRegistrationsRunnable;
  friend class GetRegistrationRunnable;
  friend class ServiceWorkerJob;
  friend class ServiceWorkerRegistrationInfo;
  friend class ServiceWorkerUnregisterJob;
  friend class ServiceWorkerUpdateJob;
  friend class UpdateTimerCallback;

public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSISERVICEWORKERMANAGER
  NS_DECL_NSIIPCBACKGROUNDCHILDCREATECALLBACK
  NS_DECL_NSIOBSERVER

  struct RegistrationDataPerPrincipal;
  nsClassHashtable<nsCStringHashKey, RegistrationDataPerPrincipal> mRegistrationInfos;

  nsTObserverArray<ServiceWorkerRegistrationListener*> mServiceWorkerRegistrationListeners;

  nsRefPtrHashtable<nsISupportsHashKey, ServiceWorkerRegistrationInfo> mControlledDocuments;

  // Track all documents that have attempted to register a service worker for a
  // given scope.
  typedef nsTArray<nsCOMPtr<nsIWeakReference>> WeakDocumentList;
  nsClassHashtable<nsCStringHashKey, WeakDocumentList> mRegisteringDocuments;

  // Track all intercepted navigation channels for a given scope.  Channels are
  // placed in the appropriate list before dispatch the FetchEvent to the worker
  // thread and removed once FetchEvent processing dispatches back to the main
  // thread.
  //
  // Note: Its safe to use weak references here because a RAII-style callback
  //       is registered with the channel before its added to this list.  We
  //       are guaranteed the callback will fire before and remove the ref
  //       from this list before the channel is destroyed.
  typedef nsTArray<nsIInterceptedChannel*> InterceptionList;
  nsClassHashtable<nsCStringHashKey, InterceptionList> mNavigationInterceptions;

  bool
  IsAvailable(nsIPrincipal* aPrincipal, nsIURI* aURI);

  bool
  IsControlled(nsIDocument* aDocument, ErrorResult& aRv);

  void
  DispatchFetchEvent(const OriginAttributes& aOriginAttributes,
                     nsIDocument* aDoc,
                     const nsAString& aDocumentIdForTopLevelNavigation,
                     nsIInterceptedChannel* aChannel,
                     bool aIsReload,
                     bool aIsSubresourceLoad,
                     ErrorResult& aRv);

  void
  Update(nsIPrincipal* aPrincipal,
         const nsACString& aScope,
         ServiceWorkerUpdateFinishCallback* aCallback);

  void
  SoftUpdate(const OriginAttributes& aOriginAttributes,
             const nsACString& aScope);

  void
  PropagateSoftUpdate(const OriginAttributes& aOriginAttributes,
                      const nsAString& aScope);

  void
  PropagateRemove(const nsACString& aHost);

  void
  Remove(const nsACString& aHost);

  void
  PropagateRemoveAll();

  void
  RemoveAll();

  already_AddRefed<ServiceWorkerRegistrationInfo>
  GetRegistration(nsIPrincipal* aPrincipal, const nsACString& aScope) const;

  ServiceWorkerRegistrationInfo*
  CreateNewRegistration(const nsCString& aScope, nsIPrincipal* aPrincipal);

  void
  RemoveRegistration(ServiceWorkerRegistrationInfo* aRegistration);

  void StoreRegistration(nsIPrincipal* aPrincipal,
                         ServiceWorkerRegistrationInfo* aRegistration);

  void
  FinishFetch(ServiceWorkerRegistrationInfo* aRegistration);

  void
  ReportToAllClients(const nsCString& aScope,
                     const nsString& aMessage,
                     const nsString& aFilename,
                     const nsString& aLine,
                     uint32_t aLineNumber,
                     uint32_t aColumnNumber,
                     uint32_t aFlags);

  // Always consumes the error by reporting to consoles of all controlled
  // documents.
  void
  HandleError(JSContext* aCx,
              nsIPrincipal* aPrincipal,
              const nsCString& aScope,
              const nsString& aWorkerURL,
              const nsString& aMessage,
              const nsString& aFilename,
              const nsString& aLine,
              uint32_t aLineNumber,
              uint32_t aColumnNumber,
              uint32_t aFlags,
              JSExnType aExnType);

  UniquePtr<ServiceWorkerClientInfo>
  GetClient(nsIPrincipal* aPrincipal,
            const nsAString& aClientId,
            ErrorResult& aRv);

  void
  GetAllClients(nsIPrincipal* aPrincipal,
                const nsCString& aScope,
                bool aIncludeUncontrolled,
                nsTArray<ServiceWorkerClientInfo>& aDocuments);

  void
  MaybeClaimClient(nsIDocument* aDocument,
                   ServiceWorkerRegistrationInfo* aWorkerRegistration);

  nsresult
  ClaimClients(nsIPrincipal* aPrincipal, const nsCString& aScope, uint64_t aId);

  nsresult
  SetSkipWaitingFlag(nsIPrincipal* aPrincipal, const nsCString& aScope,
                     uint64_t aServiceWorkerID);

  static already_AddRefed<ServiceWorkerManager>
  GetInstance();

 void
 LoadRegistration(const ServiceWorkerRegistrationData& aRegistration);

  void
  LoadRegistrations(const nsTArray<ServiceWorkerRegistrationData>& aRegistrations);

  // Used by remove() and removeAll() when clearing history.
  // MUST ONLY BE CALLED FROM UnregisterIfMatchesHost!
  void
  ForceUnregister(RegistrationDataPerPrincipal* aRegistrationData,
                  ServiceWorkerRegistrationInfo* aRegistration);

  NS_IMETHOD
  AddRegistrationEventListener(const nsAString& aScope,
                               ServiceWorkerRegistrationListener* aListener);

  NS_IMETHOD
  RemoveRegistrationEventListener(const nsAString& aScope,
                                  ServiceWorkerRegistrationListener* aListener);

  void
  MaybeCheckNavigationUpdate(nsIDocument* aDoc);

  nsresult
  SendPushEvent(const nsACString& aOriginAttributes,
                const nsACString& aScope,
                const nsAString& aMessageId,
                const Maybe<nsTArray<uint8_t>>& aData);

  nsresult
  NotifyUnregister(nsIPrincipal* aPrincipal, const nsAString& aScope);

private:
  ServiceWorkerManager();
  ~ServiceWorkerManager();

  void
  Init();

  already_AddRefed<ServiceWorkerJobQueue>
  GetOrCreateJobQueue(const nsACString& aOriginSuffix,
                      const nsACString& aScope);

  void
  MaybeRemoveRegistrationInfo(const nsACString& aScopeKey);

  already_AddRefed<ServiceWorkerRegistrationInfo>
  GetRegistration(const nsACString& aScopeKey,
                  const nsACString& aScope) const;

  void
  AbortCurrentUpdate(ServiceWorkerRegistrationInfo* aRegistration);

  nsresult
  Update(ServiceWorkerRegistrationInfo* aRegistration);

  nsresult
  GetDocumentRegistration(nsIDocument* aDoc,
                          ServiceWorkerRegistrationInfo** aRegistrationInfo);

  NS_IMETHODIMP
  GetServiceWorkerForScope(nsIDOMWindow* aWindow,
                           const nsAString& aScope,
                           WhichServiceWorker aWhichWorker,
                           nsISupports** aServiceWorker);

  ServiceWorkerInfo*
  GetActiveWorkerInfoForScope(const OriginAttributes& aOriginAttributes,
                              const nsACString& aScope);

  ServiceWorkerInfo*
  GetActiveWorkerInfoForDocument(nsIDocument* aDocument);

  void
  InvalidateServiceWorkerRegistrationWorker(ServiceWorkerRegistrationInfo* aRegistration,
                                            WhichServiceWorker aWhichOnes);

  void
  NotifyServiceWorkerRegistrationRemoved(ServiceWorkerRegistrationInfo* aRegistration);

  void
  StartControllingADocument(ServiceWorkerRegistrationInfo* aRegistration,
                            nsIDocument* aDoc,
                            const nsAString& aDocumentId);

  void
  StopControllingADocument(ServiceWorkerRegistrationInfo* aRegistration);

  already_AddRefed<ServiceWorkerRegistrationInfo>
  GetServiceWorkerRegistrationInfo(nsPIDOMWindow* aWindow);

  already_AddRefed<ServiceWorkerRegistrationInfo>
  GetServiceWorkerRegistrationInfo(nsIDocument* aDoc);

  already_AddRefed<ServiceWorkerRegistrationInfo>
  GetServiceWorkerRegistrationInfo(nsIPrincipal* aPrincipal, nsIURI* aURI);

  already_AddRefed<ServiceWorkerRegistrationInfo>
  GetServiceWorkerRegistrationInfo(const nsACString& aScopeKey,
                                   nsIURI* aURI);

  // This method generates a key using appId and isInElementBrowser from the
  // principal. We don't use the origin because it can change during the
  // loading.
  static nsresult
  PrincipalToScopeKey(nsIPrincipal* aPrincipal, nsACString& aKey);

  static void
  AddScopeAndRegistration(const nsACString& aScope,
                          ServiceWorkerRegistrationInfo* aRegistation);

  static bool
  FindScopeForPath(const nsACString& aScopeKey,
                   const nsACString& aPath,
                   RegistrationDataPerPrincipal** aData, nsACString& aMatch);

  static bool
  HasScope(nsIPrincipal* aPrincipal, const nsACString& aScope);

  static void
  RemoveScopeAndRegistration(ServiceWorkerRegistrationInfo* aRegistration);

  void
  QueueFireEventOnServiceWorkerRegistrations(ServiceWorkerRegistrationInfo* aRegistration,
                                             const nsAString& aName);

  void
  FireUpdateFoundOnServiceWorkerRegistrations(ServiceWorkerRegistrationInfo* aRegistration);

  void
  FireControllerChange(ServiceWorkerRegistrationInfo* aRegistration);

  void
  StorePendingReadyPromise(nsPIDOMWindow* aWindow, nsIURI* aURI,
                           Promise* aPromise);

  void
  CheckPendingReadyPromises();

  bool
  CheckReadyPromise(nsPIDOMWindow* aWindow, nsIURI* aURI, Promise* aPromise);

  struct PendingReadyPromise final
  {
    PendingReadyPromise(nsIURI* aURI, Promise* aPromise)
      : mURI(aURI), mPromise(aPromise)
    {}

    nsCOMPtr<nsIURI> mURI;
    RefPtr<Promise> mPromise;
  };

  void AppendPendingOperation(nsIRunnable* aRunnable);

  bool HasBackgroundActor() const
  {
    return !!mActor;
  }

  nsClassHashtable<nsISupportsHashKey, PendingReadyPromise> mPendingReadyPromises;

  void
  MaybeRemoveRegistration(ServiceWorkerRegistrationInfo* aRegistration);

  // Removes all service worker registrations that matches the given pattern.
  void
  RemoveAllRegistrations(OriginAttributesPattern* aPattern);

  RefPtr<ServiceWorkerManagerChild> mActor;

  nsTArray<nsCOMPtr<nsIRunnable>> mPendingOperations;

  bool mShuttingDown;

  nsTArray<nsCOMPtr<nsIServiceWorkerManagerListener>> mListeners;

  void
  NotifyListenersOnRegister(nsIServiceWorkerRegistrationInfo* aRegistration);

  void
  NotifyListenersOnUnregister(nsIServiceWorkerRegistrationInfo* aRegistration);

  void
  AddRegisteringDocument(const nsACString& aScope, nsIDocument* aDoc);

  class InterceptionReleaseHandle;

  void
  AddNavigationInterception(const nsACString& aScope,
                            nsIInterceptedChannel* aChannel);

  void
  RemoveNavigationInterception(const nsACString& aScope,
                               nsIInterceptedChannel* aChannel);

  void
  ScheduleUpdateTimer(nsIPrincipal* aPrincipal, const nsACString& aScope);

  void
  UpdateTimerFired(nsIPrincipal* aPrincipal, const nsACString& aScope);

  void
  MaybeSendUnregister(nsIPrincipal* aPrincipal, const nsACString& aScope);
};

} // namespace workers
} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_workers_serviceworkermanager_h
