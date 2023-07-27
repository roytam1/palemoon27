/*
 * Copyright 2013 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkTileImageFilter.h"
#include "SkBitmap.h"
#include "SkCanvas.h"
#include "SkDevice.h"
#include "SkOffsetImageFilter.h"
#include "SkReadBuffer.h"
#include "SkWriteBuffer.h"
#include "SkMatrix.h"
#include "SkPaint.h"
#include "SkShader.h"
#include "SkValidationUtils.h"

SkImageFilter* SkTileImageFilter::Create(const SkRect& srcRect, const SkRect& dstRect,
                                         SkImageFilter* input) {
    if (!SkIsValidRect(srcRect) || !SkIsValidRect(dstRect)) {
        return nullptr;
    }
    if (srcRect.width() == dstRect.width() && srcRect.height() == dstRect.height()) {
        SkRect ir = dstRect;
        if (!ir.intersect(srcRect)) {
            return SkSafeRef(input);
        }
        CropRect cropRect(ir);
        return SkOffsetImageFilter::Create(dstRect.x() - srcRect.x(),
                                           dstRect.y() - srcRect.y(),
                                           input, &cropRect);
    }
    return new SkTileImageFilter(srcRect, dstRect, input);
}

bool SkTileImageFilter::onFilterImage(Proxy* proxy, const SkBitmap& src,
                                      const Context& ctx,
                                      SkBitmap* dst, SkIPoint* offset) const {
    SkBitmap source = src;
    SkIPoint srcOffset = SkIPoint::Make(0, 0);
    if (!this->filterInput(0, proxy, src, ctx, &source, &srcOffset)) {
        return false;
    }

    SkRect dstRect;
    ctx.ctm().mapRect(&dstRect, fDstRect);
    if (!dstRect.intersect(SkRect::Make(ctx.clipBounds()))) {
        offset->fX = offset->fY = 0;
        return true;
    }
    const SkIRect dstIRect = dstRect.roundOut();
    int w = dstIRect.width();
    int h = dstIRect.height();
    if (!fSrcRect.width() || !fSrcRect.height() || !w || !h) {
        return false;
    }

    SkRect srcRect;
    ctx.ctm().mapRect(&srcRect, fSrcRect);
    SkIRect srcIRect;
    srcRect.roundOut(&srcIRect);
    srcIRect.offset(-srcOffset);
    SkBitmap subset;
    SkIRect srcBounds;
    source.getBounds(&srcBounds);

    if (!SkIRect::Intersects(srcIRect, srcBounds)) {
        offset->fX = offset->fY = 0;
        return true;
    }
    if (srcBounds.contains(srcIRect)) {
        if (!source.extractSubset(&subset, srcIRect)) {
            return false;
        }
    } else {
        SkAutoTUnref<SkBaseDevice> device(proxy->createDevice(srcIRect.width(),
                                                              srcIRect.height(),
                                                              kPossible_TileUsage));
        if (!device) {
            return false;
        }
        SkCanvas canvas(device);
        canvas.drawBitmap(src, SkIntToScalar(srcOffset.x()),
                               SkIntToScalar(srcOffset.y()));
        subset = device->accessBitmap(false);
    }
    SkASSERT(subset.width() == srcIRect.width());
    SkASSERT(subset.height() == srcIRect.height());

    SkAutoTUnref<SkBaseDevice> device(proxy->createDevice(w, h));
    if (nullptr == device.get()) {
        return false;
    }
    SkCanvas canvas(device);
    SkPaint paint;
    paint.setXfermodeMode(SkXfermode::kSrc_Mode);

    SkAutoTUnref<SkShader> shader(SkShader::CreateBitmapShader(subset,
                                  SkShader::kRepeat_TileMode, SkShader::kRepeat_TileMode));
    paint.setShader(shader);
    canvas.translate(-dstRect.fLeft, -dstRect.fTop);
    canvas.drawRect(dstRect, paint);
    *dst = device->accessBitmap(false);
    offset->fX = dstIRect.fLeft;
    offset->fY = dstIRect.fTop;
    return true;
}

void SkTileImageFilter::onFilterNodeBounds(const SkIRect& src, const SkMatrix& ctm,
                                          SkIRect* dst, MapDirection direction) const {
    SkRect rect = kReverse_MapDirection == direction ? fSrcRect : fDstRect;
    ctm.mapRect(&rect);
    rect.roundOut(dst);
#ifdef SK_SUPPORT_SRC_BOUNDS_BLOAT_FOR_IMAGEFILTERS
    dst->join(src);
#endif
}

bool SkTileImageFilter::onFilterBounds(const SkIRect& src, const SkMatrix& ctm,
                                       SkIRect* dst) const {
    this->onFilterNodeBounds(src, ctm, dst, kReverse_MapDirection);
    return true;
}

void SkTileImageFilter::computeFastBounds(const SkRect& src, SkRect* dst) const {
#ifdef SK_SUPPORT_SRC_BOUNDS_BLOAT_FOR_IMAGEFILTERS
    // This is a workaround for skia:3194.
    *dst = src;
    dst->join(fDstRect);
#else
    *dst = fDstRect;
#endif
}

SkFlattenable* SkTileImageFilter::CreateProc(SkReadBuffer& buffer) {
    SK_IMAGEFILTER_UNFLATTEN_COMMON(common, 1);
    SkRect src, dst;
    buffer.readRect(&src);
    buffer.readRect(&dst);
    return Create(src, dst, common.getInput(0));
}

void SkTileImageFilter::flatten(SkWriteBuffer& buffer) const {
    this->INHERITED::flatten(buffer);
    buffer.writeRect(fSrcRect);
    buffer.writeRect(fDstRect);
}

#ifndef SK_IGNORE_TO_STRING
void SkTileImageFilter::toString(SkString* str) const {
    str->appendf("SkTileImageFilter: (");
    str->appendf("src: %.2f %.2f %.2f %.2f",
                 fSrcRect.fLeft, fSrcRect.fTop, fSrcRect.fRight, fSrcRect.fBottom);
    str->appendf(" dst: %.2f %.2f %.2f %.2f",
                 fDstRect.fLeft, fDstRect.fTop, fDstRect.fRight, fDstRect.fBottom);
    if (this->getInput(0)) {
        str->appendf("input: (");
        this->getInput(0)->toString(str);
        str->appendf(")");
    }
    str->append(")");
}
#endif
