/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkImageDecoder.h"
#include "SkImageEncoder.h"
#include "SkColor.h"
#include "SkColorPriv.h"
#include "SkDither.h"
#include "SkMath.h"
#include "SkRTConf.h"
#include "SkScaledBitmapSampler.h"
#include "SkStream.h"
#include "SkTemplates.h"
#include "SkUtils.h"
#include "transform_scanline.h"

#include "png.h"

/* These were dropped in libpng >= 1.4 */
#ifndef png_infopp_NULL
#define png_infopp_NULL nullptr
#endif

#ifndef png_bytepp_NULL
#define png_bytepp_NULL nullptr
#endif

#ifndef int_p_NULL
#define int_p_NULL nullptr
#endif

#ifndef png_flush_ptr_NULL
#define png_flush_ptr_NULL nullptr
#endif

#define DEFAULT_FOR_SUPPRESS_PNG_IMAGE_DECODER_WARNINGS true
SK_CONF_DECLARE(bool, c_suppressPNGImageDecoderWarnings,
                "images.png.suppressDecoderWarnings",
                DEFAULT_FOR_SUPPRESS_PNG_IMAGE_DECODER_WARNINGS,
                "Suppress most PNG warnings when calling image decode "
                "functions.");

class SkPNGImageIndex {
public:
    // Takes ownership of stream.
    SkPNGImageIndex(SkStreamRewindable* stream, png_structp png_ptr, png_infop info_ptr)
        : fStream(stream)
        , fPng_ptr(png_ptr)
        , fInfo_ptr(info_ptr)
        , fColorType(kUnknown_SkColorType) {
        SkASSERT(stream != nullptr);
    }
    ~SkPNGImageIndex() {
        if (fPng_ptr) {
            png_destroy_read_struct(&fPng_ptr, &fInfo_ptr, png_infopp_NULL);
        }
    }

    SkAutoTDelete<SkStreamRewindable>   fStream;
    png_structp                         fPng_ptr;
    png_infop                           fInfo_ptr;
    SkColorType                         fColorType;
};

class SkPNGImageDecoder : public SkImageDecoder {
public:
    SkPNGImageDecoder() {
        fImageIndex = nullptr;
    }
    Format getFormat() const override {
        return kPNG_Format;
    }

    virtual ~SkPNGImageDecoder() { delete fImageIndex; }

protected:
    Result onDecode(SkStream* stream, SkBitmap* bm, Mode) override;

private:
    SkPNGImageIndex* fImageIndex;

    bool onDecodeInit(SkStream* stream, png_structp *png_ptrp, png_infop *info_ptrp);
    bool decodePalette(png_structp png_ptr, png_infop info_ptr, int bitDepth,
                       bool * SK_RESTRICT hasAlphap, bool *reallyHasAlphap,
                       SkColorTable **colorTablep);
    bool getBitmapColorType(png_structp, png_infop, SkColorType*, bool* hasAlpha,
                            SkPMColor* theTranspColor);

    typedef SkImageDecoder INHERITED;
};

#ifndef png_jmpbuf
#  define png_jmpbuf(png_ptr) ((png_ptr)->jmpbuf)
#endif

#define PNG_BYTES_TO_CHECK 4

/* Automatically clean up after throwing an exception */
struct PNGAutoClean {
    PNGAutoClean(png_structp p, png_infop i): png_ptr(p), info_ptr(i) {}
    ~PNGAutoClean() {
        png_destroy_read_struct(&png_ptr, &info_ptr, png_infopp_NULL);
    }
private:
    png_structp png_ptr;
    png_infop info_ptr;
};

static void sk_read_fn(png_structp png_ptr, png_bytep data, png_size_t length) {
    SkStream* sk_stream = (SkStream*) png_get_io_ptr(png_ptr);
    size_t bytes = sk_stream->read(data, length);
    if (bytes != length) {
        png_error(png_ptr, "Read Error!");
    }
}

#ifdef PNG_READ_UNKNOWN_CHUNKS_SUPPORTED
static int sk_read_user_chunk(png_structp png_ptr, png_unknown_chunkp chunk) {
    SkPngChunkReader* peeker = (SkPngChunkReader*)png_get_user_chunk_ptr(png_ptr);
    // readChunk() returning true means continue decoding
    return peeker->readChunk((const char*)chunk->name, chunk->data, chunk->size) ?
            1 : -1;
}
#endif

static void sk_error_fn(png_structp png_ptr, png_const_charp msg) {
    if (!c_suppressPNGImageDecoderWarnings) {
        SkDEBUGF(("------ png error %s\n", msg));
    }
    longjmp(png_jmpbuf(png_ptr), 1);
}

static void skip_src_rows(png_structp png_ptr, uint8_t storage[], int count) {
    for (int i = 0; i < count; i++) {
        uint8_t* tmp = storage;
        png_read_rows(png_ptr, &tmp, png_bytepp_NULL, 1);
    }
}

static bool pos_le(int value, int max) {
    return value > 0 && value <= max;
}

static bool substituteTranspColor(SkBitmap* bm, SkPMColor match) {
    SkASSERT(bm->colorType() == kN32_SkColorType);

    bool reallyHasAlpha = false;

    for (int y = bm->height() - 1; y >= 0; --y) {
        SkPMColor* p = bm->getAddr32(0, y);
        for (int x = bm->width() - 1; x >= 0; --x) {
            if (match == *p) {
                *p = 0;
                reallyHasAlpha = true;
            }
            p += 1;
        }
    }
    return reallyHasAlpha;
}

static bool canUpscalePaletteToConfig(SkColorType dstColorType, bool srcHasAlpha) {
    switch (dstColorType) {
        case kN32_SkColorType:
        case kARGB_4444_SkColorType:
            return true;
        case kRGB_565_SkColorType:
            // only return true if the src is opaque (since 565 is opaque)
            return !srcHasAlpha;
        default:
            return false;
    }
}

// call only if color_type is PALETTE. Returns true if the ctable has alpha
static bool hasTransparencyInPalette(png_structp png_ptr, png_infop info_ptr) {
    png_bytep trans;
    int num_trans;

    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
        png_get_tRNS(png_ptr, info_ptr, &trans, &num_trans, nullptr);
        return num_trans > 0;
    }
    return false;
}

void do_nothing_warning_fn(png_structp, png_const_charp) {
    /* do nothing */
}

bool SkPNGImageDecoder::onDecodeInit(SkStream* sk_stream, png_structp *png_ptrp,
                                     png_infop *info_ptrp) {
    /* Create and initialize the png_struct with the desired error handler
    * functions.  If you want to use the default stderr and longjump method,
    * you can supply nullptr for the last three parameters.  We also supply the
    * the compiler header file version, so that we know if the application
    * was compiled with a compatible version of the library.  */

    png_error_ptr user_warning_fn =
        (c_suppressPNGImageDecoderWarnings) ? (&do_nothing_warning_fn) : nullptr;
    /* nullptr means to leave as default library behavior. */
    /* c_suppressPNGImageDecoderWarnings default depends on SK_DEBUG. */
    /* To suppress warnings with a SK_DEBUG binary, set the
     * environment variable "skia_images_png_suppressDecoderWarnings"
     * to "true".  Inside a program that links to skia:
     * SK_CONF_SET("images.png.suppressDecoderWarnings", true); */

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING,
        nullptr, sk_error_fn, user_warning_fn);
    //   png_voidp user_error_ptr, user_error_fn, user_warning_fn);
    if (png_ptr == nullptr) {
        return false;
    }

    *png_ptrp = png_ptr;

    /* Allocate/initialize the memory for image information. */
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (info_ptr == nullptr) {
        png_destroy_read_struct(&png_ptr, png_infopp_NULL, png_infopp_NULL);
        return false;
    }
    *info_ptrp = info_ptr;

    /* Set error handling if you are using the setjmp/longjmp method (this is
    * the normal method of doing things with libpng).  REQUIRED unless you
    * set up your own error handlers in the png_create_read_struct() earlier.
    */
    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, png_infopp_NULL);
        return false;
    }

    /* If you are using replacement read functions, instead of calling
    * png_init_io() here you would call:
    */
    png_set_read_fn(png_ptr, (void *)sk_stream, sk_read_fn);
    /* where user_io_ptr is a structure you want available to the callbacks */
    /* If we have already read some of the signature */
//  png_set_sig_bytes(png_ptr, 0 /* sig_read */ );

#ifdef PNG_READ_UNKNOWN_CHUNKS_SUPPORTED
    // hookup our peeker so we can see any user-chunks the caller may be interested in
    png_set_keep_unknown_chunks(png_ptr, PNG_HANDLE_CHUNK_ALWAYS, (png_byte*)"", 0);
    if (this->getPeeker()) {
        png_set_read_user_chunk_fn(png_ptr, (png_voidp)this->getPeeker(), sk_read_user_chunk);
    }
#endif
    /* The call to png_read_info() gives us all of the information from the
    * PNG file before the first IDAT (image data chunk). */
    png_read_info(png_ptr, info_ptr);
    png_uint_32 origWidth, origHeight;
    int bitDepth, colorType;
    png_get_IHDR(png_ptr, info_ptr, &origWidth, &origHeight, &bitDepth,
                 &colorType, int_p_NULL, int_p_NULL, int_p_NULL);

    /* tell libpng to strip 16 bit/color files down to 8 bits/color */
    if (bitDepth == 16) {
        png_set_strip_16(png_ptr);
    }
#ifdef PNG_READ_PACK_SUPPORTED
    /* Extract multiple pixels with bit depths of 1, 2, and 4 from a single
     * byte into separate bytes (useful for paletted and grayscale images). */
    if (bitDepth < 8) {
        png_set_packing(png_ptr);
    }
#endif
    /* Expand grayscale images to the full 8 bits from 1, 2, or 4 bits/pixel */
    if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 8) {
        png_set_expand_gray_1_2_4_to_8(png_ptr);
    }

    return true;
}

SkImageDecoder::Result SkPNGImageDecoder::onDecode(SkStream* sk_stream, SkBitmap* decodedBitmap,
                                                   Mode mode) {
    png_structp png_ptr;
    png_infop info_ptr;

    if (!onDecodeInit(sk_stream, &png_ptr, &info_ptr)) {
        return kFailure;
    }

    PNGAutoClean autoClean(png_ptr, info_ptr);

    if (setjmp(png_jmpbuf(png_ptr))) {
        return kFailure;
    }

    png_uint_32 origWidth, origHeight;
    int bitDepth, pngColorType, interlaceType;
    png_get_IHDR(png_ptr, info_ptr, &origWidth, &origHeight, &bitDepth,
                 &pngColorType, &interlaceType, int_p_NULL, int_p_NULL);

    SkColorType         colorType;
    bool                hasAlpha = false;
    SkPMColor           theTranspColor = 0; // 0 tells us not to try to match

    if (!this->getBitmapColorType(png_ptr, info_ptr, &colorType, &hasAlpha, &theTranspColor)) {
        return kFailure;
    }

    SkAlphaType alphaType = this->getRequireUnpremultipliedColors() ?
                                kUnpremul_SkAlphaType : kPremul_SkAlphaType;
    const int sampleSize = this->getSampleSize();
    SkScaledBitmapSampler sampler(origWidth, origHeight, sampleSize);
    decodedBitmap->setInfo(SkImageInfo::Make(sampler.scaledWidth(), sampler.scaledHeight(),
                                             colorType, alphaType));

    if (SkImageDecoder::kDecodeBounds_Mode == mode) {
        return kSuccess;
    }

    // from here down we are concerned with colortables and pixels

    // we track if we actually see a non-opaque pixels, since sometimes a PNG sets its colortype
    // to |= PNG_COLOR_MASK_ALPHA, but all of its pixels are in fact opaque. We care, since we
    // draw lots faster if we can flag the bitmap has being opaque
    bool reallyHasAlpha = false;
    SkColorTable* colorTable = nullptr;

    if (pngColorType == PNG_COLOR_TYPE_PALETTE) {
        decodePalette(png_ptr, info_ptr, bitDepth, &hasAlpha, &reallyHasAlpha, &colorTable);
    }

    SkAutoUnref aur(colorTable);

    if (!this->allocPixelRef(decodedBitmap,
                             kIndex_8_SkColorType == colorType ? colorTable : nullptr)) {
        return kFailure;
    }

    SkAutoLockPixels alp(*decodedBitmap);

    // Repeat setjmp, otherwise variables declared since the last call (e.g. alp
    // and aur) won't get their destructors called in case of a failure.
    if (setjmp(png_jmpbuf(png_ptr))) {
        return kFailure;
    }

    /* Turn on interlace handling.  REQUIRED if you are not using
    *  png_read_image().  To see how to handle interlacing passes,
    *  see the png_read_row() method below:
    */
    const int number_passes = (interlaceType != PNG_INTERLACE_NONE) ?
                              png_set_interlace_handling(png_ptr) : 1;

    /* Optional call to gamma correct and add the background to the palette
    *  and update info structure.  REQUIRED if you are expecting libpng to
    *  update the palette for you (ie you selected such a transform above).
    */
    png_read_update_info(png_ptr, info_ptr);

    if ((kAlpha_8_SkColorType == colorType || kIndex_8_SkColorType == colorType) &&
            1 == sampleSize) {
        if (kAlpha_8_SkColorType == colorType) {
            // For an A8 bitmap, we assume there is an alpha for speed. It is
            // possible the bitmap is opaque, but that is an unlikely use case
            // since it would not be very interesting.
            reallyHasAlpha = true;
            // A8 is only allowed if the original was GRAY.
            SkASSERT(PNG_COLOR_TYPE_GRAY == pngColorType);
        }
        for (int i = 0; i < number_passes; i++) {
            for (png_uint_32 y = 0; y < origHeight; y++) {
                uint8_t* bmRow = decodedBitmap->getAddr8(0, y);
                png_read_rows(png_ptr, &bmRow, png_bytepp_NULL, 1);
            }
        }
    } else {
        SkScaledBitmapSampler::SrcConfig sc;
        int srcBytesPerPixel = 4;

        if (colorTable != nullptr) {
            sc = SkScaledBitmapSampler::kIndex;
            srcBytesPerPixel = 1;
        } else if (kAlpha_8_SkColorType == colorType) {
            // A8 is only allowed if the original was GRAY.
            SkASSERT(PNG_COLOR_TYPE_GRAY == pngColorType);
            sc = SkScaledBitmapSampler::kGray;
            srcBytesPerPixel = 1;
        } else if (hasAlpha) {
            sc = SkScaledBitmapSampler::kRGBA;
        } else {
            sc = SkScaledBitmapSampler::kRGBX;
        }

        /*  We have to pass the colortable explicitly, since we may have one
            even if our decodedBitmap doesn't, due to the request that we
            upscale png's palette to a direct model
         */
        const SkPMColor* colors = colorTable ? colorTable->readColors() : nullptr;
        if (!sampler.begin(decodedBitmap, sc, *this, colors)) {
            return kFailure;
        }
        const int height = decodedBitmap->height();

        if (number_passes > 1) {
            SkAutoTMalloc<uint8_t> storage(origWidth * origHeight * srcBytesPerPixel);
            uint8_t* base = storage.get();
            size_t rowBytes = origWidth * srcBytesPerPixel;

            for (int i = 0; i < number_passes; i++) {
                uint8_t* row = base;
                for (png_uint_32 y = 0; y < origHeight; y++) {
                    uint8_t* bmRow = row;
                    png_read_rows(png_ptr, &bmRow, png_bytepp_NULL, 1);
                    row += rowBytes;
                }
            }
            // now sample it
            base += sampler.srcY0() * rowBytes;
            for (int y = 0; y < height; y++) {
                reallyHasAlpha |= sampler.next(base);
                base += sampler.srcDY() * rowBytes;
            }
        } else {
            SkAutoTMalloc<uint8_t> storage(origWidth * srcBytesPerPixel);
            uint8_t* srcRow = storage.get();
            skip_src_rows(png_ptr, srcRow, sampler.srcY0());

            for (int y = 0; y < height; y++) {
                uint8_t* tmp = srcRow;
                png_read_rows(png_ptr, &tmp, png_bytepp_NULL, 1);
                reallyHasAlpha |= sampler.next(srcRow);
                if (y < height - 1) {
                    skip_src_rows(png_ptr, srcRow, sampler.srcDY() - 1);
                }
            }

            // skip the rest of the rows (if any)
            png_uint_32 read = (height - 1) * sampler.srcDY() +
                               sampler.srcY0() + 1;
            SkASSERT(read <= origHeight);
            skip_src_rows(png_ptr, srcRow, origHeight - read);
        }
    }

    /* read rest of file, and get additional chunks in info_ptr - REQUIRED */
    png_read_end(png_ptr, info_ptr);

    if (0 != theTranspColor) {
        reallyHasAlpha |= substituteTranspColor(decodedBitmap, theTranspColor);
    }
    if (reallyHasAlpha && this->getRequireUnpremultipliedColors()) {
        switch (decodedBitmap->colorType()) {
            case kIndex_8_SkColorType:
                // Fall through.
            case kARGB_4444_SkColorType:
                // We have chosen not to support unpremul for these colortypes.
                return kFailure;
            default: {
                // Fall through to finish the decode. This colortype either
                // supports unpremul or it is irrelevant because it has no
                // alpha (or only alpha).
                // These brackets prevent a warning.
            }
        }
    }

    if (!reallyHasAlpha) {
        decodedBitmap->setAlphaType(kOpaque_SkAlphaType);
    }
    return kSuccess;
}



bool SkPNGImageDecoder::getBitmapColorType(png_structp png_ptr, png_infop info_ptr,
                                           SkColorType* colorTypep,
                                           bool* hasAlphap,
                                           SkPMColor* SK_RESTRICT theTranspColorp) {
    png_uint_32 origWidth, origHeight;
    int bitDepth, colorType;
    png_get_IHDR(png_ptr, info_ptr, &origWidth, &origHeight, &bitDepth,
                 &colorType, int_p_NULL, int_p_NULL, int_p_NULL);

#ifdef PNG_sBIT_SUPPORTED
    // check for sBIT chunk data, in case we should disable dithering because
    // our data is not truely 8bits per component
    png_color_8p sig_bit;
    if (this->getDitherImage() && png_get_sBIT(png_ptr, info_ptr, &sig_bit)) {
#if 0
        SkDebugf("----- sBIT %d %d %d %d\n", sig_bit->red, sig_bit->green,
                 sig_bit->blue, sig_bit->alpha);
#endif
        // 0 seems to indicate no information available
        if (pos_le(sig_bit->red, SK_R16_BITS) &&
            pos_le(sig_bit->green, SK_G16_BITS) &&
            pos_le(sig_bit->blue, SK_B16_BITS)) {
            this->setDitherImage(false);
        }
    }
#endif

    if (colorType == PNG_COLOR_TYPE_PALETTE) {
        bool paletteHasAlpha = hasTransparencyInPalette(png_ptr, info_ptr);
        *colorTypep = this->getPrefColorType(kIndex_SrcDepth, paletteHasAlpha);
        // now see if we can upscale to their requested colortype
        if (!canUpscalePaletteToConfig(*colorTypep, paletteHasAlpha)) {
            *colorTypep = kIndex_8_SkColorType;
        }
    } else {
        png_color_16p transpColor = nullptr;
        int numTransp = 0;

        png_get_tRNS(png_ptr, info_ptr, nullptr, &numTransp, &transpColor);

        bool valid = png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS);

        if (valid && numTransp == 1 && transpColor != nullptr) {
            /*  Compute our transparent color, which we'll match against later.
                We don't really handle 16bit components properly here, since we
                do our compare *after* the values have been knocked down to 8bit
                which means we will find more matches than we should. The real
                fix seems to be to see the actual 16bit components, do the
                compare, and then knock it down to 8bits ourselves.
            */
            if (colorType & PNG_COLOR_MASK_COLOR) {
                if (16 == bitDepth) {
                    *theTranspColorp = SkPackARGB32(0xFF, transpColor->red >> 8,
                                                    transpColor->green >> 8,
                                                    transpColor->blue >> 8);
                } else {
                    /* We apply the mask because in a very small
                       number of corrupt PNGs, (transpColor->red > 255)
                       and (bitDepth == 8), for certain versions of libpng. */
                    *theTranspColorp = SkPackARGB32(0xFF,
                                                    0xFF & (transpColor->red),
                                                    0xFF & (transpColor->green),
                                                    0xFF & (transpColor->blue));
                }
            } else {    // gray
                if (16 == bitDepth) {
                    *theTranspColorp = SkPackARGB32(0xFF, transpColor->gray >> 8,
                                                    transpColor->gray >> 8,
                                                    transpColor->gray >> 8);
                } else {
                    /* We apply the mask because in a very small
                       number of corrupt PNGs, (transpColor->red >
                       255) and (bitDepth == 8), for certain versions
                       of libpng.  For safety we assume the same could
                       happen with a grayscale PNG.  */
                    *theTranspColorp = SkPackARGB32(0xFF,
                                                    0xFF & (transpColor->gray),
                                                    0xFF & (transpColor->gray),
                                                    0xFF & (transpColor->gray));
                }
            }
        }

        if (valid ||
            PNG_COLOR_TYPE_RGB_ALPHA == colorType ||
            PNG_COLOR_TYPE_GRAY_ALPHA == colorType) {
            *hasAlphap = true;
        }

        SrcDepth srcDepth = k32Bit_SrcDepth;
        if (PNG_COLOR_TYPE_GRAY == colorType) {
            srcDepth = k8BitGray_SrcDepth;
            // Remove this assert, which fails on desk_pokemonwiki.skp
            //SkASSERT(!*hasAlphap);
        }

        *colorTypep = this->getPrefColorType(srcDepth, *hasAlphap);
        // now match the request against our capabilities
        if (*hasAlphap) {
            if (*colorTypep != kARGB_4444_SkColorType) {
                *colorTypep = kN32_SkColorType;
            }
        } else {
            if (kAlpha_8_SkColorType == *colorTypep) {
                if (k8BitGray_SrcDepth != srcDepth) {
                    // Converting a non grayscale image to A8 is not currently supported.
                    *colorTypep = kN32_SkColorType;
                }
            } else if (*colorTypep != kRGB_565_SkColorType &&
                       *colorTypep != kARGB_4444_SkColorType) {
                *colorTypep = kN32_SkColorType;
            }
        }
    }

    // sanity check for size
    {
        int64_t size = sk_64_mul(origWidth, origHeight);
        // now check that if we are 4-bytes per pixel, we also don't overflow
        if (size < 0 || size > (0x7FFFFFFF >> 2)) {
            return false;
        }
    }

    // If the image has alpha and the decoder wants unpremultiplied
    // colors, the only supported colortype is 8888.
    if (this->getRequireUnpremultipliedColors() && *hasAlphap) {
        *colorTypep = kN32_SkColorType;
    }

    if (fImageIndex != nullptr) {
        if (kUnknown_SkColorType == fImageIndex->fColorType) {
            // This is the first time for this subset decode. From now on,
            // all decodes must be in the same colortype.
            fImageIndex->fColorType = *colorTypep;
        } else if (fImageIndex->fColorType != *colorTypep) {
            // Requesting a different colortype for a subsequent decode is not
            // supported. Report failure before we make changes to png_ptr.
            return false;
        }
    }

    bool convertGrayToRGB = PNG_COLOR_TYPE_GRAY == colorType && *colorTypep != kAlpha_8_SkColorType;

    // Unless the user is requesting A8, convert a grayscale image into RGB.
    // GRAY_ALPHA will always be converted to RGB
    if (convertGrayToRGB || colorType == PNG_COLOR_TYPE_GRAY_ALPHA) {
        png_set_gray_to_rgb(png_ptr);
    }

    // Add filler (or alpha) byte (after each RGB triplet) if necessary.
    if (colorType == PNG_COLOR_TYPE_RGB || convertGrayToRGB) {
        png_set_filler(png_ptr, 0xff, PNG_FILLER_AFTER);
    }

    return true;
}

typedef uint32_t (*PackColorProc)(U8CPU a, U8CPU r, U8CPU g, U8CPU b);

bool SkPNGImageDecoder::decodePalette(png_structp png_ptr, png_infop info_ptr,
                                      int bitDepth, bool *hasAlphap,
                                      bool *reallyHasAlphap,
                                      SkColorTable **colorTablep) {
    int numPalette;
    png_colorp palette;
    png_bytep trans;
    int numTrans;

    png_get_PLTE(png_ptr, info_ptr, &palette, &numPalette);

    SkPMColor colorStorage[256];    // worst-case storage
    SkPMColor* colorPtr = colorStorage;

    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
        png_get_tRNS(png_ptr, info_ptr, &trans, &numTrans, nullptr);
        *hasAlphap = (numTrans > 0);
    } else {
        numTrans = 0;
    }

    // check for bad images that might make us crash
    if (numTrans > numPalette) {
        numTrans = numPalette;
    }

    int index = 0;
    int transLessThanFF = 0;

    // Choose which function to use to create the color table. If the final destination's
    // colortype is unpremultiplied, the color table will store unpremultiplied colors.
    PackColorProc proc;
    if (this->getRequireUnpremultipliedColors()) {
        proc = &SkPackARGB32NoCheck;
    } else {
        proc = &SkPreMultiplyARGB;
    }
    for (; index < numTrans; index++) {
        transLessThanFF |= (int)*trans - 0xFF;
        *colorPtr++ = proc(*trans++, palette->red, palette->green, palette->blue);
        palette++;
    }
    bool reallyHasAlpha = (transLessThanFF < 0);

    for (; index < numPalette; index++) {
        *colorPtr++ = SkPackARGB32(0xFF, palette->red, palette->green, palette->blue);
        palette++;
    }

    /*  BUGGY IMAGE WORKAROUND

        Invalid images could contain pixel values that are greater than the number of palette
        entries. Since we use pixel values as indices into the palette this could result in reading
        beyond the end of the palette which could leak the contents of uninitialized memory. To
        ensure this doesn't happen, we grow the colortable to the maximum size that can be
        addressed by the bitdepth of the image and fill it with the last palette color or black if
        the palette is empty (really broken image).
    */
    int colorCount = SkTMax(numPalette, 1 << SkTMin(bitDepth, 8));
    SkPMColor lastColor = index > 0 ? colorPtr[-1] : SkPackARGB32(0xFF, 0, 0, 0);
    for (; index < colorCount; index++) {
        *colorPtr++ = lastColor;
    }

    *colorTablep = new SkColorTable(colorStorage, colorCount);
    *reallyHasAlphap = reallyHasAlpha;
    return true;
}

///////////////////////////////////////////////////////////////////////////////

#include "SkColorPriv.h"
#include "SkUnPreMultiply.h"

static void sk_write_fn(png_structp png_ptr, png_bytep data, png_size_t len) {
    SkWStream* sk_stream = (SkWStream*)png_get_io_ptr(png_ptr);
    if (!sk_stream->write(data, len)) {
        png_error(png_ptr, "sk_write_fn Error!");
    }
}

static transform_scanline_proc choose_proc(SkColorType ct, bool hasAlpha) {
    // we don't care about search on alpha if we're kIndex8, since only the
    // colortable packing cares about that distinction, not the pixels
    if (kIndex_8_SkColorType == ct) {
        hasAlpha = false;   // we store false in the table entries for kIndex8
    }

    static const struct {
        SkColorType             fColorType;
        bool                    fHasAlpha;
        transform_scanline_proc fProc;
    } gMap[] = {
        { kRGB_565_SkColorType,     false,  transform_scanline_565 },
        { kN32_SkColorType,         false,  transform_scanline_888 },
        { kN32_SkColorType,         true,   transform_scanline_8888 },
        { kARGB_4444_SkColorType,   false,  transform_scanline_444 },
        { kARGB_4444_SkColorType,   true,   transform_scanline_4444 },
        { kIndex_8_SkColorType,     false,  transform_scanline_memcpy },
    };

    for (int i = SK_ARRAY_COUNT(gMap) - 1; i >= 0; --i) {
        if (gMap[i].fColorType == ct && gMap[i].fHasAlpha == hasAlpha) {
            return gMap[i].fProc;
        }
    }
    sk_throw();
    return nullptr;
}

// return the minimum legal bitdepth (by png standards) for this many colortable
// entries. SkBitmap always stores in 8bits per pixel, but for colorcount <= 16,
// we can use fewer bits per in png
static int computeBitDepth(int colorCount) {
#if 0
    int bits = SkNextLog2(colorCount);
    SkASSERT(bits >= 1 && bits <= 8);
    // now we need bits itself to be a power of 2 (e.g. 1, 2, 4, 8)
    return SkNextPow2(bits);
#else
    // for the moment, we don't know how to pack bitdepth < 8
    return 8;
#endif
}

/*  Pack palette[] with the corresponding colors, and if hasAlpha is true, also
    pack trans[] and return the number of trans[] entries written. If hasAlpha
    is false, the return value will always be 0.

    Note: this routine takes care of unpremultiplying the RGB values when we
    have alpha in the colortable, since png doesn't support premul colors
*/
static inline int pack_palette(SkColorTable* ctable,
                               png_color* SK_RESTRICT palette,
                               png_byte* SK_RESTRICT trans, bool hasAlpha) {
    const SkPMColor* SK_RESTRICT colors = ctable ? ctable->readColors() : nullptr;
    const int ctCount = ctable->count();
    int i, num_trans = 0;

    if (hasAlpha) {
        /*  first see if we have some number of fully opaque at the end of the
            ctable. PNG allows num_trans < num_palette, but all of the trans
            entries must come first in the palette. If I was smarter, I'd
            reorder the indices and ctable so that all non-opaque colors came
            first in the palette. But, since that would slow down the encode,
            I'm leaving the indices and ctable order as is, and just looking
            at the tail of the ctable for opaqueness.
        */
        num_trans = ctCount;
        for (i = ctCount - 1; i >= 0; --i) {
            if (SkGetPackedA32(colors[i]) != 0xFF) {
                break;
            }
            num_trans -= 1;
        }

        const SkUnPreMultiply::Scale* SK_RESTRICT table =
                                            SkUnPreMultiply::GetScaleTable();

        for (i = 0; i < num_trans; i++) {
            const SkPMColor c = *colors++;
            const unsigned a = SkGetPackedA32(c);
            const SkUnPreMultiply::Scale s = table[a];
            trans[i] = a;
            palette[i].red = SkUnPreMultiply::ApplyScale(s, SkGetPackedR32(c));
            palette[i].green = SkUnPreMultiply::ApplyScale(s,SkGetPackedG32(c));
            palette[i].blue = SkUnPreMultiply::ApplyScale(s, SkGetPackedB32(c));
        }
        // now fall out of this if-block to use common code for the trailing
        // opaque entries
    }

    // these (remaining) entries are opaque
    for (i = num_trans; i < ctCount; i++) {
        SkPMColor c = *colors++;
        palette[i].red = SkGetPackedR32(c);
        palette[i].green = SkGetPackedG32(c);
        palette[i].blue = SkGetPackedB32(c);
    }
    return num_trans;
}

class SkPNGImageEncoder : public SkImageEncoder {
protected:
    bool onEncode(SkWStream* stream, const SkBitmap& bm, int quality) override;
private:
    bool doEncode(SkWStream* stream, const SkBitmap& bm,
                  const bool& hasAlpha, int colorType,
                  int bitDepth, SkColorType ct,
                  png_color_8& sig_bit);

    typedef SkImageEncoder INHERITED;
};

bool SkPNGImageEncoder::onEncode(SkWStream* stream,
                                 const SkBitmap& originalBitmap,
                                 int /*quality*/) {
    SkBitmap copy;
    const SkBitmap* bitmap = &originalBitmap;
    switch (originalBitmap.colorType()) {
        case kIndex_8_SkColorType:
        case kN32_SkColorType:
        case kARGB_4444_SkColorType:
        case kRGB_565_SkColorType:
            break;
        default:
            // TODO(scroggo): support 8888-but-not-N32 natively.
            // TODO(scroggo): support kGray_8 directly.
            // TODO(scroggo): support Alpha_8 as Grayscale(black)+Alpha
            if (originalBitmap.copyTo(&copy, kN32_SkColorType)) {
                bitmap = &copy;
            }
    }
    SkColorType ct = bitmap->colorType();

    const bool hasAlpha = !bitmap->isOpaque();
    int colorType = PNG_COLOR_MASK_COLOR;
    int bitDepth = 8;   // default for color
    png_color_8 sig_bit;

    switch (ct) {
        case kIndex_8_SkColorType:
            colorType |= PNG_COLOR_MASK_PALETTE;
            // fall through to the ARGB_8888 case
        case kN32_SkColorType:
            sig_bit.red = 8;
            sig_bit.green = 8;
            sig_bit.blue = 8;
            sig_bit.alpha = 8;
            break;
        case kARGB_4444_SkColorType:
            sig_bit.red = 4;
            sig_bit.green = 4;
            sig_bit.blue = 4;
            sig_bit.alpha = 4;
            break;
        case kRGB_565_SkColorType:
            sig_bit.red = 5;
            sig_bit.green = 6;
            sig_bit.blue = 5;
            sig_bit.alpha = 0;
            break;
        default:
            return false;
    }

    if (hasAlpha) {
        // don't specify alpha if we're a palette, even if our ctable has alpha
        if (!(colorType & PNG_COLOR_MASK_PALETTE)) {
            colorType |= PNG_COLOR_MASK_ALPHA;
        }
    } else {
        sig_bit.alpha = 0;
    }

    SkAutoLockPixels alp(*bitmap);
    // readyToDraw checks for pixels (and colortable if that is required)
    if (!bitmap->readyToDraw()) {
        return false;
    }

    // we must do this after we have locked the pixels
    SkColorTable* ctable = bitmap->getColorTable();
    if (ctable) {
        if (ctable->count() == 0) {
            return false;
        }
        // check if we can store in fewer than 8 bits
        bitDepth = computeBitDepth(ctable->count());
    }

    return doEncode(stream, *bitmap, hasAlpha, colorType, bitDepth, ct, sig_bit);
}

bool SkPNGImageEncoder::doEncode(SkWStream* stream, const SkBitmap& bitmap,
                  const bool& hasAlpha, int colorType,
                  int bitDepth, SkColorType ct,
                  png_color_8& sig_bit) {

    png_structp png_ptr;
    png_infop info_ptr;

    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, sk_error_fn,
                                      nullptr);
    if (nullptr == png_ptr) {
        return false;
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (nullptr == info_ptr) {
        png_destroy_write_struct(&png_ptr,  png_infopp_NULL);
        return false;
    }

    /* Set error handling.  REQUIRED if you aren't supplying your own
    * error handling functions in the png_create_write_struct() call.
    */
    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        return false;
    }

    png_set_write_fn(png_ptr, (void*)stream, sk_write_fn, png_flush_ptr_NULL);

    /* Set the image information here.  Width and height are up to 2^31,
    * bit_depth is one of 1, 2, 4, 8, or 16, but valid values also depend on
    * the color_type selected. color_type is one of PNG_COLOR_TYPE_GRAY,
    * PNG_COLOR_TYPE_GRAY_ALPHA, PNG_COLOR_TYPE_PALETTE, PNG_COLOR_TYPE_RGB,
    * or PNG_COLOR_TYPE_RGB_ALPHA.  interlace is either PNG_INTERLACE_NONE or
    * PNG_INTERLACE_ADAM7, and the compression_type and filter_type MUST
    * currently be PNG_COMPRESSION_TYPE_BASE and PNG_FILTER_TYPE_BASE. REQUIRED
    */

    png_set_IHDR(png_ptr, info_ptr, bitmap.width(), bitmap.height(),
                 bitDepth, colorType,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE,
                 PNG_FILTER_TYPE_BASE);

    // set our colortable/trans arrays if needed
    png_color paletteColors[256];
    png_byte trans[256];
    if (kIndex_8_SkColorType == ct) {
        SkColorTable* ct = bitmap.getColorTable();
        int numTrans = pack_palette(ct, paletteColors, trans, hasAlpha);
        png_set_PLTE(png_ptr, info_ptr, paletteColors, ct->count());
        if (numTrans > 0) {
            png_set_tRNS(png_ptr, info_ptr, trans, numTrans, nullptr);
        }
    }
#ifdef PNG_sBIT_SUPPORTED
    png_set_sBIT(png_ptr, info_ptr, &sig_bit);
#endif
    png_write_info(png_ptr, info_ptr);

    const char* srcImage = (const char*)bitmap.getPixels();
    SkAutoSTMalloc<1024, char> rowStorage(bitmap.width() << 2);
    char* storage = rowStorage.get();
    transform_scanline_proc proc = choose_proc(ct, hasAlpha);

    for (int y = 0; y < bitmap.height(); y++) {
        png_bytep row_ptr = (png_bytep)storage;
        proc(srcImage, bitmap.width(), storage);
        png_write_rows(png_ptr, &row_ptr, 1);
        srcImage += bitmap.rowBytes();
    }

    png_write_end(png_ptr, info_ptr);

    /* clean up after the write, and free any memory allocated */
    png_destroy_write_struct(&png_ptr, &info_ptr);
    return true;
}

///////////////////////////////////////////////////////////////////////////////
DEFINE_DECODER_CREATOR(PNGImageDecoder);
DEFINE_ENCODER_CREATOR(PNGImageEncoder);
///////////////////////////////////////////////////////////////////////////////

static bool is_png(SkStreamRewindable* stream) {
    char buf[PNG_BYTES_TO_CHECK];
    if (stream->read(buf, PNG_BYTES_TO_CHECK) == PNG_BYTES_TO_CHECK &&
        !png_sig_cmp((png_bytep) buf, (png_size_t)0, PNG_BYTES_TO_CHECK)) {
        return true;
    }
    return false;
}

SkImageDecoder* sk_libpng_dfactory(SkStreamRewindable* stream) {
    if (is_png(stream)) {
        return new SkPNGImageDecoder;
    }
    return nullptr;
}

static SkImageDecoder::Format get_format_png(SkStreamRewindable* stream) {
    if (is_png(stream)) {
        return SkImageDecoder::kPNG_Format;
    }
    return SkImageDecoder::kUnknown_Format;
}

SkImageEncoder* sk_libpng_efactory(SkImageEncoder::Type t) {
    return (SkImageEncoder::kPNG_Type == t) ? new SkPNGImageEncoder : nullptr;
}

static SkImageDecoder_DecodeReg gDReg(sk_libpng_dfactory);
static SkImageDecoder_FormatReg gFormatReg(get_format_png);
static SkImageEncoder_EncodeReg gEReg(sk_libpng_efactory);
