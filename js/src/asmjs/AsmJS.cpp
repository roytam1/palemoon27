/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 *
 * Copyright 2014 Mozilla Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "asmjs/AsmJS.h"

#include "mozilla/Compression.h"
#include "mozilla/MathAlgorithms.h"

#include "jsmath.h"
#include "jsprf.h"
#include "jsutil.h"

#include "jswrapper.h"

#include "asmjs/WasmGenerator.h"
#include "asmjs/WasmSerialize.h"
#include "builtin/SIMD.h"
#include "frontend/Parser.h"
#include "jit/AtomicOperations.h"
#include "jit/MIR.h"
#include "js/Class.h"
#include "js/MemoryMetrics.h"
#include "vm/StringBuffer.h"
#include "vm/Time.h"
#include "vm/TypedArrayObject.h"

#include "jsobjinlines.h"

#include "frontend/ParseNode-inl.h"
#include "frontend/Parser-inl.h"
#include "vm/ArrayBufferObject-inl.h"

using namespace js;
using namespace js::frontend;
using namespace js::jit;
using namespace js::wasm;

using mozilla::Compression::LZ4;
using mozilla::HashGeneric;
using mozilla::IsNaN;
using mozilla::IsNegativeZero;
using mozilla::MallocSizeOf;
using mozilla::Move;
using mozilla::PodCopy;
using mozilla::PodEqual;
using mozilla::PodZero;
using mozilla::PositiveInfinity;
using JS::AsmJSOption;
using JS::GenericNaN;

/*****************************************************************************/
// asm.js module object

// The asm.js spec recognizes this set of builtin Math functions.
enum AsmJSMathBuiltinFunction
{
    AsmJSMathBuiltin_sin, AsmJSMathBuiltin_cos, AsmJSMathBuiltin_tan,
    AsmJSMathBuiltin_asin, AsmJSMathBuiltin_acos, AsmJSMathBuiltin_atan,
    AsmJSMathBuiltin_ceil, AsmJSMathBuiltin_floor, AsmJSMathBuiltin_exp,
    AsmJSMathBuiltin_log, AsmJSMathBuiltin_pow, AsmJSMathBuiltin_sqrt,
    AsmJSMathBuiltin_abs, AsmJSMathBuiltin_atan2, AsmJSMathBuiltin_imul,
    AsmJSMathBuiltin_fround, AsmJSMathBuiltin_min, AsmJSMathBuiltin_max,
    AsmJSMathBuiltin_clz32
};

// The asm.js spec will recognize this set of builtin Atomics functions.
enum AsmJSAtomicsBuiltinFunction
{
    AsmJSAtomicsBuiltin_compareExchange,
    AsmJSAtomicsBuiltin_exchange,
    AsmJSAtomicsBuiltin_load,
    AsmJSAtomicsBuiltin_store,
    AsmJSAtomicsBuiltin_fence,
    AsmJSAtomicsBuiltin_add,
    AsmJSAtomicsBuiltin_sub,
    AsmJSAtomicsBuiltin_and,
    AsmJSAtomicsBuiltin_or,
    AsmJSAtomicsBuiltin_xor,
    AsmJSAtomicsBuiltin_isLockFree
};

// Set of known global object SIMD's attributes, i.e. types
enum AsmJSSimdType
{
    AsmJSSimdType_int32x4,
    AsmJSSimdType_float32x4,
    AsmJSSimdType_bool32x4
};

static inline bool
IsSignedIntSimdType(AsmJSSimdType type)
{
    switch (type) {
      case AsmJSSimdType_int32x4:
        return true;
      case AsmJSSimdType_float32x4:
      case AsmJSSimdType_bool32x4:
        return false;
    }
    MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("Unknown SIMD type");
}

// Set of known operations, for a given SIMD type (int32x4, float32x4,...)
enum AsmJSSimdOperation
{
#define ASMJSSIMDOPERATION(op) AsmJSSimdOperation_##op,
    FORALL_SIMD_ASMJS_OP(ASMJSSIMDOPERATION)
#undef ASMJSSIMDOPERATION
};

// An AsmJSModule extends (via containment) a wasm::Module with the extra
// persistent state necessary to represent a compiled asm.js module.
class js::AsmJSModule
{
  public:
    class Global
    {
      public:
        enum Which { Variable, FFI, ArrayView, ArrayViewCtor, MathBuiltinFunction,
                     AtomicsBuiltinFunction, Constant, SimdCtor, SimdOperation, ByteLength };
        enum VarInitKind { InitConstant, InitImport };
        enum ConstantKind { GlobalConstant, MathConstant };

      private:
        struct CacheablePod {
            Which which_;
            union {
                struct {
                    uint32_t globalDataOffset_;
                    VarInitKind initKind_;
                    union {
                        wasm::ValType importType_;
                        wasm::Val val_;
                    } u;
                } var;
                uint32_t ffiIndex_;
                Scalar::Type viewType_;
                AsmJSMathBuiltinFunction mathBuiltinFunc_;
                AsmJSAtomicsBuiltinFunction atomicsBuiltinFunc_;
                AsmJSSimdType simdCtorType_;
                struct {
                    AsmJSSimdType type_;
                    AsmJSSimdOperation which_;
                } simdOp;
                struct {
                    ConstantKind kind_;
                    double value_;
                } constant;
            } u;
        } pod;
        PropertyName* name_;

        friend class AsmJSModule;

        Global(Which which, PropertyName* name) {
            mozilla::PodZero(&pod);  // zero padding for Valgrind
            pod.which_ = which;
            name_ = name;
            MOZ_ASSERT_IF(name_, name_->isTenured());
        }

        void trace(JSTracer* trc) {
            if (name_)
                TraceManuallyBarrieredEdge(trc, &name_, "asm.js global name");
        }

      public:
        Global() {}
        Which which() const {
            return pod.which_;
        }
        uint32_t varGlobalDataOffset() const {
            MOZ_ASSERT(pod.which_ == Variable);
            return pod.u.var.globalDataOffset_;
        }
        VarInitKind varInitKind() const {
            MOZ_ASSERT(pod.which_ == Variable);
            return pod.u.var.initKind_;
        }
        wasm::Val varInitVal() const {
            MOZ_ASSERT(pod.which_ == Variable);
            MOZ_ASSERT(pod.u.var.initKind_ == InitConstant);
            return pod.u.var.u.val_;
        }
        wasm::ValType varInitImportType() const {
            MOZ_ASSERT(pod.which_ == Variable);
            MOZ_ASSERT(pod.u.var.initKind_ == InitImport);
            return pod.u.var.u.importType_;
        }
        PropertyName* varImportField() const {
            MOZ_ASSERT(pod.which_ == Variable);
            MOZ_ASSERT(pod.u.var.initKind_ == InitImport);
            return name_;
        }
        PropertyName* ffiField() const {
            MOZ_ASSERT(pod.which_ == FFI);
            return name_;
        }
        uint32_t ffiIndex() const {
            MOZ_ASSERT(pod.which_ == FFI);
            return pod.u.ffiIndex_;
        }
        // When a view is created from an imported constructor:
        //   var I32 = stdlib.Int32Array;
        //   var i32 = new I32(buffer);
        // the second import has nothing to validate and thus has a null field.
        PropertyName* maybeViewName() const {
            MOZ_ASSERT(pod.which_ == ArrayView || pod.which_ == ArrayViewCtor);
            return name_;
        }
        Scalar::Type viewType() const {
            MOZ_ASSERT(pod.which_ == ArrayView || pod.which_ == ArrayViewCtor);
            return pod.u.viewType_;
        }
        PropertyName* mathName() const {
            MOZ_ASSERT(pod.which_ == MathBuiltinFunction);
            return name_;
        }
        PropertyName* atomicsName() const {
            MOZ_ASSERT(pod.which_ == AtomicsBuiltinFunction);
            return name_;
        }
        AsmJSMathBuiltinFunction mathBuiltinFunction() const {
            MOZ_ASSERT(pod.which_ == MathBuiltinFunction);
            return pod.u.mathBuiltinFunc_;
        }
        AsmJSAtomicsBuiltinFunction atomicsBuiltinFunction() const {
            MOZ_ASSERT(pod.which_ == AtomicsBuiltinFunction);
            return pod.u.atomicsBuiltinFunc_;
        }
        AsmJSSimdType simdCtorType() const {
            MOZ_ASSERT(pod.which_ == SimdCtor);
            return pod.u.simdCtorType_;
        }
        PropertyName* simdCtorName() const {
            MOZ_ASSERT(pod.which_ == SimdCtor);
            return name_;
        }
        PropertyName* simdOperationName() const {
            MOZ_ASSERT(pod.which_ == SimdOperation);
            return name_;
        }
        AsmJSSimdOperation simdOperation() const {
            MOZ_ASSERT(pod.which_ == SimdOperation);
            return pod.u.simdOp.which_;
        }
        AsmJSSimdType simdOperationType() const {
            MOZ_ASSERT(pod.which_ == SimdOperation);
            return pod.u.simdOp.type_;
        }
        PropertyName* constantName() const {
            MOZ_ASSERT(pod.which_ == Constant);
            return name_;
        }
        ConstantKind constantKind() const {
            MOZ_ASSERT(pod.which_ == Constant);
            return pod.u.constant.kind_;
        }
        double constantValue() const {
            MOZ_ASSERT(pod.which_ == Constant);
            return pod.u.constant.value_;
        }

        WASM_DECLARE_SERIALIZABLE(Global);
    };

    typedef Vector<Global, 0, SystemAllocPolicy> GlobalVector;

    // An import is slightly different than an asm.js FFI function: a single
    // asm.js FFI function can be called with many different signatures. When
    // compiled to wasm, each unique FFI function paired with signature
    // generates a wasm import.
    class Import
    {
        uint32_t ffiIndex_;
      public:
        Import() = default;
        explicit Import(uint32_t ffiIndex) : ffiIndex_(ffiIndex) {}
        uint32_t ffiIndex() const { return ffiIndex_; }
    };

    typedef Vector<Import, 0, SystemAllocPolicy> ImportVector;

    class Export
    {
        PropertyName* name_;
        PropertyName* maybeFieldName_;
        struct CacheablePod {
            uint32_t wasmIndex_;
            uint32_t startOffsetInModule_;  // Store module-start-relative offsets
            uint32_t endOffsetInModule_;    // so preserved by serialization.
        } pod;

      public:
        Export() {}
        Export(PropertyName* name, PropertyName* maybeFieldName, uint32_t wasmIndex,
               uint32_t startOffsetInModule, uint32_t endOffsetInModule)
          : name_(name),
            maybeFieldName_(maybeFieldName)
        {
            MOZ_ASSERT(name_->isTenured());
            MOZ_ASSERT_IF(maybeFieldName_, maybeFieldName_->isTenured());
            pod.wasmIndex_ = wasmIndex;
            pod.startOffsetInModule_ = startOffsetInModule;
            pod.endOffsetInModule_ = endOffsetInModule;
        }

        void trace(JSTracer* trc) {
            TraceManuallyBarrieredEdge(trc, &name_, "asm.js export name");
            if (maybeFieldName_)
                TraceManuallyBarrieredEdge(trc, &maybeFieldName_, "asm.js export field");
        }

        PropertyName* name() const {
            return name_;
        }
        PropertyName* maybeFieldName() const {
            return maybeFieldName_;
        }
        uint32_t startOffsetInModule() const {
            return pod.startOffsetInModule_;
        }
        uint32_t endOffsetInModule() const {
            return pod.endOffsetInModule_;
        }
        static const uint32_t ChangeHeap = UINT32_MAX;
        bool isChangeHeap() const {
            return pod.wasmIndex_ == ChangeHeap;
        }
        uint32_t wasmIndex() const {
            MOZ_ASSERT(!isChangeHeap());
            return pod.wasmIndex_;
        }

        WASM_DECLARE_SERIALIZABLE(Export)
    };

    typedef Vector<Export, 0, SystemAllocPolicy> ExportVector;

    typedef JS::UniquePtr<wasm::Module, JS::DeletePolicy<wasm::Module>> UniqueWasmModule;

  private:
    UniqueWasmModule            wasmModule_;
    wasm::UniqueStaticLinkData  linkData_;
    struct CacheablePod {
        uint32_t                minHeapLength_;
        uint32_t                maxHeapLength_;
        uint32_t                heapLengthMask_;
        uint32_t                numFFIs_;
        uint32_t                srcLength_;
        uint32_t                srcLengthWithRightBrace_;
        bool                    strict_;
        bool                    hasArrayView_;
        bool                    isSharedView_;
        bool                    hasFixedMinHeapLength_;
    } pod;
    const ScriptSourceHolder    scriptSource_;
    const uint32_t              srcStart_;
    const uint32_t              srcBodyStart_;
    GlobalVector                globals_;
    ImportVector                imports_;
    ExportVector                exports_;
    PropertyName*               globalArgumentName_;
    PropertyName*               importArgumentName_;
    PropertyName*               bufferArgumentName_;

  public:
    explicit AsmJSModule(ScriptSource* scriptSource, uint32_t srcStart, uint32_t srcBodyStart,
                         bool strict)
      : scriptSource_(scriptSource),
        srcStart_(srcStart),
        srcBodyStart_(srcBodyStart),
        globalArgumentName_(nullptr),
        importArgumentName_(nullptr),
        bufferArgumentName_(nullptr)
    {
        mozilla::PodZero(&pod);
        pod.minHeapLength_ = RoundUpToNextValidAsmJSHeapLength(0);
        pod.maxHeapLength_ = 0x80000000;
        pod.strict_ = strict;

        MOZ_ASSERT(srcStart_ <= srcBodyStart_);

        // AsmJSCheckedImmediateRange should be defined to be at most the minimum
        // heap length so that offsets can be folded into bounds checks.
        MOZ_ASSERT(pod.minHeapLength_ - jit::AsmJSCheckedImmediateRange <= pod.minHeapLength_);
    }

    void trace(JSTracer* trc) {
        if (wasmModule_)
            wasmModule_->trace(trc);
        for (Global& global : globals_)
            global.trace(trc);
        for (Export& exp : exports_)
            exp.trace(trc);
        if (globalArgumentName_)
            TraceManuallyBarrieredEdge(trc, &globalArgumentName_, "asm.js global argument name");
        if (importArgumentName_)
            TraceManuallyBarrieredEdge(trc, &importArgumentName_, "asm.js import argument name");
        if (bufferArgumentName_)
            TraceManuallyBarrieredEdge(trc, &bufferArgumentName_, "asm.js buffer argument name");
    }

    /*************************************************************************/
    // These functions may be used as soon as the module is constructed:

    ScriptSource* scriptSource() const {
        return scriptSource_.get();
    }
    bool strict() const {
        return pod.strict_;
    }

    // srcStart() refers to the offset in the ScriptSource to the beginning of
    // the asm.js module function. If the function has been created with the
    // Function constructor, this will be the first character in the function
    // source. Otherwise, it will be the opening parenthesis of the arguments
    // list.
    uint32_t srcStart() const {
        return srcStart_;
    }

    // srcBodyStart() refers to the offset in the ScriptSource to the end
    // of the 'use asm' string-literal token.
    uint32_t srcBodyStart() const {
        return srcBodyStart_;
    }

    // While these functions may be accessed at any time, their values will
    // change as the module is compiled.
    uint32_t minHeapLength() const {
        return pod.minHeapLength_;
    }
    uint32_t maxHeapLength() const {
        return pod.maxHeapLength_;
    }
    uint32_t heapLengthMask() const {
        MOZ_ASSERT(pod.hasFixedMinHeapLength_);
        return pod.heapLengthMask_;
    }

    void initGlobalArgumentName(PropertyName* n) {
        MOZ_ASSERT(!isFinished());
        MOZ_ASSERT_IF(n, n->isTenured());
        globalArgumentName_ = n;
    }
    void initImportArgumentName(PropertyName* n) {
        MOZ_ASSERT(!isFinished());
        MOZ_ASSERT_IF(n, n->isTenured());
        importArgumentName_ = n;
    }
    void initBufferArgumentName(PropertyName* n) {
        MOZ_ASSERT(!isFinished());
        MOZ_ASSERT_IF(n, n->isTenured());
        bufferArgumentName_ = n;
    }
    PropertyName* globalArgumentName() const {
        return globalArgumentName_;
    }
    PropertyName* importArgumentName() const {
        return importArgumentName_;
    }
    PropertyName* bufferArgumentName() const {
        return bufferArgumentName_;
    }

    bool addGlobalVarInit(const wasm::Val& v, uint32_t globalDataOffset) {
        MOZ_ASSERT(!isFinished());
        Global g(Global::Variable, nullptr);
        g.pod.u.var.initKind_ = Global::InitConstant;
        g.pod.u.var.u.val_ = v;
        g.pod.u.var.globalDataOffset_ = globalDataOffset;
        return globals_.append(g);
    }
    bool addGlobalVarImport(PropertyName* name, wasm::ValType importType, uint32_t globalDataOffset) {
        MOZ_ASSERT(!isFinished());
        Global g(Global::Variable, name);
        g.pod.u.var.initKind_ = Global::InitImport;
        g.pod.u.var.u.importType_ = importType;
        g.pod.u.var.globalDataOffset_ = globalDataOffset;
        return globals_.append(g);
    }
    // See Import comment above for FFI vs. Import.
    bool addFFI(PropertyName* field, uint32_t* ffiIndex) {
        MOZ_ASSERT(!isFinished());
        if (pod.numFFIs_ == UINT32_MAX)
            return false;
        Global g(Global::FFI, field);
        g.pod.u.ffiIndex_ = *ffiIndex = pod.numFFIs_++;
        return globals_.append(g);
    }
    bool addArrayView(Scalar::Type vt, PropertyName* maybeField) {
        MOZ_ASSERT(!isFinished());
        pod.hasArrayView_ = true;
        pod.isSharedView_ = false;
        Global g(Global::ArrayView, maybeField);
        g.pod.u.viewType_ = vt;
        return globals_.append(g);
    }
    bool addArrayViewCtor(Scalar::Type vt, PropertyName* field) {
        MOZ_ASSERT(!isFinished());
        MOZ_ASSERT(field);
        pod.isSharedView_ = false;
        Global g(Global::ArrayViewCtor, field);
        g.pod.u.viewType_ = vt;
        return globals_.append(g);
    }
    bool addByteLength() {
        MOZ_ASSERT(!isFinished());
        Global g(Global::ByteLength, nullptr);
        return globals_.append(g);
    }
    bool addMathBuiltinFunction(AsmJSMathBuiltinFunction func, PropertyName* field) {
        MOZ_ASSERT(!isFinished());
        Global g(Global::MathBuiltinFunction, field);
        g.pod.u.mathBuiltinFunc_ = func;
        return globals_.append(g);
    }
    bool addMathBuiltinConstant(double value, PropertyName* field) {
        MOZ_ASSERT(!isFinished());
        Global g(Global::Constant, field);
        g.pod.u.constant.value_ = value;
        g.pod.u.constant.kind_ = Global::MathConstant;
        return globals_.append(g);
    }
    bool addAtomicsBuiltinFunction(AsmJSAtomicsBuiltinFunction func, PropertyName* field) {
        MOZ_ASSERT(!isFinished());
        Global g(Global::AtomicsBuiltinFunction, field);
        g.pod.u.atomicsBuiltinFunc_ = func;
        return globals_.append(g);
    }
    bool addSimdCtor(AsmJSSimdType type, PropertyName* field) {
        MOZ_ASSERT(!isFinished());
        Global g(Global::SimdCtor, field);
        g.pod.u.simdCtorType_ = type;
        return globals_.append(g);
    }
    bool addSimdOperation(AsmJSSimdType type, AsmJSSimdOperation op, PropertyName* field) {
        MOZ_ASSERT(!isFinished());
        Global g(Global::SimdOperation, field);
        g.pod.u.simdOp.type_ = type;
        g.pod.u.simdOp.which_ = op;
        return globals_.append(g);
    }
    bool addGlobalConstant(double value, PropertyName* name) {
        MOZ_ASSERT(!isFinished());
        Global g(Global::Constant, name);
        g.pod.u.constant.value_ = value;
        g.pod.u.constant.kind_ = Global::GlobalConstant;
        return globals_.append(g);
    }
    // See Import comment above for FFI vs. Import.
    bool addImport(uint32_t ffiIndex, uint32_t importIndex) {
        MOZ_ASSERT(imports_.length() == importIndex);
        return imports_.emplaceBack(ffiIndex);
    }
    bool addExport(PropertyName* name, PropertyName* maybeFieldName, uint32_t wasmIndex,
                   uint32_t funcSrcBegin, uint32_t funcSrcEnd)
    {
        // NB: funcSrcBegin/funcSrcEnd are given relative to the ScriptSource
        // (the entire file) and ExportedFunctions store offsets relative to
        // the beginning of the module (so that they are caching-invariant).
        MOZ_ASSERT(!isFinished());
        MOZ_ASSERT(srcStart_ < funcSrcBegin);
        MOZ_ASSERT(funcSrcBegin < funcSrcEnd);
        return exports_.emplaceBack(name, maybeFieldName, wasmIndex,
                                    funcSrcBegin - srcStart_, funcSrcEnd - srcStart_);
    }
    bool addChangeHeap(uint32_t mask, uint32_t min, uint32_t max) {
        MOZ_ASSERT(!isFinished());
        MOZ_ASSERT(!pod.hasFixedMinHeapLength_);
        MOZ_ASSERT(IsValidAsmJSHeapLength(mask + 1));
        MOZ_ASSERT(min >= RoundUpToNextValidAsmJSHeapLength(0));
        MOZ_ASSERT(max <= pod.maxHeapLength_);
        MOZ_ASSERT(min <= max);
        pod.heapLengthMask_ = mask;
        pod.minHeapLength_ = min;
        pod.maxHeapLength_ = max;
        pod.hasFixedMinHeapLength_ = true;
        return true;
    }

    const GlobalVector& globals() const {
        return globals_;
    }
    const ImportVector& imports() const {
        return imports_;
    }
    const ExportVector& exports() const {
        return exports_;
    }

    void setViewsAreShared() {
        if (pod.hasArrayView_)
            pod.isSharedView_ = true;
    }
    bool hasArrayView() const {
        return pod.hasArrayView_;
    }
    bool isSharedView() const {
        return pod.isSharedView_;
    }
    bool tryRequireHeapLengthToBeAtLeast(uint32_t len) {
        MOZ_ASSERT(!isFinished());
        if (pod.hasFixedMinHeapLength_ && len > pod.minHeapLength_)
            return false;
        if (len > pod.maxHeapLength_)
            return false;
        len = RoundUpToNextValidAsmJSHeapLength(len);
        if (len > pod.minHeapLength_)
            pod.minHeapLength_ = len;
        return true;
    }

    /*************************************************************************/
    // A module isFinished() when compilation completes. After being finished,
    // a module must be statically and dynamically linked before execution.

    bool isFinished() const {
        return !!wasmModule_;
    }
    void finish(wasm::Module* wasmModule, wasm::UniqueStaticLinkData linkData,
                uint32_t endBeforeCurly, uint32_t endAfterCurly)
    {
        MOZ_ASSERT(!isFinished());

        wasmModule_.reset(wasmModule);
        linkData_ = Move(linkData);

        MOZ_ASSERT(endBeforeCurly >= srcBodyStart_);
        MOZ_ASSERT(endAfterCurly >= srcBodyStart_);
        pod.srcLength_ = endBeforeCurly - srcStart_;
        pod.srcLengthWithRightBrace_ = endAfterCurly - srcStart_;

        MOZ_ASSERT(isFinished());
    }

    /*************************************************************************/
    // These accessor functions can only be used after finish():

    wasm::Module& wasmModule() const {
        MOZ_ASSERT(isFinished());
        return *wasmModule_;
    }
    uint32_t numFFIs() const {
        MOZ_ASSERT(isFinished());
        return pod.numFFIs_;
    }
    uint32_t srcEndBeforeCurly() const {
        MOZ_ASSERT(isFinished());
        return srcStart_ + pod.srcLength_;
    }
    uint32_t srcEndAfterCurly() const {
        MOZ_ASSERT(isFinished());
        return srcStart_ + pod.srcLengthWithRightBrace_;
    }
    bool staticallyLink(ExclusiveContext* cx) {
        return wasmModule_->staticallyLink(cx, *linkData_);
    }

    // See WASM_DECLARE_SERIALIZABLE.
    size_t serializedSize() const;
    uint8_t* serialize(uint8_t* cursor) const;
    const uint8_t* deserialize(ExclusiveContext* cx, const uint8_t* cursor);
    bool clone(JSContext* cx, HandleAsmJSModule moduleObj) const;
    void addSizeOfMisc(mozilla::MallocSizeOf mallocSizeOf, size_t* code, size_t* data);
};

static void
AsmJSModuleObject_finalize(FreeOp* fop, JSObject* obj)
{
    AsmJSModuleObject& moduleObj = obj->as<AsmJSModuleObject>();
    if (moduleObj.hasModule())
        fop->delete_(&moduleObj.module());
}

static void
AsmJSModuleObject_trace(JSTracer* trc, JSObject* obj)
{
    AsmJSModuleObject& moduleObj = obj->as<AsmJSModuleObject>();
    if (moduleObj.hasModule())
        moduleObj.module().trace(trc);
}

const Class AsmJSModuleObject::class_ = {
    "AsmJSModuleObject",
    JSCLASS_IS_ANONYMOUS | JSCLASS_DELAY_METADATA_CALLBACK |
    JSCLASS_HAS_RESERVED_SLOTS(AsmJSModuleObject::RESERVED_SLOTS),
    nullptr, /* addProperty */
    nullptr, /* delProperty */
    nullptr, /* getProperty */
    nullptr, /* setProperty */
    nullptr, /* enumerate */
    nullptr, /* resolve */
    nullptr, /* mayResolve */
    AsmJSModuleObject_finalize,
    nullptr, /* call */
    nullptr, /* hasInstance */
    nullptr, /* construct */
    AsmJSModuleObject_trace
};

static AsmJSModuleObject*
NewAsmJSModuleObject(ExclusiveContext* cx)
{
    AutoSetNewObjectMetadata metadata(cx);
    JSObject* obj = NewObjectWithGivenProto(cx, &AsmJSModuleObject::class_, nullptr);
    if (!obj)
        return nullptr;

    return &obj->as<AsmJSModuleObject>();
}

bool
AsmJSModuleObject::hasModule() const
{
    MOZ_ASSERT(is<AsmJSModuleObject>());
    return !getReservedSlot(MODULE_SLOT).isUndefined();
}

void
AsmJSModuleObject::setModule(AsmJSModule* newModule)
{
    MOZ_ASSERT(is<AsmJSModuleObject>());
    if (hasModule())
        js_delete(&module());
    setReservedSlot(MODULE_SLOT, PrivateValue(newModule));
}

AsmJSModule&
AsmJSModuleObject::module() const
{
    MOZ_ASSERT(is<AsmJSModuleObject>());
    return *(AsmJSModule*)getReservedSlot(MODULE_SLOT).toPrivate();
}

void
AsmJSModuleObject::addSizeOfMisc(MallocSizeOf mallocSizeOf, size_t* code, size_t* data)
{
    module().addSizeOfMisc(mallocSizeOf, code, data);
}

/*****************************************************************************/
// ParseNode utilities

static inline ParseNode*
NextNode(ParseNode* pn)
{
    return pn->pn_next;
}

static inline ParseNode*
UnaryKid(ParseNode* pn)
{
    MOZ_ASSERT(pn->isArity(PN_UNARY));
    return pn->pn_kid;
}

static inline ParseNode*
BinaryRight(ParseNode* pn)
{
    MOZ_ASSERT(pn->isArity(PN_BINARY));
    return pn->pn_right;
}

static inline ParseNode*
BinaryLeft(ParseNode* pn)
{
    MOZ_ASSERT(pn->isArity(PN_BINARY));
    return pn->pn_left;
}

static inline ParseNode*
ReturnExpr(ParseNode* pn)
{
    MOZ_ASSERT(pn->isKind(PNK_RETURN));
    return UnaryKid(pn);
}

static inline ParseNode*
TernaryKid1(ParseNode* pn)
{
    MOZ_ASSERT(pn->isArity(PN_TERNARY));
    return pn->pn_kid1;
}

static inline ParseNode*
TernaryKid2(ParseNode* pn)
{
    MOZ_ASSERT(pn->isArity(PN_TERNARY));
    return pn->pn_kid2;
}

static inline ParseNode*
TernaryKid3(ParseNode* pn)
{
    MOZ_ASSERT(pn->isArity(PN_TERNARY));
    return pn->pn_kid3;
}

static inline ParseNode*
ListHead(ParseNode* pn)
{
    MOZ_ASSERT(pn->isArity(PN_LIST));
    return pn->pn_head;
}

static inline unsigned
ListLength(ParseNode* pn)
{
    MOZ_ASSERT(pn->isArity(PN_LIST));
    return pn->pn_count;
}

static inline ParseNode*
CallCallee(ParseNode* pn)
{
    MOZ_ASSERT(pn->isKind(PNK_CALL));
    return ListHead(pn);
}

static inline unsigned
CallArgListLength(ParseNode* pn)
{
    MOZ_ASSERT(pn->isKind(PNK_CALL));
    MOZ_ASSERT(ListLength(pn) >= 1);
    return ListLength(pn) - 1;
}

static inline ParseNode*
CallArgList(ParseNode* pn)
{
    MOZ_ASSERT(pn->isKind(PNK_CALL));
    return NextNode(ListHead(pn));
}

static inline ParseNode*
VarListHead(ParseNode* pn)
{
    MOZ_ASSERT(pn->isKind(PNK_VAR) || pn->isKind(PNK_CONST));
    return ListHead(pn);
}

static inline bool
IsDefaultCase(ParseNode* pn)
{
    return pn->as<CaseClause>().isDefault();
}

static inline ParseNode*
CaseExpr(ParseNode* pn)
{
    return pn->as<CaseClause>().caseExpression();
}

static inline ParseNode*
CaseBody(ParseNode* pn)
{
    return pn->as<CaseClause>().statementList();
}

static inline ParseNode*
BinaryOpLeft(ParseNode* pn)
{
    MOZ_ASSERT(pn->isBinaryOperation());
    MOZ_ASSERT(pn->isArity(PN_LIST));
    MOZ_ASSERT(pn->pn_count == 2);
    return ListHead(pn);
}

static inline ParseNode*
BinaryOpRight(ParseNode* pn)
{
    MOZ_ASSERT(pn->isBinaryOperation());
    MOZ_ASSERT(pn->isArity(PN_LIST));
    MOZ_ASSERT(pn->pn_count == 2);
    return NextNode(ListHead(pn));
}

static inline ParseNode*
BitwiseLeft(ParseNode* pn)
{
    return BinaryOpLeft(pn);
}

static inline ParseNode*
BitwiseRight(ParseNode* pn)
{
    return BinaryOpRight(pn);
}

static inline ParseNode*
MultiplyLeft(ParseNode* pn)
{
    MOZ_ASSERT(pn->isKind(PNK_STAR));
    return BinaryOpLeft(pn);
}

static inline ParseNode*
MultiplyRight(ParseNode* pn)
{
    MOZ_ASSERT(pn->isKind(PNK_STAR));
    return BinaryOpRight(pn);
}

static inline ParseNode*
AddSubLeft(ParseNode* pn)
{
    MOZ_ASSERT(pn->isKind(PNK_ADD) || pn->isKind(PNK_SUB));
    return BinaryOpLeft(pn);
}

static inline ParseNode*
AddSubRight(ParseNode* pn)
{
    MOZ_ASSERT(pn->isKind(PNK_ADD) || pn->isKind(PNK_SUB));
    return BinaryOpRight(pn);
}

static inline ParseNode*
DivOrModLeft(ParseNode* pn)
{
    MOZ_ASSERT(pn->isKind(PNK_DIV) || pn->isKind(PNK_MOD));
    return BinaryOpLeft(pn);
}

static inline ParseNode*
DivOrModRight(ParseNode* pn)
{
    MOZ_ASSERT(pn->isKind(PNK_DIV) || pn->isKind(PNK_MOD));
    return BinaryOpRight(pn);
}

static inline ParseNode*
ComparisonLeft(ParseNode* pn)
{
    return BinaryOpLeft(pn);
}

static inline ParseNode*
ComparisonRight(ParseNode* pn)
{
    return BinaryOpRight(pn);
}

static inline ParseNode*
AndOrLeft(ParseNode* pn)
{
    return BinaryOpLeft(pn);
}

static inline ParseNode*
AndOrRight(ParseNode* pn)
{
    return BinaryOpRight(pn);
}

static inline ParseNode*
RelationalLeft(ParseNode* pn)
{
    return BinaryOpLeft(pn);
}

static inline ParseNode*
RelationalRight(ParseNode* pn)
{
    return BinaryOpRight(pn);
}

static inline bool
IsExpressionStatement(ParseNode* pn)
{
    return pn->isKind(PNK_SEMI);
}

static inline ParseNode*
ExpressionStatementExpr(ParseNode* pn)
{
    MOZ_ASSERT(pn->isKind(PNK_SEMI));
    return UnaryKid(pn);
}

static inline PropertyName*
LoopControlMaybeLabel(ParseNode* pn)
{
    MOZ_ASSERT(pn->isKind(PNK_BREAK) || pn->isKind(PNK_CONTINUE));
    MOZ_ASSERT(pn->isArity(PN_NULLARY));
    return pn->as<LoopControlStatement>().label();
}

static inline PropertyName*
LabeledStatementLabel(ParseNode* pn)
{
    return pn->as<LabeledStatement>().label();
}

static inline ParseNode*
LabeledStatementStatement(ParseNode* pn)
{
    return pn->as<LabeledStatement>().statement();
}

static double
NumberNodeValue(ParseNode* pn)
{
    MOZ_ASSERT(pn->isKind(PNK_NUMBER));
    return pn->pn_dval;
}

static bool
NumberNodeHasFrac(ParseNode* pn)
{
    MOZ_ASSERT(pn->isKind(PNK_NUMBER));
    return pn->pn_u.number.decimalPoint == HasDecimal;
}

static ParseNode*
DotBase(ParseNode* pn)
{
    MOZ_ASSERT(pn->isKind(PNK_DOT));
    MOZ_ASSERT(pn->isArity(PN_NAME));
    return pn->expr();
}

static PropertyName*
DotMember(ParseNode* pn)
{
    MOZ_ASSERT(pn->isKind(PNK_DOT));
    MOZ_ASSERT(pn->isArity(PN_NAME));
    return pn->pn_atom->asPropertyName();
}

static ParseNode*
ElemBase(ParseNode* pn)
{
    MOZ_ASSERT(pn->isKind(PNK_ELEM));
    return BinaryLeft(pn);
}

static ParseNode*
ElemIndex(ParseNode* pn)
{
    MOZ_ASSERT(pn->isKind(PNK_ELEM));
    return BinaryRight(pn);
}

static inline JSFunction*
FunctionObject(ParseNode* fn)
{
    MOZ_ASSERT(fn->isKind(PNK_FUNCTION));
    MOZ_ASSERT(fn->isArity(PN_CODE));
    return fn->pn_funbox->function();
}

static inline PropertyName*
FunctionName(ParseNode* fn)
{
    if (JSAtom* atom = FunctionObject(fn)->atom())
        return atom->asPropertyName();
    return nullptr;
}

static inline ParseNode*
FunctionStatementList(ParseNode* fn)
{
    MOZ_ASSERT(fn->pn_body->isKind(PNK_ARGSBODY));
    ParseNode* last = fn->pn_body->last();
    MOZ_ASSERT(last->isKind(PNK_STATEMENTLIST));
    return last;
}

static inline bool
IsNormalObjectField(ExclusiveContext* cx, ParseNode* pn)
{
    return pn->isKind(PNK_COLON) &&
           pn->getOp() == JSOP_INITPROP &&
           BinaryLeft(pn)->isKind(PNK_OBJECT_PROPERTY_NAME);
}

static inline PropertyName*
ObjectNormalFieldName(ExclusiveContext* cx, ParseNode* pn)
{
    MOZ_ASSERT(IsNormalObjectField(cx, pn));
    MOZ_ASSERT(BinaryLeft(pn)->isKind(PNK_OBJECT_PROPERTY_NAME));
    return BinaryLeft(pn)->pn_atom->asPropertyName();
}

static inline ParseNode*
ObjectNormalFieldInitializer(ExclusiveContext* cx, ParseNode* pn)
{
    MOZ_ASSERT(IsNormalObjectField(cx, pn));
    return BinaryRight(pn);
}

static inline bool
IsDefinition(ParseNode* pn)
{
    return pn->isKind(PNK_NAME) && pn->isDefn();
}

static inline ParseNode*
MaybeDefinitionInitializer(ParseNode* pn)
{
    MOZ_ASSERT(IsDefinition(pn));
    return pn->expr();
}

static inline bool
IsUseOfName(ParseNode* pn, PropertyName* name)
{
    return pn->isKind(PNK_NAME) && pn->name() == name;
}

static inline bool
IsIgnoredDirectiveName(ExclusiveContext* cx, JSAtom* atom)
{
    return atom != cx->names().useStrict;
}

static inline bool
IsIgnoredDirective(ExclusiveContext* cx, ParseNode* pn)
{
    return pn->isKind(PNK_SEMI) &&
           UnaryKid(pn) &&
           UnaryKid(pn)->isKind(PNK_STRING) &&
           IsIgnoredDirectiveName(cx, UnaryKid(pn)->pn_atom);
}

static inline bool
IsEmptyStatement(ParseNode* pn)
{
    return pn->isKind(PNK_SEMI) && !UnaryKid(pn);
}

static inline ParseNode*
SkipEmptyStatements(ParseNode* pn)
{
    while (pn && IsEmptyStatement(pn))
        pn = pn->pn_next;
    return pn;
}

static inline ParseNode*
NextNonEmptyStatement(ParseNode* pn)
{
    return SkipEmptyStatements(pn->pn_next);
}

static bool
GetToken(AsmJSParser& parser, TokenKind* tkp)
{
    TokenStream& ts = parser.tokenStream;
    TokenKind tk;
    while (true) {
        if (!ts.getToken(&tk, TokenStream::Operand))
            return false;
        if (tk != TOK_SEMI)
            break;
    }
    *tkp = tk;
    return true;
}

static bool
PeekToken(AsmJSParser& parser, TokenKind* tkp)
{
    TokenStream& ts = parser.tokenStream;
    TokenKind tk;
    while (true) {
        if (!ts.peekToken(&tk, TokenStream::Operand))
            return false;
        if (tk != TOK_SEMI)
            break;
        ts.consumeKnownToken(TOK_SEMI, TokenStream::Operand);
    }
    *tkp = tk;
    return true;
}

static bool
ParseVarOrConstStatement(AsmJSParser& parser, ParseNode** var)
{
    TokenKind tk;
    if (!PeekToken(parser, &tk))
        return false;
    if (tk != TOK_VAR && tk != TOK_CONST) {
        *var = nullptr;
        return true;
    }

    *var = parser.statement(YieldIsName);
    if (!*var)
        return false;

    MOZ_ASSERT((*var)->isKind(PNK_VAR) || (*var)->isKind(PNK_CONST));
    return true;
}

/*****************************************************************************/

// Represents the type and value of an asm.js numeric literal.
//
// A literal is a double iff the literal contains a decimal point (even if the
// fractional part is 0). Otherwise, integers may be classified:
//  fixnum: [0, 2^31)
//  negative int: [-2^31, 0)
//  big unsigned: [2^31, 2^32)
//  out of range: otherwise
// Lastly, a literal may be a float literal which is any double or integer
// literal coerced with Math.fround.
class NumLit
{
  public:
    enum Which {
        Fixnum,
        NegativeInt,
        BigUnsigned,
        Double,
        Float,
        Int32x4,
        Float32x4,
        Bool32x4,
        OutOfRangeInt = -1
    };

  private:
    Which which_;
    union {
        Value scalar_;
        SimdConstant simd_;
    } u;

  public:
    NumLit() = default;

    NumLit(Which w, Value v) : which_(w) {
        u.scalar_ = v;
        MOZ_ASSERT(!isSimd());
    }

    NumLit(Which w, SimdConstant c) : which_(w) {
        u.simd_ = c;
        MOZ_ASSERT(isSimd());
    }

    Which which() const {
        return which_;
    }

    int32_t toInt32() const {
        MOZ_ASSERT(which_ == Fixnum || which_ == NegativeInt || which_ == BigUnsigned);
        return u.scalar_.toInt32();
    }

    uint32_t toUint32() const {
        return (uint32_t)toInt32();
    }

    double toDouble() const {
        MOZ_ASSERT(which_ == Double);
        return u.scalar_.toDouble();
    }

    float toFloat() const {
        MOZ_ASSERT(which_ == Float);
        return float(u.scalar_.toDouble());
    }

    Value scalarValue() const {
        MOZ_ASSERT(which_ != OutOfRangeInt);
        return u.scalar_;
    }

    bool isSimd() const {
        return which_ == Int32x4 || which_ == Float32x4 || which_ == Bool32x4;
    }

    const jit::SimdConstant& simdValue() const {
        MOZ_ASSERT(isSimd());
        return u.simd_;
    }

    bool valid() const {
        return which_ != OutOfRangeInt;
    }

    ValType type() const {
        switch (which_) {
          case NumLit::Fixnum:
          case NumLit::NegativeInt:
          case NumLit::BigUnsigned:
            return ValType::I32;
          case NumLit::Double:
            return ValType::F64;
          case NumLit::Float:
            return ValType::F32;
          case NumLit::Int32x4:
            return ValType::I32x4;
          case NumLit::Float32x4:
            return ValType::F32x4;
          case NumLit::Bool32x4:
            return ValType::B32x4;
          case NumLit::OutOfRangeInt:;
        }
        MOZ_CRASH("bad literal");
    }

    Val value() const {
        switch (which_) {
          case NumLit::Fixnum:
          case NumLit::NegativeInt:
          case NumLit::BigUnsigned:
            return Val(toUint32());
          case NumLit::Float:
            return Val(toFloat());
          case NumLit::Double:
            return Val(toDouble());
          case NumLit::Int32x4:
            return Val(simdValue().asInt32x4());
          case NumLit::Float32x4:
            return Val(simdValue().asFloat32x4());
          case NumLit::Bool32x4:
            return Val(simdValue().asInt32x4(), ValType::B32x4);
          case NumLit::OutOfRangeInt:;
        }
        MOZ_CRASH("bad literal");
    }
};

// Respresents the type of a general asm.js expression.
class Type
{
  public:
    enum Which {
        Fixnum = NumLit::Fixnum,
        Signed = NumLit::NegativeInt,
        Unsigned = NumLit::BigUnsigned,
        DoubleLit = NumLit::Double,
        Float = NumLit::Float,
        Int32x4 = NumLit::Int32x4,
        Float32x4 = NumLit::Float32x4,
        Bool32x4 = NumLit::Bool32x4,
        Double,
        MaybeDouble,
        MaybeFloat,
        Floatish,
        Int,
        Intish,
        Void
    };

  private:
    Which which_;

  public:
    Type() = default;
    MOZ_IMPLICIT Type(Which w) : which_(w) {}
    MOZ_IMPLICIT Type(AsmJSSimdType type) {
        switch (type) {
          case AsmJSSimdType_int32x4:
            which_ = Int32x4;
            return;
          case AsmJSSimdType_float32x4:
            which_ = Float32x4;
            return;
          case AsmJSSimdType_bool32x4:
            which_ = Bool32x4;
            return;
        }
        MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("bad AsmJSSimdType");
    }

    static Type var(ValType t) {
        switch (t) {
          case ValType::I32:   return Int;
          case ValType::I64:   MOZ_CRASH("no int64 in asm.js");
          case ValType::F32:   return Float;
          case ValType::F64:   return Double;
          case ValType::I32x4: return Int32x4;
          case ValType::F32x4: return Float32x4;
          case ValType::B32x4: return Bool32x4;
        }
        MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("bad type");
    }

    static Type ret(ExprType t) {
        switch (t) {
          case ExprType::Void:   return Type::Void;
          case ExprType::I32:    return Signed;
          case ExprType::I64:    MOZ_CRASH("no int64 in asm.js");
          case ExprType::F32:    return Float;
          case ExprType::F64:    return Double;
          case ExprType::I32x4:  return Int32x4;
          case ExprType::F32x4:  return Float32x4;
          case ExprType::B32x4:  return Bool32x4;
        }
        MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("bad type");
    }

    static Type lit(const NumLit& lit) {
        MOZ_ASSERT(lit.valid());
        Which which = Type::Which(lit.which());
        MOZ_ASSERT(which >= Fixnum && which <= Bool32x4);
        Type t;
        t.which_ = which;
        return t;
    }

    Which which() const { return which_; }

    bool operator==(Type rhs) const { return which_ == rhs.which_; }
    bool operator!=(Type rhs) const { return which_ != rhs.which_; }

    bool operator<=(Type rhs) const {
        switch (rhs.which_) {
          case Signed:      return isSigned();
          case Unsigned:    return isUnsigned();
          case DoubleLit:   return isDoubleLit();
          case Double:      return isDouble();
          case Float:       return isFloat();
          case Int32x4:     return isInt32x4();
          case Float32x4:   return isFloat32x4();
          case Bool32x4:    return isBool32x4();
          case MaybeDouble: return isMaybeDouble();
          case MaybeFloat:  return isMaybeFloat();
          case Floatish:    return isFloatish();
          case Int:         return isInt();
          case Intish:      return isIntish();
          case Fixnum:      return isFixnum();
          case Void:        return isVoid();
        }
        MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("unexpected rhs type");
    }

    bool operator<=(ValType rhs) const {
        switch (rhs) {
          case ValType::I32:    return isInt();
          case ValType::I64:    MOZ_CRASH("no int64 in asm.js");
          case ValType::F32:    return isFloat();
          case ValType::F64:    return isDouble();
          case ValType::I32x4:  return isInt32x4();
          case ValType::F32x4:  return isFloat32x4();
          case ValType::B32x4:  return isBool32x4();
        }
        MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("Unexpected rhs type");
    }

    bool isFixnum() const {
        return which_ == Fixnum;
    }

    bool isSigned() const {
        return which_ == Signed || which_ == Fixnum;
    }

    bool isUnsigned() const {
        return which_ == Unsigned || which_ == Fixnum;
    }

    bool isInt() const {
        return isSigned() || isUnsigned() || which_ == Int;
    }

    bool isIntish() const {
        return isInt() || which_ == Intish;
    }

    bool isDoubleLit() const {
        return which_ == DoubleLit;
    }

    bool isDouble() const {
        return isDoubleLit() || which_ == Double;
    }

    bool isMaybeDouble() const {
        return isDouble() || which_ == MaybeDouble;
    }

    bool isFloat() const {
        return which_ == Float;
    }

    bool isMaybeFloat() const {
        return isFloat() || which_ == MaybeFloat;
    }

    bool isFloatish() const {
        return isMaybeFloat() || which_ == Floatish;
    }

    bool isVoid() const {
        return which_ == Void;
    }

    bool isExtern() const {
        return isDouble() || isSigned();
    }

    bool isInt32x4() const {
        return which_ == Int32x4;
    }

    bool isFloat32x4() const {
        return which_ == Float32x4;
    }

    bool isBool32x4() const {
        return which_ == Bool32x4;
    }

    bool isSimd() const {
        return isInt32x4() || isFloat32x4() || isBool32x4();
    }

    bool isVarType() const {
        return isInt() || isFloat() || isDouble() || isSimd();
    }

    ValType checkedValueType() const {
        MOZ_ASSERT(isVarType());
        if (isInt())
            return ValType::I32;
        else if (isFloat())
            return ValType::F32;
        else if (isDouble())
            return ValType::F64;
        else if (isInt32x4())
            return ValType::I32x4;
        return ValType::F32x4;
    }

    MIRType toMIRType() const {
        switch (which_) {
          case Double:
          case DoubleLit:
          case MaybeDouble:
            return MIRType_Double;
          case Float:
          case Floatish:
          case MaybeFloat:
            return MIRType_Float32;
          case Fixnum:
          case Int:
          case Signed:
          case Unsigned:
          case Intish:
            return MIRType_Int32;
          case Int32x4:
            return MIRType_Int32x4;
          case Float32x4:
            return MIRType_Float32x4;
          case Bool32x4:
            return MIRType_Bool32x4;
          case Void:
            return MIRType_None;
        }
        MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("Invalid Type");
    }

    AsmJSSimdType simdType() const {
        MOZ_ASSERT(isSimd());
        switch (which_) {
          case Int32x4:
            return AsmJSSimdType_int32x4;
          case Float32x4:
            return AsmJSSimdType_float32x4;
          case Bool32x4:
            return AsmJSSimdType_bool32x4;
          // Scalar types
          case Double:
          case DoubleLit:
          case MaybeDouble:
          case Float:
          case MaybeFloat:
          case Floatish:
          case Fixnum:
          case Int:
          case Signed:
          case Unsigned:
          case Intish:
          case Void:
            break;
        }
        MOZ_CRASH("not a SIMD Type");
    }

    const char* toChars() const {
        switch (which_) {
          case Double:      return "double";
          case DoubleLit:   return "doublelit";
          case MaybeDouble: return "double?";
          case Float:       return "float";
          case Floatish:    return "floatish";
          case MaybeFloat:  return "float?";
          case Fixnum:      return "fixnum";
          case Int:         return "int";
          case Signed:      return "signed";
          case Unsigned:    return "unsigned";
          case Intish:      return "intish";
          case Int32x4:     return "int32x4";
          case Float32x4:   return "float32x4";
          case Bool32x4:    return "bool32x4";
          case Void:        return "void";
        }
        MOZ_CRASH("Invalid Type");
    }
};

static const unsigned VALIDATION_LIFO_DEFAULT_CHUNK_SIZE = 4 * 1024;

namespace {

// The ModuleValidator encapsulates the entire validation of an asm.js module.
// Its lifetime goes from the validation of the top components of an asm.js
// module (all the globals), the emission of bytecode for all the functions in
// the module and the validation of function's pointer tables. It also finishes
// the compilation of all the module's stubs.
//
// Rooting note: ModuleValidator is a stack class that contains unrooted
// PropertyName (JSAtom) pointers.  This is safe because it cannot be
// constructed without a TokenStream reference.  TokenStream is itself a stack
// class that cannot be constructed without an AutoKeepAtoms being live on the
// stack, which prevents collection of atoms.
//
// ModuleValidator is marked as rooted in the rooting analysis.  Don't add
// non-JSAtom pointers, or this will break!
class MOZ_STACK_CLASS ModuleValidator
{
  public:
    class Func
    {
        const LifoSig& sig_;
        PropertyName* name_;
        uint32_t firstUse_;
        uint32_t index_;
        uint32_t srcBegin_;
        uint32_t srcEnd_;
        bool defined_;

      public:
        Func(PropertyName* name, uint32_t firstUse, const LifoSig& sig, uint32_t index)
          : sig_(sig), name_(name), firstUse_(firstUse), index_(index),
            srcBegin_(0), srcEnd_(0), defined_(false)
        {}

        PropertyName* name() const { return name_; }
        uint32_t firstUse() const { return firstUse_; }
        bool defined() const { return defined_; }
        uint32_t index() const { return index_; }

        void define(ParseNode* fn) {
            MOZ_ASSERT(!defined_);
            defined_ = true;
            srcBegin_ = fn->pn_pos.begin;
            srcEnd_ = fn->pn_pos.end;
        }

        uint32_t srcBegin() const { MOZ_ASSERT(defined_); return srcBegin_; }
        uint32_t srcEnd() const { MOZ_ASSERT(defined_); return srcEnd_; }
        const LifoSig& sig() const { return sig_; }
    };

    typedef Vector<const Func*> ConstFuncVector;
    typedef Vector<Func*> FuncVector;

    class FuncPtrTable
    {
        const LifoSig& sig_;
        PropertyName* name_;
        uint32_t firstUse_;
        uint32_t mask_;
        bool defined_;

        FuncPtrTable(FuncPtrTable&& rhs) = delete;

      public:
        FuncPtrTable(ExclusiveContext* cx, PropertyName* name, uint32_t firstUse,
                     const LifoSig& sig, uint32_t mask)
          : sig_(sig), name_(name), firstUse_(firstUse), mask_(mask), defined_(false)
        {}

        const LifoSig& sig() const { return sig_; }
        PropertyName* name() const { return name_; }
        uint32_t firstUse() const { return firstUse_; }
        unsigned mask() const { return mask_; }
        bool defined() const { return defined_; }
        void define() { MOZ_ASSERT(!defined_); defined_ = true; }
    };

    typedef Vector<FuncPtrTable*> FuncPtrTableVector;

    class Global
    {
      public:
        enum Which {
            Variable,
            ConstantLiteral,
            ConstantImport,
            Function,
            FuncPtrTable,
            FFI,
            ArrayView,
            ArrayViewCtor,
            MathBuiltinFunction,
            AtomicsBuiltinFunction,
            SimdCtor,
            SimdOperation,
            ByteLength,
            ChangeHeap
        };

      private:
        Which which_;
        union {
            struct {
                Type::Which type_;
                uint32_t globalDataOffset_;
                NumLit literalValue_;
            } varOrConst;
            uint32_t funcIndex_;
            uint32_t funcPtrTableIndex_;
            uint32_t ffiIndex_;
            struct {
                Scalar::Type viewType_;
            } viewInfo;
            AsmJSMathBuiltinFunction mathBuiltinFunc_;
            AsmJSAtomicsBuiltinFunction atomicsBuiltinFunc_;
            AsmJSSimdType simdCtorType_;
            struct {
                AsmJSSimdType type_;
                AsmJSSimdOperation which_;
            } simdOp;
            struct {
                uint32_t srcBegin_;
                uint32_t srcEnd_;
            } changeHeap;
        } u;

        friend class ModuleValidator;
        friend class js::LifoAlloc;

        explicit Global(Which which) : which_(which) {}

      public:
        Which which() const {
            return which_;
        }
        Type varOrConstType() const {
            MOZ_ASSERT(which_ == Variable || which_ == ConstantLiteral || which_ == ConstantImport);
            return u.varOrConst.type_;
        }
        uint32_t varOrConstGlobalDataOffset() const {
            MOZ_ASSERT(which_ == Variable || which_ == ConstantImport);
            return u.varOrConst.globalDataOffset_;
        }
        bool isConst() const {
            return which_ == ConstantLiteral || which_ == ConstantImport;
        }
        NumLit constLiteralValue() const {
            MOZ_ASSERT(which_ == ConstantLiteral);
            return u.varOrConst.literalValue_;
        }
        uint32_t funcIndex() const {
            MOZ_ASSERT(which_ == Function);
            return u.funcIndex_;
        }
        uint32_t funcPtrTableIndex() const {
            MOZ_ASSERT(which_ == FuncPtrTable);
            return u.funcPtrTableIndex_;
        }
        unsigned ffiIndex() const {
            MOZ_ASSERT(which_ == FFI);
            return u.ffiIndex_;
        }
        bool isAnyArrayView() const {
            return which_ == ArrayView || which_ == ArrayViewCtor;
        }
        Scalar::Type viewType() const {
            MOZ_ASSERT(isAnyArrayView());
            return u.viewInfo.viewType_;
        }
        bool isMathFunction() const {
            return which_ == MathBuiltinFunction;
        }
        AsmJSMathBuiltinFunction mathBuiltinFunction() const {
            MOZ_ASSERT(which_ == MathBuiltinFunction);
            return u.mathBuiltinFunc_;
        }
        bool isAtomicsFunction() const {
            return which_ == AtomicsBuiltinFunction;
        }
        AsmJSAtomicsBuiltinFunction atomicsBuiltinFunction() const {
            MOZ_ASSERT(which_ == AtomicsBuiltinFunction);
            return u.atomicsBuiltinFunc_;
        }
        bool isSimdCtor() const {
            return which_ == SimdCtor;
        }
        AsmJSSimdType simdCtorType() const {
            MOZ_ASSERT(which_ == SimdCtor);
            return u.simdCtorType_;
        }
        bool isSimdOperation() const {
            return which_ == SimdOperation;
        }
        AsmJSSimdOperation simdOperation() const {
            MOZ_ASSERT(which_ == SimdOperation);
            return u.simdOp.which_;
        }
        AsmJSSimdType simdOperationType() const {
            MOZ_ASSERT(which_ == SimdOperation);
            return u.simdOp.type_;
        }
        uint32_t changeHeapSrcBegin() const {
            MOZ_ASSERT(which_ == ChangeHeap);
            return u.changeHeap.srcBegin_;
        }
        uint32_t changeHeapSrcEnd() const {
            MOZ_ASSERT(which_ == ChangeHeap);
            return u.changeHeap.srcEnd_;
        }
    };

    struct MathBuiltin
    {
        enum Kind { Function, Constant };
        Kind kind;

        union {
            double cst;
            AsmJSMathBuiltinFunction func;
        } u;

        MathBuiltin() : kind(Kind(-1)) {}
        explicit MathBuiltin(double cst) : kind(Constant) {
            u.cst = cst;
        }
        explicit MathBuiltin(AsmJSMathBuiltinFunction func) : kind(Function) {
            u.func = func;
        }
    };

    struct ArrayView
    {
        ArrayView(PropertyName* name, Scalar::Type type)
          : name(name), type(type)
        {}

        PropertyName* name;
        Scalar::Type type;
    };

    class ImportDescriptor
    {
        PropertyName* name_;
        const LifoSig* sig_;

      public:
        ImportDescriptor(PropertyName* name, const LifoSig& sig)
          : name_(name), sig_(&sig)
        {}

        PropertyName* name() const {
            return name_;
        }
        const LifoSig& sig() const {
            return *sig_;
        }

        struct Lookup {  // implements HashPolicy
            PropertyName* name_;
            const MallocSig& sig_;
            Lookup(PropertyName* name, const MallocSig& sig) : name_(name), sig_(sig) {}
        };
        static HashNumber hash(const Lookup& l) {
            return HashGeneric(l.name_, l.sig_.hash());
        }
        static bool match(const ImportDescriptor& lhs, const Lookup& rhs) {
            return lhs.name_ == rhs.name_ && *lhs.sig_ == rhs.sig_;
        }
    };

  private:
    typedef HashMap<PropertyName*, Global*> GlobalMap;
    typedef HashMap<PropertyName*, MathBuiltin> MathNameMap;
    typedef HashMap<PropertyName*, AsmJSAtomicsBuiltinFunction> AtomicsNameMap;
    typedef HashMap<PropertyName*, AsmJSSimdOperation> SimdOperationNameMap;
    typedef Vector<ArrayView> ArrayViewVector;

  public:
    typedef HashMap<ImportDescriptor, unsigned, ImportDescriptor> ImportMap;

  private:
    ExclusiveContext*    cx_;
    AsmJSParser&         parser_;

    ModuleGenerator      mg_;
    AsmJSModule*         module_;

    LifoAlloc            validationLifo_;
    FuncVector           functions_;
    FuncPtrTableVector   funcPtrTables_;
    GlobalMap            globals_;
    ArrayViewVector      arrayViews_;
    ImportMap            imports_;

    MathNameMap          standardLibraryMathNames_;
    AtomicsNameMap       standardLibraryAtomicsNames_;
    SimdOperationNameMap standardLibrarySimdOpNames_;

    ParseNode*           moduleFunctionNode_;
    PropertyName*        moduleFunctionName_;

    UniqueChars          errorString_;
    uint32_t             errorOffset_;
    bool                 errorOverRecursed_;

    bool                 canValidateChangeHeap_;
    bool                 hasChangeHeap_;
    bool                 supportsSimd_;
    bool                 atomicsPresent_;

  public:
    ModuleValidator(ExclusiveContext* cx, AsmJSParser& parser)
      : cx_(cx),
        parser_(parser),
        mg_(cx),
        validationLifo_(VALIDATION_LIFO_DEFAULT_CHUNK_SIZE),
        functions_(cx),
        funcPtrTables_(cx),
        globals_(cx),
        arrayViews_(cx),
        imports_(cx),
        standardLibraryMathNames_(cx),
        standardLibraryAtomicsNames_(cx),
        standardLibrarySimdOpNames_(cx),
        moduleFunctionNode_(parser.pc->maybeFunction),
        moduleFunctionName_(nullptr),
        errorString_(nullptr),
        errorOffset_(UINT32_MAX),
        errorOverRecursed_(false),
        canValidateChangeHeap_(false),
        hasChangeHeap_(false),
        supportsSimd_(cx->jitSupportsSimd()),
        atomicsPresent_(false)
    {
        MOZ_ASSERT(moduleFunctionNode_->pn_funbox == parser.pc->sc->asFunctionBox());
    }

    ~ModuleValidator() {
        if (errorString_) {
            MOZ_ASSERT(errorOffset_ != UINT32_MAX);
            tokenStream().reportAsmJSError(errorOffset_,
                                           JSMSG_USE_ASM_TYPE_FAIL,
                                           errorString_.get());
        }
        if (errorOverRecursed_)
            ReportOverRecursed(cx_);
    }

  private:

    // Helpers
    bool addStandardLibraryMathName(const char* name, AsmJSMathBuiltinFunction func) {
        JSAtom* atom = Atomize(cx_, name, strlen(name));
        if (!atom)
            return false;
        MathBuiltin builtin(func);
        return standardLibraryMathNames_.putNew(atom->asPropertyName(), builtin);
    }
    bool addStandardLibraryMathName(const char* name, double cst) {
        JSAtom* atom = Atomize(cx_, name, strlen(name));
        if (!atom)
            return false;
        MathBuiltin builtin(cst);
        return standardLibraryMathNames_.putNew(atom->asPropertyName(), builtin);
    }
    bool addStandardLibraryAtomicsName(const char* name, AsmJSAtomicsBuiltinFunction func) {
        JSAtom* atom = Atomize(cx_, name, strlen(name));
        if (!atom)
            return false;
        return standardLibraryAtomicsNames_.putNew(atom->asPropertyName(), func);
    }
    bool addStandardLibrarySimdOpName(const char* name, AsmJSSimdOperation op) {
        JSAtom* atom = Atomize(cx_, name, strlen(name));
        if (!atom)
            return false;
        return standardLibrarySimdOpNames_.putNew(atom->asPropertyName(), op);
    }

  public:

    bool init(HandleAsmJSModule moduleObj) {
        if (!globals_.init() || !imports_.init())
            return false;

        if (!standardLibraryMathNames_.init() ||
            !addStandardLibraryMathName("sin", AsmJSMathBuiltin_sin) ||
            !addStandardLibraryMathName("cos", AsmJSMathBuiltin_cos) ||
            !addStandardLibraryMathName("tan", AsmJSMathBuiltin_tan) ||
            !addStandardLibraryMathName("asin", AsmJSMathBuiltin_asin) ||
            !addStandardLibraryMathName("acos", AsmJSMathBuiltin_acos) ||
            !addStandardLibraryMathName("atan", AsmJSMathBuiltin_atan) ||
            !addStandardLibraryMathName("ceil", AsmJSMathBuiltin_ceil) ||
            !addStandardLibraryMathName("floor", AsmJSMathBuiltin_floor) ||
            !addStandardLibraryMathName("exp", AsmJSMathBuiltin_exp) ||
            !addStandardLibraryMathName("log", AsmJSMathBuiltin_log) ||
            !addStandardLibraryMathName("pow", AsmJSMathBuiltin_pow) ||
            !addStandardLibraryMathName("sqrt", AsmJSMathBuiltin_sqrt) ||
            !addStandardLibraryMathName("abs", AsmJSMathBuiltin_abs) ||
            !addStandardLibraryMathName("atan2", AsmJSMathBuiltin_atan2) ||
            !addStandardLibraryMathName("imul", AsmJSMathBuiltin_imul) ||
            !addStandardLibraryMathName("clz32", AsmJSMathBuiltin_clz32) ||
            !addStandardLibraryMathName("fround", AsmJSMathBuiltin_fround) ||
            !addStandardLibraryMathName("min", AsmJSMathBuiltin_min) ||
            !addStandardLibraryMathName("max", AsmJSMathBuiltin_max) ||
            !addStandardLibraryMathName("E", M_E) ||
            !addStandardLibraryMathName("LN10", M_LN10) ||
            !addStandardLibraryMathName("LN2", M_LN2) ||
            !addStandardLibraryMathName("LOG2E", M_LOG2E) ||
            !addStandardLibraryMathName("LOG10E", M_LOG10E) ||
            !addStandardLibraryMathName("PI", M_PI) ||
            !addStandardLibraryMathName("SQRT1_2", M_SQRT1_2) ||
            !addStandardLibraryMathName("SQRT2", M_SQRT2))
        {
            return false;
        }

        if (!standardLibraryAtomicsNames_.init() ||
            !addStandardLibraryAtomicsName("compareExchange", AsmJSAtomicsBuiltin_compareExchange) ||
            !addStandardLibraryAtomicsName("exchange", AsmJSAtomicsBuiltin_exchange) ||
            !addStandardLibraryAtomicsName("load", AsmJSAtomicsBuiltin_load) ||
            !addStandardLibraryAtomicsName("store", AsmJSAtomicsBuiltin_store) ||
            !addStandardLibraryAtomicsName("fence", AsmJSAtomicsBuiltin_fence) ||
            !addStandardLibraryAtomicsName("add", AsmJSAtomicsBuiltin_add) ||
            !addStandardLibraryAtomicsName("sub", AsmJSAtomicsBuiltin_sub) ||
            !addStandardLibraryAtomicsName("and", AsmJSAtomicsBuiltin_and) ||
            !addStandardLibraryAtomicsName("or", AsmJSAtomicsBuiltin_or) ||
            !addStandardLibraryAtomicsName("xor", AsmJSAtomicsBuiltin_xor) ||
            !addStandardLibraryAtomicsName("isLockFree", AsmJSAtomicsBuiltin_isLockFree))
        {
            return false;
        }

#define ADDSTDLIBSIMDOPNAME(op) || !addStandardLibrarySimdOpName(#op, AsmJSSimdOperation_##op)
        if (!standardLibrarySimdOpNames_.init()
            FORALL_SIMD_ASMJS_OP(ADDSTDLIBSIMDOPNAME))
        {
            return false;
        }
#undef ADDSTDLIBSIMDOPNAME

        uint32_t srcStart = parser_.pc->maybeFunction->pn_body->pn_pos.begin;
        uint32_t srcBodyStart = tokenStream().currentToken().pos.end;

        // "use strict" should be added to the source if we are in an implicit
        // strict context, see also comment above addUseStrict in
        // js::FunctionToString.
        bool strict = parser_.pc->sc->strict() && !parser_.pc->sc->hasExplicitUseStrict();

        module_ = cx_->new_<AsmJSModule>(parser_.ss, srcStart, srcBodyStart, strict);
        if (!module_)
            return false;

        moduleObj->setModule(module_);

        return mg_.init();
    }

    bool finish(SlowFunctionVector* slowFuncs) {
        uint32_t endBeforeCurly = tokenStream().currentToken().pos.end;
        TokenPos pos;
        JS_ALWAYS_TRUE(tokenStream().peekTokenPos(&pos, TokenStream::Operand));
        uint32_t endAfterCurly = pos.end;

        auto usesHeap = Module::HeapBool(module_->hasArrayView());
        auto sharedHeap = Module::SharedBool(module_->isSharedView());
        auto mutedErrors = Module::MutedBool(parser_.ss->mutedErrors());

        CacheableChars filename = make_string_copy(parser_.ss->filename());
        if (!filename)
            return false;

        CacheableTwoByteChars displayURL;
        if (parser_.ss->hasDisplayURL()) {
            uint32_t length = js_strlen(parser_.ss->displayURL());
            displayURL.reset(js_pod_calloc<char16_t>(length + 1));
            if (!displayURL)
                return false;
            PodCopy(displayURL.get(), parser_.ss->displayURL(), length);
        }

        UniqueStaticLinkData linkData;
        Module* wasm = mg_.finish(usesHeap, sharedHeap, mutedErrors,
                                  Move(filename), Move(displayURL),
                                  &linkData, slowFuncs);
        if (!wasm)
            return false;

        module_->finish(wasm, Move(linkData), endBeforeCurly, endAfterCurly);
        return true;
    }

    // Mutable interface.
    void initModuleFunctionName(PropertyName* name) { moduleFunctionName_ = name; }
    void initGlobalArgumentName(PropertyName* n)    { module().initGlobalArgumentName(n); }
    void initImportArgumentName(PropertyName* n)    { module().initImportArgumentName(n); }
    void initBufferArgumentName(PropertyName* n)    { module().initBufferArgumentName(n); }

    bool addGlobalVarInit(PropertyName* var, const NumLit& lit, bool isConst) {
        uint32_t globalDataOffset;
        if (!mg_.allocateGlobalVar(lit.type(), &globalDataOffset))
            return false;
        Global::Which which = isConst ? Global::ConstantLiteral : Global::Variable;
        Global* global = validationLifo_.new_<Global>(which);
        if (!global)
            return false;
        global->u.varOrConst.globalDataOffset_ = globalDataOffset;
        global->u.varOrConst.type_ = (isConst ? Type::lit(lit) : Type::var(lit.type())).which();
        if (isConst)
            global->u.varOrConst.literalValue_ = lit;
        return globals_.putNew(var, global) &&
               module().addGlobalVarInit(lit.value(), globalDataOffset);
    }
    bool addGlobalVarImport(PropertyName* var, PropertyName* field, ValType type, bool isConst) {
        uint32_t globalDataOffset;
        if (!mg_.allocateGlobalVar(type, &globalDataOffset))
            return false;
        Global::Which which = isConst ? Global::ConstantImport : Global::Variable;
        Global* global = validationLifo_.new_<Global>(which);
        if (!global)
            return false;
        global->u.varOrConst.globalDataOffset_ = globalDataOffset;
        global->u.varOrConst.type_ = Type::var(type).which();
        return globals_.putNew(var, global) &&
               module().addGlobalVarImport(field, type, globalDataOffset);
    }
    bool addArrayView(PropertyName* var, Scalar::Type vt, PropertyName* maybeField) {
        if (!arrayViews_.append(ArrayView(var, vt)))
            return false;
        Global* global = validationLifo_.new_<Global>(Global::ArrayView);
        if (!global)
            return false;
        global->u.viewInfo.viewType_ = vt;
        return globals_.putNew(var, global) &&
               module().addArrayView(vt, maybeField);
    }
    bool addMathBuiltinFunction(PropertyName* var, AsmJSMathBuiltinFunction func,
                                PropertyName* field)
    {
        Global* global = validationLifo_.new_<Global>(Global::MathBuiltinFunction);
        if (!global)
            return false;
        global->u.mathBuiltinFunc_ = func;
        return globals_.putNew(var, global) &&
               module().addMathBuiltinFunction(func, field);
    }
  private:
    bool addGlobalDoubleConstant(PropertyName* var, double constant) {
        Global* global = validationLifo_.new_<Global>(Global::ConstantLiteral);
        if (!global)
            return false;
        global->u.varOrConst.type_ = Type::Double;
        global->u.varOrConst.literalValue_ = NumLit(NumLit::Double, DoubleValue(constant));
        return globals_.putNew(var, global);
    }
  public:
    bool addMathBuiltinConstant(PropertyName* var, double constant, PropertyName* field) {
        return addGlobalDoubleConstant(var, constant) &&
               module().addMathBuiltinConstant(constant, field);
    }
    bool addGlobalConstant(PropertyName* var, double constant, PropertyName* field) {
        return addGlobalDoubleConstant(var, constant) &&
               module().addGlobalConstant(constant, field);
    }
    bool addAtomicsBuiltinFunction(PropertyName* var, AsmJSAtomicsBuiltinFunction func,
                                   PropertyName* field)
    {
        Global* global = validationLifo_.new_<Global>(Global::AtomicsBuiltinFunction);
        if (!global)
            return false;
        atomicsPresent_ = true;
        global->u.atomicsBuiltinFunc_ = func;
        return globals_.putNew(var, global) &&
               module().addAtomicsBuiltinFunction(func, field);
    }
    bool addSimdCtor(PropertyName* var, AsmJSSimdType type, PropertyName* field) {
        Global* global = validationLifo_.new_<Global>(Global::SimdCtor);
        if (!global)
            return false;
        global->u.simdCtorType_ = type;
        return globals_.putNew(var, global) &&
               module().addSimdCtor(type, field);
    }
    bool addSimdOperation(PropertyName* var, AsmJSSimdType type, AsmJSSimdOperation op,
                          PropertyName* opName)
    {
        Global* global = validationLifo_.new_<Global>(Global::SimdOperation);
        if (!global)
            return false;
        global->u.simdOp.type_ = type;
        global->u.simdOp.which_ = op;
        return globals_.putNew(var, global) &&
               module().addSimdOperation(type, op, opName);
    }
    bool addByteLength(PropertyName* name) {
        canValidateChangeHeap_ = true;
        Global* global = validationLifo_.new_<Global>(Global::ByteLength);
        return global &&
               globals_.putNew(name, global) &&
               module().addByteLength();
    }
    bool addChangeHeap(PropertyName* name, ParseNode* fn, uint32_t mask, uint32_t min, uint32_t max) {
        hasChangeHeap_ = true;
        Global* global = validationLifo_.new_<Global>(Global::ChangeHeap);
        if (!global)
            return false;
        global->u.changeHeap.srcBegin_ = fn->pn_pos.begin;
        global->u.changeHeap.srcEnd_ = fn->pn_pos.end;
        return globals_.putNew(name, global) &&
               module().addChangeHeap(mask, min, max);
    }
    bool addArrayViewCtor(PropertyName* var, Scalar::Type vt, PropertyName* field) {
        Global* global = validationLifo_.new_<Global>(Global::ArrayViewCtor);
        if (!global)
            return false;
        global->u.viewInfo.viewType_ = vt;
        return globals_.putNew(var, global) &&
               module().addArrayViewCtor(vt, field);
    }
    bool addFFI(PropertyName* var, PropertyName* field) {
        Global* global = validationLifo_.new_<Global>(Global::FFI);
        if (!global)
            return false;
        uint32_t index;
        if (!module().addFFI(field, &index))
            return false;
        global->u.ffiIndex_ = index;
        return globals_.putNew(var, global);
    }
    bool addExport(ParseNode* pn, const Func& func, PropertyName* maybeFieldName) {
        MallocSig::ArgVector args;
        if (!args.appendAll(func.sig().args()))
            return false;
        MallocSig sig(Move(args), func.sig().ret());
        uint32_t wasmIndex;
        if (!mg_.declareExport(Move(sig), func.index(), &wasmIndex))
            return false;
        if (wasmIndex == AsmJSModule::Export::ChangeHeap)
            return fail(pn, "too many exports");
        return module().addExport(func.name(), maybeFieldName, wasmIndex,
                                  func.srcBegin(), func.srcEnd());
    }
    bool addChangeHeapExport(PropertyName* name, const Global& g, PropertyName* maybeFieldName) {
        return module().addExport(name, maybeFieldName, AsmJSModule::Export::ChangeHeap,
                                  g.changeHeapSrcBegin(), g.changeHeapSrcEnd());
    }
  private:
    const LifoSig* getLifoSig(const LifoSig& sig) {
        return &sig;
    }
    const LifoSig* getLifoSig(const MallocSig& sig) {
        return mg_.newLifoSig(sig);
    }
  public:
    bool addFunction(PropertyName* name, uint32_t firstUse, const MallocSig& sig, Func** func) {
        uint32_t funcIndex = numFunctions();
        Global* global = validationLifo_.new_<Global>(Global::Function);
        if (!global)
            return false;
        global->u.funcIndex_ = funcIndex;
        if (!globals_.putNew(name, global))
            return false;
        const LifoSig* lifoSig = getLifoSig(sig);
        if (!lifoSig)
            return false;
        *func = validationLifo_.new_<Func>(name, firstUse, *lifoSig, funcIndex);
        return *func && functions_.append(*func);
    }
    template <class SigT>
    bool declareFuncPtrTable(PropertyName* name, uint32_t firstUse, SigT& sig, uint32_t mask,
                             uint32_t* index)
    {
        if (!mg_.declareFuncPtrTable(/* numElems = */ mask + 1, index))
            return false;
        MOZ_ASSERT(*index == numFuncPtrTables());
        Global* global = validationLifo_.new_<Global>(Global::FuncPtrTable);
        if (!global)
            return false;
        global->u.funcPtrTableIndex_ = *index;
        if (!globals_.putNew(name, global))
            return false;
        const LifoSig* lifoSig = getLifoSig(sig);
        if (!lifoSig)
            return false;
        FuncPtrTable* t = validationLifo_.new_<FuncPtrTable>(cx_, name, firstUse, *lifoSig, mask);
        return t && funcPtrTables_.append(t);
    }
    bool defineFuncPtrTable(uint32_t funcPtrTableIndex, const Vector<uint32_t>& elems) {
        FuncPtrTable& table = *funcPtrTables_[funcPtrTableIndex];
        if (table.defined())
            return false;
        table.define();
        mg_.defineFuncPtrTable(funcPtrTableIndex, elems);
        return true;
    }
    bool addImport(PropertyName* name, MallocSig&& sig, unsigned ffiIndex, unsigned* importIndex,
                 const LifoSig** lifoSig)
    {
        ImportDescriptor::Lookup lookup(name, sig);
        ImportMap::AddPtr p = imports_.lookupForAdd(lookup);
        if (p) {
            *lifoSig = &p->key().sig();
            *importIndex = p->value();
            return true;
        }
        *lifoSig = getLifoSig(sig);
        if (!*lifoSig)
            return false;
        return mg_.declareImport(Move(sig), importIndex) &&
               imports_.add(p, ImportDescriptor(name, **lifoSig), *importIndex) &&
               module().addImport(ffiIndex, *importIndex);
    }

    bool tryOnceToValidateChangeHeap() {
        bool ret = canValidateChangeHeap_;
        canValidateChangeHeap_ = false;
        return ret;
    }
    bool hasChangeHeap() const {
        return hasChangeHeap_;
    }
    bool tryRequireHeapLengthToBeAtLeast(uint32_t len) {
        return module().tryRequireHeapLengthToBeAtLeast(len);
    }
    uint32_t minHeapLength() const {
        return module().minHeapLength();
    }

    bool usesSharedMemory() const {
        return atomicsPresent_;
    }

    // Error handling.
    bool hasAlreadyFailed() const {
        return !!errorString_;
    }

    bool failOffset(uint32_t offset, const char* str) {
        MOZ_ASSERT(!hasAlreadyFailed());
        MOZ_ASSERT(errorOffset_ == UINT32_MAX);
        MOZ_ASSERT(str);
        errorOffset_ = offset;
        errorString_ = make_string_copy(str);
        return false;
    }

    bool fail(ParseNode* pn, const char* str) {
        return failOffset(pn->pn_pos.begin, str);
    }

    bool failfVAOffset(uint32_t offset, const char* fmt, va_list ap) {
        MOZ_ASSERT(!hasAlreadyFailed());
        MOZ_ASSERT(errorOffset_ == UINT32_MAX);
        MOZ_ASSERT(fmt);
        errorOffset_ = offset;
        errorString_.reset(JS_vsmprintf(fmt, ap));
        return false;
    }

    bool failfOffset(uint32_t offset, const char* fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        failfVAOffset(offset, fmt, ap);
        va_end(ap);
        return false;
    }

    bool failf(ParseNode* pn, const char* fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        failfVAOffset(pn->pn_pos.begin, fmt, ap);
        va_end(ap);
        return false;
    }

    bool failNameOffset(uint32_t offset, const char* fmt, PropertyName* name) {
        // This function is invoked without the caller properly rooting its locals.
        gc::AutoSuppressGC suppress(cx_);
        JSAutoByteString bytes;
        if (AtomToPrintableString(cx_, name, &bytes))
            failfOffset(offset, fmt, bytes.ptr());
        return false;
    }

    bool failName(ParseNode* pn, const char* fmt, PropertyName* name) {
        return failNameOffset(pn->pn_pos.begin, fmt, name);
    }

    bool failOverRecursed() {
        errorOverRecursed_ = true;
        return false;
    }

    // Read-only interface
    ExclusiveContext* cx() const             { return cx_; }
    ParseNode* moduleFunctionNode() const    { return moduleFunctionNode_; }
    PropertyName* moduleFunctionName() const { return moduleFunctionName_; }
    ModuleGenerator& mg()                    { return mg_; }
    AsmJSModule& module() const              { return *module_; }
    AsmJSParser& parser() const              { return parser_; }
    TokenStream& tokenStream() const         { return parser_.tokenStream; }
    bool supportsSimd() const                { return supportsSimd_; }

    unsigned numArrayViews() const {
        return arrayViews_.length();
    }
    const ArrayView& arrayView(unsigned i) const {
        return arrayViews_[i];
    }
    unsigned numFunctions() const {
        return functions_.length();
    }
    Func& function(unsigned i) const {
        return *functions_[i];
    }
    unsigned numFuncPtrTables() const {
        return funcPtrTables_.length();
    }
    FuncPtrTable& funcPtrTable(unsigned i) const {
        return *funcPtrTables_[i];
    }

    const Global* lookupGlobal(PropertyName* name) const {
        if (GlobalMap::Ptr p = globals_.lookup(name))
            return p->value();
        return nullptr;
    }

    Func* lookupFunction(PropertyName* name) {
        if (GlobalMap::Ptr p = globals_.lookup(name)) {
            Global* value = p->value();
            if (value->which() == Global::Function)
                return functions_[value->funcIndex()];
        }
        return nullptr;
    }

    bool lookupStandardLibraryMathName(PropertyName* name, MathBuiltin* mathBuiltin) const {
        if (MathNameMap::Ptr p = standardLibraryMathNames_.lookup(name)) {
            *mathBuiltin = p->value();
            return true;
        }
        return false;
    }
    bool lookupStandardLibraryAtomicsName(PropertyName* name, AsmJSAtomicsBuiltinFunction* atomicsBuiltin) const {
        if (AtomicsNameMap::Ptr p = standardLibraryAtomicsNames_.lookup(name)) {
            *atomicsBuiltin = p->value();
            return true;
        }
        return false;
    }
    bool lookupStandardSimdOpName(PropertyName* name, AsmJSSimdOperation* op) const {
        if (SimdOperationNameMap::Ptr p = standardLibrarySimdOpNames_.lookup(name)) {
            *op = p->value();
            return true;
        }
        return false;
    }

    bool startFunctionBodies() {
        if (atomicsPresent_) {
#if defined(ENABLE_SHARED_ARRAY_BUFFER)
            module().setViewsAreShared();
#else
            return failOffset(parser_.tokenStream.currentToken().pos.begin,
                              "shared memory and atomics not supported by this build");
#endif
        }
        return true;
    }
    bool finishFunctionBodies() {
        return mg_.finishFuncs();
    }
};

} // namespace

/*****************************************************************************/
// Numeric literal utilities

static bool
IsNumericNonFloatLiteral(ParseNode* pn)
{
    // Note: '-' is never rolled into the number; numbers are always positive
    // and negations must be applied manually.
    return pn->isKind(PNK_NUMBER) ||
           (pn->isKind(PNK_NEG) && UnaryKid(pn)->isKind(PNK_NUMBER));
}

static bool
IsCallToGlobal(ModuleValidator& m, ParseNode* pn, const ModuleValidator::Global** global)
{
    if (!pn->isKind(PNK_CALL))
        return false;

    ParseNode* callee = CallCallee(pn);
    if (!callee->isKind(PNK_NAME))
        return false;

    *global = m.lookupGlobal(callee->name());
    return !!*global;
}

static bool
IsCoercionCall(ModuleValidator& m, ParseNode* pn, ValType* coerceTo, ParseNode** coercedExpr)
{
    const ModuleValidator::Global* global;
    if (!IsCallToGlobal(m, pn, &global))
        return false;

    if (CallArgListLength(pn) != 1)
        return false;

    if (coercedExpr)
        *coercedExpr = CallArgList(pn);

    if (global->isMathFunction() && global->mathBuiltinFunction() == AsmJSMathBuiltin_fround) {
        *coerceTo = ValType::F32;
        return true;
    }

    if (global->isSimdOperation() && global->simdOperation() == AsmJSSimdOperation_check) {
        switch (global->simdOperationType()) {
          case AsmJSSimdType_int32x4:
            *coerceTo = ValType::I32x4;
            return true;
          case AsmJSSimdType_float32x4:
            *coerceTo = ValType::F32x4;
            return true;
          case AsmJSSimdType_bool32x4:
            *coerceTo = ValType::B32x4;
            return true;
        }
    }

    return false;
}

static bool
IsFloatLiteral(ModuleValidator& m, ParseNode* pn)
{
    ParseNode* coercedExpr;
    ValType coerceTo;
    if (!IsCoercionCall(m, pn, &coerceTo, &coercedExpr))
        return false;
    // Don't fold into || to avoid clang/memcheck bug (bug 1077031).
    if (coerceTo != ValType::F32)
        return false;
    return IsNumericNonFloatLiteral(coercedExpr);
}

static unsigned
SimdTypeToLength(AsmJSSimdType type)
{
    switch (type) {
      case AsmJSSimdType_int32x4:
      case AsmJSSimdType_float32x4:
      case AsmJSSimdType_bool32x4:
        return 4;
    }
    MOZ_CRASH("unexpected SIMD type");
}

static bool
IsSimdTuple(ModuleValidator& m, ParseNode* pn, AsmJSSimdType* type)
{
    const ModuleValidator::Global* global;
    if (!IsCallToGlobal(m, pn, &global))
        return false;

    if (!global->isSimdCtor())
        return false;

    if (CallArgListLength(pn) != SimdTypeToLength(global->simdCtorType()))
        return false;

    *type = global->simdCtorType();
    return true;
}

static bool
IsNumericLiteral(ModuleValidator& m, ParseNode* pn);

static NumLit
ExtractNumericLiteral(ModuleValidator& m, ParseNode* pn);

static inline bool
IsLiteralInt(ModuleValidator& m, ParseNode* pn, uint32_t* u32);

static bool
IsSimdLiteral(ModuleValidator& m, ParseNode* pn)
{
    AsmJSSimdType type;
    if (!IsSimdTuple(m, pn, &type))
        return false;

    ParseNode* arg = CallArgList(pn);
    unsigned length = SimdTypeToLength(type);
    for (unsigned i = 0; i < length; i++) {
        if (!IsNumericLiteral(m, arg))
            return false;

        uint32_t _;
        switch (type) {
          case AsmJSSimdType_int32x4:
          case AsmJSSimdType_bool32x4:
            if (!IsLiteralInt(m, arg, &_))
                return false;
          case AsmJSSimdType_float32x4:
            if (!IsNumericNonFloatLiteral(arg))
                return false;
        }

        arg = NextNode(arg);
    }

    MOZ_ASSERT(arg == nullptr);
    return true;
}

static bool
IsNumericLiteral(ModuleValidator& m, ParseNode* pn)
{
    return IsNumericNonFloatLiteral(pn) ||
           IsFloatLiteral(m, pn) ||
           IsSimdLiteral(m, pn);
}

// The JS grammar treats -42 as -(42) (i.e., with separate grammar
// productions) for the unary - and literal 42). However, the asm.js spec
// recognizes -42 (modulo parens, so -(42) and -((42))) as a single literal
// so fold the two potential parse nodes into a single double value.
static double
ExtractNumericNonFloatValue(ParseNode* pn, ParseNode** out = nullptr)
{
    MOZ_ASSERT(IsNumericNonFloatLiteral(pn));

    if (pn->isKind(PNK_NEG)) {
        pn = UnaryKid(pn);
        if (out)
            *out = pn;
        return -NumberNodeValue(pn);
    }

    return NumberNodeValue(pn);
}

static NumLit
ExtractSimdValue(ModuleValidator& m, ParseNode* pn)
{
    MOZ_ASSERT(IsSimdLiteral(m, pn));

    AsmJSSimdType type = AsmJSSimdType_int32x4;
    JS_ALWAYS_TRUE(IsSimdTuple(m, pn, &type));

    ParseNode* arg = CallArgList(pn);
    switch (type) {
      case AsmJSSimdType_int32x4: {
        MOZ_ASSERT(SimdTypeToLength(type) == 4);
        int32_t val[4];
        for (size_t i = 0; i < 4; i++, arg = NextNode(arg)) {
            uint32_t u32;
            JS_ALWAYS_TRUE(IsLiteralInt(m, arg, &u32));
            val[i] = int32_t(u32);
        }
        MOZ_ASSERT(arg== nullptr);
        return NumLit(NumLit::Int32x4, SimdConstant::CreateX4(val));
      }
      case AsmJSSimdType_float32x4: {
        MOZ_ASSERT(SimdTypeToLength(type) == 4);
        float val[4];
        for (size_t i = 0; i < 4; i++, arg = NextNode(arg))
            val[i] = float(ExtractNumericNonFloatValue(arg));
        MOZ_ASSERT(arg == nullptr);
        return NumLit(NumLit::Float32x4, SimdConstant::CreateX4(val));
      }
      case AsmJSSimdType_bool32x4: {
        MOZ_ASSERT(SimdTypeToLength(type) == 4);
        int32_t val[4];
        for (size_t i = 0; i < 4; i++, arg = NextNode(arg)) {
            uint32_t u32;
            JS_ALWAYS_TRUE(IsLiteralInt(m, arg, &u32));
            val[i] = u32 ? -1 : 0;
        }
        MOZ_ASSERT(arg == nullptr);
        return NumLit(NumLit::Bool32x4, SimdConstant::CreateX4(val));
      }
    }

    MOZ_CRASH("Unexpected SIMD type.");
}

static NumLit
ExtractNumericLiteral(ModuleValidator& m, ParseNode* pn)
{
    MOZ_ASSERT(IsNumericLiteral(m, pn));

    if (pn->isKind(PNK_CALL)) {
        // Float literals are explicitly coerced and thus the coerced literal may be
        // any valid (non-float) numeric literal.
        if (CallArgListLength(pn) == 1) {
            pn = CallArgList(pn);
            double d = ExtractNumericNonFloatValue(pn);
            return NumLit(NumLit::Float, DoubleValue(d));
        }

        MOZ_ASSERT(CallArgListLength(pn) == 4);
        return ExtractSimdValue(m, pn);
    }

    double d = ExtractNumericNonFloatValue(pn, &pn);

    // The asm.js spec syntactically distinguishes any literal containing a
    // decimal point or the literal -0 as having double type.
    if (NumberNodeHasFrac(pn) || IsNegativeZero(d))
        return NumLit(NumLit::Double, DoubleValue(d));

    // The syntactic checks above rule out these double values.
    MOZ_ASSERT(!IsNegativeZero(d));
    MOZ_ASSERT(!IsNaN(d));

    // Although doubles can only *precisely* represent 53-bit integers, they
    // can *imprecisely* represent integers much bigger than an int64_t.
    // Furthermore, d may be inf or -inf. In both cases, casting to an int64_t
    // is undefined, so test against the integer bounds using doubles.
    if (d < double(INT32_MIN) || d > double(UINT32_MAX))
        return NumLit(NumLit::OutOfRangeInt, UndefinedValue());

    // With the above syntactic and range limitations, d is definitely an
    // integer in the range [INT32_MIN, UINT32_MAX] range.
    int64_t i64 = int64_t(d);
    if (i64 >= 0) {
        if (i64 <= INT32_MAX)
            return NumLit(NumLit::Fixnum, Int32Value(i64));
        MOZ_ASSERT(i64 <= UINT32_MAX);
        return NumLit(NumLit::BigUnsigned, Int32Value(uint32_t(i64)));
    }
    MOZ_ASSERT(i64 >= INT32_MIN);
    return NumLit(NumLit::NegativeInt, Int32Value(i64));
}

static inline bool
IsLiteralInt(NumLit lit, uint32_t* u32)
{
    switch (lit.which()) {
      case NumLit::Fixnum:
      case NumLit::BigUnsigned:
      case NumLit::NegativeInt:
        *u32 = lit.toUint32();
        return true;
      case NumLit::Double:
      case NumLit::Float:
      case NumLit::OutOfRangeInt:
      case NumLit::Int32x4:
      case NumLit::Float32x4:
      case NumLit::Bool32x4:
        return false;
    }
    MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("Bad literal type");
}

static inline bool
IsLiteralInt(ModuleValidator& m, ParseNode* pn, uint32_t* u32)
{
    return IsNumericLiteral(m, pn) &&
           IsLiteralInt(ExtractNumericLiteral(m, pn), u32);
}

/*****************************************************************************/

namespace {

// Encapsulates the building of an asm bytecode function from an asm.js function
// source code, packing the asm.js code into the asm bytecode form that can
// be decoded and compiled with a FunctionCompiler.
class MOZ_STACK_CLASS FunctionValidator
{
  public:
    struct Local
    {
        ValType type;
        unsigned slot;
        Local(ValType t, unsigned slot) : type(t), slot(slot) {}
    };

  private:
    typedef HashMap<PropertyName*, Local> LocalMap;
    typedef HashMap<PropertyName*, uint32_t> LabelMap;

    ModuleValidator&  m_;
    ParseNode*        fn_;

    FunctionGenerator fg_;

    LocalMap          locals_;
    LabelMap          labels_;

    unsigned          heapExpressionDepth_;

    bool              hasAlreadyReturned_;
    ExprType          ret_;

  public:
    FunctionValidator(ModuleValidator& m, ParseNode* fn)
      : m_(m),
        fn_(fn),
        locals_(m.cx()),
        labels_(m.cx()),
        heapExpressionDepth_(0),
        hasAlreadyReturned_(false)
    {}

    ModuleValidator& m() const        { return m_; }
    const AsmJSModule& module() const { return m_.module(); }
    FuncIR& funcIR() const            { return fg_.func(); }
    ExclusiveContext* cx() const      { return m_.cx(); }
    ParseNode* fn() const             { return fn_; }

    bool init(PropertyName* name, unsigned line, unsigned column) {
        return locals_.init() &&
               labels_.init() &&
               m_.mg().startFunc(name, line, column, &fg_);
    }

    bool finish(uint32_t funcIndex, const LifoSig& sig, unsigned generateTime) {
        return m_.mg().finishFunc(funcIndex, sig, generateTime, &fg_);
    }

    bool fail(ParseNode* pn, const char* str) {
        return m_.fail(pn, str);
    }

    bool failf(ParseNode* pn, const char* fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        m_.failfVAOffset(pn->pn_pos.begin, fmt, ap);
        va_end(ap);
        return false;
    }

    bool failName(ParseNode* pn, const char* fmt, PropertyName* name) {
        return m_.failName(pn, fmt, name);
    }

    /***************************************************** Local scope setup */

    bool addFormal(ParseNode* pn, PropertyName* name, ValType type) {
        LocalMap::AddPtr p = locals_.lookupForAdd(name);
        if (p)
            return failName(pn, "duplicate local name '%s' not allowed", name);
        return locals_.add(p, name, Local(type, locals_.count()));
    }

    bool addVariable(ParseNode* pn, PropertyName* name, const NumLit& init) {
        LocalMap::AddPtr p = locals_.lookupForAdd(name);
        if (p)
            return failName(pn, "duplicate local name '%s' not allowed", name);
        if (!locals_.add(p, name, Local(init.type(), locals_.count())))
            return false;
        return funcIR().addVariable(init.value());
    }

    /*************************************************************************/

    void enterHeapExpression() {
        heapExpressionDepth_++;
    }
    void leaveHeapExpression() {
        MOZ_ASSERT(heapExpressionDepth_ > 0);
        heapExpressionDepth_--;
    }
    bool canCall() const {
        return heapExpressionDepth_ == 0 || !m_.hasChangeHeap();
    }

    /****************************** For consistency of returns in a function */

    bool hasAlreadyReturned() const {
        return hasAlreadyReturned_;
    }

    ExprType returnedType() const {
        return ret_;
    }

    void setReturnedType(ExprType ret) {
        ret_ = ret;
        hasAlreadyReturned_ = true;
    }

    /**************************************************************** Labels */

    uint32_t lookupLabel(PropertyName* label) const {
        if (auto p = labels_.lookup(label))
            return p->value();
        return -1;
    }

    bool addLabel(PropertyName* label, uint32_t* id) {
        *id = labels_.count();
        return labels_.putNew(label, *id);
    }

    void removeLabel(PropertyName* label) {
        auto p = labels_.lookup(label);
        MOZ_ASSERT(!!p);
        labels_.remove(p);
    }

    /*************************************************** Read-only interface */

    const Local* lookupLocal(PropertyName* name) const {
        if (auto p = locals_.lookup(name))
            return &p->value();
        return nullptr;
    }

    const ModuleValidator::Global* lookupGlobal(PropertyName* name) const {
        if (locals_.has(name))
            return nullptr;
        return m_.lookupGlobal(name);
    }

    size_t numLocals() const { return locals_.count(); }

    /************************************************* Packing interface */

    bool startedPacking() const {
        return funcIR().size() != 0;
    }

    template<class T>
    size_t writeOp(T op) {
        static_assert(sizeof(T) == sizeof(uint8_t), "opcodes must be uint8");
        return funcIR().writeU8(uint8_t(op));
    }

    void writeDebugCheckPoint() {
#ifdef DEBUG
        writeOp(Stmt::DebugCheckPoint);
#endif
    }

    size_t writeU8(uint8_t u) {
        return funcIR().writeU8(u);
    }
    size_t writeU32(uint32_t u) {
        return funcIR().writeU32(u);
    }
    size_t writeI32(int32_t u) {
        return funcIR().writeI32(u);
    }

    void writeInt32Lit(int32_t i) {
        writeOp(I32::Literal);
        funcIR().writeI32(i);
    }

    void writeLit(NumLit lit) {
        switch (lit.which()) {
          case NumLit::Fixnum:
          case NumLit::NegativeInt:
          case NumLit::BigUnsigned:
            writeInt32Lit(lit.toInt32());
            return;
          case NumLit::Float:
            writeOp(F32::Literal);
            funcIR().writeF32(lit.toFloat());
            return;
          case NumLit::Double:
            writeOp(F64::Literal);
            funcIR().writeF64(lit.toDouble());
            return;
          case NumLit::Int32x4:
            writeOp(I32X4::Literal);
            funcIR().writeI32X4(lit.simdValue().asInt32x4());
            return;
          case NumLit::Float32x4:
            writeOp(F32X4::Literal);
            funcIR().writeF32X4(lit.simdValue().asFloat32x4());
            return;
          case NumLit::Bool32x4:
            // Boolean vectors use the Int32x4 memory representation.
            writeOp(B32X4::Literal);
            funcIR().writeI32X4(lit.simdValue().asInt32x4());
            return;
          case NumLit::OutOfRangeInt:
            break;
        }
        MOZ_CRASH("unexpected literal type");
    }

    template<class T>
    void patchOp(size_t pos, T stmt) {
        static_assert(sizeof(T) == sizeof(uint8_t), "opcodes must be uint8");
        funcIR().patchU8(pos, uint8_t(stmt));
    }
    void patchU8(size_t pos, uint8_t u8) {
        funcIR().patchU8(pos, u8);
    }
    template<class T>
    void patch32(size_t pos, T val) {
        static_assert(sizeof(T) == sizeof(uint32_t), "patch32 is used for 4-bytes long ops");
        funcIR().patch32(pos, val);
    }
    void patchSig(size_t pos, const LifoSig* ptr) {
        funcIR().patchSig(pos, ptr);
    }

    size_t tempU8() {
        return funcIR().writeU8(uint8_t(Stmt::Bad));
    }
    size_t tempOp() {
        return tempU8();
    }
    size_t temp32() {
        size_t ret = funcIR().writeU8(uint8_t(Stmt::Bad));
        for (size_t i = 1; i < 4; i++)
            funcIR().writeU8(uint8_t(Stmt::Bad));
        return ret;
    }
    size_t tempPtr() {
        size_t ret = funcIR().writeU8(uint8_t(Stmt::Bad));
        for (size_t i = 1; i < sizeof(intptr_t); i++)
            funcIR().writeU8(uint8_t(Stmt::Bad));
        return ret;
    }
    /************************************************** End of build helpers */
};

} /* anonymous namespace */

/*****************************************************************************/
// asm.js type-checking and code-generation algorithm

static bool
CheckIdentifier(ModuleValidator& m, ParseNode* usepn, PropertyName* name)
{
    if (name == m.cx()->names().arguments || name == m.cx()->names().eval)
        return m.failName(usepn, "'%s' is not an allowed identifier", name);
    return true;
}

static bool
CheckModuleLevelName(ModuleValidator& m, ParseNode* usepn, PropertyName* name)
{
    if (!CheckIdentifier(m, usepn, name))
        return false;

    if (name == m.moduleFunctionName() ||
        name == m.module().globalArgumentName() ||
        name == m.module().importArgumentName() ||
        name == m.module().bufferArgumentName() ||
        m.lookupGlobal(name))
    {
        return m.failName(usepn, "duplicate name '%s' not allowed", name);
    }

    return true;
}

static bool
CheckFunctionHead(ModuleValidator& m, ParseNode* fn)
{
    JSFunction* fun = FunctionObject(fn);
    if (fun->hasRest())
        return m.fail(fn, "rest args not allowed");
    if (fun->isExprBody())
        return m.fail(fn, "expression closures not allowed");
    if (fn->pn_funbox->hasDestructuringArgs)
        return m.fail(fn, "destructuring args not allowed");
    return true;
}

static bool
CheckArgument(ModuleValidator& m, ParseNode* arg, PropertyName** name)
{
    if (!IsDefinition(arg))
        return m.fail(arg, "duplicate argument name not allowed");

    if (arg->isKind(PNK_ASSIGN))
        return m.fail(arg, "default arguments not allowed");

    if (!CheckIdentifier(m, arg, arg->name()))
        return false;

    *name = arg->name();
    return true;
}

static bool
CheckModuleArgument(ModuleValidator& m, ParseNode* arg, PropertyName** name)
{
    if (!CheckArgument(m, arg, name))
        return false;

    if (!CheckModuleLevelName(m, arg, *name))
        return false;

    return true;
}

static bool
CheckModuleArguments(ModuleValidator& m, ParseNode* fn)
{
    unsigned numFormals;
    ParseNode* arg1 = FunctionArgsList(fn, &numFormals);
    ParseNode* arg2 = arg1 ? NextNode(arg1) : nullptr;
    ParseNode* arg3 = arg2 ? NextNode(arg2) : nullptr;

    if (numFormals > 3)
        return m.fail(fn, "asm.js modules takes at most 3 argument");

    PropertyName* arg1Name = nullptr;
    if (numFormals >= 1 && !CheckModuleArgument(m, arg1, &arg1Name))
        return false;
    m.initGlobalArgumentName(arg1Name);

    PropertyName* arg2Name = nullptr;
    if (numFormals >= 2 && !CheckModuleArgument(m, arg2, &arg2Name))
        return false;
    m.initImportArgumentName(arg2Name);

    PropertyName* arg3Name = nullptr;
    if (numFormals >= 3 && !CheckModuleArgument(m, arg3, &arg3Name))
        return false;
    m.initBufferArgumentName(arg3Name);

    return true;
}

static bool
CheckPrecedingStatements(ModuleValidator& m, ParseNode* stmtList)
{
    MOZ_ASSERT(stmtList->isKind(PNK_STATEMENTLIST));

    ParseNode* stmt = ListHead(stmtList);
    for (unsigned i = 0, n = ListLength(stmtList); i < n; i++) {
        if (!IsIgnoredDirective(m.cx(), stmt))
            return m.fail(stmt, "invalid asm.js statement");
    }

    return true;
}

static bool
CheckGlobalVariableInitConstant(ModuleValidator& m, PropertyName* varName, ParseNode* initNode,
                                bool isConst)
{
    NumLit lit = ExtractNumericLiteral(m, initNode);
    if (!lit.valid())
        return m.fail(initNode, "global initializer is out of representable integer range");

    return m.addGlobalVarInit(varName, lit, isConst);
}

static bool
CheckTypeAnnotation(ModuleValidator& m, ParseNode* coercionNode, ValType* coerceTo,
                    ParseNode** coercedExpr = nullptr)
{
    switch (coercionNode->getKind()) {
      case PNK_BITOR: {
        ParseNode* rhs = BitwiseRight(coercionNode);
        uint32_t i;
        if (!IsLiteralInt(m, rhs, &i) || i != 0)
            return m.fail(rhs, "must use |0 for argument/return coercion");
        *coerceTo = ValType::I32;
        if (coercedExpr)
            *coercedExpr = BitwiseLeft(coercionNode);
        return true;
      }
      case PNK_POS: {
        *coerceTo = ValType::F64;
        if (coercedExpr)
            *coercedExpr = UnaryKid(coercionNode);
        return true;
      }
      case PNK_CALL: {
        if (IsCoercionCall(m, coercionNode, coerceTo, coercedExpr))
            return true;
      }
      default:;
    }

    return m.fail(coercionNode, "must be of the form +x, x|0, fround(x), or a SIMD check(x)");
}

static bool
CheckGlobalVariableImportExpr(ModuleValidator& m, PropertyName* varName, ValType coerceTo,
                              ParseNode* coercedExpr, bool isConst)
{
    if (!coercedExpr->isKind(PNK_DOT))
        return m.failName(coercedExpr, "invalid import expression for global '%s'", varName);

    ParseNode* base = DotBase(coercedExpr);
    PropertyName* field = DotMember(coercedExpr);

    PropertyName* importName = m.module().importArgumentName();
    if (!importName)
        return m.fail(coercedExpr, "cannot import without an asm.js foreign parameter");
    if (!IsUseOfName(base, importName))
        return m.failName(coercedExpr, "base of import expression must be '%s'", importName);

    return m.addGlobalVarImport(varName, field, coerceTo, isConst);
}

static bool
CheckGlobalVariableInitImport(ModuleValidator& m, PropertyName* varName, ParseNode* initNode,
                              bool isConst)
{
    ValType coerceTo;
    ParseNode* coercedExpr;
    if (!CheckTypeAnnotation(m, initNode, &coerceTo, &coercedExpr))
        return false;
    return CheckGlobalVariableImportExpr(m, varName, coerceTo, coercedExpr, isConst);
}

static bool
IsArrayViewCtorName(ModuleValidator& m, PropertyName* name, Scalar::Type* type)
{
    JSAtomState& names = m.cx()->names();
    if (name == names.Int8Array) {
        *type = Scalar::Int8;
    } else if (name == names.Uint8Array) {
        *type = Scalar::Uint8;
    } else if (name == names.Int16Array) {
        *type = Scalar::Int16;
    } else if (name == names.Uint16Array) {
        *type = Scalar::Uint16;
    } else if (name == names.Int32Array) {
        *type = Scalar::Int32;
    } else if (name == names.Uint32Array) {
        *type = Scalar::Uint32;
    } else if (name == names.Float32Array) {
        *type = Scalar::Float32;
    } else if (name == names.Float64Array) {
        *type = Scalar::Float64;
    } else {
        return false;
    }
    return true;
}

static bool
CheckNewArrayViewArgs(ModuleValidator& m, ParseNode* ctorExpr, PropertyName* bufferName)
{
    ParseNode* bufArg = NextNode(ctorExpr);
    if (!bufArg || NextNode(bufArg) != nullptr)
        return m.fail(ctorExpr, "array view constructor takes exactly one argument");

    if (!IsUseOfName(bufArg, bufferName))
        return m.failName(bufArg, "argument to array view constructor must be '%s'", bufferName);

    return true;
}

static bool
CheckNewArrayView(ModuleValidator& m, PropertyName* varName, ParseNode* newExpr)
{
    PropertyName* globalName = m.module().globalArgumentName();
    if (!globalName)
        return m.fail(newExpr, "cannot create array view without an asm.js global parameter");

    PropertyName* bufferName = m.module().bufferArgumentName();
    if (!bufferName)
        return m.fail(newExpr, "cannot create array view without an asm.js heap parameter");

    ParseNode* ctorExpr = ListHead(newExpr);

    PropertyName* field;
    Scalar::Type type;
    if (ctorExpr->isKind(PNK_DOT)) {
        ParseNode* base = DotBase(ctorExpr);

        if (!IsUseOfName(base, globalName))
            return m.failName(base, "expecting '%s.*Array", globalName);

        field = DotMember(ctorExpr);
        if (!IsArrayViewCtorName(m, field, &type))
            return m.fail(ctorExpr, "could not match typed array name");
    } else {
        if (!ctorExpr->isKind(PNK_NAME))
            return m.fail(ctorExpr, "expecting name of imported array view constructor");

        PropertyName* globalName = ctorExpr->name();
        const ModuleValidator::Global* global = m.lookupGlobal(globalName);
        if (!global)
            return m.failName(ctorExpr, "%s not found in module global scope", globalName);

        if (global->which() != ModuleValidator::Global::ArrayViewCtor)
            return m.failName(ctorExpr, "%s must be an imported array view constructor", globalName);

        field = nullptr;
        type = global->viewType();
    }

    if (!CheckNewArrayViewArgs(m, ctorExpr, bufferName))
        return false;

    return m.addArrayView(varName, type, field);
}

static bool
IsSimdTypeName(ModuleValidator& m, PropertyName* name, AsmJSSimdType* type)
{
    if (name == m.cx()->names().int32x4) {
        *type = AsmJSSimdType_int32x4;
        return true;
    }
    if (name == m.cx()->names().float32x4) {
        *type = AsmJSSimdType_float32x4;
        return true;
    }
    if (name == m.cx()->names().bool32x4) {
        *type = AsmJSSimdType_bool32x4;
        return true;
    }
    return false;
}

static bool
IsSimdValidOperationType(AsmJSSimdType type, AsmJSSimdOperation op)
{
#define CASE(op) case AsmJSSimdOperation_##op:
    switch(type) {
      case AsmJSSimdType_int32x4:
        switch (op) {
          FORALL_INT32X4_ASMJS_OP(CASE) return true;
          default: return false;
        }
        break;
      case AsmJSSimdType_float32x4:
        switch (op) {
          FORALL_FLOAT32X4_ASMJS_OP(CASE) return true;
          default: return false;
        }
        break;
      case AsmJSSimdType_bool32x4:
        switch (op) {
          FORALL_BOOL_SIMD_OP(CASE) return true;
          default: return false;
        }
        break;
    }
#undef CASE
    MOZ_CRASH("Unhandles SIMD type");
}

static bool
CheckGlobalMathImport(ModuleValidator& m, ParseNode* initNode, PropertyName* varName,
                      PropertyName* field)
{
    // Math builtin, with the form glob.Math.[[builtin]]
    ModuleValidator::MathBuiltin mathBuiltin;
    if (!m.lookupStandardLibraryMathName(field, &mathBuiltin))
        return m.failName(initNode, "'%s' is not a standard Math builtin", field);

    switch (mathBuiltin.kind) {
      case ModuleValidator::MathBuiltin::Function:
        return m.addMathBuiltinFunction(varName, mathBuiltin.u.func, field);
      case ModuleValidator::MathBuiltin::Constant:
        return m.addMathBuiltinConstant(varName, mathBuiltin.u.cst, field);
      default:
        break;
    }
    MOZ_CRASH("unexpected or uninitialized math builtin type");
}

static bool
CheckGlobalAtomicsImport(ModuleValidator& m, ParseNode* initNode, PropertyName* varName,
                         PropertyName* field)
{
    // Atomics builtin, with the form glob.Atomics.[[builtin]]
    AsmJSAtomicsBuiltinFunction func;
    if (!m.lookupStandardLibraryAtomicsName(field, &func))
        return m.failName(initNode, "'%s' is not a standard Atomics builtin", field);

    return m.addAtomicsBuiltinFunction(varName, func, field);
}

static bool
CheckGlobalSimdImport(ModuleValidator& m, ParseNode* initNode, PropertyName* varName,
                      PropertyName* field)
{
    if (!m.supportsSimd())
        return m.fail(initNode, "SIMD is not supported on this platform");

    // SIMD constructor, with the form glob.SIMD.[[type]]
    AsmJSSimdType simdType;
    if (!IsSimdTypeName(m, field, &simdType))
        return m.failName(initNode, "'%s' is not a standard SIMD type", field);
    return m.addSimdCtor(varName, simdType, field);
}

static bool
CheckGlobalSimdOperationImport(ModuleValidator& m, const ModuleValidator::Global* global,
                               ParseNode* initNode, PropertyName* varName, PropertyName* opName)
{
    AsmJSSimdType simdType = global->simdCtorType();
    AsmJSSimdOperation simdOp;
    if (!m.lookupStandardSimdOpName(opName, &simdOp))
        return m.failName(initNode, "'%s' is not a standard SIMD operation", opName);
    if (!IsSimdValidOperationType(simdType, simdOp))
        return m.failName(initNode, "'%s' is not an operation supported by the SIMD type", opName);
    return m.addSimdOperation(varName, simdType, simdOp, opName);
}

static bool
CheckGlobalDotImport(ModuleValidator& m, PropertyName* varName, ParseNode* initNode)
{
    ParseNode* base = DotBase(initNode);
    PropertyName* field = DotMember(initNode);

    if (base->isKind(PNK_DOT)) {
        ParseNode* global = DotBase(base);
        PropertyName* mathOrAtomicsOrSimd = DotMember(base);

        PropertyName* globalName = m.module().globalArgumentName();
        if (!globalName)
            return m.fail(base, "import statement requires the module have a stdlib parameter");

        if (!IsUseOfName(global, globalName)) {
            if (global->isKind(PNK_DOT)) {
                return m.failName(base, "imports can have at most two dot accesses "
                                        "(e.g. %s.Math.sin)", globalName);
            }
            return m.failName(base, "expecting %s.*", globalName);
        }

        if (mathOrAtomicsOrSimd == m.cx()->names().Math)
            return CheckGlobalMathImport(m, initNode, varName, field);
        if (mathOrAtomicsOrSimd == m.cx()->names().Atomics)
            return CheckGlobalAtomicsImport(m, initNode, varName, field);
        if (mathOrAtomicsOrSimd == m.cx()->names().SIMD)
            return CheckGlobalSimdImport(m, initNode, varName, field);
        return m.failName(base, "expecting %s.{Math|SIMD}", globalName);
    }

    if (!base->isKind(PNK_NAME))
        return m.fail(base, "expected name of variable or parameter");

    if (base->name() == m.module().globalArgumentName()) {
        if (field == m.cx()->names().NaN)
            return m.addGlobalConstant(varName, GenericNaN(), field);
        if (field == m.cx()->names().Infinity)
            return m.addGlobalConstant(varName, PositiveInfinity<double>(), field);
        if (field == m.cx()->names().byteLength)
            return m.addByteLength(varName);

        Scalar::Type type;
        if (IsArrayViewCtorName(m, field, &type))
            return m.addArrayViewCtor(varName, type, field);

        return m.failName(initNode, "'%s' is not a standard constant or typed array name", field);
    }

    if (base->name() == m.module().importArgumentName())
        return m.addFFI(varName, field);

    const ModuleValidator::Global* global = m.lookupGlobal(base->name());
    if (!global)
        return m.failName(initNode, "%s not found in module global scope", base->name());

    if (!global->isSimdCtor())
        return m.failName(base, "expecting SIMD constructor name, got %s", field);

    return CheckGlobalSimdOperationImport(m, global, initNode, varName, field);
}

static bool
CheckModuleGlobal(ModuleValidator& m, ParseNode* var, bool isConst)
{
    if (!IsDefinition(var))
        return m.fail(var, "import variable names must be unique");

    if (!CheckModuleLevelName(m, var, var->name()))
        return false;

    ParseNode* initNode = MaybeDefinitionInitializer(var);
    if (!initNode)
        return m.fail(var, "module import needs initializer");

    if (IsNumericLiteral(m, initNode))
        return CheckGlobalVariableInitConstant(m, var->name(), initNode, isConst);

    if (initNode->isKind(PNK_BITOR) || initNode->isKind(PNK_POS) || initNode->isKind(PNK_CALL))
        return CheckGlobalVariableInitImport(m, var->name(), initNode, isConst);

    if (initNode->isKind(PNK_NEW))
        return CheckNewArrayView(m, var->name(), initNode);

    if (initNode->isKind(PNK_DOT))
        return CheckGlobalDotImport(m, var->name(), initNode);

    return m.fail(initNode, "unsupported import expression");
}

static bool
CheckModuleProcessingDirectives(ModuleValidator& m)
{
    TokenStream& ts = m.parser().tokenStream;
    while (true) {
        bool matched;
        if (!ts.matchToken(&matched, TOK_STRING, TokenStream::Operand))
            return false;
        if (!matched)
            return true;

        if (!IsIgnoredDirectiveName(m.cx(), ts.currentToken().atom()))
            return m.failOffset(ts.currentToken().pos.begin, "unsupported processing directive");

        TokenKind tt;
        if (!ts.getToken(&tt))
            return false;
        if (tt != TOK_SEMI) {
            return m.failOffset(ts.currentToken().pos.begin,
                                "expected semicolon after string literal");
        }
    }
}

static bool
CheckModuleGlobals(ModuleValidator& m)
{
    while (true) {
        ParseNode* varStmt;
        if (!ParseVarOrConstStatement(m.parser(), &varStmt))
            return false;
        if (!varStmt)
            break;
        for (ParseNode* var = VarListHead(varStmt); var; var = NextNode(var)) {
            if (!CheckModuleGlobal(m, var, varStmt->isKind(PNK_CONST)))
                return false;
        }
    }

    return true;
}

static bool
ArgFail(FunctionValidator& f, PropertyName* argName, ParseNode* stmt)
{
    return f.failName(stmt, "expecting argument type declaration for '%s' of the "
                      "form 'arg = arg|0' or 'arg = +arg' or 'arg = fround(arg)'", argName);
}

static bool
CheckArgumentType(FunctionValidator& f, ParseNode* stmt, PropertyName* name, ValType* type)
{
    if (!stmt || !IsExpressionStatement(stmt))
        return ArgFail(f, name, stmt ? stmt : f.fn());

    ParseNode* initNode = ExpressionStatementExpr(stmt);
    if (!initNode || !initNode->isKind(PNK_ASSIGN))
        return ArgFail(f, name, stmt);

    ParseNode* argNode = BinaryLeft(initNode);
    ParseNode* coercionNode = BinaryRight(initNode);

    if (!IsUseOfName(argNode, name))
        return ArgFail(f, name, stmt);

    ParseNode* coercedExpr;
    if (!CheckTypeAnnotation(f.m(), coercionNode, type, &coercedExpr))
        return false;

    if (!IsUseOfName(coercedExpr, name))
        return ArgFail(f, name, stmt);

    return true;
}

static bool
CheckProcessingDirectives(ModuleValidator& m, ParseNode** stmtIter)
{
    ParseNode* stmt = *stmtIter;

    while (stmt && IsIgnoredDirective(m.cx(), stmt))
        stmt = NextNode(stmt);

    *stmtIter = stmt;
    return true;
}

static bool
CheckArguments(FunctionValidator& f, ParseNode** stmtIter, MallocSig::ArgVector* argTypes)
{
    ParseNode* stmt = *stmtIter;

    unsigned numFormals;
    ParseNode* argpn = FunctionArgsList(f.fn(), &numFormals);

    for (unsigned i = 0; i < numFormals; i++, argpn = NextNode(argpn), stmt = NextNode(stmt)) {
        PropertyName* name;
        if (!CheckArgument(f.m(), argpn, &name))
            return false;

        ValType type;
        if (!CheckArgumentType(f, stmt, name, &type))
            return false;

        if (!argTypes->append(type))
            return false;

        if (!f.addFormal(argpn, name, type))
            return false;
    }

    *stmtIter = stmt;
    return true;
}

static bool
IsLiteralOrConst(FunctionValidator& f, ParseNode* pn, NumLit* lit)
{
    if (pn->isKind(PNK_NAME)) {
        const ModuleValidator::Global* global = f.lookupGlobal(pn->name());
        if (!global || global->which() != ModuleValidator::Global::ConstantLiteral)
            return false;

        *lit = global->constLiteralValue();
        return true;
    }

    if (!IsNumericLiteral(f.m(), pn))
        return false;

    *lit = ExtractNumericLiteral(f.m(), pn);
    return true;
}

static bool
CheckFinalReturn(FunctionValidator& f, ParseNode* lastNonEmptyStmt)
{
    if (!f.hasAlreadyReturned()) {
        f.setReturnedType(ExprType::Void);
        f.writeOp(Stmt::Ret);
        return true;
    }

    if (!lastNonEmptyStmt->isKind(PNK_RETURN)) {
        if (!IsVoid(f.returnedType()))
            return f.fail(lastNonEmptyStmt, "void incompatible with previous return type");

        f.writeOp(Stmt::Ret);
        return true;
    }

    return true;
}

static bool
CheckVariable(FunctionValidator& f, ParseNode* var)
{
    if (!IsDefinition(var))
        return f.fail(var, "local variable names must not restate argument names");

    PropertyName* name = var->name();

    if (!CheckIdentifier(f.m(), var, name))
        return false;

    ParseNode* initNode = MaybeDefinitionInitializer(var);
    if (!initNode)
        return f.failName(var, "var '%s' needs explicit type declaration via an initial value", name);

    NumLit lit;
    if (!IsLiteralOrConst(f, initNode, &lit))
        return f.failName(var, "var '%s' initializer must be literal or const literal", name);

    if (!lit.valid())
        return f.failName(var, "var '%s' initializer out of range", name);

    return f.addVariable(var, name, lit);
}

static bool
CheckVariables(FunctionValidator& f, ParseNode** stmtIter)
{
    ParseNode* stmt = *stmtIter;

    for (; stmt && stmt->isKind(PNK_VAR); stmt = NextNonEmptyStatement(stmt)) {
        for (ParseNode* var = VarListHead(stmt); var; var = NextNode(var)) {
            if (!CheckVariable(f, var))
                return false;
        }
    }

    *stmtIter = stmt;
    return true;
}

static bool
CheckExpr(FunctionValidator& f, ParseNode* expr, Type* type);

static bool
CheckNumericLiteral(FunctionValidator& f, ParseNode* num, Type* type)
{
    NumLit lit = ExtractNumericLiteral(f.m(), num);
    if (!lit.valid())
        return f.fail(num, "numeric literal out of representable integer range");
    f.writeLit(lit);
    *type = Type::lit(lit);
    return true;
}

static bool
CheckVarRef(FunctionValidator& f, ParseNode* varRef, Type* type)
{
    PropertyName* name = varRef->name();

    if (const FunctionValidator::Local* local = f.lookupLocal(name)) {
        switch (local->type) {
          case ValType::I32:    f.writeOp(I32::GetLocal);   break;
          case ValType::I64:    MOZ_CRASH("no int64 in asm.js");
          case ValType::F32:    f.writeOp(F32::GetLocal);   break;
          case ValType::F64:    f.writeOp(F64::GetLocal);   break;
          case ValType::I32x4:  f.writeOp(I32X4::GetLocal); break;
          case ValType::F32x4:  f.writeOp(F32X4::GetLocal); break;
          case ValType::B32x4:  f.writeOp(B32X4::GetLocal); break;
        }
        f.writeU32(local->slot);
        *type = Type::var(local->type);
        return true;
    }

    if (const ModuleValidator::Global* global = f.lookupGlobal(name)) {
        switch (global->which()) {
          case ModuleValidator::Global::ConstantLiteral:
            f.writeLit(global->constLiteralValue());
            *type = global->varOrConstType();
            break;
          case ModuleValidator::Global::ConstantImport:
          case ModuleValidator::Global::Variable: {
            switch (global->varOrConstType().which()) {
              case Type::Int:       f.writeOp(I32::GetGlobal);   break;
              case Type::Double:    f.writeOp(F64::GetGlobal);   break;
              case Type::Float:     f.writeOp(F32::GetGlobal);   break;
              case Type::Int32x4:   f.writeOp(I32X4::GetGlobal); break;
              case Type::Float32x4: f.writeOp(F32X4::GetGlobal); break;
              case Type::Bool32x4:  f.writeOp(B32X4::GetGlobal); break;
              default: MOZ_CRASH("unexpected global type");
            }

            f.writeU32(global->varOrConstGlobalDataOffset());
            f.writeU8(uint8_t(global->isConst()));
            *type = global->varOrConstType();
            break;
          }
          case ModuleValidator::Global::Function:
          case ModuleValidator::Global::FFI:
          case ModuleValidator::Global::MathBuiltinFunction:
          case ModuleValidator::Global::AtomicsBuiltinFunction:
          case ModuleValidator::Global::FuncPtrTable:
          case ModuleValidator::Global::ArrayView:
          case ModuleValidator::Global::ArrayViewCtor:
          case ModuleValidator::Global::SimdCtor:
          case ModuleValidator::Global::SimdOperation:
          case ModuleValidator::Global::ByteLength:
          case ModuleValidator::Global::ChangeHeap:
            return f.failName(varRef, "'%s' may not be accessed by ordinary expressions", name);
        }
        return true;
    }

    return f.failName(varRef, "'%s' not found in local or asm.js module scope", name);
}

static inline bool
IsLiteralOrConstInt(FunctionValidator& f, ParseNode* pn, uint32_t* u32)
{
    NumLit lit;
    if (!IsLiteralOrConst(f, pn, &lit))
        return false;

    return IsLiteralInt(lit, u32);
}

static bool
FoldMaskedArrayIndex(FunctionValidator& f, ParseNode** indexExpr, int32_t* mask,
                     NeedsBoundsCheck* needsBoundsCheck)
{
    MOZ_ASSERT((*indexExpr)->isKind(PNK_BITAND));

    ParseNode* indexNode = BitwiseLeft(*indexExpr);
    ParseNode* maskNode = BitwiseRight(*indexExpr);

    uint32_t mask2;
    if (IsLiteralOrConstInt(f, maskNode, &mask2)) {
        // Flag the access to skip the bounds check if the mask ensures that an
        // 'out of bounds' access can not occur based on the current heap length
        // constraint. The unsigned maximum of a masked index is the mask
        // itself, so check that the mask is not negative and compare the mask
        // to the known minimum heap length.
        if (int32_t(mask2) >= 0 && mask2 < f.m().minHeapLength())
            *needsBoundsCheck = NO_BOUNDS_CHECK;
        *mask &= mask2;
        *indexExpr = indexNode;
        return true;
    }

    return false;
}

static const int32_t NoMask = -1;

static bool
CheckArrayAccess(FunctionValidator& f, ParseNode* viewName, ParseNode* indexExpr,
                 Scalar::Type* viewType, NeedsBoundsCheck* needsBoundsCheck, int32_t* mask)
{
    *needsBoundsCheck = NEEDS_BOUNDS_CHECK;

    if (!viewName->isKind(PNK_NAME))
        return f.fail(viewName, "base of array access must be a typed array view name");

    const ModuleValidator::Global* global = f.lookupGlobal(viewName->name());
    if (!global || !global->isAnyArrayView())
        return f.fail(viewName, "base of array access must be a typed array view name");

    *viewType = global->viewType();

    uint32_t index;
    if (IsLiteralOrConstInt(f, indexExpr, &index)) {
        uint64_t byteOffset = uint64_t(index) << TypedArrayShift(*viewType);
        if (byteOffset > INT32_MAX)
            return f.fail(indexExpr, "constant index out of range");

        unsigned elementSize = TypedArrayElemSize(*viewType);
        if (!f.m().tryRequireHeapLengthToBeAtLeast(byteOffset + elementSize)) {
            return f.failf(indexExpr, "constant index outside heap size range declared by the "
                                      "change-heap function (0x%x - 0x%x)",
                                      f.m().minHeapLength(), f.m().module().maxHeapLength());
        }

        *mask = NoMask;
        *needsBoundsCheck = NO_BOUNDS_CHECK;
        f.writeInt32Lit(byteOffset);
        return true;
    }

    // Mask off the low bits to account for the clearing effect of a right shift
    // followed by the left shift implicit in the array access. E.g., H32[i>>2]
    // loses the low two bits.
    *mask = ~(TypedArrayElemSize(*viewType) - 1);

    if (indexExpr->isKind(PNK_RSH)) {
        ParseNode* shiftAmountNode = BitwiseRight(indexExpr);

        uint32_t shift;
        if (!IsLiteralInt(f.m(), shiftAmountNode, &shift))
            return f.failf(shiftAmountNode, "shift amount must be constant");

        unsigned requiredShift = TypedArrayShift(*viewType);
        if (shift != requiredShift)
            return f.failf(shiftAmountNode, "shift amount must be %u", requiredShift);

        ParseNode* pointerNode = BitwiseLeft(indexExpr);

        if (pointerNode->isKind(PNK_BITAND))
            FoldMaskedArrayIndex(f, &pointerNode, mask, needsBoundsCheck);

        f.enterHeapExpression();

        Type pointerType;
        if (!CheckExpr(f, pointerNode, &pointerType))
            return false;

        f.leaveHeapExpression();

        if (!pointerType.isIntish())
            return f.failf(pointerNode, "%s is not a subtype of int", pointerType.toChars());
    } else {
        // For legacy compatibility, accept Int8/Uint8 accesses with no shift.
        if (TypedArrayShift(*viewType) != 0)
            return f.fail(indexExpr, "index expression isn't shifted; must be an Int8/Uint8 access");

        MOZ_ASSERT(*mask == NoMask);
        bool folded = false;

        ParseNode* pointerNode = indexExpr;

        if (pointerNode->isKind(PNK_BITAND))
            folded = FoldMaskedArrayIndex(f, &pointerNode, mask, needsBoundsCheck);

        f.enterHeapExpression();

        Type pointerType;
        if (!CheckExpr(f, pointerNode, &pointerType))
            return false;

        f.leaveHeapExpression();

        if (folded) {
            if (!pointerType.isIntish())
                return f.failf(pointerNode, "%s is not a subtype of intish", pointerType.toChars());
        } else {
            if (!pointerType.isInt())
                return f.failf(pointerNode, "%s is not a subtype of int", pointerType.toChars());
        }
    }

    return true;
}

static bool
CheckAndPrepareArrayAccess(FunctionValidator& f, ParseNode* viewName, ParseNode* indexExpr,
                           Scalar::Type* viewType, NeedsBoundsCheck* needsBoundsCheck, int32_t* mask)
{
    size_t prepareAt = f.tempOp();

    if (!CheckArrayAccess(f, viewName, indexExpr, viewType, needsBoundsCheck, mask))
        return false;

    // Don't generate the mask op if there is no need for it which could happen for
    // a shift of zero or a SIMD access.
    if (*mask != NoMask) {
        f.patchOp(prepareAt, I32::BitAnd);
        f.writeInt32Lit(*mask);
    } else {
        f.patchOp(prepareAt, I32::Id);
    }

    return true;
}

static bool
CheckLoadArray(FunctionValidator& f, ParseNode* elem, Type* type)
{
    Scalar::Type viewType;
    NeedsBoundsCheck needsBoundsCheck;
    int32_t mask;

    size_t opcodeAt = f.tempOp();
    size_t needsBoundsCheckAt = f.tempU8();

    if (!CheckAndPrepareArrayAccess(f, ElemBase(elem), ElemIndex(elem), &viewType, &needsBoundsCheck, &mask))
        return false;

    switch (viewType) {
      case Scalar::Int8:    f.patchOp(opcodeAt, I32::SLoad8);  break;
      case Scalar::Int16:   f.patchOp(opcodeAt, I32::SLoad16); break;
      case Scalar::Int32:   f.patchOp(opcodeAt, I32::SLoad32); break;
      case Scalar::Uint8:   f.patchOp(opcodeAt, I32::ULoad8);  break;
      case Scalar::Uint16:  f.patchOp(opcodeAt, I32::ULoad16); break;
      case Scalar::Uint32:  f.patchOp(opcodeAt, I32::ULoad32); break;
      case Scalar::Float32: f.patchOp(opcodeAt, F32::Load);    break;
      case Scalar::Float64: f.patchOp(opcodeAt, F64::Load);    break;
      default: MOZ_CRASH("unexpected scalar type");
    }

    f.patchU8(needsBoundsCheckAt, uint8_t(needsBoundsCheck));

    switch (viewType) {
      case Scalar::Int8:
      case Scalar::Int16:
      case Scalar::Int32:
      case Scalar::Uint8:
      case Scalar::Uint16:
      case Scalar::Uint32:
        *type = Type::Intish;
        break;
      case Scalar::Float32:
        *type = Type::MaybeFloat;
        break;
      case Scalar::Float64:
        *type = Type::MaybeDouble;
        break;
      default: MOZ_CRASH("Unexpected array type");
    }

    return true;
}

static bool
CheckStoreArray(FunctionValidator& f, ParseNode* lhs, ParseNode* rhs, Type* type)
{
    size_t opcodeAt = f.tempOp();
    size_t needsBoundsCheckAt = f.tempU8();

    Scalar::Type viewType;
    NeedsBoundsCheck needsBoundsCheck;
    int32_t mask;
    if (!CheckAndPrepareArrayAccess(f, ElemBase(lhs), ElemIndex(lhs), &viewType, &needsBoundsCheck, &mask))
        return false;

    f.enterHeapExpression();

    Type rhsType;
    if (!CheckExpr(f, rhs, &rhsType))
        return false;

    f.leaveHeapExpression();

    switch (viewType) {
      case Scalar::Int8:
      case Scalar::Int16:
      case Scalar::Int32:
      case Scalar::Uint8:
      case Scalar::Uint16:
      case Scalar::Uint32:
        if (!rhsType.isIntish())
            return f.failf(lhs, "%s is not a subtype of intish", rhsType.toChars());
        break;
      case Scalar::Float32:
        if (!rhsType.isMaybeDouble() && !rhsType.isFloatish())
            return f.failf(lhs, "%s is not a subtype of double? or floatish", rhsType.toChars());
        break;
      case Scalar::Float64:
        if (!rhsType.isMaybeFloat() && !rhsType.isMaybeDouble())
            return f.failf(lhs, "%s is not a subtype of float? or double?", rhsType.toChars());
        break;
      default:
        MOZ_CRASH("Unexpected view type");
    }

    switch (viewType) {
      case Scalar::Int8:
      case Scalar::Uint8:
        f.patchOp(opcodeAt, I32::Store8);
        break;
      case Scalar::Int16:
      case Scalar::Uint16:
        f.patchOp(opcodeAt, I32::Store16);
        break;
      case Scalar::Int32:
      case Scalar::Uint32:
        f.patchOp(opcodeAt, I32::Store32);
        break;
      case Scalar::Float32:
        if (rhsType.isFloatish())
            f.patchOp(opcodeAt, F32::StoreF32);
        else
            f.patchOp(opcodeAt, F64::StoreF32);
        break;
      case Scalar::Float64:
        if (rhsType.isFloatish())
            f.patchOp(opcodeAt, F32::StoreF64);
        else
            f.patchOp(opcodeAt, F64::StoreF64);
        break;
      default: MOZ_CRASH("unexpected scalar type");
    }

    f.patchU8(needsBoundsCheckAt, uint8_t(needsBoundsCheck));

    *type = rhsType;
    return true;
}

static bool
CheckAssignName(FunctionValidator& f, ParseNode* lhs, ParseNode* rhs, Type* type)
{
    RootedPropertyName name(f.cx(), lhs->name());

    size_t opcodeAt = f.tempOp();
    size_t indexAt = f.temp32();

    Type rhsType;
    if (!CheckExpr(f, rhs, &rhsType))
        return false;

    if (const FunctionValidator::Local* lhsVar = f.lookupLocal(name)) {
        if (!(rhsType <= lhsVar->type)) {
            return f.failf(lhs, "%s is not a subtype of %s",
                           rhsType.toChars(), Type::var(lhsVar->type).toChars());
        }

        switch (lhsVar->type) {
          case ValType::I32:    f.patchOp(opcodeAt, I32::SetLocal);   break;
          case ValType::I64:    MOZ_CRASH("no int64 in asm.js");
          case ValType::F64:    f.patchOp(opcodeAt, F64::SetLocal);   break;
          case ValType::F32:    f.patchOp(opcodeAt, F32::SetLocal);   break;
          case ValType::I32x4:  f.patchOp(opcodeAt, I32X4::SetLocal); break;
          case ValType::F32x4:  f.patchOp(opcodeAt, F32X4::SetLocal); break;
          case ValType::B32x4:  f.patchOp(opcodeAt, B32X4::SetLocal); break;
        }

        f.patch32(indexAt, lhsVar->slot);
        *type = rhsType;
        return true;
    }

    if (const ModuleValidator::Global* global = f.lookupGlobal(name)) {
        if (global->which() != ModuleValidator::Global::Variable)
            return f.failName(lhs, "'%s' is not a mutable variable", name);

        if (!(rhsType <= global->varOrConstType())) {
            return f.failf(lhs, "%s is not a subtype of %s",
                           rhsType.toChars(), global->varOrConstType().toChars());
        }

        switch (global->varOrConstType().which()) {
          case Type::Int:       f.patchOp(opcodeAt, I32::SetGlobal);   break;
          case Type::Float:     f.patchOp(opcodeAt, F32::SetGlobal);   break;
          case Type::Double:    f.patchOp(opcodeAt, F64::SetGlobal);   break;
          case Type::Int32x4:   f.patchOp(opcodeAt, I32X4::SetGlobal); break;
          case Type::Float32x4: f.patchOp(opcodeAt, F32X4::SetGlobal); break;
          case Type::Bool32x4:  f.patchOp(opcodeAt, B32X4::SetGlobal); break;
          default: MOZ_CRASH("unexpected global type");
        }

        f.patch32(indexAt, global->varOrConstGlobalDataOffset());
        *type = rhsType;
        return true;
    }

    return f.failName(lhs, "'%s' not found in local or asm.js module scope", name);
}

static bool
CheckAssign(FunctionValidator& f, ParseNode* assign, Type* type)
{
    MOZ_ASSERT(assign->isKind(PNK_ASSIGN));

    ParseNode* lhs = BinaryLeft(assign);
    ParseNode* rhs = BinaryRight(assign);

    if (lhs->getKind() == PNK_ELEM)
        return CheckStoreArray(f, lhs, rhs, type);

    if (lhs->getKind() == PNK_NAME)
        return CheckAssignName(f, lhs, rhs, type);

    return f.fail(assign, "left-hand side of assignment must be a variable or array access");
}

static bool
CheckMathIMul(FunctionValidator& f, ParseNode* call, Type* type)
{
    if (CallArgListLength(call) != 2)
        return f.fail(call, "Math.imul must be passed 2 arguments");

    ParseNode* lhs = CallArgList(call);
    ParseNode* rhs = NextNode(lhs);

    f.writeOp(I32::Mul);

    Type lhsType;
    if (!CheckExpr(f, lhs, &lhsType))
        return false;

    Type rhsType;
    if (!CheckExpr(f, rhs, &rhsType))
        return false;

    if (!lhsType.isIntish())
        return f.failf(lhs, "%s is not a subtype of intish", lhsType.toChars());
    if (!rhsType.isIntish())
        return f.failf(rhs, "%s is not a subtype of intish", rhsType.toChars());

    *type = Type::Signed;
    return true;
}

static bool
CheckMathClz32(FunctionValidator& f, ParseNode* call, Type* type)
{
    if (CallArgListLength(call) != 1)
        return f.fail(call, "Math.clz32 must be passed 1 argument");

    f.writeOp(I32::Clz);

    ParseNode* arg = CallArgList(call);

    Type argType;
    if (!CheckExpr(f, arg, &argType))
        return false;

    if (!argType.isIntish())
        return f.failf(arg, "%s is not a subtype of intish", argType.toChars());

    *type = Type::Fixnum;
    return true;
}

static bool
CheckMathAbs(FunctionValidator& f, ParseNode* call, Type* type)
{
    if (CallArgListLength(call) != 1)
        return f.fail(call, "Math.abs must be passed 1 argument");

    ParseNode* arg = CallArgList(call);

    size_t opcodeAt = f.tempOp();

    Type argType;
    if (!CheckExpr(f, arg, &argType))
        return false;

    if (argType.isSigned()) {
        f.patchOp(opcodeAt, I32::Abs);
        *type = Type::Unsigned;
        return true;
    }

    if (argType.isMaybeDouble()) {
        f.patchOp(opcodeAt, F64::Abs);
        *type = Type::Double;
        return true;
    }

    if (argType.isMaybeFloat()) {
        f.patchOp(opcodeAt, F32::Abs);
        *type = Type::Floatish;
        return true;
    }

    return f.failf(call, "%s is not a subtype of signed, float? or double?", argType.toChars());
}

static bool
CheckMathSqrt(FunctionValidator& f, ParseNode* call, Type* type)
{
    if (CallArgListLength(call) != 1)
        return f.fail(call, "Math.sqrt must be passed 1 argument");

    ParseNode* arg = CallArgList(call);

    size_t opcodeAt = f.tempOp();

    Type argType;
    if (!CheckExpr(f, arg, &argType))
        return false;

    if (argType.isMaybeDouble()) {
        f.patchOp(opcodeAt, F64::Sqrt);
        *type = Type::Double;
        return true;
    }

    if (argType.isMaybeFloat()) {
        f.patchOp(opcodeAt, F32::Sqrt);
        *type = Type::Floatish;
        return true;
    }

    return f.failf(call, "%s is neither a subtype of double? nor float?", argType.toChars());
}

static bool
CheckMathMinMax(FunctionValidator& f, ParseNode* callNode, bool isMax, Type* type)
{
    if (CallArgListLength(callNode) < 2)
        return f.fail(callNode, "Math.min/max must be passed at least 2 arguments");

    size_t opcodeAt = f.tempOp();
    size_t numArgsAt = f.tempU8();

    ParseNode* firstArg = CallArgList(callNode);
    Type firstType;
    if (!CheckExpr(f, firstArg, &firstType))
        return false;

    if (firstType.isMaybeDouble()) {
        *type = Type::Double;
        firstType = Type::MaybeDouble;
        f.patchOp(opcodeAt, isMax ? F64::Max : F64::Min);
    } else if (firstType.isMaybeFloat()) {
        *type = Type::Float;
        firstType = Type::MaybeFloat;
        f.patchOp(opcodeAt, isMax ? F32::Max : F32::Min);
    } else if (firstType.isSigned()) {
        *type = Type::Signed;
        firstType = Type::Signed;
        f.patchOp(opcodeAt, isMax ? I32::Max : I32::Min);
    } else {
        return f.failf(firstArg, "%s is not a subtype of double?, float? or signed",
                       firstType.toChars());
    }

    unsigned numArgs = CallArgListLength(callNode);
    f.patchU8(numArgsAt, numArgs);

    ParseNode* nextArg = NextNode(firstArg);
    for (unsigned i = 1; i < numArgs; i++, nextArg = NextNode(nextArg)) {
        Type nextType;
        if (!CheckExpr(f, nextArg, &nextType))
            return false;
        if (!(nextType <= firstType))
            return f.failf(nextArg, "%s is not a subtype of %s", nextType.toChars(), firstType.toChars());
    }

    return true;
}

static bool
CheckSharedArrayAtomicAccess(FunctionValidator& f, ParseNode* viewName, ParseNode* indexExpr,
                             Scalar::Type* viewType, NeedsBoundsCheck* needsBoundsCheck,
                             int32_t* mask)
{
    if (!CheckAndPrepareArrayAccess(f, viewName, indexExpr, viewType, needsBoundsCheck, mask))
        return false;

    // Atomic accesses may be made on shared integer arrays only.

    // The global will be sane, CheckArrayAccess checks it.
    const ModuleValidator::Global* global = f.lookupGlobal(viewName->name());
    if (global->which() != ModuleValidator::Global::ArrayView || !f.m().module().isSharedView())
        return f.fail(viewName, "base of array access must be a shared typed array view name");

    switch (*viewType) {
      case Scalar::Int8:
      case Scalar::Int16:
      case Scalar::Int32:
      case Scalar::Uint8:
      case Scalar::Uint16:
      case Scalar::Uint32:
        return true;
      default:
        return f.failf(viewName, "not an integer array");
    }

    return true;
}

static bool
CheckAtomicsFence(FunctionValidator& f, ParseNode* call, Type* type)
{
    if (CallArgListLength(call) != 0)
        return f.fail(call, "Atomics.fence must be passed 0 arguments");

    f.writeOp(Stmt::AtomicsFence);
    *type = Type::Void;
    return true;
}

static bool
CheckAtomicsLoad(FunctionValidator& f, ParseNode* call, Type* type)
{
    if (CallArgListLength(call) != 2)
        return f.fail(call, "Atomics.load must be passed 2 arguments");

    ParseNode* arrayArg = CallArgList(call);
    ParseNode* indexArg = NextNode(arrayArg);

    f.writeOp(I32::AtomicsLoad);
    size_t needsBoundsCheckAt = f.tempU8();
    size_t viewTypeAt = f.tempU8();

    Scalar::Type viewType;
    NeedsBoundsCheck needsBoundsCheck;
    int32_t mask;
    if (!CheckSharedArrayAtomicAccess(f, arrayArg, indexArg, &viewType, &needsBoundsCheck, &mask))
        return false;

    f.patchU8(needsBoundsCheckAt, uint8_t(needsBoundsCheck));
    f.patchU8(viewTypeAt, uint8_t(viewType));

    *type = Type::Int;
    return true;
}

static bool
CheckAtomicsStore(FunctionValidator& f, ParseNode* call, Type* type)
{
    if (CallArgListLength(call) != 3)
        return f.fail(call, "Atomics.store must be passed 3 arguments");

    ParseNode* arrayArg = CallArgList(call);
    ParseNode* indexArg = NextNode(arrayArg);
    ParseNode* valueArg = NextNode(indexArg);

    f.writeOp(I32::AtomicsStore);
    size_t needsBoundsCheckAt = f.tempU8();
    size_t viewTypeAt = f.tempU8();

    Scalar::Type viewType;
    NeedsBoundsCheck needsBoundsCheck;
    int32_t mask;
    if (!CheckSharedArrayAtomicAccess(f, arrayArg, indexArg, &viewType, &needsBoundsCheck, &mask))
        return false;

    Type rhsType;
    if (!CheckExpr(f, valueArg, &rhsType))
        return false;

    if (!rhsType.isIntish())
        return f.failf(arrayArg, "%s is not a subtype of intish", rhsType.toChars());

    f.patchU8(needsBoundsCheckAt, uint8_t(needsBoundsCheck));
    f.patchU8(viewTypeAt, uint8_t(viewType));

    *type = rhsType;
    return true;
}

static bool
CheckAtomicsBinop(FunctionValidator& f, ParseNode* call, Type* type, AtomicOp op)
{
    if (CallArgListLength(call) != 3)
        return f.fail(call, "Atomics binary operator must be passed 3 arguments");

    ParseNode* arrayArg = CallArgList(call);
    ParseNode* indexArg = NextNode(arrayArg);
    ParseNode* valueArg = NextNode(indexArg);

    f.writeOp(I32::AtomicsBinOp);
    size_t needsBoundsCheckAt = f.tempU8();
    size_t viewTypeAt = f.tempU8();
    f.writeU8(uint8_t(op));

    Scalar::Type viewType;
    NeedsBoundsCheck needsBoundsCheck;
    int32_t mask;
    if (!CheckSharedArrayAtomicAccess(f, arrayArg, indexArg, &viewType, &needsBoundsCheck, &mask))
        return false;

    Type valueArgType;
    if (!CheckExpr(f, valueArg, &valueArgType))
        return false;

    if (!valueArgType.isIntish())
        return f.failf(valueArg, "%s is not a subtype of intish", valueArgType.toChars());

    f.patchU8(needsBoundsCheckAt, uint8_t(needsBoundsCheck));
    f.patchU8(viewTypeAt, uint8_t(viewType));

    *type = Type::Int;
    return true;
}

static bool
CheckAtomicsIsLockFree(FunctionValidator& f, ParseNode* call, Type* type)
{
    if (CallArgListLength(call) != 1)
        return f.fail(call, "Atomics.isLockFree must be passed 1 argument");

    ParseNode* sizeArg = CallArgList(call);

    uint32_t size;
    if (!IsLiteralInt(f.m(), sizeArg, &size))
        return f.fail(sizeArg, "Atomics.isLockFree requires an integer literal argument");

    f.writeInt32Lit(AtomicOperations::isLockfree(size));
    *type = Type::Int;
    return true;
}

static bool
CheckAtomicsCompareExchange(FunctionValidator& f, ParseNode* call, Type* type)
{
    if (CallArgListLength(call) != 4)
        return f.fail(call, "Atomics.compareExchange must be passed 4 arguments");

    ParseNode* arrayArg = CallArgList(call);
    ParseNode* indexArg = NextNode(arrayArg);
    ParseNode* oldValueArg = NextNode(indexArg);
    ParseNode* newValueArg = NextNode(oldValueArg);

    f.writeOp(I32::AtomicsCompareExchange);
    size_t needsBoundsCheckAt = f.tempU8();
    size_t viewTypeAt = f.tempU8();

    Scalar::Type viewType;
    NeedsBoundsCheck needsBoundsCheck;
    int32_t mask;
    if (!CheckSharedArrayAtomicAccess(f, arrayArg, indexArg, &viewType, &needsBoundsCheck, &mask))
        return false;

    Type oldValueArgType;
    if (!CheckExpr(f, oldValueArg, &oldValueArgType))
        return false;

    Type newValueArgType;
    if (!CheckExpr(f, newValueArg, &newValueArgType))
        return false;

    if (!oldValueArgType.isIntish())
        return f.failf(oldValueArg, "%s is not a subtype of intish", oldValueArgType.toChars());

    if (!newValueArgType.isIntish())
        return f.failf(newValueArg, "%s is not a subtype of intish", newValueArgType.toChars());

    f.patchU8(needsBoundsCheckAt, uint8_t(needsBoundsCheck));
    f.patchU8(viewTypeAt, uint8_t(viewType));

    *type = Type::Int;
    return true;
}

static bool
CheckAtomicsExchange(FunctionValidator& f, ParseNode* call, Type* type)
{
    if (CallArgListLength(call) != 3)
        return f.fail(call, "Atomics.exchange must be passed 3 arguments");

    ParseNode* arrayArg = CallArgList(call);
    ParseNode* indexArg = NextNode(arrayArg);
    ParseNode* valueArg = NextNode(indexArg);

    f.writeOp(I32::AtomicsExchange);
    size_t needsBoundsCheckAt = f.tempU8();
    size_t viewTypeAt = f.tempU8();

    Scalar::Type viewType;
    NeedsBoundsCheck needsBoundsCheck;
    int32_t mask;
    if (!CheckSharedArrayAtomicAccess(f, arrayArg, indexArg, &viewType, &needsBoundsCheck, &mask))
        return false;

    Type valueArgType;
    if (!CheckExpr(f, valueArg, &valueArgType))
        return false;

    if (!valueArgType.isIntish())
        return f.failf(arrayArg, "%s is not a subtype of intish", valueArgType.toChars());

    f.patchU8(needsBoundsCheckAt, uint8_t(needsBoundsCheck));
    f.patchU8(viewTypeAt, uint8_t(viewType));

    *type = Type::Int;
    return true;
}

static bool
CheckAtomicsBuiltinCall(FunctionValidator& f, ParseNode* callNode, AsmJSAtomicsBuiltinFunction func,
                        Type* type)
{
    switch (func) {
      case AsmJSAtomicsBuiltin_compareExchange:
        return CheckAtomicsCompareExchange(f, callNode, type);
      case AsmJSAtomicsBuiltin_exchange:
        return CheckAtomicsExchange(f, callNode, type);
      case AsmJSAtomicsBuiltin_load:
        return CheckAtomicsLoad(f, callNode, type);
      case AsmJSAtomicsBuiltin_store:
        return CheckAtomicsStore(f, callNode, type);
      case AsmJSAtomicsBuiltin_fence:
        return CheckAtomicsFence(f, callNode, type);
      case AsmJSAtomicsBuiltin_add:
        return CheckAtomicsBinop(f, callNode, type, AtomicFetchAddOp);
      case AsmJSAtomicsBuiltin_sub:
        return CheckAtomicsBinop(f, callNode, type, AtomicFetchSubOp);
      case AsmJSAtomicsBuiltin_and:
        return CheckAtomicsBinop(f, callNode, type, AtomicFetchAndOp);
      case AsmJSAtomicsBuiltin_or:
        return CheckAtomicsBinop(f, callNode, type, AtomicFetchOrOp);
      case AsmJSAtomicsBuiltin_xor:
        return CheckAtomicsBinop(f, callNode, type, AtomicFetchXorOp);
      case AsmJSAtomicsBuiltin_isLockFree:
        return CheckAtomicsIsLockFree(f, callNode, type);
      default:
        MOZ_CRASH("unexpected atomicsBuiltin function");
    }
}

typedef bool (*CheckArgType)(FunctionValidator& f, ParseNode* argNode, Type type);

template <CheckArgType checkArg>
static bool
CheckCallArgs(FunctionValidator& f, ParseNode* callNode, MallocSig::ArgVector* args)
{
    ParseNode* argNode = CallArgList(callNode);
    for (unsigned i = 0; i < CallArgListLength(callNode); i++, argNode = NextNode(argNode)) {
        Type type;
        if (!CheckExpr(f, argNode, &type))
            return false;

        if (!checkArg(f, argNode, type))
            return false;

        if (!args->append(type.checkedValueType()))
            return false;
    }
    return true;
}

template <class SigT>
static bool
CheckSignatureAgainstExisting(ModuleValidator& m, ParseNode* usepn, SigT& sig,
                              const LifoSig& existing)
{
    if (sig.args().length() != existing.args().length()) {
        return m.failf(usepn, "incompatible number of arguments (%u here vs. %u before)",
                       sig.args().length(), existing.args().length());
    }

    for (unsigned i = 0; i < sig.args().length(); i++) {
        if (sig.arg(i) != existing.arg(i)) {
            return m.failf(usepn, "incompatible type for argument %u: (%s here vs. %s before)",
                           i, Type::var(sig.arg(i)).toChars(), Type::var(existing.arg(i)).toChars());
        }
    }

    if (sig.ret() != existing.ret()) {
        return m.failf(usepn, "%s incompatible with previous return of type %s",
                       Type::ret(sig.ret()).toChars(), Type::ret(existing.ret()).toChars());
    }

    MOZ_ASSERT(sig == existing);
    return true;
}

static bool
CheckFunctionSignature(ModuleValidator& m, ParseNode* usepn, const MallocSig& sig,
                       PropertyName* name, ModuleValidator::Func** func)
{
    ModuleValidator::Func* existing = m.lookupFunction(name);
    if (!existing) {
        if (!CheckModuleLevelName(m, usepn, name))
            return false;
        return m.addFunction(name, usepn->pn_pos.begin, sig, func);
    }

    if (!CheckSignatureAgainstExisting(m, usepn, sig, existing->sig()))
        return false;

    *func = existing;
    return true;
}

static bool
CheckIsVarType(FunctionValidator& f, ParseNode* argNode, Type type)
{
    if (!type.isVarType())
        return f.failf(argNode, "%s is not a subtype of int, float or double", type.toChars());
    return true;
}

static void
WriteCallLineCol(FunctionValidator& f, ParseNode* pn)
{
    uint32_t line, column;
    f.m().tokenStream().srcCoords.lineNumAndColumnIndex(pn->pn_pos.begin, &line, &column);
    f.writeU32(line);
    f.writeU32(column);
}

static bool
CheckInternalCall(FunctionValidator& f, ParseNode* callNode, PropertyName* calleeName,
                  ExprType ret, Type* type)
{
    if (!f.canCall()) {
        return f.fail(callNode, "call expressions may not be nested inside heap expressions "
                                "when the module contains a change-heap function");
    }

    switch (ret) {
      case ExprType::Void:   f.writeOp(Stmt::CallInternal);  break;
      case ExprType::I32:    f.writeOp(I32::CallInternal);   break;
      case ExprType::I64:    MOZ_CRASH("no int64 in asm.js");
      case ExprType::F32:    f.writeOp(F32::CallInternal);   break;
      case ExprType::F64:    f.writeOp(F64::CallInternal);   break;
      case ExprType::I32x4:  f.writeOp(I32X4::CallInternal); break;
      case ExprType::F32x4:  f.writeOp(F32X4::CallInternal); break;
      case ExprType::B32x4:  f.writeOp(B32X4::CallInternal); break;
    }

    // Function's index, to find out the function's entry
    size_t funcIndexAt = f.temp32();
    // Function's signature in lifo
    size_t sigAt = f.tempPtr();
    // Call node position (asm.js specific)
    WriteCallLineCol(f, callNode);

    MallocSig::ArgVector args;
    if (!CheckCallArgs<CheckIsVarType>(f, callNode, &args))
        return false;

    MallocSig sig(Move(args), ret);

    ModuleValidator::Func* callee;
    if (!CheckFunctionSignature(f.m(), callNode, sig, calleeName, &callee))
        return false;

    f.patch32(funcIndexAt, callee->index());
    f.patchSig(sigAt, &callee->sig());
    *type = Type::ret(ret);
    return true;
}

template <class SigT>
static bool
CheckFuncPtrTableAgainstExisting(ModuleValidator& m, ParseNode* usepn, PropertyName* name,
                                 SigT& sig, unsigned mask, uint32_t* funcPtrTableIndex)
{
    if (const ModuleValidator::Global* existing = m.lookupGlobal(name)) {
        if (existing->which() != ModuleValidator::Global::FuncPtrTable)
            return m.failName(usepn, "'%s' is not a function-pointer table", name);

        ModuleValidator::FuncPtrTable& table = m.funcPtrTable(existing->funcPtrTableIndex());
        if (mask != table.mask())
            return m.failf(usepn, "mask does not match previous value (%u)", table.mask());

        if (!CheckSignatureAgainstExisting(m, usepn, sig, table.sig()))
            return false;

        *funcPtrTableIndex = existing->funcPtrTableIndex();
        return true;
    }

    if (!CheckModuleLevelName(m, usepn, name))
        return false;

    if (!m.declareFuncPtrTable(name, usepn->pn_pos.begin, sig, mask, funcPtrTableIndex))
        return m.fail(usepn, "table too big");

    return true;
}

static bool
CheckFuncPtrCall(FunctionValidator& f, ParseNode* callNode, ExprType ret, Type* type)
{
    if (!f.canCall()) {
        return f.fail(callNode, "function-pointer call expressions may not be nested inside heap "
                                "expressions when the module contains a change-heap function");
    }

    ParseNode* callee = CallCallee(callNode);
    ParseNode* tableNode = ElemBase(callee);
    ParseNode* indexExpr = ElemIndex(callee);

    if (!tableNode->isKind(PNK_NAME))
        return f.fail(tableNode, "expecting name of function-pointer array");

    PropertyName* name = tableNode->name();
    if (const ModuleValidator::Global* existing = f.lookupGlobal(name)) {
        if (existing->which() != ModuleValidator::Global::FuncPtrTable)
            return f.failName(tableNode, "'%s' is not the name of a function-pointer array", name);
    }

    if (!indexExpr->isKind(PNK_BITAND))
        return f.fail(indexExpr, "function-pointer table index expression needs & mask");

    ParseNode* indexNode = BitwiseLeft(indexExpr);
    ParseNode* maskNode = BitwiseRight(indexExpr);

    uint32_t mask;
    if (!IsLiteralInt(f.m(), maskNode, &mask) || mask == UINT32_MAX || !IsPowerOfTwo(mask + 1))
        return f.fail(maskNode, "function-pointer table index mask value must be a power of two minus 1");

    // Opcode
    switch (ret) {
      case ExprType::Void:   f.writeOp(Stmt::CallIndirect);  break;
      case ExprType::I32:    f.writeOp(I32::CallIndirect);   break;
      case ExprType::I64:    MOZ_CRASH("no in64 in asm.js");
      case ExprType::F32:    f.writeOp(F32::CallIndirect);   break;
      case ExprType::F64:    f.writeOp(F64::CallIndirect);   break;
      case ExprType::I32x4:  f.writeOp(I32X4::CallIndirect); break;
      case ExprType::F32x4:  f.writeOp(F32X4::CallIndirect); break;
      case ExprType::B32x4:  f.writeOp(B32X4::CallIndirect); break;
    }

    // Table's mask
    f.writeU32(mask);
    // Global data offset
    size_t globalDataOffsetAt = f.temp32();
    // Signature
    size_t sigAt = f.tempPtr();
    // Call node position (asm.js specific)
    WriteCallLineCol(f, callNode);

    Type indexType;
    if (!CheckExpr(f, indexNode, &indexType))
        return false;

    if (!indexType.isIntish())
        return f.failf(indexNode, "%s is not a subtype of intish", indexType.toChars());

    MallocSig::ArgVector args;
    if (!CheckCallArgs<CheckIsVarType>(f, callNode, &args))
        return false;

    MallocSig sig(Move(args), ret);

    uint32_t funcPtrTableIndex;
    if (!CheckFuncPtrTableAgainstExisting(f.m(), tableNode, name, sig, mask, &funcPtrTableIndex))
        return false;

    f.patch32(globalDataOffsetAt, f.m().mg().funcPtrTableGlobalDataOffset(funcPtrTableIndex));
    f.patchSig(sigAt, &f.m().funcPtrTable(funcPtrTableIndex).sig());

    *type = Type::ret(ret);
    return true;
}

static bool
CheckIsExternType(FunctionValidator& f, ParseNode* argNode, Type type)
{
    if (!type.isExtern())
        return f.failf(argNode, "%s is not a subtype of extern", type.toChars());
    return true;
}

static bool
CheckFFICall(FunctionValidator& f, ParseNode* callNode, unsigned ffiIndex, ExprType ret,
             Type* type)
{
    if (!f.canCall()) {
        return f.fail(callNode, "FFI call expressions may not be nested inside heap "
                                "expressions when the module contains a change-heap function");
    }

    PropertyName* calleeName = CallCallee(callNode)->name();

    if (ret == ExprType::F32)
        return f.fail(callNode, "FFI calls can't return float");
    if (IsSimdType(ret))
        return f.fail(callNode, "FFI calls can't return SIMD values");

    switch (ret) {
      case ExprType::Void:   f.writeOp(Stmt::CallImport);  break;
      case ExprType::I32:    f.writeOp(I32::CallImport);   break;
      case ExprType::I64:    MOZ_CRASH("no int64 in asm.js");
      case ExprType::F32:    f.writeOp(F32::CallImport);   break;
      case ExprType::F64:    f.writeOp(F64::CallImport);   break;
      case ExprType::I32x4:  f.writeOp(I32X4::CallImport); break;
      case ExprType::F32x4:  f.writeOp(F32X4::CallImport); break;
      case ExprType::B32x4:  f.writeOp(B32X4::CallImport); break;
    }

    // Global data offset
    size_t offsetAt = f.temp32();
    // Pointer to the import's signature in the module's lifo
    size_t sigAt = f.tempPtr();
    // Call node position (asm.js specific)
    WriteCallLineCol(f, callNode);

    MallocSig::ArgVector args;
    if (!CheckCallArgs<CheckIsExternType>(f, callNode, &args))
        return false;

    MallocSig sig(Move(args), ret);

    unsigned importIndex = 0;
    const LifoSig* lifoSig = nullptr;
    if (!f.m().addImport(calleeName, Move(sig), ffiIndex, &importIndex, &lifoSig))
        return false;

    f.patch32(offsetAt, f.m().mg().importExitGlobalDataOffset(importIndex));
    f.patchSig(sigAt, lifoSig);
    *type = Type::ret(ret);
    return true;
}

static bool
CheckFloatCoercionArg(FunctionValidator& f, ParseNode* inputNode, Type inputType,
                      size_t opcodeAt)
{
    if (inputType.isMaybeDouble()) {
        f.patchOp(opcodeAt, F32::FromF64);
        return true;
    }
    if (inputType.isSigned()) {
        f.patchOp(opcodeAt, F32::FromS32);
        return true;
    }
    if (inputType.isUnsigned()) {
        f.patchOp(opcodeAt, F32::FromU32);
        return true;
    }
    if (inputType.isFloatish()) {
        f.patchOp(opcodeAt, F32::Id);
        return true;
    }

    return f.failf(inputNode, "%s is not a subtype of signed, unsigned, double? or floatish",
                   inputType.toChars());
}

static bool
CheckCoercedCall(FunctionValidator& f, ParseNode* call, ExprType ret, Type* type);

static bool
CheckCoercionArg(FunctionValidator& f, ParseNode* arg, ValType expected, Type* type)
{
    ExprType ret = ToExprType(expected);
    if (arg->isKind(PNK_CALL))
        return CheckCoercedCall(f, arg, ret, type);

    size_t opcodeAt = f.tempOp();

    Type argType;
    if (!CheckExpr(f, arg, &argType))
        return false;

    switch (expected) {
      case ValType::F32:
        if (!CheckFloatCoercionArg(f, arg, argType, opcodeAt))
            return false;
        break;
      case ValType::I64:
        MOZ_CRASH("no int64 in asm.js");
      case ValType::I32x4:
        if (!argType.isInt32x4())
            return f.fail(arg, "argument to SIMD int32x4 coercion isn't int32x4");
        f.patchOp(opcodeAt, I32X4::Id);
        break;
      case ValType::F32x4:
        if (!argType.isFloat32x4())
            return f.fail(arg, "argument to SIMD float32x4 coercion isn't float32x4");
        f.patchOp(opcodeAt, F32X4::Id);
        break;
      case ValType::B32x4:
        if (!argType.isBool32x4())
            return f.fail(arg, "argument to SIMD bool32x4 coercion isn't bool32x4");
        f.patchOp(opcodeAt, B32X4::Id);
        break;
      case ValType::I32:
      case ValType::F64:
        MOZ_CRASH("not call coercions");
    }

    *type = Type::ret(ret);
    return true;
}

static bool
CheckMathFRound(FunctionValidator& f, ParseNode* callNode, Type* type)
{
    if (CallArgListLength(callNode) != 1)
        return f.fail(callNode, "Math.fround must be passed 1 argument");

    ParseNode* argNode = CallArgList(callNode);
    Type argType;
    if (!CheckCoercionArg(f, argNode, ValType::F32, &argType))
        return false;

    MOZ_ASSERT(argType == Type::Float);
    *type = Type::Float;
    return true;
}

static bool
CheckMathBuiltinCall(FunctionValidator& f, ParseNode* callNode, AsmJSMathBuiltinFunction func,
                     Type* type)
{
    unsigned arity = 0;
    F32 f32;
    F64 f64;
    switch (func) {
      case AsmJSMathBuiltin_imul:   return CheckMathIMul(f, callNode, type);
      case AsmJSMathBuiltin_clz32:  return CheckMathClz32(f, callNode, type);
      case AsmJSMathBuiltin_abs:    return CheckMathAbs(f, callNode, type);
      case AsmJSMathBuiltin_sqrt:   return CheckMathSqrt(f, callNode, type);
      case AsmJSMathBuiltin_fround: return CheckMathFRound(f, callNode, type);
      case AsmJSMathBuiltin_min:    return CheckMathMinMax(f, callNode, /* isMax = */ false, type);
      case AsmJSMathBuiltin_max:    return CheckMathMinMax(f, callNode, /* isMax = */ true, type);
      case AsmJSMathBuiltin_ceil:   arity = 1; f64 = F64::Ceil;  f32 = F32::Ceil;  break;
      case AsmJSMathBuiltin_floor:  arity = 1; f64 = F64::Floor; f32 = F32::Floor; break;
      case AsmJSMathBuiltin_sin:    arity = 1; f64 = F64::Sin;   f32 = F32::Bad;   break;
      case AsmJSMathBuiltin_cos:    arity = 1; f64 = F64::Cos;   f32 = F32::Bad;   break;
      case AsmJSMathBuiltin_tan:    arity = 1; f64 = F64::Tan;   f32 = F32::Bad;   break;
      case AsmJSMathBuiltin_asin:   arity = 1; f64 = F64::Asin;  f32 = F32::Bad;   break;
      case AsmJSMathBuiltin_acos:   arity = 1; f64 = F64::Acos;  f32 = F32::Bad;   break;
      case AsmJSMathBuiltin_atan:   arity = 1; f64 = F64::Atan;  f32 = F32::Bad;   break;
      case AsmJSMathBuiltin_exp:    arity = 1; f64 = F64::Exp;   f32 = F32::Bad;   break;
      case AsmJSMathBuiltin_log:    arity = 1; f64 = F64::Log;   f32 = F32::Bad;   break;
      case AsmJSMathBuiltin_pow:    arity = 2; f64 = F64::Pow;   f32 = F32::Bad;   break;
      case AsmJSMathBuiltin_atan2:  arity = 2; f64 = F64::Atan2; f32 = F32::Bad;   break;
      default: MOZ_CRASH("unexpected mathBuiltin function");
    }

    unsigned actualArity = CallArgListLength(callNode);
    if (actualArity != arity)
        return f.failf(callNode, "call passed %u arguments, expected %u", actualArity, arity);

    size_t opcodeAt = f.tempOp();
    // Call node position (asm.js specific)
    WriteCallLineCol(f, callNode);

    Type firstType;
    ParseNode* argNode = CallArgList(callNode);
    if (!CheckExpr(f, argNode, &firstType))
        return false;

    if (!firstType.isMaybeFloat() && !firstType.isMaybeDouble())
        return f.fail(argNode, "arguments to math call should be a subtype of double? or float?");

    bool opIsDouble = firstType.isMaybeDouble();
    if (!opIsDouble && f32 == F32::Bad)
        return f.fail(callNode, "math builtin cannot be used as float");

    if (opIsDouble)
        f.patchOp(opcodeAt, f64);
    else
        f.patchOp(opcodeAt, f32);

    if (arity == 2) {
        Type secondType;
        argNode = NextNode(argNode);
        if (!CheckExpr(f, argNode, &secondType))
            return false;

        if (firstType.isMaybeDouble() && !secondType.isMaybeDouble())
            return f.fail(argNode, "both arguments to math builtin call should be the same type");
        if (firstType.isMaybeFloat() && !secondType.isMaybeFloat())
            return f.fail(argNode, "both arguments to math builtin call should be the same type");
    }

    *type = opIsDouble ? Type::Double : Type::Floatish;
    return true;
}

namespace {
// Include CheckSimdCallArgs in unnamed namespace to avoid MSVC name lookup bug.

template<class CheckArgOp>
static bool
CheckSimdCallArgs(FunctionValidator& f, ParseNode* call, unsigned expectedArity,
                  const CheckArgOp& checkArg)
{
    unsigned numArgs = CallArgListLength(call);
    if (numArgs != expectedArity)
        return f.failf(call, "expected %u arguments to SIMD call, got %u", expectedArity, numArgs);

    ParseNode* arg = CallArgList(call);
    for (size_t i = 0; i < numArgs; i++, arg = NextNode(arg)) {
        MOZ_ASSERT(!!arg);
        Type argType;
        if (!CheckExpr(f, arg, &argType))
            return false;
        if (!checkArg(f, arg, i, argType))
            return false;
    }

    return true;
}

template<class CheckArgOp>
static bool
CheckSimdCallArgsPatchable(FunctionValidator& f, ParseNode* call, unsigned expectedArity,
                           const CheckArgOp& checkArg)
{
    unsigned numArgs = CallArgListLength(call);
    if (numArgs != expectedArity)
        return f.failf(call, "expected %u arguments to SIMD call, got %u", expectedArity, numArgs);

    ParseNode* arg = CallArgList(call);
    for (size_t i = 0; i < numArgs; i++, arg = NextNode(arg)) {
        MOZ_ASSERT(!!arg);
        Type argType;
        size_t patchAt = f.tempOp();
        if (!CheckExpr(f, arg, &argType))
            return false;
        if (!checkArg(f, arg, i, argType, patchAt))
            return false;
    }

    return true;
}


class CheckArgIsSubtypeOf
{
    Type formalType_;

  public:
    explicit CheckArgIsSubtypeOf(AsmJSSimdType t) : formalType_(t) {}

    bool operator()(FunctionValidator& f, ParseNode* arg, unsigned argIndex, Type actualType) const
    {
        if (!(actualType <= formalType_)) {
            return f.failf(arg, "%s is not a subtype of %s", actualType.toChars(),
                           formalType_.toChars());
        }
        return true;
    }
};

static inline Type
SimdToCoercedScalarType(AsmJSSimdType t)
{
    switch (t) {
      case AsmJSSimdType_int32x4:
      case AsmJSSimdType_bool32x4:
        return Type::Intish;
      case AsmJSSimdType_float32x4:
        return Type::Floatish;
    }
    MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("unexpected SIMD type");
}

class CheckSimdScalarArgs
{
    AsmJSSimdType simdType_;
    Type formalType_;

  public:
    explicit CheckSimdScalarArgs(AsmJSSimdType simdType)
      : simdType_(simdType), formalType_(SimdToCoercedScalarType(simdType))
    {}

    bool operator()(FunctionValidator& f, ParseNode* arg, unsigned argIndex, Type actualType,
                    size_t patchAt) const
    {
        if (!(actualType <= formalType_)) {
            // As a special case, accept doublelit arguments to float32x4 ops by
            // re-emitting them as float32 constants.
            if (simdType_ != AsmJSSimdType_float32x4 || !actualType.isDoubleLit()) {
                return f.failf(arg, "%s is not a subtype of %s%s",
                               actualType.toChars(), formalType_.toChars(),
                               simdType_ == AsmJSSimdType_float32x4 ? " or doublelit" : "");
            }

            // We emitted a double literal and actually want a float32.
            MOZ_ASSERT(patchAt != size_t(-1));
            f.patchOp(patchAt, F32::FromF64);
            return true;
        }

        if (patchAt == size_t(-1))
            return true;

        switch (simdType_) {
          case AsmJSSimdType_bool32x4:
          case AsmJSSimdType_int32x4:   f.patchOp(patchAt, I32::Id); return true;
          case AsmJSSimdType_float32x4: f.patchOp(patchAt, F32::Id); return true;
        }

        MOZ_CRASH("unexpected simd type");
    }
};

class CheckSimdSelectArgs
{
    Type formalType_;

  public:
    explicit CheckSimdSelectArgs(AsmJSSimdType t) : formalType_(t) {}

    bool operator()(FunctionValidator& f, ParseNode* arg, unsigned argIndex, Type actualType) const
    {
        if (argIndex == 0) {
            // First argument of select is a bool32x4 mask.
            if (!(actualType <= Type::Bool32x4))
                return f.failf(arg, "%s is not a subtype of Bool32x4", actualType.toChars());
            return true;
        }

        if (!(actualType <= formalType_)) {
            return f.failf(arg, "%s is not a subtype of %s", actualType.toChars(),
                           formalType_.toChars());
        }
        return true;
    }
};

class CheckSimdVectorScalarArgs
{
    AsmJSSimdType formalSimdType_;

  public:
    explicit CheckSimdVectorScalarArgs(AsmJSSimdType t) : formalSimdType_(t) {}

    bool operator()(FunctionValidator& f, ParseNode* arg, unsigned argIndex, Type actualType,
                    size_t patchAt = -1) const
    {
        MOZ_ASSERT(argIndex < 2);
        if (argIndex == 0) {
            // First argument is the vector
            if (!(actualType <= Type(formalSimdType_))) {
                return f.failf(arg, "%s is not a subtype of %s", actualType.toChars(),
                               Type(formalSimdType_).toChars());
            }

            if (patchAt == size_t(-1))
                return true;

            switch (formalSimdType_) {
              case AsmJSSimdType_int32x4:   f.patchOp(patchAt, I32X4::Id); return true;
              case AsmJSSimdType_float32x4: f.patchOp(patchAt, F32X4::Id); return true;
              case AsmJSSimdType_bool32x4:  f.patchOp(patchAt, B32X4::Id); return true;
            }

            MOZ_CRASH("unexpected simd type");
        }

        // Second argument is the scalar
        return CheckSimdScalarArgs(formalSimdType_)(f, arg, argIndex, actualType, patchAt);
    }
};

class CheckSimdExtractLaneArgs
{
    AsmJSSimdType formalSimdType_;

  public:
    explicit CheckSimdExtractLaneArgs(AsmJSSimdType t) : formalSimdType_(t) {}

    bool operator()(FunctionValidator& f, ParseNode* arg, unsigned argIndex, Type actualType) const
    {
        MOZ_ASSERT(argIndex < 2);
        if (argIndex == 0) {
            // First argument is the vector
            if (!(actualType <= Type(formalSimdType_))) {
                return f.failf(arg, "%s is not a subtype of %s", actualType.toChars(),
                               Type(formalSimdType_).toChars());
            }
            return true;
        }

        uint32_t laneIndex;
        // Second argument is the lane < vector length
        if (!IsLiteralOrConstInt(f, arg, &laneIndex))
            return f.failf(arg, "lane selector should be a constant integer literal");
        if (laneIndex >= SimdTypeToLength(formalSimdType_))
            return f.failf(arg, "lane selector should be in bounds");
        return true;
    }
};

class CheckSimdReplaceLaneArgs
{
    AsmJSSimdType formalSimdType_;

  public:
    explicit CheckSimdReplaceLaneArgs(AsmJSSimdType t) : formalSimdType_(t) {}

    bool operator()(FunctionValidator& f, ParseNode* arg, unsigned argIndex, Type actualType,
                    size_t patchAt) const
    {
        MOZ_ASSERT(argIndex < 3);
        uint32_t u32;
        switch (argIndex) {
          case 0:
            // First argument is the vector
            if (!(actualType <= Type(formalSimdType_))) {
                return f.failf(arg, "%s is not a subtype of %s", actualType.toChars(),
                               Type(formalSimdType_).toChars());
            }
            switch (formalSimdType_) {
              case AsmJSSimdType_int32x4:   f.patchOp(patchAt, I32X4::Id); break;
              case AsmJSSimdType_float32x4: f.patchOp(patchAt, F32X4::Id); break;
              case AsmJSSimdType_bool32x4:  f.patchOp(patchAt, B32X4::Id); break;
            }
            return true;
          case 1:
            // Second argument is the lane (< vector length).
            if (!IsLiteralOrConstInt(f, arg, &u32))
                return f.failf(arg, "lane selector should be a constant integer literal");
            if (u32 >= SimdTypeToLength(formalSimdType_))
                return f.failf(arg, "lane selector should be in bounds");
            f.patchOp(patchAt, I32::Id);
            return true;
          case 2:
            // Third argument is the scalar
            return CheckSimdScalarArgs(formalSimdType_)(f, arg, argIndex, actualType, patchAt);
        }
        return false;
    }
};

} // namespace

static void
SwitchPackOp(FunctionValidator& f, AsmJSSimdType type, I32X4 i32x4, F32X4 f32x4, B32X4 b32x4)
{
    switch (type) {
      case AsmJSSimdType_int32x4:   f.writeOp(i32x4); return;
      case AsmJSSimdType_float32x4: f.writeOp(f32x4); return;
      case AsmJSSimdType_bool32x4:  f.writeOp(b32x4); return;
    }
    MOZ_CRASH("unexpected simd type");
}

static bool
CheckSimdUnary(FunctionValidator& f, ParseNode* call, AsmJSSimdType opType,
               MSimdUnaryArith::Operation op, Type* type)
{
    SwitchPackOp(f, opType, I32X4::Unary, F32X4::Unary, B32X4::Unary);
    f.writeU8(uint8_t(op));
    if (!CheckSimdCallArgs(f, call, 1, CheckArgIsSubtypeOf(opType)))
        return false;
    *type = opType;
    return true;
}

template<class OpKind>
inline bool
CheckSimdBinaryGuts(FunctionValidator& f, ParseNode* call, AsmJSSimdType opType, OpKind op,
                    Type* type)
{
    f.writeU8(uint8_t(op));
    if (!CheckSimdCallArgs(f, call, 2, CheckArgIsSubtypeOf(opType)))
        return false;
    *type = opType;
    return true;
}

static bool
CheckSimdBinary(FunctionValidator& f, ParseNode* call, AsmJSSimdType opType,
                MSimdBinaryArith::Operation op, Type* type)
{
    SwitchPackOp(f, opType, I32X4::Binary, F32X4::Binary, B32X4::Binary);
    return CheckSimdBinaryGuts(f, call, opType, op, type);
}

static bool
CheckSimdBinary(FunctionValidator& f, ParseNode* call, AsmJSSimdType opType,
                MSimdBinaryBitwise::Operation op, Type* type)
{
    SwitchPackOp(f, opType, I32X4::BinaryBitwise, F32X4::Bad, B32X4::BinaryBitwise);
    return CheckSimdBinaryGuts(f, call, opType, op, type);
}

static bool
CheckSimdBinary(FunctionValidator& f, ParseNode* call, AsmJSSimdType opType,
                MSimdBinaryComp::Operation op, Type* type)
{
    switch (opType) {
      case AsmJSSimdType_int32x4:
        f.writeOp(B32X4::BinaryCompI32X4);
        break;
      case AsmJSSimdType_float32x4:
        f.writeOp(B32X4::BinaryCompF32X4);
        break;
      case AsmJSSimdType_bool32x4:
        MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("Can't compare boolean vectors");
    }
    f.writeU8(uint8_t(op));
    if (!CheckSimdCallArgs(f, call, 2, CheckArgIsSubtypeOf(opType)))
        return false;
    *type = Type::Bool32x4;
    return true;
}

static bool
CheckSimdBinary(FunctionValidator& f, ParseNode* call, AsmJSSimdType opType,
                MSimdShift::Operation op, Type* type)
{
    f.writeOp(I32X4::BinaryShift);
    f.writeU8(uint8_t(op));
    if (!CheckSimdCallArgs(f, call, 2, CheckSimdVectorScalarArgs(opType)))
        return false;
    *type = Type::Int32x4;
    return true;
}

static bool
CheckSimdExtractLane(FunctionValidator& f, ParseNode* call, AsmJSSimdType opType, Type* type)
{
    switch (opType) {
      case AsmJSSimdType_int32x4:
        f.writeOp(I32::I32X4ExtractLane);
        *type = Type::Signed;
        break;
      case AsmJSSimdType_float32x4:
        f.writeOp(F32::F32X4ExtractLane);
        *type = Type::Float;
        break;
      case AsmJSSimdType_bool32x4:
        f.writeOp(I32::B32X4ExtractLane);
        *type = Type::Int;
        break;
    }
    return CheckSimdCallArgs(f, call, 2, CheckSimdExtractLaneArgs(opType));
}

static bool
CheckSimdReplaceLane(FunctionValidator& f, ParseNode* call, AsmJSSimdType opType, Type* type)
{
    SwitchPackOp(f, opType, I32X4::ReplaceLane, F32X4::ReplaceLane, B32X4::ReplaceLane);
    if (!CheckSimdCallArgsPatchable(f, call, 3, CheckSimdReplaceLaneArgs(opType)))
        return false;
    *type = opType;
    return true;
}

typedef bool IsBitCast;

namespace {
// Include CheckSimdCast in unnamed namespace to avoid MSVC name lookup bug (due to the use of Type).

static bool
CheckSimdCast(FunctionValidator& f, ParseNode* call, AsmJSSimdType fromType, AsmJSSimdType toType,
              bool bitcast, Type* type)
{
    SwitchPackOp(f, toType,
                 bitcast ? I32X4::FromF32X4Bits : I32X4::FromF32X4,
                 bitcast ? F32X4::FromI32X4Bits : F32X4::FromI32X4,
                 B32X4::Bad);
    if (!CheckSimdCallArgs(f, call, 1, CheckArgIsSubtypeOf(fromType)))
        return false;
    *type = toType;
    return true;
}

} // namespace

static bool
CheckSimdShuffleSelectors(FunctionValidator& f, ParseNode* lane, int32_t lanes[4], uint32_t maxLane)
{
    for (unsigned i = 0; i < 4; i++, lane = NextNode(lane)) {
        uint32_t u32;
        if (!IsLiteralInt(f.m(), lane, &u32))
            return f.failf(lane, "lane selector should be a constant integer literal");
        if (u32 >= maxLane)
            return f.failf(lane, "lane selector should be less than %u", maxLane);
        lanes[i] = int32_t(u32);
    }
    return true;
}

static bool
CheckSimdSwizzle(FunctionValidator& f, ParseNode* call, AsmJSSimdType opType, Type* type)
{
    unsigned numArgs = CallArgListLength(call);
    if (numArgs != 5)
        return f.failf(call, "expected 5 arguments to SIMD swizzle, got %u", numArgs);

    SwitchPackOp(f, opType, I32X4::Swizzle, F32X4::Swizzle, B32X4::Bad);

    Type retType = opType;
    ParseNode* vec = CallArgList(call);
    Type vecType;
    if (!CheckExpr(f, vec, &vecType))
        return false;
    if (!(vecType <= retType))
        return f.failf(vec, "%s is not a subtype of %s", vecType.toChars(), retType.toChars());

    int32_t lanes[4];
    if (!CheckSimdShuffleSelectors(f, NextNode(vec), lanes, 4))
        return false;

    for (unsigned i = 0; i < 4; i++)
        f.writeU8(uint8_t(lanes[i]));

    *type = retType;
    return true;
}

static bool
CheckSimdShuffle(FunctionValidator& f, ParseNode* call, AsmJSSimdType opType, Type* type)
{
    unsigned numArgs = CallArgListLength(call);
    if (numArgs != 6)
        return f.failf(call, "expected 6 arguments to SIMD shuffle, got %u", numArgs);

    SwitchPackOp(f, opType, I32X4::Shuffle, F32X4::Shuffle, B32X4::Bad);

    Type retType = opType;
    ParseNode* arg = CallArgList(call);
    for (unsigned i = 0; i < 2; i++, arg = NextNode(arg)) {
        Type type;
        if (!CheckExpr(f, arg, &type))
            return false;
        if (!(type <= retType))
            return f.failf(arg, "%s is not a subtype of %s", type.toChars(), retType.toChars());
    }

    int32_t lanes[4];
    if (!CheckSimdShuffleSelectors(f, arg, lanes, 8))
        return false;

    for (unsigned i = 0; i < 4; i++)
        f.writeU8(uint8_t(lanes[i]));

    *type = retType;
    return true;
}

static bool
CheckSimdLoadStoreArgs(FunctionValidator& f, ParseNode* call, AsmJSSimdType opType,
                       Scalar::Type* viewType, NeedsBoundsCheck* needsBoundsCheck)
{
    ParseNode* view = CallArgList(call);
    if (!view->isKind(PNK_NAME))
        return f.fail(view, "expected Uint8Array view as SIMD.*.load/store first argument");

    const ModuleValidator::Global* global = f.lookupGlobal(view->name());
    if (!global ||
        global->which() != ModuleValidator::Global::ArrayView ||
        global->viewType() != Scalar::Uint8)
    {
        return f.fail(view, "expected Uint8Array view as SIMD.*.load/store first argument");
    }

    *needsBoundsCheck = NEEDS_BOUNDS_CHECK;

    switch (opType) {
      case AsmJSSimdType_int32x4:   *viewType = Scalar::Int32x4;   break;
      case AsmJSSimdType_float32x4: *viewType = Scalar::Float32x4; break;
      case AsmJSSimdType_bool32x4:  MOZ_CRASH("Cannot load/store boolean SIMD type");
    }

    ParseNode* indexExpr = NextNode(view);
    uint32_t indexLit;
    if (IsLiteralOrConstInt(f, indexExpr, &indexLit)) {
        if (indexLit > INT32_MAX)
            return f.fail(indexExpr, "constant index out of range");

        if (!f.m().tryRequireHeapLengthToBeAtLeast(indexLit + Simd128DataSize)) {
            return f.failf(indexExpr, "constant index outside heap size range declared by the "
                                      "change-heap function (0x%x - 0x%x)",
                                      f.m().minHeapLength(), f.m().module().maxHeapLength());
        }

        *needsBoundsCheck = NO_BOUNDS_CHECK;
        f.writeInt32Lit(indexLit);
        return true;
    }

    f.enterHeapExpression();

    Type indexType;
    if (!CheckExpr(f, indexExpr, &indexType))
        return false;
    if (!indexType.isIntish())
        return f.failf(indexExpr, "%s is not a subtype of intish", indexType.toChars());

    f.leaveHeapExpression();

    return true;
}

static bool
CheckSimdLoad(FunctionValidator& f, ParseNode* call, AsmJSSimdType opType,
              unsigned numElems, Type* type)
{
    unsigned numArgs = CallArgListLength(call);
    if (numArgs != 2)
        return f.failf(call, "expected 2 arguments to SIMD load, got %u", numArgs);

    SwitchPackOp(f, opType, I32X4::Load, F32X4::Load, B32X4::Bad);
    size_t viewTypeAt = f.tempU8();
    size_t needsBoundsCheckAt = f.tempU8();
    f.writeU8(numElems);

    Scalar::Type viewType;
    NeedsBoundsCheck needsBoundsCheck;
    if (!CheckSimdLoadStoreArgs(f, call, opType, &viewType, &needsBoundsCheck))
        return false;

    f.patchU8(needsBoundsCheckAt, uint8_t(needsBoundsCheck));
    f.patchU8(viewTypeAt, uint8_t(viewType));

    *type = opType;
    return true;
}

static bool
CheckSimdStore(FunctionValidator& f, ParseNode* call, AsmJSSimdType opType,
               unsigned numElems, Type* type)
{
    unsigned numArgs = CallArgListLength(call);
    if (numArgs != 3)
        return f.failf(call, "expected 3 arguments to SIMD store, got %u", numArgs);

    SwitchPackOp(f, opType, I32X4::Store, F32X4::Store, B32X4::Bad);
    size_t viewTypeAt = f.tempU8();
    size_t needsBoundsCheckAt = f.tempU8();
    f.writeU8(numElems);

    Scalar::Type viewType;
    NeedsBoundsCheck needsBoundsCheck;
    if (!CheckSimdLoadStoreArgs(f, call, opType, &viewType, &needsBoundsCheck))
        return false;

    Type retType = opType;
    ParseNode* vecExpr = NextNode(NextNode(CallArgList(call)));
    Type vecType;
    if (!CheckExpr(f, vecExpr, &vecType))
        return false;
    if (!(vecType <= retType))
        return f.failf(vecExpr, "%s is not a subtype of %s", vecType.toChars(), retType.toChars());

    f.patchU8(needsBoundsCheckAt, uint8_t(needsBoundsCheck));
    f.patchU8(viewTypeAt, uint8_t(viewType));

    *type = vecType;
    return true;
}

static bool
CheckSimdSelect(FunctionValidator& f, ParseNode* call, AsmJSSimdType opType, Type* type)
{
    SwitchPackOp(f, opType, I32X4::Select, F32X4::Select, B32X4::Bad);
    if (!CheckSimdCallArgs(f, call, 3, CheckSimdSelectArgs(opType)))
        return false;
    *type = opType;
    return true;
}

static bool
CheckSimdAllTrue(FunctionValidator& f, ParseNode* call, AsmJSSimdType opType, Type* type)
{
    switch (opType) {
      case AsmJSSimdType_bool32x4:
        f.writeOp(I32::B32X4AllTrue);
        break;
      case AsmJSSimdType_int32x4:
      case AsmJSSimdType_float32x4:
        MOZ_CRASH("allTrue is only defined on bool SIMD types");
    }
    if (!CheckSimdCallArgs(f, call, 1, CheckArgIsSubtypeOf(opType)))
        return false;
    *type = Type::Int;
    return true;
}

static bool
CheckSimdAnyTrue(FunctionValidator& f, ParseNode* call, AsmJSSimdType opType, Type* type)
{
    switch (opType) {
      case AsmJSSimdType_bool32x4:
        f.writeOp(I32::B32X4AnyTrue);
        break;
      case AsmJSSimdType_int32x4:
      case AsmJSSimdType_float32x4:
        MOZ_CRASH("anyTrue is only defined on bool SIMD types");
    }
    if (!CheckSimdCallArgs(f, call, 1, CheckArgIsSubtypeOf(opType)))
        return false;
    *type = Type::Int;
    return true;
}

static bool
CheckSimdCheck(FunctionValidator& f, ParseNode* call, AsmJSSimdType opType, Type* type)
{
    ValType coerceTo;
    ParseNode* argNode;
    if (!IsCoercionCall(f.m(), call, &coerceTo, &argNode))
        return f.failf(call, "expected 1 argument in call to check");
    return CheckCoercionArg(f, argNode, coerceTo, type);
}

static bool
CheckSimdSplat(FunctionValidator& f, ParseNode* call, AsmJSSimdType opType, Type* type)
{
    SwitchPackOp(f, opType, I32X4::Splat, F32X4::Splat, B32X4::Splat);
    if (!CheckSimdCallArgsPatchable(f, call, 1, CheckSimdScalarArgs(opType)))
        return false;
    *type = opType;
    return true;
}

static bool
CheckSimdOperationCall(FunctionValidator& f, ParseNode* call, const ModuleValidator::Global* global,
                       Type* type)
{
    MOZ_ASSERT(global->isSimdOperation());

    AsmJSSimdType opType = global->simdOperationType();

    switch (global->simdOperation()) {
      case AsmJSSimdOperation_check:
        return CheckSimdCheck(f, call, opType, type);

#define OP_CHECK_CASE_LIST_(OP)                                                         \
      case AsmJSSimdOperation_##OP:                                                     \
        return CheckSimdBinary(f, call, opType, MSimdBinaryArith::Op_##OP, type);
      FOREACH_NUMERIC_SIMD_BINOP(OP_CHECK_CASE_LIST_)
      FOREACH_FLOAT_SIMD_BINOP(OP_CHECK_CASE_LIST_)
#undef OP_CHECK_CASE_LIST_

      case AsmJSSimdOperation_lessThan:
        return CheckSimdBinary(f, call, opType, MSimdBinaryComp::lessThan, type);
      case AsmJSSimdOperation_lessThanOrEqual:
        return CheckSimdBinary(f, call, opType, MSimdBinaryComp::lessThanOrEqual, type);
      case AsmJSSimdOperation_equal:
        return CheckSimdBinary(f, call, opType, MSimdBinaryComp::equal, type);
      case AsmJSSimdOperation_notEqual:
        return CheckSimdBinary(f, call, opType, MSimdBinaryComp::notEqual, type);
      case AsmJSSimdOperation_greaterThan:
        return CheckSimdBinary(f, call, opType, MSimdBinaryComp::greaterThan, type);
      case AsmJSSimdOperation_greaterThanOrEqual:
        return CheckSimdBinary(f, call, opType, MSimdBinaryComp::greaterThanOrEqual, type);

      case AsmJSSimdOperation_and:
        return CheckSimdBinary(f, call, opType, MSimdBinaryBitwise::and_, type);
      case AsmJSSimdOperation_or:
        return CheckSimdBinary(f, call, opType, MSimdBinaryBitwise::or_, type);
      case AsmJSSimdOperation_xor:
        return CheckSimdBinary(f, call, opType, MSimdBinaryBitwise::xor_, type);

      case AsmJSSimdOperation_extractLane:
        return CheckSimdExtractLane(f, call, opType, type);
      case AsmJSSimdOperation_replaceLane:
        return CheckSimdReplaceLane(f, call, opType, type);

      case AsmJSSimdOperation_fromInt32x4:
        return CheckSimdCast(f, call, AsmJSSimdType_int32x4, opType, IsBitCast(false), type);
      case AsmJSSimdOperation_fromFloat32x4:
        return CheckSimdCast(f, call, AsmJSSimdType_float32x4, opType, IsBitCast(false), type);
      case AsmJSSimdOperation_fromInt32x4Bits:
        return CheckSimdCast(f, call, AsmJSSimdType_int32x4, opType, IsBitCast(true), type);
      case AsmJSSimdOperation_fromFloat32x4Bits:
        return CheckSimdCast(f, call, AsmJSSimdType_float32x4, opType, IsBitCast(true), type);

      case AsmJSSimdOperation_shiftLeftByScalar:
        return CheckSimdBinary(f, call, opType, MSimdShift::lsh, type);
      case AsmJSSimdOperation_shiftRightByScalar:
        return CheckSimdBinary(f, call, opType,
                               IsSignedIntSimdType(opType) ? MSimdShift::rsh : MSimdShift::ursh,
                               type);
      case AsmJSSimdOperation_shiftRightArithmeticByScalar:
        return CheckSimdBinary(f, call, opType, MSimdShift::rsh, type);
      case AsmJSSimdOperation_shiftRightLogicalByScalar:
        return CheckSimdBinary(f, call, opType, MSimdShift::ursh, type);

      case AsmJSSimdOperation_abs:
        return CheckSimdUnary(f, call, opType, MSimdUnaryArith::abs, type);
      case AsmJSSimdOperation_neg:
        return CheckSimdUnary(f, call, opType, MSimdUnaryArith::neg, type);
      case AsmJSSimdOperation_not:
        return CheckSimdUnary(f, call, opType, MSimdUnaryArith::not_, type);
      case AsmJSSimdOperation_sqrt:
        return CheckSimdUnary(f, call, opType, MSimdUnaryArith::sqrt, type);
      case AsmJSSimdOperation_reciprocalApproximation:
        return CheckSimdUnary(f, call, opType, MSimdUnaryArith::reciprocalApproximation, type);
      case AsmJSSimdOperation_reciprocalSqrtApproximation:
        return CheckSimdUnary(f, call, opType, MSimdUnaryArith::reciprocalSqrtApproximation, type);

      case AsmJSSimdOperation_swizzle:
        return CheckSimdSwizzle(f, call, opType, type);
      case AsmJSSimdOperation_shuffle:
        return CheckSimdShuffle(f, call, opType, type);

      case AsmJSSimdOperation_load:
        return CheckSimdLoad(f, call, opType, 4, type);
      case AsmJSSimdOperation_load1:
        return CheckSimdLoad(f, call, opType, 1, type);
      case AsmJSSimdOperation_load2:
        return CheckSimdLoad(f, call, opType, 2, type);
      case AsmJSSimdOperation_load3:
        return CheckSimdLoad(f, call, opType, 3, type);
      case AsmJSSimdOperation_store:
        return CheckSimdStore(f, call, opType, 4, type);
      case AsmJSSimdOperation_store1:
        return CheckSimdStore(f, call, opType, 1, type);
      case AsmJSSimdOperation_store2:
        return CheckSimdStore(f, call, opType, 2, type);
      case AsmJSSimdOperation_store3:
        return CheckSimdStore(f, call, opType, 3, type);

      case AsmJSSimdOperation_select:
        return CheckSimdSelect(f, call, opType, type);

      case AsmJSSimdOperation_splat:
        return CheckSimdSplat(f, call, opType, type);

      case AsmJSSimdOperation_allTrue:
        return CheckSimdAllTrue(f, call, opType, type);
      case AsmJSSimdOperation_anyTrue:
        return CheckSimdAnyTrue(f, call, opType, type);
    }
    MOZ_CRASH("unexpected simd operation in CheckSimdOperationCall");
}

static bool
CheckSimdCtorCall(FunctionValidator& f, ParseNode* call, const ModuleValidator::Global* global,
                  Type* type)
{
    MOZ_ASSERT(call->isKind(PNK_CALL));

    AsmJSSimdType simdType = global->simdCtorType();
    SwitchPackOp(f, simdType, I32X4::Ctor, F32X4::Ctor, B32X4::Ctor);

    unsigned length = SimdTypeToLength(simdType);
    if (!CheckSimdCallArgsPatchable(f, call, length, CheckSimdScalarArgs(simdType)))
        return false;

    *type = simdType;
    return true;
}

static bool
CheckUncoercedCall(FunctionValidator& f, ParseNode* expr, Type* type)
{
    MOZ_ASSERT(expr->isKind(PNK_CALL));

    const ModuleValidator::Global* global;
    if (IsCallToGlobal(f.m(), expr, &global)) {
        if (global->isMathFunction())
            return CheckMathBuiltinCall(f, expr, global->mathBuiltinFunction(), type);
        if (global->isAtomicsFunction())
            return CheckAtomicsBuiltinCall(f, expr, global->atomicsBuiltinFunction(), type);
        if (global->isSimdCtor())
            return CheckSimdCtorCall(f, expr, global, type);
        if (global->isSimdOperation())
            return CheckSimdOperationCall(f, expr, global, type);
    }

    return f.fail(expr, "all function calls must either be calls to standard lib math functions, "
                        "standard atomic functions, standard SIMD constructors or operations, "
                        "ignored (via f(); or comma-expression), coerced to signed (via f()|0), "
                        "coerced to float (via fround(f())) or coerced to double (via +f())");
}

static bool
CoerceResult(FunctionValidator& f, ParseNode* expr, ExprType expected, Type actual, size_t patchAt,
             Type* type)
{
    // At this point, the bytecode resembles this:
    //      | patchAt | the thing we wanted to coerce | current position |>
    switch (expected) {
      case ExprType::Void:
        if (actual.isIntish())
            f.patchOp(patchAt, Stmt::I32Expr);
        else if (actual.isFloatish())
            f.patchOp(patchAt, Stmt::F32Expr);
        else if (actual.isMaybeDouble())
            f.patchOp(patchAt, Stmt::F64Expr);
        else if (actual.isInt32x4())
            f.patchOp(patchAt, Stmt::I32X4Expr);
        else if (actual.isFloat32x4())
            f.patchOp(patchAt, Stmt::F32X4Expr);
        else if (actual.isBool32x4())
            f.patchOp(patchAt, Stmt::B32X4Expr);
        else if (actual.isVoid())
            f.patchOp(patchAt, Stmt::Id);
        else
            MOZ_CRASH("unhandled return type");
        break;
      case ExprType::I32:
        if (!actual.isIntish())
            return f.failf(expr, "%s is not a subtype of intish", actual.toChars());
        f.patchOp(patchAt, I32::Id);
        break;
      case ExprType::I64:
        MOZ_CRASH("no int64 in asm.js");
      case ExprType::F32:
        if (!CheckFloatCoercionArg(f, expr, actual, patchAt))
            return false;
        break;
      case ExprType::F64:
        if (actual.isMaybeDouble())
            f.patchOp(patchAt, F64::Id);
        else if (actual.isMaybeFloat())
            f.patchOp(patchAt, F64::FromF32);
        else if (actual.isSigned())
            f.patchOp(patchAt, F64::FromS32);
        else if (actual.isUnsigned())
            f.patchOp(patchAt, F64::FromU32);
        else
            return f.failf(expr, "%s is not a subtype of double?, float?, signed or unsigned", actual.toChars());
        break;
      case ExprType::I32x4:
        if (!actual.isInt32x4())
            return f.failf(expr, "%s is not a subtype of int32x4", actual.toChars());
        f.patchOp(patchAt, I32X4::Id);
        break;
      case ExprType::F32x4:
        if (!actual.isFloat32x4())
            return f.failf(expr, "%s is not a subtype of float32x4", actual.toChars());
        f.patchOp(patchAt, F32X4::Id);
        break;
      case ExprType::B32x4:
        if (!actual.isBool32x4())
            return f.failf(expr, "%s is not a subtype of bool32x4", actual.toChars());
        f.patchOp(patchAt, B32X4::Id);
        break;
    }

    *type = Type::ret(expected);
    return true;
}

static bool
CheckCoercedMathBuiltinCall(FunctionValidator& f, ParseNode* callNode, AsmJSMathBuiltinFunction func,
                            ExprType ret, Type* type)
{
    size_t opcodeAt = f.tempOp();
    Type actual;
    if (!CheckMathBuiltinCall(f, callNode, func, &actual))
        return false;
    return CoerceResult(f, callNode, ret, actual, opcodeAt, type);
}

static bool
CheckCoercedSimdCall(FunctionValidator& f, ParseNode* call, const ModuleValidator::Global* global,
                     ExprType ret, Type* type)
{
    size_t opcodeAt = f.tempOp();

    Type actual;
    if (global->isSimdCtor()) {
        if (!CheckSimdCtorCall(f, call, global, &actual))
            return false;
        MOZ_ASSERT(actual.isSimd());
    } else {
        MOZ_ASSERT(global->isSimdOperation());
        if (!CheckSimdOperationCall(f, call, global, &actual))
            return false;
    }

    return CoerceResult(f, call, ret, actual, opcodeAt, type);
}

static bool
CheckCoercedAtomicsBuiltinCall(FunctionValidator& f, ParseNode* callNode,
                               AsmJSAtomicsBuiltinFunction func, ExprType ret, Type* type)
{
    size_t opcodeAt = f.tempOp();
    Type actual;
    if (!CheckAtomicsBuiltinCall(f, callNode, func, &actual))
        return false;
    return CoerceResult(f, callNode, ret, actual, opcodeAt, type);
}

static bool
CheckCoercedCall(FunctionValidator& f, ParseNode* call, ExprType ret, Type* type)
{
    JS_CHECK_RECURSION_DONT_REPORT(f.cx(), return f.m().failOverRecursed());

    if (IsNumericLiteral(f.m(), call)) {
        size_t coerceOp = f.tempOp();
        NumLit lit = ExtractNumericLiteral(f.m(), call);
        f.writeLit(lit);
        return CoerceResult(f, call, ret, Type::lit(lit), coerceOp, type);
    }

    ParseNode* callee = CallCallee(call);

    if (callee->isKind(PNK_ELEM))
        return CheckFuncPtrCall(f, call, ret, type);

    if (!callee->isKind(PNK_NAME))
        return f.fail(callee, "unexpected callee expression type");

    PropertyName* calleeName = callee->name();

    if (const ModuleValidator::Global* global = f.lookupGlobal(calleeName)) {
        switch (global->which()) {
          case ModuleValidator::Global::FFI:
            return CheckFFICall(f, call, global->ffiIndex(), ret, type);
          case ModuleValidator::Global::MathBuiltinFunction:
            return CheckCoercedMathBuiltinCall(f, call, global->mathBuiltinFunction(), ret, type);
          case ModuleValidator::Global::AtomicsBuiltinFunction:
            return CheckCoercedAtomicsBuiltinCall(f, call, global->atomicsBuiltinFunction(), ret, type);
          case ModuleValidator::Global::ConstantLiteral:
          case ModuleValidator::Global::ConstantImport:
          case ModuleValidator::Global::Variable:
          case ModuleValidator::Global::FuncPtrTable:
          case ModuleValidator::Global::ArrayView:
          case ModuleValidator::Global::ArrayViewCtor:
          case ModuleValidator::Global::ByteLength:
          case ModuleValidator::Global::ChangeHeap:
            return f.failName(callee, "'%s' is not callable function", callee->name());
          case ModuleValidator::Global::SimdCtor:
          case ModuleValidator::Global::SimdOperation:
            return CheckCoercedSimdCall(f, call, global, ret, type);
          case ModuleValidator::Global::Function:
            break;
        }
    }

    return CheckInternalCall(f, call, calleeName, ret, type);
}

static bool
CheckPos(FunctionValidator& f, ParseNode* pos, Type* type)
{
    MOZ_ASSERT(pos->isKind(PNK_POS));
    ParseNode* operand = UnaryKid(pos);

    if (operand->isKind(PNK_CALL))
        return CheckCoercedCall(f, operand, ExprType::F64, type);

    size_t opcodeAt = f.tempOp();
    Type actual;
    if (!CheckExpr(f, operand, &actual))
        return false;

    return CoerceResult(f, operand, ExprType::F64, actual, opcodeAt, type);
}

static bool
CheckNot(FunctionValidator& f, ParseNode* expr, Type* type)
{
    MOZ_ASSERT(expr->isKind(PNK_NOT));
    ParseNode* operand = UnaryKid(expr);

    f.writeOp(I32::Not);

    Type operandType;
    if (!CheckExpr(f, operand, &operandType))
        return false;

    if (!operandType.isInt())
        return f.failf(operand, "%s is not a subtype of int", operandType.toChars());

    *type = Type::Int;
    return true;
}

static bool
CheckNeg(FunctionValidator& f, ParseNode* expr, Type* type)
{
    MOZ_ASSERT(expr->isKind(PNK_NEG));
    ParseNode* operand = UnaryKid(expr);

    size_t opcodeAt = f.tempOp();

    Type operandType;
    if (!CheckExpr(f, operand, &operandType))
        return false;

    if (operandType.isInt()) {
        f.patchOp(opcodeAt, I32::Neg);
        *type = Type::Intish;
        return true;
    }

    if (operandType.isMaybeDouble()) {
        f.patchOp(opcodeAt, F64::Neg);
        *type = Type::Double;
        return true;
    }

    if (operandType.isMaybeFloat()) {
        f.patchOp(opcodeAt, F32::Neg);
        *type = Type::Floatish;
        return true;
    }

    return f.failf(operand, "%s is not a subtype of int, float? or double?", operandType.toChars());
}

static bool
CheckCoerceToInt(FunctionValidator& f, ParseNode* expr, Type* type)
{
    MOZ_ASSERT(expr->isKind(PNK_BITNOT));
    ParseNode* operand = UnaryKid(expr);

    size_t opcodeAt = f.tempOp();

    Type operandType;
    if (!CheckExpr(f, operand, &operandType))
        return false;

    if (operandType.isMaybeDouble() || operandType.isMaybeFloat()) {
        f.patchOp(opcodeAt, operandType.isMaybeDouble() ? I32::FromF64 : I32::FromF32);
        *type = Type::Signed;
        return true;
    }

    if (!operandType.isIntish())
        return f.failf(operand, "%s is not a subtype of double?, float? or intish", operandType.toChars());

    f.patchOp(opcodeAt, I32::Id);
    *type = Type::Signed;
    return true;
}

static bool
CheckBitNot(FunctionValidator& f, ParseNode* neg, Type* type)
{
    MOZ_ASSERT(neg->isKind(PNK_BITNOT));
    ParseNode* operand = UnaryKid(neg);

    if (operand->isKind(PNK_BITNOT))
        return CheckCoerceToInt(f, operand, type);

    f.writeOp(I32::BitNot);

    Type operandType;
    if (!CheckExpr(f, operand, &operandType))
        return false;

    if (!operandType.isIntish())
        return f.failf(operand, "%s is not a subtype of intish", operandType.toChars());

    *type = Type::Signed;
    return true;
}

static bool
CheckAsExprStatement(FunctionValidator& f, ParseNode* exprStmt);

static bool
CheckComma(FunctionValidator& f, ParseNode* comma, Type* type)
{
    MOZ_ASSERT(comma->isKind(PNK_COMMA));
    ParseNode* operands = ListHead(comma);

    size_t commaAt = f.tempOp();
    f.writeU32(ListLength(comma));

    ParseNode* pn = operands;
    for (; NextNode(pn); pn = NextNode(pn)) {
        if (!CheckAsExprStatement(f, pn))
            return false;
    }

    if (!CheckExpr(f, pn, type))
        return false;

    if (type->isIntish())
        f.patchOp(commaAt, I32::Comma);
    else if (type->isFloatish())
        f.patchOp(commaAt, F32::Comma);
    else if (type->isMaybeDouble())
        f.patchOp(commaAt, F64::Comma);
    else if (type->isInt32x4())
        f.patchOp(commaAt, I32X4::Comma);
    else if (type->isFloat32x4())
        f.patchOp(commaAt, F32X4::Comma);
    else if (type->isBool32x4())
        f.patchOp(commaAt, B32X4::Comma);
    else
        MOZ_CRASH("unexpected or unimplemented expression statement");

    return true;
}

static bool
CheckConditional(FunctionValidator& f, ParseNode* ternary, Type* type)
{
    MOZ_ASSERT(ternary->isKind(PNK_CONDITIONAL));

    size_t opcodeAt = f.tempOp();

    ParseNode* cond = TernaryKid1(ternary);
    ParseNode* thenExpr = TernaryKid2(ternary);
    ParseNode* elseExpr = TernaryKid3(ternary);

    Type condType;
    if (!CheckExpr(f, cond, &condType))
        return false;

    if (!condType.isInt())
        return f.failf(cond, "%s is not a subtype of int", condType.toChars());

    Type thenType;
    if (!CheckExpr(f, thenExpr, &thenType))
        return false;

    Type elseType;
    if (!CheckExpr(f, elseExpr, &elseType))
        return false;

    if (thenType.isInt() && elseType.isInt()) {
        f.patchOp(opcodeAt, I32::Conditional);
        *type = Type::Int;
    } else if (thenType.isDouble() && elseType.isDouble()) {
        f.patchOp(opcodeAt, F64::Conditional);
        *type = Type::Double;
    } else if (thenType.isFloat() && elseType.isFloat()) {
        f.patchOp(opcodeAt, F32::Conditional);
        *type = Type::Float;
    } else if (elseType.isInt32x4() && thenType.isInt32x4()) {
        f.patchOp(opcodeAt, I32X4::Conditional);
        *type = Type::Int32x4;
    } else if (elseType.isFloat32x4() && thenType.isFloat32x4()) {
        f.patchOp(opcodeAt, F32X4::Conditional);
        *type = Type::Float32x4;
    } else if (elseType.isBool32x4() && thenType.isBool32x4()) {
        f.patchOp(opcodeAt, B32X4::Conditional);
        *type = Type::Bool32x4;
    } else {
        return f.failf(ternary, "then/else branches of conditional must both produce int, float, "
                       "double or SIMD types, current types are %s and %s",
                       thenType.toChars(), elseType.toChars());
    }

    return true;
}

static bool
IsValidIntMultiplyConstant(ModuleValidator& m, ParseNode* expr)
{
    if (!IsNumericLiteral(m, expr))
        return false;

    NumLit lit = ExtractNumericLiteral(m, expr);
    switch (lit.which()) {
      case NumLit::Fixnum:
      case NumLit::NegativeInt:
        if (abs(lit.toInt32()) < (1<<20))
            return true;
        return false;
      case NumLit::BigUnsigned:
      case NumLit::Double:
      case NumLit::Float:
      case NumLit::OutOfRangeInt:
      case NumLit::Int32x4:
      case NumLit::Float32x4:
      case NumLit::Bool32x4:
        return false;
    }

    MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("Bad literal");
}

static bool
CheckMultiply(FunctionValidator& f, ParseNode* star, Type* type)
{
    MOZ_ASSERT(star->isKind(PNK_STAR));
    ParseNode* lhs = MultiplyLeft(star);
    ParseNode* rhs = MultiplyRight(star);

    size_t opcodeAt = f.tempOp();

    Type lhsType;
    if (!CheckExpr(f, lhs, &lhsType))
        return false;

    Type rhsType;
    if (!CheckExpr(f, rhs, &rhsType))
        return false;

    if (lhsType.isInt() && rhsType.isInt()) {
        if (!IsValidIntMultiplyConstant(f.m(), lhs) && !IsValidIntMultiplyConstant(f.m(), rhs))
            return f.fail(star, "one arg to int multiply must be a small (-2^20, 2^20) int literal");
        f.patchOp(opcodeAt, I32::Mul);
        *type = Type::Intish;
        return true;
    }

    if (lhsType.isMaybeDouble() && rhsType.isMaybeDouble()) {
        f.patchOp(opcodeAt, F64::Mul);
        *type = Type::Double;
        return true;
    }

    if (lhsType.isMaybeFloat() && rhsType.isMaybeFloat()) {
        f.patchOp(opcodeAt, F32::Mul);
        *type = Type::Floatish;
        return true;
    }

    return f.fail(star, "multiply operands must be both int, both double? or both float?");
}

static bool
CheckAddOrSub(FunctionValidator& f, ParseNode* expr, Type* type, unsigned* numAddOrSubOut = nullptr)
{
    JS_CHECK_RECURSION_DONT_REPORT(f.cx(), return f.m().failOverRecursed());

    MOZ_ASSERT(expr->isKind(PNK_ADD) || expr->isKind(PNK_SUB));
    ParseNode* lhs = AddSubLeft(expr);
    ParseNode* rhs = AddSubRight(expr);

    Type lhsType, rhsType;
    unsigned lhsNumAddOrSub, rhsNumAddOrSub;

    size_t opcodeAt = f.tempOp();

    if (lhs->isKind(PNK_ADD) || lhs->isKind(PNK_SUB)) {
        if (!CheckAddOrSub(f, lhs, &lhsType, &lhsNumAddOrSub))
            return false;
        if (lhsType == Type::Intish)
            lhsType = Type::Int;
    } else {
        if (!CheckExpr(f, lhs, &lhsType))
            return false;
        lhsNumAddOrSub = 0;
    }

    if (rhs->isKind(PNK_ADD) || rhs->isKind(PNK_SUB)) {
        if (!CheckAddOrSub(f, rhs, &rhsType, &rhsNumAddOrSub))
            return false;
        if (rhsType == Type::Intish)
            rhsType = Type::Int;
    } else {
        if (!CheckExpr(f, rhs, &rhsType))
            return false;
        rhsNumAddOrSub = 0;
    }

    unsigned numAddOrSub = lhsNumAddOrSub + rhsNumAddOrSub + 1;
    if (numAddOrSub > (1<<20))
        return f.fail(expr, "too many + or - without intervening coercion");

    if (lhsType.isInt() && rhsType.isInt()) {
        f.patchOp(opcodeAt, expr->isKind(PNK_ADD) ? I32::Add : I32::Sub);
        *type = Type::Intish;
    } else if (lhsType.isMaybeDouble() && rhsType.isMaybeDouble()) {
        f.patchOp(opcodeAt, expr->isKind(PNK_ADD) ? F64::Add : F64::Sub);
        *type = Type::Double;
    } else if (lhsType.isMaybeFloat() && rhsType.isMaybeFloat()) {
        f.patchOp(opcodeAt, expr->isKind(PNK_ADD) ? F32::Add : F32::Sub);
        *type = Type::Floatish;
    } else {
        return f.failf(expr, "operands to + or - must both be int, float? or double?, got %s and %s",
                       lhsType.toChars(), rhsType.toChars());
    }

    if (numAddOrSubOut)
        *numAddOrSubOut = numAddOrSub;
    return true;
}

static bool
CheckDivOrMod(FunctionValidator& f, ParseNode* expr, Type* type)
{
    MOZ_ASSERT(expr->isKind(PNK_DIV) || expr->isKind(PNK_MOD));

    size_t opcodeAt = f.tempOp();

    ParseNode* lhs = DivOrModLeft(expr);
    ParseNode* rhs = DivOrModRight(expr);

    Type lhsType, rhsType;
    if (!CheckExpr(f, lhs, &lhsType))
        return false;
    if (!CheckExpr(f, rhs, &rhsType))
        return false;

    if (lhsType.isMaybeDouble() && rhsType.isMaybeDouble()) {
        f.patchOp(opcodeAt, expr->isKind(PNK_DIV) ? F64::Div : F64::Mod);
        *type = Type::Double;
        return true;
    }

    if (lhsType.isMaybeFloat() && rhsType.isMaybeFloat()) {
        if (expr->isKind(PNK_DIV))
            f.patchOp(opcodeAt, F32::Div);
        else
            return f.fail(expr, "modulo cannot receive float arguments");
        *type = Type::Floatish;
        return true;
    }

    if (lhsType.isSigned() && rhsType.isSigned()) {
        f.patchOp(opcodeAt, expr->isKind(PNK_DIV) ? I32::SDiv : I32::SMod);
        *type = Type::Intish;
        return true;
    }

    if (lhsType.isUnsigned() && rhsType.isUnsigned()) {
        f.patchOp(opcodeAt, expr->isKind(PNK_DIV) ? I32::UDiv : I32::UMod);
        *type = Type::Intish;
        return true;
    }

    return f.failf(expr, "arguments to / or %% must both be double?, float?, signed, or unsigned; "
                   "%s and %s are given", lhsType.toChars(), rhsType.toChars());
}

static bool
CheckComparison(FunctionValidator& f, ParseNode* comp, Type* type)
{
    MOZ_ASSERT(comp->isKind(PNK_LT) || comp->isKind(PNK_LE) || comp->isKind(PNK_GT) ||
               comp->isKind(PNK_GE) || comp->isKind(PNK_EQ) || comp->isKind(PNK_NE));

    size_t opcodeAt = f.tempOp();

    ParseNode* lhs = ComparisonLeft(comp);
    ParseNode* rhs = ComparisonRight(comp);

    Type lhsType, rhsType;
    if (!CheckExpr(f, lhs, &lhsType))
        return false;
    if (!CheckExpr(f, rhs, &rhsType))
        return false;

    if (!(lhsType.isSigned() && rhsType.isSigned()) &&
        !(lhsType.isUnsigned() && rhsType.isUnsigned()) &&
        !(lhsType.isDouble() && rhsType.isDouble()) &&
        !(lhsType.isFloat() && rhsType.isFloat()))
    {
        return f.failf(comp, "arguments to a comparison must both be signed, unsigned, floats or doubles; "
                       "%s and %s are given", lhsType.toChars(), rhsType.toChars());
    }

    I32 stmt;
    if (lhsType.isSigned() && rhsType.isSigned()) {
        switch (comp->getOp()) {
          case JSOP_EQ: stmt = I32::EqI32;  break;
          case JSOP_NE: stmt = I32::NeI32;  break;
          case JSOP_LT: stmt = I32::SLtI32; break;
          case JSOP_LE: stmt = I32::SLeI32; break;
          case JSOP_GT: stmt = I32::SGtI32; break;
          case JSOP_GE: stmt = I32::SGeI32; break;
          default: MOZ_CRASH("unexpected comparison op");
        }
    } else if (lhsType.isUnsigned() && rhsType.isUnsigned()) {
        switch (comp->getOp()) {
          case JSOP_EQ: stmt = I32::EqI32;  break;
          case JSOP_NE: stmt = I32::NeI32;  break;
          case JSOP_LT: stmt = I32::ULtI32; break;
          case JSOP_LE: stmt = I32::ULeI32; break;
          case JSOP_GT: stmt = I32::UGtI32; break;
          case JSOP_GE: stmt = I32::UGeI32; break;
          default: MOZ_CRASH("unexpected comparison op");
        }
    } else if (lhsType.isDouble()) {
        switch (comp->getOp()) {
          case JSOP_EQ: stmt = I32::EqF64;  break;
          case JSOP_NE: stmt = I32::NeF64;  break;
          case JSOP_LT: stmt = I32::LtF64; break;
          case JSOP_LE: stmt = I32::LeF64; break;
          case JSOP_GT: stmt = I32::GtF64; break;
          case JSOP_GE: stmt = I32::GeF64; break;
          default: MOZ_CRASH("unexpected comparison op");
        }
    } else if (lhsType.isFloat()) {
        switch (comp->getOp()) {
          case JSOP_EQ: stmt = I32::EqF32;  break;
          case JSOP_NE: stmt = I32::NeF32;  break;
          case JSOP_LT: stmt = I32::LtF32; break;
          case JSOP_LE: stmt = I32::LeF32; break;
          case JSOP_GT: stmt = I32::GtF32; break;
          case JSOP_GE: stmt = I32::GeF32; break;
          default: MOZ_CRASH("unexpected comparison op");
        }
    } else {
        MOZ_CRASH("unexpected type");
    }

    f.patchOp(opcodeAt, stmt);
    *type = Type::Int;
    return true;
}

static bool
CheckBitwise(FunctionValidator& f, ParseNode* bitwise, Type* type)
{
    ParseNode* lhs = BitwiseLeft(bitwise);
    ParseNode* rhs = BitwiseRight(bitwise);

    int32_t identityElement;
    bool onlyOnRight;
    switch (bitwise->getKind()) {
      case PNK_BITOR:  identityElement = 0;  onlyOnRight = false; *type = Type::Signed;   break;
      case PNK_BITAND: identityElement = -1; onlyOnRight = false; *type = Type::Signed;   break;
      case PNK_BITXOR: identityElement = 0;  onlyOnRight = false; *type = Type::Signed;   break;
      case PNK_LSH:    identityElement = 0;  onlyOnRight = true;  *type = Type::Signed;   break;
      case PNK_RSH:    identityElement = 0;  onlyOnRight = true;  *type = Type::Signed;   break;
      case PNK_URSH:   identityElement = 0;  onlyOnRight = true;  *type = Type::Unsigned; break;
      default: MOZ_CRASH("not a bitwise op");
    }

    uint32_t i;
    if (!onlyOnRight && IsLiteralInt(f.m(), lhs, &i) && i == uint32_t(identityElement)) {
        Type rhsType;
        if (!CheckExpr(f, rhs, &rhsType))
            return false;
        if (!rhsType.isIntish())
            return f.failf(bitwise, "%s is not a subtype of intish", rhsType.toChars());
        return true;
    }

    if (IsLiteralInt(f.m(), rhs, &i) && i == uint32_t(identityElement)) {
        if (bitwise->isKind(PNK_BITOR) && lhs->isKind(PNK_CALL))
            return CheckCoercedCall(f, lhs, ExprType::I32, type);

        Type lhsType;
        if (!CheckExpr(f, lhs, &lhsType))
            return false;
        if (!lhsType.isIntish())
            return f.failf(bitwise, "%s is not a subtype of intish", lhsType.toChars());
        return true;
    }

    switch (bitwise->getKind()) {
      case PNK_BITOR:  f.writeOp(I32::BitOr); break;
      case PNK_BITAND: f.writeOp(I32::BitAnd); break;
      case PNK_BITXOR: f.writeOp(I32::BitXor); break;
      case PNK_LSH:    f.writeOp(I32::Lsh); break;
      case PNK_RSH:    f.writeOp(I32::ArithRsh); break;
      case PNK_URSH:   f.writeOp(I32::LogicRsh); break;
      default: MOZ_CRASH("not a bitwise op");
    }

    Type lhsType;
    if (!CheckExpr(f, lhs, &lhsType))
        return false;

    Type rhsType;
    if (!CheckExpr(f, rhs, &rhsType))
        return false;

    if (!lhsType.isIntish())
        return f.failf(lhs, "%s is not a subtype of intish", lhsType.toChars());
    if (!rhsType.isIntish())
        return f.failf(rhs, "%s is not a subtype of intish", rhsType.toChars());

    return true;
}

static bool
CheckExpr(FunctionValidator& f, ParseNode* expr, Type* type)
{
    JS_CHECK_RECURSION_DONT_REPORT(f.cx(), return f.m().failOverRecursed());

    if (IsNumericLiteral(f.m(), expr))
        return CheckNumericLiteral(f, expr, type);

    switch (expr->getKind()) {
      case PNK_NAME:        return CheckVarRef(f, expr, type);
      case PNK_ELEM:        return CheckLoadArray(f, expr, type);
      case PNK_ASSIGN:      return CheckAssign(f, expr, type);
      case PNK_POS:         return CheckPos(f, expr, type);
      case PNK_NOT:         return CheckNot(f, expr, type);
      case PNK_NEG:         return CheckNeg(f, expr, type);
      case PNK_BITNOT:      return CheckBitNot(f, expr, type);
      case PNK_COMMA:       return CheckComma(f, expr, type);
      case PNK_CONDITIONAL: return CheckConditional(f, expr, type);
      case PNK_STAR:        return CheckMultiply(f, expr, type);
      case PNK_CALL:        return CheckUncoercedCall(f, expr, type);

      case PNK_ADD:
      case PNK_SUB:         return CheckAddOrSub(f, expr, type);

      case PNK_DIV:
      case PNK_MOD:         return CheckDivOrMod(f, expr, type);

      case PNK_LT:
      case PNK_LE:
      case PNK_GT:
      case PNK_GE:
      case PNK_EQ:
      case PNK_NE:          return CheckComparison(f, expr, type);

      case PNK_BITOR:
      case PNK_BITAND:
      case PNK_BITXOR:
      case PNK_LSH:
      case PNK_RSH:
      case PNK_URSH:        return CheckBitwise(f, expr, type);

      default:;
    }

    return f.fail(expr, "unsupported expression");
}

static bool
CheckStatement(FunctionValidator& f, ParseNode* stmt);

static bool
CheckAsExprStatement(FunctionValidator& f, ParseNode* expr)
{
    if (expr->isKind(PNK_CALL)) {
        Type _;
        return CheckCoercedCall(f, expr, ExprType::Void, &_);
    }

    size_t opcodeAt = f.tempOp();

    Type type;
    if (!CheckExpr(f, expr, &type))
        return false;

    if (type.isIntish())
        f.patchOp(opcodeAt, Stmt::I32Expr);
    else if (type.isFloatish())
        f.patchOp(opcodeAt, Stmt::F32Expr);
    else if (type.isMaybeDouble())
        f.patchOp(opcodeAt, Stmt::F64Expr);
    else if (type.isInt32x4())
        f.patchOp(opcodeAt, Stmt::I32X4Expr);
    else if (type.isFloat32x4())
        f.patchOp(opcodeAt, Stmt::F32X4Expr);
    else if (type.isBool32x4())
        f.patchOp(opcodeAt, Stmt::B32X4Expr);
    else
        MOZ_CRASH("unexpected or unimplemented expression statement");

    return true;
}

static bool
CheckExprStatement(FunctionValidator& f, ParseNode* exprStmt)
{
    MOZ_ASSERT(exprStmt->isKind(PNK_SEMI));
    ParseNode* expr = UnaryKid(exprStmt);

    if (!expr) {
        f.writeOp(Stmt::Noop);
        return true;
    }

    return CheckAsExprStatement(f, expr);
}

enum class InterruptCheckPosition {
    Head,
    Loop
};

static void
MaybeAddInterruptCheck(FunctionValidator& f, InterruptCheckPosition pos, ParseNode* pn)
{
    if (f.m().mg().args().useSignalHandlersForInterrupt)
        return;

    switch (pos) {
      case InterruptCheckPosition::Head: f.writeOp(Stmt::InterruptCheckHead); break;
      case InterruptCheckPosition::Loop: f.writeOp(Stmt::InterruptCheckLoop); break;
    }

    unsigned lineno = 0, column = 0;
    f.m().tokenStream().srcCoords.lineNumAndColumnIndex(pn->pn_pos.begin, &lineno, &column);
    f.writeU32(lineno);
    f.writeU32(column);
}

static bool
CheckWhile(FunctionValidator& f, ParseNode* whileStmt)
{
    MOZ_ASSERT(whileStmt->isKind(PNK_WHILE));
    ParseNode* cond = BinaryLeft(whileStmt);
    ParseNode* body = BinaryRight(whileStmt);

    f.writeOp(Stmt::While);

    Type condType;
    if (!CheckExpr(f, cond, &condType))
        return false;
    if (!condType.isInt())
        return f.failf(cond, "%s is not a subtype of int", condType.toChars());

    MaybeAddInterruptCheck(f, InterruptCheckPosition::Loop, whileStmt);

    return CheckStatement(f, body);
}

static bool
CheckFor(FunctionValidator& f, ParseNode* forStmt)
{
    MOZ_ASSERT(forStmt->isKind(PNK_FOR));
    ParseNode* forHead = BinaryLeft(forStmt);
    ParseNode* body = BinaryRight(forStmt);

    if (!forHead->isKind(PNK_FORHEAD))
        return f.fail(forHead, "unsupported for-loop statement");

    ParseNode* maybeInit = TernaryKid1(forHead);
    ParseNode* maybeCond = TernaryKid2(forHead);
    ParseNode* maybeInc = TernaryKid3(forHead);

    f.writeOp(maybeInit ? (maybeInc ? Stmt::ForInitInc   : Stmt::ForInitNoInc)
                        : (maybeInc ? Stmt::ForNoInitInc : Stmt::ForNoInitNoInc));

    if (maybeInit && !CheckAsExprStatement(f, maybeInit))
        return false;

    if (maybeCond) {
        Type condType;
        if (!CheckExpr(f, maybeCond, &condType))
            return false;
        if (!condType.isInt())
            return f.failf(maybeCond, "%s is not a subtype of int", condType.toChars());
    } else {
        f.writeInt32Lit(1);
    }

    MaybeAddInterruptCheck(f, InterruptCheckPosition::Loop, forStmt);

    if (!CheckStatement(f, body))
        return false;

    if (maybeInc && !CheckAsExprStatement(f, maybeInc))
        return false;

    f.writeDebugCheckPoint();
    return true;
}

static bool
CheckDoWhile(FunctionValidator& f, ParseNode* whileStmt)
{
    MOZ_ASSERT(whileStmt->isKind(PNK_DOWHILE));
    ParseNode* body = BinaryLeft(whileStmt);
    ParseNode* cond = BinaryRight(whileStmt);

    f.writeOp(Stmt::DoWhile);

    MaybeAddInterruptCheck(f, InterruptCheckPosition::Loop, cond);

    if (!CheckStatement(f, body))
        return false;

    Type condType;
    if (!CheckExpr(f, cond, &condType))
        return false;
    if (!condType.isInt())
        return f.failf(cond, "%s is not a subtype of int", condType.toChars());

    return true;
}

static bool
CheckLabel(FunctionValidator& f, ParseNode* labeledStmt)
{
    MOZ_ASSERT(labeledStmt->isKind(PNK_LABEL));
    PropertyName* label = LabeledStatementLabel(labeledStmt);
    ParseNode* stmt = LabeledStatementStatement(labeledStmt);

    f.writeOp(Stmt::Label);

    uint32_t labelId;
    if (!f.addLabel(label, &labelId))
        return false;

    f.writeU32(labelId);

    if (!CheckStatement(f, stmt))
        return false;

    f.removeLabel(label);
    return true;
}

static bool
CheckIf(FunctionValidator& f, ParseNode* ifStmt)
{
  recurse:
    size_t opcodeAt = f.tempOp();

    MOZ_ASSERT(ifStmt->isKind(PNK_IF));
    ParseNode* cond = TernaryKid1(ifStmt);
    ParseNode* thenStmt = TernaryKid2(ifStmt);
    ParseNode* elseStmt = TernaryKid3(ifStmt);

    Type condType;
    if (!CheckExpr(f, cond, &condType))
        return false;
    if (!condType.isInt())
        return f.failf(cond, "%s is not a subtype of int", condType.toChars());

    if (!CheckStatement(f, thenStmt))
        return false;

    if (!elseStmt) {
        f.patchOp(opcodeAt, Stmt::IfThen);
    } else {
        f.patchOp(opcodeAt, Stmt::IfElse);

        if (elseStmt->isKind(PNK_IF)) {
            ifStmt = elseStmt;
            goto recurse;
        }

        if (!CheckStatement(f, elseStmt))
            return false;
    }

    return true;
}

static bool
CheckCaseExpr(FunctionValidator& f, ParseNode* caseExpr, int32_t* value)
{
    if (!IsNumericLiteral(f.m(), caseExpr))
        return f.fail(caseExpr, "switch case expression must be an integer literal");

    NumLit lit = ExtractNumericLiteral(f.m(), caseExpr);
    switch (lit.which()) {
      case NumLit::Fixnum:
      case NumLit::NegativeInt:
        *value = lit.toInt32();
        break;
      case NumLit::OutOfRangeInt:
      case NumLit::BigUnsigned:
        return f.fail(caseExpr, "switch case expression out of integer range");
      case NumLit::Double:
      case NumLit::Float:
      case NumLit::Int32x4:
      case NumLit::Float32x4:
      case NumLit::Bool32x4:
        return f.fail(caseExpr, "switch case expression must be an integer literal");
    }

    return true;
}

static bool
CheckDefaultAtEnd(FunctionValidator& f, ParseNode* stmt)
{
    for (; stmt; stmt = NextNode(stmt)) {
        if (IsDefaultCase(stmt) && NextNode(stmt) != nullptr)
            return f.fail(stmt, "default label must be at the end");
    }

    return true;
}

static bool
CheckSwitchRange(FunctionValidator& f, ParseNode* stmt, int32_t* low, int32_t* high,
                 int32_t* tableLength)
{
    if (IsDefaultCase(stmt)) {
        *low = 0;
        *high = -1;
        *tableLength = 0;
        return true;
    }

    int32_t i = 0;
    if (!CheckCaseExpr(f, CaseExpr(stmt), &i))
        return false;

    *low = *high = i;

    ParseNode* initialStmt = stmt;
    for (stmt = NextNode(stmt); stmt && !IsDefaultCase(stmt); stmt = NextNode(stmt)) {
        int32_t i = 0;
        if (!CheckCaseExpr(f, CaseExpr(stmt), &i))
            return false;

        *low = Min(*low, i);
        *high = Max(*high, i);
    }

    int64_t i64 = (int64_t(*high) - int64_t(*low)) + 1;
    if (i64 > 4*1024*1024)
        return f.fail(initialStmt, "all switch statements generate tables; this table would be too big");

    *tableLength = int32_t(i64);
    return true;
}

void
PatchSwitch(FunctionValidator& f,
            size_t hasDefaultAt, bool hasDefault,
            size_t lowAt, int32_t low,
            size_t highAt, int32_t high,
            size_t numCasesAt, uint32_t numCases)
{
    f.patchU8(hasDefaultAt, uint8_t(hasDefault));
    f.patch32(lowAt, low);
    f.patch32(highAt, high);
    f.patch32(numCasesAt, numCases);
}

static bool
CheckSwitch(FunctionValidator& f, ParseNode* switchStmt)
{
    MOZ_ASSERT(switchStmt->isKind(PNK_SWITCH));

    f.writeOp(Stmt::Switch);
    // Has default
    size_t hasDefaultAt = f.tempU8();
    // Low / High / Num cases
    size_t lowAt = f.temp32();
    size_t highAt = f.temp32();
    size_t numCasesAt = f.temp32();

    ParseNode* switchExpr = BinaryLeft(switchStmt);
    ParseNode* switchBody = BinaryRight(switchStmt);

    if (!switchBody->isKind(PNK_STATEMENTLIST))
        return f.fail(switchBody, "switch body may not contain 'let' declarations");

    Type exprType;
    if (!CheckExpr(f, switchExpr, &exprType))
        return false;

    if (!exprType.isSigned())
        return f.failf(switchExpr, "%s is not a subtype of signed", exprType.toChars());

    ParseNode* stmt = ListHead(switchBody);

    if (!CheckDefaultAtEnd(f, stmt))
        return false;

    if (!stmt) {
        PatchSwitch(f, hasDefaultAt, false, lowAt, 0, highAt, 0, numCasesAt, 0);
        return true;
    }

    int32_t low = 0, high = 0, tableLength = 0;
    if (!CheckSwitchRange(f, stmt, &low, &high, &tableLength))
        return false;

    Vector<bool, 8> cases(f.cx());
    if (!cases.resize(tableLength))
        return false;

    uint32_t numCases = 0;
    for (; stmt && !IsDefaultCase(stmt); stmt = NextNode(stmt)) {
        int32_t caseValue = ExtractNumericLiteral(f.m(), CaseExpr(stmt)).toInt32();
        unsigned caseIndex = caseValue - low;

        if (cases[caseIndex])
            return f.fail(stmt, "no duplicate case labels");

        cases[caseIndex] = true;
        numCases += 1;
        f.writeI32(caseValue);

        if (!CheckStatement(f, CaseBody(stmt)))
            return false;

    }

    bool hasDefault = false;
    if (stmt && IsDefaultCase(stmt)) {
        hasDefault = true;
        if (!CheckStatement(f, CaseBody(stmt)))
            return false;
    }

    PatchSwitch(f, hasDefaultAt, hasDefault, lowAt, low, highAt, high, numCasesAt, numCases);
    return true;
}

static bool
CheckReturnType(FunctionValidator& f, ParseNode* usepn, ExprType ret)
{
    if (!f.hasAlreadyReturned()) {
        f.setReturnedType(ret);
        return true;
    }

    if (f.returnedType() != ret) {
        return f.failf(usepn, "%s incompatible with previous return of type %s",
                       Type::ret(ret).toChars(), Type::ret(f.returnedType()).toChars());
    }

    return true;
}

static bool
CheckReturn(FunctionValidator& f, ParseNode* returnStmt)
{
    ParseNode* expr = ReturnExpr(returnStmt);

    f.writeOp(Stmt::Ret);

    if (!expr)
        return CheckReturnType(f, returnStmt, ExprType::Void);

    Type type;
    if (!CheckExpr(f, expr, &type))
        return false;

    ExprType ret;
    if (type.isSigned())
        ret = ExprType::I32;
    else if (type.isFloat())
        ret = ExprType::F32;
    else if (type.isDouble())
        ret = ExprType::F64;
    else if (type.isInt32x4())
        ret = ExprType::I32x4;
    else if (type.isFloat32x4())
        ret = ExprType::F32x4;
    else if (type.isBool32x4())
        ret = ExprType::B32x4;
    else if (type.isVoid())
        ret = ExprType::Void;
    else
        return f.failf(expr, "%s is not a valid return type", type.toChars());

    return CheckReturnType(f, expr, ret);
}

static bool
CheckStatementList(FunctionValidator& f, ParseNode* stmtList)
{
    MOZ_ASSERT(stmtList->isKind(PNK_STATEMENTLIST));

    f.writeOp(Stmt::Block);
    f.writeU32(ListLength(stmtList));

    for (ParseNode* stmt = ListHead(stmtList); stmt; stmt = NextNode(stmt)) {
        if (!CheckStatement(f, stmt))
            return false;
    }

    f.writeDebugCheckPoint();
    return true;
}

static bool
CheckBreakOrContinue(FunctionValidator& f, PropertyName* maybeLabel,
                     Stmt withoutLabel, Stmt withLabel)
{
    if (!maybeLabel) {
        f.writeOp(withoutLabel);
        return true;
    }

    f.writeOp(withLabel);

    uint32_t labelId = f.lookupLabel(maybeLabel);
    MOZ_ASSERT(labelId != uint32_t(-1));

    f.writeU32(labelId);
    return true;
}

static bool
CheckStatement(FunctionValidator& f, ParseNode* stmt)
{
    JS_CHECK_RECURSION_DONT_REPORT(f.cx(), return f.m().failOverRecursed());

    switch (stmt->getKind()) {
      case PNK_SEMI:          return CheckExprStatement(f, stmt);
      case PNK_WHILE:         return CheckWhile(f, stmt);
      case PNK_FOR:           return CheckFor(f, stmt);
      case PNK_DOWHILE:       return CheckDoWhile(f, stmt);
      case PNK_LABEL:         return CheckLabel(f, stmt);
      case PNK_IF:            return CheckIf(f, stmt);
      case PNK_SWITCH:        return CheckSwitch(f, stmt);
      case PNK_RETURN:        return CheckReturn(f, stmt);
      case PNK_STATEMENTLIST: return CheckStatementList(f, stmt);
      case PNK_BREAK:         return CheckBreakOrContinue(f, LoopControlMaybeLabel(stmt),
                                                          Stmt::Break, Stmt::BreakLabel);
      case PNK_CONTINUE:      return CheckBreakOrContinue(f, LoopControlMaybeLabel(stmt),
                                                          Stmt::Continue, Stmt::ContinueLabel);
      default:;
    }

    return f.fail(stmt, "unexpected statement kind");
}

static bool
CheckByteLengthCall(ModuleValidator& m, ParseNode* pn, PropertyName* newBufferName)
{
    if (!pn->isKind(PNK_CALL) || !CallCallee(pn)->isKind(PNK_NAME))
        return m.fail(pn, "expecting call to imported byteLength");

    const ModuleValidator::Global* global = m.lookupGlobal(CallCallee(pn)->name());
    if (!global || global->which() != ModuleValidator::Global::ByteLength)
        return m.fail(pn, "expecting call to imported byteLength");

    if (CallArgListLength(pn) != 1 || !IsUseOfName(CallArgList(pn), newBufferName))
        return m.failName(pn, "expecting %s as argument to byteLength call", newBufferName);

    return true;
}

static bool
CheckHeapLengthCondition(ModuleValidator& m, ParseNode* cond, PropertyName* newBufferName,
                         uint32_t* mask, uint32_t* minLength, uint32_t* maxLength)
{
    if (!cond->isKind(PNK_OR) || !AndOrLeft(cond)->isKind(PNK_OR))
        return m.fail(cond, "expecting byteLength & K || byteLength <= L || byteLength > M");

    ParseNode* cond1 = AndOrLeft(AndOrLeft(cond));
    ParseNode* cond2 = AndOrRight(AndOrLeft(cond));
    ParseNode* cond3 = AndOrRight(cond);

    if (!cond1->isKind(PNK_BITAND))
        return m.fail(cond1, "expecting byteLength & K");

    if (!CheckByteLengthCall(m, BitwiseLeft(cond1), newBufferName))
        return false;

    ParseNode* maskNode = BitwiseRight(cond1);
    if (!IsLiteralInt(m, maskNode, mask))
        return m.fail(maskNode, "expecting integer literal mask");
    if (*mask == UINT32_MAX)
        return m.fail(maskNode, "invalid mask value");
    if ((*mask & 0xffffff) != 0xffffff)
        return m.fail(maskNode, "mask value must have the bits 0xffffff set");

    if (!cond2->isKind(PNK_LE))
        return m.fail(cond2, "expecting byteLength <= L");

    if (!CheckByteLengthCall(m, RelationalLeft(cond2), newBufferName))
        return false;

    ParseNode* minLengthNode = RelationalRight(cond2);
    uint32_t minLengthExclusive;
    if (!IsLiteralInt(m, minLengthNode, &minLengthExclusive))
        return m.fail(minLengthNode, "expecting integer literal");
    if (minLengthExclusive < 0xffffff || minLengthExclusive == UINT32_MAX)
        return m.fail(minLengthNode, "literal must be >= 0xffffff and < 0xffffffff");

    // Add one to convert from exclusive (the branch rejects if ==) to inclusive.
    *minLength = minLengthExclusive + 1;

    if (!cond3->isKind(PNK_GT))
        return m.fail(cond3, "expecting byteLength > M");

    if (!CheckByteLengthCall(m, RelationalLeft(cond3), newBufferName))
        return false;

    ParseNode* maxLengthNode = RelationalRight(cond3);
    if (!IsLiteralInt(m, maxLengthNode, maxLength))
        return m.fail(maxLengthNode, "expecting integer literal");
    if (*maxLength > 0x80000000)
        return m.fail(maxLengthNode, "literal must be <= 0x80000000");

    if (*maxLength < *minLength)
        return m.fail(maxLengthNode, "maximum length must be greater or equal to minimum length");

    return true;
}

static bool
CheckReturnBoolLiteral(ModuleValidator& m, ParseNode* stmt, bool retval)
{
    if (stmt->isKind(PNK_STATEMENTLIST)) {
        ParseNode* next = SkipEmptyStatements(ListHead(stmt));
        if (!next)
            return m.fail(stmt, "expected return statement");
        stmt = next;
        if (NextNonEmptyStatement(stmt))
            return m.fail(stmt, "expected single return statement");
    }

    if (!stmt->isKind(PNK_RETURN))
        return m.fail(stmt, "expected return statement");

    ParseNode* returnExpr = ReturnExpr(stmt);
    if (!returnExpr || !returnExpr->isKind(retval ? PNK_TRUE : PNK_FALSE))
        return m.failf(stmt, "expected 'return %s;'", retval ? "true" : "false");

    return true;
}

static bool
CheckReassignmentTo(ModuleValidator& m, ParseNode* stmt, PropertyName* lhsName, ParseNode** rhs)
{
    if (!stmt->isKind(PNK_SEMI))
        return m.fail(stmt, "missing reassignment");

    ParseNode* assign = UnaryKid(stmt);
    if (!assign || !assign->isKind(PNK_ASSIGN))
        return m.fail(stmt, "missing reassignment");

    ParseNode* lhs = BinaryLeft(assign);
    if (!IsUseOfName(lhs, lhsName))
        return m.failName(lhs, "expecting reassignment of %s", lhsName);

    *rhs = BinaryRight(assign);
    return true;
}

static bool
CheckChangeHeap(ModuleValidator& m, ParseNode* fn, bool* validated)
{
    MOZ_ASSERT(fn->isKind(PNK_FUNCTION));

    // We don't yet know whether this is a change-heap function.
    // The point at which we know we have a change-heap function is once we see
    // whether the argument is coerced according to the normal asm.js rules. If
    // it is coerced, it's not change-heap and must validate according to normal
    // rules; otherwise it must validate as a change-heap function.
    *validated = false;

    PropertyName* changeHeapName = FunctionName(fn);
    if (!CheckModuleLevelName(m, fn, changeHeapName))
        return false;

    unsigned numFormals;
    ParseNode* arg = FunctionArgsList(fn, &numFormals);
    if (numFormals != 1)
        return true;

    PropertyName* newBufferName;
    if (!CheckArgument(m, arg, &newBufferName))
        return false;

    ParseNode* stmtIter = SkipEmptyStatements(ListHead(FunctionStatementList(fn)));
    if (!stmtIter || !stmtIter->isKind(PNK_IF))
        return true;

    // We can now issue validation failures if we see something that isn't a
    // valid change-heap function.
    *validated = true;

    PropertyName* bufferName = m.module().bufferArgumentName();
    if (!bufferName)
        return m.fail(fn, "to change heaps, the module must have a buffer argument");

    ParseNode* cond = TernaryKid1(stmtIter);
    ParseNode* thenStmt = TernaryKid2(stmtIter);
    if (ParseNode* elseStmt = TernaryKid3(stmtIter))
        return m.fail(elseStmt, "unexpected else statement");

    uint32_t mask, min = 0, max;  // initialize min to silence GCC warning
    if (!CheckHeapLengthCondition(m, cond, newBufferName, &mask, &min, &max))
        return false;

    if (!CheckReturnBoolLiteral(m, thenStmt, false))
        return false;

    ParseNode* next = NextNonEmptyStatement(stmtIter);

    for (unsigned i = 0; i < m.numArrayViews(); i++, next = NextNonEmptyStatement(stmtIter)) {
        if (!next)
            return m.failOffset(stmtIter->pn_pos.end, "missing reassignment");
        stmtIter = next;

        const ModuleValidator::ArrayView& view = m.arrayView(i);

        ParseNode* rhs;
        if (!CheckReassignmentTo(m, stmtIter, view.name, &rhs))
            return false;

        if (!rhs->isKind(PNK_NEW))
            return m.failName(rhs, "expecting assignment of new array view to %s", view.name);

        ParseNode* ctorExpr = ListHead(rhs);
        if (!ctorExpr->isKind(PNK_NAME))
            return m.fail(rhs, "expecting name of imported typed array constructor");

        const ModuleValidator::Global* global = m.lookupGlobal(ctorExpr->name());
        if (!global || global->which() != ModuleValidator::Global::ArrayViewCtor)
            return m.fail(rhs, "expecting name of imported typed array constructor");
        if (global->viewType() != view.type)
            return m.fail(rhs, "can't change the type of a global view variable");

        if (!CheckNewArrayViewArgs(m, ctorExpr, newBufferName))
            return false;
    }

    if (!next)
        return m.failOffset(stmtIter->pn_pos.end, "missing reassignment");
    stmtIter = next;

    ParseNode* rhs;
    if (!CheckReassignmentTo(m, stmtIter, bufferName, &rhs))
        return false;
    if (!IsUseOfName(rhs, newBufferName))
        return m.failName(stmtIter, "expecting assignment of new buffer to %s", bufferName);

    next = NextNonEmptyStatement(stmtIter);
    if (!next)
        return m.failOffset(stmtIter->pn_pos.end, "expected return statement");
    stmtIter = next;

    if (!CheckReturnBoolLiteral(m, stmtIter, true))
        return false;

    stmtIter = NextNonEmptyStatement(stmtIter);
    if (stmtIter)
        return m.fail(stmtIter, "expecting end of function");

    return m.addChangeHeap(changeHeapName, fn, mask, min, max);
}

static bool
ParseFunction(ModuleValidator& m, ParseNode** fnOut, unsigned* line, unsigned* column)
{
    TokenStream& tokenStream = m.tokenStream();

    tokenStream.consumeKnownToken(TOK_FUNCTION, TokenStream::Operand);
    tokenStream.srcCoords.lineNumAndColumnIndex(tokenStream.currentToken().pos.end, line, column);

    RootedPropertyName name(m.cx());

    TokenKind tk;
    if (!tokenStream.getToken(&tk, TokenStream::Operand))
        return false;
    if (tk == TOK_NAME) {
        name = tokenStream.currentName();
    } else if (tk == TOK_YIELD) {
        if (!m.parser().checkYieldNameValidity())
            return false;
        name = m.cx()->names().yield;
    } else {
        return false;  // The regular parser will throw a SyntaxError, no need to m.fail.
    }

    ParseNode* fn = m.parser().handler.newFunctionDefinition();
    if (!fn)
        return false;

    // This flows into FunctionBox, so must be tenured.
    RootedFunction fun(m.cx(),
                       NewScriptedFunction(m.cx(), 0, JSFunction::INTERPRETED,
                                           name, gc::AllocKind::FUNCTION,
                                           TenuredObject));
    if (!fun)
        return false;

    AsmJSParseContext* outerpc = m.parser().pc;

    Directives directives(outerpc);
    FunctionBox* funbox = m.parser().newFunctionBox(fn, fun, outerpc, directives, NotGenerator);
    if (!funbox)
        return false;

    Directives newDirectives = directives;
    AsmJSParseContext funpc(&m.parser(), outerpc, fn, funbox, &newDirectives);
    if (!funpc.init(m.parser()))
        return false;

    if (!m.parser().functionArgsAndBodyGeneric(InAllowed, YieldIsName, fn, fun, Statement)) {
        if (tokenStream.hadError() || directives == newDirectives)
            return false;

        return m.fail(fn, "encountered new directive in function");
    }

    MOZ_ASSERT(!tokenStream.hadError());
    MOZ_ASSERT(directives == newDirectives);

    fn->pn_blockid = outerpc->blockid();

    *fnOut = fn;
    return true;
}

static bool
CheckFunction(ModuleValidator& m)
{
    // asm.js modules can be quite large when represented as parse trees so pop
    // the backing LifoAlloc after parsing/compiling each function.
    AsmJSParser::Mark mark = m.parser().mark();

    int64_t before = PRMJ_Now();

    ParseNode* fn = nullptr;
    unsigned line = 0, column = 0;
    if (!ParseFunction(m, &fn, &line, &column))
        return false;

    if (!CheckFunctionHead(m, fn))
        return false;

    if (m.tryOnceToValidateChangeHeap()) {
        bool validated;
        if (!CheckChangeHeap(m, fn, &validated))
            return false;
        if (validated)
            return true;
    }

    FunctionValidator f(m, fn);
    if (!f.init(FunctionName(fn), line, column))
        return m.fail(fn, "internal compiler failure (probably out of memory)");

    ParseNode* stmtIter = ListHead(FunctionStatementList(fn));

    if (!CheckProcessingDirectives(m, &stmtIter))
        return false;

    MallocSig::ArgVector args;
    if (!CheckArguments(f, &stmtIter, &args))
        return false;

    if (!CheckVariables(f, &stmtIter))
        return false;

    MOZ_ASSERT(!f.startedPacking(), "No bytecode should be written at this point.");
    MaybeAddInterruptCheck(f, InterruptCheckPosition::Head, fn);

    ParseNode* lastNonEmptyStmt = nullptr;
    for (; stmtIter; stmtIter = NextNode(stmtIter)) {
        if (!CheckStatement(f, stmtIter))
            return false;
        if (!IsEmptyStatement(stmtIter))
            lastNonEmptyStmt = stmtIter;
    }

    if (!CheckFinalReturn(f, lastNonEmptyStmt))
        return false;

    MallocSig sig(Move(args), f.returnedType());

    ModuleValidator::Func* func = nullptr;
    if (!CheckFunctionSignature(m, fn, sig, FunctionName(fn), &func))
        return false;

    if (func->defined())
        return m.failName(fn, "function '%s' already defined", FunctionName(fn));

    func->define(fn);

    if (!f.finish(func->index(), func->sig(), (PRMJ_Now() - before) / PRMJ_USEC_PER_MSEC))
        return m.fail(fn, "internal compiler failure (probably out of memory)");

    // Release the parser's lifo memory only after the last use of a parse node.
    m.parser().release(mark);
    return true;
}

static bool
CheckAllFunctionsDefined(ModuleValidator& m)
{
    for (unsigned i = 0; i < m.numFunctions(); i++) {
        ModuleValidator::Func& f = m.function(i);
        if (!f.defined())
            return m.failNameOffset(f.firstUse(), "missing definition of function %s", f.name());
    }

    return true;
}

static bool
CheckFunctions(ModuleValidator& m)
{
    while (true) {
        TokenKind tk;
        if (!PeekToken(m.parser(), &tk))
            return false;

        if (tk != TOK_FUNCTION)
            break;

        if (!CheckFunction(m))
            return false;
    }

    return CheckAllFunctionsDefined(m);
}

static bool
CheckFuncPtrTable(ModuleValidator& m, ParseNode* var)
{
    if (!IsDefinition(var))
        return m.fail(var, "function-pointer table name must be unique");

    ParseNode* arrayLiteral = MaybeDefinitionInitializer(var);
    if (!arrayLiteral || !arrayLiteral->isKind(PNK_ARRAY))
        return m.fail(var, "function-pointer table's initializer must be an array literal");

    unsigned length = ListLength(arrayLiteral);

    if (!IsPowerOfTwo(length))
        return m.failf(arrayLiteral, "function-pointer table length must be a power of 2 (is %u)", length);

    unsigned mask = length - 1;

    Vector<uint32_t> elemFuncIndices(m.cx());
    const LifoSig* sig = nullptr;
    for (ParseNode* elem = ListHead(arrayLiteral); elem; elem = NextNode(elem)) {
        if (!elem->isKind(PNK_NAME))
            return m.fail(elem, "function-pointer table's elements must be names of functions");

        PropertyName* funcName = elem->name();
        const ModuleValidator::Func* func = m.lookupFunction(funcName);
        if (!func)
            return m.fail(elem, "function-pointer table's elements must be names of functions");

        if (sig) {
            if (*sig != func->sig())
                return m.fail(elem, "all functions in table must have same signature");
        } else {
            sig = &func->sig();
        }

        if (!elemFuncIndices.append(func->index()))
            return false;
    }

    uint32_t funcPtrTableIndex;
    if (!CheckFuncPtrTableAgainstExisting(m, var, var->name(), *sig, mask, &funcPtrTableIndex))
        return false;

    if (!m.defineFuncPtrTable(funcPtrTableIndex, elemFuncIndices))
        return m.fail(var, "duplicate function-pointer definition");

    return true;
}

static bool
CheckFuncPtrTables(ModuleValidator& m)
{
    while (true) {
        ParseNode* varStmt;
        if (!ParseVarOrConstStatement(m.parser(), &varStmt))
            return false;
        if (!varStmt)
            break;
        for (ParseNode* var = VarListHead(varStmt); var; var = NextNode(var)) {
            if (!CheckFuncPtrTable(m, var))
                return false;
        }
    }

    for (unsigned i = 0; i < m.numFuncPtrTables(); i++) {
        ModuleValidator::FuncPtrTable& funcPtrTable = m.funcPtrTable(i);
        if (!funcPtrTable.defined()) {
            return m.failNameOffset(funcPtrTable.firstUse(),
                                    "function-pointer table %s wasn't defined",
                                    funcPtrTable.name());
        }
    }

    return true;
}

static bool
CheckModuleExportFunction(ModuleValidator& m, ParseNode* pn, PropertyName* maybeFieldName = nullptr)
{
    if (!pn->isKind(PNK_NAME))
        return m.fail(pn, "expected name of exported function");

    PropertyName* funcName = pn->name();
    const ModuleValidator::Global* global = m.lookupGlobal(funcName);
    if (!global)
        return m.failName(pn, "exported function name '%s' not found", funcName);

    if (global->which() == ModuleValidator::Global::Function)
        return m.addExport(pn, m.function(global->funcIndex()), maybeFieldName);

    if (global->which() == ModuleValidator::Global::ChangeHeap)
        return m.addChangeHeapExport(funcName, *global, maybeFieldName);

    return m.failName(pn, "'%s' is not a function", funcName);
}

static bool
CheckModuleExportObject(ModuleValidator& m, ParseNode* object)
{
    MOZ_ASSERT(object->isKind(PNK_OBJECT));

    for (ParseNode* pn = ListHead(object); pn; pn = NextNode(pn)) {
        if (!IsNormalObjectField(m.cx(), pn))
            return m.fail(pn, "only normal object properties may be used in the export object literal");

        PropertyName* fieldName = ObjectNormalFieldName(m.cx(), pn);

        ParseNode* initNode = ObjectNormalFieldInitializer(m.cx(), pn);
        if (!initNode->isKind(PNK_NAME))
            return m.fail(initNode, "initializer of exported object literal must be name of function");

        if (!CheckModuleExportFunction(m, initNode, fieldName))
            return false;
    }

    return true;
}

static bool
CheckModuleReturn(ModuleValidator& m)
{
    TokenKind tk;
    if (!GetToken(m.parser(), &tk))
        return false;
    TokenStream& ts = m.parser().tokenStream;
    if (tk != TOK_RETURN) {
        const char* msg = (tk == TOK_RC || tk == TOK_EOF)
                          ? "expecting return statement"
                          : "invalid asm.js. statement";
        return m.failOffset(ts.currentToken().pos.begin, msg);
    }
    ts.ungetToken();

    ParseNode* returnStmt = m.parser().statement(YieldIsName);
    if (!returnStmt)
        return false;

    ParseNode* returnExpr = ReturnExpr(returnStmt);
    if (!returnExpr)
        return m.fail(returnStmt, "export statement must return something");

    if (returnExpr->isKind(PNK_OBJECT)) {
        if (!CheckModuleExportObject(m, returnExpr))
            return false;
    } else {
        if (!CheckModuleExportFunction(m, returnExpr))
            return false;
    }

    // Function statements are not added to the lexical scope in ParseContext
    // (since cx->tempLifoAlloc is marked/released after each function
    // statement) and thus all the identifiers in the return statement will be
    // mistaken as free variables and added to lexdeps. Clear these now.
    m.parser().pc->lexdeps->clear();
    return true;
}

static bool
CheckModuleEnd(ModuleValidator &m)
{
    TokenKind tk;
    if (!GetToken(m.parser(), &tk))
        return false;

    if (tk != TOK_EOF && tk != TOK_RC) {
        return m.failOffset(m.parser().tokenStream.currentToken().pos.begin,
                            "top-level export (return) must be the last statement");
    }

    m.parser().tokenStream.ungetToken();
    return true;
}

static bool
CheckModule(ExclusiveContext* cx, AsmJSParser& parser, ParseNode* stmtList, HandleAsmJSModule obj,
            unsigned* time, SlowFunctionVector* slowFuncs)
{
    int64_t before = PRMJ_Now();

    ModuleValidator m(cx, parser);
    if (!m.init(obj))
        return false;

    if (PropertyName* moduleFunctionName = FunctionName(m.moduleFunctionNode())) {
        if (!CheckModuleLevelName(m, m.moduleFunctionNode(), moduleFunctionName))
            return false;
        m.initModuleFunctionName(moduleFunctionName);
    }

    if (!CheckFunctionHead(m, m.moduleFunctionNode()))
        return false;

    if (!CheckModuleArguments(m, m.moduleFunctionNode()))
        return false;

    if (!CheckPrecedingStatements(m, stmtList))
        return false;

    if (!CheckModuleProcessingDirectives(m))
        return false;

    if (!CheckModuleGlobals(m))
        return false;

    if (!m.startFunctionBodies())
        return false;

    if (!CheckFunctions(m))
        return false;

    if (!m.finishFunctionBodies())
        return false;

    if (!CheckFuncPtrTables(m))
        return false;

    if (!CheckModuleReturn(m))
        return false;

    if (!CheckModuleEnd(m))
        return false;

    if (!m.finish(slowFuncs))
        return false;

    *time = (PRMJ_Now() - before) / PRMJ_USEC_PER_MSEC;
    return true;
}

/*****************************************************************************/
// Runtime calls to asm.js module exports

static AsmJSModuleObject&
FunctionToModuleObject(JSFunction* fun)
{
    MOZ_ASSERT(IsAsmJSFunction(fun) || IsAsmJSModule(fun));
    const Value& v = fun->getExtendedSlot(FunctionExtended::ASM_MODULE_SLOT);
    return v.toObject().as<AsmJSModuleObject>();
}

static unsigned
FunctionToExportIndex(JSFunction* fun)
{
    MOZ_ASSERT(IsAsmJSFunction(fun));
    const Value& v = fun->getExtendedSlot(FunctionExtended::ASM_EXPORT_INDEX_SLOT);
    return v.toInt32();
}

static bool
ChangeHeap(JSContext* cx, AsmJSModule& module, const CallArgs& args)
{
    HandleValue bufferArg = args.get(0);
    if (!IsArrayBuffer(bufferArg)) {
        ReportIncompatible(cx, args);
        return false;
    }

    Rooted<ArrayBufferObject*> newBuffer(cx, &bufferArg.toObject().as<ArrayBufferObject>());
    uint32_t heapLength = newBuffer->byteLength();
    if (heapLength & module.heapLengthMask() ||
        heapLength < module.minHeapLength() ||
        heapLength > module.maxHeapLength())
    {
        args.rval().set(BooleanValue(false));
        return true;
    }

    if (!module.hasArrayView()) {
        args.rval().set(BooleanValue(true));
        return true;
    }

    MOZ_ASSERT(IsValidAsmJSHeapLength(heapLength));

    bool useSignalHandlers = module.wasmModule().compileArgs().useSignalHandlersForOOB;
    if (!ArrayBufferObject::prepareForAsmJS(cx, newBuffer, useSignalHandlers))
        return false;

    args.rval().set(BooleanValue(module.wasmModule().changeHeap(newBuffer, cx)));
    return true;
}

static bool
CallAsmJS(JSContext* cx, unsigned argc, Value* vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);
    RootedFunction callee(cx, &args.callee().as<JSFunction>());

    AsmJSModule& module = FunctionToModuleObject(callee).module();
    const AsmJSModule::Export& exp = module.exports()[FunctionToExportIndex(callee)];

    // The heap-changing function is a special-case and is implemented by C++.
    if (exp.isChangeHeap())
        return ChangeHeap(cx, module, args);

    return module.wasmModule().callExport(cx, exp.wasmIndex(), args);
}

/*****************************************************************************/
// Link-time validation

static bool
LinkFail(JSContext* cx, const char* str)
{
    JS_ReportErrorFlagsAndNumber(cx, JSREPORT_WARNING, GetErrorMessage,
                                 nullptr, JSMSG_USE_ASM_LINK_FAIL, str);
    return false;
}

static bool
GetDataProperty(JSContext* cx, HandleValue objVal, HandlePropertyName field, MutableHandleValue v)
{
    if (!objVal.isObject())
        return LinkFail(cx, "accessing property of non-object");

    RootedObject obj(cx, &objVal.toObject());
    if (IsScriptedProxy(obj))
        return LinkFail(cx, "accessing property of a Proxy");

    Rooted<PropertyDescriptor> desc(cx);
    RootedId id(cx, NameToId(field));
    if (!GetPropertyDescriptor(cx, obj, id, &desc))
        return false;

    if (!desc.object())
        return LinkFail(cx, "property not present on object");

    if (!desc.isDataDescriptor())
        return LinkFail(cx, "property is not a data property");

    v.set(desc.value());
    return true;
}

static bool
HasPureCoercion(JSContext* cx, HandleValue v)
{
    if (IsVectorObject<Int32x4>(v) || IsVectorObject<Float32x4>(v) || IsVectorObject<Bool32x4>(v))
        return true;

    // Ideally, we'd reject all non-SIMD non-primitives, but Emscripten has a
    // bug that generates code that passes functions for some imports. To avoid
    // breaking all the code that contains this bug, we make an exception for
    // functions that don't have user-defined valueOf or toString, for their
    // coercions are not observable and coercion via ToNumber/ToInt32
    // definitely produces NaN/0. We should remove this special case later once
    // most apps have been built with newer Emscripten.
    jsid toString = NameToId(cx->names().toString);
    if (v.toObject().is<JSFunction>() &&
        HasObjectValueOf(&v.toObject(), cx) &&
        ClassMethodIsNative(cx, &v.toObject().as<JSFunction>(), &JSFunction::class_, toString, fun_toString))
    {
        return true;
    }

    return false;
}

static bool
ValidateGlobalVariable(JSContext* cx, const AsmJSModule::Global& global, uint8_t* globalData,
                       HandleValue importVal)
{
    void* datum = globalData + global.varGlobalDataOffset();

    switch (global.varInitKind()) {
      case AsmJSModule::Global::InitConstant: {
        Val v = global.varInitVal();
        switch (v.type()) {
          case ValType::I32:
            *(int32_t*)datum = v.i32();
            break;
          case ValType::I64:
            MOZ_CRASH("int64");
          case ValType::F32:
            *(float*)datum = v.f32();
            break;
          case ValType::F64:
            *(double*)datum = v.f64();
            break;
          case ValType::I32x4:
          case ValType::B32x4:
            // Bool32x4 uses the same data layout as Int32x4.
            memcpy(datum, v.i32x4(), Simd128DataSize);
            break;
          case ValType::F32x4:
            memcpy(datum, v.f32x4(), Simd128DataSize);
            break;
        }
        break;
      }

      case AsmJSModule::Global::InitImport: {
        RootedPropertyName field(cx, global.varImportField());
        RootedValue v(cx);
        if (!GetDataProperty(cx, importVal, field, &v))
            return false;

        if (!v.isPrimitive() && !HasPureCoercion(cx, v))
            return LinkFail(cx, "Imported values must be primitives");

        switch (global.varInitImportType()) {
          case ValType::I32:
            if (!ToInt32(cx, v, (int32_t*)datum))
                return false;
            break;
          case ValType::I64:
            MOZ_CRASH("int64");
          case ValType::F32:
            if (!RoundFloat32(cx, v, (float*)datum))
                return false;
            break;
          case ValType::F64:
            if (!ToNumber(cx, v, (double*)datum))
                return false;
            break;
          case ValType::I32x4: {
            SimdConstant simdConstant;
            if (!ToSimdConstant<Int32x4>(cx, v, &simdConstant))
                return false;
            memcpy(datum, simdConstant.asInt32x4(), Simd128DataSize);
            break;
          }
          case ValType::F32x4: {
            SimdConstant simdConstant;
            if (!ToSimdConstant<Float32x4>(cx, v, &simdConstant))
                return false;
            memcpy(datum, simdConstant.asFloat32x4(), Simd128DataSize);
            break;
          }
          case ValType::B32x4: {
            SimdConstant simdConstant;
            if (!ToSimdConstant<Bool32x4>(cx, v, &simdConstant))
                return false;
            // Bool32x4 uses the same data layout as Int32x4.
            memcpy(datum, simdConstant.asInt32x4(), Simd128DataSize);
            break;
          }
        }
        break;
      }
    }

    return true;
}

static bool
ValidateFFI(JSContext* cx, const AsmJSModule::Global& global, HandleValue importVal,
            AutoVectorRooter<JSFunction*>* ffis)
{
    RootedPropertyName field(cx, global.ffiField());
    RootedValue v(cx);
    if (!GetDataProperty(cx, importVal, field, &v))
        return false;

    if (!v.isObject() || !v.toObject().is<JSFunction>())
        return LinkFail(cx, "FFI imports must be functions");

    (*ffis)[global.ffiIndex()].set(&v.toObject().as<JSFunction>());
    return true;
}

static bool
ValidateArrayView(JSContext* cx, const AsmJSModule::Global& global, HandleValue globalVal)
{
    RootedPropertyName field(cx, global.maybeViewName());
    if (!field)
        return true;

    RootedValue v(cx);
    if (!GetDataProperty(cx, globalVal, field, &v))
        return false;

    bool tac = IsTypedArrayConstructor(v, global.viewType());
    if (!tac)
        return LinkFail(cx, "bad typed array constructor");

    return true;
}

static bool
ValidateByteLength(JSContext* cx, HandleValue globalVal)
{
    RootedPropertyName field(cx, cx->names().byteLength);
    RootedValue v(cx);
    if (!GetDataProperty(cx, globalVal, field, &v))
        return false;

    if (!v.isObject() || !v.toObject().isBoundFunction())
        return LinkFail(cx, "byteLength must be a bound function object");

    RootedFunction fun(cx, &v.toObject().as<JSFunction>());

    RootedValue boundTarget(cx, ObjectValue(*fun->getBoundFunctionTarget()));
    if (!IsNativeFunction(boundTarget, fun_call))
        return LinkFail(cx, "bound target of byteLength must be Function.prototype.call");

    RootedValue boundThis(cx, fun->getBoundFunctionThis());
    if (!IsNativeFunction(boundThis, ArrayBufferObject::byteLengthGetter))
        return LinkFail(cx, "bound this value must be ArrayBuffer.protototype.byteLength accessor");

    return true;
}

static bool
ValidateMathBuiltinFunction(JSContext* cx, const AsmJSModule::Global& global, HandleValue globalVal)
{
    RootedValue v(cx);
    if (!GetDataProperty(cx, globalVal, cx->names().Math, &v))
        return false;

    RootedPropertyName field(cx, global.mathName());
    if (!GetDataProperty(cx, v, field, &v))
        return false;

    Native native = nullptr;
    switch (global.mathBuiltinFunction()) {
      case AsmJSMathBuiltin_sin: native = math_sin; break;
      case AsmJSMathBuiltin_cos: native = math_cos; break;
      case AsmJSMathBuiltin_tan: native = math_tan; break;
      case AsmJSMathBuiltin_asin: native = math_asin; break;
      case AsmJSMathBuiltin_acos: native = math_acos; break;
      case AsmJSMathBuiltin_atan: native = math_atan; break;
      case AsmJSMathBuiltin_ceil: native = math_ceil; break;
      case AsmJSMathBuiltin_floor: native = math_floor; break;
      case AsmJSMathBuiltin_exp: native = math_exp; break;
      case AsmJSMathBuiltin_log: native = math_log; break;
      case AsmJSMathBuiltin_pow: native = math_pow; break;
      case AsmJSMathBuiltin_sqrt: native = math_sqrt; break;
      case AsmJSMathBuiltin_min: native = math_min; break;
      case AsmJSMathBuiltin_max: native = math_max; break;
      case AsmJSMathBuiltin_abs: native = math_abs; break;
      case AsmJSMathBuiltin_atan2: native = math_atan2; break;
      case AsmJSMathBuiltin_imul: native = math_imul; break;
      case AsmJSMathBuiltin_clz32: native = math_clz32; break;
      case AsmJSMathBuiltin_fround: native = math_fround; break;
    }

    if (!IsNativeFunction(v, native))
        return LinkFail(cx, "bad Math.* builtin function");

    return true;
}

static PropertyName*
SimdTypeToName(JSContext* cx, AsmJSSimdType type)
{
    switch (type) {
      case AsmJSSimdType_int32x4:   return cx->names().int32x4;
      case AsmJSSimdType_float32x4: return cx->names().float32x4;
      case AsmJSSimdType_bool32x4:  return cx->names().bool32x4;
    }
    MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("unexpected SIMD type");
}

static SimdTypeDescr::Type
AsmJSSimdTypeToTypeDescrType(AsmJSSimdType type)
{
    switch (type) {
      case AsmJSSimdType_int32x4: return Int32x4::type;
      case AsmJSSimdType_float32x4: return Float32x4::type;
      case AsmJSSimdType_bool32x4: return Bool32x4::type;
    }
    MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("unexpected AsmJSSimdType");
}

static bool
ValidateSimdType(JSContext* cx, const AsmJSModule::Global& global, HandleValue globalVal,
                 MutableHandleValue out)
{
    RootedValue v(cx);
    if (!GetDataProperty(cx, globalVal, cx->names().SIMD, &v))
        return false;

    AsmJSSimdType type;
    if (global.which() == AsmJSModule::Global::SimdCtor)
        type = global.simdCtorType();
    else
        type = global.simdOperationType();

    RootedPropertyName simdTypeName(cx, SimdTypeToName(cx, type));
    if (!GetDataProperty(cx, v, simdTypeName, &v))
        return false;

    if (!v.isObject())
        return LinkFail(cx, "bad SIMD type");

    RootedObject simdDesc(cx, &v.toObject());
    if (!simdDesc->is<SimdTypeDescr>())
        return LinkFail(cx, "bad SIMD type");

    if (AsmJSSimdTypeToTypeDescrType(type) != simdDesc->as<SimdTypeDescr>().type())
        return LinkFail(cx, "bad SIMD type");

    out.set(v);
    return true;
}

static bool
ValidateSimdType(JSContext* cx, const AsmJSModule::Global& global, HandleValue globalVal)
{
    RootedValue _(cx);
    return ValidateSimdType(cx, global, globalVal, &_);
}

static bool
ValidateSimdOperation(JSContext* cx, const AsmJSModule::Global& global, HandleValue globalVal)
{
    // SIMD operations are loaded from the SIMD type, so the type must have been
    // validated before the operation.
    RootedValue v(cx);
    JS_ALWAYS_TRUE(ValidateSimdType(cx, global, globalVal, &v));

    RootedPropertyName opName(cx, global.simdOperationName());
    if (!GetDataProperty(cx, v, opName, &v))
        return false;

    Native native = nullptr;
    switch (global.simdOperationType()) {
#define SET_NATIVE_INT32X4(op) case AsmJSSimdOperation_##op: native = simd_int32x4_##op; break;
#define SET_NATIVE_FLOAT32X4(op) case AsmJSSimdOperation_##op: native = simd_float32x4_##op; break;
#define SET_NATIVE_BOOL32X4(op) case AsmJSSimdOperation_##op: native = simd_bool32x4_##op; break;
#define FALLTHROUGH(op) case AsmJSSimdOperation_##op:
      case AsmJSSimdType_int32x4:
        switch (global.simdOperation()) {
          FORALL_INT32X4_ASMJS_OP(SET_NATIVE_INT32X4)
          default:
            MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("shouldn't have been validated in the first "
                                                    "place");
        }
        break;
      case AsmJSSimdType_float32x4:
        switch (global.simdOperation()) {
          FORALL_FLOAT32X4_ASMJS_OP(SET_NATIVE_FLOAT32X4)
          default:
             MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("shouldn't have been validated in the first "
                                                     "place");
        }
        break;
      case AsmJSSimdType_bool32x4:
        switch (global.simdOperation()) {
          FORALL_BOOL_SIMD_OP(SET_NATIVE_BOOL32X4)
          default:
             MOZ_MAKE_COMPILER_ASSUME_IS_UNREACHABLE("shouldn't have been validated in the first "
                                                     "place");
        }
        break;
#undef FALLTHROUGH
#undef SET_NATIVE_FLOAT32X4
#undef SET_NATIVE_INT32X4
#undef SET_NATIVE_BOOL32X4
#undef SET_NATIVE
    }
    if (!native || !IsNativeFunction(v, native))
        return LinkFail(cx, "bad SIMD.type.* operation");
    return true;
}

static bool
ValidateAtomicsBuiltinFunction(JSContext* cx, const AsmJSModule::Global& global, HandleValue globalVal)
{
    RootedValue v(cx);
    if (!GetDataProperty(cx, globalVal, cx->names().Atomics, &v))
        return false;
    RootedPropertyName field(cx, global.atomicsName());
    if (!GetDataProperty(cx, v, field, &v))
        return false;

    Native native = nullptr;
    switch (global.atomicsBuiltinFunction()) {
      case AsmJSAtomicsBuiltin_compareExchange: native = atomics_compareExchange; break;
      case AsmJSAtomicsBuiltin_exchange: native = atomics_exchange; break;
      case AsmJSAtomicsBuiltin_load: native = atomics_load; break;
      case AsmJSAtomicsBuiltin_store: native = atomics_store; break;
      case AsmJSAtomicsBuiltin_fence: native = atomics_fence; break;
      case AsmJSAtomicsBuiltin_add: native = atomics_add; break;
      case AsmJSAtomicsBuiltin_sub: native = atomics_sub; break;
      case AsmJSAtomicsBuiltin_and: native = atomics_and; break;
      case AsmJSAtomicsBuiltin_or: native = atomics_or; break;
      case AsmJSAtomicsBuiltin_xor: native = atomics_xor; break;
      case AsmJSAtomicsBuiltin_isLockFree: native = atomics_isLockFree; break;
    }

    if (!IsNativeFunction(v, native))
        return LinkFail(cx, "bad Atomics.* builtin function");

    return true;
}

static bool
ValidateConstant(JSContext* cx, const AsmJSModule::Global& global, HandleValue globalVal)
{
    RootedPropertyName field(cx, global.constantName());
    RootedValue v(cx, globalVal);

    if (global.constantKind() == AsmJSModule::Global::MathConstant) {
        if (!GetDataProperty(cx, v, cx->names().Math, &v))
            return false;
    }

    if (!GetDataProperty(cx, v, field, &v))
        return false;

    if (!v.isNumber())
        return LinkFail(cx, "math / global constant value needs to be a number");

    // NaN != NaN
    if (IsNaN(global.constantValue())) {
        if (!IsNaN(v.toNumber()))
            return LinkFail(cx, "global constant value needs to be NaN");
    } else {
        if (v.toNumber() != global.constantValue())
            return LinkFail(cx, "global constant value mismatch");
    }

    return true;
}

static bool
CheckBuffer(JSContext* cx, AsmJSModule& module, HandleValue bufferVal,
            MutableHandle<ArrayBufferObjectMaybeShared*> buffer)
{
    if (module.isSharedView() && !IsSharedArrayBuffer(bufferVal))
        return LinkFail(cx, "shared views can only be constructed onto SharedArrayBuffer");

    if (!module.isSharedView() && !IsArrayBuffer(bufferVal))
        return LinkFail(cx, "unshared views can only be constructed onto ArrayBuffer");

    buffer.set(&AsAnyArrayBuffer(bufferVal));
    uint32_t heapLength = buffer->byteLength();

    if (!IsValidAsmJSHeapLength(heapLength)) {
        UniqueChars msg(
            JS_smprintf("ArrayBuffer byteLength 0x%x is not a valid heap length. The next "
                        "valid length is 0x%x",
                        heapLength,
                        RoundUpToNextValidAsmJSHeapLength(heapLength)));
        return LinkFail(cx, msg.get());
    }

    // This check is sufficient without considering the size of the loaded datum because heap
    // loads and stores start on an aligned boundary and the heap byteLength has larger alignment.
    MOZ_ASSERT((module.minHeapLength() - 1) <= INT32_MAX);
    if (heapLength < module.minHeapLength()) {
        UniqueChars msg(
            JS_smprintf("ArrayBuffer byteLength of 0x%x is less than 0x%x (the size implied "
                        "by const heap accesses and/or change-heap minimum-length requirements).",
                        heapLength,
                        module.minHeapLength()));
        return LinkFail(cx, msg.get());
    }

    if (heapLength > module.maxHeapLength()) {
        UniqueChars msg(
            JS_smprintf("ArrayBuffer byteLength 0x%x is greater than maximum length of 0x%x",
                        heapLength,
                        module.maxHeapLength()));
        return LinkFail(cx, msg.get());
    }

    // Shell builtins may have disabled signal handlers since the module we're
    // cloning was compiled. LookupAsmJSModuleInCache checks for signal handlers
    // as well for the caching case.
    if (module.wasmModule().compileArgs() != CompileArgs(cx))
        return LinkFail(cx, "Signals have been toggled since compilation");

    if (buffer->is<ArrayBufferObject>()) {
        Rooted<ArrayBufferObject*> abheap(cx, &buffer->as<ArrayBufferObject>());
        bool useSignalHandlers = module.wasmModule().compileArgs().useSignalHandlersForOOB;
        if (!ArrayBufferObject::prepareForAsmJS(cx, abheap, useSignalHandlers))
            return LinkFail(cx, "Unable to prepare ArrayBuffer for asm.js use");
    }

    return true;
}

static bool
DynamicallyLinkModule(JSContext* cx, const CallArgs& args, AsmJSModule& module)
{
    HandleValue globalVal = args.get(0);
    HandleValue importVal = args.get(1);
    HandleValue bufferVal = args.get(2);

    Rooted<ArrayBufferObjectMaybeShared*> buffer(cx);
    if (module.hasArrayView() && !CheckBuffer(cx, module, bufferVal, &buffer))
        return false;

    AutoVectorRooter<JSFunction*> ffis(cx);
    if (!ffis.resize(module.numFFIs()))
        return false;

    for (const AsmJSModule::Global& global : module.globals()) {
        switch (global.which()) {
          case AsmJSModule::Global::Variable:
            if (!ValidateGlobalVariable(cx, global, module.wasmModule().globalData(), importVal))
                return false;
            break;
          case AsmJSModule::Global::FFI:
            if (!ValidateFFI(cx, global, importVal, &ffis))
                return false;
            break;
          case AsmJSModule::Global::ArrayView:
          case AsmJSModule::Global::ArrayViewCtor:
            if (!ValidateArrayView(cx, global, globalVal))
                return false;
            break;
          case AsmJSModule::Global::ByteLength:
            if (!ValidateByteLength(cx, globalVal))
                return false;
            break;
          case AsmJSModule::Global::MathBuiltinFunction:
            if (!ValidateMathBuiltinFunction(cx, global, globalVal))
                return false;
            break;
          case AsmJSModule::Global::AtomicsBuiltinFunction:
            if (!ValidateAtomicsBuiltinFunction(cx, global, globalVal))
                return false;
            break;
          case AsmJSModule::Global::Constant:
            if (!ValidateConstant(cx, global, globalVal))
                return false;
            break;
          case AsmJSModule::Global::SimdCtor:
            if (!ValidateSimdType(cx, global, globalVal))
                return false;
            break;
          case AsmJSModule::Global::SimdOperation:
            if (!ValidateSimdOperation(cx, global, globalVal))
                return false;
            break;
        }
    }

    AutoVectorRooter<JSFunction*> imports(cx);
    for (const AsmJSModule::Import& import : module.imports()) {
        if (!imports.append(ffis[import.ffiIndex()]))
            return false;
    }

    return module.wasmModule().dynamicallyLink(cx, buffer, imports);
}

static JSFunction*
NewExportedFunction(JSContext* cx, const AsmJSModule& module, const AsmJSModule::Export& func,
                    HandleObject moduleObj, unsigned exportIndex)
{
    unsigned numArgs = func.isChangeHeap()
                       ? 1
                       : module.wasmModule().exports()[func.wasmIndex()].sig().args().length();

    RootedPropertyName name(cx, func.name());
    JSFunction* fun =
        NewNativeConstructor(cx, CallAsmJS, numArgs, name,
                             gc::AllocKind::FUNCTION_EXTENDED, GenericObject,
                             JSFunction::ASMJS_CTOR);
    if (!fun)
        return nullptr;

    fun->setExtendedSlot(FunctionExtended::ASM_MODULE_SLOT, ObjectValue(*moduleObj));
    fun->setExtendedSlot(FunctionExtended::ASM_EXPORT_INDEX_SLOT, Int32Value(exportIndex));
    return fun;
}

static bool
HandleDynamicLinkFailure(JSContext* cx, const CallArgs& args, AsmJSModule& module,
                         HandlePropertyName name)
{
    if (cx->isExceptionPending())
        return false;

    ScriptSource* source = module.scriptSource();

    // Source discarding is allowed to affect JS semantics because it is never
    // enabled for normal JS content.
    bool haveSource = source->hasSourceData();
    if (!haveSource && !JSScript::loadSource(cx, source, &haveSource))
        return false;
    if (!haveSource) {
        JS_ReportError(cx, "asm.js link failure with source discarding enabled");
        return false;
    }

    uint32_t begin = module.srcBodyStart();  // starts right after 'use asm'
    uint32_t end = module.srcEndBeforeCurly();
    Rooted<JSFlatString*> src(cx, source->substringDontDeflate(cx, begin, end));
    if (!src)
        return false;

    RootedFunction fun(cx, NewScriptedFunction(cx, 0, JSFunction::INTERPRETED_NORMAL,
                                               name, gc::AllocKind::FUNCTION,
                                               TenuredObject));
    if (!fun)
        return false;

    Rooted<PropertyNameVector> formals(cx, PropertyNameVector(cx));
    if (!formals.reserve(3))
        return false;

    if (module.globalArgumentName())
        formals.infallibleAppend(module.globalArgumentName());
    if (module.importArgumentName())
        formals.infallibleAppend(module.importArgumentName());
    if (module.bufferArgumentName())
        formals.infallibleAppend(module.bufferArgumentName());

    CompileOptions options(cx);
    options.setMutedErrors(source->mutedErrors())
           .setFile(source->filename())
           .setNoScriptRval(false);

    // The exported function inherits an implicit strict context if the module
    // also inherited it somehow.
    if (module.strict())
        options.strictOption = true;

    AutoStableStringChars stableChars(cx);
    if (!stableChars.initTwoByte(cx, src))
        return false;

    const char16_t* chars = stableChars.twoByteRange().start().get();
    SourceBufferHolder::Ownership ownership = stableChars.maybeGiveOwnershipToCaller()
                                              ? SourceBufferHolder::GiveOwnership
                                              : SourceBufferHolder::NoOwnership;
    SourceBufferHolder srcBuf(chars, end - begin, ownership);
    if (!frontend::CompileFunctionBody(cx, &fun, options, formals, srcBuf))
        return false;

    // Call the function we just recompiled.
    args.setCallee(ObjectValue(*fun));
    return Invoke(cx, args, args.isConstructing() ? CONSTRUCT : NO_CONSTRUCT);
}

static JSObject*
CreateExportObject(JSContext* cx, HandleAsmJSModule moduleObj)
{
    AsmJSModule& module = moduleObj->module();
    const AsmJSModule::ExportVector& exports = module.exports();

    if (exports.length() == 1) {
        const AsmJSModule::Export& func = exports[0];
        if (!func.maybeFieldName())
            return NewExportedFunction(cx, module, func, moduleObj, 0);
    }

    gc::AllocKind allocKind = gc::GetGCObjectKind(exports.length());
    RootedPlainObject obj(cx, NewBuiltinClassInstance<PlainObject>(cx, allocKind));
    if (!obj)
        return nullptr;

    for (unsigned i = 0; i < exports.length(); i++) {
        const AsmJSModule::Export& func = exports[i];

        RootedFunction fun(cx, NewExportedFunction(cx, module, func, moduleObj, i));
        if (!fun)
            return nullptr;

        MOZ_ASSERT(func.maybeFieldName() != nullptr);
        RootedId id(cx, NameToId(func.maybeFieldName()));
        RootedValue val(cx, ObjectValue(*fun));
        if (!NativeDefineProperty(cx, obj, id, val, nullptr, nullptr, JSPROP_ENUMERATE))
            return nullptr;
    }

    return obj;
}

// Implements the semantics of an asm.js module function that has been successfully validated.
static bool
LinkAsmJS(JSContext* cx, unsigned argc, JS::Value* vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);

    // The LinkAsmJS builtin (created by NewAsmJSModuleFunction) is an extended
    // function and stores its module in an extended slot.
    RootedFunction fun(cx, &args.callee().as<JSFunction>());
    Rooted<AsmJSModuleObject*> moduleObj(cx, &FunctionToModuleObject(fun));

    // When a module is linked, it is dynamically specialized to the given
    // arguments (buffer, ffis). Thus, if the module is linked again (it is just
    // a function so it can be called multiple times), we need to clone a new
    // module.
    if (moduleObj->module().wasmModule().dynamicallyLinked()) {
        Rooted<AsmJSModuleObject*> clone(cx, NewAsmJSModuleObject(cx));
        if (!clone)
            return false;

        if (!moduleObj->module().clone(cx, clone))
            return false;

        moduleObj = clone;
    }

    AsmJSModule& module = moduleObj->module();

    // Link the module by performing the link-time validation checks in the
    // asm.js spec and then patching the generated module to associate it with
    // the given heap (ArrayBuffer) and a new global data segment (the closure
    // state shared by the inner asm.js functions).
    if (!DynamicallyLinkModule(cx, args, module)) {
        // Linking failed, so reparse the entire asm.js module from scratch to
        // get normal interpreted bytecode which we can simply Invoke. Very slow.
        RootedPropertyName name(cx, fun->name());
        return HandleDynamicLinkFailure(cx, args, module, name);
    }

    // Link-time validation succeeded, so wrap all the exported functions with
    // CallAsmJS builtins that trampoline into the generated code.
    JSObject* obj = CreateExportObject(cx, moduleObj);
    if (!obj)
        return false;

    args.rval().set(ObjectValue(*obj));
    return true;
}

static JSFunction*
NewModuleFunction(ExclusiveContext* cx, JSFunction* origFun, HandleObject moduleObj)
{
    RootedPropertyName name(cx, origFun->name());

    JSFunction::Flags flags = origFun->isLambda() ? JSFunction::ASMJS_LAMBDA_CTOR
                                                  : JSFunction::ASMJS_CTOR;
    JSFunction* moduleFun =
        NewNativeConstructor(cx, LinkAsmJS, origFun->nargs(), name,
                             gc::AllocKind::FUNCTION_EXTENDED, TenuredObject,
                             flags);
    if (!moduleFun)
        return nullptr;

    moduleFun->setExtendedSlot(FunctionExtended::ASM_MODULE_SLOT, ObjectValue(*moduleObj));
    return moduleFun;
}

/*****************************************************************************/
// Caching and cloning

uint8_t*
AsmJSModule::Global::serialize(uint8_t* cursor) const
{
    cursor = WriteBytes(cursor, &pod, sizeof(pod));
    cursor = SerializeName(cursor, name_);
    return cursor;
}

size_t
AsmJSModule::Global::serializedSize() const
{
    return sizeof(pod) +
           SerializedNameSize(name_);
}

const uint8_t*
AsmJSModule::Global::deserialize(ExclusiveContext* cx, const uint8_t* cursor)
{
    (cursor = ReadBytes(cursor, &pod, sizeof(pod))) &&
    (cursor = DeserializeName(cx, cursor, &name_));
    return cursor;
}

bool
AsmJSModule::Global::clone(JSContext* cx, Global* out) const
{
    *out = *this;
    return true;
}

uint8_t*
AsmJSModule::Export::serialize(uint8_t* cursor) const
{
    cursor = SerializeName(cursor, name_);
    cursor = SerializeName(cursor, maybeFieldName_);
    cursor = WriteBytes(cursor, &pod, sizeof(pod));
    return cursor;
}

size_t
AsmJSModule::Export::serializedSize() const
{
    return SerializedNameSize(name_) +
           SerializedNameSize(maybeFieldName_) +
           sizeof(pod);
}

const uint8_t*
AsmJSModule::Export::deserialize(ExclusiveContext* cx, const uint8_t* cursor)
{
    (cursor = DeserializeName(cx, cursor, &name_)) &&
    (cursor = DeserializeName(cx, cursor, &maybeFieldName_)) &&
    (cursor = ReadBytes(cursor, &pod, sizeof(pod)));
    return cursor;
}

bool
AsmJSModule::Export::clone(JSContext* cx, Export* out) const
{
    out->name_ = name_;
    out->maybeFieldName_ = maybeFieldName_;
    out->pod = pod;
    return true;
}

size_t
AsmJSModule::serializedSize() const
{
    MOZ_ASSERT(isFinished());
    return wasmModule_->serializedSize() +
           linkData_->serializedSize() +
           sizeof(pod) +
           SerializedVectorSize(globals_) +
           SerializedPodVectorSize(imports_) +
           SerializedVectorSize(exports_) +
           SerializedNameSize(globalArgumentName_) +
           SerializedNameSize(importArgumentName_) +
           SerializedNameSize(bufferArgumentName_);
}

uint8_t*
AsmJSModule::serialize(uint8_t* cursor) const
{
    MOZ_ASSERT(isFinished());
    cursor = wasmModule_->serialize(cursor);
    cursor = linkData_->serialize(cursor);
    cursor = WriteBytes(cursor, &pod, sizeof(pod));
    cursor = SerializeVector(cursor, globals_);
    cursor = SerializePodVector(cursor, imports_);
    cursor = SerializeVector(cursor, exports_);
    cursor = SerializeName(cursor, globalArgumentName_);
    cursor = SerializeName(cursor, importArgumentName_);
    cursor = SerializeName(cursor, bufferArgumentName_);
    return cursor;
}

const uint8_t*
AsmJSModule::deserialize(ExclusiveContext* cx, const uint8_t* cursor)
{
    linkData_ = cx->make_unique<StaticLinkData>();
    if (!linkData_)
        return nullptr;

    // To avoid GC-during-deserialization corner cases, prevent atoms from
    // being collected.
    AutoKeepAtoms aka(cx->perThreadData);

    (cursor = Module::deserialize(cx, cursor, &wasmModule_)) &&
    (cursor = linkData_->deserialize(cx, cursor)) &&
    (cursor = ReadBytes(cursor, &pod, sizeof(pod))) &&
    (cursor = DeserializeVector(cx, cursor, &globals_)) &&
    (cursor = DeserializePodVector(cx, cursor, &imports_)) &&
    (cursor = DeserializeVector(cx, cursor, &exports_)) &&
    (cursor = DeserializeName(cx, cursor, &globalArgumentName_)) &&
    (cursor = DeserializeName(cx, cursor, &importArgumentName_)) &&
    (cursor = DeserializeName(cx, cursor, &bufferArgumentName_));

    return cursor;
}

bool
AsmJSModule::clone(JSContext* cx, HandleAsmJSModule obj) const
{
    auto out = cx->new_<AsmJSModule>(scriptSource(), srcStart_, srcBodyStart_, pod.strict_);
    if (!out)
        return false;

    obj->setModule(out);

    out->wasmModule_ = wasmModule_->clone(cx, *linkData_);
    if (!out->wasmModule_)
        return false;

    out->linkData_ = cx->make_unique<StaticLinkData>();
    if (!out->linkData_ || !linkData_->clone(cx, out->linkData_.get()))
        return false;

    out->pod = pod;

    if (!CloneVector(cx, globals_, &out->globals_) ||
        !ClonePodVector(cx, imports_, &out->imports_) ||
        !CloneVector(cx, exports_, &out->exports_))
    {
        return false;
    }

    out->globalArgumentName_ = globalArgumentName_;
    out->importArgumentName_ = importArgumentName_;
    out->bufferArgumentName_ = bufferArgumentName_;
    return true;
}

void
AsmJSModule::addSizeOfMisc(mozilla::MallocSizeOf mallocSizeOf, size_t* code, size_t* data)
{
    if (wasmModule_)
        wasmModule_->addSizeOfMisc(mallocSizeOf, code, data);

    if (linkData_)
        *data += linkData_->sizeOfExcludingThis(mallocSizeOf);

    *data += mallocSizeOf(this) +
             globals_.sizeOfExcludingThis(mallocSizeOf) +
             imports_.sizeOfExcludingThis(mallocSizeOf) +
             exports_.sizeOfExcludingThis(mallocSizeOf);
}

namespace {

struct PropertyNameWrapper
{
    PropertyName* name;

    PropertyNameWrapper()
      : name(nullptr)
    {}
    explicit PropertyNameWrapper(PropertyName* name)
      : name(name)
    {}
    size_t serializedSize() const {
        return SerializedNameSize(name);
    }
    uint8_t* serialize(uint8_t* cursor) const {
        return SerializeName(cursor, name);
    }
    const uint8_t* deserialize(ExclusiveContext* cx, const uint8_t* cursor) {
        return DeserializeName(cx, cursor, &name);
    }
};

class ModuleChars
{
  protected:
    uint32_t isFunCtor_;
    Vector<PropertyNameWrapper, 0, SystemAllocPolicy> funCtorArgs_;

  public:
    static uint32_t beginOffset(AsmJSParser& parser) {
        return parser.pc->maybeFunction->pn_pos.begin;
    }

    static uint32_t endOffset(AsmJSParser& parser) {
        TokenPos pos(0, 0);  // initialize to silence GCC warning
        MOZ_ALWAYS_TRUE(parser.tokenStream.peekTokenPos(&pos, TokenStream::Operand));
        return pos.end;
    }
};

class ModuleCharsForStore : ModuleChars
{
    uint32_t uncompressedSize_;
    uint32_t compressedSize_;
    Vector<char, 0, SystemAllocPolicy> compressedBuffer_;

  public:
    bool init(AsmJSParser& parser) {
        MOZ_ASSERT(beginOffset(parser) < endOffset(parser));

        uncompressedSize_ = (endOffset(parser) - beginOffset(parser)) * sizeof(char16_t);
        size_t maxCompressedSize = LZ4::maxCompressedSize(uncompressedSize_);
        if (maxCompressedSize < uncompressedSize_)
            return false;

        if (!compressedBuffer_.resize(maxCompressedSize))
            return false;

        const char16_t* chars = parser.tokenStream.rawCharPtrAt(beginOffset(parser));
        const char* source = reinterpret_cast<const char*>(chars);
        size_t compressedSize = LZ4::compress(source, uncompressedSize_, compressedBuffer_.begin());
        if (!compressedSize || compressedSize > UINT32_MAX)
            return false;

        compressedSize_ = compressedSize;

        // For a function statement or named function expression:
        //   function f(x,y,z) { abc }
        // the range [beginOffset, endOffset) captures the source:
        //   f(x,y,z) { abc }
        // An unnamed function expression captures the same thing, sans 'f'.
        // Since asm.js modules do not contain any free variables, equality of
        // [beginOffset, endOffset) is sufficient to guarantee identical code
        // generation, modulo MachineId.
        //
        // For functions created with 'new Function', function arguments are
        // not present in the source so we must manually explicitly serialize
        // and match the formals as a Vector of PropertyName.
        isFunCtor_ = parser.pc->isFunctionConstructorBody();
        if (isFunCtor_) {
            unsigned numArgs;
            ParseNode* arg = FunctionArgsList(parser.pc->maybeFunction, &numArgs);
            for (unsigned i = 0; i < numArgs; i++, arg = arg->pn_next) {
                if (!funCtorArgs_.append(arg->name()))
                    return false;
            }
        }

        return true;
    }

    size_t serializedSize() const {
        return sizeof(uint32_t) +
               sizeof(uint32_t) +
               compressedSize_ +
               sizeof(uint32_t) +
               (isFunCtor_ ? SerializedVectorSize(funCtorArgs_) : 0);
    }

    uint8_t* serialize(uint8_t* cursor) const {
        cursor = WriteScalar<uint32_t>(cursor, uncompressedSize_);
        cursor = WriteScalar<uint32_t>(cursor, compressedSize_);
        cursor = WriteBytes(cursor, compressedBuffer_.begin(), compressedSize_);
        cursor = WriteScalar<uint32_t>(cursor, isFunCtor_);
        if (isFunCtor_)
            cursor = SerializeVector(cursor, funCtorArgs_);
        return cursor;
    }
};

class ModuleCharsForLookup : ModuleChars
{
    Vector<char16_t, 0, SystemAllocPolicy> chars_;

  public:
    const uint8_t* deserialize(ExclusiveContext* cx, const uint8_t* cursor) {
        uint32_t uncompressedSize;
        cursor = ReadScalar<uint32_t>(cursor, &uncompressedSize);

        uint32_t compressedSize;
        cursor = ReadScalar<uint32_t>(cursor, &compressedSize);

        if (!chars_.resize(uncompressedSize / sizeof(char16_t)))
            return nullptr;

        const char* source = reinterpret_cast<const char*>(cursor);
        char* dest = reinterpret_cast<char*>(chars_.begin());
        if (!LZ4::decompress(source, dest, uncompressedSize))
            return nullptr;

        cursor += compressedSize;

        cursor = ReadScalar<uint32_t>(cursor, &isFunCtor_);
        if (isFunCtor_)
            cursor = DeserializeVector(cx, cursor, &funCtorArgs_);

        return cursor;
    }

    bool match(AsmJSParser& parser) const {
        const char16_t* parseBegin = parser.tokenStream.rawCharPtrAt(beginOffset(parser));
        const char16_t* parseLimit = parser.tokenStream.rawLimit();
        MOZ_ASSERT(parseLimit >= parseBegin);
        if (uint32_t(parseLimit - parseBegin) < chars_.length())
            return false;
        if (!PodEqual(chars_.begin(), parseBegin, chars_.length()))
            return false;
        if (isFunCtor_ != parser.pc->isFunctionConstructorBody())
            return false;
        if (isFunCtor_) {
            // For function statements, the closing } is included as the last
            // character of the matched source. For Function constructor,
            // parsing terminates with EOF which we must explicitly check. This
            // prevents
            //   new Function('"use asm"; function f() {} return f')
            // from incorrectly matching
            //   new Function('"use asm"; function f() {} return ff')
            if (parseBegin + chars_.length() != parseLimit)
                return false;
            unsigned numArgs;
            ParseNode* arg = FunctionArgsList(parser.pc->maybeFunction, &numArgs);
            if (funCtorArgs_.length() != numArgs)
                return false;
            for (unsigned i = 0; i < funCtorArgs_.length(); i++, arg = arg->pn_next) {
                if (funCtorArgs_[i].name != arg->name())
                    return false;
            }
        }
        return true;
    }
};

} // unnamed namespace

static JS::AsmJSCacheResult
StoreAsmJSModuleInCache(AsmJSParser& parser, const AsmJSModule& module, ExclusiveContext* cx)
{
    MachineId machineId;
    if (!machineId.extractCurrentState(cx))
        return JS::AsmJSCache_InternalError;

    ModuleCharsForStore moduleChars;
    if (!moduleChars.init(parser))
        return JS::AsmJSCache_InternalError;

    size_t serializedSize = machineId.serializedSize() +
                            moduleChars.serializedSize() +
                            module.serializedSize();

    JS::OpenAsmJSCacheEntryForWriteOp open = cx->asmJSCacheOps().openEntryForWrite;
    if (!open)
        return JS::AsmJSCache_Disabled_Internal;

    const char16_t* begin = parser.tokenStream.rawCharPtrAt(ModuleChars::beginOffset(parser));
    const char16_t* end = parser.tokenStream.rawCharPtrAt(ModuleChars::endOffset(parser));
    bool installed = parser.options().installedFile;

    ScopedCacheEntryOpenedForWrite entry(cx, serializedSize);
    JS::AsmJSCacheResult openResult =
        open(cx->global(), installed, begin, end, serializedSize, &entry.memory, &entry.handle);
    if (openResult != JS::AsmJSCache_Success)
        return openResult;

    uint8_t* cursor = entry.memory;
    cursor = machineId.serialize(cursor);
    cursor = moduleChars.serialize(cursor);
    cursor = module.serialize(cursor);

    MOZ_ASSERT(cursor == entry.memory + serializedSize);
    return JS::AsmJSCache_Success;
}

static bool
LookupAsmJSModuleInCache(ExclusiveContext* cx, AsmJSParser& parser, HandleAsmJSModule moduleObj,
                         bool* loadedFromCache, UniqueChars* compilationTimeReport)
{
    int64_t usecBefore = PRMJ_Now();

    *loadedFromCache = false;

    MachineId machineId;
    if (!machineId.extractCurrentState(cx))
        return true;

    JS::OpenAsmJSCacheEntryForReadOp open = cx->asmJSCacheOps().openEntryForRead;
    if (!open)
        return true;

    const char16_t* begin = parser.tokenStream.rawCharPtrAt(ModuleChars::beginOffset(parser));
    const char16_t* limit = parser.tokenStream.rawLimit();

    ScopedCacheEntryOpenedForRead entry(cx);
    if (!open(cx->global(), begin, limit, &entry.serializedSize, &entry.memory, &entry.handle))
        return true;

    const uint8_t* cursor = entry.memory;

    MachineId cachedMachineId;
    cursor = cachedMachineId.deserialize(cx, cursor);
    if (!cursor)
        return false;
    if (machineId != cachedMachineId)
        return true;

    ModuleCharsForLookup moduleChars;
    cursor = moduleChars.deserialize(cx, cursor);
    if (!moduleChars.match(parser))
        return true;

    uint32_t srcStart = parser.pc->maybeFunction->pn_body->pn_pos.begin;
    uint32_t srcBodyStart = parser.tokenStream.currentToken().pos.end;
    bool strict = parser.pc->sc->strict() && !parser.pc->sc->hasExplicitUseStrict();

    AsmJSModule* module = cx->new_<AsmJSModule>(parser.ss, srcStart, srcBodyStart, strict);
    if (!module)
        return false;

    moduleObj->setModule(module);

    cursor = module->deserialize(cx, cursor);
    if (!cursor)
        return false;

    bool atEnd = cursor == entry.memory + entry.serializedSize;
    MOZ_ASSERT(atEnd, "Corrupt cache file");
    if (!atEnd)
        return true;

    if (module->wasmModule().compileArgs() != CompileArgs(cx))
        return true;

    module->staticallyLink(cx);

    if (!parser.tokenStream.advance(module->srcEndBeforeCurly()))
        return false;

    *loadedFromCache = true;

    int64_t usecAfter = PRMJ_Now();
    int ms = (usecAfter - usecBefore) / PRMJ_USEC_PER_MSEC;
    *compilationTimeReport = UniqueChars(JS_smprintf("loaded from cache in %dms", ms));
    return true;
}

/*****************************************************************************/
// Top-level js::CompileAsmJS

static bool
NoExceptionPending(ExclusiveContext* cx)
{
    return !cx->isJSContext() || !cx->asJSContext()->isExceptionPending();
}

static bool
Warn(AsmJSParser& parser, int errorNumber, const char* str)
{
    ParseReportKind reportKind = parser.options().throwOnAsmJSValidationFailureOption &&
                                 errorNumber == JSMSG_USE_ASM_TYPE_FAIL
                                 ? ParseError
                                 : ParseWarning;
    parser.reportNoOffset(reportKind, /* strict = */ false, errorNumber, str ? str : "");
    return false;
}

static bool
EstablishPreconditions(ExclusiveContext* cx, AsmJSParser& parser)
{
#if defined(JS_CODEGEN_NONE) || defined(JS_CODEGEN_ARM64)
    return Warn(parser, JSMSG_USE_ASM_TYPE_FAIL, "Disabled by lack of a JIT compiler");
#endif

    if (!cx->jitSupportsFloatingPoint())
        return Warn(parser, JSMSG_USE_ASM_TYPE_FAIL, "Disabled by lack of floating point support");

    if (cx->gcSystemPageSize() != AsmJSPageSize)
        return Warn(parser, JSMSG_USE_ASM_TYPE_FAIL, "Disabled by non 4KiB system page size");

    switch (parser.options().asmJSOption) {
      case AsmJSOption::Disabled:
        return Warn(parser, JSMSG_USE_ASM_TYPE_FAIL, "Disabled by javascript.options.asmjs in about:config");
      case AsmJSOption::DisabledByDebugger:
        return Warn(parser, JSMSG_USE_ASM_TYPE_FAIL, "Disabled by debugger");
      case AsmJSOption::Enabled:
        break;
    }

    if (parser.pc->isGenerator())
        return Warn(parser, JSMSG_USE_ASM_TYPE_FAIL, "Disabled by generator context");

    if (parser.pc->isArrowFunction())
        return Warn(parser, JSMSG_USE_ASM_TYPE_FAIL, "Disabled by arrow function context");

    // Class constructors are also methods
    if (parser.pc->isMethod())
        return Warn(parser, JSMSG_USE_ASM_TYPE_FAIL, "Disabled by class constructor or method context");

    return true;
}

static UniqueChars
BuildConsoleMessage(ExclusiveContext* cx, AsmJSModule& module, unsigned time,
                    const SlowFunctionVector& slowFuncs, JS::AsmJSCacheResult cacheResult)
{
#ifndef JS_MORE_DETERMINISTIC
    UniqueChars slowText;
    if (!slowFuncs.empty()) {
        slowText.reset(JS_smprintf("; %d functions compiled slowly: ", slowFuncs.length()));
        if (!slowText)
            return nullptr;

        for (unsigned i = 0; i < slowFuncs.length(); i++) {
            const SlowFunction& func = slowFuncs[i];
            JSAutoByteString name;
            if (!AtomToPrintableString(cx, func.name, &name))
                return nullptr;

            slowText.reset(JS_smprintf("%s%s:%u:%u (%ums)%s", slowText.get(),
                                       name.ptr(), func.line, func.column, func.ms,
                                       i+1 < slowFuncs.length() ? ", " : ""));
            if (!slowText)
                return nullptr;
        }
    }

    const char* cacheString = "";
    switch (cacheResult) {
      case JS::AsmJSCache_Success:
        cacheString = "stored in cache";
        break;
      case JS::AsmJSCache_ModuleTooSmall:
        cacheString = "not stored in cache (too small to benefit)";
        break;
      case JS::AsmJSCache_SynchronousScript:
        cacheString = "unable to cache asm.js in synchronous scripts; try loading "
                      "asm.js via <script async> or createElement('script')";
        break;
      case JS::AsmJSCache_QuotaExceeded:
        cacheString = "not enough temporary storage quota to store in cache";
        break;
      case JS::AsmJSCache_StorageInitFailure:
        cacheString = "storage initialization failed (consider filing a bug)";
        break;
      case JS::AsmJSCache_Disabled_Internal:
        cacheString = "caching disabled by internal configuration (consider filing a bug)";
        break;
      case JS::AsmJSCache_Disabled_ShellFlags:
        cacheString = "caching disabled by missing command-line arguments";
        break;
      case JS::AsmJSCache_Disabled_JitInspector:
        cacheString = "caching disabled by active JIT inspector";
        break;
      case JS::AsmJSCache_InternalError:
        cacheString = "unable to store in cache due to internal error (consider filing a bug)";
        break;
      case JS::AsmJSCache_LIMIT:
        MOZ_CRASH("bad AsmJSCacheResult");
        break;
    }

    return UniqueChars(JS_smprintf("total compilation time %dms; %s%s",
                                   time, cacheString, slowText ? slowText.get() : ""));
#else
    return make_string_copy("");
#endif
}

bool
js::CompileAsmJS(ExclusiveContext* cx, AsmJSParser& parser, ParseNode* stmtList, bool* validated)
{
    *validated = false;

    // Various conditions disable asm.js optimizations.
    if (!EstablishPreconditions(cx, parser))
        return NoExceptionPending(cx);

    Rooted<AsmJSModuleObject*> moduleObj(cx, NewAsmJSModuleObject(cx));
    if (!moduleObj)
        return false;

    // Before spending any time parsing the module, try to look it up in the
    // embedding's cache using the chars about to be parsed as the key.
    bool loadedFromCache;
    UniqueChars message;
    if (!LookupAsmJSModuleInCache(cx, parser, moduleObj, &loadedFromCache, &message))
        return false;

    // If not present in the cache, parse, validate and generate code in a
    // single linear pass over the chars of the asm.js module.
    if (!loadedFromCache) {
        // "Checking" parses, validates and compiles, producing a fully compiled
        // AsmJSModuleObject as result.
        unsigned time;
        SlowFunctionVector slowFuncs(cx);
        if (!CheckModule(cx, parser, stmtList, moduleObj, &time, &slowFuncs))
            return NoExceptionPending(cx);

        // Try to store the AsmJSModule in the embedding's cache. The
        // AsmJSModule must be stored before static linking since static linking
        // specializes the AsmJSModule to the current process's address space
        // and therefore must be executed after a cache hit.
        AsmJSModule& module = moduleObj->module();
        JS::AsmJSCacheResult cacheResult = StoreAsmJSModuleInCache(parser, module, cx);
        if (!module.staticallyLink(cx))
            return false;

        message = BuildConsoleMessage(cx, module, time, slowFuncs, cacheResult);
        if (!message)
            return NoExceptionPending(cx);
    }

    // The module function dynamically links the AsmJSModule when called and
    // generates a set of functions wrapping all the exports.
    FunctionBox* funbox = parser.pc->maybeFunction->pn_funbox;
    RootedFunction moduleFun(cx, NewModuleFunction(cx, funbox->function(), moduleObj));
    if (!moduleFun)
        return false;

    // Finished! Clobber the default function created by the parser with the new
    // asm.js module function. Special cases in the bytecode emitter avoid
    // generating bytecode for asm.js functions, allowing this asm.js module
    // function to be the finished result.
    MOZ_ASSERT(funbox->function()->isInterpreted());
    funbox->object = moduleFun;

    // Success! Write to the console with a "warning" message.
    *validated = true;
    Warn(parser, JSMSG_USE_ASM_TYPE_OK, message.get());
    return NoExceptionPending(cx);
}

/*****************************************************************************/
// asm.js module/export queries

bool
js::IsAsmJSModuleNative(Native native)
{
    return native == LinkAsmJS;
}

bool
js::IsAsmJSModule(JSFunction* fun)
{
    return fun->isNative() && fun->maybeNative() == LinkAsmJS;
}

bool
js::IsAsmJSFunction(JSFunction* fun)
{
    return fun->isNative() && fun->maybeNative() == CallAsmJS;
}

/*****************************************************************************/
// asm.js testing natives:

bool
js::IsAsmJSCompilationAvailable(JSContext* cx, unsigned argc, Value* vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);

    // See EstablishPreconditions.
#if defined(JS_CODEGEN_NONE) || defined(JS_CODEGEN_ARM64)
    bool available = false;
#else
    bool available = cx->jitSupportsFloatingPoint() &&
                     cx->gcSystemPageSize() == AsmJSPageSize &&
                     cx->runtime()->options().asmJS();
#endif

    args.rval().set(BooleanValue(available));
    return true;
}

static bool
IsMaybeWrappedNativeFunction(const Value& v, Native native, JSFunction** fun = nullptr)
{
    if (!v.isObject())
        return false;

    JSObject* obj = CheckedUnwrap(&v.toObject());
    if (!obj)
        return false;

    if (!obj->is<JSFunction>())
        return false;

    if (fun)
        *fun = &obj->as<JSFunction>();

    return obj->as<JSFunction>().maybeNative() == native;
}

bool
js::IsAsmJSModule(JSContext* cx, unsigned argc, Value* vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);
    bool rval = args.hasDefined(0) && IsMaybeWrappedNativeFunction(args.get(0), LinkAsmJS);
    args.rval().set(BooleanValue(rval));
    return true;
}

bool
js::IsAsmJSFunction(JSContext* cx, unsigned argc, Value* vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);
    bool rval = args.hasDefined(0) && IsMaybeWrappedNativeFunction(args[0], CallAsmJS);
    args.rval().set(BooleanValue(rval));
    return true;
}

bool
js::IsAsmJSModuleLoadedFromCache(JSContext* cx, unsigned argc, Value* vp)
{
    CallArgs args = CallArgsFromVp(argc, vp);

    JSFunction* fun;
    if (!args.hasDefined(0) || !IsMaybeWrappedNativeFunction(args[0], LinkAsmJS, &fun)) {
        JS_ReportErrorNumber(cx, GetErrorMessage, nullptr, JSMSG_USE_ASM_TYPE_FAIL,
                             "argument passed to isAsmJSModuleLoadedFromCache is not a "
                             "validated asm.js module");
        return false;
    }

    bool loadedFromCache = FunctionToModuleObject(fun).module().wasmModule().loadedFromCache();

    args.rval().set(BooleanValue(loadedFromCache));
    return true;
}

/*****************************************************************************/
// asm.js toString/toSource support

static bool
AppendUseStrictSource(JSContext* cx, HandleFunction fun, Handle<JSFlatString*> src, StringBuffer& out)
{
    // We need to add "use strict" in the body right after the opening
    // brace.
    size_t bodyStart = 0, bodyEnd;

    // No need to test for functions created with the Function ctor as
    // these don't implicitly inherit the "use strict" context. Strict mode is
    // enabled for functions created with the Function ctor only if they begin with
    // the "use strict" directive, but these functions won't validate as asm.js
    // modules.

    if (!FindBody(cx, fun, src, &bodyStart, &bodyEnd))
        return false;

    return out.appendSubstring(src, 0, bodyStart) &&
           out.append("\n\"use strict\";\n") &&
           out.appendSubstring(src, bodyStart, src->length() - bodyStart);
}

JSString*
js::AsmJSModuleToString(JSContext* cx, HandleFunction fun, bool addParenToLambda)
{
    AsmJSModule& module = FunctionToModuleObject(fun).module();

    uint32_t begin = module.srcStart();
    uint32_t end = module.srcEndAfterCurly();
    ScriptSource* source = module.scriptSource();
    StringBuffer out(cx);

    if (addParenToLambda && fun->isLambda() && !out.append("("))
        return nullptr;

    if (!out.append("function "))
        return nullptr;

    if (fun->atom() && !out.append(fun->atom()))
        return nullptr;

    bool haveSource = source->hasSourceData();
    if (!haveSource && !JSScript::loadSource(cx, source, &haveSource))
        return nullptr;

    if (!haveSource) {
        if (!out.append("() {\n    [sourceless code]\n}"))
            return nullptr;
    } else {
        // Whether the function has been created with a Function ctor
        bool funCtor = begin == 0 && end == source->length() && source->argumentsNotIncluded();
        if (funCtor) {
            // Functions created with the function constructor don't have arguments in their source.
            if (!out.append("("))
                return nullptr;

            if (PropertyName* argName = module.globalArgumentName()) {
                if (!out.append(argName))
                    return nullptr;
            }
            if (PropertyName* argName = module.importArgumentName()) {
                if (!out.append(", ") || !out.append(argName))
                    return nullptr;
            }
            if (PropertyName* argName = module.bufferArgumentName()) {
                if (!out.append(", ") || !out.append(argName))
                    return nullptr;
            }

            if (!out.append(") {\n"))
                return nullptr;
        }

        Rooted<JSFlatString*> src(cx, source->substring(cx, begin, end));
        if (!src)
            return nullptr;

        if (module.strict()) {
            if (!AppendUseStrictSource(cx, fun, src, out))
                return nullptr;
        } else {
            if (!out.append(src))
                return nullptr;
        }

        if (funCtor && !out.append("\n}"))
            return nullptr;
    }

    if (addParenToLambda && fun->isLambda() && !out.append(")"))
        return nullptr;

    return out.finishString();
}

JSString*
js::AsmJSFunctionToString(JSContext* cx, HandleFunction fun)
{
    AsmJSModule& module = FunctionToModuleObject(fun).module();
    const AsmJSModule::Export& f = module.exports()[FunctionToExportIndex(fun)];
    uint32_t begin = module.srcStart() + f.startOffsetInModule();
    uint32_t end = module.srcStart() + f.endOffsetInModule();

    ScriptSource* source = module.scriptSource();
    StringBuffer out(cx);

    if (!out.append("function "))
        return nullptr;

    bool haveSource = source->hasSourceData();
    if (!haveSource && !JSScript::loadSource(cx, source, &haveSource))
        return nullptr;

    if (!haveSource) {
        // asm.js functions can't be anonymous
        MOZ_ASSERT(fun->atom());
        if (!out.append(fun->atom()))
            return nullptr;
        if (!out.append("() {\n    [sourceless code]\n}"))
            return nullptr;
    } else {
        // asm.js functions cannot have been created with a Function constructor
        // as they belong within a module.
        MOZ_ASSERT(!(begin == 0 && end == source->length() && source->argumentsNotIncluded()));

        if (module.strict()) {
            // AppendUseStrictSource expects its input to start right after the
            // function name, so split the source chars from the src into two parts:
            // the function name and the rest (arguments + body).

            // asm.js functions can't be anonymous
            MOZ_ASSERT(fun->atom());
            if (!out.append(fun->atom()))
                return nullptr;

            size_t nameEnd = begin + fun->atom()->length();
            Rooted<JSFlatString*> src(cx, source->substring(cx, nameEnd, end));
            if (!src || !AppendUseStrictSource(cx, fun, src, out))
                return nullptr;
        } else {
            Rooted<JSFlatString*> src(cx, source->substring(cx, begin, end));
            if (!src)
                return nullptr;
            if (!out.append(src))
                return nullptr;
        }
    }

    return out.finishString();
}

/*****************************************************************************/
// asm.js heap

static const size_t MinHeapLength = 64 * 1024;
static_assert(MinHeapLength % AsmJSPageSize == 0, "Invalid page size");

#if defined(ASMJS_MAY_USE_SIGNAL_HANDLERS_FOR_OOB)

// Targets define AsmJSImmediateRange to be the size of an address immediate,
// and AsmJSCheckedImmediateRange, to be the size of an address immediate that
// can be supported by signal-handler OOB handling.
static_assert(jit::AsmJSCheckedImmediateRange <= jit::AsmJSImmediateRange,
              "AsmJSImmediateRange should be the size of an unconstrained "
              "address immediate");

// To support the use of signal handlers for catching Out Of Bounds accesses,
// the internal ArrayBuffer data array is inflated to 4GiB (only the
// byteLength portion of which is accessible) so that out-of-bounds accesses
// (made using a uint32 index) are guaranteed to raise a SIGSEGV.
// Then, an additional extent is added to permit folding of immediate
// values into addresses. And finally, unaligned accesses and mask optimizations
// might also try to access a few bytes after this limit, so just inflate it by
// AsmJSPageSize.
const size_t js::AsmJSMappedSize = 4 * 1024ULL * 1024ULL * 1024ULL +
                                   jit::AsmJSImmediateRange +
                                   AsmJSPageSize;

#endif // ASMJS_MAY_USE_SIGNAL_HANDLERS_FOR_OOB

// From the asm.js spec Linking section:
//  the heap object's byteLength must be either
//    2^n for n in [12, 24)
//  or
//    2^24 * n for n >= 1.

bool
js::IsValidAsmJSHeapLength(uint32_t length)
{
    bool valid = length >= MinHeapLength &&
                 (IsPowerOfTwo(length) ||
                  (length & 0x00ffffff) == 0);

    MOZ_ASSERT_IF(valid, length % AsmJSPageSize == 0);
    MOZ_ASSERT_IF(valid, length == RoundUpToNextValidAsmJSHeapLength(length));

    return valid;
}

uint32_t
js::RoundUpToNextValidAsmJSHeapLength(uint32_t length)
{
    if (length <= MinHeapLength)
        return MinHeapLength;

    if (length <= 16 * 1024 * 1024)
        return mozilla::RoundUpPow2(length);

    MOZ_ASSERT(length <= 0xff000000);
    return (length + 0x00ffffff) & ~0x00ffffff;
}

bool
js::OnDetachAsmJSArrayBuffer(JSContext* cx, Handle<ArrayBufferObject*> buffer)
{
    for (Module* m = cx->runtime()->linkedWasmModules; m; m = m->nextLinked()) {
        if (buffer == m->maybeBuffer() && !m->detachHeap(cx))
            return false;
    }
    return true;
}

