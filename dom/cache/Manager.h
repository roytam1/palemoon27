/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_cache_Manager_h
#define mozilla_dom_cache_Manager_h

#include "mozilla/dom/cache/CacheInitData.h"
#include "mozilla/dom/cache/PCacheStreamControlParent.h"
#include "mozilla/dom/cache/Types.h"
#include "nsCOMPtr.h"
#include "nsISupportsImpl.h"
#include "nsString.h"
#include "nsTArray.h"

class nsIInputStream;
class nsIThread;

namespace mozilla {
namespace dom {
namespace cache {

class CacheRequestResponse;
class Context;
class ManagerId;
class PCacheQueryParams;
class PCacheRequest;
class PCacheRequestOrVoid;
struct SavedRequest;
struct SavedResponse;
class StreamList;

// The Manager is class is responsible for performing all of the underlying
// work for a Cache or CacheStorage operation.  The DOM objects and IPC actors
// are basically just plumbing to get the request to the right Manager object
// running in the parent process.
//
// There should be exactly one Manager object for each origin or app using the
// Cache API.  This uniqueness is defined by the ManagerId equality operator.
// The uniqueness is enforced by the Manager GetOrCreate() factory method.
//
// The Manager object can out live the IPC actors in the case where the child
// process is killed; e.g a child process OOM.  The Manager object can
// The Manager object can potentially use non-trivial resources.  Long lived
// DOM objects and their actors should not maintain a reference to the Manager
// while idle.  Transient DOM objects that may keep a reference for their
// lifetimes.
//
// For example, once a CacheStorage DOM object is access it will live until its
// global is released.  Therefore, CacheStorage should release its Manager
// reference after operations complete and it becomes idle.  Cache objects,
// however, can be GC'd once content are done using them and can therefore keep
// their Manager reference alive.  Its expected that more operations are
// performed on a Cache object, so keeping the Manager reference will help
// minimize overhead for each reference.
//
// As an invariant, all Manager objects must cease all IO before shutdown.  This
// is enforced by the ShutdownObserver.  If content still holds references to
// Cache DOM objects during shutdown, then all operations will begin rejecting.
class Manager final
{
public:
  // Callback interface implemented by clients of Manager, such as CacheParent
  // and CacheStorageParent.  In general, if you call a Manager method you
  // should expect to receive exactly one On*() callback.  For example, if
  // you call Manager::CacheMatch(), then you should expect to receive
  // OnCacheMatch() back in response.
  //
  // Listener objects are set on a per-operation basis.  So you pass the
  // Listener to a call like Manager::CacheMatch().  Once set in this way,
  // the Manager will continue to reference the Listener until RemoveListener()
  // is called.  This is done to allow the same listener to be used for
  // multiple operations simultaneously without having to maintain an exact
  // count of operations-in-flight.
  //
  // Note, the Manager only holds weak references to Listener objects.
  // Listeners must call Manager::RemoveListener() before they are destroyed
  // to clear these weak references.
  //
  // All public methods should be invoked on the same thread used to create
  // the Manager.
  class Listener
  {
  public:
    virtual void OnCacheMatch(RequestId aRequestId, nsresult aRv,
                              const SavedResponse* aResponse,
                              StreamList* aStreamList) { }
    virtual void OnCacheMatchAll(RequestId aRequestId, nsresult aRv,
                                 const nsTArray<SavedResponse>& aSavedResponses,
                                 StreamList* aStreamList) { }
    virtual void OnCachePutAll(RequestId aRequestId, nsresult aRv) { }
    virtual void OnCacheDelete(RequestId aRequestId, nsresult aRv,
                               bool aSuccess) { }
    virtual void OnCacheKeys(RequestId aRequestId, nsresult aRv,
                             const nsTArray<SavedRequest>& aSavedRequests,
                             StreamList* aStreamList) { }

    virtual void OnStorageMatch(RequestId aRequestId, nsresult aRv,
                                const SavedResponse* aResponse,
                                StreamList* aStreamList) { }
    virtual void OnStorageHas(RequestId aRequestId, nsresult aRv,
                              bool aCacheFound) { }
    virtual void OnStorageOpen(RequestId aRequestId, nsresult aRv,
                               CacheId aCacheId) { }
    virtual void OnStorageDelete(RequestId aRequestId, nsresult aRv,
                                 bool aCacheDeleted) { }
    virtual void OnStorageKeys(RequestId aRequestId, nsresult aRv,
                               const nsTArray<nsString>& aKeys) { }

  protected:
    ~Listener() { }
  };

  static nsresult GetOrCreate(ManagerId* aManagerId, Manager** aManagerOut);
  static already_AddRefed<Manager> Get(ManagerId* aManagerId);

  // Synchronously shutdown from main thread.  This spins the event loop.
  static void ShutdownAllOnMainThread();

  // Must be called by Listener objects before they are destroyed.
  void RemoveListener(Listener* aListener);

  // Must be called by Context objects before they are destroyed.
  void RemoveContext(Context* aContext);

  // If an actor represents a long term reference to a cache or body stream,
  // then they must call AddRefCacheId() or AddRefBodyId().  This will
  // cause the Manager to keep the backing data store alive for the given
  // object.  The actor must then call ReleaseCacheId() or ReleaseBodyId()
  // exactly once for every AddRef*() call it made.  Any delayed deletion
  // will then be performed.
  void AddRefCacheId(CacheId aCacheId);
  void ReleaseCacheId(CacheId aCacheId);
  void AddRefBodyId(const nsID& aBodyId);
  void ReleaseBodyId(const nsID& aBodyId);

  already_AddRefed<ManagerId> GetManagerId() const;

  // Methods to allow a StreamList to register themselves with the Manager.
  // StreamList objects must call RemoveStreamList() before they are destroyed.
  void AddStreamList(StreamList* aStreamList);
  void RemoveStreamList(StreamList* aStreamList);

  // TODO: consider moving CacheId up in the argument lists below (bug 1110485)
  void CacheMatch(Listener* aListener, RequestId aRequestId, CacheId aCacheId,
                  const PCacheRequest& aRequest,
                  const PCacheQueryParams& aParams);
  void CacheMatchAll(Listener* aListener, RequestId aRequestId,
                     CacheId aCacheId, const PCacheRequestOrVoid& aRequestOrVoid,
                     const PCacheQueryParams& aParams);
  void CachePutAll(Listener* aListener, RequestId aRequestId, CacheId aCacheId,
                   const nsTArray<CacheRequestResponse>& aPutList,
                   const nsTArray<nsCOMPtr<nsIInputStream>>& aRequestStreamList,
                   const nsTArray<nsCOMPtr<nsIInputStream>>& aResponseStreamList);
  void CacheDelete(Listener* aListener, RequestId aRequestId,
                   CacheId aCacheId, const PCacheRequest& aRequest,
                   const PCacheQueryParams& aParams);
  void CacheKeys(Listener* aListener, RequestId aRequestId,
                 CacheId aCacheId, const PCacheRequestOrVoid& aRequestOrVoid,
                 const PCacheQueryParams& aParams);

  void StorageMatch(Listener* aListener, RequestId aRequestId,
                    Namespace aNamespace, const PCacheRequest& aRequest,
                    const PCacheQueryParams& aParams);
  void StorageHas(Listener* aListener, RequestId aRequestId,
                  Namespace aNamespace, const nsAString& aKey);
  void StorageOpen(Listener* aListener, RequestId aRequestId,
                   Namespace aNamespace, const nsAString& aKey);
  void StorageDelete(Listener* aListener, RequestId aRequestId,
                     Namespace aNamespace, const nsAString& aKey);
  void StorageKeys(Listener* aListener, RequestId aRequestId,
                   Namespace aNamespace);

private:
  class Factory;
  class BaseAction;
  class DeleteOrphanedCacheAction;

  class CacheMatchAction;
  class CacheMatchAllAction;
  class CachePutAllAction;
  class CacheDeleteAction;
  class CacheKeysAction;

  class StorageMatchAction;
  class StorageHasAction;
  class StorageOpenAction;
  class StorageDeleteAction;
  class StorageKeysAction;

  typedef uint64_t ListenerId;

  Manager(ManagerId* aManagerId, nsIThread* aIOThread);
  ~Manager();
  void Shutdown();
  already_AddRefed<Context> CurrentContext();

  ListenerId SaveListener(Listener* aListener);
  Listener* GetListener(ListenerId aListenerId) const;

  bool SetCacheIdOrphanedIfRefed(CacheId aCacheId);
  bool SetBodyIdOrphanedIfRefed(const nsID& aBodyId);
  void NoteOrphanedBodyIdList(const nsTArray<nsID>& aDeletedBodyIdList);

  nsRefPtr<ManagerId> mManagerId;
  nsCOMPtr<nsIThread> mIOThread;

  // Weak reference cleared by RemoveContext() in Context destructor.
  Context* MOZ_NON_OWNING_REF mContext;

  // Weak references cleared by RemoveListener() in Listener destructors.
  struct ListenerEntry
  {
    ListenerEntry()
      : mId(UINT64_MAX),
      mListener(nullptr)
    {
    }

    ListenerEntry(ListenerId aId, Listener* aListener)
      : mId(aId)
      , mListener(aListener)
    {
    }

    ListenerId mId;
    Listener* mListener;
  };

  class ListenerEntryIdComparator
  {
  public:
    bool Equals(const ListenerEntry& aA, const ListenerId& aB) const
    {
      return aA.mId == aB;
    }
  };

  class ListenerEntryListenerComparator
  {
  public:
    bool Equals(const ListenerEntry& aA, const Listener* aB) const
    {
      return aA.mListener == aB;
    }
  };

  typedef nsTArray<ListenerEntry> ListenerList;
  ListenerList mListeners;
  static ListenerId sNextListenerId;

  // Weak references cleared by RemoveStreamList() in StreamList destructors.
  nsTArray<StreamList*> mStreamLists;

  bool mShuttingDown;

  struct CacheIdRefCounter
  {
    CacheId mCacheId;
    MozRefCountType mCount;
    bool mOrphaned;
  };
  nsTArray<CacheIdRefCounter> mCacheIdRefs;

  struct BodyIdRefCounter
  {
    nsID mBodyId;
    MozRefCountType mCount;
    bool mOrphaned;
  };
  nsTArray<BodyIdRefCounter> mBodyIdRefs;

public:
  NS_INLINE_DECL_REFCOUNTING(cache::Manager)
};

} // namespace cache
} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_cache_Manager_h
