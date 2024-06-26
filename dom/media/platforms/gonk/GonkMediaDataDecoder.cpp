/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "GonkMediaDataDecoder.h"
#include "VideoUtils.h"
#include "nsTArray.h"
#include "MediaCodecProxy.h"

#include <stagefright/foundation/ADebug.h>

#include "mozilla/Logging.h"
#include <android/log.h>
#define GMDD_LOG(...) __android_log_print(ANDROID_LOG_DEBUG, "GonkMediaDataDecoder", __VA_ARGS__)
#define INPUT_TIMEOUT_US 0LL // Don't wait for buffer if none is available.
#define MIN_QUEUED_SAMPLES 2

#ifdef DEBUG
#include <utils/AndroidThreads.h>
#endif

extern mozilla::LogModule* GetPDMLog();
#define LOG(...) MOZ_LOG(GetPDMLog(), mozilla::LogLevel::Debug, (__VA_ARGS__))

using namespace android;

namespace mozilla {

GonkDecoderManager::GonkDecoderManager(MediaTaskQueue* aTaskQueue)
  : mMonitor("GonkDecoderManager")
  , mTaskQueue(aTaskQueue)
{
}

bool
GonkDecoderManager::InitLoopers(MediaData::Type aType)
{
  MOZ_ASSERT(mDecodeLooper.get() == nullptr && mTaskLooper.get() == nullptr);
  MOZ_ASSERT(aType == MediaData::VIDEO_DATA || aType == MediaData::AUDIO_DATA);

  const char* suffix = (aType == MediaData::VIDEO_DATA ? "video" : "audio");
  mDecodeLooper = new ALooper;
  android::AString name("MediaCodecProxy/");
  name.append(suffix);
  mDecodeLooper->setName(name.c_str());

  mTaskLooper = new ALooper;
  name.setTo("GonkDecoderManager/");
  name.append(suffix);
  mTaskLooper->setName(name.c_str());
  mTaskLooper->registerHandler(this);

#ifdef DEBUG
  sp<AMessage> findThreadId(new AMessage(kNotifyFindLooperId, id()));
  findThreadId->post();
#endif

  return mDecodeLooper->start() == OK && mTaskLooper->start() == OK;
}

nsresult
GonkDecoderManager::Input(MediaRawData* aSample)
{
  RefPtr<MediaRawData> sample;

  if (aSample) {
    sample = aSample;
  } else {
    // It means EOS with empty sample.
    sample = new MediaRawData();
  }
  {
    MutexAutoLock lock(mMutex);
    mQueuedSamples.AppendElement(sample);
  }

  sp<AMessage> input = new AMessage(kNotifyProcessInput, id());
  if (!aSample) {
    input->setInt32("input-eos", 1);
  }
  input->post();
  return NS_OK;
}

int32_t
GonkDecoderManager::ProcessQueuedSamples()
{
  MOZ_ASSERT(OnTaskLooper());

  MutexAutoLock lock(mMutex);
  status_t rv;
  while (mQueuedSamples.Length()) {
    RefPtr<MediaRawData> data = mQueuedSamples.ElementAt(0);
    {
      rv = mDecoder->Input(reinterpret_cast<const uint8_t*>(data->Data()),
                           data->Size(),
                           data->mTime,
                           0,
                           INPUT_TIMEOUT_US);
    }
    if (rv == OK) {
      mQueuedSamples.RemoveElementAt(0);
    } else if (rv == -EAGAIN || rv == -ETIMEDOUT) {
      // In most cases, EAGAIN or ETIMEOUT are safe because OMX can't fill
      // buffer on time.
      break;
    } else {
      return rv;
    }
  }
  return mQueuedSamples.Length();
}

nsresult
GonkDecoderManager::Flush()
{
  if (mDecoder == nullptr) {
    GMDD_LOG("Decoder is not initialized");
    return NS_ERROR_UNEXPECTED;
  }
  {
    MutexAutoLock lock(mMutex);
    mQueuedSamples.Clear();
  }

  mLastTime = 0;

  MonitorAutoLock lock(mFlushMonitor);
  mIsFlushing = true;
  sp<AMessage> flush = new AMessage(kNotifyProcessFlush, id());
  flush->post();
  while (mIsFlushing) {
    lock.Wait();
  }
  return NS_OK;
}

nsresult
GonkDecoderManager::Input(MediaRawData* aSample)
{
  ReentrantMonitorAutoEnter mon(mMonitor);
  nsRefPtr<MediaRawData> sample;

  if (!aSample) {
    // It means EOS with empty sample.
    sample = new MediaRawData();
  } else {
    sample = aSample;
    if (!PerformFormatSpecificProcess(sample)) {
      return NS_ERROR_FAILURE;
    }
  }

  mQueueSample.AppendElement(sample);

  status_t rv;
  while (mQueueSample.Length()) {
    nsRefPtr<MediaRawData> data = mQueueSample.ElementAt(0);
    {
      ReentrantMonitorAutoExit mon_exit(mMonitor);
      rv = SendSampleToOMX(data);
    }
    if (rv == OK) {
      mQueueSample.RemoveElementAt(0);
    } else if (rv == -EAGAIN || rv == -ETIMEDOUT) {
      // In most cases, EAGAIN or ETIMEOUT are safe because OMX can't fill
      // buffer on time.
      return NS_OK;
    } else {
      return NS_ERROR_UNEXPECTED;
    }
  }

  return NS_OK;
}

nsresult
GonkMediaDataDecoder::Init()
{
  sp<MediaCodecProxy> decoder;
  decoder = mManager->Init(mCallback);
  mDecoder = decoder;
  mDrainComplete = false;

  return NS_OK;
}

nsresult
GonkDecoderManager::Shutdown()
{
  if (mDecoder.get()) {
    mDecoder->stop();
    mDecoder->ReleaseMediaResources();
    mDecoder = nullptr;
  }

  mInitPromise.RejectIfExists(DecoderFailureReason::CANCELED, __func__);

  return NS_OK;
}

bool
GonkDecoderManager::HasQueuedSample()
{
  MutexAutoLock lock(mMutex);
  return mQueuedSamples.Length();
}

void
GonkDecoderManager::ProcessInput(bool aEndOfStream)
{
  MOZ_ASSERT(OnTaskLooper());

  status_t rv = ProcessQueuedSamples();
  if (rv >= 0) {
    if (!aEndOfStream && rv <= MIN_QUEUED_SAMPLES) {
      mDecodeCallback->InputExhausted();
    }

    if (mToDo.get() == nullptr) {
      mToDo = new AMessage(kNotifyDecoderActivity, id());
      if (aEndOfStream) {
        mToDo->setInt32("input-eos", 1);
      }
      mDecoder->requestActivityNotification(mToDo);
    }
  } else {
    GMDD_LOG("input processed: error#%d", rv);
    mDecodeCallback->Error();
  }
}

void
GonkDecoderManager::ProcessFlush()
{
  MOZ_ASSERT(OnTaskLooper());

  mLastTime = INT64_MIN;
  MonitorAutoLock lock(mFlushMonitor);
  mWaitOutput.Clear();
  if (mDecoder->flush() != OK) {
    GMDD_LOG("flush error");
    mDecodeCallback->Error();
  }
  mIsFlushing = false;
  lock.NotifyAll();
}

// Use output timestamp to determine which output buffer is already returned
// and remove corresponding info, except for EOS, from the waiting list.
// This method handles the cases that audio decoder sends multiple output
// buffers for one input.
void
GonkDecoderManager::UpdateWaitingList(int64_t aForgetUpTo)
{
  MOZ_ASSERT(OnTaskLooper());

  size_t i;
  for (i = 0; i < mWaitOutput.Length(); i++) {
    const auto& item = mWaitOutput.ElementAt(i);
    if (item.mEOS || item.mTimestamp > aForgetUpTo) {
      break;
    }
  }
  if (i > 0) {
    mWaitOutput.RemoveElementsAt(0, i);
  }
}

void
GonkDecoderManager::ProcessToDo(bool aEndOfStream)
{
  MOZ_ASSERT(OnTaskLooper());

  MOZ_ASSERT(mToDo.get() != nullptr);
  mToDo.clear();

  if (HasQueuedSample()) {
    status_t pendingInput = ProcessQueuedSamples();
    if (pendingInput < 0) {
      mDecodeCallback->Error();
      return;
    }
    if (!aEndOfStream && pendingInput <= MIN_QUEUED_SAMPLES) {
      mDecodeCallback->InputExhausted();
    }
  }

  nsresult rv = NS_OK;
  while (mWaitOutput.Length() > 0) {
    nsRefPtr<MediaData> output;
    int64_t offset = mWaitOutput.ElementAt(0);
    rv = Output(offset, output);
    if (rv == NS_OK) {
      mWaitOutput.RemoveElementAt(0);
      mDecodeCallback->Output(output);
    } else if (rv == NS_ERROR_ABORT) {
      GMDD_LOG("eos output");
      mWaitOutput.RemoveElementAt(0);
      MOZ_ASSERT(mQueuedSamples.IsEmpty());
      MOZ_ASSERT(mWaitOutput.IsEmpty());
      // EOS
      if (output) {
        mDecodeCallback->Output(output);
      }
      mDecodeCallback->DrainComplete();
      return;
    } else if (rv == NS_ERROR_NOT_AVAILABLE) {
      break;
    } else {
      mDecodeCallback->Error();
      return;
    }
  }

  if (HasQueuedSample() || mWaitOutput.Length() > 0) {
    mToDo = new AMessage(kNotifyDecoderActivity, id());
    mDecoder->requestActivityNotification(mToDo);
  }
}

void
GonkDecoderManager::onMessageReceived(const sp<AMessage> &aMessage)
{
  switch (aMessage->what()) {
    case kNotifyProcessInput:
    {
      int32_t eos = 0;
      ProcessInput(aMessage->findInt32("input-eos", &eos) && eos);
      break;
    }
    case kNotifyProcessFlush:
    {
      ProcessFlush();
      break;
    }
    case kNotifyDecoderActivity:
    {
      int32_t eos = 0;
      ProcessToDo(aMessage->findInt32("input-eos", &eos) && eos);
      break;
    }
#ifdef DEBUG
    case kNotifyFindLooperId:
    {
      mTaskLooperId = androidGetThreadId();
      MOZ_ASSERT(mTaskLooperId);
      break;
    }
#endif
    default:
      {
        TRESPASS();
        break;
      }
  }
}

#ifdef DEBUG
bool
GonkDecoderManager::OnTaskLooper()
{
  return androidGetThreadId() == mTaskLooperId;
}
#endif

GonkMediaDataDecoder::GonkMediaDataDecoder(GonkDecoderManager* aManager,
                                           FlushableTaskQueue* aTaskQueue,
                                           MediaDataDecoderCallback* aCallback)
  : mTaskQueue(aTaskQueue)
  , mManager(aManager)
{
  MOZ_COUNT_CTOR(GonkMediaDataDecoder);
  mManager->SetDecodeCallback(aCallback);
}

GonkMediaDataDecoder::~GonkMediaDataDecoder()
{
  MOZ_COUNT_DTOR(GonkMediaDataDecoder);
}

RefPtr<MediaDataDecoder::InitPromise>
GonkMediaDataDecoder::Init()
{
  return mManager->Init();
}

nsresult
GonkMediaDataDecoder::Shutdown()
{
  nsresult rv = mManager->Shutdown();

  // Because codec allocated runnable and init promise is at reader TaskQueue,
  // so manager needs to be destroyed at reader TaskQueue to prevent racing.
  mManager = nullptr;
  return rv;
}

// Inserts data into the decoder's pipeline.
nsresult
GonkMediaDataDecoder::Input(MediaRawData* aSample)
{
  mManager->Input(aSample);
  return NS_OK;
}

nsresult
GonkMediaDataDecoder::Flush()
{
  // Flush the input task queue. This cancels all pending Decode() calls.
  // Note this blocks until the task queue finishes its current job, if
  // it's executing at all. Note the MP4Reader ignores all output while
  // flushing.
  mTaskQueue->Flush();
  return mManager->Flush();
}

nsresult
GonkMediaDataDecoder::Drain()
{
  mManager->Input(nullptr);
  return NS_OK;
}

} // namespace mozilla
