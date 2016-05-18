/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: set sw=4 ts=8 et tw=80 : 
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_CrashReporterParent_h
#define mozilla_dom_CrashReporterParent_h

#include "mozilla/dom/PCrashReporterParent.h"
#include "mozilla/dom/TabMessageUtils.h"
#include "nsIFile.h"

namespace mozilla {
namespace dom {
class ProcessReporter;

class CrashReporterParent :
    public PCrashReporterParent
{
public:
  CrashReporterParent();
  virtual ~CrashReporterParent();

  /* Initialize this reporter with data from the child process */
  void
    SetChildData(const NativeThreadId& id, const uint32_t& processType);

  /* Returns the ID of the child minidump.
     GeneratePairedMinidump or GenerateCrashReport must be called first.
  */
  const nsString& ChildDumpID() {
    return mChildDumpID;
  }

  void
  AnnotateCrashReport(const nsCString& key, const nsCString& data);

 protected:
  virtual void ActorDestroy(ActorDestroyReason aWhy) override;

  virtual bool
    RecvAnnotateCrashReport(const nsCString& key, const nsCString& data) override {
    AnnotateCrashReport(key, data);
    return true;
  }
  virtual bool
    RecvAppendAppNotes(const nsCString& data) override;
  virtual mozilla::ipc::IProtocol*
  CloneProtocol(Channel* aChannel,
                mozilla::ipc::ProtocolCloneContext *aCtx) override;

  nsCString mAppNotes;
  nsString mChildDumpID;
  NativeThreadId mMainThread;
  time_t mStartTime;
  uint32_t mProcessType;
  bool mInitialized;
};

} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_CrashReporterParent_h
