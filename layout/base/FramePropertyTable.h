/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef FRAMEPROPERTYTABLE_H_
#define FRAMEPROPERTYTABLE_H_

#include "mozilla/TypeTraits.h"
#include "mozilla/MemoryReporting.h"
#include "nsTArray.h"
#include "nsTHashtable.h"
#include "nsHashKeys.h"

class nsIFrame;

namespace mozilla {

struct FramePropertyDescriptorUntyped
{
  /**
   * mDestructor will be called if it's non-null.
   */
  typedef void UntypedDestructor(void* aPropertyValue);
  UntypedDestructor* mDestructor;
  /**
   * mDestructorWithFrame will be called if it's non-null and mDestructor
   * is null. WARNING: The frame passed to mDestructorWithFrame may
   * be a dangling frame pointer, if this is being called during
   * presshell teardown. Do not use it except to compare against
   * other frame pointers. No frame will have been allocated with
   * the same address yet.
   */
  typedef void UntypedDestructorWithFrame(const nsIFrame* aFrame,
                                          void* aPropertyValue);
  UntypedDestructorWithFrame* mDestructorWithFrame;
  /**
   * mDestructor and mDestructorWithFrame may both be null, in which case
   * no value destruction is a no-op.
   */

protected:
  /**
   * At most one destructor should be passed in. In general, you should
   * just use the static function FramePropertyDescriptor::New* below
   * instead of using this constructor directly.
   */
  MOZ_CONSTEXPR FramePropertyDescriptorUntyped(
    UntypedDestructor* aDtor, UntypedDestructorWithFrame* aDtorWithFrame)
    : mDestructor(aDtor)
    , mDestructorWithFrame(aDtorWithFrame)
  {}
};

/**
 * A pointer to a FramePropertyDescriptor serves as a unique property ID.
 * The FramePropertyDescriptor stores metadata about the property.
 * Currently the only metadata is a destructor function. The destructor
 * function is called on property values when they are overwritten or
 * deleted.
 *
 * To use this class, declare a global (i.e., file, class or function-scope
 * static member) FramePropertyDescriptor and pass its address as
 * aProperty in the FramePropertyTable methods.
 */
template<typename T>
struct FramePropertyDescriptor : public FramePropertyDescriptorUntyped
{
  typedef void Destructor(T* aPropertyValue);
  typedef void DestructorWithFrame(const nsIFrame* aaFrame,
                                   T* aPropertyValue);

  template<Destructor Dtor>
  static MOZ_CONSTEXPR const FramePropertyDescriptor<T> NewWithDestructor()
  {
    return { Destruct<Dtor>, nullptr };
  }

  template<DestructorWithFrame Dtor>
  static MOZ_CONSTEXPR
  const FramePropertyDescriptor<T> NewWithDestructorWithFrame()
  {
    return { nullptr, DestructWithFrame<Dtor> };
  }

  static MOZ_CONSTEXPR const FramePropertyDescriptor<T> NewWithoutDestructor()
  {
    return { nullptr, nullptr };
  }

private:
  MOZ_CONSTEXPR FramePropertyDescriptor(
    UntypedDestructor* aDtor, UntypedDestructorWithFrame* aDtorWithFrame)
    : FramePropertyDescriptorUntyped(aDtor, aDtorWithFrame)
  {}

  template<Destructor Dtor>
  static void Destruct(void* aPropertyValue)
  {
    Dtor(static_cast<T*>(aPropertyValue));
  }

  template<DestructorWithFrame Dtor>
  static void DestructWithFrame(const nsIFrame* aFrame, void* aPropertyValue)
  {
    Dtor(aFrame, static_cast<T*>(aPropertyValue));
  }
};

// SmallValueHolder<T> is a placeholder intended to be used as template
// argument of FramePropertyDescriptor for types which can fit into the
// size of a pointer directly. This class should never be defined, so
// that we won't use it for unexpected purpose by mistake.
template<typename T>
class SmallValueHolder;

namespace detail {

template<typename T>
struct FramePropertyTypeHelper
{
  typedef T* Type;
};
template<typename T>
struct FramePropertyTypeHelper<SmallValueHolder<T>>
{
  typedef T Type;
};

}

/**
 * The FramePropertyTable is optimized for storing 0 or 1 properties on
 * a given frame. Storing very large numbers of properties on a single
 * frame will not be efficient.
 * 
 * Property values are passed as void* but do not actually have to be
 * valid pointers. You can use NS_INT32_TO_PTR/NS_PTR_TO_INT32 to
 * store int32_t values. Null/zero values can be stored and retrieved.
 * Of course, the destructor function (if any) must handle such values
 * correctly.
 */
class FramePropertyTable {
public:
  template<typename T>
  using Descriptor = const FramePropertyDescriptor<T>*;
  using UntypedDescriptor = const FramePropertyDescriptorUntyped*;

  template<typename T>
  using PropertyType = typename detail::FramePropertyTypeHelper<T>::Type;

  FramePropertyTable() : mLastFrame(nullptr), mLastEntry(nullptr)
  {
  }
  ~FramePropertyTable()
  {
    DeleteAll();
  }

  /**
   * Set a property value on a frame. This requires one hashtable
   * lookup (using the frame as the key) and a linear search through
   * the properties of that frame. Any existing value for the property
   * is destroyed.
   */
  template<typename T>
  void Set(const nsIFrame* aFrame, Descriptor<T> aProperty,
           PropertyType<T> aValue)
  {
    ReinterpretHelper<T> helper{};
    helper.value = aValue;
    SetInternal(aFrame, aProperty, helper.ptr);
  }
  /**
   * Get a property value for a frame. This requires one hashtable
   * lookup (using the frame as the key) and a linear search through
   * the properties of that frame. If the frame has no such property,
   * returns zero-filled result, which means null for pointers and
   * zero for integers and floating point types.
   * @param aFoundResult if non-null, receives a value 'true' iff
   * the frame has a value for the property. This lets callers
   * disambiguate a null result, which can mean 'no such property' or
   * 'property value is null'.
   */
  template<typename T>
  PropertyType<T> Get(const nsIFrame* aFrame, Descriptor<T> aProperty,
                      bool* aFoundResult = nullptr)
  {
    ReinterpretHelper<T> helper;
    helper.ptr = GetInternal(aFrame, aProperty, aFoundResult);
    return helper.value;
  }
  /**
   * Remove a property value for a frame. This requires one hashtable
   * lookup (using the frame as the key) and a linear search through
   * the properties of that frame. The old property value is returned
   * (and not destroyed). If the frame has no such property,
   * returns zero-filled result, which means null for pointers and
   * zero for integers and floating point types.
   * @param aFoundResult if non-null, receives a value 'true' iff
   * the frame had a value for the property. This lets callers
   * disambiguate a null result, which can mean 'no such property' or
   * 'property value is null'.
   */
  template<typename T>
  PropertyType<T> Remove(const nsIFrame* aFrame, Descriptor<T> aProperty,
                         bool* aFoundResult = nullptr)
  {
    ReinterpretHelper<T> helper;
    helper.ptr = RemoveInternal(aFrame, aProperty, aFoundResult);
    return helper.value;
  }
  /**
   * Remove and destroy a property value for a frame. This requires one
   * hashtable lookup (using the frame as the key) and a linear search
   * through the properties of that frame. If the frame has no such
   * property, nothing happens.
   */
  template<typename T>
  void Delete(const nsIFrame* aFrame, Descriptor<T> aProperty)
  {
    DeleteInternal(aFrame, aProperty);
  }
  /**
   * Remove and destroy all property values for a frame. This requires one
   * hashtable lookup (using the frame as the key).
   */
  void DeleteAllFor(const nsIFrame* aFrame);
  /**
   * Remove and destroy all property values for all frames.
   */
  void DeleteAll();

  /**
   * Check if a property exists (added for TenFourFox issue 493).
   */
  template<typename T>
  bool Has(const nsIFrame* aFrame, Descriptor<T> aProperty)
  {
    bool foundResult = false;
    mozilla::Unused << GetInternal(aFrame, aProperty, &foundResult);
    return foundResult;
  }

  size_t SizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf) const;

protected:
  void SetInternal(const nsIFrame* aFrame, UntypedDescriptor aProperty,
                   void* aValue);

  void* GetInternal(const nsIFrame* aFrame, UntypedDescriptor aProperty,
                    bool* aFoundResult);

  void* RemoveInternal(const nsIFrame* aFrame, UntypedDescriptor aProperty,
                       bool* aFoundResult);

  void DeleteInternal(const nsIFrame* aFrame, UntypedDescriptor aProperty);

  // A helper union being used here rather than simple reinterpret_cast
  // is because PropertyType<T> could be float, bool, uint8_t, etc.
  // which do not have defined behavior for a direct cast.
  template<typename T>
  union ReinterpretHelper
  {
    void* ptr;
    PropertyType<T> value;
    static_assert(sizeof(PropertyType<T>) <= sizeof(void*),
                  "size of the value must never be larger than a pointer");
  };

  /**
   * Stores a property descriptor/value pair. It can also be used to
   * store an nsTArray of PropertyValues.
   */
  struct PropertyValue {
    PropertyValue() : mProperty(nullptr), mValue(nullptr) {}
    PropertyValue(UntypedDescriptor aProperty, void* aValue)
      : mProperty(aProperty), mValue(aValue) {}

    bool IsArray() { return !mProperty && mValue; }
    nsTArray<PropertyValue>* ToArray()
    {
      NS_ASSERTION(IsArray(), "Must be array");
      return reinterpret_cast<nsTArray<PropertyValue>*>(&mValue);
    }

    void DestroyValueFor(const nsIFrame* aFrame) {
      if (mProperty->mDestructor) {
        mProperty->mDestructor(mValue);
      } else if (mProperty->mDestructorWithFrame) {
        mProperty->mDestructorWithFrame(aFrame, mValue);
      }
    }

    size_t SizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf) {
      size_t n = 0;
      // We don't need to measure mProperty because it always points to static
      // memory.  As for mValue:  if it's a single value we can't measure it,
      // because the type is opaque;  if it's an array, we measure the array
      // storage, but we can't measure the individual values, again because
      // their types are opaque.
      if (IsArray()) {
        nsTArray<PropertyValue>* array = ToArray();
        n += array->ShallowSizeOfExcludingThis(aMallocSizeOf);
      }
      return n;
    }

    UntypedDescriptor mProperty;
    void* mValue;
  };

  /**
   * Used with an array of PropertyValues to allow lookups that compare
   * only on the FramePropertyDescriptor.
   */
  class PropertyComparator {
  public:
    bool Equals(const PropertyValue& a, const PropertyValue& b) const {
      return a.mProperty == b.mProperty;
    }
    bool Equals(UntypedDescriptor a, const PropertyValue& b) const {
      return a == b.mProperty;
    }
    bool Equals(const PropertyValue& a, UntypedDescriptor b) const {
      return a.mProperty == b;
    }
  };

  /**
   * Our hashtable entry. The key is an nsIFrame*, the value is a
   * PropertyValue representing one or more property/value pairs.
   */
  class Entry : public nsPtrHashKey<const nsIFrame>
  {
  public:
    explicit Entry(KeyTypePointer aKey) : nsPtrHashKey<const nsIFrame>(aKey) {}
    Entry(const Entry &toCopy) :
      nsPtrHashKey<const nsIFrame>(toCopy), mProp(toCopy.mProp) {}

    size_t SizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf) {
      return mProp.SizeOfExcludingThis(aMallocSizeOf);
    }

    PropertyValue mProp;
  };

  static void DeleteAllForEntry(Entry* aEntry);

  // Note that mLastEntry points into mEntries, so we need to be careful about
  // not triggering a resize of mEntries, e.g. use RawRemoveEntry() instead of
  // RemoveEntry() in some places.
  nsTHashtable<Entry> mEntries;
  const nsIFrame* mLastFrame;
  Entry* mLastEntry;
};

/**
 * This class encapsulates the properties of a frame.
 */
class FrameProperties {
public:
  template<typename T> using Descriptor = FramePropertyTable::Descriptor<T>;
  template<typename T> using PropertyType = FramePropertyTable::PropertyType<T>;

  FrameProperties(FramePropertyTable* aTable, const nsIFrame* aFrame)
    : mTable(aTable), mFrame(aFrame) {}

  template<typename T>
  void Set(Descriptor<T> aProperty, PropertyType<T> aValue) const
  {
    mTable->Set(mFrame, aProperty, aValue);
  }
  template<typename T>
  PropertyType<T> Get(Descriptor<T> aProperty,
                      bool* aFoundResult = nullptr) const
  {
    return mTable->Get(mFrame, aProperty, aFoundResult);
  }
  template<typename T>
  PropertyType<T> Remove(Descriptor<T> aProperty,
                         bool* aFoundResult = nullptr) const
  {
    return mTable->Remove(mFrame, aProperty, aFoundResult);
  }
  template<typename T>
  void Delete(Descriptor<T> aProperty)
  {
    mTable->Delete(mFrame, aProperty);
  }

  template<typename T>
  bool Has(Descriptor<T> aProperty) const
  {
    return mTable->Has(mFrame, aProperty);
  }

private:
  FramePropertyTable* mTable;
  const nsIFrame* mFrame;
};

} // namespace mozilla

#endif /* FRAMEPROPERTYTABLE_H_ */
