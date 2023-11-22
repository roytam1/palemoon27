/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/licenses/publicdomain/ */

// We should preserve holes when sorting sparce arrays.
// See bug: 1246860

function denseCount(arr) {
    var c = 0;
    for (var i = 0; i < arr.length; i++)
        if (i in arr)
            c++;
    return c;
}

let a = [,,,,,,,,,,,,,,,,,,,,{size: 1},{size: 2}];
let b = [,,,,,,,,,,,,,,,,,,,,{size: 1},{size: 2}].sort();
let c = [,,,,,,,,,,,,,,,,,,,,{size: 1},{size: 2}].sort((a, b) => {+a.size - +b.size});

assertEq(a.length, 22);
assertEq(denseCount(a), 2);
assertEq(a.length, b.length);
assertEq(b.length, c.length);
assertEq(denseCount(a), denseCount(b));
assertEq(denseCount(b), denseCount(c));

let superSparce = new Array(5000);
superSparce[0] = 99;
superSparce[4000] = 0;
superSparce[4999] = -1;

assertEq(superSparce.length, 5000);
assertEq(denseCount(superSparce), 3);

superSparce.sort((a, b) => 1*a-b);
assertEq(superSparce.length, 5000);
assertEq(denseCount(superSparce), 3);
assertEq(superSparce[0], -1);
assertEq(superSparce[1], 0);
assertEq(superSparce[2], 99);

let allHoles = new Array(2600);
assertEq(allHoles.length, 2600);
assertEq(denseCount(allHoles), 0);
allHoles.sort((a, b) => 1*a-b);
assertEq(allHoles.length, 2600);
assertEq(denseCount(allHoles), 0);

let oneHole = new Array(2600);
oneHole[1399] = {size: 27};
assertEq(oneHole.length, 2600);
assertEq(denseCount(oneHole), 1);
oneHole.sort((a, b) => {+a.size - +b.size});
assertDeepEq(oneHole[0], {size: 27});
assertEq(oneHole.length, 2600);
assertEq(denseCount(oneHole), 1);

// Ensure that the array setter is touch touched during sorting.

Object.defineProperty(Array.prototype, "0", {
    set: (value) => {throw "Illegally touched the array's setter!"},
    configurable: true
});

assertThrows(() => {o[1] = 11;});

let o = [,,,,,,,,,,,,,,,,,,,,{size: 1},{size: 2}];
o.sort((a, b) => {+a.size - +b.size});

delete Array.prototype["0"];

if (typeof reportCompare === 'function')
    reportCompare(0, 0);
