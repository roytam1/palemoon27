// |reftest| skip-if(!xulRuntime.shell)
/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/licenses/publicdomain/
 */

(function runtest(main) {
    try {
        main();
    } catch (exc) {
        print(exc.stack);
        throw exc;
    }
})(function main() {

var { Pattern, MatchError } = Match;

var _ = Pattern.ANY;

function program(elts) Pattern({ type: "Program", body: elts })
function exprStmt(expr) Pattern({ type: "ExpressionStatement", expression: expr })
function throwStmt(expr) Pattern({ type: "ThrowStatement", argument: expr })
function returnStmt(expr) Pattern({ type: "ReturnStatement", argument: expr })
function yieldExpr(expr) Pattern({ type: "YieldExpression", argument: expr })
function lit(val) Pattern({ type: "Literal", value: val })
function comp(name) Pattern({ type: "ComputedName", name: name })
function spread(val) Pattern({ type: "SpreadExpression", expression: val})
var thisExpr = Pattern({ type: "ThisExpression" });
function funDecl(id, params, body, defaults=[], rest=null) Pattern(
    { type: "FunctionDeclaration",
      id: id,
      params: params,
      defaults: defaults,
      body: body,
      rest: rest,
      generator: false })
function genFunDecl(id, params, body) Pattern({ type: "FunctionDeclaration",
                                                id: id,
                                                params: params,
                                                defaults: [],
                                                body: body,
                                                generator: true })
function varDecl(decls) Pattern({ type: "VariableDeclaration", declarations: decls, kind: "var" })
function letDecl(decls) Pattern({ type: "VariableDeclaration", declarations: decls, kind: "let" })
function constDecl(decls) Pattern({ type: "VariableDeclaration", declarations: decls, kind: "const" })
function ident(name) Pattern({ type: "Identifier", name: name })
function dotExpr(obj, id) Pattern({ type: "MemberExpression", computed: false, object: obj, property: id })
function memExpr(obj, id) Pattern({ type: "MemberExpression", computed: true, object: obj, property: id })
function forStmt(init, test, update, body) Pattern({ type: "ForStatement", init: init, test: test, update: update, body: body })
function forOfStmt(lhs, rhs, body) Pattern({ type: "ForOfStatement", left: lhs, right: rhs, body: body })
function forInStmt(lhs, rhs, body) Pattern({ type: "ForInStatement", left: lhs, right: rhs, body: body, each: false })
function forEachInStmt(lhs, rhs, body) Pattern({ type: "ForInStatement", left: lhs, right: rhs, body: body, each: true })
function breakStmt(lab) Pattern({ type: "BreakStatement", label: lab })
function continueStmt(lab) Pattern({ type: "ContinueStatement", label: lab })
function blockStmt(body) Pattern({ type: "BlockStatement", body: body })
function literal(val) Pattern({ type: "Literal",  value: val })
var emptyStmt = Pattern({ type: "EmptyStatement" })
function ifStmt(test, cons, alt) Pattern({ type: "IfStatement", test: test, alternate: alt, consequent: cons })
function labStmt(lab, stmt) Pattern({ type: "LabeledStatement", label: lab, body: stmt })
function withStmt(obj, stmt) Pattern({ type: "WithStatement", object: obj, body: stmt })
function whileStmt(test, stmt) Pattern({ type: "WhileStatement", test: test, body: stmt })
function doStmt(stmt, test) Pattern({ type: "DoWhileStatement", test: test, body: stmt })
function switchStmt(disc, cases) Pattern({ type: "SwitchStatement", discriminant: disc, cases: cases })
function caseClause(test, stmts) Pattern({ type: "SwitchCase", test: test, consequent: stmts })
function defaultClause(stmts) Pattern({ type: "SwitchCase", test: null, consequent: stmts })
function catchClause(id, guard, body) Pattern({ type: "CatchClause", param: id, guard: guard, body: body })
function tryStmt(body, guarded, unguarded, fin) Pattern({ type: "TryStatement", block: body, guardedHandlers: guarded, handler: unguarded, finalizer: fin })
function letStmt(head, body) Pattern({ type: "LetStatement", head: head, body: body })

function classStmt(id, heritage, body) Pattern({ type: "ClassStatement",
                                                 name: id,
                                                 heritage: heritage,
                                                 body: body})
function classMethod(id, body, kind, static) Pattern({ type: "ClassMethod",
                                                       name: id,
                                                       body: body,
                                                       kind: kind,
                                                       static: static })


function funExpr(id, args, body, gen) Pattern({ type: "FunctionExpression",
                                                id: id,
                                                params: args,
                                                body: body,
                                                generator: false })
function genFunExpr(id, args, body) Pattern({ type: "FunctionExpression",
                                              id: id,
                                              params: args,
                                              body: body,
                                              generator: true })
function arrowExpr(args, body) Pattern({ type: "ArrowExpression",
                                         params: args,
                                         body: body })

function unExpr(op, arg) Pattern({ type: "UnaryExpression", operator: op, argument: arg })
function binExpr(op, left, right) Pattern({ type: "BinaryExpression", operator: op, left: left, right: right })
function aExpr(op, left, right) Pattern({ type: "AssignmentExpression", operator: op, left: left, right: right })
function updExpr(op, arg, prefix) Pattern({ type: "UpdateExpression", operator: op, argument: arg, prefix: prefix })
function logExpr(op, left, right) Pattern({ type: "LogicalExpression", operator: op, left: left, right: right })

function condExpr(test, cons, alt) Pattern({ type: "ConditionalExpression", test: test, consequent: cons, alternate: alt })
function seqExpr(exprs) Pattern({ type: "SequenceExpression", expressions: exprs })
function newExpr(callee, args) Pattern({ type: "NewExpression", callee: callee, arguments: args })
function callExpr(callee, args) Pattern({ type: "CallExpression", callee: callee, arguments: args })
function arrExpr(elts) Pattern({ type: "ArrayExpression", elements: elts })
function objExpr(elts) Pattern({ type: "ObjectExpression", properties: elts })
function computedName(elts) Pattern({ type: "ComputedName", name: elts })
function templateLit(elts) Pattern({ type: "TemplateLiteral", elements: elts })
function taggedTemplate(tagPart, templatePart) Pattern({ type: "TaggedTemplate", callee: tagPart,
                arguments : templatePart })
function template(raw, cooked, ...args) Pattern([{ type: "CallSiteObject", raw: raw, cooked:
cooked}, ...args])
function compExpr(body, blocks, filter, style) {
  if (style == "legacy" || !filter)
    return Pattern({ type: "ComprehensionExpression", body, blocks, filter, style });
  else
    return Pattern({ type: "ComprehensionExpression", body, blocks: blocks.concat(compIf(filter)), filter: null, style });
}
function genExpr(body, blocks, filter, style) {
  if (style == "legacy" || !filter)
    return Pattern({ type: "GeneratorExpression", body, blocks, filter, style });
  else
    return Pattern({ type: "GeneratorExpression", body, blocks: blocks.concat(compIf(filter)), filter: null, style });
}
function graphExpr(idx, body) Pattern({ type: "GraphExpression", index: idx, expression: body })
function letExpr(head, body) Pattern({ type: "LetExpression", head: head, body: body })
function idxExpr(idx) Pattern({ type: "GraphIndexExpression", index: idx })

function compBlock(left, right) Pattern({ type: "ComprehensionBlock", left: left, right: right, each: false, of: false })
function compEachBlock(left, right) Pattern({ type: "ComprehensionBlock", left: left, right: right, each: true, of: false })
function compOfBlock(left, right) Pattern({ type: "ComprehensionBlock", left: left, right: right, each: false, of: true })
function compIf(test) Pattern({ type: "ComprehensionIf", test: test })

function arrPatt(elts) Pattern({ type: "ArrayPattern", elements: elts })
function objPatt(elts) Pattern({ type: "ObjectPattern", properties: elts })

function assignElem(target, defaultExpr = null, targetIdent = typeof target == 'string' ? ident(target) : target) defaultExpr ? aExpr('=', targetIdent, defaultExpr) : targetIdent
function assignProp(property, target, defaultExpr = null, shorthand = !target, targetProp = target || ident(property)) Pattern({
    type: "Property", key: ident(property), shorthand,
    value: defaultExpr ? aExpr('=', targetProp, defaultExpr) : targetProp })

function localSrc(src) "(function(){ " + src + " })"
function localPatt(patt) program([exprStmt(funExpr(null, [], blockStmt([patt])))])
function blockSrc(src) "(function(){ { " + src + " } })"
function blockPatt(patt) program([exprStmt(funExpr(null, [], blockStmt([blockStmt([patt])])))])

function assertBlockStmt(src, patt) {
    blockPatt(patt).assert(Reflect.parse(blockSrc(src)));
}

function assertBlockExpr(src, patt) {
    assertBlockStmt(src, exprStmt(patt));
}

function assertBlockDecl(src, patt, builder) {
    blockPatt(patt).assert(Reflect.parse(blockSrc(src), {builder: builder}));
}

function assertLocalStmt(src, patt) {
    localPatt(patt).assert(Reflect.parse(localSrc(src)));
}

function assertLocalExpr(src, patt) {
    assertLocalStmt(src, exprStmt(patt));
}

function assertLocalDecl(src, patt) {
    localPatt(patt).assert(Reflect.parse(localSrc(src)));
}

function assertGlobalStmt(src, patt, builder) {
    program([patt]).assert(Reflect.parse(src, {builder: builder}));
}

function assertStringExpr(src, patt) {
    program([exprStmt(patt)]).assert(Reflect.parse(src));
}

function assertGlobalExpr(src, patt, builder) {
    program([exprStmt(patt)]).assert(Reflect.parse(src, {builder: builder}));
    //assertStmt(src, exprStmt(patt));
}

function assertGlobalDecl(src, patt) {
    program([patt]).assert(Reflect.parse(src));
}

function assertProg(src, patt) {
    program(patt).assert(Reflect.parse(src));
}

function assertStmt(src, patt) {
    assertLocalStmt(src, patt);
    assertGlobalStmt(src, patt);
    assertBlockStmt(src, patt);
}

function assertExpr(src, patt) {
    assertLocalExpr(src, patt);
    assertGlobalExpr(src, patt);
    assertBlockExpr(src, patt);
}

function assertDecl(src, patt) {
    assertLocalDecl(src, patt);
    assertGlobalDecl(src, patt);
    assertBlockDecl(src, patt);
}

function assertError(src, errorType) {
    try {
        Reflect.parse(src);
    } catch (expected if expected instanceof errorType) {
        return;
    }
    throw new Error("expected " + errorType.name + " for " + uneval(src));
}


// general tests

// NB: These are useful but for now jit-test doesn't do I/O reliably.

//program(_).assert(Reflect.parse(snarf('data/flapjax.txt')));
//program(_).assert(Reflect.parse(snarf('data/jquery-1.4.2.txt')));
//program(_).assert(Reflect.parse(snarf('data/prototype.js')));
//program(_).assert(Reflect.parse(snarf('data/dojo.js.uncompressed.js')));
//program(_).assert(Reflect.parse(snarf('data/mootools-1.2.4-core-nc.js')));


// declarations

assertDecl("var x = 1, y = 2, z = 3",
           varDecl([{ id: ident("x"), init: lit(1) },
                    { id: ident("y"), init: lit(2) },
                    { id: ident("z"), init: lit(3) }]));
assertDecl("var x, y, z",
           varDecl([{ id: ident("x"), init: null },
                    { id: ident("y"), init: null },
                    { id: ident("z"), init: null }]));
assertDecl("function foo() { }",
           funDecl(ident("foo"), [], blockStmt([])));
assertDecl("function foo() { return 42 }",
           funDecl(ident("foo"), [], blockStmt([returnStmt(lit(42))])));

assertDecl("function foo(...rest) { }",
           funDecl(ident("foo"), [], blockStmt([]), [], ident("rest")));

assertDecl("function foo(a=4) { }", funDecl(ident("foo"), [ident("a")], blockStmt([]), [lit(4)]));
assertDecl("function foo(a, b=4) { }", funDecl(ident("foo"), [ident("a"), ident("b")], blockStmt([]), [lit(4)]));
assertDecl("function foo(a, b=4, ...rest) { }",
           funDecl(ident("foo"), [ident("a"), ident("b")], blockStmt([]), [lit(4)], ident("rest")));
assertDecl("function foo(a=(function () {})) { function a() {} }",
           funDecl(ident("foo"), [ident("a")], blockStmt([funDecl(ident("a"), [], blockStmt([]))]),
                   [funExpr(null, [], blockStmt([]))]));


// Bug 591437: rebound args have their defs turned into uses
assertDecl("function f(a) { function a() { } }",
           funDecl(ident("f"), [ident("a")], blockStmt([funDecl(ident("a"), [], blockStmt([]))])));
assertDecl("function f(a,b,c) { function b() { } }",
           funDecl(ident("f"), [ident("a"),ident("b"),ident("c")], blockStmt([funDecl(ident("b"), [], blockStmt([]))])));
assertDecl("function f(a,[x,y]) { function a() { } }",
           funDecl(ident("f"),
                   [ident("a"), arrPatt([assignElem("x"), assignElem("y")])],
                   blockStmt([funDecl(ident("a"), [], blockStmt([]))])));

// Bug 632027: array holes should reflect as null
assertExpr("[,]=[,]", aExpr("=", arrPatt([null]), arrExpr([null])));

// Bug 591450: this test currently crashes because of a bug in jsparse
// assertDecl("function f(a,[x,y],b,[w,z],c) { function b() { } }",
//            funDecl(ident("f"),
//                    [ident("a"), arrPatt([ident("x"), ident("y")]), ident("b"), arrPatt([ident("w"), ident("z")]), ident("c")],
//                    blockStmt([funDecl(ident("b"), [], blockStmt([]))])));


// expressions

assertExpr("true", lit(true));
assertExpr("false", lit(false));
assertExpr("42", lit(42));
assertExpr("(/asdf/)", lit(/asdf/));
assertExpr("this", thisExpr);
assertExpr("foo", ident("foo"));
assertExpr("foo.bar", dotExpr(ident("foo"), ident("bar")));
assertExpr("foo[bar]", memExpr(ident("foo"), ident("bar")));
assertExpr("foo['bar']", memExpr(ident("foo"), lit("bar")));
assertExpr("foo[42]", memExpr(ident("foo"), lit(42)));
assertExpr("(function(){})", funExpr(null, [], blockStmt([])));
assertExpr("(function f() {})", funExpr(ident("f"), [], blockStmt([])));
assertExpr("(function f(x,y,z) {})", funExpr(ident("f"), [ident("x"),ident("y"),ident("z")], blockStmt([])));
assertExpr("a => a", arrowExpr([ident("a")], ident("a")));
assertExpr("(a) => a", arrowExpr([ident("a")], ident("a")));
assertExpr("a => b => a", arrowExpr([ident("a")], arrowExpr([ident("b")], ident("a"))));
assertExpr("a => {}", arrowExpr([ident("a")], blockStmt([])));
assertExpr("a => ({})", arrowExpr([ident("a")], objExpr([])));
assertExpr("(a, b, c) => {}", arrowExpr([ident("a"), ident("b"), ident("c")], blockStmt([])));
assertExpr("([a, b]) => {}", arrowExpr([arrPatt([assignElem("a"), assignElem("b")])], blockStmt([])));
assertExpr("(++x)", updExpr("++", ident("x"), true));
assertExpr("(x++)", updExpr("++", ident("x"), false));
assertExpr("(+x)", unExpr("+", ident("x")));
assertExpr("(-x)", unExpr("-", ident("x")));
assertExpr("(!x)", unExpr("!", ident("x")));
assertExpr("(~x)", unExpr("~", ident("x")));
assertExpr("(delete x)", unExpr("delete", ident("x")));
assertExpr("(typeof x)", unExpr("typeof", ident("x")));
assertExpr("(void x)", unExpr("void", ident("x")));
assertExpr("(x == y)", binExpr("==", ident("x"), ident("y")));
assertExpr("(x != y)", binExpr("!=", ident("x"), ident("y")));
assertExpr("(x === y)", binExpr("===", ident("x"), ident("y")));
assertExpr("(x !== y)", binExpr("!==", ident("x"), ident("y")));
assertExpr("(x < y)", binExpr("<", ident("x"), ident("y")));
assertExpr("(x <= y)", binExpr("<=", ident("x"), ident("y")));
assertExpr("(x > y)", binExpr(">", ident("x"), ident("y")));
assertExpr("(x >= y)", binExpr(">=", ident("x"), ident("y")));
assertExpr("(x << y)", binExpr("<<", ident("x"), ident("y")));
assertExpr("(x >> y)", binExpr(">>", ident("x"), ident("y")));
assertExpr("(x >>> y)", binExpr(">>>", ident("x"), ident("y")));
assertExpr("(x + y)", binExpr("+", ident("x"), ident("y")));
assertExpr("(w + x + y + z)", binExpr("+", binExpr("+", binExpr("+", ident("w"), ident("x")), ident("y")), ident("z")));
assertExpr("(x - y)", binExpr("-", ident("x"), ident("y")));
assertExpr("(w - x - y - z)", binExpr("-", binExpr("-", binExpr("-", ident("w"), ident("x")), ident("y")), ident("z")));
assertExpr("(x * y)", binExpr("*", ident("x"), ident("y")));
assertExpr("(x / y)", binExpr("/", ident("x"), ident("y")));
assertExpr("(x % y)", binExpr("%", ident("x"), ident("y")));
assertExpr("(x | y)", binExpr("|", ident("x"), ident("y")));
assertExpr("(x ^ y)", binExpr("^", ident("x"), ident("y")));
assertExpr("(x & y)", binExpr("&", ident("x"), ident("y")));
assertExpr("(x in y)", binExpr("in", ident("x"), ident("y")));
assertExpr("(x instanceof y)", binExpr("instanceof", ident("x"), ident("y")));
assertExpr("(x = y)", aExpr("=", ident("x"), ident("y")));
assertExpr("(x += y)", aExpr("+=", ident("x"), ident("y")));
assertExpr("(x -= y)", aExpr("-=", ident("x"), ident("y")));
assertExpr("(x *= y)", aExpr("*=", ident("x"), ident("y")));
assertExpr("(x /= y)", aExpr("/=", ident("x"), ident("y")));
assertExpr("(x %= y)", aExpr("%=", ident("x"), ident("y")));
assertExpr("(x <<= y)", aExpr("<<=", ident("x"), ident("y")));
assertExpr("(x >>= y)", aExpr(">>=", ident("x"), ident("y")));
assertExpr("(x >>>= y)", aExpr(">>>=", ident("x"), ident("y")));
assertExpr("(x |= y)", aExpr("|=", ident("x"), ident("y")));
assertExpr("(x ^= y)", aExpr("^=", ident("x"), ident("y")));
assertExpr("(x &= y)", aExpr("&=", ident("x"), ident("y")));
assertExpr("(x || y)", logExpr("||", ident("x"), ident("y")));
assertExpr("(x && y)", logExpr("&&", ident("x"), ident("y")));
assertExpr("(w || x || y || z)", logExpr("||", logExpr("||", logExpr("||", ident("w"), ident("x")), ident("y")), ident("z")))
assertExpr("(x ? y : z)", condExpr(ident("x"), ident("y"), ident("z")));
assertExpr("(x,y)", seqExpr([ident("x"),ident("y")]))
assertExpr("(x,y,z)", seqExpr([ident("x"),ident("y"),ident("z")]))
assertExpr("(a,b,c,d,e,f,g)", seqExpr([ident("a"),ident("b"),ident("c"),ident("d"),ident("e"),ident("f"),ident("g")]));
assertExpr("(new Object)", newExpr(ident("Object"), []));
assertExpr("(new Object())", newExpr(ident("Object"), []));
assertExpr("(new Object(42))", newExpr(ident("Object"), [lit(42)]));
assertExpr("(new Object(1,2,3))", newExpr(ident("Object"), [lit(1),lit(2),lit(3)]));
assertExpr("(String())", callExpr(ident("String"), []));
assertExpr("(String(42))", callExpr(ident("String"), [lit(42)]));
assertExpr("(String(1,2,3))", callExpr(ident("String"), [lit(1),lit(2),lit(3)]));
assertExpr("[]", arrExpr([]));
assertExpr("[1]", arrExpr([lit(1)]));
assertExpr("[1,2]", arrExpr([lit(1),lit(2)]));
assertExpr("[1,2,3]", arrExpr([lit(1),lit(2),lit(3)]));
assertExpr("[1,,2,3]", arrExpr([lit(1),null,lit(2),lit(3)]));
assertExpr("[1,,,2,3]", arrExpr([lit(1),null,null,lit(2),lit(3)]));
assertExpr("[1,,,2,,3]", arrExpr([lit(1),null,null,lit(2),null,lit(3)]));
assertExpr("[1,,,2,,,3]", arrExpr([lit(1),null,null,lit(2),null,null,lit(3)]));
assertExpr("[,1,2,3]", arrExpr([null,lit(1),lit(2),lit(3)]));
assertExpr("[,,1,2,3]", arrExpr([null,null,lit(1),lit(2),lit(3)]));
assertExpr("[,,,1,2,3]", arrExpr([null,null,null,lit(1),lit(2),lit(3)]));
assertExpr("[,,,1,2,3,]", arrExpr([null,null,null,lit(1),lit(2),lit(3)]));
assertExpr("[,,,1,2,3,,]", arrExpr([null,null,null,lit(1),lit(2),lit(3),null]));
assertExpr("[,,,1,2,3,,,]", arrExpr([null,null,null,lit(1),lit(2),lit(3),null,null]));
assertExpr("[,,,,,]", arrExpr([null,null,null,null,null]));
assertExpr("[1, ...a, 2]", arrExpr([lit(1), spread(ident("a")), lit(2)]));
assertExpr("[,, ...a,, ...b, 42]", arrExpr([null,null, spread(ident("a")),, spread(ident("b")), lit(42)]));
assertExpr("[1,(2,3)]", arrExpr([lit(1),seqExpr([lit(2),lit(3)])]));
assertExpr("[,(2,3)]", arrExpr([null,seqExpr([lit(2),lit(3)])]));
assertExpr("({})", objExpr([]));
assertExpr("({x:1})", objExpr([{ key: ident("x"), value: lit(1) }]));
assertExpr("({x:x, y})", objExpr([{ key: ident("x"), value: ident("x"), shorthand: false },
                                  { key: ident("y"), value: ident("y"), shorthand: true }]));
assertExpr("({x:1, y:2})", objExpr([{ key: ident("x"), value: lit(1) },
                                    { key: ident("y"), value: lit(2) } ]));
assertExpr("({x:1, y:2, z:3})", objExpr([{ key: ident("x"), value: lit(1) },
                                         { key: ident("y"), value: lit(2) },
                                         { key: ident("z"), value: lit(3) } ]));
assertExpr("({x:1, 'y':2, z:3})", objExpr([{ key: ident("x"), value: lit(1) },
                                           { key: lit("y"), value: lit(2) },
                                           { key: ident("z"), value: lit(3) } ]));
assertExpr("({'x':1, 'y':2, z:3})", objExpr([{ key: lit("x"), value: lit(1) },
                                             { key: lit("y"), value: lit(2) },
                                             { key: ident("z"), value: lit(3) } ]));
assertExpr("({'x':1, 'y':2, 3:3})", objExpr([{ key: lit("x"), value: lit(1) },
                                             { key: lit("y"), value: lit(2) },
                                             { key: lit(3), value: lit(3) } ]));
assertExpr("({__proto__:x})", objExpr([{ type: "PrototypeMutation", value: ident("x") }]));
assertExpr("({'__proto__':x})", objExpr([{ type: "PrototypeMutation", value: ident("x") }]));
assertExpr("({['__proto__']:x})", objExpr([{ type: "Property", key: comp(lit("__proto__")), value: ident("x") }]));
assertExpr("({['__proto__']:q, __proto__() {}, __proto__: null })",
           objExpr([{ type: "Property", key: comp(lit("__proto__")), value: ident("q") },
                    { type: "Property", key: ident("__proto__"), method: true },
                    { type: "PrototypeMutation", value: lit(null) }]));

// Bug 571617: eliminate constant-folding
assertExpr("2 + 3", binExpr("+", lit(2), lit(3)));

// Bug 632026: constant-folding
assertExpr("typeof(0?0:a)", unExpr("typeof", condExpr(lit(0), lit(0), ident("a"))));

// Bug 632029: constant-folding
assertExpr("[x for each (x in y) if (false)]", compExpr(ident("x"), [compEachBlock(ident("x"), ident("y"))], lit(false), "legacy"));

// Bug 632056: constant-folding
program([exprStmt(ident("f")),
         ifStmt(lit(1),
                funDecl(ident("f"), [], blockStmt([])),
                null)]).assert(Reflect.parse("f; if (1) function f(){}"));

// Bug 924688: computed property names
assertExpr('a= {[field1]: "a", [field2=1]: "b"}',
          aExpr("=", ident("a"),
                objExpr([{ key: computedName(ident("field1")), value: lit("a")},
                         { key: computedName(aExpr("=", ident("field2"), lit(1))),
                           value: lit("b")}])));

assertExpr('a= {["field1"]: "a", field2 : "b"}',
          aExpr("=", ident("a"),
                objExpr([{ key: computedName(lit("field1")), value: lit("a") },
                         { key: ident("field2"), value: lit("b") }])));

assertExpr('a= {[1]: 1, 2 : 2}',
          aExpr("=", ident("a"),
                objExpr([{ key: computedName(lit(1)), value: lit(1) },
                         { key: lit(2), value: lit(2) }])));

// Bug 924688: computed property names - location information
var node = Reflect.parse("a = {[field1]: 5}");
Pattern({ body: [ { expression: { right: { properties: [ {key: { loc:
    { start: { line: 1, column: 5 }, end: { line: 1, column: 13 }}}}]}}}]}).match(node);

// Bug 1048384 - Getter/setter syntax with computed names
assertExpr("b = { get [meth]() { } }", aExpr("=", ident("b"),
              objExpr([{ key: computedName(ident("meth")), value: funExpr(null, [], blockStmt([])),
                method: false, kind: "get"}])));
assertExpr("b = { set [meth](a) { } }", aExpr("=", ident("b"),
              objExpr([{ key: computedName(ident("meth")), value: funExpr(null, [ident("a")],
                blockStmt([])), method: false, kind: "set"}])));

// Bug 1073809 - Getter/setter syntax with generators
assertExpr("({*get() { }})", objExpr([{ type: "Property", key: ident("get"), method: true,
                                        value: genFunExpr(ident("get"), [], blockStmt([]))}]));
assertExpr("({*set() { }})", objExpr([{ type: "Property", key: ident("set"), method: true,
                                        value: genFunExpr(ident("set"), [], blockStmt([]))}]));
assertError("({*get foo() { }})", SyntaxError);
assertError("({*set foo() { }})", SyntaxError);

assertError("({ *get 1() {} })", SyntaxError);
assertError("({ *get \"\"() {} })", SyntaxError);
assertError("({ *get foo() {} })", SyntaxError);
assertError("({ *get []() {} })", SyntaxError);
assertError("({ *get [1]() {} })", SyntaxError);

assertError("({ *set 1() {} })", SyntaxError);
assertError("({ *set \"\"() {} })", SyntaxError);
assertError("({ *set foo() {} })", SyntaxError);
assertError("({ *set []() {} })", SyntaxError);
assertError("({ *set [1]() {} })", SyntaxError);
// statements

assertStmt("throw 42", throwStmt(lit(42)));
assertStmt("for (;;) break", forStmt(null, null, null, breakStmt(null)));
assertStmt("for (x; y; z) break", forStmt(ident("x"), ident("y"), ident("z"), breakStmt(null)));
assertStmt("for (var x; y; z) break", forStmt(varDecl([{ id: ident("x"), init: null }]), ident("y"), ident("z")));
assertStmt("for (var x = 42; y; z) break", forStmt(varDecl([{ id: ident("x"), init: lit(42) }]), ident("y"), ident("z")));
assertStmt("for (x; ; z) break", forStmt(ident("x"), null, ident("z"), breakStmt(null)));
assertStmt("for (var x; ; z) break", forStmt(varDecl([{ id: ident("x"), init: null }]), null, ident("z")));
assertStmt("for (var x = 42; ; z) break", forStmt(varDecl([{ id: ident("x"), init: lit(42) }]), null, ident("z")));
assertStmt("for (x; y; ) break", forStmt(ident("x"), ident("y"), null, breakStmt(null)));
assertStmt("for (var x; y; ) break", forStmt(varDecl([{ id: ident("x"), init: null }]), ident("y"), null, breakStmt(null)));
assertStmt("for (var x = 42; y; ) break", forStmt(varDecl([{ id: ident("x"), init: lit(42) }]), ident("y"), null, breakStmt(null)));
assertStmt("for (var x in y) break", forInStmt(varDecl([{ id: ident("x"), init: null }]), ident("y"), breakStmt(null)));
assertStmt("for (x in y) break", forInStmt(ident("x"), ident("y"), breakStmt(null)));
assertStmt("{ }", blockStmt([]));
assertStmt("{ throw 1; throw 2; throw 3; }", blockStmt([ throwStmt(lit(1)), throwStmt(lit(2)), throwStmt(lit(3))]));
assertStmt(";", emptyStmt);
assertStmt("if (foo) throw 42;", ifStmt(ident("foo"), throwStmt(lit(42)), null));
assertStmt("if (foo) throw 42; else true;", ifStmt(ident("foo"), throwStmt(lit(42)), exprStmt(lit(true))));
assertStmt("if (foo) { throw 1; throw 2; throw 3; }",
           ifStmt(ident("foo"),
                  blockStmt([throwStmt(lit(1)), throwStmt(lit(2)), throwStmt(lit(3))]),
                  null));
assertStmt("if (foo) { throw 1; throw 2; throw 3; } else true;",
           ifStmt(ident("foo"),
                  blockStmt([throwStmt(lit(1)), throwStmt(lit(2)), throwStmt(lit(3))]),
                  exprStmt(lit(true))));

// template strings
assertStringExpr("`hey there`", literal("hey there"));
assertStringExpr("`hey\nthere`", literal("hey\nthere"));
assertExpr("`hey${\"there\"}`", templateLit([lit("hey"), lit("there"), lit("")]));
assertExpr("`hey${\"there\"}mine`", templateLit([lit("hey"), lit("there"), lit("mine")]));
assertExpr("`hey${a == 5}mine`", templateLit([lit("hey"), binExpr("==", ident("a"), lit(5)), lit("mine")]));
assertExpr("`hey${`there${\"how\"}`}mine`", templateLit([lit("hey"),
           templateLit([lit("there"), lit("how"), lit("")]), lit("mine")]));
assertExpr("func`hey`", taggedTemplate(ident("func"), template(["hey"], ["hey"])));
assertExpr("func`hey${\"4\"}there`", taggedTemplate(ident("func"),
           template(["hey", "there"], ["hey", "there"], lit("4"))));
assertExpr("func`hey${\"4\"}there${5}`", taggedTemplate(ident("func"),
           template(["hey", "there", ""], ["hey", "there", ""],
                  lit("4"), lit(5))));
assertExpr("func`hey\r\n`", taggedTemplate(ident("func"), template(["hey\n"], ["hey\n"])));
assertExpr("func`hey${4}``${5}there``mine`",
           taggedTemplate(taggedTemplate(taggedTemplate(
               ident("func"), template(["hey", ""], ["hey", ""], lit(4))),
               template(["", "there"], ["", "there"], lit(5))),
               template(["mine"], ["mine"])));

// multi-line template string - line numbers
var node = Reflect.parse("`\n\n   ${2}\n\n\n`");
Pattern({loc:{start:{line:1, column:0}, end:{line:6, column:1}, source:null}, type:"Program",
body:[{loc:{start:{line:1, column:0}, end:{line:6, column:1}, source:null},
type:"ExpressionStatement", expression:{loc:{start:{line:1, column:0}, end:{line:6, column:1},
source:null}, type:"TemplateLiteral", elements:[{loc:{start:{line:1, column:0}, end:{line:3,
column:5}, source:null}, type:"Literal", value:"\n\n   "}, {loc:{start:{line:3, column:5},
end:{line:3, column:6}, source:null}, type:"Literal", value:2}, {loc:{start:{line:3, column:6},
end:{line:6, column:1}, source:null}, type:"Literal", value:"\n\n\n"}]}}]}).match(node);


assertStringExpr("\"hey there\"", literal("hey there"));

assertStmt("foo: for(;;) break foo;", labStmt(ident("foo"), forStmt(null, null, null, breakStmt(ident("foo")))));
assertStmt("foo: for(;;) continue foo;", labStmt(ident("foo"), forStmt(null, null, null, continueStmt(ident("foo")))));
assertStmt("with (obj) { }", withStmt(ident("obj"), blockStmt([])));
assertStmt("with (obj) { obj; }", withStmt(ident("obj"), blockStmt([exprStmt(ident("obj"))])));
assertStmt("while (foo) { }", whileStmt(ident("foo"), blockStmt([])));
assertStmt("while (foo) { foo; }", whileStmt(ident("foo"), blockStmt([exprStmt(ident("foo"))])));
assertStmt("do { } while (foo);", doStmt(blockStmt([]), ident("foo")));
assertStmt("do { foo; } while (foo)", doStmt(blockStmt([exprStmt(ident("foo"))]), ident("foo")));
assertStmt("switch (foo) { case 1: 1; break; case 2: 2; break; default: 3; }",
           switchStmt(ident("foo"),
                      [ caseClause(lit(1), [ exprStmt(lit(1)), breakStmt(null) ]),
                        caseClause(lit(2), [ exprStmt(lit(2)), breakStmt(null) ]),
                        defaultClause([ exprStmt(lit(3)) ]) ]));
assertStmt("switch (foo) { case 1: 1; break; case 2: 2; break; default: 3; case 42: 42; }",
           switchStmt(ident("foo"),
                      [ caseClause(lit(1), [ exprStmt(lit(1)), breakStmt(null) ]),
                        caseClause(lit(2), [ exprStmt(lit(2)), breakStmt(null) ]),
                        defaultClause([ exprStmt(lit(3)) ]),
                        caseClause(lit(42), [ exprStmt(lit(42)) ]) ]));
assertStmt("try { } catch (e) { }",
           tryStmt(blockStmt([]),
                   [],
		   catchClause(ident("e"), null, blockStmt([])),
                   null));
assertStmt("try { } catch (e) { } finally { }",
           tryStmt(blockStmt([]),
                   [],
		   catchClause(ident("e"), null, blockStmt([])),
                   blockStmt([])));
assertStmt("try { } finally { }",
           tryStmt(blockStmt([]),
                   [],
		   null,
                   blockStmt([])));
assertStmt("try { } catch (e if foo) { } catch (e if bar) { } finally { }",
           tryStmt(blockStmt([]),
                   [ catchClause(ident("e"), ident("foo"), blockStmt([])),
                     catchClause(ident("e"), ident("bar"), blockStmt([])) ],
		   null,
                   blockStmt([])));
assertStmt("try { } catch (e if foo) { } catch (e if bar) { } catch (e) { } finally { }",
           tryStmt(blockStmt([]),
                   [ catchClause(ident("e"), ident("foo"), blockStmt([])),
                     catchClause(ident("e"), ident("bar"), blockStmt([])) ],
                   catchClause(ident("e"), null, blockStmt([])),
                   blockStmt([])));


// Bug 924672: Method definitions
assertExpr("b = { a() { } }", aExpr("=", ident("b"),
              objExpr([{ key: ident("a"), value: funExpr(ident("a"), [], blockStmt([])), method:
              true}])));

assertExpr("b = { *a() { } }", aExpr("=", ident("b"),
              objExpr([{ key: ident("a"), value: genFunExpr(ident("a"), [], blockStmt([])), method:
              true}])));

// Bug 632028: yield outside of a function should throw
(function() {
    var threw = false;
    try {
        Reflect.parse("yield 0");
    } catch (expected) {
        threw = true;
    }
    assertEq(threw, true);
})();

// redeclarations (TOK_NAME nodes with lexdef)

assertStmt("function f() { function g() { } function g() { } }",
           funDecl(ident("f"), [], blockStmt([emptyStmt,
                                              funDecl(ident("g"), [], blockStmt([]))])));

// Fails due to parser quirks (bug 638577)
//assertStmt("function f() { function g() { } function g() { return 42 } }",
//           funDecl(ident("f"), [], blockStmt([funDecl(ident("g"), [], blockStmt([])),
//                                              funDecl(ident("g"), [], blockStmt([returnStmt(lit(42))]))])));

assertStmt("function f() { var x = 42; var x = 43; }",
           funDecl(ident("f"), [], blockStmt([varDecl([{ id: ident("x"), init: lit(42) }]),
                                              varDecl([{ id: ident("x"), init: lit(43) }])])));


assertDecl("var {x:y} = foo;", varDecl([{ id: objPatt([assignProp("x", ident("y"))]),
                                          init: ident("foo") }]));
assertDecl("var {x} = foo;", varDecl([{ id: objPatt([assignProp("x")]),
                                        init: ident("foo") }]));

// Bug 632030: redeclarations between var and funargs, var and function
assertStmt("function g(x) { var x }",
           funDecl(ident("g"), [ident("x")], blockStmt([varDecl[{ id: ident("x"), init: null }]])));
assertProg("f.p = 1; var f; f.p; function f(){}",
           [exprStmt(aExpr("=", dotExpr(ident("f"), ident("p")), lit(1))),
            varDecl([{ id: ident("f"), init: null }]),
            exprStmt(dotExpr(ident("f"), ident("p"))),
            funDecl(ident("f"), [], blockStmt([]))]);

// global let is var
assertGlobalDecl("let {x:y} = foo;", varDecl([{ id: objPatt([assignProp("x", ident("y"))]),
                                                init: ident("foo") }]));
// function-global let is let
assertLocalDecl("let {x:y} = foo;", letDecl([{ id: objPatt([assignProp("x", ident("y"))]),
                                               init: ident("foo") }]));
// block-local let is let
assertBlockDecl("let {x:y} = foo;", letDecl([{ id: objPatt([assignProp("x", ident("y"))]),
                                               init: ident("foo") }]));

assertDecl("const {x:y} = foo;", constDecl([{ id: objPatt([assignProp("x", ident("y"))]),
                                              init: ident("foo") }]));


// various combinations of identifiers and destructuring patterns:
function makePatternCombinations(id, destr)
    [
      [ id(1)                                            ],
      [ id(1),    id(2)                                  ],
      [ id(1),    id(2),    id(3)                        ],
      [ id(1),    id(2),    id(3),    id(4)              ],
      [ id(1),    id(2),    id(3),    id(4),    id(5)    ],

      [ destr(1)                                         ],
      [ destr(1), destr(2)                               ],
      [ destr(1), destr(2), destr(3)                     ],
      [ destr(1), destr(2), destr(3), destr(4)           ],
      [ destr(1), destr(2), destr(3), destr(4), destr(5) ],

      [ destr(1), id(2)                                  ],

      [ destr(1), id(2),    id(3)                        ],
      [ destr(1), id(2),    id(3),    id(4)              ],
      [ destr(1), id(2),    id(3),    id(4),    id(5)    ],
      [ destr(1), id(2),    id(3),    id(4),    destr(5) ],
      [ destr(1), id(2),    id(3),    destr(4)           ],
      [ destr(1), id(2),    id(3),    destr(4), id(5)    ],
      [ destr(1), id(2),    id(3),    destr(4), destr(5) ],

      [ destr(1), id(2),    destr(3)                     ],
      [ destr(1), id(2),    destr(3), id(4)              ],
      [ destr(1), id(2),    destr(3), id(4),    id(5)    ],
      [ destr(1), id(2),    destr(3), id(4),    destr(5) ],
      [ destr(1), id(2),    destr(3), destr(4)           ],
      [ destr(1), id(2),    destr(3), destr(4), id(5)    ],
      [ destr(1), id(2),    destr(3), destr(4), destr(5) ],

      [ id(1),    destr(2)                               ],

      [ id(1),    destr(2), id(3)                        ],
      [ id(1),    destr(2), id(3),    id(4)              ],
      [ id(1),    destr(2), id(3),    id(4),    id(5)    ],
      [ id(1),    destr(2), id(3),    id(4),    destr(5) ],
      [ id(1),    destr(2), id(3),    destr(4)           ],
      [ id(1),    destr(2), id(3),    destr(4), id(5)    ],
      [ id(1),    destr(2), id(3),    destr(4), destr(5) ],

      [ id(1),    destr(2), destr(3)                     ],
      [ id(1),    destr(2), destr(3), id(4)              ],
      [ id(1),    destr(2), destr(3), id(4),    id(5)    ],
      [ id(1),    destr(2), destr(3), id(4),    destr(5) ],
      [ id(1),    destr(2), destr(3), destr(4)           ],
      [ id(1),    destr(2), destr(3), destr(4), id(5)    ],
      [ id(1),    destr(2), destr(3), destr(4), destr(5) ]
    ]

// destructuring function parameters

function testParamPatternCombinations(makePattSrc, makePattPatt) {
    var pattSrcs = makePatternCombinations(function(n) ("x" + n), makePattSrc);
    var pattPatts = makePatternCombinations(function(n) (ident("x" + n)), makePattPatt);

    for (var i = 0; i < pattSrcs.length; i++) {
        function makeSrc(body) ("(function(" + pattSrcs[i].join(",") + ") " + body + ")")
        function makePatt(body) (funExpr(null, pattPatts[i], body))

        // no upvars, block body
        assertExpr(makeSrc("{ }", makePatt(blockStmt([]))));
        // upvars, block body
        assertExpr(makeSrc("{ return [x1,x2,x3,x4,x5]; }"),
                   makePatt(blockStmt([returnStmt(arrExpr([ident("x1"), ident("x2"), ident("x3"), ident("x4"), ident("x5")]))])));
        // no upvars, expression body
        assertExpr(makeSrc("(0)"), makePatt(lit(0)));
        // upvars, expression body
        assertExpr(makeSrc("[x1,x2,x3,x4,x5]"),
                   makePatt(arrExpr([ident("x1"), ident("x2"), ident("x3"), ident("x4"), ident("x5")])));
    }
}

testParamPatternCombinations(function(n) ("{a" + n + ":x" + n + "," + "b" + n + ":y" + n + "," + "c" + n + ":z" + n + "}"),
                             function(n) (objPatt([assignProp("a" + n, ident("x" + n)),
                                                   assignProp("b" + n, ident("y" + n)),
                                                   assignProp("c" + n, ident("z" + n))])));

testParamPatternCombinations(function(n) ("{a" + n + ":x" + n + " = 10," + "b" + n + ":y" + n + " = 10," + "c" + n + ":z" + n + " = 10}"),
                             function(n) (objPatt([assignProp("a" + n, ident("x" + n), lit(10)),
                                                   assignProp("b" + n, ident("y" + n), lit(10)),
                                                   assignProp("c" + n, ident("z" + n), lit(10))])));

testParamPatternCombinations(function(n) ("[x" + n + "," + "y" + n + "," + "z" + n + "]"),
                             function(n) (arrPatt([assignElem("x" + n), assignElem("y" + n), assignElem("z" + n)])));

testParamPatternCombinations(function(n) ("[a" + n + ", ..." + "b" + n + "]"),
                             function(n) (arrPatt([assignElem("a" + n), spread(ident("b" + n))])));

testParamPatternCombinations(function(n) ("[a" + n + ", " + "b" + n + " = 10]"),
                             function(n) (arrPatt([assignElem("a" + n), assignElem("b" + n, lit(10))])));

// destructuring variable declarations

function testVarPatternCombinations(makePattSrc, makePattPatt) {
    var pattSrcs = makePatternCombinations(function(n) ("x" + n), makePattSrc);
    var pattPatts = makePatternCombinations(function(n) ({ id: ident("x" + n), init: null }), makePattPatt);
    // It's illegal to have uninitialized const declarations, so we need a
    // separate set of patterns and sources.
    var constSrcs = makePatternCombinations(function(n) ("x" + n + " = undefined"), makePattSrc);
    var constPatts = makePatternCombinations(function(n) ({ id: ident("x" + n), init: ident("undefined") }), makePattPatt);

    for (var i = 0; i < pattSrcs.length; i++) {
        // variable declarations in blocks
        assertDecl("var " + pattSrcs[i].join(",") + ";", varDecl(pattPatts[i]));

        assertGlobalDecl("let " + pattSrcs[i].join(",") + ";", varDecl(pattPatts[i]));
        assertLocalDecl("let " + pattSrcs[i].join(",") + ";", letDecl(pattPatts[i]));
        assertBlockDecl("let " + pattSrcs[i].join(",") + ";", letDecl(pattPatts[i]));

        assertDecl("const " + constSrcs[i].join(",") + ";", constDecl(constPatts[i]));

        // variable declarations in for-loop heads
        assertStmt("for (var " + pattSrcs[i].join(",") + "; foo; bar);",
                   forStmt(varDecl(pattPatts[i]), ident("foo"), ident("bar"), emptyStmt));
        assertStmt("for (let " + pattSrcs[i].join(",") + "; foo; bar);",
                   letStmt(pattPatts[i], forStmt(null, ident("foo"), ident("bar"), emptyStmt)));
        assertStmt("for (const " + constSrcs[i].join(",") + "; foo; bar);",
                   letStmt(constPatts[i], forStmt(null, ident("foo"), ident("bar"), emptyStmt)));
    }
}

testVarPatternCombinations(function (n) ("{a" + n + ":x" + n + "," + "b" + n + ":y" + n + "," + "c" + n + ":z" + n + "} = 0"),
                           function (n) ({ id: objPatt([assignProp("a" + n, ident("x" + n)),
                                                        assignProp("b" + n, ident("y" + n)),
                                                        assignProp("c" + n, ident("z" + n))]),
                                           init: lit(0) }));

testVarPatternCombinations(function (n) ("{a" + n + ":x" + n + " = 10," + "b" + n + ":y" + n + " = 10," + "c" + n + ":z" + n + " = 10} = 0"),
                           function (n) ({ id: objPatt([assignProp("a" + n, ident("x" + n), lit(10)),
                                                        assignProp("b" + n, ident("y" + n), lit(10)),
                                                        assignProp("c" + n, ident("z" + n), lit(10))]),
                                           init: lit(0) }));

testVarPatternCombinations(function(n) ("[x" + n + "," + "y" + n + "," + "z" + n + "] = 0"),
                           function(n) ({ id: arrPatt([assignElem("x" + n), assignElem("y" + n), assignElem("z" + n)]),
                                          init: lit(0) }));

testVarPatternCombinations(function(n) ("[a" + n + ", ..." + "b" + n + "] = 0"),
                           function(n) ({ id: arrPatt([assignElem("a" + n), spread(ident("b" + n))]),
                                          init: lit(0) }));

testVarPatternCombinations(function(n) ("[a" + n + ", " + "b" + n + " = 10] = 0"),
                           function(n) ({ id: arrPatt([assignElem("a" + n), assignElem("b" + n, lit(10))]),
                                          init: lit(0) }));
// destructuring assignment

function testAssignmentCombinations(makePattSrc, makePattPatt) {
    var pattSrcs = makePatternCombinations(function(n) ("x" + n + " = 0"), makePattSrc);
    var pattPatts = makePatternCombinations(function(n) (aExpr("=", ident("x" + n), lit(0))), makePattPatt);

    for (var i = 0; i < pattSrcs.length; i++) {
        var src = pattSrcs[i].join(",");
        var patt = pattPatts[i].length === 1 ? pattPatts[i][0] : seqExpr(pattPatts[i]);

        // assignment expression statement
        assertExpr("(" + src + ")", patt);

        // for-loop head assignment
        assertStmt("for (" + src + "; foo; bar);",
                   forStmt(patt, ident("foo"), ident("bar"), emptyStmt));
    }
}

testAssignmentCombinations(function (n) ("{a" + n + ":x" + n + "," + "b" + n + ":y" + n + "," + "c" + n + ":z" + n + "} = 0"),
                           function (n) (aExpr("=",
                                               objPatt([assignProp("a" + n, ident("x" + n)),
                                                        assignProp("b" + n, ident("y" + n)),
                                                        assignProp("c" + n, ident("z" + n))]),
                                               lit(0))));


// destructuring in for-in and for-each-in loop heads

var axbycz = objPatt([assignProp("a", ident("x")),
                      assignProp("b", ident("y")),
                      assignProp("c", ident("z"))]);
var xyz = arrPatt([assignElem("x"), assignElem("y"), assignElem("z")]);

assertStmt("for (var {a:x,b:y,c:z} in foo);", forInStmt(varDecl([{ id: axbycz, init: null }]), ident("foo"), emptyStmt));
assertStmt("for (let {a:x,b:y,c:z} in foo);", forInStmt(letDecl([{ id: axbycz, init: null }]), ident("foo"), emptyStmt));
assertStmt("for ({a:x,b:y,c:z} in foo);", forInStmt(axbycz, ident("foo"), emptyStmt));
assertStmt("for (var [x,y,z] in foo);", forInStmt(varDecl([{ id: xyz, init: null }]), ident("foo"), emptyStmt));
assertStmt("for (let [x,y,z] in foo);", forInStmt(letDecl([{ id: xyz, init: null }]), ident("foo"), emptyStmt));
assertStmt("for ([x,y,z] in foo);", forInStmt(xyz, ident("foo"), emptyStmt));
assertStmt("for (var {a:x,b:y,c:z} of foo);", forOfStmt(varDecl([{ id: axbycz, init: null }]), ident("foo"), emptyStmt));
assertStmt("for (let {a:x,b:y,c:z} of foo);", forOfStmt(letDecl([{ id: axbycz, init: null }]), ident("foo"), emptyStmt));
assertStmt("for ({a:x,b:y,c:z} of foo);", forOfStmt(axbycz, ident("foo"), emptyStmt));
assertStmt("for (var [x,y,z] of foo);", forOfStmt(varDecl([{ id: xyz, init: null }]), ident("foo"), emptyStmt));
assertStmt("for (let [x,y,z] of foo);", forOfStmt(letDecl([{ id: xyz, init: null }]), ident("foo"), emptyStmt));
assertStmt("for ([x,y,z] of foo);", forOfStmt(xyz, ident("foo"), emptyStmt));
assertStmt("for each (var {a:x,b:y,c:z} in foo);", forEachInStmt(varDecl([{ id: axbycz, init: null }]), ident("foo"), emptyStmt));
assertStmt("for each (let {a:x,b:y,c:z} in foo);", forEachInStmt(letDecl([{ id: axbycz, init: null }]), ident("foo"), emptyStmt));
assertStmt("for each ({a:x,b:y,c:z} in foo);", forEachInStmt(axbycz, ident("foo"), emptyStmt));
assertStmt("for each (var [x,y,z] in foo);", forEachInStmt(varDecl([{ id: xyz, init: null }]), ident("foo"), emptyStmt));
assertStmt("for each (let [x,y,z] in foo);", forEachInStmt(letDecl([{ id: xyz, init: null }]), ident("foo"), emptyStmt));
assertStmt("for each ([x,y,z] in foo);", forEachInStmt(xyz, ident("foo"), emptyStmt));
assertError("for (const x in foo);", SyntaxError);
assertError("for (const {a:x,b:y,c:z} in foo);", SyntaxError);
assertError("for (const [x,y,z] in foo);", SyntaxError);
assertError("for (const x of foo);", SyntaxError);
assertError("for (const {a:x,b:y,c:z} of foo);", SyntaxError);
assertError("for (const [x,y,z] of foo);", SyntaxError);
assertError("for each (const x in foo);", SyntaxError);
assertError("for each (const {a:x,b:y,c:z} in foo);", SyntaxError);
assertError("for each (const [x,y,z] in foo);", SyntaxError);

// destructuring in for-in and for-each-in loop heads with initializers

assertStmt("for (var {a:x,b:y,c:z} = 22 in foo);", forInStmt(varDecl([{ id: axbycz, init: lit(22) }]), ident("foo"), emptyStmt));
assertStmt("for (var [x,y,z] = 22 in foo);", forInStmt(varDecl([{ id: xyz, init: lit(22) }]), ident("foo"), emptyStmt));
assertStmt("for each (var {a:x,b:y,c:z} = 22 in foo);", forEachInStmt(varDecl([{ id: axbycz, init: lit(22) }]), ident("foo"), emptyStmt));
assertStmt("for each (var [x,y,z] = 22 in foo);", forEachInStmt(varDecl([{ id: xyz, init: lit(22) }]), ident("foo"), emptyStmt));
assertError("for (x = 22 in foo);", SyntaxError);
assertError("for ({a:x,b:y,c:z} = 22 in foo);", SyntaxError);
assertError("for ([x,y,z] = 22 in foo);", SyntaxError);
assertError("for (const x = 22 in foo);", SyntaxError);
assertError("for (const {a:x,b:y,c:z} = 22 in foo);", SyntaxError);
assertError("for (const [x,y,z] = 22 in foo);", SyntaxError);
assertError("for each (const x = 22 in foo);", SyntaxError);
assertError("for each (const {a:x,b:y,c:z} = 22 in foo);", SyntaxError);
assertError("for each (const [x,y,z] = 22 in foo);", SyntaxError);

// expression closures

assertDecl("function inc(x) (x + 1)", funDecl(ident("inc"), [ident("x")], binExpr("+", ident("x"), lit(1))));
assertExpr("(function(x) (x+1))", funExpr(null, [ident("x")], binExpr("+"), ident("x"), lit(1)));

// generators

assertDecl("function gen(x) { yield }", genFunDecl(ident("gen"), [ident("x")], blockStmt([exprStmt(yieldExpr(null))])));
assertExpr("(function(x) { yield })", genFunExpr(null, [ident("x")], blockStmt([exprStmt(yieldExpr(null))])));
assertDecl("function gen(x) { yield 42 }", genFunDecl(ident("gen"), [ident("x")], blockStmt([exprStmt(yieldExpr(lit(42)))])));
assertExpr("(function(x) { yield 42 })", genFunExpr(null, [ident("x")], blockStmt([exprStmt(yieldExpr(lit(42)))])));

// getters and setters

assertExpr("({ get x() { return 42 } })",
           objExpr([ { key: ident("x"),
                       value: funExpr(null, [], blockStmt([returnStmt(lit(42))])),
                       kind: "get" } ]));
assertExpr("({ set x(v) { return 42 } })",
           objExpr([ { key: ident("x"),
                       value: funExpr(null, [ident("v")], blockStmt([returnStmt(lit(42))])),
                       kind: "set" } ]));

// comprehensions

assertExpr("[ x         for (x in foo)]",
           compExpr(ident("x"), [compBlock(ident("x"), ident("foo"))], null, "legacy"));
assertExpr("[ [x,y]     for (x in foo) for (y in bar)]",
           compExpr(arrExpr([ident("x"), ident("y")]), [compBlock(ident("x"), ident("foo")), compBlock(ident("y"), ident("bar"))], null, "legacy"));
assertExpr("[ [x,y,z] for (x in foo) for (y in bar) for (z in baz)]",
           compExpr(arrExpr([ident("x"), ident("y"), ident("z")]),
                    [compBlock(ident("x"), ident("foo")), compBlock(ident("y"), ident("bar")), compBlock(ident("z"), ident("baz"))],
                    null,
                    "legacy"));

assertExpr("[ x         for (x in foo) if (p)]",
           compExpr(ident("x"), [compBlock(ident("x"), ident("foo"))], ident("p"), "legacy"));
assertExpr("[ [x,y]     for (x in foo) for (y in bar) if (p)]",
           compExpr(arrExpr([ident("x"), ident("y")]), [compBlock(ident("x"), ident("foo")), compBlock(ident("y"), ident("bar"))], ident("p"), "legacy"));
assertExpr("[ [x,y,z] for (x in foo) for (y in bar) for (z in baz) if (p) ]",
           compExpr(arrExpr([ident("x"), ident("y"), ident("z")]),
                    [compBlock(ident("x"), ident("foo")), compBlock(ident("y"), ident("bar")), compBlock(ident("z"), ident("baz"))],
                    ident("p"),
                    "legacy"));

assertExpr("[ x         for each (x in foo)]",
           compExpr(ident("x"), [compEachBlock(ident("x"), ident("foo"))], null, "legacy"));
assertExpr("[ [x,y]     for each (x in foo) for each (y in bar)]",
           compExpr(arrExpr([ident("x"), ident("y")]), [compEachBlock(ident("x"), ident("foo")), compEachBlock(ident("y"), ident("bar"))], null, "legacy"));
assertExpr("[ [x,y,z] for each (x in foo) for each (y in bar) for each (z in baz)]",
           compExpr(arrExpr([ident("x"), ident("y"), ident("z")]),
                    [compEachBlock(ident("x"), ident("foo")), compEachBlock(ident("y"), ident("bar")), compEachBlock(ident("z"), ident("baz"))],
                    null,
                    "legacy"));

assertExpr("[ x         for each (x in foo) if (p)]",
           compExpr(ident("x"), [compEachBlock(ident("x"), ident("foo"))], ident("p"), "legacy"));
assertExpr("[ [x,y]     for each (x in foo) for each (y in bar) if (p)]",
           compExpr(arrExpr([ident("x"), ident("y")]), [compEachBlock(ident("x"), ident("foo")), compEachBlock(ident("y"), ident("bar"))], ident("p"), "legacy"));
assertExpr("[ [x,y,z] for each (x in foo) for each (y in bar) for each (z in baz) if (p) ]",
           compExpr(arrExpr([ident("x"), ident("y"), ident("z")]),
                    [compEachBlock(ident("x"), ident("foo")), compEachBlock(ident("y"), ident("bar")), compEachBlock(ident("z"), ident("baz"))],
                    ident("p"),
                    "legacy"));

// Comprehension expressions using for-of can be written in two different styles.
function assertLegacyAndModernArrayComp(expr, body, blocks, filter) {
    assertExpr(expr, compExpr(body, blocks, filter, "legacy"));

    // Transform the legacy comprehension to a modern comprehension and test it
    // that way too.
    let match = expr.match(/^\[(.*?) for (.*)\]$/);
    assertEq(match !== null, true);
    let expr2 = "[for " + match[2] + " " + match[1] + "]";
    assertExpr(expr2, compExpr(body, blocks, filter, "modern"));
}

assertLegacyAndModernArrayComp("[ x         for (x of foo)]",
                               ident("x"), [compOfBlock(ident("x"), ident("foo"))], null);
assertLegacyAndModernArrayComp("[ [x,y]     for (x of foo) for (y of bar)]",
                               arrExpr([ident("x"), ident("y")]), [compOfBlock(ident("x"), ident("foo")), compOfBlock(ident("y"), ident("bar"))], null);
assertLegacyAndModernArrayComp("[ [x,y,z] for (x of foo) for (y of bar) for (z of baz)]",
                               arrExpr([ident("x"), ident("y"), ident("z")]),
                               [compOfBlock(ident("x"), ident("foo")), compOfBlock(ident("y"), ident("bar")), compOfBlock(ident("z"), ident("baz"))],
                               null);

assertLegacyAndModernArrayComp("[ x         for (x of foo) if (p)]",
                               ident("x"), [compOfBlock(ident("x"), ident("foo"))], ident("p"));
assertLegacyAndModernArrayComp("[ [x,y]     for (x of foo) for (y of bar) if (p)]",
                               arrExpr([ident("x"), ident("y")]), [compOfBlock(ident("x"), ident("foo")), compOfBlock(ident("y"), ident("bar"))], ident("p"));
assertLegacyAndModernArrayComp("[ [x,y,z] for (x of foo) for (y of bar) for (z of baz) if (p) ]",
                               arrExpr([ident("x"), ident("y"), ident("z")]),
                               [compOfBlock(ident("x"), ident("foo")), compOfBlock(ident("y"), ident("bar")), compOfBlock(ident("z"), ident("baz"))],
                               ident("p"));

// Modern comprehensions with multiple ComprehensionIf.

assertExpr("[for (x of foo) x]",
           compExpr(ident("x"), [compOfBlock(ident("x"), ident("foo"))], null, "modern"));
assertExpr("[for (x of foo) if (c1) x]",
           compExpr(ident("x"), [compOfBlock(ident("x"), ident("foo")),
                                 compIf(ident("c1"))], null, "modern"));
assertExpr("[for (x of foo) if (c1) if (c2) x]",
           compExpr(ident("x"), [compOfBlock(ident("x"), ident("foo")),
                                 compIf(ident("c1")), compIf(ident("c2"))], null, "modern"));

assertExpr("[for (x of foo) if (c1) for (y of bar) x]",
           compExpr(ident("x"), [compOfBlock(ident("x"), ident("foo")),
                                 compIf(ident("c1")),
                                 compOfBlock(ident("y"), ident("bar"))], null, "modern"));
assertExpr("[for (x of foo) if (c1) for (y of bar) if (c2) x]",
           compExpr(ident("x"), [compOfBlock(ident("x"), ident("foo")),
                                 compIf(ident("c1")),
                                 compOfBlock(ident("y"), ident("bar")),
                                 compIf(ident("c2"))], null, "modern"));
assertExpr("[for (x of foo) if (c1) if (c2) for (y of bar) if (c3) if (c4) x]",
           compExpr(ident("x"), [compOfBlock(ident("x"), ident("foo")),
                                 compIf(ident("c1")), compIf(ident("c2")),
                                 compOfBlock(ident("y"), ident("bar")),
                                 compIf(ident("c3")), compIf(ident("c4"))], null, "modern"));

assertExpr("[for (x of y) if (false) for (z of w) if (0) x]",
           compExpr(ident("x"), [compOfBlock(ident("x"), ident("y")),
                                 compIf(lit(false)),
                                 compOfBlock(ident("z"), ident("w")),
                                 compIf(lit(0))], null, "modern"));

// generator expressions

assertExpr("( x         for (x in foo))",
           genExpr(ident("x"), [compBlock(ident("x"), ident("foo"))], null, "legacy"));
assertExpr("( [x,y]     for (x in foo) for (y in bar))",
           genExpr(arrExpr([ident("x"), ident("y")]), [compBlock(ident("x"), ident("foo")), compBlock(ident("y"), ident("bar"))], null, "legacy"));
assertExpr("( [x,y,z] for (x in foo) for (y in bar) for (z in baz))",
           genExpr(arrExpr([ident("x"), ident("y"), ident("z")]),
                   [compBlock(ident("x"), ident("foo")), compBlock(ident("y"), ident("bar")), compBlock(ident("z"), ident("baz"))],
                   null,
                   "legacy"));

assertExpr("( x         for (x in foo) if (p))",
           genExpr(ident("x"), [compBlock(ident("x"), ident("foo"))], ident("p"), "legacy"));
assertExpr("( [x,y]     for (x in foo) for (y in bar) if (p))",
           genExpr(arrExpr([ident("x"), ident("y")]), [compBlock(ident("x"), ident("foo")), compBlock(ident("y"), ident("bar"))], ident("p"), "legacy"));
assertExpr("( [x,y,z] for (x in foo) for (y in bar) for (z in baz) if (p) )",
           genExpr(arrExpr([ident("x"), ident("y"), ident("z")]),
                   [compBlock(ident("x"), ident("foo")), compBlock(ident("y"), ident("bar")), compBlock(ident("z"), ident("baz"))],
                   ident("p"),
                   "legacy"));

assertExpr("( x         for each (x in foo))",
           genExpr(ident("x"), [compEachBlock(ident("x"), ident("foo"))], null, "legacy"));
assertExpr("( [x,y]     for each (x in foo) for each (y in bar))",
           genExpr(arrExpr([ident("x"), ident("y")]), [compEachBlock(ident("x"), ident("foo")), compEachBlock(ident("y"), ident("bar"))], null, "legacy"));
assertExpr("( [x,y,z] for each (x in foo) for each (y in bar) for each (z in baz))",
           genExpr(arrExpr([ident("x"), ident("y"), ident("z")]),
                   [compEachBlock(ident("x"), ident("foo")), compEachBlock(ident("y"), ident("bar")), compEachBlock(ident("z"), ident("baz"))],
                   null,
                   "legacy"));

assertExpr("( x         for each (x in foo) if (p))",
           genExpr(ident("x"), [compEachBlock(ident("x"), ident("foo"))], ident("p"), "legacy"));
assertExpr("( [x,y]     for each (x in foo) for each (y in bar) if (p))",
           genExpr(arrExpr([ident("x"), ident("y")]), [compEachBlock(ident("x"), ident("foo")), compEachBlock(ident("y"), ident("bar"))], ident("p"), "legacy"));
assertExpr("( [x,y,z] for each (x in foo) for each (y in bar) for each (z in baz) if (p) )",
           genExpr(arrExpr([ident("x"), ident("y"), ident("z")]),
                   [compEachBlock(ident("x"), ident("foo")), compEachBlock(ident("y"), ident("bar")), compEachBlock(ident("z"), ident("baz"))],
                   ident("p"),
                   "legacy"));

// Generator expressions using for-of can be written in two different styles.
function assertLegacyAndModernGenExpr(expr, body, blocks, filter) {
    assertExpr(expr, genExpr(body, blocks, filter, "legacy"));

    // Transform the legacy genexpr to a modern genexpr and test it that way
    // too.
    let match = expr.match(/^\((.*?) for (.*)\)$/);
    assertEq(match !== null, true);
    let expr2 = "(for " + match[2] + " " + match[1] + ")";
    assertExpr(expr2, genExpr(body, blocks, filter, "modern"));
}

assertLegacyAndModernGenExpr("( x         for (x of foo))",
                             ident("x"), [compOfBlock(ident("x"), ident("foo"))], null);
assertLegacyAndModernGenExpr("( [x,y]     for (x of foo) for (y of bar))",
                             arrExpr([ident("x"), ident("y")]), [compOfBlock(ident("x"), ident("foo")), compOfBlock(ident("y"), ident("bar"))], null);
assertLegacyAndModernGenExpr("( [x,y,z] for (x of foo) for (y of bar) for (z of baz))",
                             arrExpr([ident("x"), ident("y"), ident("z")]),
                             [compOfBlock(ident("x"), ident("foo")), compOfBlock(ident("y"), ident("bar")), compOfBlock(ident("z"), ident("baz"))],
                             null);

assertLegacyAndModernGenExpr("( x         for (x of foo) if (p))",
                             ident("x"), [compOfBlock(ident("x"), ident("foo"))], ident("p"));
assertLegacyAndModernGenExpr("( [x,y]     for (x of foo) for (y of bar) if (p))",
                             arrExpr([ident("x"), ident("y")]), [compOfBlock(ident("x"), ident("foo")), compOfBlock(ident("y"), ident("bar"))], ident("p"));
assertLegacyAndModernGenExpr("( [x,y,z] for (x of foo) for (y of bar) for (z of baz) if (p) )",
                             arrExpr([ident("x"), ident("y"), ident("z")]),
                             [compOfBlock(ident("x"), ident("foo")), compOfBlock(ident("y"), ident("bar")), compOfBlock(ident("z"), ident("baz"))],
                             ident("p"));

// Modern generator comprehension with multiple ComprehensionIf.

assertExpr("(for (x of foo) x)",
           genExpr(ident("x"), [compOfBlock(ident("x"), ident("foo"))], null, "modern"));
assertExpr("(for (x of foo) if (c1) x)",
           genExpr(ident("x"), [compOfBlock(ident("x"), ident("foo")),
                                compIf(ident("c1"))], null, "modern"));
assertExpr("(for (x of foo) if (c1) if (c2) x)",
           genExpr(ident("x"), [compOfBlock(ident("x"), ident("foo")),
                                compIf(ident("c1")), compIf(ident("c2"))], null, "modern"));

assertExpr("(for (x of foo) if (c1) for (y of bar) x)",
           genExpr(ident("x"), [compOfBlock(ident("x"), ident("foo")),
                                compIf(ident("c1")),
                                compOfBlock(ident("y"), ident("bar"))], null, "modern"));
assertExpr("(for (x of foo) if (c1) for (y of bar) if (c2) x)",
           genExpr(ident("x"), [compOfBlock(ident("x"), ident("foo")),
                                compIf(ident("c1")),
                                compOfBlock(ident("y"), ident("bar")),
                                compIf(ident("c2"))], null, "modern"));
assertExpr("(for (x of foo) if (c1) if (c2) for (y of bar) if (c3) if (c4) x)",
           genExpr(ident("x"), [compOfBlock(ident("x"), ident("foo")),
                                compIf(ident("c1")), compIf(ident("c2")),
                                compOfBlock(ident("y"), ident("bar")),
                                compIf(ident("c3")), compIf(ident("c4"))], null, "modern"));

// NOTE: it would be good to test generator expressions both with and without upvars, just like functions above.


// let expressions

assertExpr("(let (x=1) x)", letExpr([{ id: ident("x"), init: lit(1) }], ident("x")));
assertExpr("(let (x=1,y=2) y)", letExpr([{ id: ident("x"), init: lit(1) },
                                         { id: ident("y"), init: lit(2) }],
                                        ident("y")));
assertExpr("(let (x=1,y=2,z=3) z)", letExpr([{ id: ident("x"), init: lit(1) },
                                             { id: ident("y"), init: lit(2) },
                                             { id: ident("z"), init: lit(3) }],
                                            ident("z")));
assertExpr("(let (x) x)", letExpr([{ id: ident("x"), init: null }], ident("x")));
assertExpr("(let (x,y) y)", letExpr([{ id: ident("x"), init: null },
                                     { id: ident("y"), init: null }],
                                    ident("y")));
assertExpr("(let (x,y,z) z)", letExpr([{ id: ident("x"), init: null },
                                       { id: ident("y"), init: null },
                                       { id: ident("z"), init: null }],
                                      ident("z")));
assertExpr("(let (x = 1, y = x) y)", letExpr([{ id: ident("x"), init: lit(1) },
                                              { id: ident("y"), init: ident("x") }],
                                             ident("y")));
assertError("(let (x = 1, x = 2) x)", TypeError);

// let statements

assertStmt("let (x=1) { }", letStmt([{ id: ident("x"), init: lit(1) }], blockStmt([])));
assertStmt("let (x=1,y=2) { }", letStmt([{ id: ident("x"), init: lit(1) },
                                         { id: ident("y"), init: lit(2) }],
                                        blockStmt([])));
assertStmt("let (x=1,y=2,z=3) { }", letStmt([{ id: ident("x"), init: lit(1) },
                                             { id: ident("y"), init: lit(2) },
                                             { id: ident("z"), init: lit(3) }],
                                            blockStmt([])));
assertStmt("let (x) { }", letStmt([{ id: ident("x"), init: null }], blockStmt([])));
assertStmt("let (x,y) { }", letStmt([{ id: ident("x"), init: null },
                                     { id: ident("y"), init: null }],
                                    blockStmt([])));
assertStmt("let (x,y,z) { }", letStmt([{ id: ident("x"), init: null },
                                       { id: ident("y"), init: null },
                                       { id: ident("z"), init: null }],
                                      blockStmt([])));
assertStmt("let (x = 1, y = x) { }", letStmt([{ id: ident("x"), init: lit(1) },
                                              { id: ident("y"), init: ident("x") }],
                                             blockStmt([])));
assertError("let (x = 1, x = 2) { }", TypeError);


// Bug 632024: no crashing on stack overflow
try {
    Reflect.parse(Array(3000).join("x + y - ") + "z")
} catch (e) { }


// Classes
function classesEnabled() {
    try {
        Reflect.parse("class foo { constructor() { } }");
        return true;
    } catch (e) {
        assertEq(e instanceof SyntaxError, true);
        return false;
    }
}

function testClasses() {
    // No unnamed class statements.
    assertError("class { constructor() { } }", SyntaxError);

    function simpleMethod(id, kind, generator, args=[], isStatic=false) {
        assertEq(generator && kind === "method", generator);
        let idN = ident(id);
        let methodMaker = generator ? genFunExpr : funExpr;
        let methodName = kind !== "method" ? null : idN;
        let methodFun = methodMaker(methodName, args.map(ident), blockStmt([]));

        return classMethod(idN, methodFun, kind, isStatic);
    }
    function emptyCPNMethod(id, isStatic) {
        return classMethod(computedName(lit(id)),
                           funExpr(null, [], blockStmt([])),
                           "method", isStatic);
    }
    function setClassMethods(class_, methods) {
        class_.template.body = methods;
    }
    function setClassHeritage(class_, heritage) {
        class_.template.heritage = heritage;
    }

    let simpleConstructor = simpleMethod("constructor", "method", false);
    let emptyFooClass = classStmt(ident("Foo"), null, [simpleConstructor]);

    /* Trivial classes */
    assertStmt("class Foo { constructor() { } }", emptyFooClass);

    // Allow methods and accessors
    let stmt = classStmt(ident("Foo"), null,
                         [simpleConstructor, simpleMethod("method", "method", false)]);
    assertStmt("class Foo { constructor() { } method() { } }", stmt);

    setClassMethods(stmt, [simpleConstructor, simpleMethod("method", "get", false)]);
    assertStmt("class Foo { constructor() { } get method() { } }", stmt);

    setClassMethods(stmt, [simpleConstructor, simpleMethod("method", "set", false, ["x"])]);
    assertStmt("class Foo { constructor() { } set method(x) { } }", stmt);

    /* Static */
    setClassMethods(stmt, [simpleConstructor,
                           simpleMethod("method", "method", false, [], true),
                           simpleMethod("methodGen", "method", true, [], true),
                           simpleMethod("getter", "get", false, [], true),
                           simpleMethod("setter", "set", false, ["x"], true)]);
    assertStmt(`class Foo {
                  constructor() { };
                  static method() { };
                  static *methodGen() { };
                  static get getter() { };
                  static set setter(x) { }
                }`, stmt);


    // It's not an error to have a method named static, static, or not.
    setClassMethods(stmt, [simpleConstructor, simpleMethod("static", "method", false, [], false)]);
    assertStmt("class Foo{ constructor() { } static() { } }", stmt);
    setClassMethods(stmt, [simpleMethod("static", "method", false, [], true), simpleConstructor]);
    assertStmt("class Foo{ static static() { }; constructor() { } }", stmt);
    setClassMethods(stmt, [simpleMethod("static", "get", false, [], true), simpleConstructor]);
    assertStmt("class Foo { static get static() { }; constructor() { } }", stmt);
    setClassMethods(stmt, [simpleConstructor, simpleMethod("static", "set", false, ["x"], true)]);
    assertStmt("class Foo { constructor() { }; static set static(x) { } }", stmt);

    // You do, however, have to put static in the right spot
    assertError("class Foo { constructor() { }; get static foo() { } }", SyntaxError);

    // Spec disallows "prototype" as a static member in a class, since that
    // one's important to make the desugaring work
    assertError("class Foo { constructor() { } static prototype() { } }", SyntaxError);
    assertError("class Foo { constructor() { } static *prototype() { } }", SyntaxError);
    assertError("class Foo { static get prototype() { }; constructor() { } }", SyntaxError);
    assertError("class Foo { static set prototype(x) { }; constructor() { } }", SyntaxError);

    // You are, however, allowed to have a CPN called prototype as a static
    setClassMethods(stmt, [simpleConstructor, emptyCPNMethod("prototype", true)]);
    assertStmt("class Foo { constructor() { }; static [\"prototype\"]() { } }", stmt);

    /* Constructor */
    // Currently, we do not allow default constructors
    assertError("class Foo { }", TypeError);

    // It is an error to have two methods named constructor, but not other
    // names, regardless if one is an accessor or a generator or static.
    assertError("class Foo { constructor() { } constructor(a) { } }", SyntaxError);
    let methods = [["method() { }", simpleMethod("method", "method", false)],
                   ["*method() { }", simpleMethod("method", "method", true)],
                   ["get method() { }", simpleMethod("method", "get", false)],
                   ["set method(x) { }", simpleMethod("method", "set", false, ["x"])],
                   ["static method() { }", simpleMethod("method", "method", false, [], true)],
                   ["static *method() { }", simpleMethod("method", "method", true, [], true)],
                   ["static get method() { }", simpleMethod("method", "get", false, [], true)],
                   ["static set method(x) { }", simpleMethod("method", "set", false, ["x"], true)]];
    let i,j;
    for (i=0; i < methods.length; i++) {
        for (j=0; j < methods.length; j++) {
            setClassMethods(stmt,
                            [simpleConstructor,
                             methods[i][1],
                             methods[j][1]]);
            let str = "class Foo { constructor() { } " +
                       methods[i][0] + " " + methods[j][0] +
                       " }";
            assertStmt(str, stmt);
        }
    }

    // It is, however, not an error to have a constructor, and a method with a
    // computed property name 'constructor'
    setClassMethods(stmt, [simpleConstructor, emptyCPNMethod("constructor", false)]);
    assertStmt("class Foo { constructor () { } [\"constructor\"] () { } }", stmt);

    // It is an error to have a generator or accessor named constructor
    assertError("class Foo { *constructor() { } }", SyntaxError);
    assertError("class Foo { get constructor() { } }", SyntaxError);
    assertError("class Foo { set constructor() { } }", SyntaxError);

    /* Semicolons */
    // Allow Semicolons in Class Definitions
    assertStmt("class Foo { constructor() { }; }", emptyFooClass);

    // Allow more than one semicolon, even in otherwise trivial classses
    assertStmt("class Foo { ;;; constructor() { } }", emptyFooClass);

    // Semicolons are optional, even if the methods share a line
    setClassMethods(stmt, [simpleMethod("method", "method", false), simpleConstructor]);
    assertStmt("class Foo { method() { } constructor() { } }", stmt);

    /* Generators */
    // No yield as a class name inside a generator
    assertError("function *foo() {\
                    class yield {\
                        constructor() { } \
                    }\
                 }", SyntaxError);

    // Methods may be generators, but not accessors
    assertError("class Foo { constructor() { } *get foo() { } }", SyntaxError);
    assertError("class Foo { constructor() { } *set foo() { } }", SyntaxError);

    setClassMethods(stmt, [simpleMethod("method", "method", true), simpleConstructor]);
    assertStmt("class Foo { *method() { } constructor() { } }", stmt);

    /* Strictness */
    // yield is a strict-mode keyword, and class definitions are always strict.
    assertError("class Foo { constructor() { var yield; } }", SyntaxError);
    // Beware of the strictness of computed property names. Here use bareword
    // deletion (a deprecated action) to check.
    assertError("class Foo { constructor() { } [delete bar]() { }}", SyntaxError);

    /* Bindings */
    // Classes should bind lexically, so they should collide with other in-block
    // lexical bindings
    assertError("{ let Foo; class Foo { constructor() { } } }", TypeError);
    assertError("{ const Foo = 0; class Foo { constructor() { } } }", TypeError);
    assertError("{ class Foo { constructor() { } } class Foo { constructor() { } } }", TypeError);

    // Can't make a lexical binding inside a block.
    assertError("if (1) class Foo { constructor() { } }", SyntaxError);

    /* Heritage Expressions */
    // It's illegal to have things that look like "multiple inheritance":
    // non-parenthesized comma expressions.
    assertError("class Foo extends null, undefined { constructor() { } }", SyntaxError);

    // Again check for strict-mode in heritage expressions
    assertError("class Foo extends (delete x) { constructor() { } }", SyntaxError);

    // You must specify an inheritance if you say "extends"
    assertError("class Foo extends { constructor() { } }", SyntaxError);

    // "extends" is still a valid name for a method
    setClassMethods(stmt, [simpleConstructor, simpleMethod("extends", "method", false)]);
    assertStmt("class Foo { constructor() { }; extends() { } }", stmt);

    // Immediate expression
    setClassMethods(stmt, [simpleConstructor]);
    setClassHeritage(stmt, lit(null));
    assertStmt("class Foo extends null { constructor() { } }", stmt);

    // Sequence expresson
    setClassHeritage(stmt, seqExpr([ident("undefined"), ident("undefined")]));
    assertStmt("class Foo extends (undefined, undefined) { constructor() { } }", stmt);

    // Function expression
    let emptyFunction = funExpr(null, [], blockStmt([]));
    setClassHeritage(stmt, emptyFunction);
    assertStmt("class Foo extends function(){ } { constructor() { } }", stmt);

    // New expression
    setClassHeritage(stmt, newExpr(emptyFunction, []));
    assertStmt("class Foo extends new function(){ }() { constructor() { } }", stmt);

    // Call expression
    setClassHeritage(stmt, callExpr(emptyFunction, []));
    assertStmt("class Foo extends function(){ }() { constructor() { } }", stmt);

    // Dot expression
    setClassHeritage(stmt, dotExpr(objExpr([]), ident("foo")));
    assertStmt("class Foo extends {}.foo { constructor() { } }", stmt);

    // Member expression
    setClassHeritage(stmt, memExpr(objExpr([]), ident("foo")));
    assertStmt("class Foo extends {}[foo] { constructor() { } }", stmt);

    /* EOF */
    // Clipped classes should throw a syntax error
    assertError("class Foo {", SyntaxError);
    assertError("class Foo {;", SyntaxError);
    assertError("class Foo { constructor", SyntaxError);
    assertError("class Foo { constructor(", SyntaxError);
    assertError("class Foo { constructor()", SyntaxError);
    assertError("class Foo { constructor()", SyntaxError);
    assertError("class Foo { constructor() {", SyntaxError);
    assertError("class Foo { constructor() { }", SyntaxError);
    assertError("class Foo { static", SyntaxError);
    assertError("class Foo { static y", SyntaxError);
    assertError("class Foo { static *", SyntaxError);
    assertError("class Foo { static *y", SyntaxError);
    assertError("class Foo { static get", SyntaxError);
    assertError("class Foo { static get y", SyntaxError);
    assertError("class Foo extends", SyntaxError);
}

if (classesEnabled())
    testClasses();


// Source location information


var withoutFileOrLine = Reflect.parse("42");
var withFile = Reflect.parse("42", {source:"foo.js"});
var withFileAndLine = Reflect.parse("42", {source:"foo.js", line:111});

Pattern({ source: null, start: { line: 1, column: 0 }, end: { line: 1, column: 2 } }).match(withoutFileOrLine.loc);
Pattern({ source: "foo.js", start: { line: 1, column: 0 }, end: { line: 1, column: 2 } }).match(withFile.loc);
Pattern({ source: "foo.js", start: { line: 111, column: 0 }, end: { line: 111, column: 2 } }).match(withFileAndLine.loc);

var withoutFileOrLine2 = Reflect.parse("foo +\nbar");
var withFile2 = Reflect.parse("foo +\nbar", {source:"foo.js"});
var withFileAndLine2 = Reflect.parse("foo +\nbar", {source:"foo.js", line:111});

Pattern({ source: null, start: { line: 1, column: 0 }, end: { line: 2, column: 3 } }).match(withoutFileOrLine2.loc);
Pattern({ source: "foo.js", start: { line: 1, column: 0 }, end: { line: 2, column: 3 } }).match(withFile2.loc);
Pattern({ source: "foo.js", start: { line: 111, column: 0 }, end: { line: 112, column: 3 } }).match(withFileAndLine2.loc);

var nested = Reflect.parse("(-b + sqrt(sqr(b) - 4 * a * c)) / (2 * a)", {source:"quad.js"});
var fourAC = nested.body[0].expression.left.right.arguments[0].right;

Pattern({ source: "quad.js", start: { line: 1, column: 20 }, end: { line: 1, column: 29 } }).match(fourAC.loc);


// No source location

assertEq(Reflect.parse("42", {loc:false}).loc, null);
program([exprStmt(lit(42))]).assert(Reflect.parse("42", {loc:false}));


// Builder tests

Pattern("program").match(Reflect.parse("42", {builder:{program:function()"program"}}));

assertGlobalStmt("throw 42", 1, { throwStatement: function() 1 });
assertGlobalStmt("for (;;);", 2, { forStatement: function() 2 });
assertGlobalStmt("for (x in y);", 3, { forInStatement: function() 3 });
assertGlobalStmt("{ }", 4, { blockStatement: function() 4 });
assertGlobalStmt("foo: { }", 5, { labeledStatement: function() 5 });
assertGlobalStmt("with (o) { }", 6, { withStatement: function() 6 });
assertGlobalStmt("while (x) { }", 7, { whileStatement: function() 7 });
assertGlobalStmt("do { } while(false);", 8, { doWhileStatement: function() 8 });
assertGlobalStmt("switch (x) { }", 9, { switchStatement: function() 9 });
assertGlobalStmt("try { } catch(e) { }", 10, { tryStatement: function() 10 });
assertGlobalStmt(";", 11, { emptyStatement: function() 11 });
assertGlobalStmt("debugger;", 12, { debuggerStatement: function() 12 });
assertGlobalStmt("42;", 13, { expressionStatement: function() 13 });
assertGlobalStmt("for (;;) break", forStmt(null, null, null, 14), { breakStatement: function() 14 });
assertGlobalStmt("for (;;) continue", forStmt(null, null, null, 15), { continueStatement: function() 15 });

assertBlockDecl("var x", "var", { variableDeclaration: function(kind) kind });
assertBlockDecl("let x", "let", { variableDeclaration: function(kind) kind });
assertBlockDecl("const x = undefined", "const", { variableDeclaration: function(kind) kind });
assertBlockDecl("function f() { }", "function", { functionDeclaration: function() "function" });

assertGlobalExpr("(x,y,z)", 1, { sequenceExpression: function() 1 });
assertGlobalExpr("(x ? y : z)", 2, { conditionalExpression: function() 2 });
assertGlobalExpr("x + y", 3, { binaryExpression: function() 3 });
assertGlobalExpr("delete x", 4, { unaryExpression: function() 4 });
assertGlobalExpr("x = y", 5, { assignmentExpression: function() 5 });
assertGlobalExpr("x || y", 6, { logicalExpression: function() 6 });
assertGlobalExpr("x++", 7, { updateExpression: function() 7 });
assertGlobalExpr("new x", 8, { newExpression: function() 8 });
assertGlobalExpr("x()", 9, { callExpression: function() 9 });
assertGlobalExpr("x.y", 10, { memberExpression: function() 10 });
assertGlobalExpr("(function() { })", 11, { functionExpression: function() 11 });
assertGlobalExpr("[1,2,3]", 12, { arrayExpression: function() 12 });
assertGlobalExpr("({ x: y })", 13, { objectExpression: function() 13 });
assertGlobalExpr("this", 14, { thisExpression: function() 14 });
assertGlobalExpr("[x for (x in y)]", 17, { comprehensionExpression: function() 17 });
assertGlobalExpr("(x for (x in y))", 18, { generatorExpression: function() 18 });
assertGlobalExpr("(function() { yield 42 })", genFunExpr(null, [], blockStmt([exprStmt(19)])), { yieldExpression: function() 19 });
assertGlobalExpr("(let (x) x)", 20, { letExpression: function() 20 });

assertGlobalStmt("switch (x) { case y: }", switchStmt(ident("x"), [1]), { switchCase: function() 1 });
assertGlobalStmt("try { } catch (e) { }", 2, { tryStatement: (function(b, g, u, f) u), catchClause: function() 2 });
assertGlobalStmt("try { } catch (e if e instanceof A) { } catch (e if e instanceof B) { }", [2, 2], { tryStatement: (function(b, g, u, f) g), catchClause: function() 2 });
assertGlobalStmt("try { } catch (e) { }", tryStmt(blockStmt([]), [], 2, null), { catchClause: function() 2 });
assertGlobalStmt("try { } catch (e if e instanceof A) { } catch (e if e instanceof B) { }",
                 tryStmt(blockStmt([]), [2, 2], null, null),
                 { catchClause: function() 2 });
assertGlobalExpr("[x for (y in z) for (x in y)]",
                 compExpr(ident("x"), [3, 3], null, "legacy"),
                 { comprehensionBlock: function() 3 });

assertGlobalExpr("({ x: y } = z)", aExpr("=", 1, ident("z")), { objectPattern: function() 1 });
assertGlobalExpr("({ x: y } = z)", aExpr("=", objPatt([2]), ident("z")), { propertyPattern: function() 2 });
assertGlobalExpr("[ x ] = y", aExpr("=", 3, ident("y")), { arrayPattern: function() 3 });

// Ensure that exceptions thrown by builder methods propagate.
var thrown = false;
try {
    Reflect.parse("42", { builder: { program: function() { throw "expected" } } });
} catch (e if e === "expected") {
    thrown = true;
}
if (!thrown)
    throw new Error("builder exception not propagated");

// A simple proof-of-concept that the builder API can be used to generate other
// formats, such as JsonMLAst:
// 
//     http://code.google.com/p/es-lab/wiki/JsonMLASTFormat
// 
// It's incomplete (e.g., it doesn't convert source-location information and
// doesn't use all the direct-eval rules), but I think it proves the point.

var JsonMLAst = (function() {
function reject() {
    throw new SyntaxError("node type not supported");
}

function isDirectEval(expr) {
    // an approximation to the actual rules. you get the idea
    return (expr[0] === "IdExpr" && expr[1].name === "eval");
}

function functionNode(type) {
    return function(id, args, body, isGenerator, isExpression) {
        if (isExpression)
            body = ["ReturnStmt", {}, body];

        if (!id)
            id = ["Empty"];

        // Patch up the argument node types: s/IdExpr/IdPatt/g
        for (var i = 0; i < args.length; i++) {
            args[i][0] = "IdPatt";
        }

        args.unshift("ParamDecl", {});

        return [type, {}, id, args, body];
    }
}

return {
    program: function(stmts) {
        stmts.unshift("Program", {});
        return stmts;
    },
    identifier: function(name) {
        return ["IdExpr", { name: name }];
    },
    literal: function(val) {
        return ["LiteralExpr", { value: val }];
    },
    expressionStatement: function(expr) expr,
    conditionalExpression: function(test, cons, alt) {
        return ["ConditionalExpr", {}, test, cons, alt];
    },
    unaryExpression: function(op, expr) {
        return ["UnaryExpr", {op: op}, expr];
    },
    binaryExpression: function(op, left, right) {
        return ["BinaryExpr", {op: op}, left, right];
    },
    property: function(kind, key, val) {
        return [kind === "init"
                ? "DataProp"
                : kind === "get"
                ? "GetterProp"
                : "SetterProp",
                {name: key[1].name}, val];
    },
    functionDeclaration: functionNode("FunctionDecl"),
    variableDeclaration: function(kind, elts) {
        if (kind === "let" || kind === "const")
            throw new SyntaxError("let and const not supported");
        elts.unshift("VarDecl", {});
        return elts;
    },
    variableDeclarator: function(id, init) {
        id[0] = "IdPatt";
        if (!init)
            return id;
        return ["InitPatt", {}, id, init];
    },
    sequenceExpression: function(exprs) {
        var length = exprs.length;
        var result = ["BinaryExpr", {op:","}, exprs[exprs.length - 2], exprs[exprs.length - 1]];
        for (var i = exprs.length - 3; i >= 0; i--) {
            result = ["BinaryExpr", {op:","}, exprs[i], result];
        }
        return result;
    },
    assignmentExpression: function(op, lhs, expr) {
        return ["AssignExpr", {op: op}, lhs, expr];
    },
    logicalExpression: function(op, left, right) {
        return [op === "&&" ? "LogicalAndExpr" : "LogicalOrExpr", {}, left, right];
    },
    updateExpression: function(expr, op, isPrefix) {
        return ["CountExpr", {isPrefix:isPrefix, op:op}, expr];
    },
    newExpression: function(callee, args) {
        args.unshift("NewExpr", {}, callee);
        return args;
    },
    callExpression: function(callee, args) {
        args.unshift(isDirectEval(callee) ? "EvalExpr" : "CallExpr", {}, callee);
        return args;
    },
    memberExpression: function(isComputed, expr, member) {
        return ["MemberExpr", {}, expr, isComputed ? member : ["LiteralExpr", {type: "string", value: member[1].name}]];
    },
    functionExpression: functionNode("FunctionExpr"),
    arrayExpression: function(elts) {
        for (var i = 0; i < elts.length; i++) {
            if (!elts[i])
                elts[i] = ["Empty"];
        }
        elts.unshift("ArrayExpr", {});
        return elts;
    },
    objectExpression: function(props) {
        props.unshift("ObjectExpr", {});
        return props;
    },
    thisExpression: function() {
        return ["ThisExpr", {}];
    },
    templateLiteral: function(elts) {
        for (var i = 0; i < elts.length; i++) {
            if (!elts[i])
                elts[i] = ["Empty"];
        }
        elts.unshift("TemplateLit", {});
        return elts;
    },

    graphExpression: reject,
    graphIndexExpression: reject,
    comprehensionExpression: reject,
    generatorExpression: reject,
    yieldExpression: reject,
    letExpression: reject,

    emptyStatement: function() ["EmptyStmt", {}],
    blockStatement: function(stmts) {
        stmts.unshift("BlockStmt", {});
        return stmts;
    },
    labeledStatement: function(lab, stmt) {
        return ["LabelledStmt", {label: lab}, stmt];
    },
    ifStatement: function(test, cons, alt) {
        return ["IfStmt", {}, test, cons, alt || ["EmptyStmt", {}]];
    },
    switchStatement: function(test, clauses, isLexical) {
        clauses.unshift("SwitchStmt", {}, test);
        return clauses;
    },
    whileStatement: function(expr, stmt) {
        return ["WhileStmt", {}, expr, stmt];
    },
    doWhileStatement: function(stmt, expr) {
        return ["DoWhileStmt", {}, stmt, expr];
    },
    forStatement: function(init, test, update, body) {
        return ["ForStmt", {}, init || ["Empty"], test || ["Empty"], update || ["Empty"], body];
    },
    forInStatement: function(lhs, rhs, body) {
        return ["ForInStmt", {}, lhs, rhs, body];
    },
    breakStatement: function(lab) {
        return lab ? ["BreakStmt", {}, lab] : ["BreakStmt", {}];
    },
    continueStatement: function(lab) {
        return lab ? ["ContinueStmt", {}, lab] : ["ContinueStmt", {}];
    },
    withStatement: function(expr, stmt) {
        return ["WithStmt", {}, expr, stmt];
    },
    returnStatement: function(expr) {
        return expr ? ["ReturnStmt", {}, expr] : ["ReturnStmt", {}];
    },
    tryStatement: function(body, catches, fin) {
        if (catches.length > 1)
            throw new SyntaxError("multiple catch clauses not supported");
        var node = ["TryStmt", body, catches[0] || ["Empty"]];
        if (fin)
            node.push(fin);
        return node;
    },
    throwStatement: function(expr) {
        return ["ThrowStmt", {}, expr];
    },
    debuggerStatement: function() ["DebuggerStmt", {}],
    letStatement: reject,
    switchCase: function(expr, stmts) {
        if (expr)
            stmts.unshift("SwitchCase", {}, expr);
        else
            stmts.unshift("DefaultCase", {});
        return stmts;
    },
    catchClause: function(param, guard, body) {
        if (guard)
            throw new SyntaxError("catch guards not supported");
        param[0] = "IdPatt";
        return ["CatchClause", {}, param, body];
    },
    comprehensionBlock: reject,

    arrayPattern: reject,
    objectPattern: reject,
    propertyPattern: reject,
};
})();

Pattern(["Program", {},
         ["BinaryExpr", {op: "+"},
          ["LiteralExpr", {value: 2}],
          ["BinaryExpr", {op: "*"},
           ["UnaryExpr", {op: "-"}, ["IdExpr", {name: "x"}]],
           ["IdExpr", {name: "y"}]]]]).match(Reflect.parse("2 + (-x * y)", {loc: false, builder: JsonMLAst}));

reportCompare(true, true);

});
