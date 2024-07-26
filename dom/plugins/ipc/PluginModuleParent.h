/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: sw=4 ts=4 et :
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_plugins_PluginModuleParent_h
#define mozilla_plugins_PluginModuleParent_h

#include "base/process.h"
#include "mozilla/FileUtils.h"
#include "mozilla/HangAnnotations.h"
#include "mozilla/PluginLibrary.h"
#include "mozilla/plugins/ScopedMethodFactory.h"
#include "mozilla/plugins/PluginProcessParent.h"
#include "mozilla/plugins/PPluginModuleParent.h"
#include "mozilla/plugins/PluginMessageUtils.h"
#include "mozilla/plugins/PluginTypes.h"
#include "mozilla/TimeStamp.h"
#include "npapi.h"
#include "npfunctions.h"
#include "nsAutoPtr.h"
#include "nsDataHashtable.h"
#include "nsHashKeys.h"
#include "nsIObserver.h"

class nsPluginTag;

namespace mozilla {
namespace plugins {
//-----------------------------------------------------------------------------

class BrowserStreamParent;
class PluginAsyncSurrogate;
class PluginInstanceParent;

#ifdef XP_WIN
class PluginHangUIParent;
#endif

/**
 * PluginModuleParent
 *
 * This class implements the NPP API from the perspective of the rest
 * of Goanna, forwarding NPP calls along to the child process that is
 * actually running the plugin.
 *
 * This class /also/ implements a version of the NPN API, because the
 * child process needs to make these calls back into Goanna proper.
 * This class is responsible for "actually" making those function calls.
 *
 * If a plugin is running, there will always be one PluginModuleParent for it in
 * the chrome process. In addition, any content process using the plugin will
 * have its own PluginModuleParent. The subclasses PluginModuleChromeParent and
 * PluginModuleContentParent implement functionality that is specific to one
 * case or the other.
 */
class PluginModuleParent
    : public PPluginModuleParent
    , public PluginLibrary
{
protected:
    typedef mozilla::PluginLibrary PluginLibrary;

    PPluginInstanceParent*
    AllocPPluginInstanceParent(const nsCString& aMimeType,
                               const uint16_t& aMode,
                               const InfallibleTArray<nsCString>& aNames,
                               const InfallibleTArray<nsCString>& aValues)
                               override;

    virtual bool
    DeallocPPluginInstanceParent(PPluginInstanceParent* aActor) override;

public:
    explicit PluginModuleParent(bool aIsChrome);
    virtual ~PluginModuleParent();

    bool RemovePendingSurrogate(const nsRefPtr<PluginAsyncSurrogate>& aSurrogate);

    /** @return the state of the pref that controls async plugin init */
    bool IsStartingAsync() const { return mIsStartingAsync; }
    /** @return whether this modules NP_Initialize has successfully completed
        executing */
    bool IsInitialized() const { return mNPInitialized; }
    bool IsChrome() const { return mIsChrome; }

    virtual void SetPlugin(nsNPAPIPlugin* plugin) override
    {
        mPlugin = plugin;
    }

    virtual void ActorDestroy(ActorDestroyReason why) override;

    const NPNetscapeFuncs* GetNetscapeFuncs() {
        return mNPNIface;
    }

    bool OkToCleanup() const {
        return !IsOnCxxStack();
    }

    void ProcessRemoteNativeEventsInInterruptCall() override;

    virtual bool WaitForIPCConnection() { return true; }

    nsCString GetHistogramKey() const {
        return mPluginName + mPluginVersion;
    }

protected:
    virtual mozilla::ipc::RacyInterruptPolicy
    MediateInterruptRace(const Message& parent, const Message& child) override
    {
        return MediateRace(parent, child);
    }

    virtual bool
    RecvBackUpXResources(const FileDescriptor& aXSocketFd) override;

    virtual bool AnswerProcessSomeEvents() override;

    virtual bool
    RecvProcessNativeEventsInInterruptCall() override;

    virtual bool
    RecvPluginShowWindow(const uint32_t& aWindowId, const bool& aModal,
                         const int32_t& aX, const int32_t& aY,
                         const size_t& aWidth, const size_t& aHeight) override;

    virtual bool
    RecvPluginHideWindow(const uint32_t& aWindowId) override;

    virtual bool
    RecvSetCursor(const NSCursorInfo& aCursorInfo) override;

    virtual bool
    RecvShowCursor(const bool& aShow) override;

    virtual bool
    RecvPushCursor(const NSCursorInfo& aCursorInfo) override;

    virtual bool
    RecvPopCursor() override;

    virtual bool
    RecvNPN_SetException(const nsCString& aMessage) override;

    virtual bool
    RecvNPN_ReloadPlugins(const bool& aReloadPages) override;

    virtual bool
    RecvNP_InitializeResult(const NPError& aError) override;

    static BrowserStreamParent* StreamCast(NPP instance, NPStream* s,
                                           PluginAsyncSurrogate** aSurrogate = nullptr);

protected:
    void SetChildTimeout(const int32_t aChildTimeout);
    static void TimeoutChanged(const char* aPref, void* aModule);

    virtual void UpdatePluginTimeout() {}

    virtual bool RecvNotifyContentModuleDestroyed() override { return true; }

    void SetPluginFuncs(NPPluginFuncs* aFuncs);

    nsresult NPP_NewInternal(NPMIMEType pluginType, NPP instance, uint16_t mode,
                             InfallibleTArray<nsCString>& names,
                             InfallibleTArray<nsCString>& values,
                             NPSavedData* saved, NPError* error);

    // NPP-like API that Goanna calls are trampolined into.  These 
    // messages then get forwarded along to the plugin instance,
    // and then eventually the child process.

    static NPError NPP_Destroy(NPP instance, NPSavedData** save);

    static NPError NPP_SetWindow(NPP instance, NPWindow* window);
    static NPError NPP_NewStream(NPP instance, NPMIMEType type, NPStream* stream,
                                 NPBool seekable, uint16_t* stype);
    static NPError NPP_DestroyStream(NPP instance,
                                     NPStream* stream, NPReason reason);
    static int32_t NPP_WriteReady(NPP instance, NPStream* stream);
    static int32_t NPP_Write(NPP instance, NPStream* stream,
                             int32_t offset, int32_t len, void* buffer);
    static void NPP_StreamAsFile(NPP instance,
                                 NPStream* stream, const char* fname);
    static void NPP_Print(NPP instance, NPPrint* platformPrint);
    static int16_t NPP_HandleEvent(NPP instance, void* event);
    static void NPP_URLNotify(NPP instance, const char* url,
                              NPReason reason, void* notifyData);
    static NPError NPP_GetValue(NPP instance,
                                NPPVariable variable, void *ret_value);
    static NPError NPP_SetValue(NPP instance, NPNVariable variable,
                                void *value);
    static void NPP_URLRedirectNotify(NPP instance, const char* url,
                                      int32_t status, void* notifyData);

    virtual bool HasRequiredFunctions() override;
    virtual nsresult AsyncSetWindow(NPP aInstance, NPWindow* aWindow) override;
    virtual nsresult GetImageContainer(NPP aInstance, mozilla::layers::ImageContainer** aContainer) override;
    virtual nsresult GetImageSize(NPP aInstance, nsIntSize* aSize) override;
    virtual bool IsOOP() override { return true; }
    virtual nsresult SetBackgroundUnknown(NPP instance) override;
    virtual nsresult BeginUpdateBackground(NPP instance,
                                           const nsIntRect& aRect,
                                           gfxContext** aCtx) override;
    virtual nsresult EndUpdateBackground(NPP instance,
                                         gfxContext* aCtx,
                                         const nsIntRect& aRect) override;

#if defined(XP_UNIX) && !defined(XP_MACOSX) && !defined(MOZ_WIDGET_GONK)
    virtual nsresult NP_Initialize(NPNetscapeFuncs* bFuncs, NPPluginFuncs* pFuncs, NPError* error) override;
#else
    virtual nsresult NP_Initialize(NPNetscapeFuncs* bFuncs, NPError* error) override;
#endif
    virtual nsresult NP_Shutdown(NPError* error) override;

    virtual nsresult NP_GetMIMEDescription(const char** mimeDesc) override;
    virtual nsresult NP_GetValue(void *future, NPPVariable aVariable,
                                 void *aValue, NPError* error) override;
#if defined(XP_WIN) || defined(XP_MACOSX)
    virtual nsresult NP_GetEntryPoints(NPPluginFuncs* pFuncs, NPError* error) override;
#endif
    virtual nsresult NPP_New(NPMIMEType pluginType, NPP instance,
                             uint16_t mode, int16_t argc, char* argn[],
                             char* argv[], NPSavedData* saved,
                             NPError* error) override;
    virtual nsresult NPP_ClearSiteData(const char* site, uint64_t flags,
                                       uint64_t maxAge) override;
    virtual nsresult NPP_GetSitesWithData(InfallibleTArray<nsCString>& result) override;

#if defined(XP_MACOSX)
    virtual nsresult IsRemoteDrawingCoreAnimation(NPP instance, bool *aDrawing) override;
    virtual nsresult ContentsScaleFactorChanged(NPP instance, double aContentsScaleFactor) override;
#endif

    void InitAsyncSurrogates();

protected:
    void NotifyFlashHang();
    void NotifyPluginCrashed();
    void OnInitFailure();

    bool GetSetting(NPNVariable aVariable);
    void GetSettings(PluginSettings* aSettings);

    bool mIsChrome;
    bool mShutdown;
    bool mClearSiteDataSupported;
    bool mGetSitesWithDataSupported;
    NPNetscapeFuncs* mNPNIface;
    NPPluginFuncs* mNPPIface;
    nsNPAPIPlugin* mPlugin;
    ScopedMethodFactory<PluginModuleParent> mTaskFactory;
    nsString mPluginDumpID;
    nsString mBrowserDumpID;
    nsString mHangID;
    nsRefPtr<nsIObserver> mProfilerObserver;
    TimeDuration mTimeBlocked;
    nsCString mPluginName;
    nsCString mPluginVersion;
    bool mIsFlashPlugin;

#ifdef MOZ_X11
    // Dup of plugin's X socket, used to scope its resources to this
    // object instead of the plugin process's lifetime
    ScopedClose mPluginXSocketFdDup;
#endif

    bool
    GetPluginDetails();

    friend class mozilla::plugins::PluginAsyncSurrogate;

    bool              mIsStartingAsync;
    bool              mNPInitialized;
    nsTArray<nsRefPtr<PluginAsyncSurrogate>> mSurrogateInstances;
    nsresult          mAsyncNewRv;
};

class PluginModuleContentParent : public PluginModuleParent
{
  public:
    explicit PluginModuleContentParent();

    static PluginLibrary* LoadModule(uint32_t aPluginId);

    static PluginModuleContentParent* Initialize(mozilla::ipc::Transport* aTransport,
                                                 base::ProcessId aOtherProcess);

    static void OnLoadPluginResult(const uint32_t& aPluginId, const bool& aResult);
    static void AssociatePluginId(uint32_t aPluginId, base::ProcessId aProcessId);

    virtual ~PluginModuleContentParent();

#if defined(XP_WIN) || defined(XP_MACOSX)
    nsresult NP_Initialize(NPNetscapeFuncs* bFuncs, NPError* error) override;
#endif

  private:
    virtual bool ShouldContinueFromReplyTimeout() override;
    virtual void OnExitedSyncSend() override;

    static PluginModuleContentParent* sSavedModuleParent;

    uint32_t mPluginId;
};

class PluginModuleChromeParent
    : public PluginModuleParent
    , public mozilla::HangMonitor::Annotator
{
  public:
    /**
     * LoadModule
     *
     * This may or may not launch a plugin child process,
     * and may or may not be very expensive.
     */
    static PluginLibrary* LoadModule(const char* aFilePath, uint32_t aPluginId,
                                     nsPluginTag* aPluginTag);

    /**
     * The following two functions are called by SetupBridge to determine
     * whether an existing plugin module was reused, or whether a new module
     * was instantiated by the plugin host.
     */
    static void ClearInstantiationFlag() { sInstantiated = false; }
    static bool DidInstantiate() { return sInstantiated; }

    virtual ~PluginModuleChromeParent();

    void TerminateChildProcess(MessageLoop* aMsgLoop);

#ifdef XP_WIN
    /**
     * Called by Plugin Hang UI to notify that the user has clicked continue.
     * Used for chrome hang annotations.
     */
    void
    OnHangUIContinue();

    void
    EvaluateHangUIState(const bool aReset);
#endif // XP_WIN

    virtual bool WaitForIPCConnection() override;

    virtual bool
    RecvNP_InitializeResult(const NPError& aError) override;

    void
    SetContentParent(dom::ContentParent* aContentParent);

    bool
    SendAssociatePluginId();

    void CachedSettingChanged();

    void OnEnteredCall() override;
    void OnExitedCall() override;
    void OnEnteredSyncSend() override;
    void OnExitedSyncSend() override;

private:
    virtual void
    EnteredCxxStack() override;

    void
    ExitedCxxStack() override;

    mozilla::ipc::IProtocol* GetInvokingProtocol();
    PluginInstanceParent* GetManagingInstance(mozilla::ipc::IProtocol* aProtocol);

    virtual void
    AnnotateHang(mozilla::HangMonitor::HangAnnotations& aAnnotations) override;

    virtual bool ShouldContinueFromReplyTimeout() override;

    PluginProcessParent* Process() const { return mSubprocess; }
    base::ProcessHandle ChildProcessHandle() { return mSubprocess->GetChildProcessHandle(); }

#if defined(XP_UNIX) && !defined(XP_MACOSX) && !defined(MOZ_WIDGET_GONK)
    virtual nsresult NP_Initialize(NPNetscapeFuncs* bFuncs, NPPluginFuncs* pFuncs, NPError* error) override;
#else
    virtual nsresult NP_Initialize(NPNetscapeFuncs* bFuncs, NPError* error) override;
#endif

#if defined(XP_WIN) || defined(XP_MACOSX)
    virtual nsresult NP_GetEntryPoints(NPPluginFuncs* pFuncs, NPError* error) override;
#endif

    virtual void ActorDestroy(ActorDestroyReason why) override;

    // aFilePath is UTF8, not native!
    explicit PluginModuleChromeParent(const char* aFilePath, uint32_t aPluginId);

    void CleanupFromTimeout(const bool aByHangUI);

    virtual void UpdatePluginTimeout() override;

#ifdef MOZ_ENABLE_PROFILER_SPS
    void InitPluginProfiling();
    void ShutdownPluginProfiling();
#endif

    void RegisterSettingsCallbacks();
    void UnregisterSettingsCallbacks();

    virtual bool RecvNotifyContentModuleDestroyed() override;

    static void CachedSettingChanged(const char* aPref, void* aModule);

    PluginProcessParent* mSubprocess;
    uint32_t mPluginId;

    ScopedMethodFactory<PluginModuleChromeParent> mChromeTaskFactory;

    enum HangAnnotationFlags
    {
        kInPluginCall = (1u << 0),
        kHangUIShown = (1u << 1),
        kHangUIContinued = (1u << 2),
        kHangUIDontShow = (1u << 3)
    };
    Atomic<uint32_t> mHangAnnotationFlags;
    mozilla::Mutex mHangAnnotatorMutex;
    InfallibleTArray<mozilla::ipc::IProtocol*> mProtocolCallStack;
#ifdef XP_WIN
    InfallibleTArray<float> mPluginCpuUsageOnHang;
    PluginHangUIParent *mHangUIParent;
    bool mHangUIEnabled;
    bool mIsTimerReset;

    /**
     * Launches the Plugin Hang UI.
     *
     * @return true if plugin-hang-ui.exe has been successfully launched.
     *         false if the Plugin Hang UI is disabled, already showing,
     *               or the launch failed.
     */
    bool
    LaunchHangUI();

    /**
     * Finishes the Plugin Hang UI and cancels if it is being shown to the user.
     */
    void
    FinishHangUI();
#endif

    friend class mozilla::plugins::PluginAsyncSurrogate;

    void OnProcessLaunched(const bool aSucceeded);

    class LaunchedTask : public LaunchCompleteTask
    {
    public:
        explicit LaunchedTask(PluginModuleChromeParent* aModule)
            : mModule(aModule)
        {
            MOZ_ASSERT(aModule);
        }

        void Run() override
        {
            mModule->OnProcessLaunched(mLaunchSucceeded);
        }

    private:
        PluginModuleChromeParent* mModule;
    };

    friend class LaunchedTask;

    bool                mInitOnAsyncConnect;
    nsresult            mAsyncInitRv;
    NPError             mAsyncInitError;
    dom::ContentParent* mContentParent;
    nsCOMPtr<nsIObserver> mOfflineObserver;
    bool mIsBlocklisted;
    static bool sInstantiated;
};

} // namespace plugins
} // namespace mozilla

#endif // mozilla_plugins_PluginModuleParent_h
