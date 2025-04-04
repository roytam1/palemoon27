/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/TaskQueue.h"

#include "nsThreadUtils.h"
#include "nsAutoPtr.h"
#include "ImageContainer.h"
#include "FFmpegRuntimeLinker.h"

#include "MediaInfo.h"

#include "FFmpegH264Decoder.h"
#include "FFmpegLog.h"
#include "mozilla/PodOperations.h"
#include "mozilla/Preferences.h"

#include "libavutil/pixfmt.h"
#if LIBAVCODEC_VERSION_MAJOR < 54
#define AVPixelFormat PixelFormat
#define AV_PIX_FMT_YUV420P PIX_FMT_YUV420P
#define AV_PIX_FMT_YUVJ420P PIX_FMT_YUVJ420P
#define AV_PIX_FMT_YUV444P PIX_FMT_YUV444P
#define AV_PIX_FMT_NONE PIX_FMT_NONE
#endif

typedef mozilla::layers::Image Image;
typedef mozilla::layers::PlanarYCbCrImage PlanarYCbCrImage;

namespace mozilla
{

/**
 * FFmpeg calls back to this function with a list of pixel formats it supports.
 * We choose a pixel format that we support and return it.
 * For now, we just look for YUV420P, YUVJ420P and YUV444 as those are the only
 * non-HW accelerated format supported by FFmpeg's H.264 and VP9 decoder.
 */

#if defined(XP_WIN)
static int (*avcodec_decode_video2)(AVCodecContext*,AVFrame*,
                         int*,const AVPacket*) = nullptr;
static void (*av_init_packet)(AVPacket*) = nullptr;
#endif

static AVPixelFormat
ChoosePixelFormat(AVCodecContext* aCodecContext, const AVPixelFormat* aFormats)
{
  FFMPEG_LOG("Choosing FFmpeg pixel format for video decoding.");
  for (; *aFormats > -1; aFormats++) {
    switch (*aFormats) {
      case AV_PIX_FMT_YUV444P:
        FFMPEG_LOG("Requesting pixel format YUV444P.");
        return AV_PIX_FMT_YUV444P;
      case AV_PIX_FMT_YUV420P:
        FFMPEG_LOG("Requesting pixel format YUV420P.");
        return AV_PIX_FMT_YUV420P;
      case AV_PIX_FMT_YUVJ420P:
        FFMPEG_LOG("Requesting pixel format YUVJ420P.");
        return AV_PIX_FMT_YUVJ420P;
      default:
        break;
    }
  }

  NS_WARNING("FFmpeg does not share any supported pixel formats.");
  return AV_PIX_FMT_NONE;
}

FFmpegH264Decoder<LIBAV_VER>::PtsCorrectionContext::PtsCorrectionContext()
  : mNumFaultyPts(0)
  , mNumFaultyDts(0)
  , mLastPts(INT64_MIN)
  , mLastDts(INT64_MIN)
{
}

int64_t
FFmpegH264Decoder<LIBAV_VER>::PtsCorrectionContext::GuessCorrectPts(int64_t aPts, int64_t aDts)
{
  int64_t pts = AV_NOPTS_VALUE;

  if (aDts != int64_t(AV_NOPTS_VALUE)) {
    mNumFaultyDts += aDts <= mLastDts;
    mLastDts = aDts;
  }
  if (aPts != int64_t(AV_NOPTS_VALUE)) {
    mNumFaultyPts += aPts <= mLastPts;
    mLastPts = aPts;
  }
  if ((mNumFaultyPts <= mNumFaultyDts || aDts == int64_t(AV_NOPTS_VALUE)) &&
      aPts != int64_t(AV_NOPTS_VALUE)) {
    pts = aPts;
  } else {
    pts = aDts;
  }
  return pts;
}

void
FFmpegH264Decoder<LIBAV_VER>::PtsCorrectionContext::Reset()
{
  mNumFaultyPts = 0;
  mNumFaultyDts = 0;
  mLastPts = INT64_MIN;
  mLastDts = INT64_MIN;
}

FFmpegH264Decoder<LIBAV_VER>::FFmpegH264Decoder(
  TaskQueue* aTaskQueue, MediaDataDecoderCallback* aCallback,
  const VideoInfo& aConfig,
  ImageContainer* aImageContainer)
  : FFmpegDataDecoder(aTaskQueue, aCallback, GetCodecId(aConfig.mMimeType))
  , mImageContainer(aImageContainer)
  , mInfo(aConfig)
  , mCodecParser(nullptr)
  , mLastInputDts(INT64_MIN)
{
  MOZ_COUNT_CTOR(FFmpegH264Decoder);
  // Use a new MediaByteBuffer as the object will be modified during initialization.
  mExtraData = new MediaByteBuffer;
  mExtraData->AppendElements(*aConfig.mExtraData);
}

RefPtr<MediaDataDecoder::InitPromise>
FFmpegH264Decoder<LIBAV_VER>::Init()
{
  if (NS_FAILED(InitDecoder())) {
    return InitPromise::CreateAndReject(DecoderFailureReason::INIT_ERROR, __func__);
  }

  avcodec_decode_video2 = (decltype(avcodec_decode_video2))FFmpegRuntimeLinker::avc_ptr[_decode_video2];
  av_init_packet = (decltype(av_init_packet))FFmpegRuntimeLinker::avc_ptr[_init_packet];

  return InitPromise::CreateAndResolve(TrackInfo::kVideoTrack, __func__);
}

void
FFmpegH264Decoder<LIBAV_VER>::InitCodecContext()
{
  mCodecContext->width = mInfo.mImage.width;
  mCodecContext->height = mInfo.mImage.height;

  // We use the same logic as libvpx in determining the number of threads to use
  // so that we end up behaving in the same fashion when using ffmpeg as
  // we would otherwise cause various crashes (see bug 1236167)
  int decode_threads = 1;
  if (mInfo.mDisplay.width >= 2048) {
    decode_threads = 8;
  } else if (mInfo.mDisplay.width >= 1024) {
    decode_threads = 4;
  } else if (mInfo.mDisplay.width >= 320) {
    decode_threads = 2;
  }

  decode_threads = std::min(decode_threads, PR_GetNumberOfProcessors() - 1);
  decode_threads = std::max(decode_threads, 1);
  mCodecContext->thread_count = decode_threads;
  if (decode_threads > 1) {
    mCodecContext->thread_type = FF_THREAD_SLICE | FF_THREAD_FRAME;
  }

  if(Preferences::GetBool("media.ffmpeg.skip_loop_filter", false)) {
    // Enable skipping loop filter and allow non spec compliant speedup tricks.
    mCodecContext->flags2 |= 1; //AV_CODEC_FLAG2_FAST - could not inline for unknown reason ^-^'
    mCodecContext->skip_loop_filter = AVDISCARD_ALL;
  }

  // FFmpeg will call back to this to negotiate a video pixel format.
  mCodecContext->get_format = ChoosePixelFormat;

  mCodecParser =
#if defined(XP_WIN)
           reinterpret_cast<AVCodecParserContext*(*)(int)>(FFmpegRuntimeLinker::avc_ptr[_parser_init])(mCodecID);
#else
           av_parser_init(mCodecID);
#endif
  if (mCodecParser) {
    mCodecParser->flags |= PARSER_FLAG_COMPLETE_FRAMES;
  }
}

FFmpegH264Decoder<LIBAV_VER>::DecodeResult
FFmpegH264Decoder<LIBAV_VER>::DoDecode(MediaRawData* aSample)
{
  uint8_t* inputData = const_cast<uint8_t*>(aSample->Data());
  size_t inputSize = aSample->Size();

#if LIBAVCODEC_VERSION_MAJOR >= 54
  if (inputSize && mCodecParser && (mCodecID == AV_CODEC_ID_VP8
#if LIBAVCODEC_VERSION_MAJOR >= 55
      || mCodecID == AV_CODEC_ID_VP9
#endif
      )) {
    bool gotFrame = false;
    while (inputSize) {
      uint8_t* data;
      int size;

      int len = 
#if defined(XP_WIN)
                reinterpret_cast<int(*)(AVCodecParserContext*,AVCodecContext*,uint8_t**,int*,
                                 const uint8_t*,int,int64_t,int64_t,int64_t)>
                                 (FFmpegRuntimeLinker::avc_ptr[_parser_parse2])(mCodecParser,
                                 mCodecContext, &data, &size,
                                 inputData, inputSize,
                                 aSample->mTime, aSample->mTimecode,
                                 aSample->mOffset);
#else
                av_parser_parse2(mCodecParser, mCodecContext, &data, &size,
                                 inputData, inputSize,
                                 aSample->mTime, aSample->mTimecode,
                                 aSample->mOffset);
#endif
      if (size_t(len) > inputSize) {
        return DecodeResult::DECODE_ERROR;
      }
      inputData += len;
      inputSize -= len;
      if (size) {
        switch (DoDecode(aSample, data, size)) {
          case DecodeResult::DECODE_ERROR:
            return DecodeResult::DECODE_ERROR;
          case DecodeResult::DECODE_FRAME:
            gotFrame = true;
            break;
          default:
            break;
        }
      }
    }
    return gotFrame ? DecodeResult::DECODE_FRAME : DecodeResult::DECODE_NO_FRAME;
  }
#endif
  return DoDecode(aSample, inputData, inputSize);
}

FFmpegH264Decoder<LIBAV_VER>::DecodeResult
FFmpegH264Decoder<LIBAV_VER>::DoDecode(MediaRawData* aSample,
                                       uint8_t* aData, int aSize)
{
  MOZ_ASSERT(mTaskQueue->IsCurrentThreadIn());

  AVPacket packet;
  av_init_packet(&packet);

  packet.data = aData;
  packet.size = aSize;
  packet.dts = mLastInputDts = aSample->mTimecode;
  packet.pts = aSample->mTime;
  packet.flags = aSample->mKeyframe ? AV_PKT_FLAG_KEY : 0;
  packet.pos = aSample->mOffset;

  // LibAV provides no API to retrieve the decoded sample's duration.
  // (FFmpeg >= 1.0 provides av_frame_get_pkt_duration)
  // As such we instead use a map using the dts as key that we will retrieve
  // later.
  // The map will have a typical size of 16 entry.
  mDurationMap.Insert(aSample->mTimecode, aSample->mDuration);

  if (!PrepareFrame()) {
    NS_WARNING("FFmpeg h264 decoder failed to allocate frame.");
    return DecodeResult::FATAL_ERROR;
  }

  // Required with old version of FFmpeg/LibAV
  mFrame->reordered_opaque = AV_NOPTS_VALUE;

  int decoded;
  int bytesConsumed =
    avcodec_decode_video2(mCodecContext, mFrame, &decoded, &packet);

  FFMPEG_LOG("DoDecodeFrame:decode_video: rv=%d decoded=%d "
             "(Input: pts(%lld) dts(%lld) Output: pts(%lld) "
             "opaque(%lld) pkt_pts(%lld) pkt_dts(%lld))",
             bytesConsumed, decoded, packet.pts, packet.dts, mFrame->pts,
             mFrame->reordered_opaque, mFrame->pkt_pts, mFrame->pkt_dts);

  if (bytesConsumed < 0) {
    NS_WARNING("FFmpeg video decoder error.");
    return DecodeResult::DECODE_ERROR;
  }

  // If we've decoded a frame then we need to output it
  if (decoded) {
    int64_t pts = mPtsContext.GuessCorrectPts(mFrame->pkt_pts, mFrame->pkt_dts);
    // Retrieve duration from dts.
    // We use the first entry found matching this dts (this is done to
    // handle damaged file with multiple frames with the same dts)

    int64_t duration;
    if (!mDurationMap.Find(mFrame->pkt_dts, duration)) {
      NS_WARNING("Unable to retrieve duration from map");
      duration = aSample->mDuration;
      // dts are probably incorrectly reported ; so clear the map as we're
      // unlikely to find them in the future anyway. This also guards
      // against the map becoming extremely big.
      mDurationMap.Clear();
    }
    FFMPEG_LOG("Got one frame output with pts=%lld dts=%lld duration=%lld opaque=%lld",
               pts, mFrame->pkt_dts, duration, mCodecContext->reordered_opaque);

    VideoData::YCbCrBuffer b;
    b.mPlanes[0].mData = mFrame->data[0];
    b.mPlanes[1].mData = mFrame->data[1];
    b.mPlanes[2].mData = mFrame->data[2];

    b.mPlanes[0].mStride = mFrame->linesize[0];
    b.mPlanes[1].mStride = mFrame->linesize[1];
    b.mPlanes[2].mStride = mFrame->linesize[2];

    b.mPlanes[0].mOffset = b.mPlanes[0].mSkip = 0;
    b.mPlanes[1].mOffset = b.mPlanes[1].mSkip = 0;
    b.mPlanes[2].mOffset = b.mPlanes[2].mSkip = 0;

    b.mPlanes[0].mWidth = mFrame->width;
    b.mPlanes[0].mHeight = mFrame->height;
    if (mCodecContext->pix_fmt == AV_PIX_FMT_YUV444P) {
      b.mPlanes[1].mWidth = b.mPlanes[2].mWidth = mFrame->width;
      b.mPlanes[1].mHeight = b.mPlanes[2].mHeight = mFrame->height;
    } else {
      b.mPlanes[1].mWidth = b.mPlanes[2].mWidth = (mFrame->width + 1) >> 1;
      b.mPlanes[1].mHeight = b.mPlanes[2].mHeight = (mFrame->height + 1) >> 1;
    }

    RefPtr<VideoData> v = VideoData::Create(mInfo,
                                            mImageContainer,
                                            aSample->mOffset,
                                            pts,
                                            duration,
                                            b,
                                            !!mFrame->key_frame,
                                            -1,
                                            mInfo.ScaledImageRect(mFrame->width,
                                                                  mFrame->height));

    if (!v) {
      NS_WARNING("image allocation error.");
      return DecodeResult::FATAL_ERROR;
    }
    mCallback->Output(v);
    return DecodeResult::DECODE_FRAME;
  }
  return DecodeResult::DECODE_NO_FRAME;
}

void
FFmpegH264Decoder<LIBAV_VER>::ProcessDrain()
{
  RefPtr<MediaRawData> empty(new MediaRawData());
  empty->mTimecode = mLastInputDts;
  while (DoDecode(empty) == DecodeResult::DECODE_FRAME) {
  }
  mCallback->DrainComplete();
}

void
FFmpegH264Decoder<LIBAV_VER>::ProcessFlush()
{
  mPtsContext.Reset();
  mDurationMap.Clear();
  FFmpegDataDecoder::ProcessFlush();
}

FFmpegH264Decoder<LIBAV_VER>::~FFmpegH264Decoder()
{
  MOZ_COUNT_DTOR(FFmpegH264Decoder);
  if (mCodecParser) {
#if defined(XP_WIN)
    reinterpret_cast<void(*)(AVCodecParserContext*)>(FFmpegRuntimeLinker::avc_ptr[_parser_close])(mCodecParser);
#else
    av_parser_close(mCodecParser);
#endif
    mCodecParser = nullptr;
  }
}

AVCodecID
FFmpegH264Decoder<LIBAV_VER>::GetCodecId(const nsACString& aMimeType)
{
  if (aMimeType.EqualsLiteral("video/avc") || aMimeType.EqualsLiteral("video/mp4")) {
    return AV_CODEC_ID_H264;
  }

  if (aMimeType.EqualsLiteral("video/x-vnd.on2.vp6")) {
    return AV_CODEC_ID_VP6F;
  }

#if LIBAVCODEC_VERSION_MAJOR >= 54
  if (aMimeType.EqualsLiteral("video/webm; codecs=vp8")) {
    return AV_CODEC_ID_VP8;
  }
#endif

  return AV_CODEC_ID_NONE;
}

} // namespace mozilla
