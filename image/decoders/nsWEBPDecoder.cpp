/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ImageLogging.h"
#include "nsWEBPDecoder.h"

#include "nsIInputStream.h"

#include "nspr.h"
#include "nsCRT.h"
#include "gfxColor.h"

#include "gfxPlatform.h"

namespace mozilla {
namespace image {

#if defined(PR_LOGGING)
static PRLogModuleInfo *gWEBPLog = PR_NewLogModule("WEBPDecoder");
static PRLogModuleInfo *gWEBPDecoderAccountingLog =
                        PR_NewLogModule("WEBPDecoderAccounting");
#else
#define gWEBPlog
#define gWEBPDecoderAccountingLog
#endif

nsWEBPDecoder::nsWEBPDecoder(RasterImage* aImage)
 : Decoder(aImage)
{
  PR_LOG(gWEBPDecoderAccountingLog, PR_LOG_DEBUG,
         ("nsWEBPDecoder::nsWEBPDecoder: Creating WEBP decoder %p",
          this));
}

nsWEBPDecoder::~nsWEBPDecoder()
{
  PR_LOG(gWEBPDecoderAccountingLog, PR_LOG_DEBUG,
         ("nsWEBPDecoder::~nsWEBPDecoder: Destroying WEBP decoder %p",
          this));
}


void
nsWEBPDecoder::InitInternal()
{
  if (!WebPInitDecBuffer(&mDecBuf)) {
    PostDecoderError(NS_ERROR_FAILURE);
    return;
  }
  mLastLine = 0;
  mDecBuf.colorspace = MODE_rgbA;
  mDecoder = WebPINewDecoder(&mDecBuf);
  if (!mDecoder) {
    PostDecoderError(NS_ERROR_FAILURE);
  }
}

void
nsWEBPDecoder::FinishInternal()
{
  // Flush the Decoder and let it free the output image buffer.
  WebPIDelete(mDecoder);
  WebPFreeDecBuffer(&mDecBuf);

  // We should never make multiple frames
  MOZ_ASSERT(GetFrameCount() <= 1, "Multiple WebP frames?");

  // Send notifications if appropriate
  if (!IsSizeDecode() && (GetFrameCount() == 1)) {
    PostFrameStop();
    PostDecodeDone();
  }
}

void
nsWEBPDecoder::WriteInternal(const char *aBuffer, uint32_t aCount)
{
  MOZ_ASSERT(!HasError(), "Shouldn't call WriteInternal after error!");

  const uint8_t* buf = (const uint8_t*)aBuffer;
  VP8StatusCode rv = WebPIAppend(mDecoder, buf, aCount);
  if (rv == VP8_STATUS_OUT_OF_MEMORY) {
    PostDecoderError(NS_ERROR_OUT_OF_MEMORY);
    return;
  } else if (rv == VP8_STATUS_INVALID_PARAM ||
             rv == VP8_STATUS_BITSTREAM_ERROR) {
    PostDataError();
    return;
  } else if (rv == VP8_STATUS_UNSUPPORTED_FEATURE ||
             rv == VP8_STATUS_USER_ABORT) {
    PostDecoderError(NS_ERROR_FAILURE);
    return;
  }

  // Catch any remaining erroneous return value.
  if (rv != VP8_STATUS_OK && rv != VP8_STATUS_SUSPENDED) {
    PostDecoderError(NS_ERROR_FAILURE);
    return;
  }

  int lastLineRead = -1;
  int height = 0;
  int width = 0;
  int stride = 0;

  mData = WebPIDecGetRGB(mDecoder, &lastLineRead, &width, &height, &stride);

  if (lastLineRead == -1 || !mData)
    return;

  if (width <= 0 || height <= 0) {
    PostDataError();
    return;
  }

  if (!HasSize())
    PostSize(width, height);

  if (IsSizeDecode())
    return;

  // First incremental Image data chunk. Special handling required.
  if (mLastLine == 0 && lastLineRead > 0) {
    Decoder::NeedNewFrame(0, 0, 0, width, height,
                          gfx::SurfaceFormat::B8G8R8A8);
    Decoder::AllocateFrame();
  }

  if (!mImageData) {
    PostDecoderError(NS_ERROR_FAILURE);
    return;
  }

  // Transfer from mData to mImageData
  if (lastLineRead > mLastLine) {
    for (int line = mLastLine; line < lastLineRead; line++) {
      for (int pix = 0; pix < width; pix++) {
        // RGBA -> BGRA
        uint32_t DataOffset = 4 * (line * width + pix);
        mImageData[DataOffset+0] = mData[DataOffset+2];
        mImageData[DataOffset+1] = mData[DataOffset+1];
        mImageData[DataOffset+2] = mData[DataOffset+0];
        mImageData[DataOffset+3] = mData[DataOffset+3];
      }
    } 

    // Invalidate
    nsIntRect r(0, mLastLine, width, lastLineRead);
    PostInvalidation(r);
  }

  mLastLine = lastLineRead;
  return;
}

} // namespace imagelib
} // namespace mozilla

