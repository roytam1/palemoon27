// Do the things we write in classes actually appear as they are supposed to?

var test= `

var methodCalled = false;
var getterCalled = false;
var setterCalled = false;
var constructorCalled = false;
var staticMethodCalled = false;
var staticGetterCalled = false;
var staticSetterCalled = false;
class a {
    constructor() { constructorCalled = true; }
    __proto__() { methodCalled = true }
    get getter() { getterCalled = true; }
    set setter(x) { setterCalled = true; }
    static staticMethod() { staticMethodCalled = true; }
    static get staticGetter() { staticGetterCalled = true; }
    static set staticSetter(x) { staticSetterCalled = true; }
    *[Symbol.iterator]() { yield "cow"; yield "pig"; }
}
var aConstDesc = Object.getOwnPropertyDescriptor(a.prototype, \"constructor\");
assertEq(aConstDesc.writable, true);
assertEq(aConstDesc.configurable, true);
assertEq(aConstDesc.enumerable, false);
aConstDesc.value();
assertEq(constructorCalled, true);

// __proto__ is just an identifier for classes. No prototype changes are made.
assertEq(Object.getPrototypeOf(a.prototype), Object.prototype);
var aMethDesc = Object.getOwnPropertyDescriptor(a.prototype, \"__proto__\");
assertEq(aMethDesc.writable, true);
assertEq(aMethDesc.configurable, true);
assertEq(aMethDesc.enumerable, true);
aMethDesc.value();
assertEq(methodCalled, true);

var aGetDesc = Object.getOwnPropertyDescriptor(a.prototype, \"getter\");
assertEq(aGetDesc.configurable, true);
assertEq(aGetDesc.enumerable, true);
aGetDesc.get();
assertEq(getterCalled, true);

var aSetDesc = Object.getOwnPropertyDescriptor(a.prototype, \"setter\");
assertEq(aSetDesc.configurable, true);
assertEq(aSetDesc.enumerable, true);
aSetDesc.set();
assertEq(setterCalled, true);
assertDeepEq(aSetDesc, Object.getOwnPropertyDescriptor(a.prototype, \"setter\"));

assertEq(Object.getOwnPropertyDescriptor(new a(), \"staticMethod\"), undefined);
var aStaticMethDesc = Object.getOwnPropertyDescriptor(a, \"staticMethod\");
assertEq(aStaticMethDesc.configurable, true);
assertEq(aStaticMethDesc.enumerable, true);
assertEq(aStaticMethDesc.writable, true);
aStaticMethDesc.value();
assertEq(staticMethodCalled, true);

assertEq(Object.getOwnPropertyDescriptor(new a(), \"staticGetter\"), undefined);
var aStaticGetDesc = Object.getOwnPropertyDescriptor(a, \"staticGetter\");
assertEq(aStaticGetDesc.configurable, true);
assertEq(aStaticGetDesc.enumerable, true);
aStaticGetDesc.get();
assertEq(staticGetterCalled, true);

assertEq(Object.getOwnPropertyDescriptor(new a(), \"staticSetter\"), undefined);
var aStaticSetDesc = Object.getOwnPropertyDescriptor(a, \"staticSetter\");
assertEq(aStaticSetDesc.configurable, true);
assertEq(aStaticSetDesc.enumerable, true);
aStaticSetDesc.set();
assertEq(staticSetterCalled, true);

assertEq([...new a()].join(), "cow,pig");
`;

if (classesEnabled())
    eval(test);

if (typeof reportCompare === "function")
    reportCompare(0, 0, "OK");
