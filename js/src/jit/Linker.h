/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef jit_Linker_h
#define jit_Linker_h

#include "jscntxt.h"
#include "jscompartment.h"
#include "jsgc.h"

#include "jit/ExecutableAllocator.h"
#include "jit/IonCode.h"
#include "jit/JitCompartment.h"
#include "jit/MacroAssembler.h"

namespace js {
namespace jit {

class Linker
{
    MacroAssembler& masm;
    mozilla::Maybe<AutoWritableJitCode> awjc;

    JitCode* fail(JSContext* cx) {
        ReportOutOfMemory(cx);
        return nullptr;
    }

  public:
    explicit Linker(MacroAssembler& masm)
      : masm(masm)
    {
        masm.finish();
    }

    template <AllowGC allowGC>
    JitCode* newCode(JSContext* cx, CodeKind kind, bool hasPatchableBackedges = false) {
        MOZ_ASSERT(masm.numAsmJSAbsoluteAddresses() == 0);
        MOZ_ASSERT_IF(hasPatchableBackedges, kind == ION_CODE);

        gc::AutoSuppressGC suppressGC(cx);
        if (masm.oom())
            return fail(cx);

        ExecutablePool* pool;
        size_t bytesNeeded = masm.bytesNeeded() + sizeof(JitCode*) + CodeAlignment;
        if (bytesNeeded >= MAX_BUFFER_SIZE)
            return fail(cx);

        // ExecutableAllocator requires bytesNeeded to be word-size aligned.
        bytesNeeded = AlignBytes(bytesNeeded, sizeof(void*));

        ExecutableAllocator& execAlloc = hasPatchableBackedges
                                       ? cx->runtime()->jitRuntime()->backedgeExecAlloc()
                                       : cx->runtime()->jitRuntime()->execAlloc();
        uint8_t* result = (uint8_t*)execAlloc.alloc(bytesNeeded, &pool, kind);
        if (!result)
            return fail(cx);

        // The JitCode pointer will be stored right before the code buffer.
        uint8_t* codeStart = result + sizeof(JitCode*);

        // Bump the code up to a nice alignment.
        codeStart = (uint8_t*)AlignBytes((uintptr_t)codeStart, CodeAlignment);
        uint32_t headerSize = codeStart - result;
        JitCode* code = JitCode::New<allowGC>(cx, codeStart, bytesNeeded - headerSize,
                                              headerSize, pool, kind);
        if (!code)
            return nullptr;
        if (masm.oom())
            return fail(cx);
        awjc.emplace(result, bytesNeeded);
        code->copyFrom(masm);
        masm.link(code);
        if (masm.embedsNurseryPointers())
            cx->runtime()->gc.storeBuffer.putWholeCell(code);
        return code;
    }
};

} // namespace jit
} // namespace js

#endif /* jit_Linker_h */
