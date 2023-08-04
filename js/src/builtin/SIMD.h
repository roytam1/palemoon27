/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef builtin_SIMD_h
#define builtin_SIMD_h

#include "jsapi.h"
#include "jsobj.h"

#include "builtin/TypedObject.h"
#include "js/Conversions.h"
#include "vm/GlobalObject.h"

/*
 * JS SIMD functions.
 * Spec matching polyfill:
 * https://github.com/tc39/ecmascript_simd/blob/master/src/ecmascript_simd.js
 */

// Bool8x16.
#define BOOL8X16_UNARY_FUNCTION_LIST(V)                                               \
  V(not, (UnaryFunc<Bool8x16, LogicalNot, Bool8x16>), 1)                              \
  V(check, (UnaryFunc<Bool8x16, Identity, Bool8x16>), 1)                              \
  V(splat, (FuncSplat<Bool8x16>), 1)                                                  \
  V(allTrue, (AllTrue<Bool8x16>), 1)                                                  \
  V(anyTrue, (AnyTrue<Bool8x16>), 1)

#define BOOL8X16_BINARY_FUNCTION_LIST(V)                                              \
  V(extractLane, (ExtractLane<Bool8x16>), 2)                                          \
  V(and, (BinaryFunc<Bool8x16, And, Bool8x16>), 2)                                    \
  V(or, (BinaryFunc<Bool8x16, Or, Bool8x16>), 2)                                      \
  V(xor, (BinaryFunc<Bool8x16, Xor, Bool8x16>), 2)                                    \

#define BOOL8X16_TERNARY_FUNCTION_LIST(V)                                             \
  V(replaceLane, (ReplaceLane<Bool8x16>), 3)

#define BOOL8X16_FUNCTION_LIST(V)                                                     \
  BOOL8X16_UNARY_FUNCTION_LIST(V)                                                     \
  BOOL8X16_BINARY_FUNCTION_LIST(V)                                                    \
  BOOL8X16_TERNARY_FUNCTION_LIST(V)

// Bool 16x8.
#define BOOL16X8_UNARY_FUNCTION_LIST(V)                                               \
  V(not, (UnaryFunc<Bool16x8, LogicalNot, Bool16x8>), 1)                              \
  V(check, (UnaryFunc<Bool16x8, Identity, Bool16x8>), 1)                              \
  V(splat, (FuncSplat<Bool16x8>), 1)                                                  \
  V(allTrue, (AllTrue<Bool16x8>), 1)                                                  \
  V(anyTrue, (AnyTrue<Bool16x8>), 1)

#define BOOL16X8_BINARY_FUNCTION_LIST(V)                                              \
  V(extractLane, (ExtractLane<Bool16x8>), 2)                                          \
  V(and, (BinaryFunc<Bool16x8, And, Bool16x8>), 2)                                    \
  V(or, (BinaryFunc<Bool16x8, Or, Bool16x8>), 2)                                      \
  V(xor, (BinaryFunc<Bool16x8, Xor, Bool16x8>), 2)                                    \

#define BOOL16X8_TERNARY_FUNCTION_LIST(V)                                             \
  V(replaceLane, (ReplaceLane<Bool16x8>), 3)

#define BOOL16X8_FUNCTION_LIST(V)                                                     \
  BOOL16X8_UNARY_FUNCTION_LIST(V)                                                     \
  BOOL16X8_BINARY_FUNCTION_LIST(V)                                                    \
  BOOL16X8_TERNARY_FUNCTION_LIST(V)

// Bool32x4.
#define BOOL32X4_UNARY_FUNCTION_LIST(V)                                               \
  V(not, (UnaryFunc<Bool32x4, LogicalNot, Bool32x4>), 1)                              \
  V(check, (UnaryFunc<Bool32x4, Identity, Bool32x4>), 1)                              \
  V(splat, (FuncSplat<Bool32x4>), 1)                                                  \
  V(allTrue, (AllTrue<Bool32x4>), 1)                                                  \
  V(anyTrue, (AnyTrue<Bool32x4>), 1)

#define BOOL32X4_BINARY_FUNCTION_LIST(V)                                              \
  V(extractLane, (ExtractLane<Bool32x4>), 2)                                          \
  V(and, (BinaryFunc<Bool32x4, And, Bool32x4>), 2)                                    \
  V(or, (BinaryFunc<Bool32x4, Or, Bool32x4>), 2)                                      \
  V(xor, (BinaryFunc<Bool32x4, Xor, Bool32x4>), 2)                                    \

#define BOOL32X4_TERNARY_FUNCTION_LIST(V)                                             \
  V(replaceLane, (ReplaceLane<Bool32x4>), 3)

#define BOOL32X4_FUNCTION_LIST(V)                                                     \
  BOOL32X4_UNARY_FUNCTION_LIST(V)                                                     \
  BOOL32X4_BINARY_FUNCTION_LIST(V)                                                    \
  BOOL32X4_TERNARY_FUNCTION_LIST(V)

// Bool64x2.
#define BOOL64X2_UNARY_FUNCTION_LIST(V)                                               \
  V(not, (UnaryFunc<Bool64x2, LogicalNot, Bool64x2>), 1)                              \
  V(check, (UnaryFunc<Bool64x2, Identity, Bool64x2>), 1)                              \
  V(splat, (FuncSplat<Bool64x2>), 1)                                                  \
  V(allTrue, (AllTrue<Bool64x2>), 1)                                                  \
  V(anyTrue, (AnyTrue<Bool64x2>), 1)

#define BOOL64X2_BINARY_FUNCTION_LIST(V)                                              \
  V(extractLane, (ExtractLane<Bool64x2>), 2)                                          \
  V(and, (BinaryFunc<Bool64x2, And, Bool64x2>), 2)                                    \
  V(or, (BinaryFunc<Bool64x2, Or, Bool64x2>), 2)                                      \
  V(xor, (BinaryFunc<Bool64x2, Xor, Bool64x2>), 2)                                    \

#define BOOL64X2_TERNARY_FUNCTION_LIST(V)                                             \
  V(replaceLane, (ReplaceLane<Bool64x2>), 3)

#define BOOL64X2_FUNCTION_LIST(V)                                                     \
  BOOL64X2_UNARY_FUNCTION_LIST(V)                                                     \
  BOOL64X2_BINARY_FUNCTION_LIST(V)                                                    \
  BOOL64X2_TERNARY_FUNCTION_LIST(V)

// Float32x4.
#define FLOAT32X4_UNARY_FUNCTION_LIST(V)                                              \
  V(abs, (UnaryFunc<Float32x4, Abs, Float32x4>), 1)                                   \
  V(check, (UnaryFunc<Float32x4, Identity, Float32x4>), 1)                            \
  V(fromFloat64x2Bits, (FuncConvertBits<Float64x2, Float32x4>), 1)                    \
  V(fromInt8x16Bits,   (FuncConvertBits<Int8x16,   Float32x4>), 1)                    \
  V(fromInt16x8Bits,   (FuncConvertBits<Int16x8,   Float32x4>), 1)                    \
  V(fromInt32x4,       (FuncConvert<Int32x4,       Float32x4>), 1)                    \
  V(fromInt32x4Bits,   (FuncConvertBits<Int32x4,   Float32x4>), 1)                    \
  V(fromUint8x16Bits,  (FuncConvertBits<Uint8x16,  Float32x4>), 1)                    \
  V(fromUint16x8Bits,  (FuncConvertBits<Uint16x8,  Float32x4>), 1)                    \
  V(fromUint32x4,      (FuncConvert<Uint32x4,      Float32x4>), 1)                    \
  V(fromUint32x4Bits,  (FuncConvertBits<Uint32x4,  Float32x4>), 1)                    \
  V(neg, (UnaryFunc<Float32x4, Neg, Float32x4>), 1)                                   \
  V(reciprocalApproximation, (UnaryFunc<Float32x4, RecApprox, Float32x4>), 1)         \
  V(reciprocalSqrtApproximation, (UnaryFunc<Float32x4, RecSqrtApprox, Float32x4>), 1) \
  V(splat, (FuncSplat<Float32x4>), 1)                                                 \
  V(sqrt, (UnaryFunc<Float32x4, Sqrt, Float32x4>), 1)

#define FLOAT32X4_BINARY_FUNCTION_LIST(V)                                             \
  V(add, (BinaryFunc<Float32x4, Add, Float32x4>), 2)                                  \
  V(div, (BinaryFunc<Float32x4, Div, Float32x4>), 2)                                  \
  V(equal, (CompareFunc<Float32x4, Equal, Bool32x4>), 2)                              \
  V(extractLane, (ExtractLane<Float32x4>), 2)                                         \
  V(greaterThan, (CompareFunc<Float32x4, GreaterThan, Bool32x4>), 2)                  \
  V(greaterThanOrEqual, (CompareFunc<Float32x4, GreaterThanOrEqual, Bool32x4>), 2)    \
  V(lessThan, (CompareFunc<Float32x4, LessThan, Bool32x4>), 2)                        \
  V(lessThanOrEqual, (CompareFunc<Float32x4, LessThanOrEqual, Bool32x4>), 2)          \
  V(load,  (Load<Float32x4, 4>), 2)                                                   \
  V(load3, (Load<Float32x4, 3>), 2)                                                   \
  V(load2, (Load<Float32x4, 2>), 2)                                                   \
  V(load1, (Load<Float32x4, 1>), 2)                                                   \
  V(max, (BinaryFunc<Float32x4, Maximum, Float32x4>), 2)                              \
  V(maxNum, (BinaryFunc<Float32x4, MaxNum, Float32x4>), 2)                            \
  V(min, (BinaryFunc<Float32x4, Minimum, Float32x4>), 2)                              \
  V(minNum, (BinaryFunc<Float32x4, MinNum, Float32x4>), 2)                            \
  V(mul, (BinaryFunc<Float32x4, Mul, Float32x4>), 2)                                  \
  V(notEqual, (CompareFunc<Float32x4, NotEqual, Bool32x4>), 2)                        \
  V(sub, (BinaryFunc<Float32x4, Sub, Float32x4>), 2)

#define FLOAT32X4_TERNARY_FUNCTION_LIST(V)                                            \
  V(replaceLane, (ReplaceLane<Float32x4>), 3)                                         \
  V(select, (Select<Float32x4, Bool32x4>), 3)                                         \
  V(store,  (Store<Float32x4, 4>), 3)                                                 \
  V(store3, (Store<Float32x4, 3>), 3)                                                 \
  V(store2, (Store<Float32x4, 2>), 3)                                                 \
  V(store1, (Store<Float32x4, 1>), 3)

#define FLOAT32X4_SHUFFLE_FUNCTION_LIST(V)                                            \
  V(swizzle, Swizzle<Float32x4>, 5)                                                   \
  V(shuffle, Shuffle<Float32x4>, 6)

#define FLOAT32X4_FUNCTION_LIST(V)                                                    \
  FLOAT32X4_UNARY_FUNCTION_LIST(V)                                                    \
  FLOAT32X4_BINARY_FUNCTION_LIST(V)                                                   \
  FLOAT32X4_TERNARY_FUNCTION_LIST(V)                                                  \
  FLOAT32X4_SHUFFLE_FUNCTION_LIST(V)

// Float64x2.
#define FLOAT64X2_UNARY_FUNCTION_LIST(V)                                              \
  V(abs, (UnaryFunc<Float64x2, Abs, Float64x2>), 1)                                   \
  V(check, (UnaryFunc<Float64x2, Identity, Float64x2>), 1)                            \
  V(fromFloat32x4Bits, (FuncConvertBits<Float32x4, Float64x2>), 1)                    \
  V(fromInt8x16Bits,   (FuncConvertBits<Int8x16,   Float64x2>), 1)                    \
  V(fromInt16x8Bits,   (FuncConvertBits<Int16x8,   Float64x2>), 1)                    \
  V(fromInt32x4Bits,   (FuncConvertBits<Int32x4,   Float64x2>), 1)                    \
  V(fromUint8x16Bits,  (FuncConvertBits<Uint8x16,  Float64x2>), 1)                    \
  V(fromUint16x8Bits,  (FuncConvertBits<Uint16x8,  Float64x2>), 1)                    \
  V(fromUint32x4Bits,  (FuncConvertBits<Uint32x4,  Float64x2>), 1)                    \
  V(neg, (UnaryFunc<Float64x2, Neg, Float64x2>), 1)                                   \
  V(reciprocalApproximation, (UnaryFunc<Float64x2, RecApprox, Float64x2>), 1)         \
  V(reciprocalSqrtApproximation, (UnaryFunc<Float64x2, RecSqrtApprox, Float64x2>), 1) \
  V(splat, (FuncSplat<Float64x2>), 1)                                                 \
  V(sqrt, (UnaryFunc<Float64x2, Sqrt, Float64x2>), 1)

#define FLOAT64X2_BINARY_FUNCTION_LIST(V)                                             \
  V(add, (BinaryFunc<Float64x2, Add, Float64x2>), 2)                                  \
  V(div, (BinaryFunc<Float64x2, Div, Float64x2>), 2)                                  \
  V(equal, (CompareFunc<Float64x2, Equal, Bool64x2>), 2)                              \
  V(extractLane, (ExtractLane<Float64x2>), 2)                                         \
  V(greaterThan, (CompareFunc<Float64x2, GreaterThan, Bool64x2>), 2)                  \
  V(greaterThanOrEqual, (CompareFunc<Float64x2, GreaterThanOrEqual, Bool64x2>), 2)    \
  V(lessThan, (CompareFunc<Float64x2, LessThan, Bool64x2>), 2)                        \
  V(lessThanOrEqual, (CompareFunc<Float64x2, LessThanOrEqual, Bool64x2>), 2)          \
  V(load,  (Load<Float64x2, 2>), 2)                                                   \
  V(load1, (Load<Float64x2, 1>), 2)                                                   \
  V(max, (BinaryFunc<Float64x2, Maximum, Float64x2>), 2)                              \
  V(maxNum, (BinaryFunc<Float64x2, MaxNum, Float64x2>), 2)                            \
  V(min, (BinaryFunc<Float64x2, Minimum, Float64x2>), 2)                              \
  V(minNum, (BinaryFunc<Float64x2, MinNum, Float64x2>), 2)                            \
  V(mul, (BinaryFunc<Float64x2, Mul, Float64x2>), 2)                                  \
  V(notEqual, (CompareFunc<Float64x2, NotEqual, Bool64x2>), 2)                        \
  V(sub, (BinaryFunc<Float64x2, Sub, Float64x2>), 2)

#define FLOAT64X2_TERNARY_FUNCTION_LIST(V)                                            \
  V(replaceLane, (ReplaceLane<Float64x2>), 3)                                         \
  V(select, (Select<Float64x2, Bool64x2>), 3)                                         \
  V(store,  (Store<Float64x2, 2>), 3)                                                 \
  V(store1, (Store<Float64x2, 1>), 3)

#define FLOAT64X2_SHUFFLE_FUNCTION_LIST(V)                                            \
  V(swizzle, Swizzle<Float64x2>, 3)                                                   \
  V(shuffle, Shuffle<Float64x2>, 4)

#define FLOAT64X2_FUNCTION_LIST(V)                                                    \
  FLOAT64X2_UNARY_FUNCTION_LIST(V)                                                    \
  FLOAT64X2_BINARY_FUNCTION_LIST(V)                                                   \
  FLOAT64X2_TERNARY_FUNCTION_LIST(V)                                                  \
  FLOAT64X2_SHUFFLE_FUNCTION_LIST(V)

// Int8x16.
#define INT8X16_UNARY_FUNCTION_LIST(V)                                                \
  V(check, (UnaryFunc<Int8x16, Identity, Int8x16>), 1)                                \
  V(fromFloat32x4Bits, (FuncConvertBits<Float32x4, Int8x16>), 1)                      \
  V(fromFloat64x2Bits, (FuncConvertBits<Float64x2, Int8x16>), 1)                      \
  V(fromInt16x8Bits,   (FuncConvertBits<Int16x8,   Int8x16>), 1)                      \
  V(fromInt32x4Bits,   (FuncConvertBits<Int32x4,   Int8x16>), 1)                      \
  V(fromUint8x16Bits,  (FuncConvertBits<Uint8x16,  Int8x16>), 1)                      \
  V(fromUint16x8Bits,  (FuncConvertBits<Uint16x8,  Int8x16>), 1)                      \
  V(fromUint32x4Bits,  (FuncConvertBits<Uint32x4,  Int8x16>), 1)                      \
  V(neg, (UnaryFunc<Int8x16, Neg, Int8x16>), 1)                                       \
  V(not, (UnaryFunc<Int8x16, Not, Int8x16>), 1)                                       \
  V(splat, (FuncSplat<Int8x16>), 1)

#define INT8X16_BINARY_FUNCTION_LIST(V)                                               \
  V(add, (BinaryFunc<Int8x16, Add, Int8x16>), 2)                                      \
  V(addSaturate, (BinaryFunc<Int8x16, AddSaturate, Int8x16>), 2)                      \
  V(and, (BinaryFunc<Int8x16, And, Int8x16>), 2)                                      \
  V(equal, (CompareFunc<Int8x16, Equal, Bool8x16>), 2)                                \
  V(extractLane, (ExtractLane<Int8x16>), 2)                                           \
  V(greaterThan, (CompareFunc<Int8x16, GreaterThan, Bool8x16>), 2)                    \
  V(greaterThanOrEqual, (CompareFunc<Int8x16, GreaterThanOrEqual, Bool8x16>), 2)      \
  V(lessThan, (CompareFunc<Int8x16, LessThan, Bool8x16>), 2)                          \
  V(lessThanOrEqual, (CompareFunc<Int8x16, LessThanOrEqual, Bool8x16>), 2)            \
  V(load, (Load<Int8x16, 16>), 2)                                                     \
  V(mul, (BinaryFunc<Int8x16, Mul, Int8x16>), 2)                                      \
  V(notEqual, (CompareFunc<Int8x16, NotEqual, Bool8x16>), 2)                          \
  V(or, (BinaryFunc<Int8x16, Or, Int8x16>), 2)                                        \
  V(sub, (BinaryFunc<Int8x16, Sub, Int8x16>), 2)                                      \
  V(subSaturate, (BinaryFunc<Int8x16, SubSaturate, Int8x16>), 2)                      \
  V(shiftLeftByScalar, (BinaryScalar<Int8x16, ShiftLeft>), 2)                         \
  V(shiftRightByScalar, (BinaryScalar<Int8x16, ShiftRightArithmetic>), 2)             \
  V(shiftRightArithmeticByScalar, (BinaryScalar<Int8x16, ShiftRightArithmetic>), 2)   \
  V(shiftRightLogicalByScalar, (BinaryScalar<Int8x16, ShiftRightLogical>), 2)         \
  V(xor, (BinaryFunc<Int8x16, Xor, Int8x16>), 2)

#define INT8X16_TERNARY_FUNCTION_LIST(V)                                              \
  V(replaceLane, (ReplaceLane<Int8x16>), 3)                                           \
  V(select, (Select<Int8x16, Bool8x16>), 3)                                           \
  V(store, (Store<Int8x16, 16>), 3)

#define INT8X16_SHUFFLE_FUNCTION_LIST(V)                                              \
  V(swizzle, Swizzle<Int8x16>, 17)                                                    \
  V(shuffle, Shuffle<Int8x16>, 18)

#define INT8X16_FUNCTION_LIST(V)                                                      \
  INT8X16_UNARY_FUNCTION_LIST(V)                                                      \
  INT8X16_BINARY_FUNCTION_LIST(V)                                                     \
  INT8X16_TERNARY_FUNCTION_LIST(V)                                                    \
  INT8X16_SHUFFLE_FUNCTION_LIST(V)

// Uint8x16.
#define UINT8X16_UNARY_FUNCTION_LIST(V)                                               \
  V(check, (UnaryFunc<Uint8x16, Identity, Uint8x16>), 1)                              \
  V(fromFloat32x4Bits, (FuncConvertBits<Float32x4, Uint8x16>), 1)                     \
  V(fromFloat64x2Bits, (FuncConvertBits<Float64x2, Uint8x16>), 1)                     \
  V(fromInt8x16Bits,   (FuncConvertBits<Int8x16,   Uint8x16>), 1)                     \
  V(fromInt16x8Bits,   (FuncConvertBits<Int16x8,   Uint8x16>), 1)                     \
  V(fromInt32x4Bits,   (FuncConvertBits<Int32x4,   Uint8x16>), 1)                     \
  V(fromUint16x8Bits,  (FuncConvertBits<Uint16x8,  Uint8x16>), 1)                     \
  V(fromUint32x4Bits,  (FuncConvertBits<Uint32x4,  Uint8x16>), 1)                     \
  V(neg, (UnaryFunc<Uint8x16, Neg, Uint8x16>), 1)                                     \
  V(not, (UnaryFunc<Uint8x16, Not, Uint8x16>), 1)                                     \
  V(splat, (FuncSplat<Uint8x16>), 1)

#define UINT8X16_BINARY_FUNCTION_LIST(V)                                              \
  V(add, (BinaryFunc<Uint8x16, Add, Uint8x16>), 2)                                    \
  V(addSaturate, (BinaryFunc<Uint8x16, AddSaturate, Uint8x16>), 2)                    \
  V(and, (BinaryFunc<Uint8x16, And, Uint8x16>), 2)                                    \
  V(equal, (CompareFunc<Uint8x16, Equal, Bool8x16>), 2)                               \
  V(extractLane, (ExtractLane<Uint8x16>), 2)                                          \
  V(greaterThan, (CompareFunc<Uint8x16, GreaterThan, Bool8x16>), 2)                   \
  V(greaterThanOrEqual, (CompareFunc<Uint8x16, GreaterThanOrEqual, Bool8x16>), 2)     \
  V(lessThan, (CompareFunc<Uint8x16, LessThan, Bool8x16>), 2)                         \
  V(lessThanOrEqual, (CompareFunc<Uint8x16, LessThanOrEqual, Bool8x16>), 2)           \
  V(load, (Load<Uint8x16, 16>), 2)                                                    \
  V(mul, (BinaryFunc<Uint8x16, Mul, Uint8x16>), 2)                                    \
  V(notEqual, (CompareFunc<Uint8x16, NotEqual, Bool8x16>), 2)                         \
  V(or, (BinaryFunc<Uint8x16, Or, Uint8x16>), 2)                                      \
  V(sub, (BinaryFunc<Uint8x16, Sub, Uint8x16>), 2)                                    \
  V(subSaturate, (BinaryFunc<Uint8x16, SubSaturate, Uint8x16>), 2)                    \
  V(shiftLeftByScalar, (BinaryScalar<Uint8x16, ShiftLeft>), 2)                        \
  V(shiftRightByScalar, (BinaryScalar<Uint8x16, ShiftRightLogical>), 2)               \
  V(shiftRightArithmeticByScalar, (BinaryScalar<Uint8x16, ShiftRightArithmetic>), 2)  \
  V(shiftRightLogicalByScalar, (BinaryScalar<Uint8x16, ShiftRightLogical>), 2)        \
  V(xor, (BinaryFunc<Uint8x16, Xor, Uint8x16>), 2)

#define UINT8X16_TERNARY_FUNCTION_LIST(V)                                             \
  V(replaceLane, (ReplaceLane<Uint8x16>), 3)                                          \
  V(select, (Select<Uint8x16, Bool8x16>), 3)                                          \
  V(store, (Store<Uint8x16, 16>), 3)

#define UINT8X16_SHUFFLE_FUNCTION_LIST(V)                                             \
  V(swizzle, Swizzle<Uint8x16>, 17)                                                   \
  V(shuffle, Shuffle<Uint8x16>, 18)

#define UINT8X16_FUNCTION_LIST(V)                                                     \
  UINT8X16_UNARY_FUNCTION_LIST(V)                                                     \
  UINT8X16_BINARY_FUNCTION_LIST(V)                                                    \
  UINT8X16_TERNARY_FUNCTION_LIST(V)                                                   \
  UINT8X16_SHUFFLE_FUNCTION_LIST(V)

// Int16x8.
#define INT16X8_UNARY_FUNCTION_LIST(V)                                                \
  V(check, (UnaryFunc<Int16x8, Identity, Int16x8>), 1)                                \
  V(fromFloat32x4Bits, (FuncConvertBits<Float32x4, Int16x8>), 1)                      \
  V(fromFloat64x2Bits, (FuncConvertBits<Float64x2, Int16x8>), 1)                      \
  V(fromInt8x16Bits,   (FuncConvertBits<Int8x16,   Int16x8>), 1)                      \
  V(fromInt32x4Bits,   (FuncConvertBits<Int32x4,   Int16x8>), 1)                      \
  V(fromUint8x16Bits,  (FuncConvertBits<Uint8x16,  Int16x8>), 1)                      \
  V(fromUint16x8Bits,  (FuncConvertBits<Uint16x8,  Int16x8>), 1)                      \
  V(fromUint32x4Bits,  (FuncConvertBits<Uint32x4,  Int16x8>), 1)                      \
  V(neg, (UnaryFunc<Int16x8, Neg, Int16x8>), 1)                                       \
  V(not, (UnaryFunc<Int16x8, Not, Int16x8>), 1)                                       \
  V(splat, (FuncSplat<Int16x8>), 1)

#define INT16X8_BINARY_FUNCTION_LIST(V)                                               \
  V(add, (BinaryFunc<Int16x8, Add, Int16x8>), 2)                                      \
  V(addSaturate, (BinaryFunc<Int16x8, AddSaturate, Int16x8>), 2)                      \
  V(and, (BinaryFunc<Int16x8, And, Int16x8>), 2)                                      \
  V(equal, (CompareFunc<Int16x8, Equal, Bool16x8>), 2)                                \
  V(extractLane, (ExtractLane<Int16x8>), 2)                                           \
  V(greaterThan, (CompareFunc<Int16x8, GreaterThan, Bool16x8>), 2)                    \
  V(greaterThanOrEqual, (CompareFunc<Int16x8, GreaterThanOrEqual, Bool16x8>), 2)      \
  V(lessThan, (CompareFunc<Int16x8, LessThan, Bool16x8>), 2)                          \
  V(lessThanOrEqual, (CompareFunc<Int16x8, LessThanOrEqual, Bool16x8>), 2)            \
  V(load, (Load<Int16x8, 8>), 2)                                                      \
  V(mul, (BinaryFunc<Int16x8, Mul, Int16x8>), 2)                                      \
  V(notEqual, (CompareFunc<Int16x8, NotEqual, Bool16x8>), 2)                          \
  V(or, (BinaryFunc<Int16x8, Or, Int16x8>), 2)                                        \
  V(sub, (BinaryFunc<Int16x8, Sub, Int16x8>), 2)                                      \
  V(subSaturate, (BinaryFunc<Int16x8, SubSaturate, Int16x8>), 2)                      \
  V(shiftLeftByScalar, (BinaryScalar<Int16x8, ShiftLeft>), 2)                         \
  V(shiftRightByScalar, (BinaryScalar<Int16x8, ShiftRightArithmetic>), 2)             \
  V(shiftRightArithmeticByScalar, (BinaryScalar<Int16x8, ShiftRightArithmetic>), 2)   \
  V(shiftRightLogicalByScalar, (BinaryScalar<Int16x8, ShiftRightLogical>), 2)         \
  V(xor, (BinaryFunc<Int16x8, Xor, Int16x8>), 2)

#define INT16X8_TERNARY_FUNCTION_LIST(V)                                              \
  V(replaceLane, (ReplaceLane<Int16x8>), 3)                                           \
  V(select, (Select<Int16x8, Bool16x8>), 3)                                           \
  V(store, (Store<Int16x8, 8>), 3)

#define INT16X8_SHUFFLE_FUNCTION_LIST(V)                                              \
  V(swizzle, Swizzle<Int16x8>, 9)                                                     \
  V(shuffle, Shuffle<Int16x8>, 10)

#define INT16X8_FUNCTION_LIST(V)                                                      \
  INT16X8_UNARY_FUNCTION_LIST(V)                                                      \
  INT16X8_BINARY_FUNCTION_LIST(V)                                                     \
  INT16X8_TERNARY_FUNCTION_LIST(V)                                                    \
  INT16X8_SHUFFLE_FUNCTION_LIST(V)

// Uint16x8.
#define UINT16X8_UNARY_FUNCTION_LIST(V)                                               \
  V(check, (UnaryFunc<Uint16x8, Identity, Uint16x8>), 1)                              \
  V(fromFloat32x4Bits, (FuncConvertBits<Float32x4, Uint16x8>), 1)                     \
  V(fromFloat64x2Bits, (FuncConvertBits<Float64x2, Uint16x8>), 1)                     \
  V(fromInt8x16Bits,   (FuncConvertBits<Int8x16,   Uint16x8>), 1)                     \
  V(fromInt16x8Bits,   (FuncConvertBits<Int16x8,   Uint16x8>), 1)                     \
  V(fromInt32x4Bits,   (FuncConvertBits<Int32x4,   Uint16x8>), 1)                     \
  V(fromUint8x16Bits,  (FuncConvertBits<Uint8x16,  Uint16x8>), 1)                     \
  V(fromUint32x4Bits,  (FuncConvertBits<Uint32x4,  Uint16x8>), 1)                     \
  V(neg, (UnaryFunc<Uint16x8, Neg, Uint16x8>), 1)                                     \
  V(not, (UnaryFunc<Uint16x8, Not, Uint16x8>), 1)                                     \
  V(splat, (FuncSplat<Uint16x8>), 1)

#define UINT16X8_BINARY_FUNCTION_LIST(V)                                              \
  V(add, (BinaryFunc<Uint16x8, Add, Uint16x8>), 2)                                    \
  V(addSaturate, (BinaryFunc<Uint16x8, AddSaturate, Uint16x8>), 2)                    \
  V(and, (BinaryFunc<Uint16x8, And, Uint16x8>), 2)                                    \
  V(equal, (CompareFunc<Uint16x8, Equal, Bool16x8>), 2)                               \
  V(extractLane, (ExtractLane<Uint16x8>), 2)                                          \
  V(greaterThan, (CompareFunc<Uint16x8, GreaterThan, Bool16x8>), 2)                   \
  V(greaterThanOrEqual, (CompareFunc<Uint16x8, GreaterThanOrEqual, Bool16x8>), 2)     \
  V(lessThan, (CompareFunc<Uint16x8, LessThan, Bool16x8>), 2)                         \
  V(lessThanOrEqual, (CompareFunc<Uint16x8, LessThanOrEqual, Bool16x8>), 2)           \
  V(load, (Load<Uint16x8, 8>), 2)                                                     \
  V(mul, (BinaryFunc<Uint16x8, Mul, Uint16x8>), 2)                                    \
  V(notEqual, (CompareFunc<Uint16x8, NotEqual, Bool16x8>), 2)                         \
  V(or, (BinaryFunc<Uint16x8, Or, Uint16x8>), 2)                                      \
  V(sub, (BinaryFunc<Uint16x8, Sub, Uint16x8>), 2)                                    \
  V(subSaturate, (BinaryFunc<Uint16x8, SubSaturate, Uint16x8>), 2)                    \
  V(shiftLeftByScalar, (BinaryScalar<Uint16x8, ShiftLeft>), 2)                        \
  V(shiftRightByScalar, (BinaryScalar<Uint16x8, ShiftRightLogical>), 2)               \
  V(shiftRightArithmeticByScalar, (BinaryScalar<Uint16x8, ShiftRightArithmetic>), 2)  \
  V(shiftRightLogicalByScalar, (BinaryScalar<Uint16x8, ShiftRightLogical>), 2)        \
  V(xor, (BinaryFunc<Uint16x8, Xor, Uint16x8>), 2)

#define UINT16X8_TERNARY_FUNCTION_LIST(V)                                             \
  V(replaceLane, (ReplaceLane<Uint16x8>), 3)                                          \
  V(select, (Select<Uint16x8, Bool16x8>), 3)                                          \
  V(store, (Store<Uint16x8, 8>), 3)

#define UINT16X8_SHUFFLE_FUNCTION_LIST(V)                                             \
  V(swizzle, Swizzle<Uint16x8>, 9)                                                    \
  V(shuffle, Shuffle<Uint16x8>, 10)

#define UINT16X8_FUNCTION_LIST(V)                                                     \
  UINT16X8_UNARY_FUNCTION_LIST(V)                                                     \
  UINT16X8_BINARY_FUNCTION_LIST(V)                                                    \
  UINT16X8_TERNARY_FUNCTION_LIST(V)                                                   \
  UINT16X8_SHUFFLE_FUNCTION_LIST(V)

// Int32x4.
#define INT32X4_UNARY_FUNCTION_LIST(V)                                                \
  V(check, (UnaryFunc<Int32x4, Identity, Int32x4>), 1)                                \
  V(fromFloat32x4,     (FuncConvert<Float32x4,     Int32x4>), 1)                      \
  V(fromFloat32x4Bits, (FuncConvertBits<Float32x4, Int32x4>), 1)                      \
  V(fromFloat64x2Bits, (FuncConvertBits<Float64x2, Int32x4>), 1)                      \
  V(fromInt8x16Bits,   (FuncConvertBits<Int8x16,   Int32x4>), 1)                      \
  V(fromInt16x8Bits,   (FuncConvertBits<Int16x8,   Int32x4>), 1)                      \
  V(fromUint8x16Bits,  (FuncConvertBits<Uint8x16,  Int32x4>), 1)                      \
  V(fromUint16x8Bits,  (FuncConvertBits<Uint16x8,  Int32x4>), 1)                      \
  V(fromUint32x4Bits,  (FuncConvertBits<Uint32x4,  Int32x4>), 1)                      \
  V(neg, (UnaryFunc<Int32x4, Neg, Int32x4>), 1)                                       \
  V(not, (UnaryFunc<Int32x4, Not, Int32x4>), 1)                                       \
  V(splat, (FuncSplat<Int32x4>), 0)

#define INT32X4_BINARY_FUNCTION_LIST(V)                                               \
  V(add, (BinaryFunc<Int32x4, Add, Int32x4>), 2)                                      \
  V(and, (BinaryFunc<Int32x4, And, Int32x4>), 2)                                      \
  V(equal, (CompareFunc<Int32x4, Equal, Bool32x4>), 2)                                \
  V(extractLane, (ExtractLane<Int32x4>), 2)                                           \
  V(greaterThan, (CompareFunc<Int32x4, GreaterThan, Bool32x4>), 2)                    \
  V(greaterThanOrEqual, (CompareFunc<Int32x4, GreaterThanOrEqual, Bool32x4>), 2)      \
  V(lessThan, (CompareFunc<Int32x4, LessThan, Bool32x4>), 2)                          \
  V(lessThanOrEqual, (CompareFunc<Int32x4, LessThanOrEqual, Bool32x4>), 2)            \
  V(load,  (Load<Int32x4, 4>), 2)                                                     \
  V(load3, (Load<Int32x4, 3>), 2)                                                     \
  V(load2, (Load<Int32x4, 2>), 2)                                                     \
  V(load1, (Load<Int32x4, 1>), 2)                                                     \
  V(mul, (BinaryFunc<Int32x4, Mul, Int32x4>), 2)                                      \
  V(notEqual, (CompareFunc<Int32x4, NotEqual, Bool32x4>), 2)                          \
  V(or, (BinaryFunc<Int32x4, Or, Int32x4>), 2)                                        \
  V(sub, (BinaryFunc<Int32x4, Sub, Int32x4>), 2)                                      \
  V(shiftLeftByScalar, (BinaryScalar<Int32x4, ShiftLeft>), 2)                         \
  V(shiftRightByScalar, (BinaryScalar<Int32x4, ShiftRightArithmetic>), 2)             \
  V(shiftRightArithmeticByScalar, (BinaryScalar<Int32x4, ShiftRightArithmetic>), 2)   \
  V(shiftRightLogicalByScalar, (BinaryScalar<Int32x4, ShiftRightLogical>), 2)         \
  V(xor, (BinaryFunc<Int32x4, Xor, Int32x4>), 2)

#define INT32X4_TERNARY_FUNCTION_LIST(V)                                              \
  V(replaceLane, (ReplaceLane<Int32x4>), 3)                                           \
  V(select, (Select<Int32x4, Bool32x4>), 3)                                           \
  V(store,  (Store<Int32x4, 4>), 3)                                                   \
  V(store3, (Store<Int32x4, 3>), 3)                                                   \
  V(store2, (Store<Int32x4, 2>), 3)                                                   \
  V(store1, (Store<Int32x4, 1>), 3)

#define INT32X4_SHUFFLE_FUNCTION_LIST(V)                                              \
  V(swizzle, Swizzle<Int32x4>, 5)                                                     \
  V(shuffle, Shuffle<Int32x4>, 6)

#define INT32X4_FUNCTION_LIST(V)                                                      \
  INT32X4_UNARY_FUNCTION_LIST(V)                                                      \
  INT32X4_BINARY_FUNCTION_LIST(V)                                                     \
  INT32X4_TERNARY_FUNCTION_LIST(V)                                                    \
  INT32X4_SHUFFLE_FUNCTION_LIST(V)

// Uint32x4.
#define UINT32X4_UNARY_FUNCTION_LIST(V)                                               \
  V(check, (UnaryFunc<Uint32x4, Identity, Uint32x4>), 1)                              \
  V(fromFloat32x4,     (FuncConvert<Float32x4,     Uint32x4>), 1)                     \
  V(fromFloat32x4Bits, (FuncConvertBits<Float32x4, Uint32x4>), 1)                     \
  V(fromFloat64x2Bits, (FuncConvertBits<Float64x2, Uint32x4>), 1)                     \
  V(fromInt8x16Bits,   (FuncConvertBits<Int8x16,   Uint32x4>), 1)                     \
  V(fromInt16x8Bits,   (FuncConvertBits<Int16x8,   Uint32x4>), 1)                     \
  V(fromInt32x4Bits,   (FuncConvertBits<Int32x4,   Uint32x4>), 1)                     \
  V(fromUint8x16Bits,  (FuncConvertBits<Uint8x16,  Uint32x4>), 1)                     \
  V(fromUint16x8Bits,  (FuncConvertBits<Uint16x8,  Uint32x4>), 1)                     \
  V(neg, (UnaryFunc<Uint32x4, Neg, Uint32x4>), 1)                                     \
  V(not, (UnaryFunc<Uint32x4, Not, Uint32x4>), 1)                                     \
  V(splat, (FuncSplat<Uint32x4>), 0)

#define UINT32X4_BINARY_FUNCTION_LIST(V)                                              \
  V(add, (BinaryFunc<Uint32x4, Add, Uint32x4>), 2)                                    \
  V(and, (BinaryFunc<Uint32x4, And, Uint32x4>), 2)                                    \
  V(equal, (CompareFunc<Uint32x4, Equal, Bool32x4>), 2)                               \
  V(extractLane, (ExtractLane<Uint32x4>), 2)                                          \
  V(greaterThan, (CompareFunc<Uint32x4, GreaterThan, Bool32x4>), 2)                   \
  V(greaterThanOrEqual, (CompareFunc<Uint32x4, GreaterThanOrEqual, Bool32x4>), 2)     \
  V(lessThan, (CompareFunc<Uint32x4, LessThan, Bool32x4>), 2)                         \
  V(lessThanOrEqual, (CompareFunc<Uint32x4, LessThanOrEqual, Bool32x4>), 2)           \
  V(load,  (Load<Uint32x4, 4>), 2)                                                    \
  V(load3, (Load<Uint32x4, 3>), 2)                                                    \
  V(load2, (Load<Uint32x4, 2>), 2)                                                    \
  V(load1, (Load<Uint32x4, 1>), 2)                                                    \
  V(mul, (BinaryFunc<Uint32x4, Mul, Uint32x4>), 2)                                    \
  V(notEqual, (CompareFunc<Uint32x4, NotEqual, Bool32x4>), 2)                         \
  V(or, (BinaryFunc<Uint32x4, Or, Uint32x4>), 2)                                      \
  V(sub, (BinaryFunc<Uint32x4, Sub, Uint32x4>), 2)                                    \
  V(shiftLeftByScalar, (BinaryScalar<Uint32x4, ShiftLeft>), 2)                        \
  V(shiftRightByScalar, (BinaryScalar<Uint32x4, ShiftRightLogical>), 2)               \
  V(shiftRightArithmeticByScalar, (BinaryScalar<Uint32x4, ShiftRightArithmetic>), 2)  \
  V(shiftRightLogicalByScalar, (BinaryScalar<Uint32x4, ShiftRightLogical>), 2)        \
  V(xor, (BinaryFunc<Uint32x4, Xor, Uint32x4>), 2)

#define UINT32X4_TERNARY_FUNCTION_LIST(V)                                             \
  V(replaceLane, (ReplaceLane<Uint32x4>), 3)                                          \
  V(select, (Select<Uint32x4, Bool32x4>), 3)                                          \
  V(store,  (Store<Uint32x4, 4>), 3)                                                  \
  V(store3, (Store<Uint32x4, 3>), 3)                                                  \
  V(store2, (Store<Uint32x4, 2>), 3)                                                  \
  V(store1, (Store<Uint32x4, 1>), 3)

#define UINT32X4_SHUFFLE_FUNCTION_LIST(V)                                             \
  V(swizzle, Swizzle<Uint32x4>, 5)                                                    \
  V(shuffle, Shuffle<Uint32x4>, 6)

#define UINT32X4_FUNCTION_LIST(V)                                                     \
  UINT32X4_UNARY_FUNCTION_LIST(V)                                                     \
  UINT32X4_BINARY_FUNCTION_LIST(V)                                                    \
  UINT32X4_TERNARY_FUNCTION_LIST(V)                                                   \
  UINT32X4_SHUFFLE_FUNCTION_LIST(V)

/*
 * The FOREACH macros below partition all of the SIMD operations into disjoint
 * sets.
 */

// Operations available on all SIMD types. Mixed arity.
#define FOREACH_COMMON_SIMD_OP(_)     \
    _(extractLane)                    \
    _(replaceLane)                    \
    _(check)                          \
    _(splat)

// Lanewise operations available on numeric SIMD types.
// Include lane-wise select here since it is not arithmetic and defined on
// numeric types too.
#define FOREACH_LANE_SIMD_OP(_)       \
    _(select)                         \
    _(swizzle)                        \
    _(shuffle)

// Memory operations available on numeric SIMD types.
#define FOREACH_MEMORY_SIMD_OP(_)     \
    _(load)                           \
    _(store)

// Memory operations available on numeric X4 SIMD types.
#define FOREACH_MEMORY_X4_SIMD_OP(_)  \
    _(load1)                          \
    _(load2)                          \
    _(load3)                          \
    _(store1)                         \
    _(store2)                         \
    _(store3)

// Unary operations on Bool vectors.
#define FOREACH_BOOL_SIMD_UNOP(_)     \
    _(allTrue)                        \
    _(anyTrue)

// Unary bitwise SIMD operators defined on all integer and boolean SIMD types.
#define FOREACH_BITWISE_SIMD_UNOP(_)  \
    _(not)

// Binary bitwise SIMD operators defined on all integer and boolean SIMD types.
#define FOREACH_BITWISE_SIMD_BINOP(_) \
    _(and)                            \
    _(or)                             \
    _(xor)

// Bitwise shifts defined on integer SIMD types.
#define FOREACH_SHIFT_SIMD_OP(_)      \
    _(shiftLeftByScalar)              \
    _(shiftRightByScalar)             \
    _(shiftRightArithmeticByScalar)   \
    _(shiftRightLogicalByScalar)

// Unary arithmetic operators defined on numeric SIMD types.
#define FOREACH_NUMERIC_SIMD_UNOP(_)  \
    _(neg)

// Binary arithmetic operators defined on numeric SIMD types.
#define FOREACH_NUMERIC_SIMD_BINOP(_) \
    _(add)                            \
    _(sub)                            \
    _(mul)

// Unary arithmetic operators defined on floating point SIMD types.
#define FOREACH_FLOAT_SIMD_UNOP(_)    \
    _(abs)                            \
    _(sqrt)                           \
    _(reciprocalApproximation)        \
    _(reciprocalSqrtApproximation)

// Binary arithmetic operators defined on floating point SIMD types.
#define FOREACH_FLOAT_SIMD_BINOP(_)   \
    _(div)                            \
    _(max)                            \
    _(min)                            \
    _(maxNum)                         \
    _(minNum)

// Comparison operators defined on numeric SIMD types.
#define FOREACH_COMP_SIMD_OP(_)       \
    _(lessThan)                       \
    _(lessThanOrEqual)                \
    _(equal)                          \
    _(notEqual)                       \
    _(greaterThan)                    \
    _(greaterThanOrEqual)

/*
 * All SIMD operations, excluding casts.
 */
#define FORALL_SIMD_NONCAST_OP(_)     \
    FOREACH_COMMON_SIMD_OP(_)         \
    FOREACH_LANE_SIMD_OP(_)           \
    FOREACH_MEMORY_SIMD_OP(_)         \
    FOREACH_MEMORY_X4_SIMD_OP(_)      \
    FOREACH_BOOL_SIMD_UNOP(_)         \
    FOREACH_BITWISE_SIMD_UNOP(_)      \
    FOREACH_BITWISE_SIMD_BINOP(_)     \
    FOREACH_SHIFT_SIMD_OP(_)          \
    FOREACH_NUMERIC_SIMD_UNOP(_)      \
    FOREACH_NUMERIC_SIMD_BINOP(_)     \
    FOREACH_FLOAT_SIMD_UNOP(_)        \
    FOREACH_FLOAT_SIMD_BINOP(_)       \
    FOREACH_COMP_SIMD_OP(_)

/*
 * All operations on integer SIMD types, excluding casts and
 * FOREACH_MEMORY_X4_OP.
 */
#define FORALL_INT_SIMD_OP(_)         \
    FOREACH_COMMON_SIMD_OP(_)         \
    FOREACH_LANE_SIMD_OP(_)           \
    FOREACH_MEMORY_SIMD_OP(_)         \
    FOREACH_BITWISE_SIMD_UNOP(_)      \
    FOREACH_BITWISE_SIMD_BINOP(_)     \
    FOREACH_SHIFT_SIMD_OP(_)          \
    FOREACH_NUMERIC_SIMD_UNOP(_)      \
    FOREACH_NUMERIC_SIMD_BINOP(_)     \
    FOREACH_COMP_SIMD_OP(_)

/*
 * All operations on floating point SIMD types, excluding casts and
 * FOREACH_MEMORY_X4_OP.
 */
#define FORALL_FLOAT_SIMD_OP(_)       \
    FOREACH_COMMON_SIMD_OP(_)         \
    FOREACH_LANE_SIMD_OP(_)           \
    FOREACH_MEMORY_SIMD_OP(_)         \
    FOREACH_NUMERIC_SIMD_UNOP(_)      \
    FOREACH_NUMERIC_SIMD_BINOP(_)     \
    FOREACH_FLOAT_SIMD_UNOP(_)        \
    FOREACH_FLOAT_SIMD_BINOP(_)       \
    FOREACH_COMP_SIMD_OP(_)

/*
 * All operations on Bool SIMD types.
 *
 * These types don't have casts, so no need to specialize.
 */
#define FORALL_BOOL_SIMD_OP(_)        \
    FOREACH_COMMON_SIMD_OP(_)         \
    FOREACH_BOOL_SIMD_UNOP(_)         \
    FOREACH_BITWISE_SIMD_UNOP(_)      \
    FOREACH_BITWISE_SIMD_BINOP(_)

/*
 * The sets of cast operations are listed per type below.
 *
 * These sets are not disjoint.
 */

#define FOREACH_INT8X16_SIMD_CAST(_)  \
    _(fromFloat32x4Bits)              \
    _(fromFloat64x2Bits)              \
    _(fromInt16x8Bits)                \
    _(fromInt32x4Bits)

#define FOREACH_INT16X8_SIMD_CAST(_)  \
    _(fromFloat32x4Bits)              \
    _(fromFloat64x2Bits)              \
    _(fromInt8x16Bits)                \
    _(fromInt32x4Bits)

#define FOREACH_INT32X4_SIMD_CAST(_)  \
    _(fromFloat32x4)                  \
    _(fromFloat32x4Bits)              \
    _(fromFloat64x2Bits)              \
    _(fromInt8x16Bits)                \
    _(fromInt16x8Bits)

#define FOREACH_FLOAT32X4_SIMD_CAST(_)\
    _(fromFloat64x2Bits)              \
    _(fromInt8x16Bits)                \
    _(fromInt16x8Bits)                \
    _(fromInt32x4)                    \
    _(fromInt32x4Bits)

#define FOREACH_FLOAT64X2_SIMD_CAST(_)\
    _(fromFloat32x4Bits)              \
    _(fromInt8x16Bits)                \
    _(fromInt16x8Bits)                \
    _(fromInt32x4Bits)

// All operations on Int32x4.
#define FORALL_INT32X4_SIMD_OP(_)     \
    FORALL_INT_SIMD_OP(_)             \
    FOREACH_MEMORY_X4_SIMD_OP(_)      \
    FOREACH_INT32X4_SIMD_CAST(_)

// All operations on Float32X4
#define FORALL_FLOAT32X4_SIMD_OP(_)   \
    FORALL_FLOAT_SIMD_OP(_)           \
    FOREACH_MEMORY_X4_SIMD_OP(_)      \
    FOREACH_FLOAT32X4_SIMD_CAST(_)

/*
 * All SIMD operations assuming only 32x4 types exist.
 * This is used in the current asm.js impl.
 */
#define FORALL_SIMD_ASMJS_OP(_)       \
    FORALL_SIMD_NONCAST_OP(_)         \
    _(fromFloat32x4)                  \
    _(fromFloat32x4Bits)              \
    _(fromInt32x4)                    \
    _(fromInt32x4Bits)

// All operations on Int32x4 in the asm.js world
#define FORALL_INT32X4_ASMJS_OP(_)    \
    FORALL_INT_SIMD_OP(_)             \
    FOREACH_MEMORY_X4_SIMD_OP(_)      \
    _(fromFloat32x4)                  \
    _(fromFloat32x4Bits)

// All operations on Float32X4 in the asm.js world.
#define FORALL_FLOAT32X4_ASMJS_OP(_)  \
    FORALL_FLOAT_SIMD_OP(_)           \
    FOREACH_MEMORY_X4_SIMD_OP(_)      \
    _(fromInt32x4)                    \
    _(fromInt32x4Bits)

// TODO: remove when all SIMD calls are inlined (bug 1112155)
#define ION_COMMONX4_SIMD_OP(_)      \
    FOREACH_NUMERIC_SIMD_BINOP(_)    \
    _(extractLane)                   \
    _(replaceLane)                   \
    _(select)                        \
    _(splat)                         \
    _(neg)                           \
    _(swizzle)                       \
    _(shuffle)                       \
    _(load)                          \
    _(load1)                         \
    _(load2)                         \
    _(load3)                         \
    _(store)                         \
    _(store1)                        \
    _(store2)                        \
    _(store3)                        \
    _(check)

namespace js {

class SIMDObject : public JSObject
{
  public:
    static const Class class_;
    static bool toString(JSContext* cx, unsigned int argc, Value* vp);
    static bool resolve(JSContext* cx, JS::HandleObject obj, JS::HandleId, bool* resolved);
};

// These classes implement the concept containing the following constraints:
// - requires typename Elem: this is the scalar lane type, stored in each lane
// of the SIMD vector.
// - requires static const unsigned lanes: this is the number of lanes (length)
// of the SIMD vector.
// - requires static const SimdTypeDescr::Type type: this is the SimdTypeDescr
// enum value corresponding to the SIMD type.
// - requires static bool Cast(JSContext*, JS::HandleValue, Elem*): casts a
// given Value to the current scalar lane type and saves it in the Elem
// out-param.
// - requires static Value ToValue(Elem): returns a Value of the right type
// containing the given value.
//
// This concept is used in the templates above to define the functions
// associated to a given type and in their implementations, to avoid code
// redundancy.

struct Float32x4 {
    typedef float Elem;
    static const unsigned lanes = 4;
    static const SimdTypeDescr::Type type = SimdTypeDescr::Float32x4;
    static bool Cast(JSContext* cx, JS::HandleValue v, Elem* out) {
        double d;
        if (!ToNumber(cx, v, &d))
            return false;
        *out = float(d);
        return true;
    }
    static Value ToValue(Elem value) {
        return DoubleValue(JS::CanonicalizeNaN(value));
    }
};

struct Float64x2 {
    typedef double Elem;
    static const unsigned lanes = 2;
    static const SimdTypeDescr::Type type = SimdTypeDescr::Float64x2;
    static bool Cast(JSContext* cx, JS::HandleValue v, Elem* out) {
        return ToNumber(cx, v, out);
    }
    static Value ToValue(Elem value) {
        return DoubleValue(JS::CanonicalizeNaN(value));
    }
};

struct Int8x16 {
    typedef int8_t Elem;
    static const unsigned lanes = 16;
    static const SimdTypeDescr::Type type = SimdTypeDescr::Int8x16;
    static bool Cast(JSContext* cx, JS::HandleValue v, Elem* out) {
        return ToInt8(cx, v, out);
    }
    static Value ToValue(Elem value) {
        return NumberValue(value);
    }
};

struct Int16x8 {
    typedef int16_t Elem;
    static const unsigned lanes = 8;
    static const SimdTypeDescr::Type type = SimdTypeDescr::Int16x8;
    static bool Cast(JSContext* cx, JS::HandleValue v, Elem* out) {
        return ToInt16(cx, v, out);
    }
    static Value ToValue(Elem value) {
        return NumberValue(value);
    }
};

struct Int32x4 {
    typedef int32_t Elem;
    static const unsigned lanes = 4;
    static const SimdTypeDescr::Type type = SimdTypeDescr::Int32x4;
    static bool Cast(JSContext* cx, JS::HandleValue v, Elem* out) {
        return ToInt32(cx, v, out);
    }
    static Value ToValue(Elem value) {
        return NumberValue(value);
    }
};

struct Uint8x16 {
    typedef uint8_t Elem;
    static const unsigned lanes = 16;
    static const SimdTypeDescr::Type type = SimdTypeDescr::Uint8x16;
    static bool Cast(JSContext* cx, JS::HandleValue v, Elem* out) {
        return ToUint8(cx, v, out);
    }
    static Value ToValue(Elem value) {
        return NumberValue(value);
    }
};

struct Uint16x8 {
    typedef uint16_t Elem;
    static const unsigned lanes = 8;
    static const SimdTypeDescr::Type type = SimdTypeDescr::Uint16x8;
    static bool Cast(JSContext* cx, JS::HandleValue v, Elem* out) {
        return ToUint16(cx, v, out);
    }
    static Value ToValue(Elem value) {
        return NumberValue(value);
    }
};

struct Uint32x4 {
    typedef uint32_t Elem;
    static const unsigned lanes = 4;
    static const SimdTypeDescr::Type type = SimdTypeDescr::Uint32x4;
    static bool Cast(JSContext* cx, JS::HandleValue v, Elem* out) {
        return ToUint32(cx, v, out);
    }
    static Value ToValue(Elem value) {
        return NumberValue(value);
    }
};

struct Bool8x16 {
    typedef int8_t Elem;
    static const unsigned lanes = 16;
    static const SimdTypeDescr::Type type = SimdTypeDescr::Bool8x16;
    static bool Cast(JSContext* cx, JS::HandleValue v, Elem* out) {
        *out = ToBoolean(v) ? -1 : 0;
        return true;
    }
    static Value ToValue(Elem value) {
        return BooleanValue(value);
    }
};

struct Bool16x8 {
    typedef int16_t Elem;
    static const unsigned lanes = 8;
    static const SimdTypeDescr::Type type = SimdTypeDescr::Bool16x8;
    static bool Cast(JSContext* cx, JS::HandleValue v, Elem* out) {
        *out = ToBoolean(v) ? -1 : 0;
        return true;
    }
    static Value ToValue(Elem value) {
        return BooleanValue(value);
    }
};

struct Bool32x4 {
    typedef int32_t Elem;
    static const unsigned lanes = 4;
    static const SimdTypeDescr::Type type = SimdTypeDescr::Bool32x4;
    static bool Cast(JSContext* cx, JS::HandleValue v, Elem* out) {
        *out = ToBoolean(v) ? -1 : 0;
        return true;
    }
    static Value ToValue(Elem value) {
        return BooleanValue(value);
    }
};

struct Bool64x2 {
    typedef int64_t Elem;
    static const unsigned lanes = 2;
    static const SimdTypeDescr::Type type = SimdTypeDescr::Bool64x2;
    static bool Cast(JSContext* cx, JS::HandleValue v, Elem* out) {
        *out = ToBoolean(v) ? -1 : 0;
        return true;
    }
    static Value ToValue(Elem value) {
        return BooleanValue(value);
    }
};

template<typename V>
JSObject* CreateSimd(JSContext* cx, const typename V::Elem* data);

template<typename V>
bool IsVectorObject(HandleValue v);

template<typename V>
bool ToSimdConstant(JSContext* cx, HandleValue v, jit::SimdConstant* out);

#define DECLARE_SIMD_FLOAT32X4_FUNCTION(Name, Func, Operands)   \
extern bool                                                     \
simd_float32x4_##Name(JSContext* cx, unsigned argc, Value* vp);
FLOAT32X4_FUNCTION_LIST(DECLARE_SIMD_FLOAT32X4_FUNCTION)
#undef DECLARE_SIMD_FLOAT32X4_FUNCTION

#define DECLARE_SIMD_FLOAT64X2_FUNCTION(Name, Func, Operands)   \
extern bool                                                     \
simd_float64x2_##Name(JSContext* cx, unsigned argc, Value* vp);
FLOAT64X2_FUNCTION_LIST(DECLARE_SIMD_FLOAT64X2_FUNCTION)
#undef DECLARE_SIMD_FLOAT64X2_FUNCTION

#define DECLARE_SIMD_INT8X16_FUNCTION(Name, Func, Operands)     \
extern bool                                                     \
simd_int8x16_##Name(JSContext* cx, unsigned argc, Value* vp);
INT8X16_FUNCTION_LIST(DECLARE_SIMD_INT8X16_FUNCTION)
#undef DECLARE_SIMD_INT8X16_FUNCTION

#define DECLARE_SIMD_INT16X8_FUNCTION(Name, Func, Operands)     \
extern bool                                                     \
simd_int16x8_##Name(JSContext* cx, unsigned argc, Value* vp);
INT16X8_FUNCTION_LIST(DECLARE_SIMD_INT16X8_FUNCTION)
#undef DECLARE_SIMD_INT16X8_FUNCTION

#define DECLARE_SIMD_INT32x4_FUNCTION(Name, Func, Operands)     \
extern bool                                                     \
simd_int32x4_##Name(JSContext* cx, unsigned argc, Value* vp);
INT32X4_FUNCTION_LIST(DECLARE_SIMD_INT32x4_FUNCTION)
#undef DECLARE_SIMD_INT32x4_FUNCTION

#define DECLARE_SIMD_UINT8X16_FUNCTION(Name, Func, Operands)    \
extern bool                                                     \
simd_uint8x16_##Name(JSContext* cx, unsigned argc, Value* vp);
UINT8X16_FUNCTION_LIST(DECLARE_SIMD_UINT8X16_FUNCTION)
#undef DECLARE_SIMD_UINT8X16_FUNCTION

#define DECLARE_SIMD_UINT16X8_FUNCTION(Name, Func, Operands)    \
extern bool                                                     \
simd_uint16x8_##Name(JSContext* cx, unsigned argc, Value* vp);
UINT16X8_FUNCTION_LIST(DECLARE_SIMD_UINT16X8_FUNCTION)
#undef DECLARE_SIMD_UINT16X8_FUNCTION

#define DECLARE_SIMD_UINT32x4_FUNCTION(Name, Func, Operands)    \
extern bool                                                     \
simd_uint32x4_##Name(JSContext* cx, unsigned argc, Value* vp);
UINT32X4_FUNCTION_LIST(DECLARE_SIMD_UINT32x4_FUNCTION)
#undef DECLARE_SIMD_UINT32x4_FUNCTION

#define DECLARE_SIMD_BOOL8X16_FUNCTION(Name, Func, Operands)    \
extern bool                                                     \
simd_bool8x16_##Name(JSContext* cx, unsigned argc, Value* vp);
BOOL8X16_FUNCTION_LIST(DECLARE_SIMD_BOOL8X16_FUNCTION)
#undef DECLARE_SIMD_BOOL8X16_FUNCTION

#define DECLARE_SIMD_BOOL16X8_FUNCTION(Name, Func, Operands)    \
extern bool                                                     \
simd_bool16x8_##Name(JSContext* cx, unsigned argc, Value* vp);
BOOL16X8_FUNCTION_LIST(DECLARE_SIMD_BOOL16X8_FUNCTION)
#undef DECLARE_SIMD_BOOL16X8_FUNCTION

#define DECLARE_SIMD_BOOL32X4_FUNCTION(Name, Func, Operands)    \
extern bool                                                     \
simd_bool32x4_##Name(JSContext* cx, unsigned argc, Value* vp);
BOOL32X4_FUNCTION_LIST(DECLARE_SIMD_BOOL32X4_FUNCTION)
#undef DECLARE_SIMD_BOOL32X4_FUNCTION

#define DECLARE_SIMD_BOOL64x2_FUNCTION(Name, Func, Operands)    \
extern bool                                                     \
simd_bool64x2_##Name(JSContext* cx, unsigned argc, Value* vp);
BOOL64X2_FUNCTION_LIST(DECLARE_SIMD_BOOL64x2_FUNCTION)
#undef DECLARE_SIMD_BOOL64x2_FUNCTION

JSObject*
InitSIMDClass(JSContext* cx, HandleObject obj);

}  /* namespace js */

#endif /* builtin_SIMD_h */
