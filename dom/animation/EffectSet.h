/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_EffectSet_h
#define mozilla_EffectSet_h

#include "nsCSSPseudoElements.h" // For nsCSSPseudoElements::Type
#include "nsHashKeys.h" // For nsPtrHashKey
#include "nsTHashtable.h" // For nsTHashtable

namespace mozilla {

namespace dom {
class Element;
class KeyframeEffectReadOnly;
} // namespace dom

// A wrapper around a hashset of AnimationEffect objects to handle
// storing the set as a property of an element.
class EffectSet
{
public:
  EffectSet()
    : mCascadeNeedsUpdate(false)
#ifdef DEBUG
    , mCalledPropertyDtor(false)
#endif
  {
    MOZ_COUNT_CTOR(EffectSet);
  }

  ~EffectSet()
  {
    MOZ_ASSERT(mCalledPropertyDtor,
               "must call destructor through element property dtor");
    MOZ_COUNT_DTOR(EffectSet);
  }
  static void PropertyDtor(void* aObject, nsIAtom* aPropertyName,
                           void* aPropertyValue, void* aData);

  // Methods for supporting cycle-collection
  void Traverse(nsCycleCollectionTraversalCallback& aCallback);

  static EffectSet* GetEffectSet(dom::Element* aElement,
                                 nsCSSPseudoElements::Type aPseudoType);
  static EffectSet* GetEffectSet(const nsIFrame* aFrame);
  static EffectSet* GetOrCreateEffectSet(dom::Element* aElement,
                                         nsCSSPseudoElements::Type aPseudoType);

  void AddEffect(dom::KeyframeEffectReadOnly& aEffect);
  void RemoveEffect(dom::KeyframeEffectReadOnly& aEffect);

private:
  typedef nsTHashtable<nsRefPtrHashKey<dom::KeyframeEffectReadOnly>>
    OwningEffectSet;

public:
  // A simple iterator to support iterating over the effects in this object in
  // range-based for loops.
  //
  // This allows us to avoid exposing mEffects directly and saves the
  // caller from having to dereference hashtable iterators using
  // the rather complicated: iter.Get()->GetKey().
  class Iterator
  {
  public:
    explicit Iterator(OwningEffectSet::Iterator&& aHashIterator)
      : mHashIterator(mozilla::Move(aHashIterator))
      , mIsEndIterator(false) { }
    Iterator(Iterator&& aOther)
      : mHashIterator(mozilla::Move(aOther.mHashIterator))
      , mIsEndIterator(aOther.mIsEndIterator) { }

    static Iterator EndIterator(OwningEffectSet::Iterator&& aHashIterator)
    {
      Iterator result(mozilla::Move(aHashIterator));
      result.mIsEndIterator = true;
      return result;
    }

    bool operator!=(const Iterator& aOther) const {
      if (Done() || aOther.Done()) {
        return Done() != aOther.Done();
      }
      return mHashIterator.Get() != aOther.mHashIterator.Get();
    }

    Iterator& operator++() {
      MOZ_ASSERT(!Done());
      mHashIterator.Next();
      return *this;
    }

    dom::KeyframeEffectReadOnly* operator* ()
    {
      MOZ_ASSERT(!Done());
      return mHashIterator.Get()->GetKey();
    }

  private:
    Iterator() = delete;
    Iterator(const Iterator&) = delete;
    Iterator& operator=(const Iterator&) = delete;
    Iterator& operator=(const Iterator&&) = delete;

    bool Done() const {
      return mIsEndIterator || mHashIterator.Done();
    }

    OwningEffectSet::Iterator mHashIterator;
    bool mIsEndIterator;
  };

  Iterator begin() { return Iterator(mEffects.Iter()); }
  Iterator end()
  {
    return Iterator::EndIterator(mEffects.Iter());
  }
  bool IsEmpty() const { return mEffects.IsEmpty(); }

  bool CascadeNeedsUpdate() const { return mCascadeNeedsUpdate; }
  void MarkCascadeNeedsUpdate() { mCascadeNeedsUpdate = true; }
  void MarkCascadeUpdated() { mCascadeNeedsUpdate = false; }

  static nsIAtom** GetEffectSetPropertyAtoms();

private:
  static nsIAtom* GetEffectSetPropertyAtom(nsCSSPseudoElements::Type
                                             aPseudoType);

  OwningEffectSet mEffects;

  // Dirty flag to represent when the mWinsInCascade flag on effects in
  // this set might need to be updated.
  //
  // Set to true any time the set of effects is changed or when
  // one the effects goes in or out of the "in effect" state.
  bool mCascadeNeedsUpdate;

#ifdef DEBUG
  bool mCalledPropertyDtor;
#endif
};

} // namespace mozilla

#endif // mozilla_EffectSet_h
