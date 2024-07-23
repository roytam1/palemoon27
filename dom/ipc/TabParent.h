/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set sw=4 ts=8 et tw=80 : */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_tabs_TabParent_h
#define mozilla_tabs_TabParent_h

#include "mozilla/EventForwards.h"
#include "mozilla/dom/PBrowserParent.h"
#include "mozilla/dom/PFilePickerParent.h"
#include "mozilla/dom/TabContext.h"
#include "mozilla/dom/ipc/IdType.h"
#include "nsCOMPtr.h"
#include "nsIAuthPromptProvider.h"
#include "nsIBrowserDOMWindow.h"
#include "nsISecureBrowserUI.h"
#include "nsITabParent.h"
#include "nsIXULBrowserWindow.h"
#include "nsWeakReference.h"
#include "Units.h"
#include "WritingModes.h"
#include "js/TypeDecls.h"
#include "TabMessageUtils.h"

class nsFrameLoader;
class nsIFrameLoader;
class nsIContent;
class nsIPrincipal;
class nsIURI;
class nsIWidget;
class nsILoadContext;
class nsIDocShell;

namespace mozilla {

namespace jsipc {
class CpowHolder;
}

namespace layers {
struct FrameMetrics;
struct TextureFactoryIdentifier;
}

namespace layout {
class RenderFrameParent;
}

namespace widget {
struct IMENotification;
}

namespace dom {

class ClonedMessageData;
class nsIContentParent;
class Element;
struct StructuredCloneData;

class TabParent : public PBrowserParent 
                , public nsITabParent 
                , public nsIAuthPromptProvider
                , public nsISecureBrowserUI
                , public nsSupportsWeakReference
                , public TabContext
{
    typedef mozilla::dom::ClonedMessageData ClonedMessageData;

    virtual ~TabParent();

public:
    // nsITabParent
    NS_DECL_NSITABPARENT

    TabParent(nsIContentParent* aManager,
              const TabId& aTabId,
              const TabContext& aContext,
              uint32_t aChromeFlags);
    Element* GetOwnerElement() const { return mFrameElement; }
    void SetOwnerElement(Element* aElement);

    /**
     * Get the mozapptype attribute from this TabParent's owner DOM element.
     */
    void GetAppType(nsAString& aOut);

    /**
     * Returns true iff this TabParent's nsIFrameLoader is visible.
     *
     * The frameloader's visibility can be independent of e.g. its docshell's
     * visibility.
     */
    bool IsVisible();

    nsIBrowserDOMWindow *GetBrowserDOMWindow() { return mBrowserDOMWindow; }
    void SetBrowserDOMWindow(nsIBrowserDOMWindow* aBrowserDOMWindow) {
        mBrowserDOMWindow = aBrowserDOMWindow;
    }

    already_AddRefed<nsILoadContext> GetLoadContext();

    nsIXULBrowserWindow* GetXULBrowserWindow();

    void Destroy();

    virtual bool RecvMoveFocus(const bool& aForward) override;
    virtual bool RecvEvent(const RemoteDOMEvent& aEvent) override;
    virtual bool RecvReplyKeyEvent(const WidgetKeyboardEvent& aEvent) override;
    virtual bool RecvDispatchAfterKeyboardEvent(const WidgetKeyboardEvent& aEvent) override;
    virtual bool RecvBrowserFrameOpenWindow(PBrowserParent* aOpener,
                                            const nsString& aURL,
                                            const nsString& aName,
                                            const nsString& aFeatures,
                                            bool* aOutWindowOpened) override;
    virtual bool RecvCreateWindow(PBrowserParent* aOpener,
                                  const uint32_t& aChromeFlags,
                                  const bool& aCalledFromJS,
                                  const bool& aPositionSpecified,
                                  const bool& aSizeSpecified,
                                  const nsString& aURI,
                                  const nsString& aName,
                                  const nsString& aFeatures,
                                  const nsString& aBaseURI,
                                  bool* aWindowIsNew,
                                  InfallibleTArray<FrameScriptInfo>* aFrameScripts,
                                  nsCString* aURLToLoad) override;
    virtual bool RecvSyncMessage(const nsString& aMessage,
                                 const ClonedMessageData& aData,
                                 InfallibleTArray<CpowEntry>&& aCpows,
                                 const IPC::Principal& aPrincipal,
                                 InfallibleTArray<nsString>* aJSONRetVal) override;
    virtual bool RecvRpcMessage(const nsString& aMessage,
                                const ClonedMessageData& aData,
                                InfallibleTArray<CpowEntry>&& aCpows,
                                const IPC::Principal& aPrincipal,
                                InfallibleTArray<nsString>* aJSONRetVal) override;
    virtual bool RecvAsyncMessage(const nsString& aMessage,
                                  const ClonedMessageData& aData,
                                  InfallibleTArray<CpowEntry>&& aCpows,
                                  const IPC::Principal& aPrincipal) override;
    virtual bool RecvNotifyIMEFocus(const bool& aFocus,
                                    nsIMEUpdatePreference* aPreference,
                                    uint32_t* aSeqno) override;
    virtual bool RecvNotifyIMETextChange(const uint32_t& aStart,
                                         const uint32_t& aEnd,
                                         const uint32_t& aNewEnd,
                                         const bool& aCausedByComposition) override;
    virtual bool RecvNotifyIMESelectedCompositionRect(
                   const uint32_t& aOffset,
                   InfallibleTArray<LayoutDeviceIntRect>&& aRects,
                   const uint32_t& aCaretOffset,
                   const LayoutDeviceIntRect& aCaretRect) override;
    virtual bool RecvNotifyIMESelection(const uint32_t& aSeqno,
                                        const uint32_t& aAnchor,
                                        const uint32_t& aFocus,
                                        const mozilla::WritingMode& aWritingMode,
                                        const bool& aCausedByComposition) override;
    virtual bool RecvNotifyIMETextHint(const nsString& aText) override;
    virtual bool RecvNotifyIMEMouseButtonEvent(const widget::IMENotification& aEventMessage,
                                               bool* aConsumedByIME) override;
    virtual bool RecvNotifyIMEEditorRect(const LayoutDeviceIntRect& aRect) override;
    virtual bool RecvNotifyIMEPositionChange(
                   const LayoutDeviceIntRect& aEditorRect,
                   InfallibleTArray<LayoutDeviceIntRect>&& aCompositionRects,
                   const LayoutDeviceIntRect& aCaretRect) override;
    virtual bool RecvEndIMEComposition(const bool& aCancel,
                                       bool* aNoCompositionEvent,
                                       nsString* aComposition) override;
    virtual bool RecvStartPluginIME(const WidgetKeyboardEvent& aKeyboardEvent,
                                    const int32_t& aPanelX,
                                    const int32_t& aPanelY,
                                    nsString* aCommitted) override;
    virtual bool RecvSetPluginFocused(const bool& aFocused) override;
    virtual bool RecvGetInputContext(int32_t* aIMEEnabled,
                                     int32_t* aIMEOpen,
                                     intptr_t* aNativeIMEContext) override;
    virtual bool RecvSetInputContext(const int32_t& aIMEEnabled,
                                     const int32_t& aIMEOpen,
                                     const nsString& aType,
                                     const nsString& aInputmode,
                                     const nsString& aActionHint,
                                     const int32_t& aCause,
                                     const int32_t& aFocusChange) override;
    virtual bool RecvRequestFocus(const bool& aCanRaise) override;
    virtual bool RecvEnableDisableCommands(const nsString& aAction,
                                           nsTArray<nsCString>&& aEnabledCommands,
                                           nsTArray<nsCString>&& aDisabledCommands) override;
    virtual bool RecvSetCursor(const uint32_t& aValue, const bool& aForce) override;
    virtual bool RecvSetBackgroundColor(const nscolor& aValue) override;
    virtual bool RecvSetStatus(const uint32_t& aType, const nsString& aStatus) override;
    virtual bool RecvIsParentWindowMainWidgetVisible(bool* aIsVisible) override;
    virtual bool RecvShowTooltip(const uint32_t& aX, const uint32_t& aY, const nsString& aTooltip) override;
    virtual bool RecvHideTooltip() override;
    virtual bool RecvGetDPI(float* aValue) override;
    virtual bool RecvGetDefaultScale(double* aValue) override;
    virtual bool RecvGetWidgetNativeData(WindowsHandle* aValue) override;
    virtual bool RecvZoomToRect(const uint32_t& aPresShellId,
                                const ViewID& aViewId,
                                const CSSRect& aRect) override;
    virtual bool RecvUpdateZoomConstraints(const uint32_t& aPresShellId,
                                           const ViewID& aViewId,
                                           const bool& aIsRoot,
                                           const ZoomConstraints& aConstraints) override;
    virtual bool RecvContentReceivedInputBlock(const ScrollableLayerGuid& aGuid,
                                               const uint64_t& aInputBlockId,
                                               const bool& aPreventDefault) override;
    virtual bool RecvSetTargetAPZC(const uint64_t& aInputBlockId,
                                   nsTArray<ScrollableLayerGuid>&& aTargets) override;
    virtual bool RecvSynthesizedMouseWheelEvent(const mozilla::WidgetWheelEvent& aEvent) override;

    virtual PColorPickerParent*
    AllocPColorPickerParent(const nsString& aTitle, const nsString& aInitialColor) override;
    virtual bool DeallocPColorPickerParent(PColorPickerParent* aColorPicker) override;

    void LoadURL(nsIURI* aURI);
    // XXX/cjones: it's not clear what we gain by hiding these
    // message-sending functions under a layer of indirection and
    // eating the return values
    void Show(const ScreenIntSize& size, bool aParentIsActive);
    void UpdateDimensions(const nsIntRect& rect, const ScreenIntSize& size,
                          const nsIntPoint& chromeDisp);
    void UpdateFrame(const layers::FrameMetrics& aFrameMetrics);
    void UIResolutionChanged();
    void RequestFlingSnap(const FrameMetrics::ViewID& aScrollId,
                          const mozilla::CSSPoint& aDestination);
    void AcknowledgeScrollUpdate(const ViewID& aScrollId, const uint32_t& aScrollGeneration);
    void HandleDoubleTap(const CSSPoint& aPoint,
                         Modifiers aModifiers,
                         const ScrollableLayerGuid& aGuid);
    void HandleSingleTap(const CSSPoint& aPoint,
                         Modifiers aModifiers,
                         const ScrollableLayerGuid& aGuid);
    void HandleLongTap(const CSSPoint& aPoint,
                       Modifiers aModifiers,
                       const ScrollableLayerGuid& aGuid,
                       uint64_t aInputBlockId);
    void HandleLongTapUp(const CSSPoint& aPoint,
                         Modifiers aModifiers,
                         const ScrollableLayerGuid& aGuid);
    void NotifyAPZStateChange(ViewID aViewId,
                              APZStateChange aChange,
                              int aArg);
    void Activate();
    void Deactivate();

    bool MapEventCoordinatesForChildProcess(mozilla::WidgetEvent* aEvent);
    void MapEventCoordinatesForChildProcess(const LayoutDeviceIntPoint& aOffset,
                                            mozilla::WidgetEvent* aEvent);
    LayoutDeviceToCSSScale GetLayoutDeviceToCSSScale();

    virtual bool RecvRequestNativeKeyBindings(const mozilla::WidgetKeyboardEvent& aEvent,
                                              MaybeNativeKeyBinding* aBindings) override;

    void SendMouseEvent(const nsAString& aType, float aX, float aY,
                        int32_t aButton, int32_t aClickCount,
                        int32_t aModifiers, bool aIgnoreRootScrollFrame);
    void SendKeyEvent(const nsAString& aType, int32_t aKeyCode,
                      int32_t aCharCode, int32_t aModifiers,
                      bool aPreventDefault);
    bool SendRealMouseEvent(mozilla::WidgetMouseEvent& event);
    bool SendMouseWheelEvent(mozilla::WidgetWheelEvent& event);
    bool SendRealKeyEvent(mozilla::WidgetKeyboardEvent& event);
    bool SendRealTouchEvent(WidgetTouchEvent& event);
    bool SendHandleSingleTap(const CSSPoint& aPoint, const Modifiers& aModifiers, const ScrollableLayerGuid& aGuid);
    bool SendHandleLongTap(const CSSPoint& aPoint, const Modifiers& aModifiers, const ScrollableLayerGuid& aGuid, const uint64_t& aInputBlockId);
    bool SendHandleLongTapUp(const CSSPoint& aPoint, const Modifiers& aModifiers, const ScrollableLayerGuid& aGuid);
    bool SendHandleDoubleTap(const CSSPoint& aPoint, const Modifiers& aModifiers, const ScrollableLayerGuid& aGuid);

    virtual PDocumentRendererParent*
    AllocPDocumentRendererParent(const nsRect& documentRect,
                                 const gfx::Matrix& transform,
                                 const nsString& bgcolor,
                                 const uint32_t& renderFlags,
                                 const bool& flushLayout,
                                 const nsIntSize& renderSize) override;
    virtual bool DeallocPDocumentRendererParent(PDocumentRendererParent* actor) override;

    virtual PContentPermissionRequestParent*
    AllocPContentPermissionRequestParent(const InfallibleTArray<PermissionRequest>& aRequests,
                                         const IPC::Principal& aPrincipal) override;
    virtual bool
    DeallocPContentPermissionRequestParent(PContentPermissionRequestParent* actor) override;

    virtual PFilePickerParent*
    AllocPFilePickerParent(const nsString& aTitle,
                           const int16_t& aMode) override;
    virtual bool DeallocPFilePickerParent(PFilePickerParent* actor) override;

    virtual PIndexedDBPermissionRequestParent*
    AllocPIndexedDBPermissionRequestParent(const Principal& aPrincipal)
                                           override;

    virtual bool
    RecvPIndexedDBPermissionRequestConstructor(
                                      PIndexedDBPermissionRequestParent* aActor,
                                      const Principal& aPrincipal)
                                      override;

    virtual bool
    DeallocPIndexedDBPermissionRequestParent(
                                      PIndexedDBPermissionRequestParent* aActor)
                                      override;

    bool GetGlobalJSObject(JSContext* cx, JSObject** globalp);

    NS_DECL_ISUPPORTS
    NS_DECL_NSIAUTHPROMPTPROVIDER
    NS_DECL_NSISECUREBROWSERUI

    static TabParent *GetIMETabParent() { return mIMETabParent; }
    bool HandleQueryContentEvent(mozilla::WidgetQueryContentEvent& aEvent);
    bool SendCompositionEvent(mozilla::WidgetCompositionEvent& event);
    bool SendSelectionEvent(mozilla::WidgetSelectionEvent& event);

    static TabParent* GetFrom(nsFrameLoader* aFrameLoader);
    static TabParent* GetFrom(nsIFrameLoader* aFrameLoader);
    static TabParent* GetFrom(nsITabParent* aTabParent);
    static TabParent* GetFrom(PBrowserParent* aTabParent);
    static TabParent* GetFrom(nsIContent* aContent);
    static TabId GetTabIdFrom(nsIDocShell* docshell);

    nsIContentParent* Manager() { return mManager; }

    /**
     * Let managees query if Destroy() is already called so they don't send out
     * messages when the PBrowser actor is being destroyed.
     */
    bool IsDestroyed() const { return mIsDestroyed; }

    already_AddRefed<nsIWidget> GetWidget() const;

    const TabId GetTabId() const
    {
      return mTabId;
    }

    LayoutDeviceIntPoint GetChildProcessOffset();

    /**
     * Native widget remoting protocol for use with windowed plugins with e10s.
     */
    virtual PPluginWidgetParent* AllocPPluginWidgetParent() override;
    virtual bool DeallocPPluginWidgetParent(PPluginWidgetParent* aActor) override;

    void SetInitedByParent() { mInitedByParent = true; }
    bool IsInitedByParent() const { return mInitedByParent; }

    static TabParent* GetNextTabParent();

    bool SendLoadRemoteScript(const nsString& aURL,
                              const bool& aRunInGlobalScope);

    // See nsIFrameLoader requestNotifyLayerTreeReady.
    bool RequestNotifyLayerTreeReady();
    bool RequestNotifyLayerTreeCleared();
    bool LayerTreeUpdate(bool aActive);

protected:
    bool ReceiveMessage(const nsString& aMessage,
                        bool aSync,
                        const StructuredCloneData* aCloneData,
                        mozilla::jsipc::CpowHolder* aCpows,
                        nsIPrincipal* aPrincipal,
                        InfallibleTArray<nsString>* aJSONRetVal = nullptr);

    virtual bool RecvAsyncAuthPrompt(const nsCString& aUri,
                                     const nsString& aRealm,
                                     const uint64_t& aCallbackId) override;

    virtual bool Recv__delete__() override;

    virtual void ActorDestroy(ActorDestroyReason why) override;

    Element* mFrameElement;
    nsCOMPtr<nsIBrowserDOMWindow> mBrowserDOMWindow;

    bool AllowContentIME();

    virtual PRenderFrameParent* AllocPRenderFrameParent() override;
    virtual bool DeallocPRenderFrameParent(PRenderFrameParent* aFrame) override;

    virtual bool RecvRemotePaintIsReady() override;

    virtual bool RecvGetRenderFrameInfo(PRenderFrameParent* aRenderFrame,
                                        TextureFactoryIdentifier* aTextureFactoryIdentifier,
                                        uint64_t* aLayersId) override;

    virtual bool RecvSetDimensions(const uint32_t& aFlags,
                                   const int32_t& aX, const int32_t& aY,
                                   const int32_t& aCx, const int32_t& aCy) override;

    bool SendCompositionChangeEvent(mozilla::WidgetCompositionEvent& event);

    bool InitBrowserConfiguration(nsIURI* aURI,
                                  BrowserConfiguration& aConfiguration);

    // IME
    static TabParent *mIMETabParent;
    nsString mIMECacheText;
    uint32_t mIMESelectionAnchor;
    uint32_t mIMESelectionFocus;
    mozilla::WritingMode mWritingMode;
    bool mIMEComposing;
    bool mIMECompositionEnding;
    uint32_t mIMEEventCountAfterEnding;
    // Buffer to store composition text during ResetInputState
    // Compositions in almost all cases are small enough for nsAutoString
    nsAutoString mIMECompositionText;
    uint32_t mIMECompositionStart;
    uint32_t mIMESeqno;

    uint32_t mIMECompositionRectOffset;
    InfallibleTArray<LayoutDeviceIntRect> mIMECompositionRects;
    uint32_t mIMECaretOffset;
    LayoutDeviceIntRect mIMECaretRect;
    LayoutDeviceIntRect mIMEEditorRect;

    nsIntRect mRect;
    ScreenIntSize mDimensions;
    ScreenOrientation mOrientation;
    float mDPI;
    CSSToLayoutDeviceScale mDefaultScale;
    bool mShown;
    bool mUpdatedDimensions;

private:
    already_AddRefed<nsFrameLoader> GetFrameLoader() const;
    layout::RenderFrameParent* GetRenderFrame();
    nsRefPtr<nsIContentParent> mManager;
    void TryCacheDPIAndScale();

    CSSPoint AdjustTapToChildWidget(const CSSPoint& aPoint);

    // Update state prior to routing an APZ-aware event to the child process.
    // |aOutTargetGuid| will contain the identifier
    // of the APZC instance that handled the event. aOutTargetGuid may be
    // null.
    // |aOutInputBlockId| will contain the identifier of the input block
    // that this event was added to, if there was one. aOutInputBlockId may
    // be null.
    void ApzAwareEventRoutingToChild(ScrollableLayerGuid* aOutTargetGuid,
                                     uint64_t* aOutInputBlockId);
    // The offset for the child process which is sampled at touch start. This
    // means that the touch events are relative to where the frame was at the
    // start of the touch. We need to look for a better solution to this
    // problem see bug 872911.
    LayoutDeviceIntPoint mChildProcessOffsetAtTouchStart;
    // When true, we've initiated normal shutdown and notified our
    // managing PContent.
    bool mMarkedDestroying;
    // When true, the TabParent is invalid and we should not send IPC messages
    // anymore.
    bool mIsDestroyed;
    // Whether we have already sent a FileDescriptor for the app package.
    bool mAppPackageFileDescriptorSent;

    // Whether we need to send the offline status to the TabChild
    // This is true, until the first call of LoadURL
    bool mSendOfflineStatus;

    uint32_t mChromeFlags;

    // When true, the TabParent is initialized without child side's request.
    // When false, the TabParent is initialized by window.open() from child side.
    bool mInitedByParent;

    nsCOMPtr<nsILoadContext> mLoadContext;

    TabId mTabId;

    // Helper class for RecvCreateWindow.
    struct AutoUseNewTab;

    // When loading a new tab or window via window.open, the child process sends
    // a new PBrowser to use. We store that tab in sNextTabParent and then
    // proceed through the browser's normal paths to create a new
    // window/tab. When it comes time to create a new TabParent, we instead use
    // sNextTabParent.
    static TabParent* sNextTabParent;

    // When loading a new tab or window via window.open, the child is
    // responsible for loading the URL it wants into the new TabChild. When the
    // parent receives the CreateWindow message, though, it sends a LoadURL
    // message, usually for about:blank. It's important for the about:blank load
    // to get processed because the Firefox frontend expects every new window to
    // immediately start loading something (see bug 1123090). However, we want
    // the child to process the LoadURL message before it returns from
    // ProvideWindow so that the URL sent from the parent doesn't override the
    // child's URL. This is not possible using our IPC mechanisms. To solve the
    // problem, we skip sending the LoadURL message in the parent and instead
    // return the URL as a result from CreateWindow. The child simulates
    // receiving a LoadURL message before returning from ProvideWindow.
    //
    // The mCreatingWindow flag is set while dispatching CreateWindow. During
    // that time, any LoadURL calls are skipped and the URL is stored in
    // mSkippedURL.
    bool mCreatingWindow;
    nsCString mDelayedURL;

    // When loading a new tab or window via window.open, we want to ensure that
    // frame scripts for that tab are loaded before any scripts start to run in
    // the window. We can't load the frame scripts the normal way, using
    // separate IPC messages, since they won't be processed by the child until
    // returning to the event loop, which is too late. Instead, we queue up
    // frame scripts that we intend to load and send them as part of the
    // CreateWindow response. Then TabChild loads them immediately.
    nsTArray<FrameScriptInfo> mDelayedFrameScripts;

private:
    // This is used when APZ needs to find the TabParent associated with a layer
    // to dispatch events.
    typedef nsDataHashtable<nsUint64HashKey, TabParent*> LayerToTabParentTable;
    static LayerToTabParentTable* sLayerToTabParentTable;

    static void AddTabParentToTable(uint64_t aLayersId, TabParent* aTabParent);
    static void RemoveTabParentFromTable(uint64_t aLayersId);

public:
    static TabParent* GetTabParentFromLayersId(uint64_t aLayersId);
};

} // namespace dom
} // namespace mozilla

#endif
