/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_workers_serviceworkermanager_h
#define mozilla_dom_workers_serviceworkermanager_h

#include "nsIServiceWorkerManager.h"
#include "nsCOMPtr.h"

#include "ipc/IPCMessageUtils.h"
#include "mozilla/Attributes.h"
#include "mozilla/LinkedList.h"
#include "mozilla/Preferences.h"
#include "mozilla/TypedEnumInternal.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/ServiceWorkerBinding.h" // For ServiceWorkerState
#include "mozilla/dom/ServiceWorkerCommon.h"
#include "mozilla/dom/ServiceWorkerRegistrar.h"
#include "mozilla/dom/ServiceWorkerRegistrarTypes.h"
#include "mozilla/ipc/BackgroundUtils.h"
#include "nsIIPCBackgroundChildCreateCallback.h"
#include "nsClassHashtable.h"
#include "nsDataHashtable.h"
#include "nsRefPtrHashtable.h"
#include "nsTArrayForwardDeclare.h"
#include "nsTObserverArray.h"

class nsIScriptError;

namespace mozilla {

namespace ipc {
class BackgroundChild;
}

namespace dom {

class ServiceWorkerRegistration;

namespace workers {

class ServiceWorker;
class ServiceWorkerClientInfo;
class ServiceWorkerInfo;

class ServiceWorkerJobQueue;

class ServiceWorkerJob : public nsISupports
{
protected:
  // The queue keeps the jobs alive, so they can hold a rawptr back to the
  // queue.
  ServiceWorkerJobQueue* mQueue;

public:
  NS_DECL_ISUPPORTS

  virtual void Start() = 0;

protected:
  explicit ServiceWorkerJob(ServiceWorkerJobQueue* aQueue)
    : mQueue(aQueue)
  {
  }

  virtual ~ServiceWorkerJob()
  { }

  void
  Done(nsresult aStatus);
};

class ServiceWorkerJobQueue final
{
  friend class ServiceWorkerJob;

  nsTArray<nsRefPtr<ServiceWorkerJob>> mJobs;

public:
  ~ServiceWorkerJobQueue()
  {
    if (!mJobs.IsEmpty()) {
      NS_WARNING("Pending/running jobs still around on shutdown!");
    }
  }

  void
  Append(ServiceWorkerJob* aJob)
  {
    MOZ_ASSERT(aJob);
    MOZ_ASSERT(!mJobs.Contains(aJob));
    bool wasEmpty = mJobs.IsEmpty();
    mJobs.AppendElement(aJob);
    if (wasEmpty) {
      aJob->Start();
    }
  }

  // Only used by HandleError, keep it that way!
  ServiceWorkerJob*
  Peek()
  {
    MOZ_ASSERT(!mJobs.IsEmpty());
    return mJobs[0];
  }

private:
  void
  Pop()
  {
    MOZ_ASSERT(!mJobs.IsEmpty());
    mJobs.RemoveElementAt(0);
    if (!mJobs.IsEmpty()) {
      mJobs[0]->Start();
    }
  }

  void
  Done(ServiceWorkerJob* aJob)
  {
    MOZ_ASSERT(!mJobs.IsEmpty());
    MOZ_ASSERT(mJobs[0] == aJob);
    Pop();
  }
};

// Needs to inherit from nsISupports because NS_ProxyRelease() does not support
// non-ISupports classes.
class ServiceWorkerRegistrationInfo final : public nsISupports
{
  uint32_t mControlledDocumentsCounter;

  virtual ~ServiceWorkerRegistrationInfo();

public:
  NS_DECL_ISUPPORTS

  nsCString mScope;
  // The scriptURL for the registration. This may be completely different from
  // the URLs of the following three workers.
  nsCString mScriptSpec;

  nsCOMPtr<nsIPrincipal> mPrincipal;

  nsRefPtr<ServiceWorkerInfo> mActiveWorker;
  nsRefPtr<ServiceWorkerInfo> mWaitingWorker;
  nsRefPtr<ServiceWorkerInfo> mInstallingWorker;

  // When unregister() is called on a registration, it is not immediately
  // removed since documents may be controlled. It is marked as
  // pendingUninstall and when all controlling documents go away, removed.
  bool mPendingUninstall;

  explicit ServiceWorkerRegistrationInfo(const nsACString& aScope,
                                         nsIPrincipal* aPrincipal);

  already_AddRefed<ServiceWorkerInfo>
  Newest()
  {
    nsRefPtr<ServiceWorkerInfo> newest;
    if (mInstallingWorker) {
      newest = mInstallingWorker;
    } else if (mWaitingWorker) {
      newest = mWaitingWorker;
    } else {
      newest = mActiveWorker;
    }

    return newest.forget();
  }

  void
  StartControllingADocument()
  {
    ++mControlledDocumentsCounter;
  }

  void
  StopControllingADocument()
  {
    --mControlledDocumentsCounter;
  }

  bool
  IsControllingDocuments() const
  {
    return mActiveWorker && mControlledDocumentsCounter > 0;
  }

  void
  Clear();

  void
  TryToActivate();

  void
  Activate();

  void
  FinishActivate(bool aSuccess);

};

/*
 * Wherever the spec treats a worker instance and a description of said worker
 * as the same thing; i.e. "Resolve foo with
 * _GetNewestWorker(serviceWorkerRegistration)", we represent the description
 * by this class and spawn a ServiceWorker in the right global when required.
 */
class ServiceWorkerInfo final
{
private:
  const ServiceWorkerRegistrationInfo* mRegistration;
  nsCString mScriptSpec;
  ServiceWorkerState mState;
  // We hold rawptrs since the ServiceWorker constructor and destructor ensure
  // addition and removal.
  // There is a high chance of there being at least one ServiceWorker
  // associated with this all the time.
  nsAutoTArray<ServiceWorker*, 1> mInstances;

  ~ServiceWorkerInfo()
  { }

public:
  NS_INLINE_DECL_REFCOUNTING(ServiceWorkerInfo)

  const nsCString&
  ScriptSpec() const
  {
    return mScriptSpec;
  }

  const nsCString&
  Scope() const
  {
    return mRegistration->mScope;
  }

  void SetScriptSpec(const nsCString& aSpec)
  {
    MOZ_ASSERT(!aSpec.IsEmpty());
    mScriptSpec = aSpec;
  }

  explicit ServiceWorkerInfo(ServiceWorkerRegistrationInfo* aReg,
                             const nsACString& aScriptSpec)
    : mRegistration(aReg)
    , mScriptSpec(aScriptSpec)
    , mState(ServiceWorkerState::EndGuard_)
  {
    MOZ_ASSERT(mRegistration);
  }

  ServiceWorkerState
  State() const
  {
    return mState;
  }

  void
  UpdateState(ServiceWorkerState aState);

  // Only used to set initial state when loading from disk!
  void
  SetActivateStateUncheckedWithoutEvent(ServiceWorkerState aState)
  {
    mState = aState;
  }

  void
  AppendWorker(ServiceWorker* aWorker);

  void
  RemoveWorker(ServiceWorker* aWorker);
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
{
  friend class GetReadyPromiseRunnable;
  friend class GetRegistrationsRunnable;
  friend class GetRegistrationRunnable;
  friend class ServiceWorkerRegisterJob;
  friend class ServiceWorkerRegistrationInfo;
  friend class ServiceWorkerUnregisterJob;

public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSISERVICEWORKERMANAGER
  NS_DECL_NSIIPCBACKGROUNDCHILDCREATECALLBACK
  NS_DECLARE_STATIC_IID_ACCESSOR(NS_SERVICEWORKERMANAGER_IMPL_IID)

  static ServiceWorkerManager* FactoryCreate()
  {
    AssertIsOnMainThread();

    ServiceWorkerManager* res = new ServiceWorkerManager;
    NS_ADDREF(res);
    return res;
  }

  // Ordered list of scopes for glob matching.
  // Each entry is an absolute URL representing the scope.
  //
  // An array is used for now since the number of controlled scopes per
  // domain is expected to be relatively low. If that assumption was proved
  // wrong this should be replaced with a better structure to avoid the
  // memmoves associated with inserting stuff in the middle of the array.
  nsTArray<nsCString> mOrderedScopes;

  // Scope to registration. 
  // The scope should be a fully qualified valid URL.
  nsRefPtrHashtable<nsCStringHashKey, ServiceWorkerRegistrationInfo> mServiceWorkerRegistrationInfos;

  nsTObserverArray<ServiceWorkerRegistration*> mServiceWorkerRegistrations;

  nsRefPtrHashtable<nsISupportsHashKey, ServiceWorkerRegistrationInfo> mControlledDocuments;

  // Maps scopes to job queues.
  nsClassHashtable<nsCStringHashKey, ServiceWorkerJobQueue> mJobQueues;

  nsDataHashtable<nsCStringHashKey, bool> mSetOfScopesBeingUpdated;

  already_AddRefed<ServiceWorkerRegistrationInfo>
  GetRegistration(const nsCString& aScope) const
  {
    nsRefPtr<ServiceWorkerRegistrationInfo> reg;
    mServiceWorkerRegistrationInfos.Get(aScope, getter_AddRefs(reg));
    return reg.forget();
  }

  ServiceWorkerRegistrationInfo*
  CreateNewRegistration(const nsCString& aScope, nsIPrincipal* aPrincipal);

  void
  RemoveRegistration(ServiceWorkerRegistrationInfo* aRegistration);

  ServiceWorkerJobQueue*
  GetOrCreateJobQueue(const nsCString& aScope)
  {
    return mJobQueues.LookupOrAdd(aScope);
  }

  void StoreRegistration(nsIPrincipal* aPrincipal,
                         ServiceWorkerRegistrationInfo* aRegistration);

  void
  FinishFetch(ServiceWorkerRegistrationInfo* aRegistration);

  // Returns true if the error was handled, false if normal worker error
  // handling should continue.
  bool
  HandleError(JSContext* aCx,
              const nsCString& aScope,
              const nsString& aWorkerURL,
              nsString aMessage,
              nsString aFilename,
              nsString aLine,
              uint32_t aLineNumber,
              uint32_t aColumnNumber,
              uint32_t aFlags);

  void
  GetAllClients(const nsCString& aScope,
                nsTArray<ServiceWorkerClientInfo>& aControlledDocuments);

  static already_AddRefed<ServiceWorkerManager>
  GetInstance();

 void LoadRegistrations(
                 const nsTArray<ServiceWorkerRegistrationData>& aRegistrations);

private:
  ServiceWorkerManager();
  ~ServiceWorkerManager();

  void
  AbortCurrentUpdate(ServiceWorkerRegistrationInfo* aRegistration);

  nsresult
  Update(ServiceWorkerRegistrationInfo* aRegistration);

  nsresult
  GetDocumentRegistration(nsIDocument* aDoc, ServiceWorkerRegistrationInfo** aRegistrationInfo);

  NS_IMETHOD
  CreateServiceWorkerForWindow(nsPIDOMWindow* aWindow,
                               ServiceWorkerInfo* aInfo,
                               ServiceWorker** aServiceWorker);

  NS_IMETHOD
  CreateServiceWorker(nsIPrincipal* aPrincipal,
                      ServiceWorkerInfo* aInfo,
                      ServiceWorker** aServiceWorker);

  NS_IMETHODIMP
  GetServiceWorkerForScope(nsIDOMWindow* aWindow,
                           const nsAString& aScope,
                           WhichServiceWorker aWhichWorker,
                           nsISupports** aServiceWorker);

  void
  InvalidateServiceWorkerRegistrationWorker(ServiceWorkerRegistrationInfo* aRegistration,
                                            WhichServiceWorker aWhichOnes);

  already_AddRefed<ServiceWorkerRegistrationInfo>
  GetServiceWorkerRegistrationInfo(nsPIDOMWindow* aWindow);

  already_AddRefed<ServiceWorkerRegistrationInfo>
  GetServiceWorkerRegistrationInfo(nsIDocument* aDoc);

  already_AddRefed<ServiceWorkerRegistrationInfo>
  GetServiceWorkerRegistrationInfo(nsIURI* aURI);

  static void
  AddScope(nsTArray<nsCString>& aList, const nsACString& aScope);

  static nsCString
  FindScopeForPath(nsTArray<nsCString>& aList, const nsACString& aPath);

  static void
  RemoveScope(nsTArray<nsCString>& aList, const nsACString& aScope);

  void
  QueueFireEventOnServiceWorkerRegistrations(ServiceWorkerRegistrationInfo* aRegistration,
                                             const nsAString& aName);

  void
  FireEventOnServiceWorkerRegistrations(ServiceWorkerRegistrationInfo* aRegistration,
                                        const nsAString& aName);

  void
  FireUpdateFound(ServiceWorkerRegistrationInfo* aRegistration)
  {
    FireEventOnServiceWorkerRegistrations(aRegistration,
                                          NS_LITERAL_STRING("updatefound"));
  }

  void
  FireControllerChange(ServiceWorkerRegistrationInfo* aRegistration);

  void
  StorePendingReadyPromise(nsPIDOMWindow* aWindow, nsIURI* aURI, Promise* aPromise);

  void
  CheckPendingReadyPromises();

  bool
  CheckReadyPromise(nsPIDOMWindow* aWindow, nsIURI* aURI, Promise* aPromise);

  struct PendingReadyPromise
  {
    PendingReadyPromise(nsIURI* aURI, Promise* aPromise)
      : mURI(aURI), mPromise(aPromise)
    { }

    nsCOMPtr<nsIURI> mURI;
    nsRefPtr<Promise> mPromise;
  };

  void AppendPendingOperation(nsIRunnable* aRunnable);
  void AppendPendingOperation(ServiceWorkerJobQueue* aQueue,
                              ServiceWorkerJob* aJob);

  bool HasBackgroundActor() const
  {
    return !!mActor;
  }

  static PLDHashOperator
  CheckPendingReadyPromisesEnumerator(nsISupports* aSupports,
                                      nsAutoPtr<PendingReadyPromise>& aData,
                                      void* aUnused);

  nsClassHashtable<nsISupportsHashKey, PendingReadyPromise> mPendingReadyPromises;
 
  void
  MaybeRemoveRegistration(ServiceWorkerRegistrationInfo* aRegistration);

  mozilla::ipc::PBackgroundChild* mActor;

  struct PendingOperation;
  nsTArray<PendingOperation> mPendingOperations;
};

NS_DEFINE_STATIC_IID_ACCESSOR(ServiceWorkerManager,
                              NS_SERVICEWORKERMANAGER_IMPL_IID);

} // namespace workers
} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_workers_serviceworkermanager_h
