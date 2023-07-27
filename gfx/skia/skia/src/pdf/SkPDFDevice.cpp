/*
 * Copyright 2011 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkPDFDevice.h"

#include "SkAnnotation.h"
#include "SkColor.h"
#include "SkColorFilter.h"
#include "SkClipStack.h"
#include "SkData.h"
#include "SkDraw.h"
#include "SkGlyphCache.h"
#include "SkPaint.h"
#include "SkPath.h"
#include "SkPathOps.h"
#include "SkPDFBitmap.h"
#include "SkPDFCanon.h"
#include "SkPDFFont.h"
#include "SkPDFFormXObject.h"
#include "SkPDFGraphicState.h"
#include "SkPDFResourceDict.h"
#include "SkPDFShader.h"
#include "SkPDFStream.h"
#include "SkPDFTypes.h"
#include "SkPDFUtils.h"
#include "SkRasterClip.h"
#include "SkRect.h"
#include "SkRRect.h"
#include "SkString.h"
#include "SkSurface.h"
#include "SkTextFormatParams.h"
#include "SkTemplates.h"
#include "SkTypefacePriv.h"
#include "SkXfermodeInterpretation.h"

#define DPI_FOR_RASTER_SCALE_ONE 72

// Utility functions

static bool excessive_translation(const SkMatrix& m) {
    const SkScalar kExcessiveTranslation = 8192.0f;
    return SkScalarAbs(m.getTranslateX()) > kExcessiveTranslation
        || SkScalarAbs(m.getTranslateY()) > kExcessiveTranslation;
}

static SkMatrix untranslate(const SkMatrix& matrix, SkScalar x, SkScalar y) {
    // https://bug.skia.org/257 If the translation is too large,
    // PDF can't exactly represent the float values as numbers.
    SkMatrix result(matrix);
    result.preTranslate(x, y);
    return result;
}

// If the paint will definitely draw opaquely, replace kSrc_Mode with
// kSrcOver_Mode.  http://crbug.com/473572
static void replace_srcmode_on_opaque_paint(SkPaint* paint) {
    if (kSrcOver_SkXfermodeInterpretation
        == SkInterpretXfermode(*paint, false)) {
        paint->setXfermode(nullptr);
    }
}

static void emit_pdf_color(SkColor color, SkWStream* result) {
    SkASSERT(SkColorGetA(color) == 0xFF);  // We handle alpha elsewhere.
    SkScalar colorScale = SkScalarInvert(0xFF);
    SkPDFUtils::AppendScalar(SkColorGetR(color) * colorScale, result);
    result->writeText(" ");
    SkPDFUtils::AppendScalar(SkColorGetG(color) * colorScale, result);
    result->writeText(" ");
    SkPDFUtils::AppendScalar(SkColorGetB(color) * colorScale, result);
    result->writeText(" ");
}

static SkPaint calculate_text_paint(const SkPaint& paint) {
    SkPaint result = paint;
    if (result.isFakeBoldText()) {
        SkScalar fakeBoldScale = SkScalarInterpFunc(result.getTextSize(),
                                                    kStdFakeBoldInterpKeys,
                                                    kStdFakeBoldInterpValues,
                                                    kStdFakeBoldInterpLength);
        SkScalar width = SkScalarMul(result.getTextSize(), fakeBoldScale);
        if (result.getStyle() == SkPaint::kFill_Style) {
            result.setStyle(SkPaint::kStrokeAndFill_Style);
        } else {
            width += result.getStrokeWidth();
        }
        result.setStrokeWidth(width);
    }
    return result;
}

// Stolen from measure_text in SkDraw.cpp and then tweaked.
static void align_text(SkDrawCacheProc glyphCacheProc, const SkPaint& paint,
                       const uint16_t* glyphs, size_t len,
                       SkScalar* x, SkScalar* y) {
    if (paint.getTextAlign() == SkPaint::kLeft_Align) {
        return;
    }

    SkMatrix ident;
    ident.reset();
    SkAutoGlyphCache autoCache(paint, nullptr, &ident);
    SkGlyphCache* cache = autoCache.getCache();

    const char* start = reinterpret_cast<const char*>(glyphs);
    const char* stop = reinterpret_cast<const char*>(glyphs + len);
    SkFixed xAdv = 0, yAdv = 0;

    // TODO(vandebo): This probably needs to take kerning into account.
    while (start < stop) {
        const SkGlyph& glyph = glyphCacheProc(cache, &start, 0, 0);
        xAdv += glyph.fAdvanceX;
        yAdv += glyph.fAdvanceY;
    };
    if (paint.getTextAlign() == SkPaint::kLeft_Align) {
        return;
    }

    SkScalar xAdj = SkFixedToScalar(xAdv);
    SkScalar yAdj = SkFixedToScalar(yAdv);
    if (paint.getTextAlign() == SkPaint::kCenter_Align) {
        xAdj = SkScalarHalf(xAdj);
        yAdj = SkScalarHalf(yAdj);
    }
    *x = *x - xAdj;
    *y = *y - yAdj;
}

static int max_glyphid_for_typeface(SkTypeface* typeface) {
    SkAutoResolveDefaultTypeface autoResolve(typeface);
    typeface = autoResolve.get();
    return typeface->countGlyphs() - 1;
}

typedef SkAutoSTMalloc<128, uint16_t> SkGlyphStorage;

static int force_glyph_encoding(const SkPaint& paint, const void* text,
                                size_t len, SkGlyphStorage* storage,
                                const uint16_t** glyphIDs) {
    // Make sure we have a glyph id encoding.
    if (paint.getTextEncoding() != SkPaint::kGlyphID_TextEncoding) {
        int numGlyphs = paint.textToGlyphs(text, len, nullptr);
        storage->reset(numGlyphs);
        paint.textToGlyphs(text, len, storage->get());
        *glyphIDs = storage->get();
        return numGlyphs;
    }

    // For user supplied glyph ids we need to validate them.
    SkASSERT((len & 1) == 0);
    int numGlyphs = SkToInt(len / 2);
    const uint16_t* input = static_cast<const uint16_t*>(text);

    int maxGlyphID = max_glyphid_for_typeface(paint.getTypeface());
    int validated;
    for (validated = 0; validated < numGlyphs; ++validated) {
        if (input[validated] > maxGlyphID) {
            break;
        }
    }
    if (validated >= numGlyphs) {
        *glyphIDs = static_cast<const uint16_t*>(text);
        return numGlyphs;
    }

    // Silently drop anything out of range.
    storage->reset(numGlyphs);
    if (validated > 0) {
        memcpy(storage->get(), input, validated * sizeof(uint16_t));
    }

    for (int i = validated; i < numGlyphs; ++i) {
        storage->get()[i] = input[i];
        if (input[i] > maxGlyphID) {
            storage->get()[i] = 0;
        }
    }
    *glyphIDs = storage->get();
    return numGlyphs;
}

static void set_text_transform(SkScalar x, SkScalar y, SkScalar textSkewX,
                               SkWStream* content) {
    // Flip the text about the x-axis to account for origin swap and include
    // the passed parameters.
    content->writeText("1 0 ");
    SkPDFUtils::AppendScalar(0 - textSkewX, content);
    content->writeText(" -1 ");
    SkPDFUtils::AppendScalar(x, content);
    content->writeText(" ");
    SkPDFUtils::AppendScalar(y, content);
    content->writeText(" Tm\n");
}

// It is important to not confuse GraphicStateEntry with SkPDFGraphicState, the
// later being our representation of an object in the PDF file.
struct GraphicStateEntry {
    GraphicStateEntry();

    // Compare the fields we care about when setting up a new content entry.
    bool compareInitialState(const GraphicStateEntry& b);

    SkMatrix fMatrix;
    // We can't do set operations on Paths, though PDF natively supports
    // intersect.  If the clip stack does anything other than intersect,
    // we have to fall back to the region.  Treat fClipStack as authoritative.
    // See http://code.google.com/p/skia/issues/detail?id=221
    SkClipStack fClipStack;
    SkRegion fClipRegion;

    // When emitting the content entry, we will ensure the graphic state
    // is set to these values first.
    SkColor fColor;
    SkScalar fTextScaleX;  // Zero means we don't care what the value is.
    SkPaint::Style fTextFill;  // Only if TextScaleX is non-zero.
    int fShaderIndex;
    int fGraphicStateIndex;

    // We may change the font (i.e. for Type1 support) within a
    // ContentEntry.  This is the one currently in effect, or nullptr if none.
    SkPDFFont* fFont;
    // In PDF, text size has no default value. It is only valid if fFont is
    // not nullptr.
    SkScalar fTextSize;
};

GraphicStateEntry::GraphicStateEntry() : fColor(SK_ColorBLACK),
                                         fTextScaleX(SK_Scalar1),
                                         fTextFill(SkPaint::kFill_Style),
                                         fShaderIndex(-1),
                                         fGraphicStateIndex(-1),
                                         fFont(nullptr),
                                         fTextSize(SK_ScalarNaN) {
    fMatrix.reset();
}

bool GraphicStateEntry::compareInitialState(const GraphicStateEntry& cur) {
    return fColor == cur.fColor &&
           fShaderIndex == cur.fShaderIndex &&
           fGraphicStateIndex == cur.fGraphicStateIndex &&
           fMatrix == cur.fMatrix &&
           fClipStack == cur.fClipStack &&
           (fTextScaleX == 0 ||
               (fTextScaleX == cur.fTextScaleX && fTextFill == cur.fTextFill));
}

class GraphicStackState {
public:
    GraphicStackState(const SkClipStack& existingClipStack,
                      const SkRegion& existingClipRegion,
                      SkWStream* contentStream)
            : fStackDepth(0),
              fContentStream(contentStream) {
        fEntries[0].fClipStack = existingClipStack;
        fEntries[0].fClipRegion = existingClipRegion;
    }

    void updateClip(const SkClipStack& clipStack, const SkRegion& clipRegion,
                    const SkPoint& translation);
    void updateMatrix(const SkMatrix& matrix);
    void updateDrawingState(const GraphicStateEntry& state);

    void drainStack();

private:
    void push();
    void pop();
    GraphicStateEntry* currentEntry() { return &fEntries[fStackDepth]; }

    // Conservative limit on save depth, see impl. notes in PDF 1.4 spec.
    static const int kMaxStackDepth = 12;
    GraphicStateEntry fEntries[kMaxStackDepth + 1];
    int fStackDepth;
    SkWStream* fContentStream;
};

void GraphicStackState::drainStack() {
    while (fStackDepth) {
        pop();
    }
}

void GraphicStackState::push() {
    SkASSERT(fStackDepth < kMaxStackDepth);
    fContentStream->writeText("q\n");
    fStackDepth++;
    fEntries[fStackDepth] = fEntries[fStackDepth - 1];
}

void GraphicStackState::pop() {
    SkASSERT(fStackDepth > 0);
    fContentStream->writeText("Q\n");
    fStackDepth--;
}

// This function initializes iter to be an iterator on the "stack" argument
// and then skips over the leading entries as specified in prefix.  It requires
// and asserts that "prefix" will be a prefix to "stack."
static void skip_clip_stack_prefix(const SkClipStack& prefix,
                                   const SkClipStack& stack,
                                   SkClipStack::Iter* iter) {
    SkClipStack::B2TIter prefixIter(prefix);
    iter->reset(stack, SkClipStack::Iter::kBottom_IterStart);

    const SkClipStack::Element* prefixEntry;
    const SkClipStack::Element* iterEntry;

    for (prefixEntry = prefixIter.next(); prefixEntry;
            prefixEntry = prefixIter.next()) {
        iterEntry = iter->next();
        SkASSERT(iterEntry);
        // Because of SkClipStack does internal intersection, the last clip
        // entry may differ.
        if (*prefixEntry != *iterEntry) {
            SkASSERT(prefixEntry->getOp() == SkRegion::kIntersect_Op);
            SkASSERT(iterEntry->getOp() == SkRegion::kIntersect_Op);
            SkASSERT(iterEntry->getType() == prefixEntry->getType());
            // back up the iterator by one
            iter->prev();
            prefixEntry = prefixIter.next();
            break;
        }
    }

    SkASSERT(prefixEntry == nullptr);
}

static void emit_clip(SkPath* clipPath, SkRect* clipRect,
                      SkWStream* contentStream) {
    SkASSERT(clipPath || clipRect);

    SkPath::FillType clipFill;
    if (clipPath) {
        SkPDFUtils::EmitPath(*clipPath, SkPaint::kFill_Style, contentStream);
        clipFill = clipPath->getFillType();
    } else {
        SkPDFUtils::AppendRectangle(*clipRect, contentStream);
        clipFill = SkPath::kWinding_FillType;
    }

    NOT_IMPLEMENTED(clipFill == SkPath::kInverseEvenOdd_FillType, false);
    NOT_IMPLEMENTED(clipFill == SkPath::kInverseWinding_FillType, false);
    if (clipFill == SkPath::kEvenOdd_FillType) {
        contentStream->writeText("W* n\n");
    } else {
        contentStream->writeText("W n\n");
    }
}

/* Calculate an inverted path's equivalent non-inverted path, given the
 * canvas bounds.
 * outPath may alias with invPath (since this is supported by PathOps).
 */
static bool calculate_inverse_path(const SkRect& bounds, const SkPath& invPath,
                                   SkPath* outPath) {
    SkASSERT(invPath.isInverseFillType());

    SkPath clipPath;
    clipPath.addRect(bounds);

    return Op(clipPath, invPath, kIntersect_SkPathOp, outPath);
}

#ifdef SK_PDF_USE_PATHOPS_CLIPPING
// Sanity check the numerical values of the SkRegion ops and PathOps ops
// enums so region_op_to_pathops_op can do a straight passthrough cast.
// If these are failing, it may be necessary to make region_op_to_pathops_op
// do more.
static_assert(SkRegion::kDifference_Op == (int)kDifference_SkPathOp, "region_pathop_mismatch");
static_assert(SkRegion::kIntersect_Op == (int)kIntersect_SkPathOp, "region_pathop_mismatch");
static_assert(SkRegion::kUnion_Op == (int)kUnion_SkPathOp, "region_pathop_mismatch");
static_assert(SkRegion::kXOR_Op == (int)kXOR_SkPathOp, "region_pathop_mismatch");
static_assert(SkRegion::kReverseDifference_Op == (int)kReverseDifference_SkPathOp,
              "region_pathop_mismatch");

static SkPathOp region_op_to_pathops_op(SkRegion::Op op) {
    SkASSERT(op >= 0);
    SkASSERT(op <= SkRegion::kReverseDifference_Op);
    return (SkPathOp)op;
}

/* Uses Path Ops to calculate a vector SkPath clip from a clip stack.
 * Returns true if successful, or false if not successful.
 * If successful, the resulting clip is stored in outClipPath.
 * If not successful, outClipPath is undefined, and a fallback method
 * should be used.
 */
static bool get_clip_stack_path(const SkMatrix& transform,
                                const SkClipStack& clipStack,
                                const SkRegion& clipRegion,
                                SkPath* outClipPath) {
    outClipPath->reset();
    outClipPath->setFillType(SkPath::kInverseWinding_FillType);

    const SkClipStack::Element* clipEntry;
    SkClipStack::Iter iter;
    iter.reset(clipStack, SkClipStack::Iter::kBottom_IterStart);
    for (clipEntry = iter.next(); clipEntry; clipEntry = iter.next()) {
        SkPath entryPath;
        if (SkClipStack::Element::kEmpty_Type == clipEntry->getType()) {
            outClipPath->reset();
            outClipPath->setFillType(SkPath::kInverseWinding_FillType);
            continue;
        } else {
            clipEntry->asPath(&entryPath);
        }
        entryPath.transform(transform);

        if (SkRegion::kReplace_Op == clipEntry->getOp()) {
            *outClipPath = entryPath;
        } else {
            SkPathOp op = region_op_to_pathops_op(clipEntry->getOp());
            if (!Op(*outClipPath, entryPath, op, outClipPath)) {
                return false;
            }
        }
    }

    if (outClipPath->isInverseFillType()) {
        // The bounds are slightly outset to ensure this is correct in the
        // face of floating-point accuracy and possible SkRegion bitmap
        // approximations.
        SkRect clipBounds = SkRect::Make(clipRegion.getBounds());
        clipBounds.outset(SK_Scalar1, SK_Scalar1);
        if (!calculate_inverse_path(clipBounds, *outClipPath, outClipPath)) {
            return false;
        }
    }
    return true;
}
#endif

// TODO(vandebo): Take advantage of SkClipStack::getSaveCount(), the PDF
// graphic state stack, and the fact that we can know all the clips used
// on the page to optimize this.
void GraphicStackState::updateClip(const SkClipStack& clipStack,
                                   const SkRegion& clipRegion,
                                   const SkPoint& translation) {
    if (clipStack == currentEntry()->fClipStack) {
        return;
    }

    while (fStackDepth > 0) {
        pop();
        if (clipStack == currentEntry()->fClipStack) {
            return;
        }
    }
    push();

    currentEntry()->fClipStack = clipStack;
    currentEntry()->fClipRegion = clipRegion;

    SkMatrix transform;
    transform.setTranslate(translation.fX, translation.fY);

#ifdef SK_PDF_USE_PATHOPS_CLIPPING
    SkPath clipPath;
    if (get_clip_stack_path(transform, clipStack, clipRegion, &clipPath)) {
        emit_clip(&clipPath, nullptr, fContentStream);
        return;
    }
#endif
    // gsState->initialEntry()->fClipStack/Region specifies the clip that has
    // already been applied.  (If this is a top level device, then it specifies
    // a clip to the content area.  If this is a layer, then it specifies
    // the clip in effect when the layer was created.)  There's no need to
    // reapply that clip; SKCanvas's SkDrawIter will draw anything outside the
    // initial clip on the parent layer.  (This means there's a bug if the user
    // expands the clip and then uses any xfer mode that uses dst:
    // http://code.google.com/p/skia/issues/detail?id=228 )
    SkClipStack::Iter iter;
    skip_clip_stack_prefix(fEntries[0].fClipStack, clipStack, &iter);

    // If the clip stack does anything other than intersect or if it uses
    // an inverse fill type, we have to fall back to the clip region.
    bool needRegion = false;
    const SkClipStack::Element* clipEntry;
    for (clipEntry = iter.next(); clipEntry; clipEntry = iter.next()) {
        if (clipEntry->getOp() != SkRegion::kIntersect_Op ||
                clipEntry->isInverseFilled()) {
            needRegion = true;
            break;
        }
    }

    if (needRegion) {
        SkPath clipPath;
        SkAssertResult(clipRegion.getBoundaryPath(&clipPath));
        emit_clip(&clipPath, nullptr, fContentStream);
    } else {
        skip_clip_stack_prefix(fEntries[0].fClipStack, clipStack, &iter);
        const SkClipStack::Element* clipEntry;
        for (clipEntry = iter.next(); clipEntry; clipEntry = iter.next()) {
            SkASSERT(clipEntry->getOp() == SkRegion::kIntersect_Op);
            switch (clipEntry->getType()) {
                case SkClipStack::Element::kRect_Type: {
                    SkRect translatedClip;
                    transform.mapRect(&translatedClip, clipEntry->getRect());
                    emit_clip(nullptr, &translatedClip, fContentStream);
                    break;
                }
                default: {
                    SkPath translatedPath;
                    clipEntry->asPath(&translatedPath);
                    translatedPath.transform(transform, &translatedPath);
                    emit_clip(&translatedPath, nullptr, fContentStream);
                    break;
                }
            }
        }
    }
}

void GraphicStackState::updateMatrix(const SkMatrix& matrix) {
    if (matrix == currentEntry()->fMatrix) {
        return;
    }

    if (currentEntry()->fMatrix.getType() != SkMatrix::kIdentity_Mask) {
        SkASSERT(fStackDepth > 0);
        SkASSERT(fEntries[fStackDepth].fClipStack ==
                 fEntries[fStackDepth -1].fClipStack);
        pop();

        SkASSERT(currentEntry()->fMatrix.getType() == SkMatrix::kIdentity_Mask);
    }
    if (matrix.getType() == SkMatrix::kIdentity_Mask) {
        return;
    }

    push();
    SkPDFUtils::AppendTransform(matrix, fContentStream);
    currentEntry()->fMatrix = matrix;
}

void GraphicStackState::updateDrawingState(const GraphicStateEntry& state) {
    // PDF treats a shader as a color, so we only set one or the other.
    if (state.fShaderIndex >= 0) {
        if (state.fShaderIndex != currentEntry()->fShaderIndex) {
            SkPDFUtils::ApplyPattern(state.fShaderIndex, fContentStream);
            currentEntry()->fShaderIndex = state.fShaderIndex;
        }
    } else {
        if (state.fColor != currentEntry()->fColor ||
                currentEntry()->fShaderIndex >= 0) {
            emit_pdf_color(state.fColor, fContentStream);
            fContentStream->writeText("RG ");
            emit_pdf_color(state.fColor, fContentStream);
            fContentStream->writeText("rg\n");
            currentEntry()->fColor = state.fColor;
            currentEntry()->fShaderIndex = -1;
        }
    }

    if (state.fGraphicStateIndex != currentEntry()->fGraphicStateIndex) {
        SkPDFUtils::ApplyGraphicState(state.fGraphicStateIndex, fContentStream);
        currentEntry()->fGraphicStateIndex = state.fGraphicStateIndex;
    }

    if (state.fTextScaleX) {
        if (state.fTextScaleX != currentEntry()->fTextScaleX) {
            SkScalar pdfScale = SkScalarMul(state.fTextScaleX,
                                            SkIntToScalar(100));
            SkPDFUtils::AppendScalar(pdfScale, fContentStream);
            fContentStream->writeText(" Tz\n");
            currentEntry()->fTextScaleX = state.fTextScaleX;
        }
        if (state.fTextFill != currentEntry()->fTextFill) {
            static_assert(SkPaint::kFill_Style == 0, "enum_must_match_value");
            static_assert(SkPaint::kStroke_Style == 1, "enum_must_match_value");
            static_assert(SkPaint::kStrokeAndFill_Style == 2, "enum_must_match_value");
            fContentStream->writeDecAsText(state.fTextFill);
            fContentStream->writeText(" Tr\n");
            currentEntry()->fTextFill = state.fTextFill;
        }
    }
}

static bool not_supported_for_layers(const SkPaint& layerPaint) {
    // PDF does not support image filters, so render them on CPU.
    // Note that this rendering is done at "screen" resolution (100dpi), not
    // printer resolution.
    // TODO: It may be possible to express some filters natively using PDF
    // to improve quality and file size (https://bug.skia.org/3043)

    // TODO: should we return true if there is a colorfilter?
    return layerPaint.getImageFilter() != nullptr;
}

SkBaseDevice* SkPDFDevice::onCreateDevice(const CreateInfo& cinfo, const SkPaint* layerPaint) {
    if (cinfo.fForImageFilter ||
        (layerPaint && not_supported_for_layers(*layerPaint))) {
        return nullptr;
    }
    SkISize size = SkISize::Make(cinfo.fInfo.width(), cinfo.fInfo.height());
    return SkPDFDevice::Create(size, fRasterDpi, fCanon);
}


struct ContentEntry {
    GraphicStateEntry fState;
    SkDynamicMemoryWStream fContent;
    SkAutoTDelete<ContentEntry> fNext;

    // If the stack is too deep we could get Stack Overflow.
    // So we manually destruct the object.
    ~ContentEntry() {
        ContentEntry* val = fNext.detach();
        while (val != nullptr) {
            ContentEntry* valNext = val->fNext.detach();
            // When the destructor is called, fNext is nullptr and exits.
            delete val;
            val = valNext;
        }
    }
};

// A helper class to automatically finish a ContentEntry at the end of a
// drawing method and maintain the state needed between set up and finish.
class ScopedContentEntry {
public:
    ScopedContentEntry(SkPDFDevice* device, const SkDraw& draw,
                       const SkPaint& paint, bool hasText = false)
        : fDevice(device),
          fContentEntry(nullptr),
          fXfermode(SkXfermode::kSrcOver_Mode),
          fDstFormXObject(nullptr) {
        init(draw.fClipStack, *draw.fClip, *draw.fMatrix, paint, hasText);
    }
    ScopedContentEntry(SkPDFDevice* device, const SkClipStack* clipStack,
                       const SkRegion& clipRegion, const SkMatrix& matrix,
                       const SkPaint& paint, bool hasText = false)
        : fDevice(device),
          fContentEntry(nullptr),
          fXfermode(SkXfermode::kSrcOver_Mode),
          fDstFormXObject(nullptr) {
        init(clipStack, clipRegion, matrix, paint, hasText);
    }

    ~ScopedContentEntry() {
        if (fContentEntry) {
            SkPath* shape = &fShape;
            if (shape->isEmpty()) {
                shape = nullptr;
            }
            fDevice->finishContentEntry(fXfermode, fDstFormXObject, shape);
        }
        SkSafeUnref(fDstFormXObject);
    }

    ContentEntry* entry() { return fContentEntry; }

    /* Returns true when we explicitly need the shape of the drawing. */
    bool needShape() {
        switch (fXfermode) {
            case SkXfermode::kClear_Mode:
            case SkXfermode::kSrc_Mode:
            case SkXfermode::kSrcIn_Mode:
            case SkXfermode::kSrcOut_Mode:
            case SkXfermode::kDstIn_Mode:
            case SkXfermode::kDstOut_Mode:
            case SkXfermode::kSrcATop_Mode:
            case SkXfermode::kDstATop_Mode:
            case SkXfermode::kModulate_Mode:
                return true;
            default:
                return false;
        }
    }

    /* Returns true unless we only need the shape of the drawing. */
    bool needSource() {
        if (fXfermode == SkXfermode::kClear_Mode) {
            return false;
        }
        return true;
    }

    /* If the shape is different than the alpha component of the content, then
     * setShape should be called with the shape.  In particular, images and
     * devices have rectangular shape.
     */
    void setShape(const SkPath& shape) {
        fShape = shape;
    }

private:
    SkPDFDevice* fDevice;
    ContentEntry* fContentEntry;
    SkXfermode::Mode fXfermode;
    SkPDFFormXObject* fDstFormXObject;
    SkPath fShape;

    void init(const SkClipStack* clipStack, const SkRegion& clipRegion,
              const SkMatrix& matrix, const SkPaint& paint, bool hasText) {
        // Shape has to be flatten before we get here.
        if (matrix.hasPerspective()) {
            NOT_IMPLEMENTED(!matrix.hasPerspective(), false);
            return;
        }
        if (paint.getXfermode()) {
            paint.getXfermode()->asMode(&fXfermode);
        }
        fContentEntry = fDevice->setUpContentEntry(clipStack, clipRegion,
                                                   matrix, paint, hasText,
                                                   &fDstFormXObject);
    }
};

////////////////////////////////////////////////////////////////////////////////

SkPDFDevice::SkPDFDevice(SkISize pageSize, SkScalar rasterDpi, SkPDFCanon* canon, bool flip)
    : INHERITED(SkSurfaceProps(0, kUnknown_SkPixelGeometry))
    , fPageSize(pageSize)
    , fContentSize(pageSize)
    , fExistingClipRegion(SkIRect::MakeSize(pageSize))
    , fLastContentEntry(nullptr)
    , fLastMarginContentEntry(nullptr)
    , fDrawingArea(kContent_DrawingArea)
    , fClipStack(nullptr)
    , fFontGlyphUsage(new SkPDFGlyphSetMap)
    , fRasterDpi(rasterDpi)
    , fCanon(canon) {
    SkASSERT(pageSize.width() > 0);
    SkASSERT(pageSize.height() > 0);
    fLegacyBitmap.setInfo(
            SkImageInfo::MakeUnknown(pageSize.width(), pageSize.height()));
    if (flip) {
        // Skia generally uses the top left as the origin but PDF
        // natively has the origin at the bottom left. This matrix
        // corrects for that.  But that only needs to be done once, we
        // don't do it when layering.
        fInitialTransform.setTranslate(0, SkIntToScalar(pageSize.fHeight));
        fInitialTransform.preScale(SK_Scalar1, -SK_Scalar1);
    } else {
        fInitialTransform.setIdentity();
    }
}

SkPDFDevice::~SkPDFDevice() {
    this->cleanUp(true);
}

void SkPDFDevice::init() {
    fContentEntries.free();
    fLastContentEntry = nullptr;
    fMarginContentEntries.free();
    fLastMarginContentEntry = nullptr;
    fDrawingArea = kContent_DrawingArea;
    if (fFontGlyphUsage.get() == nullptr) {
        fFontGlyphUsage.reset(new SkPDFGlyphSetMap);
    }
}

void SkPDFDevice::cleanUp(bool clearFontUsage) {
    fGraphicStateResources.unrefAll();
    fXObjectResources.unrefAll();
    fFontResources.unrefAll();
    fShaderResources.unrefAll();
    fLinkToURLs.deleteAll();
    fLinkToDestinations.deleteAll();
    fNamedDestinations.deleteAll();

    if (clearFontUsage) {
        fFontGlyphUsage->reset();
    }
}

void SkPDFDevice::drawPaint(const SkDraw& d, const SkPaint& paint) {
    SkPaint newPaint = paint;
    replace_srcmode_on_opaque_paint(&newPaint);

    newPaint.setStyle(SkPaint::kFill_Style);
    ScopedContentEntry content(this, d, newPaint);
    internalDrawPaint(newPaint, content.entry());
}

void SkPDFDevice::internalDrawPaint(const SkPaint& paint,
                                    ContentEntry* contentEntry) {
    if (!contentEntry) {
        return;
    }
    SkRect bbox = SkRect::MakeWH(SkIntToScalar(this->width()),
                                 SkIntToScalar(this->height()));
    SkMatrix inverse;
    if (!contentEntry->fState.fMatrix.invert(&inverse)) {
        return;
    }
    inverse.mapRect(&bbox);

    SkPDFUtils::AppendRectangle(bbox, &contentEntry->fContent);
    SkPDFUtils::PaintPath(paint.getStyle(), SkPath::kWinding_FillType,
                          &contentEntry->fContent);
}

void SkPDFDevice::drawPoints(const SkDraw& srcDraw,
                             SkCanvas::PointMode mode,
                             size_t count,
                             const SkPoint* points,
                             const SkPaint& srcPaint) {
    SkPaint passedPaint = srcPaint;
    replace_srcmode_on_opaque_paint(&passedPaint);

    if (count == 0) {
        return;
    }

    if (SkAnnotation* annotation = passedPaint.getAnnotation()) {
        if (handlePointAnnotation(points, count, *srcDraw.fMatrix, annotation)) {
            return;
        }
    }
    SkMatrix newMatrix;
    SkDraw d(srcDraw);
    SkTArray<SkPoint> pointsCopy;
    if (excessive_translation(*d.fMatrix)) {
        newMatrix = untranslate(*d.fMatrix, points[0].x(), points[0].y());
        d.fMatrix = &newMatrix;
        pointsCopy.reset(points, SkToInt(count));
        SkPoint::Offset(&pointsCopy[0], SkToInt(count),
                        -points[0].x(), -points[0].y());
        points = &pointsCopy[0];
    }

    // SkDraw::drawPoints converts to multiple calls to fDevice->drawPath.
    // We only use this when there's a path effect because of the overhead
    // of multiple calls to setUpContentEntry it causes.
    if (passedPaint.getPathEffect()) {
        if (d.fClip->isEmpty()) {
            return;
        }
        SkDraw pointDraw(d);
        pointDraw.fDevice = this;
        pointDraw.drawPoints(mode, count, points, passedPaint, true);
        return;
    }

    const SkPaint* paint = &passedPaint;
    SkPaint modifiedPaint;

    if (mode == SkCanvas::kPoints_PointMode &&
            paint->getStrokeCap() != SkPaint::kRound_Cap) {
        modifiedPaint = *paint;
        paint = &modifiedPaint;
        if (paint->getStrokeWidth()) {
            // PDF won't draw a single point with square/butt caps because the
            // orientation is ambiguous.  Draw a rectangle instead.
            modifiedPaint.setStyle(SkPaint::kFill_Style);
            SkScalar strokeWidth = paint->getStrokeWidth();
            SkScalar halfStroke = SkScalarHalf(strokeWidth);
            for (size_t i = 0; i < count; i++) {
                SkRect r = SkRect::MakeXYWH(points[i].fX, points[i].fY, 0, 0);
                r.inset(-halfStroke, -halfStroke);
                drawRect(d, r, modifiedPaint);
            }
            return;
        } else {
            modifiedPaint.setStrokeCap(SkPaint::kRound_Cap);
        }
    }

    ScopedContentEntry content(this, d, *paint);
    if (!content.entry()) {
        return;
    }

    switch (mode) {
        case SkCanvas::kPolygon_PointMode:
            SkPDFUtils::MoveTo(points[0].fX, points[0].fY,
                               &content.entry()->fContent);
            for (size_t i = 1; i < count; i++) {
                SkPDFUtils::AppendLine(points[i].fX, points[i].fY,
                                       &content.entry()->fContent);
            }
            SkPDFUtils::StrokePath(&content.entry()->fContent);
            break;
        case SkCanvas::kLines_PointMode:
            for (size_t i = 0; i < count/2; i++) {
                SkPDFUtils::MoveTo(points[i * 2].fX, points[i * 2].fY,
                                   &content.entry()->fContent);
                SkPDFUtils::AppendLine(points[i * 2 + 1].fX,
                                       points[i * 2 + 1].fY,
                                       &content.entry()->fContent);
                SkPDFUtils::StrokePath(&content.entry()->fContent);
            }
            break;
        case SkCanvas::kPoints_PointMode:
            SkASSERT(paint->getStrokeCap() == SkPaint::kRound_Cap);
            for (size_t i = 0; i < count; i++) {
                SkPDFUtils::MoveTo(points[i].fX, points[i].fY,
                                   &content.entry()->fContent);
                SkPDFUtils::ClosePath(&content.entry()->fContent);
                SkPDFUtils::StrokePath(&content.entry()->fContent);
            }
            break;
        default:
            SkASSERT(false);
    }
}

static SkPDFDict* create_link_annotation(const SkRect& translatedRect) {
    SkAutoTUnref<SkPDFDict> annotation(new SkPDFDict("Annot"));
    annotation->insertName("Subtype", "Link");

    SkAutoTUnref<SkPDFArray> border(new SkPDFArray);
    border->reserve(3);
    border->appendInt(0);  // Horizontal corner radius.
    border->appendInt(0);  // Vertical corner radius.
    border->appendInt(0);  // Width, 0 = no border.
    annotation->insertObject("Border", border.detach());

    SkAutoTUnref<SkPDFArray> rect(new SkPDFArray);
    rect->reserve(4);
    rect->appendScalar(translatedRect.fLeft);
    rect->appendScalar(translatedRect.fTop);
    rect->appendScalar(translatedRect.fRight);
    rect->appendScalar(translatedRect.fBottom);
    annotation->insertObject("Rect", rect.detach());

    return annotation.detach();
}

static SkPDFDict* create_link_to_url(const SkData* urlData, const SkRect& r) {
    SkAutoTUnref<SkPDFDict> annotation(create_link_annotation(r));

    SkString url(static_cast<const char *>(urlData->data()),
                 urlData->size() - 1);
    SkAutoTUnref<SkPDFDict> action(new SkPDFDict("Action"));
    action->insertName("S", "URI");
    action->insertString("URI", url);
    annotation->insertObject("A", action.detach());
    return annotation.detach();
}

static SkPDFDict* create_link_named_dest(const SkData* nameData,
                                         const SkRect& r) {
    SkAutoTUnref<SkPDFDict> annotation(create_link_annotation(r));
    SkString name(static_cast<const char *>(nameData->data()),
                  nameData->size() - 1);
    annotation->insertName("Dest", name);
    return annotation.detach();
}

void SkPDFDevice::drawRect(const SkDraw& srcDraw,
                           const SkRect& rect,
                           const SkPaint& srcPaint) {
    SkPaint paint = srcPaint;
    replace_srcmode_on_opaque_paint(&paint);
    SkRect r = rect;
    r.sort();

    SkMatrix newMatrix;
    SkDraw d(srcDraw);
    if (excessive_translation(*d.fMatrix)) {
        newMatrix = untranslate(*d.fMatrix, r.x(), r.y());
        d.fMatrix = &newMatrix;
        r.offsetTo(0, 0);
    }

    if (paint.getPathEffect()) {
        if (d.fClip->isEmpty()) {
            return;
        }
        SkPath path;
        path.addRect(r);
        drawPath(d, path, paint, nullptr, true);
        return;
    }

    if (SkAnnotation* annotation = paint.getAnnotation()) {
        SkPath path;
        path.addRect(rect);
        if (handlePathAnnotation(path, d, annotation)) {
            return;
        }
    }

    ScopedContentEntry content(this, d, paint);
    if (!content.entry()) {
        return;
    }
    SkPDFUtils::AppendRectangle(r, &content.entry()->fContent);
    SkPDFUtils::PaintPath(paint.getStyle(), SkPath::kWinding_FillType,
                          &content.entry()->fContent);
}

void SkPDFDevice::drawRRect(const SkDraw& draw,
                            const SkRRect& rrect,
                            const SkPaint& srcPaint) {
    SkPaint paint = srcPaint;
    replace_srcmode_on_opaque_paint(&paint);
    SkPath  path;
    path.addRRect(rrect);
    this->drawPath(draw, path, paint, nullptr, true);
}

void SkPDFDevice::drawOval(const SkDraw& draw,
                           const SkRect& oval,
                           const SkPaint& srcPaint) {
    SkPaint paint = srcPaint;
    replace_srcmode_on_opaque_paint(&paint);
    SkPath  path;
    path.addOval(oval);
    this->drawPath(draw, path, paint, nullptr, true);
}

void SkPDFDevice::drawPath(const SkDraw& srcDraw,
                           const SkPath& origPath,
                           const SkPaint& srcPaint,
                           const SkMatrix* prePathMatrix,
                           bool pathIsMutable) {
    SkMatrix newMatrix;
    SkDraw d(srcDraw);
    SkPath modifiedPath;
    SkPath* pathPtr = const_cast<SkPath*>(&origPath);
    if (excessive_translation(*d.fMatrix)) {
        SkPoint firstPt;
        if (origPath.getPoints(&firstPt, 1) > 0) {
            newMatrix = untranslate(*d.fMatrix, firstPt.x(), firstPt.y());
            d.fMatrix = &newMatrix;
            modifiedPath = origPath;
            modifiedPath.offset(-firstPt.x(), -firstPt.y());
            pathPtr = &modifiedPath;  // NOTE: shader behavior will be off.
            pathIsMutable = true;
        }
    }

    SkPaint paint = srcPaint;
    replace_srcmode_on_opaque_paint(&paint);

    SkMatrix matrix = *d.fMatrix;
    if (prePathMatrix) {
        if (paint.getPathEffect() || paint.getStyle() != SkPaint::kFill_Style) {
            if (pathIsMutable) {
                pathPtr->transform(*prePathMatrix);
            } else {
                pathPtr->transform(*prePathMatrix, &modifiedPath);
                pathPtr = &modifiedPath;
                pathIsMutable = true;
            }
        } else {
            matrix.preConcat(*prePathMatrix);
        }
    }

    if (paint.getPathEffect()) {
        if (d.fClip->isEmpty()) {
            return;
        }
        bool fill;
        if (pathIsMutable) {
            fill = paint.getFillPath(*pathPtr, pathPtr);
        } else {
            fill = paint.getFillPath(*pathPtr, &modifiedPath);
            pathPtr = &modifiedPath;
            pathIsMutable = true;
        }

        SkPaint noEffectPaint(paint);
        noEffectPaint.setPathEffect(nullptr);
        if (fill) {
            noEffectPaint.setStyle(SkPaint::kFill_Style);
        } else {
            noEffectPaint.setStyle(SkPaint::kStroke_Style);
            noEffectPaint.setStrokeWidth(0);
        }
        drawPath(d, *pathPtr, noEffectPaint, nullptr, true);
        return;
    }

    if (handleInversePath(d, origPath, paint, pathIsMutable, prePathMatrix)) {
        return;
    }

    if (SkAnnotation* annotation = paint.getAnnotation()) {
        if (handlePathAnnotation(*pathPtr, d, annotation)) {
            return;
        }
    }

    ScopedContentEntry content(this, d.fClipStack, *d.fClip, matrix, paint);
    if (!content.entry()) {
        return;
    }
    bool consumeDegeratePathSegments =
           paint.getStyle() == SkPaint::kFill_Style ||
           (paint.getStrokeCap() != SkPaint::kRound_Cap &&
            paint.getStrokeCap() != SkPaint::kSquare_Cap);
    SkPDFUtils::EmitPath(*pathPtr, paint.getStyle(),
                         consumeDegeratePathSegments,
                         &content.entry()->fContent);
    SkPDFUtils::PaintPath(paint.getStyle(), pathPtr->getFillType(),
                          &content.entry()->fContent);
}

void SkPDFDevice::drawBitmapRect(const SkDraw& draw,
                                 const SkBitmap& bitmap,
                                 const SkRect* src,
                                 const SkRect& dst,
                                 const SkPaint& srcPaint,
                                 SkCanvas::SrcRectConstraint constraint) {
    const SkImage* image = fCanon->bitmapToImage(bitmap);
    if (!image) {
        return;
    }
    // ownership of this image is retained by the canon.
    this->drawImageRect(draw, image, src, dst, srcPaint, constraint);
}

void SkPDFDevice::drawBitmap(const SkDraw& d,
                             const SkBitmap& bitmap,
                             const SkMatrix& matrix,
                             const SkPaint& srcPaint) {
    SkPaint paint = srcPaint;
    if (bitmap.isOpaque()) {
        replace_srcmode_on_opaque_paint(&paint);
    }

    if (d.fClip->isEmpty()) {
        return;
    }

    SkMatrix transform = matrix;
    transform.postConcat(*d.fMatrix);
    const SkImage* image = fCanon->bitmapToImage(bitmap);
    if (!image) {
        return;
    }
    this->internalDrawImage(transform, d.fClipStack, *d.fClip, image, nullptr,
                            paint);
}

void SkPDFDevice::drawSprite(const SkDraw& d,
                             const SkBitmap& bitmap,
                             int x,
                             int y,
                             const SkPaint& srcPaint) {
    SkPaint paint = srcPaint;
    if (bitmap.isOpaque()) {
        replace_srcmode_on_opaque_paint(&paint);
    }

    if (d.fClip->isEmpty()) {
        return;
    }

    SkMatrix matrix;
    matrix.setTranslate(SkIntToScalar(x), SkIntToScalar(y));
    const SkImage* image = fCanon->bitmapToImage(bitmap);
    if (!image) {
        return;
    }
    this->internalDrawImage(matrix, d.fClipStack, *d.fClip, image, nullptr,
                            paint);
}

void SkPDFDevice::drawImage(const SkDraw& draw,
                            const SkImage* image,
                            SkScalar x,
                            SkScalar y,
                            const SkPaint& srcPaint) {
    SkPaint paint = srcPaint;
    if (!image) {
        return;
    }
    if (image->isOpaque()) {
        replace_srcmode_on_opaque_paint(&paint);
    }
    if (draw.fClip->isEmpty()) {
        return;
    }
    SkMatrix transform = SkMatrix::MakeTrans(x, y);
    transform.postConcat(*draw.fMatrix);
    this->internalDrawImage(transform, draw.fClipStack, *draw.fClip, image,
                            nullptr, paint);
}

void SkPDFDevice::drawImageRect(const SkDraw& draw,
                                const SkImage* image,
                                const SkRect* src,
                                const SkRect& dst,
                                const SkPaint& srcPaint,
                                SkCanvas::SrcRectConstraint constraint) {
    if (!image) {
        return;
    }
    if (draw.fClip->isEmpty()) {
        return;
    }
    SkPaint paint = srcPaint;
    if (image->isOpaque()) {
        replace_srcmode_on_opaque_paint(&paint);
    }
    // TODO: this code path must be updated to respect the flags parameter
    SkMatrix matrix;
    SkRect tmpSrc, tmpDst;
    SkRect imageBounds = SkRect::Make(image->bounds());

    // Compute matrix from the two rectangles
    if (src) {
        tmpSrc = *src;
    } else {
        tmpSrc = imageBounds;
    }
    matrix.setRectToRect(tmpSrc, dst, SkMatrix::kFill_ScaleToFit);

    // clip the tmpSrc to the bounds of the bitmap, and recompute dstRect if
    // needed (if the src was clipped). No check needed if src==null.
    SkAutoTUnref<const SkImage> autoImageUnref;
    if (src) {
        if (!imageBounds.contains(*src)) {
            if (!tmpSrc.intersect(imageBounds)) {
                return; // nothing to draw
            }
            // recompute dst, based on the smaller tmpSrc
            matrix.mapRect(&tmpDst, tmpSrc);
        }

        // since we may need to clamp to the borders of the src rect within
        // the bitmap, we extract a subset.
        SkIRect srcIR;
        tmpSrc.roundOut(&srcIR);

        autoImageUnref.reset(image->newSubset(srcIR));
        if (!autoImageUnref) {
            return;
        }
        image = autoImageUnref;
        // Since we did an extract, we need to adjust the matrix accordingly
        SkScalar dx = 0, dy = 0;
        if (srcIR.fLeft > 0) {
            dx = SkIntToScalar(srcIR.fLeft);
        }
        if (srcIR.fTop > 0) {
            dy = SkIntToScalar(srcIR.fTop);
        }
        if (dx || dy) {
            matrix.preTranslate(dx, dy);
        }
    }
    matrix.postConcat(*draw.fMatrix);
    this->internalDrawImage(matrix, draw.fClipStack, *draw.fClip, image,
                            nullptr, paint);
}

//  Create a PDF string. Maximum length (in bytes) is 65,535.
//  @param input     A string value.
//  @param len       The length of the input array.
//  @param wideChars True iff the upper byte in each uint16_t is
//                   significant and should be encoded and not
//                   discarded.  If true, the upper byte is encoded
//                   first.  Otherwise, we assert the upper byte is
//                   zero.
static SkString format_wide_string(const uint16_t* input,
                                   size_t len,
                                   bool wideChars) {
    if (wideChars) {
        SkASSERT(2 * len < 65535);
        static const char gHex[] = "0123456789ABCDEF";
        SkString result(4 * len + 2);
        result[0] = '<';
        for (size_t i = 0; i < len; i++) {
            result[4 * i + 1] = gHex[(input[i] >> 12) & 0xF];
            result[4 * i + 2] = gHex[(input[i] >>  8) & 0xF];
            result[4 * i + 3] = gHex[(input[i] >>  4) & 0xF];
            result[4 * i + 4] = gHex[(input[i]      ) & 0xF];
        }
        result[4 * len + 1] = '>';
        return result;
    } else {
        SkASSERT(len <= 65535);
        SkString tmp(len);
        for (size_t i = 0; i < len; i++) {
            SkASSERT(0 == input[i] >> 8);
            tmp[i] = static_cast<uint8_t>(input[i]);
        }
        return SkPDFUtils::FormatString(tmp.c_str(), tmp.size());
    }
}

static void draw_transparent_text(SkPDFDevice* device,
                                  const SkDraw& d,
                                  const void* text, size_t len,
                                  SkScalar x, SkScalar y,
                                  const SkPaint& srcPaint) {

    SkPaint transparent;
    if (!SkPDFFont::CanEmbedTypeface(transparent.getTypeface(),
                                     device->getCanon())) {
        SkDEBUGFAIL("default typeface should be embeddable");
        return;  // Avoid infinite loop in release.
    }
    transparent.setTextSize(srcPaint.getTextSize());
    transparent.setColor(SK_ColorTRANSPARENT);
    switch (srcPaint.getTextEncoding()) {
        case SkPaint::kGlyphID_TextEncoding: {
            // Since a glyphId<->Unicode mapping is typeface-specific,
            // map back to Unicode first.
            size_t glyphCount = len / 2;
            SkAutoTMalloc<SkUnichar> unichars(glyphCount);
            srcPaint.glyphsToUnichars(
                    (const uint16_t*)text, SkToInt(glyphCount), &unichars[0]);
            transparent.setTextEncoding(SkPaint::kUTF32_TextEncoding);
            device->drawText(d, &unichars[0],
                             glyphCount * sizeof(SkUnichar),
                             x, y, transparent);
            break;
        }
        case SkPaint::kUTF8_TextEncoding:
        case SkPaint::kUTF16_TextEncoding:
        case SkPaint::kUTF32_TextEncoding:
            transparent.setTextEncoding(srcPaint.getTextEncoding());
            device->drawText(d, text, len, x, y, transparent);
            break;
        default:
            SkFAIL("unknown text encoding");
    }
}


void SkPDFDevice::drawText(const SkDraw& srcDraw, const void* text, size_t len,
                           SkScalar x, SkScalar y, const SkPaint& srcPaint) {
    SkMatrix newMatrix;
    SkDraw d(srcDraw);
    if (excessive_translation(*d.fMatrix)) {
        newMatrix = untranslate(*d.fMatrix, x, y);
        d.fMatrix = &newMatrix;
        x = 0;
        y = 0;
    }

    if (!SkPDFFont::CanEmbedTypeface(srcPaint.getTypeface(), fCanon)) {
        // https://bug.skia.org/3866
        SkPath path;
        srcPaint.getTextPath(text, len, x, y, &path);
        this->drawPath(d, path, srcPaint, &SkMatrix::I(), true);
        // Draw text transparently to make it copyable/searchable/accessable.
        draw_transparent_text(this, d, text, len, x, y, srcPaint);
        return;
    }
    SkPaint paint = srcPaint;
    replace_srcmode_on_opaque_paint(&paint);

    NOT_IMPLEMENTED(paint.getMaskFilter() != nullptr, false);
    if (paint.getMaskFilter() != nullptr) {
        // Don't pretend we support drawing MaskFilters, it makes for artifacts
        // making text unreadable (e.g. same text twice when using CSS shadows).
        return;
    }
    SkPaint textPaint = calculate_text_paint(paint);
    ScopedContentEntry content(this, d, textPaint, true);
    if (!content.entry()) {
        return;
    }

    SkGlyphStorage storage(0);
    const uint16_t* glyphIDs = nullptr;
    int numGlyphs = force_glyph_encoding(paint, text, len, &storage, &glyphIDs);
    textPaint.setTextEncoding(SkPaint::kGlyphID_TextEncoding);

    SkDrawCacheProc glyphCacheProc = textPaint.getDrawCacheProc();
    align_text(glyphCacheProc, textPaint, glyphIDs, numGlyphs, &x, &y);
    content.entry()->fContent.writeText("BT\n");
    set_text_transform(x, y, textPaint.getTextSkewX(),
                       &content.entry()->fContent);
    int consumedGlyphCount = 0;

    SkTDArray<uint16_t> glyphIDsCopy(glyphIDs, numGlyphs);

    while (numGlyphs > consumedGlyphCount) {
        this->updateFont(textPaint, glyphIDs[consumedGlyphCount], content.entry());
        SkPDFFont* font = content.entry()->fState.fFont;

        int availableGlyphs = font->glyphsToPDFFontEncoding(
                glyphIDsCopy.begin() + consumedGlyphCount,
                numGlyphs - consumedGlyphCount);
        fFontGlyphUsage->noteGlyphUsage(
                font,  glyphIDsCopy.begin() + consumedGlyphCount,
                availableGlyphs);
        SkString encodedString =
                format_wide_string(glyphIDsCopy.begin() + consumedGlyphCount,
                                   availableGlyphs, font->multiByteGlyphs());
        content.entry()->fContent.writeText(encodedString.c_str());
        consumedGlyphCount += availableGlyphs;
        content.entry()->fContent.writeText(" Tj\n");
    }
    content.entry()->fContent.writeText("ET\n");
}

void SkPDFDevice::drawPosText(const SkDraw& srcDraw, const void* text, size_t len,
                              const SkScalar pos[], int scalarsPerPos,
                              const SkPoint& srcOffset, const SkPaint& srcPaint) {
    if (len == 0) {
        return;
    }
    SkMatrix newMatrix;
    SkDraw d(srcDraw);
    SkPoint offset(srcOffset);
    SkAutoTMalloc<SkScalar> scalarsBuffer;
    if (excessive_translation(*d.fMatrix)) {
        SkPoint first;
        if (scalarsPerPos != 2) {
            first.set(pos[0], 0);
        } else {
            first.set(pos[0], pos[1]);
        }
        newMatrix = untranslate(*d.fMatrix,
                                first.x() + offset.x(),
                                first.y() + offset.y());
        d.fMatrix = &newMatrix;
        offset.set(0, 0);  // offset -= offset;
        if (first.x() != 0 || first.y() != 0) {
            int glyphCount = srcPaint.textToGlyphs(text, len, NULL);
            if (scalarsPerPos != 2) {
                scalarsBuffer.reset(glyphCount);
                for (int i = 0; i < glyphCount; ++i) {
                    scalarsBuffer[i] = pos[i] - first.x();
                }
            } else {
                scalarsBuffer.reset(2 * glyphCount);
                for (int i = 0; i < glyphCount; ++i) {
                    scalarsBuffer[2 * i]     = pos[2 * i]     - first.x();
                    scalarsBuffer[2 * i + 1] = pos[2 * i + 1] - first.y();
                }
            }
            pos = &scalarsBuffer[0];
        }
    }

    if (!SkPDFFont::CanEmbedTypeface(srcPaint.getTypeface(), fCanon)) {
        const SkPoint* positions = reinterpret_cast<const SkPoint*>(pos);
        SkAutoTMalloc<SkPoint> positionsBuffer;
        if (2 != scalarsPerPos) {
            int glyphCount = srcPaint.textToGlyphs(text, len, NULL);
            positionsBuffer.reset(glyphCount);
            for (int  i = 0; i < glyphCount; ++i) {
                positionsBuffer[i].set(pos[i], 0.0f);
            }
            positions = &positionsBuffer[0];
        }
        SkPath path;
        srcPaint.getPosTextPath(text, len, positions, &path);
        SkMatrix matrix;
        matrix.setTranslate(offset);
        this->drawPath(d, path, srcPaint, &matrix, true);
        // Draw text transparently to make it copyable/searchable/accessable.
        draw_transparent_text(
                this, d, text, len, offset.x() + positions[0].x(),
                offset.y() + positions[0].y(), srcPaint);
        return;
    }

    SkPaint paint = srcPaint;
    replace_srcmode_on_opaque_paint(&paint);

    NOT_IMPLEMENTED(paint.getMaskFilter() != nullptr, false);
    if (paint.getMaskFilter() != nullptr) {
        // Don't pretend we support drawing MaskFilters, it makes for artifacts
        // making text unreadable (e.g. same text twice when using CSS shadows).
        return;
    }
    SkASSERT(1 == scalarsPerPos || 2 == scalarsPerPos);
    SkPaint textPaint = calculate_text_paint(paint);
    ScopedContentEntry content(this, d, textPaint, true);
    if (!content.entry()) {
        return;
    }

    SkGlyphStorage storage(0);
    const uint16_t* glyphIDs = nullptr;
    size_t numGlyphs = force_glyph_encoding(paint, text, len, &storage, &glyphIDs);
    textPaint.setTextEncoding(SkPaint::kGlyphID_TextEncoding);

    SkDrawCacheProc glyphCacheProc = textPaint.getDrawCacheProc();
    content.entry()->fContent.writeText("BT\n");
    this->updateFont(textPaint, glyphIDs[0], content.entry());
    for (size_t i = 0; i < numGlyphs; i++) {
        SkPDFFont* font = content.entry()->fState.fFont;
        uint16_t encodedValue = glyphIDs[i];
        if (font->glyphsToPDFFontEncoding(&encodedValue, 1) != 1) {
            // The current pdf font cannot encode the current glyph.
            // Try to get a pdf font which can encode the current glyph.
            this->updateFont(textPaint, glyphIDs[i], content.entry());
            font = content.entry()->fState.fFont;
            if (font->glyphsToPDFFontEncoding(&encodedValue, 1) != 1) {
                SkDEBUGFAIL("PDF could not encode glyph.");
                continue;
            }
        }

        fFontGlyphUsage->noteGlyphUsage(font, &encodedValue, 1);
        SkScalar x = offset.x() + pos[i * scalarsPerPos];
        SkScalar y = offset.y() + (2 == scalarsPerPos ? pos[i * scalarsPerPos + 1] : 0);

        align_text(glyphCacheProc, textPaint, glyphIDs + i, 1, &x, &y);
        set_text_transform(x, y, textPaint.getTextSkewX(), &content.entry()->fContent);
        SkString encodedString =
                format_wide_string(&encodedValue, 1, font->multiByteGlyphs());
        content.entry()->fContent.writeText(encodedString.c_str());
        content.entry()->fContent.writeText(" Tj\n");
    }
    content.entry()->fContent.writeText("ET\n");
}

void SkPDFDevice::drawVertices(const SkDraw& d, SkCanvas::VertexMode,
                               int vertexCount, const SkPoint verts[],
                               const SkPoint texs[], const SkColor colors[],
                               SkXfermode* xmode, const uint16_t indices[],
                               int indexCount, const SkPaint& paint) {
    if (d.fClip->isEmpty()) {
        return;
    }
    // TODO: implement drawVertices
}

struct RectWithData {
    SkRect rect;
    SkAutoTUnref<const SkData> data;

    RectWithData(const SkRect& rect, const SkData* data)
        : rect(rect), data(SkRef(data)) {}
};

struct NamedDestination {
    SkAutoTUnref<const SkData> nameData;
    SkPoint point;

    NamedDestination(const SkData* nameData, const SkPoint& point)
        : nameData(SkRef(nameData)), point(point) {}
};

void SkPDFDevice::drawDevice(const SkDraw& d, SkBaseDevice* device,
                             int x, int y, const SkPaint& paint) {
    // our onCreateCompatibleDevice() always creates SkPDFDevice subclasses.
    SkPDFDevice* pdfDevice = static_cast<SkPDFDevice*>(device);

    SkScalar scalarX = SkIntToScalar(x);
    SkScalar scalarY = SkIntToScalar(y);
    for (RectWithData* link : pdfDevice->fLinkToURLs) {
        fLinkToURLs.push(new RectWithData(
                link->rect.makeOffset(scalarX, scalarY), link->data));
    }
    for (RectWithData* link : pdfDevice->fLinkToDestinations) {
        fLinkToDestinations.push(new RectWithData(
                link->rect.makeOffset(scalarX, scalarY), link->data));
    }
    for (NamedDestination* d : pdfDevice->fNamedDestinations) {
        fNamedDestinations.push(new NamedDestination(
                d->nameData, d->point + SkPoint::Make(scalarX, scalarY)));
    }

    if (pdfDevice->isContentEmpty()) {
        return;
    }

    SkMatrix matrix;
    matrix.setTranslate(SkIntToScalar(x), SkIntToScalar(y));
    ScopedContentEntry content(this, d.fClipStack, *d.fClip, matrix, paint);
    if (!content.entry()) {
        return;
    }
    if (content.needShape()) {
        SkPath shape;
        shape.addRect(SkRect::MakeXYWH(SkIntToScalar(x), SkIntToScalar(y),
                                       SkIntToScalar(device->width()),
                                       SkIntToScalar(device->height())));
        content.setShape(shape);
    }
    if (!content.needSource()) {
        return;
    }

    SkAutoTUnref<SkPDFFormXObject> xObject(new SkPDFFormXObject(pdfDevice));
    SkPDFUtils::DrawFormXObject(this->addXObjectResource(xObject.get()),
                                &content.entry()->fContent);

    // Merge glyph sets from the drawn device.
    fFontGlyphUsage->merge(pdfDevice->getFontGlyphUsage());
}

SkImageInfo SkPDFDevice::imageInfo() const {
    return fLegacyBitmap.info();
}

void SkPDFDevice::onAttachToCanvas(SkCanvas* canvas) {
    INHERITED::onAttachToCanvas(canvas);

    // Canvas promises that this ptr is valid until onDetachFromCanvas is called
    fClipStack = canvas->getClipStack();
}

void SkPDFDevice::onDetachFromCanvas() {
    INHERITED::onDetachFromCanvas();

    fClipStack = nullptr;
}

SkSurface* SkPDFDevice::newSurface(const SkImageInfo& info, const SkSurfaceProps& props) {
    return SkSurface::NewRaster(info, &props);
}

ContentEntry* SkPDFDevice::getLastContentEntry() {
    if (fDrawingArea == kContent_DrawingArea) {
        return fLastContentEntry;
    } else {
        return fLastMarginContentEntry;
    }
}

SkAutoTDelete<ContentEntry>* SkPDFDevice::getContentEntries() {
    if (fDrawingArea == kContent_DrawingArea) {
        return &fContentEntries;
    } else {
        return &fMarginContentEntries;
    }
}

void SkPDFDevice::setLastContentEntry(ContentEntry* contentEntry) {
    if (fDrawingArea == kContent_DrawingArea) {
        fLastContentEntry = contentEntry;
    } else {
        fLastMarginContentEntry = contentEntry;
    }
}

void SkPDFDevice::setDrawingArea(DrawingArea drawingArea) {
    // A ScopedContentEntry only exists during the course of a draw call, so
    // this can't be called while a ScopedContentEntry exists.
    fDrawingArea = drawingArea;
}

SkPDFDict* SkPDFDevice::createResourceDict() const {
    SkTDArray<SkPDFObject*> fonts;
    fonts.setReserve(fFontResources.count());
    for (SkPDFFont* font : fFontResources) {
        fonts.push(font);
    }
    return SkPDFResourceDict::Create(
            &fGraphicStateResources,
            &fShaderResources,
            &fXObjectResources,
            &fonts);
}

const SkTDArray<SkPDFFont*>& SkPDFDevice::getFontResources() const {
    return fFontResources;
}

SkPDFArray* SkPDFDevice::copyMediaBox() const {
    // should this be a singleton?

    SkAutoTUnref<SkPDFArray> mediaBox(new SkPDFArray);
    mediaBox->reserve(4);
    mediaBox->appendInt(0);
    mediaBox->appendInt(0);
    mediaBox->appendInt(fPageSize.fWidth);
    mediaBox->appendInt(fPageSize.fHeight);
    return mediaBox.detach();
}

SkStreamAsset* SkPDFDevice::content() const {
    SkDynamicMemoryWStream buffer;
    this->writeContent(&buffer);
    return buffer.bytesWritten() > 0
        ? buffer.detachAsStream()
        : new SkMemoryStream;
}

void SkPDFDevice::copyContentEntriesToData(ContentEntry* entry,
        SkWStream* data) const {
    // TODO(ctguil): For margins, I'm not sure fExistingClipStack/Region is the
    // right thing to pass here.
    GraphicStackState gsState(fExistingClipStack, fExistingClipRegion, data);
    while (entry != nullptr) {
        SkPoint translation;
        translation.iset(this->getOrigin());
        translation.negate();
        gsState.updateClip(entry->fState.fClipStack, entry->fState.fClipRegion,
                           translation);
        gsState.updateMatrix(entry->fState.fMatrix);
        gsState.updateDrawingState(entry->fState);

        entry->fContent.writeToStream(data);
        entry = entry->fNext.get();
    }
    gsState.drainStack();
}

void SkPDFDevice::writeContent(SkWStream* out) const {
    if (fInitialTransform.getType() != SkMatrix::kIdentity_Mask) {
        SkPDFUtils::AppendTransform(fInitialTransform, out);
    }

    // TODO(aayushkumar): Apply clip along the margins.  Currently, webkit
    // colors the contentArea white before it starts drawing into it and
    // that currently acts as our clip.
    // Also, think about adding a transform here (or assume that the values
    // sent across account for that)
    SkPDFDevice::copyContentEntriesToData(fMarginContentEntries.get(), out);

    // If the content area is the entire page, then we don't need to clip
    // the content area (PDF area clips to the page size).  Otherwise,
    // we have to clip to the content area; we've already applied the
    // initial transform, so just clip to the device size.
    if (fPageSize != fContentSize) {
        SkRect r = SkRect::MakeWH(SkIntToScalar(this->width()),
                                  SkIntToScalar(this->height()));
        emit_clip(nullptr, &r, out);
    }

    SkPDFDevice::copyContentEntriesToData(fContentEntries.get(), out);
}

/* Draws an inverse filled path by using Path Ops to compute the positive
 * inverse using the current clip as the inverse bounds.
 * Return true if this was an inverse path and was properly handled,
 * otherwise returns false and the normal drawing routine should continue,
 * either as a (incorrect) fallback or because the path was not inverse
 * in the first place.
 */
bool SkPDFDevice::handleInversePath(const SkDraw& d, const SkPath& origPath,
                                    const SkPaint& paint, bool pathIsMutable,
                                    const SkMatrix* prePathMatrix) {
    if (!origPath.isInverseFillType()) {
        return false;
    }

    if (d.fClip->isEmpty()) {
        return false;
    }

    SkPath modifiedPath;
    SkPath* pathPtr = const_cast<SkPath*>(&origPath);
    SkPaint noInversePaint(paint);

    // Merge stroking operations into final path.
    if (SkPaint::kStroke_Style == paint.getStyle() ||
        SkPaint::kStrokeAndFill_Style == paint.getStyle()) {
        bool doFillPath = paint.getFillPath(origPath, &modifiedPath);
        if (doFillPath) {
            noInversePaint.setStyle(SkPaint::kFill_Style);
            noInversePaint.setStrokeWidth(0);
            pathPtr = &modifiedPath;
        } else {
            // To be consistent with the raster output, hairline strokes
            // are rendered as non-inverted.
            modifiedPath.toggleInverseFillType();
            drawPath(d, modifiedPath, paint, nullptr, true);
            return true;
        }
    }

    // Get bounds of clip in current transform space
    // (clip bounds are given in device space).
    SkRect bounds;
    SkMatrix transformInverse;
    SkMatrix totalMatrix = *d.fMatrix;
    if (prePathMatrix) {
        totalMatrix.preConcat(*prePathMatrix);
    }
    if (!totalMatrix.invert(&transformInverse)) {
        return false;
    }
    bounds.set(d.fClip->getBounds());
    transformInverse.mapRect(&bounds);

    // Extend the bounds by the line width (plus some padding)
    // so the edge doesn't cause a visible stroke.
    bounds.outset(paint.getStrokeWidth() + SK_Scalar1,
                  paint.getStrokeWidth() + SK_Scalar1);

    if (!calculate_inverse_path(bounds, *pathPtr, &modifiedPath)) {
        return false;
    }

    drawPath(d, modifiedPath, noInversePaint, prePathMatrix, true);
    return true;
}

bool SkPDFDevice::handlePointAnnotation(const SkPoint* points, size_t count,
                                        const SkMatrix& matrix,
                                        SkAnnotation* annotationInfo) {
    SkData* nameData = annotationInfo->find(
            SkAnnotationKeys::Define_Named_Dest_Key());
    if (nameData) {
        for (size_t i = 0; i < count; i++) {
            SkPoint transformedPoint;
            matrix.mapXY(points[i].x(), points[i].y(), &transformedPoint);
            fNamedDestinations.push(new NamedDestination(nameData, transformedPoint));
        }
        return true;
    }
    return false;
}

bool SkPDFDevice::handlePathAnnotation(const SkPath& path,
                                       const SkDraw& d,
                                       SkAnnotation* annotation) {
    SkASSERT(annotation);

    SkPath transformedPath = path;
    transformedPath.transform(*d.fMatrix);
    SkRasterClip clip = *d.fRC;
    clip.op(transformedPath, SkISize::Make(width(), height()), SkRegion::kIntersect_Op, false);
    SkRect transformedRect = SkRect::Make(clip.getBounds());

    SkData* urlData = annotation->find(SkAnnotationKeys::URL_Key());
    if (urlData) {
        if (!transformedRect.isEmpty()) {
            fLinkToURLs.push(new RectWithData(transformedRect, urlData));
        }
        return true;
    }

    SkData* linkToDestination =
            annotation->find(SkAnnotationKeys::Link_Named_Dest_Key());
    if (linkToDestination) {
        if (!transformedRect.isEmpty()) {
            fLinkToDestinations.push(new RectWithData(transformedRect, linkToDestination));
        }
        return true;
    }

    return false;
}

void SkPDFDevice::appendAnnotations(SkPDFArray* array) const {
    array->reserve(fLinkToURLs.count() + fLinkToDestinations.count());
    for (RectWithData* rectWithURL : fLinkToURLs) {
        SkRect r;
        fInitialTransform.mapRect(&r, rectWithURL->rect);
        array->appendObject(create_link_to_url(rectWithURL->data, r));
    }
    for (RectWithData* linkToDestination : fLinkToDestinations) {
        SkRect r;
        fInitialTransform.mapRect(&r, linkToDestination->rect);
        array->appendObject(create_link_named_dest(linkToDestination->data, r));
    }
}

void SkPDFDevice::appendDestinations(SkPDFDict* dict, SkPDFObject* page) const {
    for (NamedDestination* dest : fNamedDestinations) {
        SkAutoTUnref<SkPDFArray> pdfDest(new SkPDFArray);
        pdfDest->reserve(5);
        pdfDest->appendObjRef(SkRef(page));
        pdfDest->appendName("XYZ");
        SkPoint p = fInitialTransform.mapXY(dest->point.x(), dest->point.y());
        pdfDest->appendScalar(p.x());
        pdfDest->appendScalar(p.y());
        pdfDest->appendInt(0);  // Leave zoom unchanged
        SkString name(static_cast<const char*>(dest->nameData->data()));
        dict->insertObject(name, pdfDest.detach());
    }
}

SkPDFFormXObject* SkPDFDevice::createFormXObjectFromDevice() {
    SkPDFFormXObject* xobject = new SkPDFFormXObject(this);
    // We always draw the form xobjects that we create back into the device, so
    // we simply preserve the font usage instead of pulling it out and merging
    // it back in later.
    cleanUp(false);  // Reset this device to have no content.
    init();
    return xobject;
}

void SkPDFDevice::drawFormXObjectWithMask(int xObjectIndex,
                                          SkPDFFormXObject* mask,
                                          const SkClipStack* clipStack,
                                          const SkRegion& clipRegion,
                                          SkXfermode::Mode mode,
                                          bool invertClip) {
    if (clipRegion.isEmpty() && !invertClip) {
        return;
    }

    SkAutoTUnref<SkPDFObject> sMaskGS(SkPDFGraphicState::GetSMaskGraphicState(
            mask, invertClip, SkPDFGraphicState::kAlpha_SMaskMode));

    SkMatrix identity;
    identity.reset();
    SkPaint paint;
    paint.setXfermodeMode(mode);
    ScopedContentEntry content(this, clipStack, clipRegion, identity, paint);
    if (!content.entry()) {
        return;
    }
    SkPDFUtils::ApplyGraphicState(addGraphicStateResource(sMaskGS.get()),
                                  &content.entry()->fContent);
    SkPDFUtils::DrawFormXObject(xObjectIndex, &content.entry()->fContent);

    sMaskGS.reset(SkPDFGraphicState::GetNoSMaskGraphicState());
    SkPDFUtils::ApplyGraphicState(addGraphicStateResource(sMaskGS.get()),
                                  &content.entry()->fContent);
}

ContentEntry* SkPDFDevice::setUpContentEntry(const SkClipStack* clipStack,
                                             const SkRegion& clipRegion,
                                             const SkMatrix& matrix,
                                             const SkPaint& paint,
                                             bool hasText,
                                             SkPDFFormXObject** dst) {
    *dst = nullptr;
    if (clipRegion.isEmpty()) {
        return nullptr;
    }

    // The clip stack can come from an SkDraw where it is technically optional.
    SkClipStack synthesizedClipStack;
    if (clipStack == nullptr) {
        if (clipRegion == fExistingClipRegion) {
            clipStack = &fExistingClipStack;
        } else {
            // GraphicStackState::updateClip expects the clip stack to have
            // fExistingClip as a prefix, so start there, then set the clip
            // to the passed region.
            synthesizedClipStack = fExistingClipStack;
            SkPath clipPath;
            clipRegion.getBoundaryPath(&clipPath);
            synthesizedClipStack.clipDevPath(clipPath, SkRegion::kReplace_Op,
                                             false);
            clipStack = &synthesizedClipStack;
        }
    }

    SkXfermode::Mode xfermode = SkXfermode::kSrcOver_Mode;
    if (paint.getXfermode()) {
        paint.getXfermode()->asMode(&xfermode);
    }

    // For the following modes, we want to handle source and destination
    // separately, so make an object of what's already there.
    if (xfermode == SkXfermode::kClear_Mode       ||
            xfermode == SkXfermode::kSrc_Mode     ||
            xfermode == SkXfermode::kSrcIn_Mode   ||
            xfermode == SkXfermode::kDstIn_Mode   ||
            xfermode == SkXfermode::kSrcOut_Mode  ||
            xfermode == SkXfermode::kDstOut_Mode  ||
            xfermode == SkXfermode::kSrcATop_Mode ||
            xfermode == SkXfermode::kDstATop_Mode ||
            xfermode == SkXfermode::kModulate_Mode) {
        if (!isContentEmpty()) {
            *dst = createFormXObjectFromDevice();
            SkASSERT(isContentEmpty());
        } else if (xfermode != SkXfermode::kSrc_Mode &&
                   xfermode != SkXfermode::kSrcOut_Mode) {
            // Except for Src and SrcOut, if there isn't anything already there,
            // then we're done.
            return nullptr;
        }
    }
    // TODO(vandebo): Figure out how/if we can handle the following modes:
    // Xor, Plus.

    // Dst xfer mode doesn't draw source at all.
    if (xfermode == SkXfermode::kDst_Mode) {
        return nullptr;
    }

    ContentEntry* entry;
    SkAutoTDelete<ContentEntry> newEntry;

    ContentEntry* lastContentEntry = getLastContentEntry();
    if (lastContentEntry && lastContentEntry->fContent.getOffset() == 0) {
        entry = lastContentEntry;
    } else {
        newEntry.reset(new ContentEntry);
        entry = newEntry.get();
    }

    populateGraphicStateEntryFromPaint(matrix, *clipStack, clipRegion, paint,
                                       hasText, &entry->fState);
    if (lastContentEntry && xfermode != SkXfermode::kDstOver_Mode &&
            entry->fState.compareInitialState(lastContentEntry->fState)) {
        return lastContentEntry;
    }

    SkAutoTDelete<ContentEntry>* contentEntries = getContentEntries();
    if (!lastContentEntry) {
        contentEntries->reset(entry);
        setLastContentEntry(entry);
    } else if (xfermode == SkXfermode::kDstOver_Mode) {
        entry->fNext.reset(contentEntries->detach());
        contentEntries->reset(entry);
    } else {
        lastContentEntry->fNext.reset(entry);
        setLastContentEntry(entry);
    }
    newEntry.detach();
    return entry;
}

void SkPDFDevice::finishContentEntry(SkXfermode::Mode xfermode,
                                     SkPDFFormXObject* dst,
                                     SkPath* shape) {
    if (xfermode != SkXfermode::kClear_Mode       &&
            xfermode != SkXfermode::kSrc_Mode     &&
            xfermode != SkXfermode::kDstOver_Mode &&
            xfermode != SkXfermode::kSrcIn_Mode   &&
            xfermode != SkXfermode::kDstIn_Mode   &&
            xfermode != SkXfermode::kSrcOut_Mode  &&
            xfermode != SkXfermode::kDstOut_Mode  &&
            xfermode != SkXfermode::kSrcATop_Mode &&
            xfermode != SkXfermode::kDstATop_Mode &&
            xfermode != SkXfermode::kModulate_Mode) {
        SkASSERT(!dst);
        return;
    }
    if (xfermode == SkXfermode::kDstOver_Mode) {
        SkASSERT(!dst);
        ContentEntry* firstContentEntry = getContentEntries()->get();
        if (firstContentEntry->fContent.getOffset() == 0) {
            // For DstOver, an empty content entry was inserted before the rest
            // of the content entries. If nothing was drawn, it needs to be
            // removed.
            SkAutoTDelete<ContentEntry>* contentEntries = getContentEntries();
            contentEntries->reset(firstContentEntry->fNext.detach());
        }
        return;
    }
    if (!dst) {
        SkASSERT(xfermode == SkXfermode::kSrc_Mode ||
                 xfermode == SkXfermode::kSrcOut_Mode);
        return;
    }

    ContentEntry* contentEntries = getContentEntries()->get();
    SkASSERT(dst);
    SkASSERT(!contentEntries->fNext.get());
    // Changing the current content into a form-xobject will destroy the clip
    // objects which is fine since the xobject will already be clipped. However
    // if source has shape, we need to clip it too, so a copy of the clip is
    // saved.
    SkClipStack clipStack = contentEntries->fState.fClipStack;
    SkRegion clipRegion = contentEntries->fState.fClipRegion;

    SkMatrix identity;
    identity.reset();
    SkPaint stockPaint;

    SkAutoTUnref<SkPDFFormXObject> srcFormXObject;
    if (isContentEmpty()) {
        // If nothing was drawn and there's no shape, then the draw was a
        // no-op, but dst needs to be restored for that to be true.
        // If there is shape, then an empty source with Src, SrcIn, SrcOut,
        // DstIn, DstAtop or Modulate reduces to Clear and DstOut or SrcAtop
        // reduces to Dst.
        if (shape == nullptr || xfermode == SkXfermode::kDstOut_Mode ||
                xfermode == SkXfermode::kSrcATop_Mode) {
            ScopedContentEntry content(this, &fExistingClipStack,
                                       fExistingClipRegion, identity,
                                       stockPaint);
            SkPDFUtils::DrawFormXObject(this->addXObjectResource(dst),
                                        &content.entry()->fContent);
            return;
        } else {
            xfermode = SkXfermode::kClear_Mode;
        }
    } else {
        SkASSERT(!fContentEntries->fNext.get());
        srcFormXObject.reset(createFormXObjectFromDevice());
    }

    // TODO(vandebo) srcFormXObject may contain alpha, but here we want it
    // without alpha.
    if (xfermode == SkXfermode::kSrcATop_Mode) {
        // TODO(vandebo): In order to properly support SrcATop we have to track
        // the shape of what's been drawn at all times. It's the intersection of
        // the non-transparent parts of the device and the outlines (shape) of
        // all images and devices drawn.
        drawFormXObjectWithMask(addXObjectResource(srcFormXObject.get()), dst,
                                &fExistingClipStack, fExistingClipRegion,
                                SkXfermode::kSrcOver_Mode, true);
    } else {
        SkAutoTUnref<SkPDFFormXObject> dstMaskStorage;
        SkPDFFormXObject* dstMask = srcFormXObject.get();
        if (shape != nullptr) {
            // Draw shape into a form-xobject.
            SkDraw d;
            d.fMatrix = &identity;
            d.fClip = &clipRegion;
            d.fClipStack = &clipStack;
            SkPaint filledPaint;
            filledPaint.setColor(SK_ColorBLACK);
            filledPaint.setStyle(SkPaint::kFill_Style);
            this->drawPath(d, *shape, filledPaint, nullptr, true);

            dstMaskStorage.reset(createFormXObjectFromDevice());
            dstMask = dstMaskStorage.get();
        }
        drawFormXObjectWithMask(addXObjectResource(dst), dstMask,
                                &fExistingClipStack, fExistingClipRegion,
                                SkXfermode::kSrcOver_Mode, true);
    }

    if (xfermode == SkXfermode::kClear_Mode) {
        return;
    } else if (xfermode == SkXfermode::kSrc_Mode ||
            xfermode == SkXfermode::kDstATop_Mode) {
        ScopedContentEntry content(this, &fExistingClipStack,
                                   fExistingClipRegion, identity, stockPaint);
        if (content.entry()) {
            SkPDFUtils::DrawFormXObject(
                    this->addXObjectResource(srcFormXObject.get()),
                    &content.entry()->fContent);
        }
        if (xfermode == SkXfermode::kSrc_Mode) {
            return;
        }
    } else if (xfermode == SkXfermode::kSrcATop_Mode) {
        ScopedContentEntry content(this, &fExistingClipStack,
                                   fExistingClipRegion, identity, stockPaint);
        if (content.entry()) {
            SkPDFUtils::DrawFormXObject(this->addXObjectResource(dst),
                                        &content.entry()->fContent);
        }
    }

    SkASSERT(xfermode == SkXfermode::kSrcIn_Mode   ||
             xfermode == SkXfermode::kDstIn_Mode   ||
             xfermode == SkXfermode::kSrcOut_Mode  ||
             xfermode == SkXfermode::kDstOut_Mode  ||
             xfermode == SkXfermode::kSrcATop_Mode ||
             xfermode == SkXfermode::kDstATop_Mode ||
             xfermode == SkXfermode::kModulate_Mode);

    if (xfermode == SkXfermode::kSrcIn_Mode ||
            xfermode == SkXfermode::kSrcOut_Mode ||
            xfermode == SkXfermode::kSrcATop_Mode) {
        drawFormXObjectWithMask(addXObjectResource(srcFormXObject.get()), dst,
                                &fExistingClipStack, fExistingClipRegion,
                                SkXfermode::kSrcOver_Mode,
                                xfermode == SkXfermode::kSrcOut_Mode);
    } else {
        SkXfermode::Mode mode = SkXfermode::kSrcOver_Mode;
        if (xfermode == SkXfermode::kModulate_Mode) {
            drawFormXObjectWithMask(addXObjectResource(srcFormXObject.get()),
                                    dst, &fExistingClipStack,
                                    fExistingClipRegion,
                                    SkXfermode::kSrcOver_Mode, false);
            mode = SkXfermode::kMultiply_Mode;
        }
        drawFormXObjectWithMask(addXObjectResource(dst), srcFormXObject.get(),
                                &fExistingClipStack, fExistingClipRegion, mode,
                                xfermode == SkXfermode::kDstOut_Mode);
    }
}

bool SkPDFDevice::isContentEmpty() {
    ContentEntry* contentEntries = getContentEntries()->get();
    if (!contentEntries || contentEntries->fContent.getOffset() == 0) {
        SkASSERT(!contentEntries || !contentEntries->fNext.get());
        return true;
    }
    return false;
}

void SkPDFDevice::populateGraphicStateEntryFromPaint(
        const SkMatrix& matrix,
        const SkClipStack& clipStack,
        const SkRegion& clipRegion,
        const SkPaint& paint,
        bool hasText,
        GraphicStateEntry* entry) {
    NOT_IMPLEMENTED(paint.getPathEffect() != nullptr, false);
    NOT_IMPLEMENTED(paint.getMaskFilter() != nullptr, false);
    NOT_IMPLEMENTED(paint.getColorFilter() != nullptr, false);

    entry->fMatrix = matrix;
    entry->fClipStack = clipStack;
    entry->fClipRegion = clipRegion;
    entry->fColor = SkColorSetA(paint.getColor(), 0xFF);
    entry->fShaderIndex = -1;

    // PDF treats a shader as a color, so we only set one or the other.
    SkAutoTUnref<SkPDFObject> pdfShader;
    const SkShader* shader = paint.getShader();
    SkColor color = paint.getColor();
    if (shader) {
        // PDF positions patterns relative to the initial transform, so
        // we need to apply the current transform to the shader parameters.
        SkMatrix transform = matrix;
        transform.postConcat(fInitialTransform);

        // PDF doesn't support kClamp_TileMode, so we simulate it by making
        // a pattern the size of the current clip.
        SkIRect bounds = clipRegion.getBounds();

        // We need to apply the initial transform to bounds in order to get
        // bounds in a consistent coordinate system.
        SkRect boundsTemp;
        boundsTemp.set(bounds);
        fInitialTransform.mapRect(&boundsTemp);
        boundsTemp.roundOut(&bounds);

        SkScalar rasterScale =
                SkIntToScalar(fRasterDpi) / DPI_FOR_RASTER_SCALE_ONE;
        pdfShader.reset(SkPDFShader::GetPDFShader(
                fCanon, fRasterDpi, *shader, transform, bounds, rasterScale));

        if (pdfShader.get()) {
            // pdfShader has been canonicalized so we can directly compare
            // pointers.
            int resourceIndex = fShaderResources.find(pdfShader.get());
            if (resourceIndex < 0) {
                resourceIndex = fShaderResources.count();
                fShaderResources.push(pdfShader.get());
                pdfShader.get()->ref();
            }
            entry->fShaderIndex = resourceIndex;
        } else {
            // A color shader is treated as an invalid shader so we don't have
            // to set a shader just for a color.
            SkShader::GradientInfo gradientInfo;
            SkColor gradientColor;
            gradientInfo.fColors = &gradientColor;
            gradientInfo.fColorOffsets = nullptr;
            gradientInfo.fColorCount = 1;
            if (shader->asAGradient(&gradientInfo) ==
                    SkShader::kColor_GradientType) {
                entry->fColor = SkColorSetA(gradientColor, 0xFF);
                color = gradientColor;
            }
        }
    }

    SkAutoTUnref<SkPDFGraphicState> newGraphicState;
    if (color == paint.getColor()) {
        newGraphicState.reset(
                SkPDFGraphicState::GetGraphicStateForPaint(fCanon, paint));
    } else {
        SkPaint newPaint = paint;
        newPaint.setColor(color);
        newGraphicState.reset(
                SkPDFGraphicState::GetGraphicStateForPaint(fCanon, newPaint));
    }
    int resourceIndex = addGraphicStateResource(newGraphicState.get());
    entry->fGraphicStateIndex = resourceIndex;

    if (hasText) {
        entry->fTextScaleX = paint.getTextScaleX();
        entry->fTextFill = paint.getStyle();
    } else {
        entry->fTextScaleX = 0;
    }
}

int SkPDFDevice::addGraphicStateResource(SkPDFObject* gs) {
    // Assumes that gs has been canonicalized (so we can directly compare
    // pointers).
    int result = fGraphicStateResources.find(gs);
    if (result < 0) {
        result = fGraphicStateResources.count();
        fGraphicStateResources.push(gs);
        gs->ref();
    }
    return result;
}

int SkPDFDevice::addXObjectResource(SkPDFObject* xObject) {
    // Assumes that xobject has been canonicalized (so we can directly compare
    // pointers).
    int result = fXObjectResources.find(xObject);
    if (result < 0) {
        result = fXObjectResources.count();
        fXObjectResources.push(xObject);
        xObject->ref();
    }
    return result;
}

void SkPDFDevice::updateFont(const SkPaint& paint, uint16_t glyphID,
                             ContentEntry* contentEntry) {
    SkTypeface* typeface = paint.getTypeface();
    if (contentEntry->fState.fFont == nullptr ||
            contentEntry->fState.fTextSize != paint.getTextSize() ||
            !contentEntry->fState.fFont->hasGlyph(glyphID)) {
        int fontIndex = getFontResourceIndex(typeface, glyphID);
        contentEntry->fContent.writeText("/");
        contentEntry->fContent.writeText(SkPDFResourceDict::getResourceName(
                SkPDFResourceDict::kFont_ResourceType,
                fontIndex).c_str());
        contentEntry->fContent.writeText(" ");
        SkPDFUtils::AppendScalar(paint.getTextSize(), &contentEntry->fContent);
        contentEntry->fContent.writeText(" Tf\n");
        contentEntry->fState.fFont = fFontResources[fontIndex];
    }
}

int SkPDFDevice::getFontResourceIndex(SkTypeface* typeface, uint16_t glyphID) {
    SkAutoTUnref<SkPDFFont> newFont(
            SkPDFFont::GetFontResource(fCanon, typeface, glyphID));
    int resourceIndex = fFontResources.find(newFont.get());
    if (resourceIndex < 0) {
        resourceIndex = fFontResources.count();
        fFontResources.push(newFont.get());
        newFont.get()->ref();
    }
    return resourceIndex;
}

static SkSize rect_to_size(const SkRect& r) {
    return SkSize::Make(r.width(), r.height());
}

static const SkImage* color_filter(const SkImage* image,
                                   SkColorFilter* colorFilter) {
    SkAutoTUnref<SkSurface> surface(SkSurface::NewRaster(
            SkImageInfo::MakeN32Premul(image->dimensions())));
    if (!surface) {
        return image;
    }
    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorTRANSPARENT);
    SkPaint paint;
    paint.setColorFilter(colorFilter);
    canvas->drawImage(image, 0, 0, &paint);
    canvas->flush();
    return surface->newImageSnapshot();
}

////////////////////////////////////////////////////////////////////////////////
void SkPDFDevice::internalDrawImage(const SkMatrix& origMatrix,
                                    const SkClipStack* clipStack,
                                    const SkRegion& origClipRegion,
                                    const SkImage* image,
                                    const SkIRect* srcRect,
                                    const SkPaint& paint) {
    SkASSERT(image);
    #ifdef SK_PDF_IMAGE_STATS
    gDrawImageCalls.fetch_add(1);
    #endif
    SkMatrix matrix = origMatrix;
    SkRegion perspectiveBounds;
    const SkRegion* clipRegion = &origClipRegion;
    SkAutoTUnref<const SkImage> autoImageUnref;

    if (srcRect) {
        autoImageUnref.reset(image->newSubset(*srcRect));
        if (!autoImageUnref) {
            return;
        }
        image = autoImageUnref;
    }
    // Rasterize the bitmap using perspective in a new bitmap.
    if (origMatrix.hasPerspective()) {
        if (fRasterDpi == 0) {
            return;
        }
        // Transform the bitmap in the new space, without taking into
        // account the initial transform.
        SkPath perspectiveOutline;
        SkRect imageBounds = SkRect::Make(image->bounds());
        perspectiveOutline.addRect(imageBounds);
        perspectiveOutline.transform(origMatrix);

        // TODO(edisonn): perf - use current clip too.
        // Retrieve the bounds of the new shape.
        SkRect bounds = perspectiveOutline.getBounds();

        // Transform the bitmap in the new space, taking into
        // account the initial transform.
        SkMatrix total = origMatrix;
        total.postConcat(fInitialTransform);
        SkScalar dpiScale = SkIntToScalar(fRasterDpi) /
                            SkIntToScalar(DPI_FOR_RASTER_SCALE_ONE);
        total.postScale(dpiScale, dpiScale);

        SkPath physicalPerspectiveOutline;
        physicalPerspectiveOutline.addRect(imageBounds);
        physicalPerspectiveOutline.transform(total);

        SkRect physicalPerspectiveBounds =
                physicalPerspectiveOutline.getBounds();
        SkScalar scaleX = physicalPerspectiveBounds.width() / bounds.width();
        SkScalar scaleY = physicalPerspectiveBounds.height() / bounds.height();

        // TODO(edisonn): A better approach would be to use a bitmap shader
        // (in clamp mode) and draw a rect over the entire bounding box. Then
        // intersect perspectiveOutline to the clip. That will avoid introducing
        // alpha to the image while still giving good behavior at the edge of
        // the image.  Avoiding alpha will reduce the pdf size and generation
        // CPU time some.

        SkISize wh = rect_to_size(physicalPerspectiveBounds).toCeil();

        SkAutoTUnref<SkSurface> surface(
                SkSurface::NewRaster(SkImageInfo::MakeN32Premul(wh)));
        if (!surface) {
            return;
        }
        SkCanvas* canvas = surface->getCanvas();
        canvas->clear(SK_ColorTRANSPARENT);

        SkScalar deltaX = bounds.left();
        SkScalar deltaY = bounds.top();

        SkMatrix offsetMatrix = origMatrix;
        offsetMatrix.postTranslate(-deltaX, -deltaY);
        offsetMatrix.postScale(scaleX, scaleY);

        // Translate the draw in the new canvas, so we perfectly fit the
        // shape in the bitmap.
        canvas->setMatrix(offsetMatrix);
        canvas->drawImage(image, 0, 0, nullptr);
        // Make sure the final bits are in the bitmap.
        canvas->flush();

        // In the new space, we use the identity matrix translated
        // and scaled to reflect DPI.
        matrix.setScale(1 / scaleX, 1 / scaleY);
        matrix.postTranslate(deltaX, deltaY);

        perspectiveBounds.setRect(bounds.roundOut());
        clipRegion = &perspectiveBounds;
        srcRect = nullptr;

        autoImageUnref.reset(surface->newImageSnapshot());
        image = autoImageUnref;
    }

    SkMatrix scaled;
    // Adjust for origin flip.
    scaled.setScale(SK_Scalar1, -SK_Scalar1);
    scaled.postTranslate(0, SK_Scalar1);
    // Scale the image up from 1x1 to WxH.
    SkIRect subset = image->bounds();
    scaled.postScale(SkIntToScalar(image->width()),
                     SkIntToScalar(image->height()));
    scaled.postConcat(matrix);
    ScopedContentEntry content(this, clipStack, *clipRegion, scaled, paint);
    if (!content.entry() || (srcRect && !subset.intersect(*srcRect))) {
        return;
    }
    if (content.needShape()) {
        SkPath shape;
        shape.addRect(SkRect::Make(subset));
        shape.transform(matrix);
        content.setShape(shape);
    }
    if (!content.needSource()) {
        return;
    }

    if (SkColorFilter* colorFilter = paint.getColorFilter()) {
        // TODO(https://bug.skia.org/4378): implement colorfilter on other
        // draw calls.  This code here works for all
        // drawBitmap*()/drawImage*() calls amd ImageFilters (which
        // rasterize a layer on this backend).  Fortuanely, this seems
        // to be how Chromium impements most color-filters.
        autoImageUnref.reset(color_filter(image, colorFilter));
        image = autoImageUnref;
        // TODO(halcanary): de-dupe this by caching filtered images.
        // (maybe in the resource cache?)
    }
    SkAutoTUnref<SkPDFObject> pdfimage(SkSafeRef(fCanon->findPDFBitmap(image)));
    if (!pdfimage) {
        pdfimage.reset(SkPDFCreateBitmapObject(
                               image, fCanon->fPixelSerializer));
        if (!pdfimage) {
            return;
        }
        fCanon->addPDFBitmap(image->uniqueID(), pdfimage);
    }
    SkPDFUtils::DrawFormXObject(this->addXObjectResource(pdfimage.get()),
                                &content.entry()->fContent);
}
