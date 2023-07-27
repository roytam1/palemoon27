/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "GrGLVertexBuffer.h"
#include "GrGLGpu.h"
#include "SkTraceMemoryDump.h"

GrGLVertexBuffer::GrGLVertexBuffer(GrGLGpu* gpu, const Desc& desc)
    : INHERITED(gpu, desc.fSizeInBytes, GrGLBufferImpl::kDynamicDraw_Usage == desc.fUsage,
                0 == desc.fID)
    , fImpl(gpu, desc, GR_GL_ARRAY_BUFFER) {
    this->registerWithCache();
}

void GrGLVertexBuffer::onRelease() {
    if (!this->wasDestroyed()) {
        fImpl.release(this->getGpuGL());
    }

    INHERITED::onRelease();
}

void GrGLVertexBuffer::onAbandon() {
    fImpl.abandon();
    INHERITED::onAbandon();
}

void* GrGLVertexBuffer::onMap() {
    if (!this->wasDestroyed()) {
        return fImpl.map(this->getGpuGL());
    } else {
        return nullptr;
    }
}

void GrGLVertexBuffer::onUnmap() {
    if (!this->wasDestroyed()) {
        fImpl.unmap(this->getGpuGL());
    }
}

bool GrGLVertexBuffer::onUpdateData(const void* src, size_t srcSizeInBytes) {
    if (!this->wasDestroyed()) {
        return fImpl.updateData(this->getGpuGL(), src, srcSizeInBytes);
    } else {
        return false;
    }
}

void GrGLVertexBuffer::setMemoryBacking(SkTraceMemoryDump* traceMemoryDump,
                                        const SkString& dumpName) const {
    SkString buffer_id;
    buffer_id.appendU32(this->bufferID());
    traceMemoryDump->setMemoryBacking(dumpName.c_str(), "gl_buffer",
                                      buffer_id.c_str());
}
