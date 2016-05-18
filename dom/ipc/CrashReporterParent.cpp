/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: set sw=4 ts=8 et tw=80 : 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "CrashReporterParent.h"
#include "mozilla/dom/ContentParent.h"
#include "nsXULAppAPI.h"
#include <time.h>

#include "mozilla/Telemetry.h"

namespace mozilla {
namespace dom {

void
CrashReporterParent::AnnotateCrashReport(const nsCString& key,
                                         const nsCString& data)
{
}

void
CrashReporterParent::ActorDestroy(ActorDestroyReason aWhy)
{
  // Implement me! Bug 1005155
}

bool
CrashReporterParent::RecvAppendAppNotes(const nsCString& data)
{
    mAppNotes.Append(data);
    return true;
}

mozilla::ipc::IProtocol*
CrashReporterParent::CloneProtocol(Channel* aChannel,
                                   mozilla::ipc::ProtocolCloneContext* aCtx)
{
    MOZ_CRASH("Not Implemented");
    return nullptr;
}

CrashReporterParent::CrashReporterParent()
    :
      mStartTime(::time(nullptr))
    , mInitialized(false)
{
    MOZ_COUNT_CTOR(CrashReporterParent);
}

CrashReporterParent::~CrashReporterParent()
{
    MOZ_COUNT_DTOR(CrashReporterParent);
}

void
CrashReporterParent::SetChildData(const NativeThreadId& tid,
                                  const uint32_t& processType)
{
    mInitialized = true;
    mMainThread = tid;
    mProcessType = processType;
}

} // namespace dom
} // namespace mozilla
