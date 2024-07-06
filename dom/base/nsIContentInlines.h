/* -*- Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil; tab-width: 8; -*- */
/* vim: set sw=2 sts=2 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsIContentInlines_h
#define nsIContentInlines_h

#include "nsIContent.h"
#include "nsIDocument.h"
#include "nsContentUtils.h"
#include "nsIFrame.h"

inline bool
nsIContent::IsInHTMLDocument() const
{
  return OwnerDoc()->IsHTML();
}

inline void
nsIContent::SetPrimaryFrame(nsIFrame* aFrame)
{
  MOZ_ASSERT(IsInUncomposedDoc() || IsInShadowTree(), "This will end badly!");
  NS_PRECONDITION(!aFrame || !mPrimaryFrame || aFrame == mPrimaryFrame,
                  "Losing track of existing primary frame");

  if (aFrame) {
    aFrame->SetIsPrimaryFrame(true);
  } else if (nsIFrame* currentPrimaryFrame = GetPrimaryFrame()) {
    currentPrimaryFrame->SetIsPrimaryFrame(false);
  }

  mPrimaryFrame = aFrame;
}

#endif // nsIContentInlines_h
