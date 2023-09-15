/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_x86_shared_MacroAssembler_x86_shared_inl_h
#define jit_x86_shared_MacroAssembler_x86_shared_inl_h

#include "jit/x86-shared/MacroAssembler-x86-shared.h"

namespace js {
namespace jit {

//{{{ check_macroassembler_style
// ===============================================================
// Logical instructions

void
MacroAssembler::not32(Register reg)
{
    notl(reg);
}

void
MacroAssembler::and32(Register src, Register dest)
{
    andl(src, dest);
}

void
MacroAssembler::and32(Imm32 imm, Register dest)
{
    andl(imm, dest);
}

void
MacroAssembler::and32(Imm32 imm, const Address& dest)
{
    andl(imm, Operand(dest));
}

void
MacroAssembler::and32(const Address& src, Register dest)
{
    andl(Operand(src), dest);
}

void
MacroAssembler::or32(Register src, Register dest)
{
    orl(src, dest);
}

void
MacroAssembler::or32(Imm32 imm, Register dest)
{
    orl(imm, dest);
}

void
MacroAssembler::or32(Imm32 imm, const Address& dest)
{
    orl(imm, Operand(dest));
}

void
MacroAssembler::xor32(Register src, Register dest)
{
    xorl(src, dest);
}

void
MacroAssembler::xor32(Imm32 imm, Register dest)
{
    xorl(imm, dest);
}

// ===============================================================
// Arithmetic instructions

void
MacroAssembler::add32(Register src, Register dest)
{
    addl(src, dest);
}

void
MacroAssembler::add32(Imm32 imm, Register dest)
{
    addl(imm, dest);
}

void
MacroAssembler::add32(Imm32 imm, const Address& dest)
{
    addl(imm, Operand(dest));
}

void
MacroAssembler::add32(Imm32 imm, const AbsoluteAddress& dest)
{
    addl(imm, Operand(dest));
}

void
MacroAssembler::addFloat32(FloatRegister src, FloatRegister dest)
{
    vaddss(src, dest, dest);
}

void
MacroAssembler::addDouble(FloatRegister src, FloatRegister dest)
{
    vaddsd(src, dest, dest);
}

void
MacroAssembler::sub32(Register src, Register dest)
{
    subl(src, dest);
}

void
MacroAssembler::sub32(Imm32 imm, Register dest)
{
    subl(imm, dest);
}

void
MacroAssembler::sub32(const Address& src, Register dest)
{
    subl(Operand(src), dest);
}

void
MacroAssembler::subDouble(FloatRegister src, FloatRegister dest)
{
    vsubsd(src, dest, dest);
}

void
MacroAssembler::mulDouble(FloatRegister src, FloatRegister dest)
{
    vmulsd(src, dest, dest);
}

void
MacroAssembler::divDouble(FloatRegister src, FloatRegister dest)
{
    vdivsd(src, dest, dest);
}

void
MacroAssembler::neg32(Register reg)
{
    negl(reg);
}

void
MacroAssembler::negateFloat(FloatRegister reg)
{
    ScratchFloat32Scope scratch(*this);
    vpcmpeqw(scratch, scratch, scratch);
    vpsllq(Imm32(31), scratch, scratch);

    // XOR the float in a float register with -0.0.
    vxorps(scratch, reg, reg); // s ^ 0x80000000
}

void
MacroAssembler::negateDouble(FloatRegister reg)
{
    // From MacroAssemblerX86Shared::maybeInlineDouble
    ScratchDoubleScope scratch(*this);
    vpcmpeqw(scratch, scratch, scratch);
    vpsllq(Imm32(63), scratch, scratch);

    // XOR the float in a float register with -0.0.
    vxorpd(scratch, reg, reg); // s ^ 0x80000000000000
}

// ===============================================================
// Branch instructions

void
MacroAssembler::branch32(Condition cond, Register lhs, Register rhs, Label* label)
{
    cmp32(lhs, rhs);
    j(cond, label);
}

template <class L>
void
MacroAssembler::branch32(Condition cond, Register lhs, Imm32 rhs, L label)
{
    cmp32(lhs, rhs);
    j(cond, label);
}

void
MacroAssembler::branch32(Condition cond, const Address& lhs, Register rhs, Label* label)
{
    cmp32(Operand(lhs), rhs);
    j(cond, label);
}

void
MacroAssembler::branch32(Condition cond, const Address& lhs, Imm32 rhs, Label* label)
{
    cmp32(Operand(lhs), rhs);
    j(cond, label);
}

void
MacroAssembler::branch32(Condition cond, const BaseIndex& lhs, Register rhs, Label* label)
{
    cmp32(Operand(lhs), rhs);
    j(cond, label);
}

void
MacroAssembler::branch32(Condition cond, const BaseIndex& lhs, Imm32 rhs, Label* label)
{
    cmp32(Operand(lhs), rhs);
    j(cond, label);
}

void
MacroAssembler::branch32(Condition cond, const Operand& lhs, Register rhs, Label* label)
{
    cmp32(lhs, rhs);
    j(cond, label);
}

void
MacroAssembler::branch32(Condition cond, const Operand& lhs, Imm32 rhs, Label* label)
{
    cmp32(lhs, rhs);
    j(cond, label);
}

void
MacroAssembler::branchPtr(Condition cond, Register lhs, Register rhs, Label* label)
{
    cmpPtr(lhs, rhs);
    j(cond, label);
}

void
MacroAssembler::branchPtr(Condition cond, Register lhs, Imm32 rhs, Label* label)
{
    branchPtrImpl(cond, lhs, rhs, label);
}

void
MacroAssembler::branchPtr(Condition cond, Register lhs, ImmPtr rhs, Label* label)
{
    branchPtrImpl(cond, lhs, rhs, label);
}

void
MacroAssembler::branchPtr(Condition cond, Register lhs, ImmGCPtr rhs, Label* label)
{
    branchPtrImpl(cond, lhs, rhs, label);
}

void
MacroAssembler::branchPtr(Condition cond, Register lhs, ImmWord rhs, Label* label)
{
    branchPtrImpl(cond, lhs, rhs, label);
}

void
MacroAssembler::branchPtr(Condition cond, const Address& lhs, Register rhs, Label* label)
{
    branchPtrImpl(cond, lhs, rhs, label);
}

void
MacroAssembler::branchPtr(Condition cond, const Address& lhs, ImmPtr rhs, Label* label)
{
    branchPtrImpl(cond, lhs, rhs, label);
}

void
MacroAssembler::branchPtr(Condition cond, const Address& lhs, ImmGCPtr rhs, Label* label)
{
    branchPtrImpl(cond, lhs, rhs, label);
}

void
MacroAssembler::branchPtr(Condition cond, const Address& lhs, ImmWord rhs, Label* label)
{
    branchPtrImpl(cond, lhs, rhs, label);
}

void
MacroAssembler::branchFloat(DoubleCondition cond, FloatRegister lhs, FloatRegister rhs,
                            Label* label)
{
    compareFloat(cond, lhs, rhs);

    if (cond == DoubleEqual) {
        Label unordered;
        j(Parity, &unordered);
        j(Equal, label);
        bind(&unordered);
        return;
    }

    if (cond == DoubleNotEqualOrUnordered) {
        j(NotEqual, label);
        j(Parity, label);
        return;
    }

    MOZ_ASSERT(!(cond & DoubleConditionBitSpecial));
    j(ConditionFromDoubleCondition(cond), label);
}

void
MacroAssembler::branchDouble(DoubleCondition cond, FloatRegister lhs, FloatRegister rhs,
                             Label* label)
{
    compareDouble(cond, lhs, rhs);

    if (cond == DoubleEqual) {
        Label unordered;
        j(Parity, &unordered);
        j(Equal, label);
        bind(&unordered);
        return;
    }
    if (cond == DoubleNotEqualOrUnordered) {
        j(NotEqual, label);
        j(Parity, label);
        return;
    }

    MOZ_ASSERT(!(cond & DoubleConditionBitSpecial));
    j(ConditionFromDoubleCondition(cond), label);
}

template <class L>
void
MacroAssembler::branchTest32(Condition cond, Register lhs, Register rhs, L label)
{
    MOZ_ASSERT(cond == Zero || cond == NonZero || cond == Signed || cond == NotSigned);
    test32(lhs, rhs);
    j(cond, label);
}

template <class L>
void
MacroAssembler::branchTest32(Condition cond, Register lhs, Imm32 rhs, L label)
{
    MOZ_ASSERT(cond == Zero || cond == NonZero || cond == Signed || cond == NotSigned);
    test32(lhs, rhs);
    j(cond, label);
}

void
MacroAssembler::branchTest32(Condition cond, const Address& lhs, Imm32 rhs, Label* label)
{
    MOZ_ASSERT(cond == Zero || cond == NonZero || cond == Signed || cond == NotSigned);
    test32(Operand(lhs), rhs);
    j(cond, label);
}

void
MacroAssembler::branchTestPtr(Condition cond, Register lhs, Register rhs, Label* label)
{
    testPtr(lhs, rhs);
    j(cond, label);
}

void
MacroAssembler::branchTestPtr(Condition cond, Register lhs, Imm32 rhs, Label* label)
{
    testPtr(lhs, rhs);
    j(cond, label);
}

void
MacroAssembler::branchTestPtr(Condition cond, const Address& lhs, Imm32 rhs, Label* label)
{
    testPtr(Operand(lhs), rhs);
    j(cond, label);
}

//}}} check_macroassembler_style
// ===============================================================

void
MacroAssemblerX86Shared::clampIntToUint8(Register reg)
{
    Label inRange;
    asMasm().branchTest32(Assembler::Zero, reg, Imm32(0xffffff00), &inRange);
    {
        sarl(Imm32(31), reg);
        notl(reg);
        andl(Imm32(255), reg);
    }
    bind(&inRange);
}

} // namespace jit
} // namespace js

#endif /* jit_x86_shared_MacroAssembler_x86_shared_inl_h */
