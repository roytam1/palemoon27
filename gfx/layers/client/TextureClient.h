/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
//  * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_TEXTURECLIENT_H
#define MOZILLA_GFX_TEXTURECLIENT_H

#include <stddef.h>                     // for size_t
#include <stdint.h>                     // for uint32_t, uint8_t, uint64_t
#include "GLTextureImage.h"             // for TextureImage
#include "ImageTypes.h"                 // for StereoMode
#include "mozilla/Assertions.h"         // for MOZ_ASSERT, etc
#include "mozilla/Attributes.h"         // for override
#include "mozilla/DebugOnly.h"
#include "mozilla/RefPtr.h"             // for RefPtr, RefCounted
#include "mozilla/gfx/2D.h"             // for DrawTarget
#include "mozilla/gfx/Point.h"          // for IntSize
#include "mozilla/gfx/Types.h"          // for SurfaceFormat
#include "mozilla/layers/FenceUtils.h"  // for FenceHandle
#include "mozilla/ipc/Shmem.h"          // for Shmem
#include "mozilla/layers/AtomicRefCountedWithFinalize.h"
#include "mozilla/layers/CompositorTypes.h"  // for TextureFlags, etc
#include "mozilla/layers/LayersTypes.h"
#include "mozilla/layers/LayersSurfaces.h"  // for SurfaceDescriptor
#include "mozilla/mozalloc.h"           // for operator delete
#include "mozilla/gfx/CriticalSection.h"
#include "nsAutoPtr.h"                  // for nsRefPtr
#include "nsCOMPtr.h"                   // for already_AddRefed
#include "nsISupportsImpl.h"            // for TextureImage::AddRef, etc
#include "GfxTexturesReporter.h"

class gfxImageSurface;

namespace mozilla {

// When defined, we track which pool the tile came from and test for
// any inconsistencies.  This can be defined in release build as well.
#ifdef DEBUG
#define GFX_DEBUG_TRACK_CLIENTS_IN_POOL 1
#endif

namespace gl {
class SharedSurface_Gralloc;
}

namespace layers {

class AsyncTransactionWaiter;
class CompositableForwarder;
class GrallocTextureData;
class ClientIPCAllocator;
class CompositableClient;
struct PlanarYCbCrData;
class Image;
class PTextureChild;
class TextureChild;
class TextureData;
struct RawTextureBuffer;
class RawYCbCrTextureBuffer;
class TextureClient;
class TextureClientRecycleAllocator;
#ifdef GFX_DEBUG_TRACK_CLIENTS_IN_POOL
class TextureClientPool;
#endif
class KeepAlive;

/**
 * TextureClient is the abstraction that allows us to share data between the
 * content and the compositor side.
 */

enum TextureAllocationFlags {
  ALLOC_DEFAULT = 0,
  ALLOC_CLEAR_BUFFER = 1 << 1,  // Clear the buffer to whatever is best for the draw target
  ALLOC_CLEAR_BUFFER_WHITE = 1 << 2,  // explicit all white
  ALLOC_CLEAR_BUFFER_BLACK = 1 << 3,  // explicit all black
  ALLOC_DISALLOW_BUFFERTEXTURECLIENT = 1 << 4,

  // Allocate the texture for out-of-band content updates. This is mostly for
  // TextureClientD3D11, which may otherwise choose D3D10 or non-KeyedMutex
  // surfaces when used on the main thread.
  ALLOC_FOR_OUT_OF_BAND_CONTENT = 1 << 5,

  // Disable any cross-device synchronization. This is also for TextureClientD3D11,
  // and creates a texture without KeyedMutex.
  ALLOC_MANUAL_SYNCHRONIZATION = 1 << 6,
};

#ifdef XP_WIN
typedef void* SyncHandle;
#else
typedef uintptr_t SyncHandle;
#endif // XP_WIN

class SyncObject : public RefCounted<SyncObject>
{
public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(SyncObject)
  virtual ~SyncObject() { }

  static already_AddRefed<SyncObject> CreateSyncObject(SyncHandle aHandle);

  enum class SyncType {
    D3D11,
  };

  virtual SyncType GetSyncType() = 0;
  virtual void FinalizeFrame() = 0;

protected:
  SyncObject() { }
};

/**
 * This class may be used to asynchronously receive an update when the content
 * drawn to this texture client is available for reading in CPU memory. This
 * can only be used on texture clients that support draw target creation.
 */
class TextureReadbackSink
{
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(TextureReadbackSink)
public:
  /**
   * Callback function to implement in order to receive a DataSourceSurface
   * containing the data read back from the texture client. This will always
   * be called on the main thread, and this may not hold on to the
   * DataSourceSurface beyond the execution of this function.
   */
  virtual void ProcessReadback(gfx::DataSourceSurface *aSourceSurface) = 0;

protected:
  virtual ~TextureReadbackSink() {}
};

enum class BackendSelector
{
  Content,
  Canvas
};

/// Temporary object providing direct access to a Texture's memory.
///
/// see TextureClient::CanExposeMappedData() and TextureClient::BorrowMappedData().
struct MappedTextureData
{
  uint8_t* data;
  gfx::IntSize size;
  int32_t stride;
  gfx::SurfaceFormat format;
};

struct MappedYCbCrChannelData
{
  uint8_t* data;
  gfx::IntSize size;
  int32_t stride;
  int32_t skip;

  bool CopyInto(MappedYCbCrChannelData& aDst);
};

struct MappedYCbCrTextureData {
  MappedYCbCrChannelData y;
  MappedYCbCrChannelData cb;
  MappedYCbCrChannelData cr;
  // Sad but because of how SharedPlanarYCbCrData is used we have to expose this for now.
  uint8_t* metadata;
  StereoMode stereoMode;

  bool CopyInto(MappedYCbCrTextureData& aDst)
  {
    return y.CopyInto(aDst.y)
        && cb.CopyInto(aDst.cb)
        && cr.CopyInto(aDst.cr);
  }
};

#ifdef XP_WIN
class D3D11TextureData;
#endif

class TextureData {
public:
  struct Info {
    gfx::IntSize size;
    gfx::SurfaceFormat format;
    bool hasIntermediateBuffer;
    bool hasSynchronization;
    bool supportsMoz2D;
    bool canExposeMappedData;

    Info()
    : format(gfx::SurfaceFormat::UNKNOWN)
    , hasIntermediateBuffer(false)
    , hasSynchronization(false)
    , supportsMoz2D(false)
    , canExposeMappedData(false)
    {}
  };

  TextureData() { MOZ_COUNT_CTOR(TextureData); }

  virtual ~TextureData() { MOZ_COUNT_DTOR(TextureData); }

  virtual void FillInfo(TextureData::Info& aInfo) const = 0;

  virtual bool Lock(OpenMode aMode, FenceHandle* aFence) = 0;

  virtual void Unlock() = 0;

  virtual already_AddRefed<gfx::DrawTarget> BorrowDrawTarget() { return nullptr; }

  virtual bool BorrowMappedData(MappedTextureData&) { return false; }

  virtual bool BorrowMappedYCbCrData(MappedYCbCrTextureData&) { return false; }

  virtual void Deallocate(ClientIPCAllocator* aAllocator) = 0;

  /// Depending on the texture's flags either Deallocate or Forget is called.
  virtual void Forget(ClientIPCAllocator* aAllocator) {}

  virtual bool Serialize(SurfaceDescriptor& aDescriptor) = 0;

  virtual TextureData*
  CreateSimilar(ClientIPCAllocator* aAllocator,
                TextureFlags aFlags = TextureFlags::DEFAULT,
                TextureAllocationFlags aAllocFlags = ALLOC_DEFAULT) const { return nullptr; }

  virtual bool UpdateFromSurface(gfx::SourceSurface* aSurface) { return false; };

  virtual bool ReadBack(TextureReadbackSink* aReadbackSink) { return false; }

  /// Ideally this should not be exposed and users of TextureClient would use Lock/Unlock
  /// preoperly but that requires a few changes to SharedSurface and maybe gonk video.
  virtual void WaitForFence(FenceHandle* aFence) {};

  virtual void SyncWithObject(SyncObject* aFence) {};

  virtual TextureFlags GetTextureFlags() const { return TextureFlags::NO_FLAGS; }

#ifdef XP_WIN
  virtual D3D11TextureData* AsD3D11TextureData() {
    return nullptr;
  }
#endif

  virtual GrallocTextureData* AsGrallocTextureData() { return nullptr; }
};

/**
 * TextureClient is a thin abstraction over texture data that need to be shared
 * between the content process and the compositor process. It is the
 * content-side half of a TextureClient/TextureHost pair. A corresponding
 * TextureHost lives on the compositor-side.
 *
 * TextureClient's primary purpose is to present texture data in a way that is
 * understood by the IPC system. There are two ways to use it:
 * - Use it to serialize image data that is not IPC-friendly (most likely
 * involving a copy into shared memory)
 * - preallocate it and paint directly into it, which avoids copy but requires
 * the painting code to be aware of TextureClient (or at least the underlying
 * shared memory).
 *
 * There is always one and only one TextureClient per TextureHost, and the
 * TextureClient/Host pair only owns one buffer of image data through its
 * lifetime. This means that the lifetime of the underlying shared data
 * matches the lifetime of the TextureClient/Host pair. It also means
 * TextureClient/Host do not implement double buffering, which is the
 * responsibility of the compositable (which would use two Texture pairs).
 * In order to send several different buffers to the compositor side, use
 * several TextureClients.
 */
class TextureClient
  : public AtomicRefCountedWithFinalize<TextureClient>
{
public:
  explicit TextureClient(TextureData* aData, TextureFlags aFlags, ClientIPCAllocator* aAllocator);

  virtual ~TextureClient();

  static already_AddRefed<TextureClient>
  CreateWithData(TextureData* aData, TextureFlags aFlags, ClientIPCAllocator* aAllocator);

  // Creates and allocates a TextureClient usable with Moz2D.
  static already_AddRefed<TextureClient>
  CreateForDrawing(CompositableForwarder* aAllocator,
                   gfx::SurfaceFormat aFormat,
                   gfx::IntSize aSize,
                   BackendSelector aSelector,
                   TextureFlags aTextureFlags,
                   TextureAllocationFlags flags = ALLOC_DEFAULT);

  // Creates and allocates a TextureClient supporting the YCbCr format.
  static already_AddRefed<TextureClient>
  CreateForYCbCr(ClientIPCAllocator* aAllocator,
                 gfx::IntSize aYSize,
                 gfx::IntSize aCbCrSize,
                 StereoMode aStereoMode,
                 TextureFlags aTextureFlags);

  // Creates and allocates a TextureClient (can be accessed through raw
  // pointers).
  static already_AddRefed<TextureClient>
  CreateForRawBufferAccess(ClientIPCAllocator* aAllocator,
                           gfx::SurfaceFormat aFormat,
                           gfx::IntSize aSize,
                           gfx::BackendType aMoz2dBackend,
                           TextureFlags aTextureFlags,
                           TextureAllocationFlags flags = ALLOC_DEFAULT);

  // Creates and allocates a TextureClient (can beaccessed through raw
  // pointers) with a certain buffer size. It's unfortunate that we need this.
  // providing format and sizes could let us do more optimization.
  static already_AddRefed<TextureClient>
  CreateForYCbCrWithBufferSize(ClientIPCAllocator* aAllocator,
                               gfx::SurfaceFormat aFormat,
                               size_t aSize,
                               TextureFlags aTextureFlags);

  // Creates and allocates a TextureClient of the same type.
  already_AddRefed<TextureClient>
  CreateSimilar(TextureFlags aFlags = TextureFlags::DEFAULT,
                TextureAllocationFlags aAllocFlags = ALLOC_DEFAULT) const;

  /**
   * Locks the shared data, allowing the caller to get access to it.
   *
   * Please always lock/unlock when accessing the shared data.
   * If Lock() returns false, you should not attempt to access the shared data.
   */
  bool Lock(OpenMode aMode);

  void Unlock();

  bool IsLocked() const { return mIsLocked; }

  gfx::IntSize GetSize() const { return mInfo.size; }

  gfx::SurfaceFormat GetFormat() const { return mInfo.format; }

  /**
   * Returns true if this texture has a synchronization mechanism (mutex, fence, etc.).
   * Textures that do not implement synchronization should be immutable or should
   * use immediate uploads (see TextureFlags in CompositorTypes.h)
   * Even if a texture does not implement synchronization, Lock and Unlock need
   * to be used appropriately since the latter are also there to map/numap data.
   */
  bool HasSynchronization() const { return mInfo.hasSynchronization; }

  /**
   * Indicates whether the TextureClient implementation is backed by an
   * in-memory buffer. The consequence of this is that locking the
   * TextureClient does not contend with locking the texture on the host side.
   */
  bool HasIntermediateBuffer() const { return mInfo.hasIntermediateBuffer; }

  bool CanExposeDrawTarget() const { return mInfo.supportsMoz2D; }

  bool CanExposeMappedData() const { return mInfo.canExposeMappedData; }

  /**
   * Returns a DrawTarget to draw into the TextureClient.
   * This function should never be called when not on the main thread!
   *
   * This must never be called on a TextureClient that is not sucessfully locked.
   * When called several times within one Lock/Unlock pair, this method should
   * return the same DrawTarget.
   * The DrawTarget is automatically flushed by the TextureClient when the latter
   * is unlocked, and the DrawTarget that will be returned within the next
   * lock/unlock pair may or may not be the same object.
   * Do not keep references to the DrawTarget outside of the lock/unlock pair.
   *
   * This is typically used as follows:
   *
   * if (!texture->Lock(OpenMode::OPEN_READ_WRITE)) {
   *   return false;
   * }
   * {
   *   // Restrict this code's scope to ensure all references to dt are gone
   *   // when Unlock is called.
   *   DrawTarget* dt = texture->BorrowDrawTarget();
   *   // use the draw target ...
   * }
   * texture->Unlock();
   *
   */
  gfx::DrawTarget* BorrowDrawTarget();

  /**
   * Similar to BorrowDrawTarget but provides direct access to the texture's bits
   * instead of a DrawTarget.
   */
  bool BorrowMappedData(MappedTextureData&);
  bool BorrowMappedYCbCrData(MappedYCbCrTextureData&);

  /**
   * This function can be used to update the contents of the TextureClient
   * off the main thread.
   */
  void UpdateFromSurface(gfx::SourceSurface* aSurface);

  /**
   * This method is strictly for debugging. It causes locking and
   * needless copies.
   */
  already_AddRefed<gfx::DataSourceSurface> GetAsSurface();

  virtual void PrintInfo(std::stringstream& aStream, const char* aPrefix);

  /**
   * Copies a rectangle from this texture client to a position in aTarget.
   * It is assumed that the necessary locks are in place; so this should at
   * least have a read lock and aTarget should at least have a write lock.
   */
  bool CopyToTextureClient(TextureClient* aTarget,
                           const gfx::IntRect* aRect,
                           const gfx::IntPoint* aPoint);

  /**
   * Allocate and deallocate a TextureChild actor.
   *
   * TextureChild is an implementation detail of TextureClient that is not
   * exposed to the rest of the code base. CreateIPDLActor and DestroyIPDLActor
   * are for use with the managing IPDL protocols only (so that they can
   * implement AllocPextureChild and DeallocPTextureChild).
   */
  static PTextureChild* CreateIPDLActor();
  static bool DestroyIPDLActor(PTextureChild* actor);
  // call this if the transaction that was supposed to destroy the actor failed.
  static bool DestroyFallback(PTextureChild* actor);

  /**
   * Get the TextureClient corresponding to the actor passed in parameter.
   */
  static already_AddRefed<TextureClient> AsTextureClient(PTextureChild* actor);

  /**
   * TextureFlags contain important information about various aspects
   * of the texture, like how its liferime is managed, and how it
   * should be displayed.
   * See TextureFlags in CompositorTypes.h.
   */
  TextureFlags GetFlags() const { return mFlags; }

  bool HasFlags(TextureFlags aFlags) const
  {
    return (mFlags & aFlags) == aFlags;
  }

  void AddFlags(TextureFlags aFlags);

  void RemoveFlags(TextureFlags aFlags);

  // The TextureClient must not be locked when calling this method.
  void RecycleTexture(TextureFlags aFlags);

  /**
   * valid only for TextureFlags::RECYCLE TextureClient.
   * When called this texture client will grab a strong reference and release
   * it once the compositor notifies that it is done with the texture.
   * NOTE: In this stage the texture client can no longer be used by the
   * client in a transaction.
   */
  void WaitForCompositorRecycle();

  /**
   * Should only be called when dying. We no longer care whether the compositor
   * has finished with the texture.
   */
  void CancelWaitForCompositorRecycle();

  /**
   * After being shared with the compositor side, an immutable texture is never
   * modified, it can only be read. It is safe to not Lock/Unlock immutable
   * textures.
   */
  bool IsImmutable() const { return !!(mFlags & TextureFlags::IMMUTABLE); }

  void MarkImmutable() { AddFlags(TextureFlags::IMMUTABLE); }

  bool IsSharedWithCompositor() const;

  /**
   * If this method returns false users of TextureClient are not allowed
   * to access the shared data.
   */
  bool IsValid() const { return !!mData; }

  /**
   * Called when TextureClient is added to CompositableClient.
   */
  void SetAddedToCompositableClient();

  /**
   * If this method retuns false, TextureClient is already added to CompositableClient,
   * since its creation or recycling.
   */
  bool IsAddedToCompositableClient() const { return mAddedToCompositableClient; }

  /**
   * Create and init the TextureChild/Parent IPDL actor pair.
   *
   * Should be called only once per TextureClient.
   * The TextureClient must not be locked when calling this method.
   */
  bool InitIPDLActor(CompositableForwarder* aForwarder);

  /**
   * Return a pointer to the IPDLActor.
   *
   * This is to be used with IPDL messages only. Do not store the returned
   * pointer.
   */
  PTextureChild* GetIPDLActor();

  /**
   * Triggers the destruction of the shared data and the corresponding TextureHost.
   *
   * If the texture flags contain TextureFlags::DEALLOCATE_CLIENT, the destruction
   * will be synchronously coordinated with the compositor side, otherwise it
   * will be done asynchronously.
   * If sync is true, the destruction will be synchronous regardless of the
   * texture's flags (bad for performance, use with care).
   */
  void Destroy(bool sync = false);

  virtual void SetReleaseFenceHandle(const FenceHandle& aReleaseFenceHandle)
  {
    mReleaseFenceHandle.Merge(aReleaseFenceHandle);
  }

  virtual FenceHandle GetAndResetReleaseFenceHandle()
  {
    FenceHandle fence;
    mReleaseFenceHandle.TransferToAnotherFenceHandle(fence);
    return fence;
  }

  virtual void SetAcquireFenceHandle(const FenceHandle& aAcquireFenceHandle)
  {
    mAcquireFenceHandle = aAcquireFenceHandle;
  }

  virtual const FenceHandle& GetAcquireFenceHandle() const
  {
    return mAcquireFenceHandle;
  }

  /**
   * Set AsyncTransactionTracker of RemoveTextureFromCompositableAsync() transaction.
   */
  virtual void SetRemoveFromCompositableWaiter(AsyncTransactionWaiter* aWaiter);

  /**
   * This function waits until the buffer is no longer being used.
   *
   * XXX - Ideally we shouldn't need this method because Lock the right
   * thing already.
   */
  virtual void WaitForBufferOwnership(bool aWaitReleaseFence = true);

  /**
   * Track how much of this texture is wasted.
   * For example we might allocate a 256x256 tile but only use 10x10.
   */
  void SetWaste(int aWasteArea) {
    mWasteTracker.Update(aWasteArea, BytesPerPixel(GetFormat()));
  }

  /**
   * This sets the readback sink that this texture is to use. This will
   * receive the data for this texture as soon as it becomes available after
   * texture unlock.
   */
  virtual void SetReadbackSink(TextureReadbackSink* aReadbackSink) {
    mReadbackSink = aReadbackSink;
  }

  void SyncWithObject(SyncObject* aFence) { mData->SyncWithObject(aFence); }

  ClientIPCAllocator* GetAllocator() { return mAllocator; }

  TextureClientRecycleAllocator* GetRecycleAllocator() { return mRecycleAllocator; }
  void SetRecycleAllocator(TextureClientRecycleAllocator* aAllocator);

  /// If you add new code that uses this method, you are probably doing something wrong.
  TextureData* GetInternalData() { return mData; }
  const TextureData* GetInternalData() const { return mData; }

  virtual void RemoveFromCompositable(CompositableClient* aCompositable,
                                      AsyncTransactionWaiter* aWaiter = nullptr);

private:
  static void TextureClientRecycleCallback(TextureClient* aClient, void* aClosure);

  /**
   * Called once, during the destruction of the Texture, on the thread in which
   * texture's reference count reaches 0 (could be any thread).
   *
   * Here goes the shut-down code that uses virtual methods.
   * Must only be called by Release().
   */
  void Finalize() {}

  friend class AtomicRefCountedWithFinalize<TextureClient>;
  friend class gl::SharedSurface_Gralloc;
protected:
  /**
   * Should only be called *once* per texture, in TextureClient::InitIPDLActor.
   * Some texture implementations rely on the fact that the descriptor will be
   * deserialized.
   * Calling ToSurfaceDescriptor again after it has already returned true,
   * or never constructing a TextureHost with aDescriptor may result in a memory
   * leak (see TextureClientD3D9 for example).
   */
  bool ToSurfaceDescriptor(SurfaceDescriptor& aDescriptor);

  void LockActor() const;
  void UnlockActor() const;

  TextureData::Info mInfo;

  RefPtr<ClientIPCAllocator> mAllocator;
  RefPtr<TextureChild> mActor;
  RefPtr<TextureClientRecycleAllocator> mRecycleAllocator;
  RefPtr<AsyncTransactionWaiter> mRemoveFromCompositableWaiter;

  TextureData* mData;
  RefPtr<gfx::DrawTarget> mBorrowedDrawTarget;

  TextureFlags mFlags;
  FenceHandle mReleaseFenceHandle;
  FenceHandle mAcquireFenceHandle;
  gl::GfxTextureWasteTracker mWasteTracker;

  OpenMode mOpenMode;
#ifdef DEBUG
  uint32_t mExpectedDtRefs;
#endif
  bool mIsLocked;

  bool mAddedToCompositableClient;
  bool mWorkaroundAnnoyingSharedSurfaceLifetimeIssues;
  bool mWorkaroundAnnoyingSharedSurfaceOwnershipIssues;

  RefPtr<TextureReadbackSink> mReadbackSink;

  friend class TextureChild;
  friend class RemoveTextureFromCompositableTracker;
  friend void TestTextureClientSurface(TextureClient*, gfxImageSurface*);
  friend void TestTextureClientYCbCr(TextureClient*, PlanarYCbCrData&);

#ifdef GFX_DEBUG_TRACK_CLIENTS_IN_POOL
public:
  // Pointer to the pool this tile came from.
  TextureClientPool* mPoolTracker;
#endif
};

/**
 * Task that releases TextureClient pointer on a specified thread.
 */
class TextureClientReleaseTask : public Runnable
{
public:
    explicit TextureClientReleaseTask(TextureClient* aClient)
        : mTextureClient(aClient) {
    }

    NS_IMETHOD Run() override
    {
        mTextureClient = nullptr;
        return NS_OK;
    }

private:
    RefPtr<TextureClient> mTextureClient;
};

// Automatically lock and unlock a texture. Since texture locking is fallible,
// Succeeded() must be checked on the guard object before proceeding.
class MOZ_RAII TextureClientAutoLock
{
  MOZ_DECL_USE_GUARD_OBJECT_NOTIFIER;

public:
  TextureClientAutoLock(TextureClient* aTexture, OpenMode aMode
                        MOZ_GUARD_OBJECT_NOTIFIER_PARAM)
   : mTexture(aTexture),
     mSucceeded(false)
  {
    MOZ_GUARD_OBJECT_NOTIFIER_INIT;

    mSucceeded = mTexture->Lock(aMode);
#ifdef DEBUG
    mChecked = false;
#endif
  }
  ~TextureClientAutoLock() {
    MOZ_ASSERT(mChecked);
    if (mSucceeded) {
      mTexture->Unlock();
    }
  }

  bool Succeeded() {
#ifdef DEBUG
    mChecked = true;
#endif
    return mSucceeded;
  }

private:
  TextureClient* mTexture;
#ifdef DEBUG
  bool mChecked;
#endif
  bool mSucceeded;
};

class KeepAlive
{
public:
  virtual ~KeepAlive() {}
};

template<typename T>
class TKeepAlive : public KeepAlive
{
public:
  explicit TKeepAlive(T* aData) : mData(aData) {}
protected:
  RefPtr<T> mData;
};

/// Convenience function to set the content of ycbcr texture.
bool UpdateYCbCrTextureClient(TextureClient* aTexture, const PlanarYCbCrData& aData);

} // namespace layers
} // namespace mozilla

#endif
