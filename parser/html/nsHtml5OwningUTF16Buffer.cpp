/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsHtml5OwningUTF16Buffer.h"

nsHtml5OwningUTF16Buffer::nsHtml5OwningUTF16Buffer(char16_t* aBuffer)
  : nsHtml5UTF16Buffer(aBuffer, 0),
    next(nullptr),
    key(nullptr)
{
  MOZ_COUNT_CTOR(nsHtml5OwningUTF16Buffer);
}

nsHtml5OwningUTF16Buffer::nsHtml5OwningUTF16Buffer(void* aKey)
  : nsHtml5UTF16Buffer(nullptr, 0),
    next(nullptr),
    key(aKey)
{
  MOZ_COUNT_CTOR(nsHtml5OwningUTF16Buffer);
}

nsHtml5OwningUTF16Buffer::~nsHtml5OwningUTF16Buffer()
{
  MOZ_COUNT_DTOR(nsHtml5OwningUTF16Buffer);
  DeleteBuffer();

  // This is to avoid dtor recursion on 'next', bug 706932.
  RefPtr<nsHtml5OwningUTF16Buffer> tail;
  tail.swap(next);
  while (tail && tail->mRefCnt == 1) {
    RefPtr<nsHtml5OwningUTF16Buffer> tmp;
    tmp.swap(tail->next);
    tail.swap(tmp);
  }
}

// static
already_AddRefed<nsHtml5OwningUTF16Buffer>
nsHtml5OwningUTF16Buffer::FalliblyCreate(int32_t aLength)
{
  char16_t* newBuf = new (mozilla::fallible) char16_t[aLength];
  if (!newBuf) {
    return nullptr;
  }
  RefPtr<nsHtml5OwningUTF16Buffer> newObj =
    new (mozilla::fallible) nsHtml5OwningUTF16Buffer(newBuf);
  if (!newObj) {
    delete[] newBuf;
    return nullptr;
  }
  return newObj.forget();
}

void
nsHtml5OwningUTF16Buffer::Swap(nsHtml5OwningUTF16Buffer* aOther)
{
  nsHtml5UTF16Buffer::Swap(aOther);
}

