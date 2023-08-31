/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set ts=8 sts=4 et sw=4 tw=99:
 *
 * Copyright 2015 Mozilla Foundation
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

#include "asmjs/WasmGenerator.h"

#include "asmjs/WasmStubs.h"

#include "jit/MacroAssembler-inl.h"

using namespace js;
using namespace js::jit;
using namespace js::wasm;

// ****************************************************************************
// ModuleGenerator

static const unsigned GENERATOR_LIFO_DEFAULT_CHUNK_SIZE = 4 * 1024;
static const unsigned COMPILATION_LIFO_DEFAULT_CHUNK_SIZE = 64 * 1024;

ModuleGenerator::ModuleGenerator(ExclusiveContext* cx)
  : cx_(cx),
    jcx_(CompileRuntime::get(cx->compartment()->runtimeFromAnyThread())),
    slowFuncs_(cx),
    numSigs_(0),
    lifo_(GENERATOR_LIFO_DEFAULT_CHUNK_SIZE),
    alloc_(&lifo_),
    masm_(MacroAssembler::AsmJSToken(), alloc_),
    funcIndexToExport_(cx),
    parallel_(false),
    outstanding_(0),
    tasks_(cx),
    freeTasks_(cx),
    activeFunc_(nullptr),
    finishedFuncs_(false)
{
    MOZ_ASSERT(IsCompilingAsmJS());
}

ModuleGenerator::~ModuleGenerator()
{
    if (parallel_) {
        // Wait for any outstanding jobs to fail or complete.
        if (outstanding_) {
            AutoLockHelperThreadState lock;
            while (true) {
                IonCompileTaskVector& worklist = HelperThreadState().wasmWorklist();
                MOZ_ASSERT(outstanding_ >= worklist.length());
                outstanding_ -= worklist.length();
                worklist.clear();

                IonCompileTaskVector& finished = HelperThreadState().wasmFinishedList();
                MOZ_ASSERT(outstanding_ >= finished.length());
                outstanding_ -= finished.length();
                finished.clear();

                uint32_t numFailed = HelperThreadState().harvestFailedWasmJobs();
                MOZ_ASSERT(outstanding_ >= numFailed);
                outstanding_ -= numFailed;

                if (!outstanding_)
                    break;

                HelperThreadState().wait(GlobalHelperThreadState::CONSUMER);
            }
        }

        MOZ_ASSERT(HelperThreadState().wasmCompilationInProgress);
        HelperThreadState().wasmCompilationInProgress = false;
    } else {
        MOZ_ASSERT(!outstanding_);
    }
}

static bool
ParallelCompilationEnabled(ExclusiveContext* cx)
{
    // Since there are a fixed number of helper threads and one is already being
    // consumed by this parsing task, ensure that there another free thread to
    // avoid deadlock. (Note: there is at most one thread used for parsing so we
    // don't have to worry about general dining philosophers.)
    if (HelperThreadState().threadCount <= 1 || !CanUseExtraThreads())
        return false;

    // If 'cx' isn't a JSContext, then we are already off the main thread so
    // off-thread compilation must be enabled.
    return !cx->isJSContext() || cx->asJSContext()->runtime()->canUseOffthreadIonCompilation();
}

bool
ModuleGenerator::init(UniqueModuleGeneratorData shared, UniqueChars filename, ModuleKind kind)
{
    if (!funcIndexToExport_.init())
        return false;

    module_ = MakeUnique<ModuleData>();
    if (!module_)
        return false;

    module_->globalBytes = InitialGlobalDataBytes;
    module_->compileArgs = CompileArgs(cx_);
    module_->kind = kind;
    module_->heapUsage = HeapUsage::None;
    module_->filename = Move(filename);

    link_ = MakeUnique<StaticLinkData>();
    if (!link_)
        return false;

    exportMap_ = MakeUnique<ExportMap>();
    if (!exportMap_)
        return false;

    // For asm.js, the Vectors in ModuleGeneratorData are max-sized reservations
    // and will be initialized in a linear order via init* functions as the
    // module is generated. For wasm, the Vectors are correctly-sized and
    // already initialized.
    shared_ = Move(shared);
    if (kind == ModuleKind::Wasm) {
        numSigs_ = shared_->sigs.length();
        module_->numFuncs = shared_->funcSigs.length();
        module_->globalBytes = AlignBytes(module_->globalBytes, sizeof(void*));
        for (ModuleImportGeneratorData& import : shared_->imports) {
            MOZ_ASSERT(!import.globalDataOffset);
            import.globalDataOffset = module_->globalBytes;
            module_->globalBytes += Module::SizeOfImportExit;
            if (!addImport(*import.sig, import.globalDataOffset))
                return false;
        }
    }

    return true;
}

bool
ModuleGenerator::finishOutstandingTask()
{
    MOZ_ASSERT(parallel_);

    IonCompileTask* task = nullptr;
    {
        AutoLockHelperThreadState lock;
        while (true) {
            MOZ_ASSERT(outstanding_ > 0);

            if (HelperThreadState().wasmFailed())
                return false;

            if (!HelperThreadState().wasmFinishedList().empty()) {
                outstanding_--;
                task = HelperThreadState().wasmFinishedList().popCopy();
                break;
            }

            HelperThreadState().wait(GlobalHelperThreadState::CONSUMER);
        }
    }

    return finishTask(task);
}

bool
ModuleGenerator::finishTask(IonCompileTask* task)
{
    const FuncBytecode& func = task->func();
    FuncCompileResults& results = task->results();

    // Offset the recorded FuncOffsets by the offset of the function in the
    // whole module's code segment.
    uint32_t offsetInWhole = masm_.size();
    results.offsets().offsetBy(offsetInWhole);

    // Record the non-profiling entry for whole-module linking later.
    // Cannot simply append because funcIndex order is nonlinear.
    if (func.index() >= funcEntryOffsets_.length()) {
        if (!funcEntryOffsets_.resize(func.index() + 1))
            return false;
    }
    MOZ_ASSERT(funcEntryOffsets_[func.index()] == 0);
    funcEntryOffsets_[func.index()] = results.offsets().nonProfilingEntry;

    // Merge the compiled results into the whole-module masm.
    DebugOnly<size_t> sizeBefore = masm_.size();
    if (!masm_.asmMergeWith(results.masm()))
        return false;
    MOZ_ASSERT(masm_.size() == offsetInWhole + results.masm().size());

    // Add the CodeRange for this function.
    if (!module_->codeRanges.emplaceBack(func.index(), func.lineOrBytecode(), results.offsets()))
        return false;

    // Keep a record of slow functions for printing in the final console message.
    unsigned totalTime = func.generateTime() + results.compileTime();
    if (totalTime >= SlowFunction::msThreshold) {
        if (!slowFuncs_.emplaceBack(func.index(), totalTime, func.lineOrBytecode()))
            return false;
    }

    freeTasks_.infallibleAppend(task);
    return true;
}

bool
ModuleGenerator::addImport(const Sig& sig, uint32_t globalDataOffset)
{
    Sig copy;
    if (!copy.clone(sig))
        return false;

    return module_->imports.emplaceBack(Move(copy), globalDataOffset);
}

bool
ModuleGenerator::allocateGlobalBytes(uint32_t bytes, uint32_t align, uint32_t* globalDataOffset)
{
    uint32_t globalBytes = module_->globalBytes;

    uint32_t pad = ComputeByteAlignment(globalBytes, align);
    if (UINT32_MAX - globalBytes < pad + bytes)
        return false;

    globalBytes += pad;
    *globalDataOffset = globalBytes;
    globalBytes += bytes;

    module_->globalBytes = globalBytes;
    return true;
}

bool
ModuleGenerator::allocateGlobalVar(ValType type, bool isConst, uint32_t* index)
{
    MOZ_ASSERT(!startedFuncDefs());
    unsigned width = 0;
    switch (type) {
      case ValType::I32:
      case ValType::F32:
        width = 4;
        break;
      case ValType::I64:
      case ValType::F64:
        width = 8;
        break;
      case ValType::I32x4:
      case ValType::F32x4:
      case ValType::B32x4:
        width = 16;
        break;
      case ValType::Limit:
        MOZ_CRASH("Limit");
        break;
    }

    uint32_t offset;
    if (!allocateGlobalBytes(width, width, &offset))
        return false;

    *index = shared_->globals.length();
    return shared_->globals.append(AsmJSGlobalVariable(ToExprType(type), offset, isConst));
}

void
ModuleGenerator::initHeapUsage(HeapUsage heapUsage)
{
    MOZ_ASSERT(module_->heapUsage == HeapUsage::None);
    module_->heapUsage = heapUsage;
}

bool
ModuleGenerator::usesHeap() const
{
    return UsesHeap(module_->heapUsage);
}

void
ModuleGenerator::initSig(uint32_t sigIndex, Sig&& sig)
{
    MOZ_ASSERT(isAsmJS());
    MOZ_ASSERT(sigIndex == numSigs_);
    numSigs_++;

    MOZ_ASSERT(shared_->sigs[sigIndex] == Sig());
    shared_->sigs[sigIndex] = Move(sig);
}

const DeclaredSig&
ModuleGenerator::sig(uint32_t index) const
{
    MOZ_ASSERT(index < numSigs_);
    return shared_->sigs[index];
}

bool
ModuleGenerator::initFuncSig(uint32_t funcIndex, uint32_t sigIndex)
{
    MOZ_ASSERT(isAsmJS());
    MOZ_ASSERT(funcIndex == module_->numFuncs);
    MOZ_ASSERT(!shared_->funcSigs[funcIndex]);

    module_->numFuncs++;
    shared_->funcSigs[funcIndex] = &shared_->sigs[sigIndex];
    return true;
}

const DeclaredSig&
ModuleGenerator::funcSig(uint32_t funcIndex) const
{
    MOZ_ASSERT(shared_->funcSigs[funcIndex]);
    return *shared_->funcSigs[funcIndex];
}

bool
ModuleGenerator::initImport(uint32_t importIndex, uint32_t sigIndex)
{
    uint32_t globalDataOffset;
    if (!allocateGlobalBytes(Module::SizeOfImportExit, sizeof(void*), &globalDataOffset))
        return false;

    MOZ_ASSERT(isAsmJS());
    MOZ_ASSERT(importIndex == module_->imports.length());
    if (!addImport(sig(sigIndex), globalDataOffset))
        return false;

    ModuleImportGeneratorData& import = shared_->imports[importIndex];
    MOZ_ASSERT(!import.sig);
    import.sig = &shared_->sigs[sigIndex];
    import.globalDataOffset = globalDataOffset;
    return true;
}

uint32_t
ModuleGenerator::numImports() const
{
    return module_->imports.length();
}

const ModuleImportGeneratorData&
ModuleGenerator::import(uint32_t index) const
{
    MOZ_ASSERT(shared_->imports[index].sig);
    return shared_->imports[index];
}

bool
ModuleGenerator::defineImport(uint32_t index, ProfilingOffsets interpExit, ProfilingOffsets jitExit)
{
    Import& import = module_->imports[index];
    import.initInterpExitOffset(interpExit.begin);
    import.initJitExitOffset(jitExit.begin);
    return module_->codeRanges.emplaceBack(CodeRange::ImportInterpExit, interpExit) &&
           module_->codeRanges.emplaceBack(CodeRange::ImportJitExit, jitExit);
}

bool
ModuleGenerator::declareExport(UniqueChars fieldName, uint32_t funcIndex, uint32_t* exportIndex)
{
    if (!exportMap_->fieldNames.append(Move(fieldName)))
        return false;

    FuncIndexMap::AddPtr p = funcIndexToExport_.lookupForAdd(funcIndex);
    if (p) {
        if (exportIndex)
            *exportIndex = p->value();
        return exportMap_->fieldsToExports.append(p->value());
    }

    uint32_t newExportIndex = module_->exports.length();
    MOZ_ASSERT(newExportIndex < MaxExports);

    if (exportIndex)
        *exportIndex = newExportIndex;

    Sig copy;
    if (!copy.clone(funcSig(funcIndex)))
        return false;

    return module_->exports.append(Move(copy)) &&
           funcIndexToExport_.add(p, funcIndex, newExportIndex) &&
           exportMap_->fieldsToExports.append(newExportIndex) &&
           exportMap_->exportFuncIndices.append(funcIndex);
}

uint32_t
ModuleGenerator::exportFuncIndex(uint32_t index) const
{
    return exportMap_->exportFuncIndices[index];
}

uint32_t
ModuleGenerator::exportEntryOffset(uint32_t index) const
{
    return funcEntryOffsets_[exportMap_->exportFuncIndices[index]];
}

const Sig&
ModuleGenerator::exportSig(uint32_t index) const
{
    return module_->exports[index].sig();
}

uint32_t
ModuleGenerator::numExports() const
{
    return module_->exports.length();
}

bool
ModuleGenerator::defineExport(uint32_t index, Offsets offsets)
{
    module_->exports[index].initStubOffset(offsets.begin);
    return module_->codeRanges.emplaceBack(CodeRange::Entry, offsets);
}

bool
ModuleGenerator::addMemoryExport(UniqueChars fieldName)
{
    return exportMap_->fieldNames.append(Move(fieldName)) &&
           exportMap_->fieldsToExports.append(ExportMap::MemoryExport);
}

bool
ModuleGenerator::startFuncDefs()
{
    MOZ_ASSERT(!startedFuncDefs());
    threadView_ = MakeUnique<ModuleGeneratorThreadView>(*shared_);
    if (!threadView_)
        return false;

    if (!funcIndexToExport_.init())
        return false;

    uint32_t numTasks;
    if (ParallelCompilationEnabled(cx_) &&
        HelperThreadState().wasmCompilationInProgress.compareExchange(false, true))
    {
#ifdef DEBUG
        {
            AutoLockHelperThreadState lock;
            MOZ_ASSERT(!HelperThreadState().wasmFailed());
            MOZ_ASSERT(HelperThreadState().wasmWorklist().empty());
            MOZ_ASSERT(HelperThreadState().wasmFinishedList().empty());
        }
#endif

        parallel_ = true;
        numTasks = HelperThreadState().maxWasmCompilationThreads();
    } else {
        numTasks = 1;
    }

    if (!tasks_.initCapacity(numTasks))
        return false;
    JSRuntime* rt = cx_->compartment()->runtimeFromAnyThread();
    for (size_t i = 0; i < numTasks; i++)
        tasks_.infallibleEmplaceBack(rt, args(), *threadView_, COMPILATION_LIFO_DEFAULT_CHUNK_SIZE);

    if (!freeTasks_.reserve(numTasks))
        return false;
    for (size_t i = 0; i < numTasks; i++)
        freeTasks_.infallibleAppend(&tasks_[i]);

    MOZ_ASSERT(startedFuncDefs());
    return true;
}

bool
ModuleGenerator::startFuncDef(uint32_t lineOrBytecode, FunctionGenerator* fg)
{
    MOZ_ASSERT(startedFuncDefs());
    MOZ_ASSERT(!activeFunc_);
    MOZ_ASSERT(!finishedFuncs_);

    if (freeTasks_.empty() && !finishOutstandingTask())
        return false;

    IonCompileTask* task = freeTasks_.popCopy();

    task->reset(&fg->bytecode_);
    if (fg->bytecode_) {
        fg->bytecode_->clear();
    } else {
        fg->bytecode_ = MakeUnique<Bytecode>();
        if (!fg->bytecode_)
            return false;
    }

    fg->lineOrBytecode_ = lineOrBytecode;
    fg->m_ = this;
    fg->task_ = task;
    activeFunc_ = fg;
    return true;
}

bool
ModuleGenerator::finishFuncDef(uint32_t funcIndex, unsigned generateTime, FunctionGenerator* fg)
{
    MOZ_ASSERT(activeFunc_ == fg);

    UniqueFuncBytecode func =
        js::MakeUnique<FuncBytecode>(funcIndex,
                                     funcSig(funcIndex),
                                     Move(fg->bytecode_),
                                     Move(fg->locals_),
                                     fg->lineOrBytecode_,
                                     Move(fg->callSiteLineNums_),
                                     generateTime,
                                     module_->kind);
    if (!func)
        return false;

    fg->task_->init(Move(func));

    if (parallel_) {
        if (!StartOffThreadWasmCompile(cx_, fg->task_))
            return false;
        outstanding_++;
    } else {
        if (!IonCompileFunction(fg->task_))
            return false;
        if (!finishTask(fg->task_))
            return false;
    }

    fg->m_ = nullptr;
    fg->task_ = nullptr;
    activeFunc_ = nullptr;
    return true;
}

bool
ModuleGenerator::finishFuncDefs()
{
    MOZ_ASSERT(startedFuncDefs());
    MOZ_ASSERT(!activeFunc_);
    MOZ_ASSERT(!finishedFuncs_);

    while (outstanding_ > 0) {
        if (!finishOutstandingTask())
            return false;
    }

    // During codegen, all wasm->wasm (internal) calls use AsmJSInternalCallee
    // as the call target, which contains the function-index of the target.
    // These get recorded in a CallSiteAndTargetVector in the MacroAssembler
    // so that we can patch them now that all the function entry offsets are
    // known.

    for (CallSiteAndTarget& cs : masm_.callSites()) {
        if (!cs.isInternal())
            continue;
        MOZ_ASSERT(cs.kind() == CallSiteDesc::Relative);
        uint32_t callerOffset = cs.returnAddressOffset();
        uint32_t calleeOffset = funcEntryOffsets_[cs.targetIndex()];
        masm_.patchCall(callerOffset, calleeOffset);
    }

    module_->functionBytes = masm_.size();
    finishedFuncs_ = true;
    return true;
}

bool
ModuleGenerator::declareFuncPtrTable(uint32_t numElems, uint32_t* index)
{
    // Here just add an uninitialized FuncPtrTable and claim space in the global
    // data section. Later, 'defineFuncPtrTable' will be called with function
    // indices for all the elements of the table.

    // Avoid easy way to OOM the process.
    if (numElems > 1024 * 1024)
        return false;

    uint32_t globalDataOffset;
    if (!allocateGlobalBytes(numElems * sizeof(void*), sizeof(void*), &globalDataOffset))
        return false;

    StaticLinkData::FuncPtrTableVector& tables = link_->funcPtrTables;

    *index = tables.length();
    if (!tables.emplaceBack(globalDataOffset))
        return false;

    if (!tables.back().elemOffsets.resize(numElems))
        return false;

    return true;
}

uint32_t
ModuleGenerator::funcPtrTableGlobalDataOffset(uint32_t index) const
{
    return link_->funcPtrTables[index].globalDataOffset;
}

void
ModuleGenerator::defineFuncPtrTable(uint32_t index, const Vector<uint32_t>& elemFuncIndices)
{
    MOZ_ASSERT(finishedFuncs_);

    StaticLinkData::FuncPtrTable& table = link_->funcPtrTables[index];
    MOZ_ASSERT(table.elemOffsets.length() == elemFuncIndices.length());

    for (size_t i = 0; i < elemFuncIndices.length(); i++)
        table.elemOffsets[i] = funcEntryOffsets_[elemFuncIndices[i]];
}

bool
ModuleGenerator::defineInlineStub(Offsets offsets)
{
    MOZ_ASSERT(finishedFuncs_);
    return module_->codeRanges.emplaceBack(CodeRange::Inline, offsets);
}

void
ModuleGenerator::defineInterruptExit(uint32_t offset)
{
    MOZ_ASSERT(finishedFuncs_);
    link_->pod.interruptOffset = offset;
}

void
ModuleGenerator::defineOutOfBoundsExit(uint32_t offset)
{
    MOZ_ASSERT(finishedFuncs_);
    link_->pod.outOfBoundsOffset = offset;
}

bool
ModuleGenerator::finish(CacheableCharsVector&& prettyFuncNames,
                        UniqueModuleData* module,
                        UniqueStaticLinkData* linkData,
                        UniqueExportMap* exportMap,
                        SlowFunctionVector* slowFuncs)
{
    MOZ_ASSERT(!activeFunc_);
    MOZ_ASSERT(finishedFuncs_);

    module_->prettyFuncNames = Move(prettyFuncNames);

    if (!GenerateStubs(*this))
        return false;

    masm_.finish();
    if (masm_.oom())
        return false;

    // Start global data on a new page so JIT code may be given independent
    // protection flags. Note assumption that global data starts right after
    // code below.
    module_->codeBytes = AlignBytes(masm_.bytesNeeded(), gc::SystemPageSize());

    // Inflate the global bytes up to page size so that the total bytes are a
    // page size (as required by the allocator functions).
    module_->globalBytes = AlignBytes(module_->globalBytes, gc::SystemPageSize());

    // Allocate the code (guarded by a UniquePtr until it is given to the Module).
    module_->code = AllocateCode(cx_, module_->totalBytes());
    if (!module_->code)
        return false;

    // Delay flushing until Module::dynamicallyLink. The flush-inhibited range
    // is set by executableCopy.
    AutoFlushICache afc("ModuleGenerator::finish", /* inhibit = */ true);
    uint8_t* code = module_->code.get();
    masm_.executableCopy(code);

    // c.f. JitCode::copyFrom
    MOZ_ASSERT(masm_.jumpRelocationTableBytes() == 0);
    MOZ_ASSERT(masm_.dataRelocationTableBytes() == 0);
    MOZ_ASSERT(masm_.preBarrierTableBytes() == 0);
    MOZ_ASSERT(!masm_.hasSelfReference());

    // Convert the CallSiteAndTargetVector (needed during generation) to a
    // CallSiteVector (what is stored in the Module).
    if (!module_->callSites.appendAll(masm_.callSites()))
        return false;

    // The MacroAssembler has accumulated all the heap accesses during codegen.
    module_->heapAccesses = masm_.extractHeapAccesses();

    // Add links to absolute addresses identified symbolically.
    StaticLinkData::SymbolicLinkArray& symbolicLinks = link_->symbolicLinks;
    for (size_t i = 0; i < masm_.numAsmJSAbsoluteAddresses(); i++) {
        AsmJSAbsoluteAddress src = masm_.asmJSAbsoluteAddress(i);
        if (!symbolicLinks[src.target].append(src.patchAt.offset()))
            return false;
    }

    // Relative link metadata: absolute addresses that refer to another point within
    // the asm.js module.

    // CodeLabels are used for switch cases and loads from floating-point /
    // SIMD values in the constant pool.
    for (size_t i = 0; i < masm_.numCodeLabels(); i++) {
        CodeLabel cl = masm_.codeLabel(i);
        StaticLinkData::InternalLink link(StaticLinkData::InternalLink::CodeLabel);
        link.patchAtOffset = masm_.labelToPatchOffset(*cl.patchAt());
        link.targetOffset = cl.target()->offset();
        if (!link_->internalLinks.append(link))
            return false;
    }

#if defined(JS_CODEGEN_X86)
    // Global data accesses in x86 need to be patched with the absolute
    // address of the global. Globals are allocated sequentially after the
    // code section so we can just use an InternalLink.
    for (size_t i = 0; i < masm_.numAsmJSGlobalAccesses(); i++) {
        AsmJSGlobalAccess a = masm_.asmJSGlobalAccess(i);
        StaticLinkData::InternalLink link(StaticLinkData::InternalLink::RawPointer);
        link.patchAtOffset = masm_.labelToPatchOffset(a.patchAt);
        link.targetOffset = module_->codeBytes + a.globalDataOffset;
        if (!link_->internalLinks.append(link))
            return false;
    }
#endif

#if defined(JS_CODEGEN_MIPS32) || defined(JS_CODEGEN_MIPS64)
    // On MIPS we need to update all the long jumps because they contain an
    // absolute adress. The values are correctly patched for the current address
    // space, but not after serialization or profiling-mode toggling.
    for (size_t i = 0; i < masm_.numLongJumps(); i++) {
        size_t off = masm_.longJump(i);
        StaticLinkData::InternalLink link(StaticLinkData::InternalLink::InstructionImmediate);
        link.patchAtOffset = off;
        link.targetOffset = Assembler::ExtractInstructionImmediate(code + off) - uintptr_t(code);
        if (!link_->internalLinks.append(link))
            return false;
    }
#endif

#if defined(JS_CODEGEN_X64)
    // Global data accesses on x64 use rip-relative addressing and thus do
    // not need patching after deserialization.
    uint8_t* globalData = code + module_->codeBytes;
    for (size_t i = 0; i < masm_.numAsmJSGlobalAccesses(); i++) {
        AsmJSGlobalAccess a = masm_.asmJSGlobalAccess(i);
        masm_.patchAsmJSGlobalAccess(a.patchAt, code, globalData, a.globalDataOffset);
    }
#endif

    *module = Move(module_);
    *linkData = Move(link_);
    *exportMap = Move(exportMap_);
    *slowFuncs = Move(slowFuncs_);
    return true;
}
