/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef gc_Allocator_h
#define gc_Allocator_h

#include "gc/Heap.h"
#include "js/RootingAPI.h"

namespace js {
struct Class;

template <typename T, AllowGC allowGC>
JSObject *
Allocate(ExclusiveContext *cx, gc::AllocKind kind, size_t nDynamicSlots, gc::InitialHeap heap,
         const Class *clasp);

template <typename T>
JSObject *
Allocate(ExclusiveContext *cx, gc::AllocKind kind, size_t nDynamicSlots, gc::InitialHeap heap,
         const Class *clasp) {
  return Allocate<T, CanGC>(cx, kind, nDynamicSlots, heap, clasp);
}

template <typename T, AllowGC allowGC>
T *
Allocate(ExclusiveContext *cx);
template <typename T>
T *
Allocate(ExclusiveContext *cx) {
  return Allocate<T, CanGC>(cx);
}

} // namespace js

#endif // gc_Allocator_h
