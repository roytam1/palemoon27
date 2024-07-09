/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "js/Proxy.h"
#include "vm/ProxyObject.h"

#include "jscntxtinlines.h"
#include "jsobjinlines.h"

using namespace js;

bool
BaseProxyHandler::enter(JSContext* cx, HandleObject wrapper, HandleId id, Action act,
                        bool* bp) const
{
    *bp = true;
    return true;
}

bool
BaseProxyHandler::has(JSContext* cx, HandleObject proxy, HandleId id, bool* bp) const
{
    assertEnteredPolicy(cx, proxy, id, GET);
    Rooted<PropertyDescriptor> desc(cx);
    if (!getPropertyDescriptor(cx, proxy, id, &desc))
        return false;
    *bp = !!desc.object();
    return true;
}

bool
BaseProxyHandler::hasOwn(JSContext* cx, HandleObject proxy, HandleId id, bool* bp) const
{
    assertEnteredPolicy(cx, proxy, id, GET);
    Rooted<PropertyDescriptor> desc(cx);
    if (!getOwnPropertyDescriptor(cx, proxy, id, &desc))
        return false;
    *bp = !!desc.object();
    return true;
}

bool
BaseProxyHandler::get(JSContext* cx, HandleObject proxy, HandleObject receiver,
                      HandleId id, MutableHandleValue vp) const
{
    assertEnteredPolicy(cx, proxy, id, GET);

    Rooted<PropertyDescriptor> desc(cx);
    if (!getPropertyDescriptor(cx, proxy, id, &desc))
        return false;
    if (!desc.object()) {
        vp.setUndefined();
        return true;
    }
    MOZ_ASSERT(desc.getter() != JS_PropertyStub);
    if (!desc.getter()) {
        vp.set(desc.value());
        return true;
    }
    if (desc.hasGetterObject())
        return InvokeGetterOrSetter(cx, receiver, ObjectValue(*desc.getterObject()),
                                    0, nullptr, vp);
    if (!desc.isShared())
        vp.set(desc.value());
    else
        vp.setUndefined();

    return CallJSGetterOp(cx, desc.getter(), receiver, id, vp);
}

bool
BaseProxyHandler::set(JSContext* cx, HandleObject proxy, HandleObject receiver,
                      HandleId id, MutableHandleValue vp, ObjectOpResult &result) const
{
    assertEnteredPolicy(cx, proxy, id, SET);

    // This method is not covered by any spec, but we follow ES6 draft rev 28
    // (2014 Oct 14) 9.1.9 fairly closely, adapting it slightly for
    // SpiderMonkey's particular foibles.

    // Steps 2-3.  (Step 1 is a superfluous assertion.)
    Rooted<PropertyDescriptor> ownDesc(cx);
    if (!getOwnPropertyDescriptor(cx, proxy, id, &ownDesc))
        return false;

    // The rest is factored out into a separate function with a weird name.
    // This algorithm continues just below.
    return SetPropertyIgnoringNamedGetter(cx, proxy, id, vp, receiver, &ownDesc, result);
}

bool
js::SetPropertyIgnoringNamedGetter(JSContext *cx, HandleObject obj, HandleId id,
                                   MutableHandleValue vp, HandleObject receiver,
                                   MutableHandle<PropertyDescriptor> ownDesc,
                                   ObjectOpResult &result)
{
    // Step 4.
    if (!ownDesc.object()) {
        // The spec calls this variable "parent", but that word has weird
        // connotations in SpiderMonkey, so let's go with "proto".
        RootedObject proto(cx);
        if (!GetPrototype(cx, obj, &proto))
            return false;
        if (proto)
            return SetProperty(cx, proto, receiver, id, vp, result);

        // Change ownDesc to be a complete descriptor for a configurable,
        // writable, enumerable data property. Then fall through to step 5.
        ownDesc.clear();
        ownDesc.setAttributes(JSPROP_ENUMERATE);
    }

    // Step 5.
    if (ownDesc.isDataDescriptor()) {
        // Steps 5.a-b, adapted to our nonstandard implementation of ES6
        // [[Set]] return values.
        if (!ownDesc.writable())
            return result.fail(JSMSG_READ_ONLY);

        // Nonstandard SpiderMonkey special case: setter ops.
        SetterOp setter = ownDesc.setter();
        MOZ_ASSERT(setter != JS_StrictPropertyStub);
        if (setter && setter != JS_StrictPropertyStub)
            return CallSetter(cx, receiver, id, setter, ownDesc.attributes(), vp, result);

        // Steps 5.c-d. Adapt for SpiderMonkey by using HasOwnProperty instead
        // of the standard [[GetOwnProperty]].
        bool existingDescriptor;
        if (!HasOwnProperty(cx, receiver, id, &existingDescriptor))
            return false;

        // Steps 5.e-f.
        unsigned attrs =
            existingDescriptor
            ? JSPROP_IGNORE_ENUMERATE | JSPROP_IGNORE_READONLY | JSPROP_IGNORE_PERMANENT
            : JSPROP_ENUMERATE;

        // A very old nonstandard SpiderMonkey extension: default to the Class
        // getter and setter ops.
        const Class* clasp = receiver->getClass();
        MOZ_ASSERT(clasp->getProperty != JS_PropertyStub);
        MOZ_ASSERT(clasp->setProperty != JS_StrictPropertyStub);
        return DefineProperty(cx, receiver, id, vp, clasp->getProperty, clasp->setProperty,
                              attrs, result);
    }

    // Step 6.
    MOZ_ASSERT(ownDesc.isAccessorDescriptor());
    RootedObject setter(cx);
    if (ownDesc.hasSetterObject())
        setter = ownDesc.setterObject();
    if (!setter)
        return result.fail(JSMSG_GETTER_ONLY);
    RootedValue setterValue(cx, ObjectValue(*setter));
    if (!InvokeGetterOrSetter(cx, receiver, setterValue, 1, vp.address(), vp))
        return false;
    return result.succeed();
}

bool
BaseProxyHandler::getOwnEnumerablePropertyKeys(JSContext* cx, HandleObject proxy,
                                               AutoIdVector& props) const
{
    assertEnteredPolicy(cx, proxy, JSID_VOID, ENUMERATE);
    MOZ_ASSERT(props.length() == 0);

    if (!ownPropertyKeys(cx, proxy, props))
        return false;

    /* Select only the enumerable properties through in-place iteration. */
    RootedId id(cx);
    size_t i = 0;
    for (size_t j = 0, len = props.length(); j < len; j++) {
        MOZ_ASSERT(i <= j);
        id = props[j];
        if (JSID_IS_SYMBOL(id))
            continue;

        AutoWaivePolicy policy(cx, proxy, id, BaseProxyHandler::GET);
        Rooted<PropertyDescriptor> desc(cx);
        if (!getOwnPropertyDescriptor(cx, proxy, id, &desc))
            return false;
        if (desc.object() && desc.enumerable())
            props[i++].set(id);
    }

    MOZ_ASSERT(i <= props.length());
    props.resize(i);

    return true;
}

bool
BaseProxyHandler::enumerate(JSContext* cx, HandleObject proxy, MutableHandleObject objp) const
{
    assertEnteredPolicy(cx, proxy, JSID_VOID, ENUMERATE);

    // GetPropertyKeys will invoke getOwnEnumerablePropertyKeys along the proto
    // chain for us.
    AutoIdVector props(cx);
    if (!GetPropertyKeys(cx, proxy, 0, &props))
        return false;

    return EnumeratedIdVectorToIterator(cx, proxy, 0, props, objp);
}

bool
BaseProxyHandler::call(JSContext* cx, HandleObject proxy, const CallArgs& args) const
{
    MOZ_CRASH("callable proxies should implement call trap");
}

bool
BaseProxyHandler::construct(JSContext* cx, HandleObject proxy, const CallArgs& args) const
{
    MOZ_CRASH("callable proxies should implement construct trap");
}

const char*
BaseProxyHandler::className(JSContext* cx, HandleObject proxy) const
{
    return proxy->isCallable() ? "Function" : "Object";
}

JSString*
BaseProxyHandler::fun_toString(JSContext* cx, HandleObject proxy, unsigned indent) const
{
    if (proxy->isCallable())
        return JS_NewStringCopyZ(cx, "function () {\n    [native code]\n}");
    RootedValue v(cx, ObjectValue(*proxy));
    ReportIsNotFunction(cx, v);
    return nullptr;
}

bool
BaseProxyHandler::regexp_toShared(JSContext* cx, HandleObject proxy,
                                  RegExpGuard* g) const
{
    MOZ_CRASH("This should have been a wrapped regexp");
}

bool
BaseProxyHandler::boxedValue_unbox(JSContext* cx, HandleObject proxy, MutableHandleValue vp) const
{
    vp.setUndefined();
    return true;
}

bool
BaseProxyHandler::defaultValue(JSContext* cx, HandleObject proxy, JSType hint,
                               MutableHandleValue vp) const
{
    return OrdinaryToPrimitive(cx, proxy, hint, vp);
}

bool
BaseProxyHandler::nativeCall(JSContext* cx, IsAcceptableThis test, NativeImpl impl,
                             const CallArgs& args) const
{
    ReportIncompatible(cx, args);
    return false;
}

bool
BaseProxyHandler::hasInstance(JSContext* cx, HandleObject proxy, MutableHandleValue v,
                              bool* bp) const
{
    assertEnteredPolicy(cx, proxy, JSID_VOID, GET);
    RootedValue val(cx, ObjectValue(*proxy.get()));
    ReportValueError(cx, JSMSG_BAD_INSTANCEOF_RHS,
                     JSDVG_SEARCH_STACK, val, js::NullPtr());
    return false;
}

bool
BaseProxyHandler::objectClassIs(HandleObject proxy, ESClassValue classValue, JSContext* cx) const
{
    return false;
}

void
BaseProxyHandler::trace(JSTracer* trc, JSObject* proxy) const
{
}

void
BaseProxyHandler::finalize(JSFreeOp* fop, JSObject* proxy) const
{
}

void
BaseProxyHandler::objectMoved(JSObject* proxy, const JSObject* old) const
{
}

JSObject*
BaseProxyHandler::weakmapKeyDelegate(JSObject* proxy) const
{
    return nullptr;
}

bool
BaseProxyHandler::getPrototype(JSContext *cx, HandleObject proxy, MutableHandleObject protop) const
{
    MOZ_CRASH("Must override getPrototype with lazy prototype.");
}

bool
BaseProxyHandler::setPrototype(JSContext *cx, HandleObject proxy, HandleObject proto,
                               ObjectOpResult &result) const
{
    // Disallow sets of protos on proxies with lazy protos, but no hook.
    // This keeps us away from the footgun of having the first proto set opt
    // you out of having dynamic protos altogether.
    JS_ReportErrorNumber(cx, GetErrorMessage, nullptr, JSMSG_CANT_SET_PROTO_OF,
                         "incompatible Proxy");
    return false;
}

bool
BaseProxyHandler::setImmutablePrototype(JSContext *cx, HandleObject proxy, bool *succeeded) const
{
    *succeeded = false;
    return true;
}

bool
BaseProxyHandler::watch(JSContext* cx, HandleObject proxy, HandleId id, HandleObject callable) const
{
    JS_ReportErrorNumber(cx, GetErrorMessage, nullptr, JSMSG_CANT_WATCH,
                         proxy->getClass()->name);
    return false;
}

bool
BaseProxyHandler::unwatch(JSContext* cx, HandleObject proxy, HandleId id) const
{
    return true;
}

bool
BaseProxyHandler::getElements(JSContext* cx, HandleObject proxy, uint32_t begin, uint32_t end,
                              ElementAdder* adder) const
{
    assertEnteredPolicy(cx, proxy, JSID_VOID, GET);

    return js::GetElementsWithAdder(cx, proxy, proxy, begin, end, adder);
}

bool
BaseProxyHandler::isCallable(JSObject* obj) const
{
    return false;
}

bool
BaseProxyHandler::isConstructor(JSObject* obj) const
{
    return false;
}
