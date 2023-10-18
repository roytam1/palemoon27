"use strict";

Components.utils.import("resource://gre/modules/Schemas.jsm");
Components.utils.import("resource://gre/modules/BrowserUtils.jsm");

let json = [
  {namespace: "testing",

   properties: {
     PROP1: {value: 20},
     prop2: {type: "string"},
   },

   types: [
     {
       id: "type1",
       type: "string",
       "enum": ["value1", "value2", "value3"],
     },

     {
       id: "type2",
       type: "object",
       properties: {
         prop1: {type: "integer"},
         prop2: {type: "array", items: {"$ref": "type1"}},
       },
     }
   ],

   functions: [
     {
       name: "foo",
       type: "function",
       parameters: [
         {name: "arg1", type: "integer", optional: true},
         {name: "arg2", type: "boolean", optional: true},
       ],
     },

     {
       name: "bar",
       type: "function",
       parameters: [
         {name: "arg1", type: "integer", optional: true},
         {name: "arg2", type: "boolean"},
       ],
     },

     {
       name: "baz",
       type: "function",
       parameters: [
         {name: "arg1", type: "object", properties: {
           prop1: {type: "string"},
           prop2: {type: "integer", optional: true},
           prop3: {type: "integer", unsupported: true},
         }},
       ],
     },

     {
       name: "qux",
       type: "function",
       parameters: [
         {name: "arg1", "$ref": "type1"},
       ],
     },

     {
       name: "quack",
       type: "function",
       parameters: [
         {name: "arg1", "$ref": "type2"},
       ],
     },

     {
       name: "quora",
       type: "function",
       parameters: [
         {name: "arg1", type: "function"},
       ],
     },

     {
       name: "quileute",
       type: "function",
       parameters: [
         {name: "arg1", type: "integer", optional: true},
         {name: "arg2", type: "integer"},
       ],
     },

     {
       name: "queets",
       type: "function",
       unsupported: true,
       parameters: [],
     },

     {
       name: "quintuplets",
       type: "function",
       parameters: [
         {name: "obj", type: "object", properties: [], additionalProperties: {type: "integer"}},
       ],
     },

     {
       name: "quasar",
       type: "function",
       parameters: [
         {name: "abc", type: "object", properties: {
           func: {type: "function", parameters: [
             {name: "x", type: "integer"}
           ]}
         }}
       ],
     },

     {
       name: "quosimodo",
       type: "function",
       parameters: [
         {name: "xyz", type: "object"},
       ],
     },
   ],

   events: [
     {
       name: "onFoo",
       type: "function",
     },

     {
       name: "onBar",
       type: "function",
       extraParameters: [{
         name: "filter",
         type: "integer",
       }],
     }
   ],
  }
];

let tallied = null;

function tally(kind, ns, name, args) {
  tallied = [kind, ns, name, args];
}

function verify(...args) {
  do_check_eq(JSON.stringify(tallied), JSON.stringify(args));
  tallied = null;
}

let wrapper = {
  callFunction(ns, name, args) {
    tally("call", ns, name, args);
  },

  addListener(ns, name, listener, args) {
    tally("addListener", ns, name, [listener, args]);
  },
  removeListener(ns, name, listener) {
    tally("removeListener", ns, name, [listener]);
  },
  hasListener(ns, name, listener) {
    tally("hasListener", ns, name, [listener]);
  },
};

add_task(function* () {
  let url = "data:," + JSON.stringify(json);
  let uri = BrowserUtils.makeURI(url);
  yield Schemas.load(uri);

  let root = {};
  Schemas.inject(root, wrapper);

  do_check_eq(root.testing.PROP1, 20, "simple value property");
  do_check_eq(root.testing.type1.VALUE1, "value1", "enum type");
  do_check_eq(root.testing.type1.VALUE2, "value2", "enum type");

  root.testing.foo(11, true);
  verify("call", "testing", "foo", [11, true]);

  root.testing.foo(true);
  verify("call", "testing", "foo", [null, true]);

  root.testing.foo(null, true);
  verify("call", "testing", "foo", [null, true]);

  root.testing.foo(undefined, true);
  verify("call", "testing", "foo", [null, true]);

  root.testing.foo(11);
  verify("call", "testing", "foo", [11, null]);

  Assert.throws(() => root.testing.bar(11),
                /Incorrect argument types/,
                "should throw without required arg");

  Assert.throws(() => root.testing.bar(11, true, 10),
                /Incorrect argument types/,
                "should throw with too many arguments");

  root.testing.bar(true);
  verify("call", "testing", "bar", [null, true]);

  root.testing.baz({ prop1: "hello", prop2: 22 });
  verify("call", "testing", "baz", [{ prop1: "hello", prop2: 22 }]);

  root.testing.baz({ prop1: "hello" });
  verify("call", "testing", "baz", [{ prop1: "hello", prop2: null }]);

  root.testing.baz({ prop1: "hello", prop2: null });
  verify("call", "testing", "baz", [{ prop1: "hello", prop2: null }]);

  Assert.throws(() => root.testing.baz({ prop2: 12 }),
                /Property "prop1" is required/,
                "should throw without required property");

  Assert.throws(() => root.testing.baz({ prop1: "hi", prop3: 12 }),
                /Property "prop3" is unsupported by Firefox/,
                "should throw with unsupported property");

  Assert.throws(() => root.testing.baz({ prop1: "hi", prop4: 12 }),
                /Unexpected property "prop4"/,
                "should throw with unexpected property");

  Assert.throws(() => root.testing.baz({ prop1: 12 }),
                /Expected string instead of 12/,
                "should throw with wrong type");

  root.testing.qux("value2");
  verify("call", "testing", "qux", ["value2"]);

  Assert.throws(() => root.testing.qux("value4"),
                /Invalid enumeration value "value4"/,
                "should throw for invalid enum value");

  root.testing.quack({prop1: 12, prop2: ["value1", "value3"]});
  verify("call", "testing", "quack", [{prop1: 12, prop2: ["value1", "value3"]}]);

  Assert.throws(() => root.testing.quack({prop1: 12, prop2: ["value1", "value3", "value4"]}),
                /Invalid enumeration value "value4"/,
                "should throw for invalid array type");

  function f() {}
  root.testing.quora(f);
  do_check_eq(JSON.stringify(tallied.slice(0, -1)), JSON.stringify(["call", "testing", "quora"]));
  do_check_eq(tallied[3][0], f);
  tallied = null;

  let g = () => 0;
  root.testing.quora(g);
  do_check_eq(JSON.stringify(tallied.slice(0, -1)), JSON.stringify(["call", "testing", "quora"]));
  do_check_eq(tallied[3][0], g);
  tallied = null;

  root.testing.quileute(10);
  verify("call", "testing", "quileute", [null, 10]);

  Assert.throws(() => root.testing.queets(),
                /queets is not a function/,
                "should throw for unsupported functions");

  root.testing.quintuplets({a: 10, b: 20, c: 30});
  verify("call", "testing", "quintuplets", [{a: 10, b: 20, c: 30}]);

  Assert.throws(() => root.testing.quintuplets({a: 10, b: 20, c: 30, d: "hi"}),
                /Expected integer instead of "hi"/,
                "should throw for wrong additionalProperties type");

  root.testing.quasar({func: f});
  do_check_eq(JSON.stringify(tallied.slice(0, -1)), JSON.stringify(["call", "testing", "quasar"]));
  do_check_eq(tallied[3][0].func, f);
  tallied = null;

  root.testing.quosimodo({a: 10, b: 20, c: 30});
  verify("call", "testing", "quosimodo", [{a: 10, b: 20, c: 30}]);

  Assert.throws(() => root.testing.quosimodo(10),
                /Incorrect argument types/,
                "should throw for wrong type");

  root.testing.onFoo.addListener(f);
  do_check_eq(JSON.stringify(tallied.slice(0, -1)), JSON.stringify(["addListener", "testing", "onFoo"]));
  do_check_eq(tallied[3][0], f);
  do_check_eq(JSON.stringify(tallied[3][1]), JSON.stringify([]));
  tallied = null;

  root.testing.onFoo.removeListener(f);
  do_check_eq(JSON.stringify(tallied.slice(0, -1)), JSON.stringify(["removeListener", "testing", "onFoo"]));
  do_check_eq(tallied[3][0], f);
  tallied = null;

  root.testing.onFoo.hasListener(f);
  do_check_eq(JSON.stringify(tallied.slice(0, -1)), JSON.stringify(["hasListener", "testing", "onFoo"]));
  do_check_eq(tallied[3][0], f);
  tallied = null;

  Assert.throws(() => root.testing.onFoo.addListener(10),
                /Invalid listener/,
                "addListener with non-function should throw");

  root.testing.onBar.addListener(f, 10);
  do_check_eq(JSON.stringify(tallied.slice(0, -1)), JSON.stringify(["addListener", "testing", "onBar"]));
  do_check_eq(tallied[3][0], f);
  do_check_eq(JSON.stringify(tallied[3][1]), JSON.stringify([10]));
  tallied = null;

  Assert.throws(() => root.testing.onBar.addListener(f, "hi"),
                /Incorrect argument types/,
                "addListener with wrong extra parameter should throw");
});
