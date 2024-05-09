/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/TaskQueue.h"

#include "FFmpegRuntimeLinker.h"
#include "FFmpegAudioDecoder.h"
#include "TimeUnits.h"

#define MAX_CHANNELS 16

namespace mozilla
{

static int (*avcodec_decode_audio4)(AVCodecContext*,AVFrame*,
                         int*,const AVPacket*) = nullptr;
static void (*av_init_packet1)(AVPacket*) = nullptr;

FFmpegAudioDecoder<LIBAV_VER>::FFmpegAudioDecoder(
  FlushableTaskQueue* aTaskQueue, MediaDataDecoderCallback* aCallback,
  const AudioInfo& aConfig)
  : FFmpegDataDecoder(aTaskQueue, aCallback, GetCodecId(aConfig.mMimeType))
{
  MOZ_COUNT_CTOR(FFmpegAudioDecoder);
  // Use a new MediaByteBuffer as the object will be modified during initialization.
  mExtraData = new MediaByteBuffer;
  mExtraData->AppendElements(*aConfig.mCodecSpecificConfig);
}

RefPtr<MediaDataDecoder::InitPromise>
FFmpegAudioDecoder<LIBAV_VER>::Init()
{
  nsresult rv = InitDecoder();

  if(rv == NS_OK) {
    avcodec_decode_audio4 = (decltype(avcodec_decode_audio4))FFmpegRuntimeLinker::avc_ptr[_decode_audio4];
    av_init_packet1 = (decltype(av_init_packet1))FFmpegRuntimeLinker::avc_ptr[_init_packet];
  }

  return rv == NS_OK ? InitPromise::CreateAndResolve(TrackInfo::kAudioTrack, __func__)
                     : InitPromise::CreateAndReject(DecoderFailureReason::INIT_ERROR, __func__);
}

void
FFmpegAudioDecoder<LIBAV_VER>::InitCodecContext()
{
  MOZ_ASSERT(mCodecContext);
  // We do not want to set this value to 0 as FFmpeg by default will
  // use the number of cores, which with our mozlibavutil get_cpu_count
  // isn't implemented.
  mCodecContext->thread_count = 1;
  // FFmpeg takes this as a suggestion for what format to use for audio samples.
  uint32_t major, minor, micro;
  FFmpegRuntimeLinker::GetVersion(major, minor, micro);
  // LibAV 0.8 produces rubbish float interleaved samples, request 16 bits audio.
  mCodecContext->request_sample_fmt =
#ifdef MOZ_SAMPLE_TYPE_FLOAT32
    (major == 53) ? AV_SAMPLE_FMT_S16 : AV_SAMPLE_FMT_FLT;
#else
    AV_SAMPLE_FMT_S16;
#endif
}

static AlignedAudioBuffer
CopyAndPackAudio(AVFrame* aFrame, uint32_t aNumChannels, uint32_t aNumAFrames)
{
  MOZ_ASSERT(aNumChannels <= MAX_CHANNELS);

  AlignedAudioBuffer audio(aNumChannels * aNumAFrames);
  if (!audio) {
    return audio;
  }

  if (aFrame->format == AV_SAMPLE_FMT_FLT) {
#ifdef MOZ_SAMPLE_TYPE_FLOAT32
    // Audio data already packed. No need to do anything other than copy it
    // into a buffer we own.
    memcpy(audio.get(), aFrame->data[0],
           aNumChannels * aNumAFrames * sizeof(AudioDataValue));
#else
    // Audio data already packed. Need to convert from 32 bits Float to S16
    AudioDataValue* tmp = audio.get();
    float* data = reinterpret_cast<float**>(aFrame->data)[0];
    for (uint32_t frame = 0; frame < aNumAFrames; frame++) {
      for (uint32_t channel = 0; channel < aNumChannels; channel++) {
        *tmp++ = FloatToAudioSample<int16_t>(*data++);
      }
    }
#endif
  } else if (aFrame->format == AV_SAMPLE_FMT_FLTP) {
#ifdef MOZ_SAMPLE_TYPE_FLOAT32
    // Planar audio data. Pack it into something we can understand.
    AudioDataValue* tmp = audio.get();
    AudioDataValue** data = reinterpret_cast<AudioDataValue**>(aFrame->data);
    for (uint32_t frame = 0; frame < aNumAFrames; frame++) {
      for (uint32_t channel = 0; channel < aNumChannels; channel++) {
        *tmp++ = data[channel][frame];
      }
    }
#else
    // Planar audio data. Convert it from 32 bits Float to S16
    // and pack it into something we can understand.
    AudioDataValue* tmp = audio.get();
    float** data = reinterpret_cast<float**>(aFrame->data);
    for (uint32_t frame = 0; frame < aNumAFrames; frame++) {
      for (uint32_t channel = 0; channel < aNumChannels; channel++) {
        *tmp++ = FloatToAudioSample<int16_t>(data[channel][frame]);
      }
    }
#endif
  } else if (aFrame->format == AV_SAMPLE_FMT_S16) {
#ifdef MOZ_SAMPLE_TYPE_FLOAT32
    // Audio data already packed. Need to convert from S16 to 32 bits Float
    AudioDataValue* tmp = audio.get();
    int16_t* data = reinterpret_cast<int16_t**>(aFrame->data)[0];
    for (uint32_t frame = 0; frame < aNumAFrames; frame++) {
      for (uint32_t channel = 0; channel < aNumChannels; channel++) {
        *tmp++ = AudioSampleToFloat(*data++);
      }
    }
#else
    // Audio data already packed. No need to do anything other than copy it
    // into a buffer we own.
    memcpy(audio.get(), aFrame->data[0],
           aNumChannels * aNumAFrames * sizeof(AudioDataValue));
#endif
  } else if (aFrame->format == AV_SAMPLE_FMT_S16P) {
#ifdef MOZ_SAMPLE_TYPE_FLOAT32
    // Planar audio data. Convert it from S16 to 32 bits float
    // and pack it into something we can understand.
    AudioDataValue* tmp = audio.get();
    int16_t** data = reinterpret_cast<int16_t**>(aFrame->data);
    for (uint32_t frame = 0; frame < aNumAFrames; frame++) {
      for (uint32_t channel = 0; channel < aNumChannels; channel++) {
        *tmp++ = AudioSampleToFloat(data[channel][frame]);
      }
    }
#else
    // Planar audio data. Pack it into something we can understand.
    AudioDataValue* tmp = audio.get();
    AudioDataValue** data = reinterpret_cast<AudioDataValue**>(aFrame->data);
    for (uint32_t frame = 0; frame < aNumAFrames; frame++) {
      for (uint32_t channel = 0; channel < aNumChannels; channel++) {
        *tmp++ = data[channel][frame];
      }
    }
#endif
  }

  return audio;
}

void
FFmpegAudioDecoder<LIBAV_VER>::DecodePacket(MediaRawData* aSample)
{
  MOZ_ASSERT(mTaskQueue->IsCurrentThreadIn());
  AVPacket packet;
  av_init_packet1(&packet);

  packet.data = const_cast<uint8_t*>(aSample->Data());
  packet.size = aSample->Size();

  if (!PrepareFrame()) {
    NS_WARNING("FFmpeg audio decoder failed to allocate frame.");
    mCallback->Error();
    return;
  }

  int64_t samplePosition = aSample->mOffset;
  media::TimeUnit pts = media::TimeUnit::FromMicroseconds(aSample->mTime);

  while (packet.size > 0) {
    int decoded;
    int bytesConsumed =
      avcodec_decode_audio4(mCodecContext, mFrame, &decoded, &packet);

    if (bytesConsumed < 0) {
      NS_WARNING("FFmpeg audio decoder error.");
      mCallback->Error();
      return;
    }

    if (decoded) {
      uint32_t numChannels = mCodecContext->channels;
      uint32_t samplingRate = mCodecContext->sample_rate;

      AlignedAudioBuffer audio =
        CopyAndPackAudio(mFrame, numChannels, mFrame->nb_samples);

      media::TimeUnit duration =
        FramesToTimeUnit(mFrame->nb_samples, samplingRate);
      if (!audio || !duration.IsValid()) {
        NS_WARNING("Invalid count of accumulated audio samples");
        mCallback->Error();
        return;
      }

      RefPtr<AudioData> data = new AudioData(samplePosition,
                                             pts.ToMicroseconds(),
                                             duration.ToMicroseconds(),
                                             mFrame->nb_samples,
                                             Move(audio),
                                             numChannels,
                                             samplingRate);
      mCallback->Output(data);
      pts += duration;
      if (!pts.IsValid()) {
        NS_WARNING("Invalid count of accumulated audio samples");
        mCallback->Error();
        return;
      }
    }
    packet.data += bytesConsumed;
    packet.size -= bytesConsumed;
    samplePosition += bytesConsumed;
  }

  if (mTaskQueue->IsEmpty()) {
    mCallback->InputExhausted();
  }
}

nsresult
FFmpegAudioDecoder<LIBAV_VER>::Input(MediaRawData* aSample)
{
  nsCOMPtr<nsIRunnable> runnable(NS_NewRunnableMethodWithArg<RefPtr<MediaRawData>>(
    this, &FFmpegAudioDecoder::DecodePacket, RefPtr<MediaRawData>(aSample)));
  mTaskQueue->Dispatch(runnable.forget());
  return NS_OK;
}

void
FFmpegAudioDecoder<LIBAV_VER>::ProcessDrain()
{
  MOZ_ASSERT(mTaskQueue->IsCurrentThreadIn());
  ProcessFlush();
  mCallback->DrainComplete();
}

AVCodecID
FFmpegAudioDecoder<LIBAV_VER>::GetCodecId(const nsACString& aMimeType)
{
  if (aMimeType.EqualsLiteral("audio/mpeg")) {
    return AV_CODEC_ID_MP3;
  }

  if (aMimeType.EqualsLiteral("audio/mp4a-latm")) {
    return AV_CODEC_ID_AAC;
  }

  return AV_CODEC_ID_NONE;
}

FFmpegAudioDecoder<LIBAV_VER>::~FFmpegAudioDecoder()
{
  MOZ_COUNT_DTOR(FFmpegAudioDecoder);
}

} // namespace mozilla
