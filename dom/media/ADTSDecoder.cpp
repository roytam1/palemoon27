/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ADTSDecoder.h"
#include "ADTSDemuxer.h"
#include "MediaDecoderStateMachine.h"
#include "MediaFormatReader.h"
#include "PlatformDecoderModule.h"

namespace mozilla {

MediaDecoder*
ADTSDecoder::Clone()
{
  if (!IsEnabled())
    return nullptr;

  return new ADTSDecoder();
}

MediaDecoderStateMachine*
ADTSDecoder::CreateStateMachine()
{
  RefPtr<MediaDecoderReader> reader =
      new MediaFormatReader(this, new ADTSDemuxer(GetResource()));
  return new MediaDecoderStateMachine(this, reader);
}

/* static */ bool
ADTSDecoder::IsEnabled()
{
  PlatformDecoderModule::Init();

  nsRefPtr<PlatformDecoderModule> platform = PlatformDecoderModule::Create();
  return (platform && platform->SupportsMimeType(NS_LITERAL_CSTRING("audio/mp4a-latm")));
}

/* static */ bool
ADTSDecoder::CanHandleMediaType(const nsACString& aType,
                                const nsAString& aCodecs)
{
  if (aType.EqualsASCII("audio/aac") || aType.EqualsASCII("audio/aacp") ||
      aType.EqualsASCII("audio/x-aac")) {
    return IsEnabled() && (aCodecs.IsEmpty() || aCodecs.EqualsASCII("aac"));
  }

  return false;
}

} // namespace mozilla
