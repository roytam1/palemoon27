/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WMFMediaDataDecoder.h"
#include "VideoUtils.h"
#include "WMFUtils.h"
#include "nsTArray.h"

#include "mozilla/Logging.h"
#include "mozilla/SyncRunnable.h"

#define LOG(...) MOZ_LOG(sPDMLog, mozilla::LogLevel::Debug, (__VA_ARGS__))

namespace mozilla {

WMFMediaDataDecoder::WMFMediaDataDecoder(MFTManager* aMFTManager,
                                         TaskQueue* aTaskQueue,
                                         MediaDataDecoderCallback* aCallback)
  : mTaskQueue(aTaskQueue)
  , mCallback(aCallback)
  , mMFTManager(aMFTManager)
  , mIsFlushing(false)
  , mIsShutDown(false)
{
}

WMFMediaDataDecoder::~WMFMediaDataDecoder()
{
}

RefPtr<MediaDataDecoder::InitPromise>
WMFMediaDataDecoder::Init()
{
  MOZ_ASSERT(!mIsShutDown);
  return InitPromise::CreateAndResolve(mMFTManager->GetType(), __func__);
}

nsresult
WMFMediaDataDecoder::Shutdown()
{
  MOZ_DIAGNOSTIC_ASSERT(!mIsShutDown);

  if (mTaskQueue) {
    mTaskQueue->Dispatch(NewRunnableMethod(this, &WMFMediaDataDecoder::ProcessShutdown));
  } else {
    ProcessShutdown();
  }
  mIsShutDown = true;
  return NS_OK;
}

void
WMFMediaDataDecoder::ProcessShutdown()
{
  if (mMFTManager) {
    mMFTManager->Shutdown();
    mMFTManager = nullptr;
    if (!mRecordedError && mHasSuccessfulOutput) {
      //SendTelemetry(S_OK);
    }
  }
}

// Inserts data into the decoder's pipeline.
nsresult
WMFMediaDataDecoder::Input(MediaRawData* aSample)
{
  MOZ_ASSERT(mCallback->OnReaderTaskQueue());
  MOZ_DIAGNOSTIC_ASSERT(!mIsShutDown);

  nsCOMPtr<nsIRunnable> runnable =
    NewRunnableMethod<RefPtr<MediaRawData>>(
      this,
      &WMFMediaDataDecoder::ProcessDecode,
      RefPtr<MediaRawData>(aSample));
  mTaskQueue->Dispatch(runnable.forget());
  return NS_OK;
}

void
WMFMediaDataDecoder::ProcessDecode(MediaRawData* aSample)
{
  if (mIsFlushing) {
    // Skip sample, to be released by runnable.
    return;
  }

  HRESULT hr = mMFTManager->Input(aSample);
  if (FAILED(hr)) {
    NS_WARNING("MFTManager rejected sample");
    mCallback->Error(MediaDataDecoderError::DECODE_ERROR);
    if (!mRecordedError) {
      //SendTelemetry(hr);
      mRecordedError = true;
    }
    return;
  }

  mLastStreamOffset = aSample->mOffset;

  ProcessOutput();
}

void
WMFMediaDataDecoder::ProcessOutput()
{
  RefPtr<MediaData> output;
  HRESULT hr = S_OK;
  while (SUCCEEDED(hr = mMFTManager->Output(mLastStreamOffset, output)) &&
         output) {
    mHasSuccessfulOutput = true;
    mCallback->Output(output);
  }
  if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
    if (mTaskQueue->IsEmpty()) {
      mCallback->InputExhausted();
    }
  } else if (FAILED(hr)) {
    NS_WARNING("WMFMediaDataDecoder failed to output data");
    mCallback->Error(MediaDataDecoderError::DECODE_ERROR);
    if (!mRecordedError) {
      //SendTelemetry(hr);
      mRecordedError = true;
    }
  }
}

void
WMFMediaDataDecoder::ProcessFlush()
{
  if (mMFTManager) {
    mMFTManager->Flush();
  }
}

nsresult
WMFMediaDataDecoder::Flush()
{
  MOZ_ASSERT(mCallback->OnReaderTaskQueue());
  MOZ_DIAGNOSTIC_ASSERT(!mIsShutDown);

  mIsFlushing = true;
  nsCOMPtr<nsIRunnable> runnable =
    NewRunnableMethod(this, &WMFMediaDataDecoder::ProcessFlush);
  SyncRunnable::DispatchToThread(mTaskQueue, runnable);
  mIsFlushing = false;
  return NS_OK;
}

void
WMFMediaDataDecoder::ProcessDrain()
{
  if (!mIsFlushing && mMFTManager) {
    // Order the decoder to drain...
    mMFTManager->Drain();
    // Then extract all available output.
    ProcessOutput();
  }
  mCallback->DrainComplete();
}

nsresult
WMFMediaDataDecoder::Drain()
{
  MOZ_ASSERT(mCallback->OnReaderTaskQueue());
  MOZ_DIAGNOSTIC_ASSERT(!mIsShutDown);

  mTaskQueue->Dispatch(NewRunnableMethod(this, &WMFMediaDataDecoder::ProcessDrain));
  return NS_OK;
}

bool
WMFMediaDataDecoder::IsHardwareAccelerated(nsACString& aFailureReason) const {
  MOZ_ASSERT(!mIsShutDown);

  return mMFTManager && mMFTManager->IsHardwareAccelerated(aFailureReason);
}

nsresult
WMFMediaDataDecoder::ConfigurationChanged(const TrackInfo& aConfig)
{
  MOZ_ASSERT(mCallback->OnReaderTaskQueue());

  nsCOMPtr<nsIRunnable> runnable =
    NewRunnableMethod<UniquePtr<TrackInfo>&&>(
    this,
    &WMFMediaDataDecoder::ProcessConfigurationChanged,
    aConfig.Clone());
  mTaskQueue->Dispatch(runnable.forget());
  return NS_OK;

}

void
WMFMediaDataDecoder::ProcessConfigurationChanged(UniquePtr<TrackInfo>&& aConfig)
{
  if (mMFTManager) {
    mMFTManager->ConfigurationChanged(*aConfig);
  }
}

} // namespace mozilla
