/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "InternalResponse.h"

#include "nsIDOMFile.h"

#include "mozilla/dom/InternalHeaders.h"
#include "nsStreamUtils.h"
#include "nsSerializationHelper.h"

namespace mozilla {
namespace dom {

InternalResponse::InternalResponse(uint16_t aStatus, const nsACString& aStatusText)
  : mType(ResponseType::Default)
  , mFinalURL(false)
  , mStatus(aStatus)
  , mStatusText(aStatusText)
  , mHeaders(new InternalHeaders(HeadersGuardEnum::Response))
{
}

// Headers are not copied since BasicResponse and CORSResponse both need custom
// header handling.  Body is not copied as it cannot be shared directly.
InternalResponse::InternalResponse(const InternalResponse& aOther)
  : mType(aOther.mType)
  , mTerminationReason(aOther.mTerminationReason)
  , mURL(aOther.mURL)
  , mFinalURL(aOther.mFinalURL)
  , mStatus(aOther.mStatus)
  , mStatusText(aOther.mStatusText)
  , mContentType(aOther.mContentType)
  , mSecurityInfo(aOther.mSecurityInfo)
{
}

already_AddRefed<InternalResponse>
InternalResponse::Clone()
{
  nsRefPtr<InternalResponse> clone = new InternalResponse(*this);
  clone->mHeaders = new InternalHeaders(*mHeaders);

  if (!mBody) {
    return clone.forget();
  }

  nsCOMPtr<nsIInputStream> clonedBody;
  nsCOMPtr<nsIInputStream> replacementBody;

  nsresult rv = NS_CloneInputStream(mBody, getter_AddRefs(clonedBody),
                                    getter_AddRefs(replacementBody));
  if (NS_WARN_IF(NS_FAILED(rv))) { return nullptr; }

  clone->mBody.swap(clonedBody);
  if (replacementBody) {
    mBody.swap(replacementBody);
  }

  return clone.forget();
}

// static
already_AddRefed<InternalResponse>
InternalResponse::BasicResponse(InternalResponse* aInner)
{
  MOZ_ASSERT(aInner);
  nsRefPtr<InternalResponse> basic = new InternalResponse(*aInner);
  basic->mType = ResponseType::Basic;
  basic->mHeaders = InternalHeaders::BasicHeaders(aInner->mHeaders);
  basic->mBody.swap(aInner->mBody);
  return basic.forget();
}

// static
already_AddRefed<InternalResponse>
InternalResponse::CORSResponse(InternalResponse* aInner)
{
  MOZ_ASSERT(aInner);
  nsRefPtr<InternalResponse> cors = new InternalResponse(*aInner);
  cors->mType = ResponseType::Cors;
  cors->mHeaders = InternalHeaders::CORSHeaders(aInner->mHeaders);
  cors->mBody.swap(aInner->mBody);
  return cors.forget();
}

void
InternalResponse::SetSecurityInfo(nsISupports* aSecurityInfo)
{
  MOZ_ASSERT(mSecurityInfo.IsEmpty(), "security info should only be set once");
  nsCOMPtr<nsISerializable> serializable = do_QueryInterface(aSecurityInfo);
  if (!serializable) {
    NS_WARNING("A non-serializable object was passed to InternalResponse::SetSecurityInfo");
    return;
  }
  NS_SerializeToString(serializable, mSecurityInfo);
}

void
InternalResponse::SetSecurityInfo(const nsCString& aSecurityInfo)
{
  MOZ_ASSERT(mSecurityInfo.IsEmpty(), "security info should only be set once");
  mSecurityInfo = aSecurityInfo;
}

} // namespace dom
} // namespace mozilla
