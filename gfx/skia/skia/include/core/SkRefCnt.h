/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef SkRefCnt_DEFINED
#define SkRefCnt_DEFINED

#include "../private/SkAtomics.h"
#include "../private/SkTLogic.h"
#include "SkTypes.h"
#include <functional>
#include <memory>
#include <utility>

#define SK_SUPPORT_TRANSITION_TO_SP_INTERFACES

/** \class SkRefCntBase

    SkRefCntBase is the base class for objects that may be shared by multiple
    objects. When an existing owner wants to share a reference, it calls ref().
    When an owner wants to release its reference, it calls unref(). When the
    shared object's reference count goes to zero as the result of an unref()
    call, its (virtual) destructor is called. It is an error for the
    destructor to be called explicitly (or via the object going out of scope on
    the stack or calling delete) if getRefCnt() > 1.
*/
class SK_API SkRefCntBase : SkNoncopyable {
public:
    /** Default construct, initializing the reference count to 1.
    */
    SkRefCntBase() : fRefCnt(1) {}

    /** Destruct, asserting that the reference count is 1.
    */
    virtual ~SkRefCntBase() {
#ifdef SK_DEBUG
        SkASSERTF(fRefCnt == 1, "fRefCnt was %d", fRefCnt);
        fRefCnt = 0;    // illegal value, to catch us if we reuse after delete
#endif
    }

    /** Return the reference count. Use only for debugging. */
    int32_t getRefCnt() const { return fRefCnt; }

    /** May return true if the caller is the only owner.
     *  Ensures that all previous owner's actions are complete.
     */
    bool unique() const {
        if (1 == sk_atomic_load(&fRefCnt, sk_memory_order_acquire)) {
            // The acquire barrier is only really needed if we return true.  It
            // prevents code conditioned on the result of unique() from running
            // until previous owners are all totally done calling unref().
            return true;
        }
        return false;
    }

    /** Increment the reference count. Must be balanced by a call to unref().
    */
    void ref() const {
#ifdef SK_BUILD_FOR_ANDROID_FRAMEWORK
        // Android employs some special subclasses that enable the fRefCnt to
        // go to zero, but not below, prior to reusing the object.  This breaks
        // the use of unique() on such objects and as such should be removed
        // once the Android code is fixed.
        SkASSERT(fRefCnt >= 0);
#else
        SkASSERT(fRefCnt > 0);
#endif
        (void)sk_atomic_fetch_add(&fRefCnt, +1, sk_memory_order_relaxed);  // No barrier required.
    }

    /** Decrement the reference count. If the reference count is 1 before the
        decrement, then delete the object. Note that if this is the case, then
        the object needs to have been allocated via new, and not on the stack.
    */
    void unref() const {
        SkASSERT(fRefCnt > 0);
        // A release here acts in place of all releases we "should" have been doing in ref().
        if (1 == sk_atomic_fetch_add(&fRefCnt, -1, sk_memory_order_acq_rel)) {
            // Like unique(), the acquire is only needed on success, to make sure
            // code in internal_dispose() doesn't happen before the decrement.
            this->internal_dispose();
        }
    }

#ifdef SK_DEBUG
    void validate() const {
        SkASSERT(fRefCnt > 0);
    }
#endif

protected:
    /**
     *  Allow subclasses to call this if they've overridden internal_dispose
     *  so they can reset fRefCnt before the destructor is called. Should only
     *  be called right before calling through to inherited internal_dispose()
     *  or before calling the destructor.
     */
    void internal_dispose_restore_refcnt_to_1() const {
#ifdef SK_DEBUG
        SkASSERT(0 == fRefCnt);
        fRefCnt = 1;
#endif
    }

private:
    /**
     *  Called when the ref count goes to 0.
     */
    virtual void internal_dispose() const {
        this->internal_dispose_restore_refcnt_to_1();
        delete this;
    }

    // The following friends are those which override internal_dispose()
    // and conditionally call SkRefCnt::internal_dispose().
    friend class SkWeakRefCnt;

    mutable int32_t fRefCnt;

    typedef SkNoncopyable INHERITED;
};

#ifdef SK_REF_CNT_MIXIN_INCLUDE
// It is the responsibility of the following include to define the type SkRefCnt.
// This SkRefCnt should normally derive from SkRefCntBase.
#include SK_REF_CNT_MIXIN_INCLUDE
#else
class SK_API SkRefCnt : public SkRefCntBase { };
#endif

///////////////////////////////////////////////////////////////////////////////

/** Helper macro to safely assign one SkRefCnt[TS]* to another, checking for
    null in on each side of the assignment, and ensuring that ref() is called
    before unref(), in case the two pointers point to the same object.
 */
#define SkRefCnt_SafeAssign(dst, src)   \
    do {                                \
        if (src) src->ref();            \
        if (dst) dst->unref();          \
        dst = src;                      \
    } while (0)


/** Call obj->ref() and return obj. The obj must not be nullptr.
 */
template <typename T> static inline T* SkRef(T* obj) {
    SkASSERT(obj);
    obj->ref();
    return obj;
}

/** Check if the argument is non-null, and if so, call obj->ref() and return obj.
 */
template <typename T> static inline T* SkSafeRef(T* obj) {
    if (obj) {
        obj->ref();
    }
    return obj;
}

/** Check if the argument is non-null, and if so, call obj->unref()
 */
template <typename T> static inline void SkSafeUnref(T* obj) {
    if (obj) {
        obj->unref();
    }
}

template<typename T> static inline void SkSafeSetNull(T*& obj) {
    if (obj) {
        obj->unref();
        obj = nullptr;
    }
}

///////////////////////////////////////////////////////////////////////////////

template <typename T> struct SkTUnref {
    void operator()(T* t) { t->unref(); }
};

/**
 *  Utility class that simply unref's its argument in the destructor.
 */
template <typename T> class SkAutoTUnref : public std::unique_ptr<T, SkTUnref<T>> {
public:
    explicit SkAutoTUnref(T* obj = nullptr) : std::unique_ptr<T, SkTUnref<T>>(obj) {}

    operator T*() const { return this->get(); }

#if defined(SK_BUILD_FOR_ANDROID_FRAMEWORK)
    // Need to update graphics/Shader.cpp.
    T* detach() { return this->release(); }
#endif
};
// Can't use the #define trick below to guard a bare SkAutoTUnref(...) because it's templated. :(

class SkAutoUnref : public SkAutoTUnref<SkRefCnt> {
public:
    SkAutoUnref(SkRefCnt* obj) : SkAutoTUnref<SkRefCnt>(obj) {}
};
#define SkAutoUnref(...) SK_REQUIRE_LOCAL_VAR(SkAutoUnref)

// This is a variant of SkRefCnt that's Not Virtual, so weighs 4 bytes instead of 8 or 16.
// There's only benefit to using this if the deriving class does not otherwise need a vtable.
template <typename Derived>
class SkNVRefCnt : SkNoncopyable {
public:
    SkNVRefCnt() : fRefCnt(1) {}
    ~SkNVRefCnt() { SkASSERTF(1 == fRefCnt, "NVRefCnt was %d", fRefCnt); }

    // Implementation is pretty much the same as SkRefCntBase. All required barriers are the same:
    //   - unique() needs acquire when it returns true, and no barrier if it returns false;
    //   - ref() doesn't need any barrier;
    //   - unref() needs a release barrier, and an acquire if it's going to call delete.

    bool unique() const { return 1 == sk_atomic_load(&fRefCnt, sk_memory_order_acquire); }
    void    ref() const { (void)sk_atomic_fetch_add(&fRefCnt, +1, sk_memory_order_relaxed); }
    void  unref() const {
        if (1 == sk_atomic_fetch_add(&fRefCnt, -1, sk_memory_order_acq_rel)) {
            SkDEBUGCODE(fRefCnt = 1;)  // restore the 1 for our destructor's assert
            delete (const Derived*)this;
        }
    }
    void  deref() const { this->unref(); }

private:
    mutable int32_t fRefCnt;
};

///////////////////////////////////////////////////////////////////////////////////////////////////

/**
 *  Shared pointer class to wrap classes that support a ref()/unref() interface.
 *
 *  This can be used for classes inheriting from SkRefCnt, but it also works for other
 *  classes that match the interface, but have different internal choices: e.g. the hosted class
 *  may have its ref/unref be thread-safe, but that is not assumed/imposed by sk_sp.
 */
template <typename T> class sk_sp {
    /** Supports safe bool idiom. Obsolete with explicit operator bool. */
    using unspecified_bool_type = T* sk_sp::*;
public:
    using element_type = T;

    sk_sp() : fPtr(nullptr) {}
    sk_sp(std::nullptr_t) : fPtr(nullptr) {}

    /**
     *  Shares the underlying object by calling ref(), so that both the argument and the newly
     *  created sk_sp both have a reference to it.
     */
    sk_sp(const sk_sp<T>& that) : fPtr(SkSafeRef(that.get())) {}
    template <typename U, typename = skstd::enable_if_t<skstd::is_convertible<U*, T*>::value>>
    sk_sp(const sk_sp<U>& that) : fPtr(SkSafeRef(that.get())) {}

    /**
     *  Move the underlying object from the argument to the newly created sk_sp. Afterwards only
     *  the new sk_sp will have a reference to the object, and the argument will point to null.
     *  No call to ref() or unref() will be made.
     */
    sk_sp(sk_sp<T>&& that) : fPtr(that.release()) {}
    template <typename U, typename = skstd::enable_if_t<skstd::is_convertible<U*, T*>::value>>
    sk_sp(sk_sp<U>&& that) : fPtr(that.release()) {}

    /**
     *  Adopt the bare pointer into the newly created sk_sp.
     *  No call to ref() or unref() will be made.
     */
    explicit sk_sp(T* obj) : fPtr(obj) {}

    /**
     *  Calls unref() on the underlying object pointer.
     */
    ~sk_sp() {
        SkSafeUnref(fPtr);
        SkDEBUGCODE(fPtr = nullptr);
    }

    sk_sp<T>& operator=(std::nullptr_t) { this->reset(); return *this; }

    /**
     *  Shares the underlying object referenced by the argument by calling ref() on it. If this
     *  sk_sp previously had a reference to an object (i.e. not null) it will call unref() on that
     *  object.
     */
    sk_sp<T>& operator=(const sk_sp<T>& that) {
        this->reset(SkSafeRef(that.get()));
        return *this;
    }
    template <typename U, typename = skstd::enable_if_t<skstd::is_convertible<U*, T*>::value>>
    sk_sp<T>& operator=(const sk_sp<U>& that) {
        this->reset(SkSafeRef(that.get()));
        return *this;
    }

    /**
     *  Move the underlying object from the argument to the sk_sp. If the sk_sp previously held
     *  a reference to another object, unref() will be called on that object. No call to ref()
     *  will be made.
     */
    sk_sp<T>& operator=(sk_sp<T>&& that) {
        this->reset(that.release());
        return *this;
    }
    template <typename U, typename = skstd::enable_if_t<skstd::is_convertible<U*, T*>::value>>
    sk_sp<T>& operator=(sk_sp<U>&& that) {
        this->reset(that.release());
        return *this;
    }

    T& operator*() const {
        SkASSERT(this->get() != nullptr);
        return *this->get();
    }

    // MSVC 2013 does not work correctly with explicit operator bool.
    // https://chromium-cpp.appspot.com/#core-blacklist
    // When explicit operator bool can be used, remove operator! and operator unspecified_bool_type.
    //explicit operator bool() const { return this->get() != nullptr; }
    operator unspecified_bool_type() const { return this->get() ? &sk_sp::fPtr : nullptr; }
    bool operator!() const { return this->get() == nullptr; }

    T* get() const { return fPtr; }
    T* operator->() const { return fPtr; }

    /**
     *  Adopt the new bare pointer, and call unref() on any previously held object (if not null).
     *  No call to ref() will be made.
     */
    void reset(T* ptr = nullptr) {
        // Calling fPtr->unref() may call this->~() or this->reset(T*).
        // http://wg21.cmeerw.net/lwg/issue998
        // http://wg21.cmeerw.net/lwg/issue2262
        T* oldPtr = fPtr;
        fPtr = ptr;
        SkSafeUnref(oldPtr);
    }

    /**
     *  Return the bare pointer, and set the internal object pointer to nullptr.
     *  The caller must assume ownership of the object, and manage its reference count directly.
     *  No call to unref() will be made.
     */
    T* SK_WARN_UNUSED_RESULT release() {
        T* ptr = fPtr;
        fPtr = nullptr;
        return ptr;
    }

    void swap(sk_sp<T>& that) /*noexcept*/ {
        using std::swap;
        swap(fPtr, that.fPtr);
    }

private:
    T*  fPtr;
};

template <typename T> inline void swap(sk_sp<T>& a, sk_sp<T>& b) /*noexcept*/ {
    a.swap(b);
}

template <typename T, typename U> inline bool operator==(const sk_sp<T>& a, const sk_sp<U>& b) {
    return a.get() == b.get();
}
template <typename T> inline bool operator==(const sk_sp<T>& a, std::nullptr_t) /*noexcept*/ {
    return !a;
}
template <typename T> inline bool operator==(std::nullptr_t, const sk_sp<T>& b) /*noexcept*/ {
    return !b;
}

template <typename T, typename U> inline bool operator!=(const sk_sp<T>& a, const sk_sp<U>& b) {
    return a.get() != b.get();
}
template <typename T> inline bool operator!=(const sk_sp<T>& a, std::nullptr_t) /*noexcept*/ {
    return static_cast<bool>(a);
}
template <typename T> inline bool operator!=(std::nullptr_t, const sk_sp<T>& b) /*noexcept*/ {
    return static_cast<bool>(b);
}

template <typename T, typename U> inline bool operator<(const sk_sp<T>& a, const sk_sp<U>& b) {
    // Provide defined total order on sk_sp.
    // http://wg21.cmeerw.net/lwg/issue1297
    // http://wg21.cmeerw.net/lwg/issue1401 .
    return std::less<void*>()((void*)a.get(), (void*)b.get());
}
template <typename T> inline bool operator<(const sk_sp<T>& a, std::nullptr_t) {
    return std::less<T*>()(a.get(), nullptr);
}
template <typename T> inline bool operator<(std::nullptr_t, const sk_sp<T>& b) {
    return std::less<T*>()(nullptr, b.get());
}

template <typename T, typename U> inline bool operator<=(const sk_sp<T>& a, const sk_sp<U>& b) {
    return !(b < a);
}
template <typename T> inline bool operator<=(const sk_sp<T>& a, std::nullptr_t) {
    return !(nullptr < a);
}
template <typename T> inline bool operator<=(std::nullptr_t, const sk_sp<T>& b) {
    return !(b < nullptr);
}

template <typename T, typename U> inline bool operator>(const sk_sp<T>& a, const sk_sp<U>& b) {
    return b < a;
}
template <typename T> inline bool operator>(const sk_sp<T>& a, std::nullptr_t) {
    return nullptr < a;
}
template <typename T> inline bool operator>(std::nullptr_t, const sk_sp<T>& b) {
    return b < nullptr;
}

template <typename T, typename U> inline bool operator>=(const sk_sp<T>& a, const sk_sp<U>& b) {
    return !(a < b);
}
template <typename T> inline bool operator>=(const sk_sp<T>& a, std::nullptr_t) {
    return !(a < nullptr);
}
template <typename T> inline bool operator>=(std::nullptr_t, const sk_sp<T>& b) {
    return !(nullptr < b);
}

template <typename T, typename... Args>
sk_sp<T> sk_make_sp(Args&&... args) {
    return sk_sp<T>(new T(std::forward<Args>(args)...));
}

#ifdef SK_SUPPORT_TRANSITION_TO_SP_INTERFACES

/*
 *  Returns a sk_sp wrapping the provided ptr AND calls ref on it (if not null).
 *
 *  This is different than the semantics of the constructor for sk_sp, which just wraps the ptr,
 *  effectively "adopting" it.
 *
 *  This function may be helpful while we convert callers from ptr-based to sk_sp-based parameters.
 */
template <typename T> sk_sp<T> sk_ref_sp(T* obj) {
    return sk_sp<T>(SkSafeRef(obj));
}

#endif

#endif
