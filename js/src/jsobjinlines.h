/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jsobjinlines_h
#define jsobjinlines_h

#include "jsobj.h"

#include "builtin/MapObject.h"
#include "builtin/TypedObject.h"
#include "gc/Allocator.h"
#include "vm/ArrayObject.h"
#include "vm/DateObject.h"
#include "vm/NumberObject.h"
#include "vm/Probes.h"
#include "vm/ScopeObject.h"
#include "vm/StringObject.h"
#include "vm/TypedArrayCommon.h"

#include "jsatominlines.h"
#include "jscompartmentinlines.h"
#include "jsgcinlines.h"

#include "vm/TypeInference-inl.h"

inline js::Shape *
JSObject::maybeShape() const
{
    if (is<js::UnboxedPlainObject>())
        return nullptr;
    return *reinterpret_cast<js::Shape **>(uintptr_t(this) + offsetOfShape());
}

inline js::Shape *
JSObject::ensureShape(js::ExclusiveContext *cx)
{
    if (is<js::UnboxedPlainObject>() && !js::UnboxedPlainObject::convertToNative(cx->asJSContext(), this))
        return nullptr;
    js::Shape *shape = maybeShape();
    MOZ_ASSERT(shape);
    return shape;
}

inline void
JSObject::finalize(js::FreeOp* fop)
{
    js::probes::FinalizeObject(this);

#ifdef DEBUG
    MOZ_ASSERT(isTenured());
    if (!IsBackgroundFinalized(asTenured().getAllocKind())) {
        /* Assert we're on the main thread. */
        MOZ_ASSERT(CurrentThreadCanAccessRuntime(fop->runtime()));
    }
#endif
    const js::Class* clasp = getClass();
    if (clasp->finalize)
        clasp->finalize(fop, this);

    if (!clasp->isNative())
        return;

    js::NativeObject* nobj = &as<js::NativeObject>();

    if (nobj->hasDynamicSlots())
        fop->free_(nobj->slots_);

    if (nobj->hasDynamicElements()) {
        js::ObjectElements* elements = nobj->getElementsHeader();
        if (elements->isCopyOnWrite()) {
            if (elements->ownerObject() == this) {
                // Don't free the elements until object finalization finishes,
                // so that other objects can access these elements while they
                // are themselves finalized.
                fop->freeLater(elements);
            }
        } else {
            fop->free_(elements);
        }
    }

    // For dictionary objects (which must be native), it's possible that
    // unreachable shapes may be marked whose listp points into this object.
    // In case this happens, null out the shape's pointer here so that a moving
    // GC will not try to access the dead object.
    if (nobj->shape_->listp == &nobj->shape_)
        nobj->shape_->listp = nullptr;
}

/* static */ inline bool
JSObject::setSingleton(js::ExclusiveContext* cx, js::HandleObject obj)
{
    MOZ_ASSERT_IF(cx->isJSContext(), !IsInsideNursery(obj));

    js::ObjectGroup* group = js::ObjectGroup::lazySingletonGroup(cx, obj->getClass(),
                                                                 obj->getTaggedProto());
    if (!group)
        return false;

    obj->group_ = group;
    return true;
}

inline js::ObjectGroup*
JSObject::getGroup(JSContext* cx)
{
    MOZ_ASSERT(cx->compartment() == compartment());
    if (hasLazyGroup()) {
        JS::RootedObject self(cx, this);
        if (cx->compartment() != compartment())
            MOZ_CRASH();
        return makeLazyGroup(cx, self);
    }
    return group_;
}

inline void
JSObject::setGroup(js::ObjectGroup* group)
{
    MOZ_ASSERT(group);
    MOZ_ASSERT(!isSingleton());
    group_ = group;
}


/*** Standard internal methods *******************************************************************/

inline bool
js::GetPrototype(JSContext* cx, js::HandleObject obj, js::MutableHandleObject protop)
{
    if (obj->getTaggedProto().isLazy()) {
        MOZ_ASSERT(obj->is<js::ProxyObject>());
        return js::Proxy::getPrototype(cx, obj, protop);
    } else {
        protop.set(obj->getTaggedProto().toObjectOrNull());
        return true;
    }
}

inline bool
js::IsExtensible(ExclusiveContext* cx, HandleObject obj, bool* extensible)
{
    if (obj->is<ProxyObject>()) {
        if (!cx->shouldBeJSContext())
            return false;
        return Proxy::isExtensible(cx->asJSContext(), obj, extensible);
    }

    *extensible = obj->nonProxyIsExtensible();
    return true;
}

inline bool
js::HasProperty(JSContext* cx, HandleObject obj, PropertyName* name, bool* found)
{
    RootedId id(cx, NameToId(name));
    return HasProperty(cx, obj, id, found);
}

inline bool
js::GetElement(JSContext* cx, HandleObject obj, HandleObject receiver, uint32_t index,
               MutableHandleValue vp)
{
    RootedId id(cx);
    if (!IndexToId(cx, index, &id))
        return false;
    return GetProperty(cx, obj, receiver, id, vp);
}

inline bool
js::GetElementNoGC(JSContext* cx, JSObject* obj, JSObject* receiver, uint32_t index, Value* vp)
{
    if (obj->getOps()->getProperty)
        return false;

    if (index > JSID_INT_MAX)
        return false;
    return GetPropertyNoGC(cx, obj, receiver, INT_TO_JSID(index), vp);
}

inline bool
js::DeleteProperty(JSContext *cx, HandleObject obj, HandleId id, ObjectOpResult &result)
{
    MarkTypePropertyNonData(cx, obj, id);
    if (DeletePropertyOp op = obj->getOps()->deleteProperty)
        return op(cx, obj, id, result);
    return NativeDeleteProperty(cx, obj.as<NativeObject>(), id, result);
}

inline bool
js::DeleteElement(JSContext *cx, HandleObject obj, uint32_t index, ObjectOpResult &result)
{
    RootedId id(cx);
    if (!IndexToId(cx, index, &id))
        return false;
    return DeleteProperty(cx, obj, id, result);
}


/* * */

inline bool
JSObject::isQualifiedVarObj()
{
    if (is<js::DebugScopeObject>())
        return as<js::DebugScopeObject>().scope().isQualifiedVarObj();
    return hasAllFlags(js::BaseShape::QUALIFIED_VAROBJ);
}

inline bool
JSObject::isUnqualifiedVarObj()
{
    if (is<js::DebugScopeObject>())
        return as<js::DebugScopeObject>().scope().isUnqualifiedVarObj();
    return hasAllFlags(js::BaseShape::UNQUALIFIED_VAROBJ);
}

namespace js {

inline bool
ClassCanHaveFixedData(const Class* clasp)
{
    // Normally, the number of fixed slots given an object is the maximum
    // permitted for its size class. For array buffers and non-shared typed
    // arrays we only use enough to cover the class reserved slots, so that
    // the remaining space in the object's allocation is available for the
    // buffer's data.
    return !clasp->isNative()
        || clasp == &js::ArrayBufferObject::class_
        || js::IsTypedArrayClass(clasp);
}

} // namespace js

/* static */ inline JSObject*
JSObject::create(js::ExclusiveContext* cx, js::gc::AllocKind kind, js::gc::InitialHeap heap,
                 js::HandleShape shape, js::HandleObjectGroup group)
{
    MOZ_ASSERT(shape && group);
    MOZ_ASSERT(group->clasp() == shape->getObjectClass());
    MOZ_ASSERT(group->clasp() != &js::ArrayObject::class_);
    MOZ_ASSERT_IF(!js::ClassCanHaveFixedData(group->clasp()),
                  js::gc::GetGCKindSlots(kind, group->clasp()) == shape->numFixedSlots());
    MOZ_ASSERT_IF(group->clasp()->flags & JSCLASS_BACKGROUND_FINALIZE,
                  IsBackgroundFinalized(kind));
    MOZ_ASSERT_IF(group->clasp()->finalize,
                  heap == js::gc::TenuredHeap ||
                  (group->clasp()->flags & JSCLASS_FINALIZE_FROM_NURSERY));

    // Non-native classes cannot have reserved slots or private data, and the
    // objects can't have any fixed slots, for compatibility with
    // GetReservedOrProxyPrivateSlot.
    MOZ_ASSERT_IF(!group->clasp()->isNative(), JSCLASS_RESERVED_SLOTS(group->clasp()) == 0);
    MOZ_ASSERT_IF(!group->clasp()->isNative(), !group->clasp()->hasPrivate());
    MOZ_ASSERT_IF(!group->clasp()->isNative(), shape->numFixedSlots() == 0);
    MOZ_ASSERT_IF(!group->clasp()->isNative(), shape->slotSpan() == 0);

    const js::Class *clasp = group->clasp();
    size_t nDynamicSlots =
        js::NativeObject::dynamicSlotsCount(shape->numFixedSlots(), shape->slotSpan(), clasp);

    JSObject *obj = js::Allocate<JSObject>(cx, kind, nDynamicSlots, heap, clasp);
    if (!obj)
        return nullptr;

    obj->group_.init(group);

    obj->setInitialShapeMaybeNonNative(shape);

    // Note: slots are created and assigned internally by Allocate<JSObject>.
    obj->setInitialElementsMaybeNonNative(js::emptyObjectElements);

    if (clasp->hasPrivate())
        obj->as<js::NativeObject>().privateRef(shape->numFixedSlots()) = nullptr;

    if (size_t span = shape->slotSpan())
        obj->as<js::NativeObject>().initializeSlotRange(0, span);

    // JSFunction's fixed slots expect POD-style initialization.
    if (group->clasp()->isJSFunction())
        memset(obj->as<JSFunction>().fixedSlots(), 0, sizeof(js::HeapSlot) * GetGCKindSlots(kind));

    js::gc::TraceCreateObject(obj);

    return obj;
}

inline void
JSObject::setInitialShapeMaybeNonNative(js::Shape *shape)
{
    static_cast<js::NativeObject *>(this)->shape_.init(shape);
}

inline void
JSObject::setShapeMaybeNonNative(js::Shape *shape)
{
    MOZ_ASSERT(!is<js::UnboxedPlainObject>());
    static_cast<js::NativeObject *>(this)->shape_ = shape;
}

inline void
JSObject::setInitialSlotsMaybeNonNative(js::HeapSlot* slots)
{
    static_cast<js::NativeObject*>(this)->slots_ = slots;
}

inline void
JSObject::setInitialElementsMaybeNonNative(js::HeapSlot *elements)
{
    static_cast<js::NativeObject *>(this)->elements_ = elements;
}

inline JSObject *
JSObject::getMetadata() const
{
    if (js::Shape *shape = maybeShape())
        return shape->getObjectMetadata();
    return nullptr;
}

inline js::GlobalObject &
JSObject::global() const
{
    /*
     * The global is read-barriered so that it is kept live by access through
     * the JSCompartment. When accessed through a JSObject, however, the global
     * will be already be kept live by the black JSObject's parent pointer, so
     * does not need to be read-barriered.
     */
    return *compartment()->unsafeUnbarrieredMaybeGlobal();
}

inline bool
JSObject::isOwnGlobal() const
{
    return &global() == this;
}

inline bool
JSObject::hasAllFlags(js::BaseShape::Flag flags) const
{
    MOZ_ASSERT(flags);
    if (js::Shape *shape = maybeShape())
        return shape->hasAllObjectFlags(flags);
    return false;
}

inline bool
JSObject::nonProxyIsExtensible() const
{
    MOZ_ASSERT(!uninlinedIsProxy());

    // [[Extensible]] for ordinary non-proxy objects is an object flag.
    return !hasAllFlags(js::BaseShape::NOT_EXTENSIBLE);
}

inline bool
JSObject::isBoundFunction() const
{
    return hasAllFlags(js::BaseShape::BOUND_FUNCTION);
}

inline bool
JSObject::watched() const
{
    return hasAllFlags(js::BaseShape::WATCHED);
}

inline bool
JSObject::isDelegate() const
{
    return hasAllFlags(js::BaseShape::DELEGATE);
}

inline bool
JSObject::hasUncacheableProto() const
{
    return hasAllFlags(js::BaseShape::UNCACHEABLE_PROTO);
}

inline bool
JSObject::hadElementsAccess() const
{
    return hasAllFlags(js::BaseShape::HAD_ELEMENTS_ACCESS);
}

inline bool
JSObject::isIndexed() const
{
    return hasAllFlags(js::BaseShape::INDEXED);
}

inline bool
JSObject::nonLazyPrototypeIsImmutable() const
{
    MOZ_ASSERT(!hasLazyPrototype());
    return hasAllFlags(js::BaseShape::IMMUTABLE_PROTOTYPE);
}

inline bool
JSObject::isIteratedSingleton() const
{
    return hasAllFlags(js::BaseShape::ITERATED_SINGLETON);
}

inline bool
JSObject::isNewGroupUnknown() const
{
    return hasAllFlags(js::BaseShape::NEW_GROUP_UNKNOWN);
}

inline bool
JSObject::wasNewScriptCleared() const
{
    return hasAllFlags(js::BaseShape::NEW_SCRIPT_CLEARED);
}

namespace js {

static MOZ_ALWAYS_INLINE bool
IsFunctionObject(const js::Value &v)
{
    return v.isObject() && v.toObject().is<JSFunction>();
}

static MOZ_ALWAYS_INLINE bool
IsFunctionObject(const js::Value& v, JSFunction** fun)
{
    if (v.isObject() && v.toObject().is<JSFunction>()) {
        *fun = &v.toObject().as<JSFunction>();
        return true;
    }
    return false;
}

static MOZ_ALWAYS_INLINE bool
IsNativeFunction(const js::Value& v)
{
    JSFunction* fun;
    return IsFunctionObject(v, &fun) && fun->isNative();
}

static MOZ_ALWAYS_INLINE bool
IsNativeFunction(const js::Value& v, JSFunction** fun)
{
    return IsFunctionObject(v, fun) && (*fun)->isNative();
}

static MOZ_ALWAYS_INLINE bool
IsNativeFunction(const js::Value& v, JSNative native)
{
    JSFunction* fun;
    return IsFunctionObject(v, &fun) && fun->maybeNative() == native;
}

/*
 * When we have an object of a builtin class, we don't quite know what its
 * valueOf/toString methods are, since these methods may have been overwritten
 * or shadowed. However, we can still do better than the general case by
 * hard-coding the necessary properties for us to find the native we expect.
 *
 * TODO: a per-thread shape-based cache would be faster and simpler.
 */
static MOZ_ALWAYS_INLINE bool
ClassMethodIsNative(JSContext* cx, NativeObject* obj, const Class* clasp, jsid methodid, JSNative native)
{
    MOZ_ASSERT(obj->getClass() == clasp);

    Value v;
    if (!HasDataProperty(cx, obj, methodid, &v)) {
        JSObject* proto = obj->getProto();
        if (!proto || proto->getClass() != clasp || !HasDataProperty(cx, &proto->as<NativeObject>(), methodid, &v))
            return false;
    }

    return IsNativeFunction(v, native);
}

// Return whether looking up 'valueOf' on 'obj' definitely resolves to the
// original Object.prototype.valueOf. The method may conservatively return
// 'false' in the case of proxies or other non-native objects.
static MOZ_ALWAYS_INLINE bool
HasObjectValueOf(JSObject* obj, JSContext* cx)
{
    if (obj->is<ProxyObject>() || !obj->isNative())
        return false;

    jsid valueOf = NameToId(cx->names().valueOf);

    Value v;
    while (!HasDataProperty(cx, &obj->as<NativeObject>(), valueOf, &v)) {
        obj = obj->getProto();
        if (!obj || obj->is<ProxyObject>() || !obj->isNative())
            return false;
    }

    return IsNativeFunction(v, obj_valueOf);
}

/* ES5 9.1 ToPrimitive(input). */
MOZ_ALWAYS_INLINE bool
ToPrimitive(JSContext* cx, MutableHandleValue vp)
{
    if (vp.isPrimitive())
        return true;

    JSObject* obj = &vp.toObject();

    /* Optimize new String(...).valueOf(). */
    if (obj->is<StringObject>()) {
        jsid id = NameToId(cx->names().valueOf);
        StringObject* nobj = &obj->as<StringObject>();
        if (ClassMethodIsNative(cx, nobj, &StringObject::class_, id, str_toString)) {
            vp.setString(nobj->unbox());
            return true;
        }
    }

    /* Optimize new Number(...).valueOf(). */
    if (obj->is<NumberObject>()) {
        jsid id = NameToId(cx->names().valueOf);
        NumberObject* nobj = &obj->as<NumberObject>();
        if (ClassMethodIsNative(cx, nobj, &NumberObject::class_, id, num_valueOf)) {
            vp.setNumber(nobj->unbox());
            return true;
        }
    }

    RootedObject objRoot(cx, obj);
    return ToPrimitive(cx, objRoot, JSTYPE_VOID, vp);
}

/* ES5 9.1 ToPrimitive(input, PreferredType). */
MOZ_ALWAYS_INLINE bool
ToPrimitive(JSContext* cx, JSType preferredType, MutableHandleValue vp)
{
    MOZ_ASSERT(preferredType != JSTYPE_VOID); /* Use the other ToPrimitive! */
    if (vp.isPrimitive())
        return true;
    RootedObject obj(cx, &vp.toObject());
    return ToPrimitive(cx, obj, preferredType, vp);
}

/*
 * Return true if this is a compiler-created internal function accessed by
 * its own object. Such a function object must not be accessible to script
 * or embedding code.
 */
inline bool
IsInternalFunctionObject(JSObject *funobj)
{
    JSFunction *fun = &funobj->as<JSFunction>();
    MOZ_ASSERT_IF(fun->isLambda(),
                  fun->isInterpreted() || fun->isAsmJSNative());
    return fun->isLambda() && fun->isInterpreted() && !fun->environment();
}

class AutoPropertyDescriptorVector : public AutoVectorRooter<PropertyDescriptor>
{
  public:
    explicit AutoPropertyDescriptorVector(JSContext *cx
                                          MOZ_GUARD_OBJECT_NOTIFIER_PARAM)
        : AutoVectorRooter(cx, DESCVECTOR)
    {
        MOZ_GUARD_OBJECT_NOTIFIER_INIT;
    }

    MOZ_DECL_USE_GUARD_OBJECT_NOTIFIER
};

/*
 * Make an object with the specified prototype. If parent is null, it will
 * default to the prototype's global if the prototype is non-null.
 */
JSObject*
NewObjectWithGivenTaggedProto(ExclusiveContext* cx, const Class* clasp, Handle<TaggedProto> proto,
                              HandleObject parent, gc::AllocKind allocKind, NewObjectKind newKind);

inline JSObject*
NewObjectWithGivenTaggedProto(ExclusiveContext* cx, const Class* clasp, Handle<TaggedProto> proto,
                              HandleObject parent, NewObjectKind newKind = GenericObject)
{
    gc::AllocKind allocKind = gc::GetGCObjectKind(clasp);
    return NewObjectWithGivenTaggedProto(cx, clasp, proto, parent, allocKind, newKind);
}

template <typename T>
inline T*
NewObjectWithGivenTaggedProto(ExclusiveContext* cx, Handle<TaggedProto> proto, HandleObject parent,
                              NewObjectKind newKind = GenericObject)
{
    JSObject* obj = NewObjectWithGivenTaggedProto(cx, &T::class_, proto, parent, newKind);
    return obj ? &obj->as<T>() : nullptr;
}

inline JSObject*
NewObjectWithGivenProto(ExclusiveContext* cx, const Class* clasp, HandleObject proto,
                        HandleObject parent, gc::AllocKind allocKind, NewObjectKind newKind)
{
    return NewObjectWithGivenTaggedProto(cx, clasp, AsTaggedProto(proto), parent, allocKind,
                                         newKind);
}

inline JSObject*
NewObjectWithGivenProto(ExclusiveContext* cx, const Class* clasp, HandleObject proto,
                        HandleObject parent, NewObjectKind newKind = GenericObject)
{
    return NewObjectWithGivenTaggedProto(cx, clasp, AsTaggedProto(proto), parent, newKind);
}

template <typename T>
inline T*
NewObjectWithGivenProto(ExclusiveContext* cx, HandleObject proto, HandleObject parent,
                        NewObjectKind newKind = GenericObject)
{
    return NewObjectWithGivenTaggedProto<T>(cx, AsTaggedProto(proto), parent, newKind);
}

template <typename T>
inline T*
NewObjectWithGivenProto(ExclusiveContext* cx, HandleObject proto, HandleObject parent,
                        gc::AllocKind allocKind, NewObjectKind newKind = GenericObject)
{
    JSObject* obj = NewObjectWithGivenTaggedProto(cx, &T::class_, AsTaggedProto(proto), parent,
                                                  allocKind, newKind);
    return obj ? &obj->as<T>() : nullptr;
}

/*
 * Make an object with the prototype set according to the specified prototype or class:
 *
 * if proto is non-null:
 *   use the specified proto
 * for a built-in class:
 *   use the memoized original value of the class constructor .prototype
 *   property object
 * else if available
 *   the current value of .prototype
 * else
 *   Object.prototype.
 *
 * The class prototype will be fetched from the parent's global. If global is
 * null, the context's active global will be used, and the resulting object's
 * parent will be that global.
 */
JSObject*
NewObjectWithClassProtoCommon(ExclusiveContext* cx, const Class* clasp, HandleObject proto,
                              HandleObject parent, gc::AllocKind allocKind,
                              NewObjectKind newKind);

inline JSObject*
NewObjectWithClassProto(ExclusiveContext* cx, const Class* clasp, HandleObject proto,
                        HandleObject parent, gc::AllocKind allocKind,
                        NewObjectKind newKind = GenericObject)
{
    return NewObjectWithClassProtoCommon(cx, clasp, proto, parent, allocKind, newKind);
}

inline JSObject*
NewObjectWithClassProto(ExclusiveContext* cx, const Class* clasp, HandleObject proto,
                        HandleObject parent, NewObjectKind newKind = GenericObject)
{
    gc::AllocKind allocKind = gc::GetGCObjectKind(clasp);
    return NewObjectWithClassProto(cx, clasp, proto, parent, allocKind, newKind);
}

template<typename T>
inline T *
NewObjectWithProto(ExclusiveContext *cx, HandleObject proto,
                   gc::AllocKind allocKind, NewObjectKind newKind = GenericObject)
{
    JSObject *obj = NewObjectWithClassProto(cx, &T::class_, proto, NullPtr(), allocKind, newKind);
    return obj ? &obj->as<T>() : nullptr;
}

template<typename T>
inline T *
NewObjectWithProto(ExclusiveContext *cx, HandleObject proto,
                   NewObjectKind newKind = GenericObject)
{
    JSObject* obj = NewObjectWithClassProto(cx, &T::class_, proto, NullPtr(), newKind);
    return obj ? &obj->as<T>() : nullptr;
}

/*
 * Create a native instance of the given class with parent and proto set
 * according to the context's active global.
 */
inline JSObject*
NewBuiltinClassInstance(ExclusiveContext* cx, const Class* clasp, gc::AllocKind allocKind,
                        NewObjectKind newKind = GenericObject)
{
    return NewObjectWithClassProto(cx, clasp, NullPtr(), NullPtr(), allocKind, newKind);
}

inline JSObject*
NewBuiltinClassInstance(ExclusiveContext* cx, const Class* clasp, NewObjectKind newKind = GenericObject)
{
    gc::AllocKind allocKind = gc::GetGCObjectKind(clasp);
    return NewBuiltinClassInstance(cx, clasp, allocKind, newKind);
}

template<typename T>
inline T*
NewBuiltinClassInstance(ExclusiveContext* cx, NewObjectKind newKind = GenericObject)
{
    JSObject* obj = NewBuiltinClassInstance(cx, &T::class_, newKind);
    return obj ? &obj->as<T>() : nullptr;
}

template<typename T>
inline T*
NewBuiltinClassInstance(ExclusiveContext* cx, gc::AllocKind allocKind, NewObjectKind newKind = GenericObject)
{
    JSObject* obj = NewBuiltinClassInstance(cx, &T::class_, allocKind, newKind);
    return obj ? &obj->as<T>() : nullptr;
}

// Used to optimize calls to (new Object())
bool
NewObjectScriptedCall(JSContext *cx, MutableHandleObject obj);

JSObject *
NewObjectWithGroupCommon(JSContext *cx, HandleObjectGroup group, HandleObject parent,
                         gc::AllocKind allocKind, NewObjectKind newKind);

template <typename T>
inline T *
NewObjectWithGroup(JSContext *cx, HandleObjectGroup group, HandleObject parent,
                   gc::AllocKind allocKind, NewObjectKind newKind = GenericObject)
{
    JSObject *obj = NewObjectWithGroupCommon(cx, group, parent, allocKind, newKind);
    return obj ? &obj->as<T>() : nullptr;
}

template <typename T>
inline T *
NewObjectWithGroup(JSContext *cx, HandleObjectGroup group, HandleObject parent,
                   NewObjectKind newKind = GenericObject)
{
    gc::AllocKind allocKind = gc::GetGCObjectKind(group->clasp());
    return NewObjectWithGroup<T>(cx, group, parent, allocKind, newKind);
}

/*
 * As for gc::GetGCObjectKind, where numSlots is a guess at the final size of
 * the object, zero if the final size is unknown. This should only be used for
 * objects that do not require any fixed slots.
 */
static inline gc::AllocKind
GuessObjectGCKind(size_t numSlots)
{
    if (numSlots)
        return gc::GetGCObjectKind(numSlots);
    return gc::AllocKind::OBJECT4;
}

static inline gc::AllocKind
GuessArrayGCKind(size_t numSlots)
{
    if (numSlots)
        return gc::GetGCArrayKind(numSlots);
    return gc::AllocKind::OBJECT8;
}

inline bool
ObjectClassIs(HandleObject obj, ESClassValue classValue, JSContext* cx)
{
    if (MOZ_UNLIKELY(obj->is<ProxyObject>()))
        return Proxy::objectClassIs(obj, classValue, cx);

    switch (classValue) {
      case ESClass_Object: return obj->is<PlainObject>();
      case ESClass_Array:
      case ESClass_IsArray:
        // There difference between those is only relevant for proxies.
        return obj->is<ArrayObject>();
      case ESClass_Number: return obj->is<NumberObject>();
      case ESClass_String: return obj->is<StringObject>();
      case ESClass_Boolean: return obj->is<BooleanObject>();
      case ESClass_RegExp: return obj->is<RegExpObject>();
      case ESClass_ArrayBuffer: return obj->is<ArrayBufferObject>();
      case ESClass_SharedArrayBuffer: return obj->is<SharedArrayBufferObject>();
      case ESClass_Date: return obj->is<DateObject>();
      case ESClass_Set: return obj->is<SetObject>();
      case ESClass_Map: return obj->is<MapObject>();
    }
    MOZ_CRASH("bad classValue");
}

inline bool
IsObjectWithClass(const Value& v, ESClassValue classValue, JSContext* cx)
{
    if (!v.isObject())
        return false;
    RootedObject obj(cx, &v.toObject());
    return ObjectClassIs(obj, classValue, cx);
}

// ES6 7.2.2
inline bool
IsArray(HandleObject obj, JSContext* cx)
{
    if (obj->is<ArrayObject>())
        return true;

    return ObjectClassIs(obj, ESClass_IsArray, cx);
}

inline bool
Unbox(JSContext* cx, HandleObject obj, MutableHandleValue vp)
{
    if (MOZ_UNLIKELY(obj->is<ProxyObject>()))
        return Proxy::boxedValue_unbox(cx, obj, vp);

    if (obj->is<BooleanObject>())
        vp.setBoolean(obj->as<BooleanObject>().unbox());
    else if (obj->is<NumberObject>())
        vp.setNumber(obj->as<NumberObject>().unbox());
    else if (obj->is<StringObject>())
        vp.setString(obj->as<StringObject>().unbox());
    else if (obj->is<DateObject>())
        vp.set(obj->as<DateObject>().UTCTime());
    else
        vp.setUndefined();

    return true;
}

static MOZ_ALWAYS_INLINE bool
NewObjectMetadata(ExclusiveContext* cxArg, JSObject** pmetadata)
{
    // The metadata callback is invoked before each created object, except when
    // analysis/compilation is active, to avoid recursion. It is also skipped
    // when we allocate objects during a bailout, to prevent stack iterations.
    MOZ_ASSERT(!*pmetadata);
    if (JSContext* cx = cxArg->maybeJSContext()) {
        if (MOZ_UNLIKELY((size_t)cx->compartment()->hasObjectMetadataCallback()) &&
            !cx->zone()->types.activeAnalysis &&
            !cx->zone()->usedByExclusiveThread)
        {
            // Use AutoEnterAnalysis to prohibit both any GC activity under the
            // callback, and any reentering of JS via Invoke() etc.
            AutoEnterAnalysis enter(cx);

            if (!cx->compartment()->callObjectMetadataCallback(cx, pmetadata))
                return false;
        }
    }
    return true;
}

static inline unsigned
ApplyAttributes(unsigned attrs, bool enumerable, bool writable, bool configurable)
{
    /*
     * Respect the fact that some callers may want to preserve existing attributes as much as
     * possible, or install defaults otherwise.
     */
    if (attrs & JSPROP_IGNORE_ENUMERATE) {
        attrs &= ~JSPROP_IGNORE_ENUMERATE;
        if (enumerable)
            attrs |= JSPROP_ENUMERATE;
        else
            attrs &= ~JSPROP_ENUMERATE;
    }
    if (attrs & JSPROP_IGNORE_READONLY) {
        attrs &= ~JSPROP_IGNORE_READONLY;
        // Only update the writability if it's relevant
        if (!(attrs & (JSPROP_GETTER | JSPROP_SETTER))) {
            if (!writable)
                attrs |= JSPROP_READONLY;
            else
                attrs &= ~JSPROP_READONLY;
        }
    }
    if (attrs & JSPROP_IGNORE_PERMANENT) {
        attrs &= ~JSPROP_IGNORE_PERMANENT;
        if (!configurable)
            attrs |= JSPROP_PERMANENT;
        else
            attrs &= ~JSPROP_PERMANENT;
    }
    return attrs;
}


extern js::NativeObject*
InitClass(JSContext* cx, js::HandleObject obj, HandleObject parent_proto,
          const Class* clasp, JSNative constructor, unsigned nargs,
          const JSPropertySpec* ps, const JSFunctionSpec* fs,
          const JSPropertySpec* static_ps, const JSFunctionSpec* static_fs,
          NativeObject** ctorp = nullptr,
          gc::AllocKind ctorKind = JSFunction::FinalizeKind);

} /* namespace js */

#endif /* jsobjinlines_h */
