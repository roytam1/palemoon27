/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Internal stuff needed by TypedEnum.h and TypedEnumBits.h. */

// NOTE: When we can assume C++11 enum class support and TypedEnum.h goes away,
// we should then consider folding TypedEnumInternal.h into TypedEnumBits.h.

#ifndef mozilla_TypedEnumInternal_h
#define mozilla_TypedEnumInternal_h

#include "mozilla/Attributes.h"

#if defined(__cplusplus)

#if defined(__clang__)
   /*
    * Per Clang documentation, "Note that marketing version numbers should not
    * be used to check for language features, as different vendors use different
    * numbering schemes. Instead, use the feature checking macros."
    */
#  ifndef __has_extension
#    define __has_extension __has_feature /* compatibility, for older versions of clang */
#  endif
#  if __has_extension(cxx_strong_enums)
#    define MOZ_HAVE_CXX11_ENUM_TYPE
#    define MOZ_HAVE_CXX11_STRONG_ENUMS
#  endif
#elif defined(__GNUC__)
#  if defined(__GXX_EXPERIMENTAL_CXX0X__) || __cplusplus >= 201103L
#    if MOZ_GCC_VERSION_AT_LEAST(4, 6, 3)
#      define MOZ_HAVE_CXX11_ENUM_TYPE
#      define MOZ_HAVE_CXX11_STRONG_ENUMS
#    endif
#  endif
#elif defined(_MSC_VER)
#  if _MSC_VER >= 1400
#    define MOZ_HAVE_CXX11_ENUM_TYPE
#  endif
#  if _MSC_VER >= 1700
#    define MOZ_HAVE_CXX11_STRONG_ENUMS
#  endif
#endif

namespace mozilla {

/*
 * The problem that CastableTypedEnumResult aims to solve is that
 * typed enums are not convertible to bool, and there is no way to make them
 * be, yet user code wants to be able to write
 *
 *   if (myFlags & Flags::SOME_PARTICULAR_FLAG)              (1)
 *
 * There are different approaches to solving this. Most of them require
 * adapting user code. For example, we could implement operator! and have
 * the user write
 *
 *   if (!!(myFlags & Flags::SOME_PARTICULAR_FLAG))          (2)
 *
 * Or we could supply a IsNonZero() or Any() function returning whether
 * an enum value is nonzero, and have the user write
 *
 *   if (Any(Flags & Flags::SOME_PARTICULAR_FLAG))           (3)
 *
 * But instead, we choose to preserve the original user syntax (1) as it
 * is inherently more readable, and to ease porting existing code to typed
 * enums. We achieve this by having operator& and other binary bitwise
 * operators have as return type a class, CastableTypedEnumResult,
 * that wraps a typed enum but adds bool convertibility.
 */
template<typename E>
class CastableTypedEnumResult
{
private:
  const E mValue;

public:
  explicit MOZ_CONSTEXPR CastableTypedEnumResult(E aValue)
    : mValue(aValue)
  {}

  MOZ_CONSTEXPR operator E() const { return mValue; }

  template<typename DestinationType>
  MOZ_EXPLICIT_CONVERSION MOZ_CONSTEXPR
  operator DestinationType() const { return DestinationType(mValue); }

  MOZ_CONSTEXPR bool operator !() const { return !bool(mValue); }

#ifndef MOZ_HAVE_CXX11_STRONG_ENUMS
  // This get() method is used to implement a constructor in the
  // non-c++11 fallback path for MOZ_BEGIN_ENUM_CLASS, taking a
  // CastableTypedEnumResult. If we try to implement it using the
  // above conversion operator E(), then at least clang 3.3
  // (when forced to take the non-c++11 fallback path) compiles
  // this constructor to an infinite recursion. So we introduce this
  // get() method, that does exactly the same as the conversion operator,
  // to work around this.
  MOZ_CONSTEXPR E get() const { return mValue; }
#endif
};

} // namespace mozilla

#endif // __cplusplus

#include "mozilla/IntegerTypeTraits.h"

namespace mozilla {

#define MOZ_CASTABLETYPEDENUMRESULT_BINOP(Op, OtherType, ReturnType) \
template<typename E> \
MOZ_CONSTEXPR ReturnType \
operator Op(const OtherType& aE, const CastableTypedEnumResult<E>& aR) \
{ \
  return ReturnType(aE Op OtherType(aR)); \
} \
template<typename E> \
MOZ_CONSTEXPR ReturnType \
operator Op(const CastableTypedEnumResult<E>& aR, const OtherType& aE) \
{ \
  return ReturnType(OtherType(aR) Op aE); \
} \
template<typename E> \
MOZ_CONSTEXPR ReturnType \
operator Op(const CastableTypedEnumResult<E>& aR1, \
            const CastableTypedEnumResult<E>& aR2) \
{ \
  return ReturnType(OtherType(aR1) Op OtherType(aR2)); \
}

MOZ_CASTABLETYPEDENUMRESULT_BINOP(|, E, CastableTypedEnumResult<E>)
MOZ_CASTABLETYPEDENUMRESULT_BINOP(&, E, CastableTypedEnumResult<E>)
MOZ_CASTABLETYPEDENUMRESULT_BINOP(^, E, CastableTypedEnumResult<E>)
MOZ_CASTABLETYPEDENUMRESULT_BINOP(==, E, bool)
MOZ_CASTABLETYPEDENUMRESULT_BINOP(!=, E, bool)
MOZ_CASTABLETYPEDENUMRESULT_BINOP(||, bool, bool)
MOZ_CASTABLETYPEDENUMRESULT_BINOP(&&, bool, bool)

template <typename E>
MOZ_CONSTEXPR CastableTypedEnumResult<E>
operator ~(const CastableTypedEnumResult<E>& aR)
{
  return CastableTypedEnumResult<E>(~(E(aR)));
}

#define MOZ_CASTABLETYPEDENUMRESULT_COMPOUND_ASSIGN_OP(Op) \
template<typename E> \
E& \
operator Op(E& aR1, \
            const CastableTypedEnumResult<E>& aR2) \
{ \
  return aR1 Op E(aR2); \
}

MOZ_CASTABLETYPEDENUMRESULT_COMPOUND_ASSIGN_OP(&=)
MOZ_CASTABLETYPEDENUMRESULT_COMPOUND_ASSIGN_OP(|=)
MOZ_CASTABLETYPEDENUMRESULT_COMPOUND_ASSIGN_OP(^=)

#undef MOZ_CASTABLETYPEDENUMRESULT_COMPOUND_ASSIGN_OP

#undef MOZ_CASTABLETYPEDENUMRESULT_BINOP

#ifndef MOZ_HAVE_CXX11_STRONG_ENUMS

#define MOZ_CASTABLETYPEDENUMRESULT_BINOP_EXTRA_NON_CXX11(Op, ReturnType) \
template<typename E> \
MOZ_CONSTEXPR ReturnType \
operator Op(typename E::Enum aE, const CastableTypedEnumResult<E>& aR) \
{ \
  return ReturnType(aE Op E(aR)); \
} \
template<typename E> \
MOZ_CONSTEXPR ReturnType \
operator Op(const CastableTypedEnumResult<E>& aR, typename E::Enum aE) \
{ \
  return ReturnType(E(aR) Op aE); \
}

MOZ_CASTABLETYPEDENUMRESULT_BINOP_EXTRA_NON_CXX11(|, CastableTypedEnumResult<E>)
MOZ_CASTABLETYPEDENUMRESULT_BINOP_EXTRA_NON_CXX11(&, CastableTypedEnumResult<E>)
MOZ_CASTABLETYPEDENUMRESULT_BINOP_EXTRA_NON_CXX11(^, CastableTypedEnumResult<E>)
MOZ_CASTABLETYPEDENUMRESULT_BINOP_EXTRA_NON_CXX11(==, bool)
MOZ_CASTABLETYPEDENUMRESULT_BINOP_EXTRA_NON_CXX11(!=, bool)

#undef MOZ_CASTABLETYPEDENUMRESULT_BINOP_EXTRA_NON_CXX11

#endif // not MOZ_HAVE_CXX11_STRONG_ENUMS

namespace detail {
template<typename E>
struct UnsignedIntegerTypeForEnum
  : UnsignedStdintTypeForSize<sizeof(E)>
{};
}

} // namespace mozilla

#define MOZ_MAKE_ENUM_CLASS_BINOP_IMPL(Name, Op) \
   inline MOZ_CONSTEXPR mozilla::CastableTypedEnumResult<Name> \
   operator Op(Name a, Name b) \
   { \
     typedef mozilla::CastableTypedEnumResult<Name> Result; \
     typedef mozilla::detail::UnsignedIntegerTypeForEnum<Name>::Type U; \
     return Result(Name(U(a) Op U(b))); \
   } \
 \
   inline Name& \
   operator Op##=(Name& a, Name b) \
   { \
     return a = a Op b; \
   }

#define MOZ_MAKE_ENUM_CLASS_OPS_IMPL(Name) \
   MOZ_MAKE_ENUM_CLASS_BINOP_IMPL(Name, |) \
   MOZ_MAKE_ENUM_CLASS_BINOP_IMPL(Name, &) \
   MOZ_MAKE_ENUM_CLASS_BINOP_IMPL(Name, ^) \
   inline MOZ_CONSTEXPR mozilla::CastableTypedEnumResult<Name> \
   operator~(Name a) \
   { \
     typedef mozilla::CastableTypedEnumResult<Name> Result; \
     typedef mozilla::detail::UnsignedIntegerTypeForEnum<Name>::Type U; \
     return Result(Name(~(U(a)))); \
   }

#ifndef MOZ_HAVE_CXX11_STRONG_ENUMS
#  define MOZ_MAKE_ENUM_CLASS_BITWISE_BINOP_EXTRA_NON_CXX11(Name, Op) \
     inline MOZ_CONSTEXPR mozilla::CastableTypedEnumResult<Name> \
     operator Op(Name a, Name::Enum b) \
     { \
       return a Op Name(b); \
     } \
 \
     inline MOZ_CONSTEXPR mozilla::CastableTypedEnumResult<Name> \
     operator Op(Name::Enum a, Name b) \
     { \
       return Name(a) Op b; \
     } \
 \
     inline MOZ_CONSTEXPR mozilla::CastableTypedEnumResult<Name> \
     operator Op(Name::Enum a, Name::Enum b) \
     { \
       return Name(a) Op Name(b); \
     } \
 \
     inline Name& \
     operator Op##=(Name& a, Name::Enum b) \
     { \
       return a = a Op Name(b); \
    }

#  define MOZ_MAKE_ENUM_CLASS_OPS_EXTRA_NON_CXX11(Name) \
     MOZ_MAKE_ENUM_CLASS_BITWISE_BINOP_EXTRA_NON_CXX11(Name, |) \
     MOZ_MAKE_ENUM_CLASS_BITWISE_BINOP_EXTRA_NON_CXX11(Name, &) \
     MOZ_MAKE_ENUM_CLASS_BITWISE_BINOP_EXTRA_NON_CXX11(Name, ^) \
     inline MOZ_CONSTEXPR mozilla::CastableTypedEnumResult<Name> \
     operator~(Name::Enum a) \
     { \
       return ~(Name(a)); \
     }
#endif

/**
 * MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS generates standard bitwise operators
 * for the given enum type. Use this to enable using an enum type as bit-field.
 */
#ifdef MOZ_HAVE_CXX11_STRONG_ENUMS
#  define MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(Name) \
     MOZ_MAKE_ENUM_CLASS_OPS_IMPL(Name)
#else
#  define MOZ_MAKE_ENUM_CLASS_BITWISE_OPERATORS(Name) \
     MOZ_MAKE_ENUM_CLASS_OPS_IMPL(Name) \
     MOZ_MAKE_ENUM_CLASS_OPS_EXTRA_NON_CXX11(Name)
#endif

#endif // mozilla_TypedEnumInternal_h
