/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsGlobalWindow_h___
#define nsGlobalWindow_h___

#include "nsPIDOMWindow.h"

#include "nsTHashtable.h"
#include "nsHashKeys.h"
#include "nsRefPtrHashtable.h"
#include "nsInterfaceHashtable.h"

// Local Includes
// Helper Classes
#include "nsCOMPtr.h"
#include "nsAutoPtr.h"
#include "nsWeakReference.h"
#include "nsDataHashtable.h"
#include "nsJSThingHashtable.h"
#include "nsCycleCollectionParticipant.h"

// Interfaces Needed
#include "nsIBrowserDOMWindow.h"
#include "nsIDOMEventTarget.h"
#include "nsIInterfaceRequestor.h"
#include "nsIDOMChromeWindow.h"
#include "nsIScriptGlobalObject.h"
#include "nsIScriptObjectPrincipal.h"
#include "nsITimer.h"
#include "nsIDOMModalContentWindow.h"
#include "mozilla/EventListenerManager.h"
#include "nsIPrincipal.h"
#include "nsSize.h"
#include "mozFlushType.h"
#include "prclist.h"
#include "mozilla/dom/StorageEvent.h"
#include "mozilla/dom/StorageEventBinding.h"
#include "mozilla/dom/UnionTypes.h"
#include "mozilla/ErrorResult.h"
#include "nsFrameMessageManager.h"
#include "mozilla/LinkedList.h"
#include "mozilla/TimeStamp.h"
#include "nsWrapperCacheInlines.h"
#include "nsIIdleObserver.h"
#include "nsIDocument.h"
#include "mozilla/dom/EventTarget.h"
#include "mozilla/dom/WindowBinding.h"
#include "Units.h"
#include "nsComponentManagerUtils.h"
#include "nsSize.h"
#include "nsCheapSets.h"
#include "mozilla/dom/ImageBitmapSource.h"

#define DEFAULT_HOME_PAGE "www.mozilla.org"
#define PREF_BROWSER_STARTUP_HOMEPAGE "browser.startup.homepage"

// Amount of time allowed between alert/prompt/confirm before enabling
// the stop dialog checkbox.
#define DEFAULT_SUCCESSIVE_DIALOG_TIME_LIMIT 3 // 3 sec

// Maximum number of successive dialogs before we prompt users to disable
// dialogs for this window.
#define MAX_SUCCESSIVE_DIALOG_COUNT 5

// Idle fuzz time upper limit
#define MAX_IDLE_FUZZ_TIME_MS 90000

// Min idle notification time in seconds.
#define MIN_IDLE_NOTIFICATION_TIME_S 1

class nsIArray;
class nsIBaseWindow;
class nsIContent;
class nsICSSDeclaration;
class nsIDocShellTreeOwner;
class nsIDOMOfflineResourceList;
class nsIScrollableFrame;
class nsIControllers;
class nsIJSID;
class nsIScriptContext;
class nsIScriptTimeoutHandler;
class nsIWebBrowserChrome;

class nsDOMWindowList;
class nsLocation;
class nsScreen;
class nsHistory;
class nsGlobalWindowObserver;
class nsGlobalWindow;
class nsDOMWindowUtils;
class nsIIdleService;
struct nsRect;

class nsWindowSizes;

namespace mozilla {
class DOMEventTargetHelper;
namespace dom {
class BarProp;
class Console;
class Crypto;
class External;
class Function;
class Gamepad;
class MediaQueryList;
class MozSelfSupport;
class Navigator;
class OwningExternalOrWindowProxy;
class Promise;
class PostMessageEvent;
struct RequestInit;
class RequestOrUSVString;
class Selection;
class SpeechSynthesis;
class U2F;
class VRDevice;
class WakeLock;
#if defined(MOZ_WIDGET_ANDROID) || defined(MOZ_WIDGET_GONK)
class WindowOrientationObserver;
#endif
namespace cache {
class CacheStorage;
} // namespace cache
class IDBFactory;
} // namespace dom
namespace gfx {
class VRDeviceProxy;
} // namespace gfx
} // namespace mozilla

extern already_AddRefed<nsIScriptTimeoutHandler>
NS_CreateJSTimeoutHandler(JSContext* aCx, nsGlobalWindow *aWindow,
                          mozilla::dom::Function& aFunction,
                          const mozilla::dom::Sequence<JS::Value>& aArguments,
                          mozilla::ErrorResult& aError);

extern already_AddRefed<nsIScriptTimeoutHandler>
NS_CreateJSTimeoutHandler(JSContext* aCx, nsGlobalWindow *aWindow,
                          const nsAString& aExpression,
                          mozilla::ErrorResult& aError);

extern const js::Class OuterWindowProxyClass;

/*
 * Timeout struct that holds information about each script
 * timeout.  Holds a strong reference to an nsIScriptTimeoutHandler, which
 * abstracts the language specific cruft.
 */
struct nsTimeout final
  : mozilla::LinkedListElement<nsTimeout>
{
private:
  ~nsTimeout();

public:
  nsTimeout();

  NS_DECL_CYCLE_COLLECTION_NATIVE_CLASS(nsTimeout)
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(nsTimeout)

  nsresult InitTimer(uint32_t aDelay);

  bool HasRefCntOne();

  // Window for which this timeout fires
  RefPtr<nsGlobalWindow> mWindow;

  // The actual timer object
  nsCOMPtr<nsITimer> mTimer;

  // True if the timeout was cleared
  bool mCleared;

  // True if this is one of the timeouts that are currently running
  bool mRunning;

  // True if this is a repeating/interval timer
  bool mIsInterval;

  // Returned as value of setTimeout()
  uint32_t mPublicId;

  // Interval in milliseconds
  uint32_t mInterval;

  // mWhen and mTimeRemaining can't be in a union, sadly, because they
  // have constructors.
  // Nominal time to run this timeout.  Use only when timeouts are not
  // suspended.
  mozilla::TimeStamp mWhen;
  // Remaining time to wait.  Used only when timeouts are suspended.
  mozilla::TimeDuration mTimeRemaining;

  // Principal with which to execute
  nsCOMPtr<nsIPrincipal> mPrincipal;

  // stack depth at which timeout is firing
  uint32_t mFiringDepth;

  // 
  uint32_t mNestingLevel;

  // The popup state at timeout creation time if not created from
  // another timeout
  PopupControlState mPopupState;

  // The language-specific information about the callback.
  nsCOMPtr<nsIScriptTimeoutHandler> mScriptHandler;
};

struct IdleObserverHolder
{
  nsCOMPtr<nsIIdleObserver> mIdleObserver;
  uint32_t mTimeInS;
  bool mPrevNotificationIdle;

  IdleObserverHolder()
    : mTimeInS(0), mPrevNotificationIdle(false)
  {
    MOZ_COUNT_CTOR(IdleObserverHolder);
  }

  IdleObserverHolder(const IdleObserverHolder& aOther)
    : mIdleObserver(aOther.mIdleObserver), mTimeInS(aOther.mTimeInS),
      mPrevNotificationIdle(aOther.mPrevNotificationIdle)
  {
    MOZ_COUNT_CTOR(IdleObserverHolder);
  }

  bool operator==(const IdleObserverHolder& aOther) const {
    return
      mIdleObserver == aOther.mIdleObserver &&
      mTimeInS == aOther.mTimeInS;
  }

  ~IdleObserverHolder()
  {
    MOZ_COUNT_DTOR(IdleObserverHolder);
  }
};

// Helper class to manage modal dialog arguments and all their quirks.
//
// Given our clunky embedding APIs, modal dialog arguments need to be passed
// as an nsISupports parameter to WindowWatcher, get stuck inside an array of
// length 1, and then passed back to the newly-created dialog.
//
// However, we need to track both the caller-passed value as well as the
// caller's, so that we can do an origin check (even for primitives) when the
// value is accessed. This class encapsulates that magic.
//
// We also use the same machinery for |returnValue|, which needs similar origin
// checks.
class DialogValueHolder final : public nsISupports
{
public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS(DialogValueHolder)

  DialogValueHolder(nsIPrincipal* aSubject, nsIVariant* aValue)
    : mOrigin(aSubject)
    , mValue(aValue) {}
  nsresult Get(nsIPrincipal* aSubject, nsIVariant** aResult);
  void Get(JSContext* aCx, JS::Handle<JSObject*> aScope, nsIPrincipal* aSubject,
           JS::MutableHandle<JS::Value> aResult, mozilla::ErrorResult& aError);
private:
  virtual ~DialogValueHolder() {}

  nsCOMPtr<nsIPrincipal> mOrigin;
  nsCOMPtr<nsIVariant> mValue;
};

//*****************************************************************************
// nsGlobalWindow: Global Object for Scripting
//*****************************************************************************
// Beware that all scriptable interfaces implemented by
// nsGlobalWindow will be reachable from JS, if you make this class
// implement new interfaces you better know what you're
// doing. Security wise this is very sensitive code. --
// jst@netscape.com

// nsGlobalWindow inherits PRCList for maintaining a list of all inner
// windows still in memory for any given outer window. This list is
// needed to ensure that mOuterWindow doesn't end up dangling. The
// nature of PRCList means that the window itself is always in the
// list, and an outer window's list will also contain all inner window
// objects that are still in memory (and in reality all inner window
// object's lists also contain its outer and all other inner windows
// belonging to the same outer window, but that's an unimportant
// side effect of inheriting PRCList).

class nsGlobalWindow : public mozilla::dom::EventTarget,
                       public nsPIDOMWindow,
                       public nsIScriptGlobalObject,
                       public nsIScriptObjectPrincipal,
                       public nsSupportsWeakReference,
                       public nsIInterfaceRequestor,
                       public PRCListStr
{
public:
  typedef mozilla::TimeStamp TimeStamp;
  typedef mozilla::TimeDuration TimeDuration;
  typedef nsDataHashtable<nsUint64HashKey, nsGlobalWindow*> WindowByIdTable;

  static void
  AssertIsOnMainThread()
#ifdef DEBUG
  ;
#else
  { }
#endif

  static nsGlobalWindow* Cast(nsPIDOMWindow* aPIWin) { return static_cast<nsGlobalWindow*>(aPIWin); }

  // public methods
  nsPIDOMWindow* GetPrivateParent();

  // callback for close event
  void ReallyCloseWindow();

  // nsISupports
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS

  // nsWrapperCache
  virtual JSObject *WrapObject(JSContext *cx, JS::Handle<JSObject*> aGivenProto) override
  {
    return IsInnerWindow() || EnsureInnerWindow() ? GetWrapper() : nullptr;
  }

  // nsIGlobalJSObjectHolder
  virtual JSObject* GetGlobalJSObject() override;

  // nsIScriptGlobalObject
  JSObject *FastGetGlobalJSObject() const
  {
    return GetWrapperPreserveColor();
  }

  void TraceGlobalJSObject(JSTracer* aTrc);

  virtual nsresult EnsureScriptEnvironment() override;

  virtual nsIScriptContext *GetScriptContext() override;

  void PoisonOuterWindowProxy(JSObject *aObject);

  virtual bool IsBlackForCC(bool aTracingNeeded = true) override;

  // nsIScriptObjectPrincipal
  virtual nsIPrincipal* GetPrincipal() override;

  // nsIDOMWindow
  NS_DECL_NSIDOMWINDOW

  nsresult
  OpenJS(const nsAString& aUrl, const nsAString& aName,
         const nsAString& aOptions, nsIDOMWindow **_retval);
  void CaptureEvents();
  void ReleaseEvents();
  void Dump(const nsAString& aStr);
  void SetResizable(bool aResizable) const;
  nsresult GetScriptableContent(JSContext* aCx,
                                JS::MutableHandle<JS::Value> aVal);

  // nsIDOMEventTarget
  NS_DECL_NSIDOMEVENTTARGET

  virtual mozilla::EventListenerManager*
    GetExistingListenerManager() const override;

  virtual mozilla::EventListenerManager*
    GetOrCreateListenerManager() override;

  using mozilla::dom::EventTarget::RemoveEventListener;
  virtual void AddEventListener(const nsAString& aType,
                                mozilla::dom::EventListener* aListener,
                                const mozilla::dom::AddEventListenerOptionsOrBoolean& aOptions,
                                const mozilla::dom::Nullable<bool>& aWantsUntrusted,
                                mozilla::ErrorResult& aRv) override;
  virtual nsIDOMWindow* GetOwnerGlobalForBindings() override
  {
    if (IsOuterWindow()) {
      return this;
    }

    return GetOuterFromCurrentInner(this);
  }

  virtual nsIGlobalObject* GetOwnerGlobal() const override
  {
    if (IsOuterWindow()) {
      return GetCurrentInnerWindowInternal();
    }

    return const_cast<nsGlobalWindow*>(this);
  }

  // nsPIDOMWindow
  virtual nsPIDOMWindow* GetPrivateRoot() override;

  // Outer windows only.
  virtual void ActivateOrDeactivate(bool aActivate) override;
  virtual void SetActive(bool aActive) override;
  virtual bool IsTopLevelWindowActive() override;
  virtual void SetIsBackground(bool aIsBackground) override;
  virtual void SetChromeEventHandler(mozilla::dom::EventTarget* aChromeEventHandler) override;

  // Outer windows only.
  virtual void SetInitialPrincipalToSubject() override;

  virtual PopupControlState PushPopupControlState(PopupControlState state, bool aForce) const override;
  virtual void PopPopupControlState(PopupControlState state) const override;
  virtual PopupControlState GetPopupControlState() const override;

  virtual already_AddRefed<nsISupports> SaveWindowState() override;
  virtual nsresult RestoreWindowState(nsISupports *aState) override;
  virtual void SuspendTimeouts(uint32_t aIncrease = 1,
                               bool aFreezeChildren = true,
                               bool aFreezeWorkers = true) override;
  virtual nsresult ResumeTimeouts(bool aThawChildren = true,
                                  bool aThawWorkers = true) override;
  virtual uint32_t TimeoutSuspendCount() override;
  virtual nsresult FireDelayedDOMEvents() override;
  virtual bool IsFrozen() const override
  {
    return mIsFrozen;
  }
  virtual bool IsRunningTimeout() override { return mTimeoutFiringDepth > 0; }

  // Outer windows only.
  virtual bool WouldReuseInnerWindow(nsIDocument* aNewDocument) override;

  virtual void SetDocShell(nsIDocShell* aDocShell) override;
  virtual void DetachFromDocShell() override;
  virtual nsresult SetNewDocument(nsIDocument *aDocument,
                                  nsISupports *aState,
                                  bool aForceReuseInnerWindow) override;

  // Outer windows only.
  void DispatchDOMWindowCreated();

  virtual void SetOpenerWindow(nsPIDOMWindow* aOpener,
                               bool aOriginalOpener) override;

  // Outer windows only.
  virtual void EnsureSizeUpToDate() override;

  virtual void EnterModalState() override;
  virtual void LeaveModalState() override;

  // Outer windows only.
  virtual bool CanClose() override;
  virtual void ForceClose() override;

  virtual void MaybeUpdateTouchState() override;

  // Outer windows only.
  virtual bool DispatchCustomEvent(const nsAString& aEventName) override;
  bool DispatchResizeEvent(const mozilla::CSSIntSize& aSize);

  // Inner windows only.
  void RefreshCompartmentPrincipal();

  // Outer windows only.
  virtual nsresult SetFullScreenInternal(bool aIsFullscreen, bool aFullscreenMode,
                                         mozilla::gfx::VRDeviceProxy *aHMD = nullptr) override;
  bool FullScreen() const;

  // Inner windows only.
  virtual void SetHasGamepadEventListener(bool aHasGamepad = true) override;

  // nsIInterfaceRequestor
  NS_DECL_NSIINTERFACEREQUESTOR

  // WebIDL interface.
  already_AddRefed<nsIDOMWindow> IndexedGetterOuter(uint32_t aIndex);
  already_AddRefed<nsIDOMWindow> IndexedGetter(uint32_t aIndex);

  static bool IsPrivilegedChromeWindow(JSContext* /* unused */, JSObject* aObj);

  static bool IsShowModalDialogEnabled(JSContext* /* unused */ = nullptr,
                                       JSObject* /* unused */ = nullptr);

  bool DoResolve(JSContext* aCx, JS::Handle<JSObject*> aObj,
                 JS::Handle<jsid> aId,
                 JS::MutableHandle<JS::PropertyDescriptor> aDesc);
  // The return value is whether DoResolve might end up resolving the given id.
  // If in doubt, return true.
  static bool MayResolve(jsid aId);

  void GetOwnPropertyNames(JSContext* aCx, nsTArray<nsString>& aNames,
                           mozilla::ErrorResult& aRv);

  // Object Management
  static already_AddRefed<nsGlobalWindow> Create(nsGlobalWindow *aOuterWindow);

  static nsGlobalWindow *FromSupports(nsISupports *supports)
  {
    // Make sure this matches the casts we do in QueryInterface().
    return (nsGlobalWindow *)(mozilla::dom::EventTarget *)supports;
  }
  static nsGlobalWindow *FromWrapper(nsIXPConnectWrappedNative *wrapper)
  {
    return FromSupports(wrapper->Native());
  }

  already_AddRefed<nsIDOMWindow> GetTopOuter();
  already_AddRefed<nsPIDOMWindow> GetTop() override;
  nsPIDOMWindow* GetScriptableTop() override;
  inline nsGlobalWindow *GetTopInternal()
  {
    nsGlobalWindow* outer = IsOuterWindow() ? this : GetOuterWindowInternal();
    nsCOMPtr<nsPIDOMWindow> top = outer ? outer->GetTop() : nullptr;
    if (top)
      return static_cast<nsGlobalWindow *>(top.get());
    return nullptr;
  }

  inline nsGlobalWindow* GetScriptableTopInternal()
  {
    nsPIDOMWindow* top = GetScriptableTop();
    return static_cast<nsGlobalWindow*>(top);
  }

  nsPIDOMWindow* GetChildWindow(const nsAString& aName);

  // These return true if we've reached the state in this top level window
  // where we ask the user if further dialogs should be blocked.
  //
  // DialogsAreBeingAbused must be called on the scriptable top inner window.
  //
  // ShouldPromptToBlockDialogs is implemented in terms of
  // DialogsAreBeingAbused, and will get the scriptable top inner window
  // automatically.
  // Outer windows only.
  bool ShouldPromptToBlockDialogs();
  // Inner windows only.
  bool DialogsAreBeingAbused();

  // These functions are used for controlling and determining whether dialogs
  // (alert, prompt, confirm) are currently allowed in this window.
  void EnableDialogs();
  void DisableDialogs();
  // Outer windows only.
  bool AreDialogsEnabled();

  nsIScriptContext *GetContextInternal()
  {
    if (mOuterWindow) {
      return GetOuterWindowInternal()->mContext;
    }

    return mContext;
  }

  nsGlobalWindow *GetOuterWindowInternal()
  {
    return static_cast<nsGlobalWindow *>(GetOuterWindow());
  }

  nsGlobalWindow *GetCurrentInnerWindowInternal() const
  {
    MOZ_ASSERT(IsOuterWindow());
    return static_cast<nsGlobalWindow *>(mInnerWindow);
  }

  nsGlobalWindow *EnsureInnerWindowInternal()
  {
    return static_cast<nsGlobalWindow *>(EnsureInnerWindow());
  }

  bool IsCreatingInnerWindow() const
  {
    return  mCreatingInnerWindow;
  }

  bool IsChromeWindow() const
  {
    return mIsChrome;
  }

  using nsPIDOMWindow::IsModalContentWindow;
  static bool IsModalContentWindow(JSContext* aCx, JSObject* aGlobal);

  // GetScrollFrame does not flush.  Callers should do it themselves as needed,
  // depending on which info they actually want off the scrollable frame.
  nsIScrollableFrame *GetScrollFrame();

  nsresult Observe(nsISupports* aSubject, const char* aTopic,
                   const char16_t* aData);

  // Outer windows only.
  void UnblockScriptedClosing();

  static void Init();
  static void ShutDown();
  static void CleanupCachedXBLHandlers(nsGlobalWindow* aWindow);
  static bool IsCallerChrome();

  static void RunPendingTimeoutsRecursive(nsGlobalWindow *aTopWindow,
                                          nsGlobalWindow *aWindow);

  friend class WindowStateHolder;

  NS_DECL_CYCLE_COLLECTION_SKIPPABLE_SCRIPT_HOLDER_CLASS_AMBIGUOUS(nsGlobalWindow,
                                                                   nsIDOMEventTarget)

#ifdef DEBUG
  // Call Unlink on this window. This may cause bad things to happen, so use
  // with caution.
  void RiskyUnlink();
#endif

  virtual JSObject*
    GetCachedXBLPrototypeHandler(nsXBLPrototypeHandler* aKey) override;

  virtual void
    CacheXBLPrototypeHandler(nsXBLPrototypeHandler* aKey,
                             JS::Handle<JSObject*> aHandler) override;

  virtual bool TakeFocus(bool aFocus, uint32_t aFocusMethod) override;
  virtual void SetReadyForFocus() override;
  virtual void PageHidden() override;
  virtual nsresult DispatchAsyncHashchange(nsIURI *aOldURI, nsIURI *aNewURI) override;
  virtual nsresult DispatchSyncPopState() override;

  // Inner windows only.
  virtual void EnableDeviceSensor(uint32_t aType) override;
  virtual void DisableDeviceSensor(uint32_t aType) override;

#if defined(MOZ_WIDGET_ANDROID) || defined(MOZ_WIDGET_GONK)
  virtual void EnableOrientationChangeListener() override;
  virtual void DisableOrientationChangeListener() override;
#endif

  virtual void EnableTimeChangeNotifications() override;
  virtual void DisableTimeChangeNotifications() override;

#ifdef MOZ_B2G
  // Inner windows only.
  virtual void EnableNetworkEvent(mozilla::EventMessage aEventMessage) override;
  virtual void DisableNetworkEvent(
                 mozilla::EventMessage aEventMessage) override;
#endif // MOZ_B2G

  virtual nsresult SetArguments(nsIArray* aArguments) override;

  void MaybeForgiveSpamCount();
  bool IsClosedOrClosing() {
    return (mIsClosed ||
            mInClose ||
            mHavePendingClose ||
            mCleanedUp);
  }

  bool
  HadOriginalOpener() const
  {
    MOZ_ASSERT(IsOuterWindow());
    return mHadOriginalOpener;
  }

  bool
  IsTopLevelWindow()
  {
    MOZ_ASSERT(IsOuterWindow());
    nsPIDOMWindow* parentWindow = GetScriptableTop();
    return parentWindow == static_cast<nsPIDOMWindow*>(this);
  }

  virtual void
  FirePopupBlockedEvent(nsIDocument* aDoc,
                        nsIURI* aPopupURI,
                        const nsAString& aPopupWindowName,
                        const nsAString& aPopupWindowFeatures) override;

  virtual uint32_t GetSerial() override {
    return mSerial;
  }

  static nsGlobalWindow* GetOuterWindowWithId(uint64_t aWindowID) {
    AssertIsOnMainThread();

    if (!sWindowsById) {
      return nullptr;
    }

    nsGlobalWindow* outerWindow = sWindowsById->Get(aWindowID);
    return outerWindow && !outerWindow->IsInnerWindow() ? outerWindow : nullptr;
  }

  static nsGlobalWindow* GetInnerWindowWithId(uint64_t aInnerWindowID) {
    AssertIsOnMainThread();

    if (!sWindowsById) {
      return nullptr;
    }

    nsGlobalWindow* innerWindow = sWindowsById->Get(aInnerWindowID);
    return innerWindow && innerWindow->IsInnerWindow() ? innerWindow : nullptr;
  }

  static WindowByIdTable* GetWindowsTable() {
    AssertIsOnMainThread();

    return sWindowsById;
  }

  void AddSizeOfIncludingThis(nsWindowSizes* aWindowSizes) const;

  void UnmarkGrayTimers();

  // Inner windows only.
  void AddEventTargetObject(mozilla::DOMEventTargetHelper* aObject);
  void RemoveEventTargetObject(mozilla::DOMEventTargetHelper* aObject);

  void NotifyIdleObserver(IdleObserverHolder* aIdleObserverHolder,
                          bool aCallOnidle);
  nsresult HandleIdleActiveEvent();
  bool ContainsIdleObserver(nsIIdleObserver* aIdleObserver, uint32_t timeInS);
  void HandleIdleObserverCallback();

  void AllowScriptsToClose()
  {
    mAllowScriptsToClose = true;
  }

  enum SlowScriptResponse {
    ContinueSlowScript = 0,
    ContinueSlowScriptAndKeepNotifying,
    AlwaysContinueSlowScript,
    KillSlowScript
  };
  SlowScriptResponse ShowSlowScriptDialog();

#ifdef MOZ_GAMEPAD
  // Inner windows only.
  void AddGamepad(uint32_t aIndex, mozilla::dom::Gamepad* aGamepad);
  void RemoveGamepad(uint32_t aIndex);
  void GetGamepads(nsTArray<RefPtr<mozilla::dom::Gamepad> >& aGamepads);
  already_AddRefed<mozilla::dom::Gamepad> GetGamepad(uint32_t aIndex);
  void SetHasSeenGamepadInput(bool aHasSeen);
  bool HasSeenGamepadInput();
  void SyncGamepadState();
#endif

  // Inner windows only.
  // Enable/disable updates for gamepad input.
  void EnableGamepadUpdates();
  void DisableGamepadUpdates();

  // Update the VR devices for this window
  bool UpdateVRDevices(nsTArray<RefPtr<mozilla::dom::VRDevice>>& aDevices);

#define EVENT(name_, id_, type_, struct_)                                     \
  mozilla::dom::EventHandlerNonNull* GetOn##name_()                           \
  {                                                                           \
    mozilla::EventListenerManager* elm = GetExistingListenerManager();        \
    return elm ? elm->GetEventHandler(nsGkAtoms::on##name_, EmptyString())    \
               : nullptr;                                                     \
  }                                                                           \
  void SetOn##name_(mozilla::dom::EventHandlerNonNull* handler)               \
  {                                                                           \
    mozilla::EventListenerManager* elm = GetOrCreateListenerManager();        \
    if (elm) {                                                                \
      elm->SetEventHandler(nsGkAtoms::on##name_, EmptyString(), handler);     \
    }                                                                         \
  }
#define ERROR_EVENT(name_, id_, type_, struct_)                               \
  mozilla::dom::OnErrorEventHandlerNonNull* GetOn##name_()                    \
  {                                                                           \
    mozilla::EventListenerManager* elm = GetExistingListenerManager();        \
    return elm ? elm->GetOnErrorEventHandler() : nullptr;                     \
  }                                                                           \
  void SetOn##name_(mozilla::dom::OnErrorEventHandlerNonNull* handler)        \
  {                                                                           \
    mozilla::EventListenerManager* elm = GetOrCreateListenerManager();        \
    if (elm) {                                                                \
      elm->SetEventHandler(handler);                                          \
    }                                                                         \
  }
#define BEFOREUNLOAD_EVENT(name_, id_, type_, struct_)                        \
  mozilla::dom::OnBeforeUnloadEventHandlerNonNull* GetOn##name_()             \
  {                                                                           \
    mozilla::EventListenerManager* elm = GetExistingListenerManager();        \
    return elm ? elm->GetOnBeforeUnloadEventHandler() : nullptr;              \
  }                                                                           \
  void SetOn##name_(mozilla::dom::OnBeforeUnloadEventHandlerNonNull* handler) \
  {                                                                           \
    mozilla::EventListenerManager* elm = GetOrCreateListenerManager();        \
    if (elm) {                                                                \
      elm->SetEventHandler(handler);                                          \
    }                                                                         \
  }
#define WINDOW_ONLY_EVENT EVENT
#define TOUCH_EVENT EVENT
#include "mozilla/EventNameList.h"
#undef TOUCH_EVENT
#undef WINDOW_ONLY_EVENT
#undef BEFOREUNLOAD_EVENT
#undef ERROR_EVENT
#undef EVENT

  nsISupports* GetParentObject()
  {
    return nullptr;
  }

  static JSObject*
    CreateNamedPropertiesObject(JSContext *aCx, JS::Handle<JSObject*> aProto);

  nsGlobalWindow* Window();
  nsGlobalWindow* Self();
  nsIDocument* GetDocument()
  {
    return GetDoc();
  }
  void GetNameOuter(nsAString& aName);
  void GetName(nsAString& aName, mozilla::ErrorResult& aError);
  void SetNameOuter(const nsAString& aName, mozilla::ErrorResult& aError);
  void SetName(const nsAString& aName, mozilla::ErrorResult& aError);
  nsLocation* GetLocation(mozilla::ErrorResult& aError);
  nsHistory* GetHistory(mozilla::ErrorResult& aError);
  mozilla::dom::BarProp* GetLocationbar(mozilla::ErrorResult& aError);
  mozilla::dom::BarProp* GetMenubar(mozilla::ErrorResult& aError);
  mozilla::dom::BarProp* GetPersonalbar(mozilla::ErrorResult& aError);
  mozilla::dom::BarProp* GetScrollbars(mozilla::ErrorResult& aError);
  mozilla::dom::BarProp* GetStatusbar(mozilla::ErrorResult& aError);
  mozilla::dom::BarProp* GetToolbar(mozilla::ErrorResult& aError);
  void GetStatusOuter(nsAString& aStatus);
  void GetStatus(nsAString& aStatus, mozilla::ErrorResult& aError);
  void SetStatusOuter(const nsAString& aStatus);
  void SetStatus(const nsAString& aStatus, mozilla::ErrorResult& aError);
  void CloseOuter(bool aTrustedCaller);
  void Close(mozilla::ErrorResult& aError);
  bool GetClosedOuter();
  bool GetClosed(mozilla::ErrorResult& aError);
  void StopOuter(mozilla::ErrorResult& aError);
  void Stop(mozilla::ErrorResult& aError);
  void FocusOuter(mozilla::ErrorResult& aError);
  void Focus(mozilla::ErrorResult& aError);
  void BlurOuter();
  void Blur(mozilla::ErrorResult& aError);
  already_AddRefed<nsIDOMWindow> GetFramesOuter();
  already_AddRefed<nsIDOMWindow> GetFrames(mozilla::ErrorResult& aError);
  uint32_t Length();
  already_AddRefed<nsIDOMWindow> GetTop(mozilla::ErrorResult& aError);
protected:
  explicit nsGlobalWindow(nsGlobalWindow *aOuterWindow);
  nsPIDOMWindow* GetOpenerWindowOuter();
  // Initializes the mWasOffline member variable
  void InitWasOffline();
public:
  nsPIDOMWindow* GetOpenerWindow(mozilla::ErrorResult& aError);
  void GetOpener(JSContext* aCx, JS::MutableHandle<JS::Value> aRetval,
                 mozilla::ErrorResult& aError);
  already_AddRefed<nsPIDOMWindow> GetOpener() override;
  void SetOpener(JSContext* aCx, JS::Handle<JS::Value> aOpener,
                 mozilla::ErrorResult& aError);
  already_AddRefed<nsIDOMWindow> GetParentOuter();
  already_AddRefed<nsIDOMWindow> GetParent(mozilla::ErrorResult& aError);
  already_AddRefed<nsPIDOMWindow> GetParent() override;
  nsPIDOMWindow* GetScriptableParent() override;
  nsPIDOMWindow* GetScriptableParentOrNull() override;
  mozilla::dom::Element* GetFrameElementOuter();
  mozilla::dom::Element* GetFrameElement(mozilla::ErrorResult& aError);
  already_AddRefed<nsIDOMWindow> OpenOuter(const nsAString& aUrl,
                                           const nsAString& aName,
                                           const nsAString& aOptions,
                                           mozilla::ErrorResult& aError);
  already_AddRefed<nsIDOMWindow> Open(const nsAString& aUrl,
                                      const nsAString& aName,
                                      const nsAString& aOptions,
                                      mozilla::ErrorResult& aError);
  nsresult Open(const nsAString& aUrl, const nsAString& aName,
                const nsAString& aOptions,
                nsIDocShellLoadInfo* aLoadInfo,
     	          bool aForceNoOpener,
                nsPIDOMWindow **_retval) override;
  mozilla::dom::Navigator* GetNavigator(mozilla::ErrorResult& aError);
  nsIDOMOfflineResourceList* GetApplicationCache(mozilla::ErrorResult& aError);

  mozilla::dom::Console* GetConsole(mozilla::ErrorResult& aRv);

  // https://w3c.github.io/webappsec-secure-contexts/#dom-window-issecurecontext
  bool IsSecureContext() const;

  void GetSidebar(mozilla::dom::OwningExternalOrWindowProxy& aResult,
                  mozilla::ErrorResult& aRv);
  already_AddRefed<mozilla::dom::External> GetExternal(mozilla::ErrorResult& aRv);

  // Exposed only for testing
  static bool
  TokenizeDialogOptions(nsAString& aToken, nsAString::const_iterator& aIter,
                        nsAString::const_iterator aEnd);
  static void
  ConvertDialogOptions(const nsAString& aOptions, nsAString& aResult);

protected:
  bool AlertOrConfirm(bool aAlert, const nsAString& aMessage,
                      mozilla::ErrorResult& aError);

public:
  void Alert(mozilla::ErrorResult& aError);
  void AlertOuter(const nsAString& aMessage, mozilla::ErrorResult& aError);
  void Alert(const nsAString& aMessage, mozilla::ErrorResult& aError);
  already_AddRefed<mozilla::dom::cache::CacheStorage> GetCaches(mozilla::ErrorResult& aRv);
  bool ConfirmOuter(const nsAString& aMessage, mozilla::ErrorResult& aError);
  bool Confirm(const nsAString& aMessage, mozilla::ErrorResult& aError);
  already_AddRefed<mozilla::dom::Promise> Fetch(const mozilla::dom::RequestOrUSVString& aInput,
                                                const mozilla::dom::RequestInit& aInit,
                                                mozilla::ErrorResult& aRv);
  void PromptOuter(const nsAString& aMessage, const nsAString& aInitial,
                   nsAString& aReturn, mozilla::ErrorResult& aError);
  void Prompt(const nsAString& aMessage, const nsAString& aInitial,
              nsAString& aReturn, mozilla::ErrorResult& aError);
  void PrintOuter(mozilla::ErrorResult& aError);
  void Print(mozilla::ErrorResult& aError);
  void ShowModalDialog(JSContext* aCx, const nsAString& aUrl,
                       JS::Handle<JS::Value> aArgument,
                       const nsAString& aOptions,
                       JS::MutableHandle<JS::Value> aRetval,
                       mozilla::ErrorResult& aError);
  void PostMessageMoz(JSContext* aCx, JS::Handle<JS::Value> aMessage,
                      const nsAString& aTargetOrigin,
                      const mozilla::dom::Optional<mozilla::dom::Sequence<JS::Value > >& aTransfer,
                      mozilla::ErrorResult& aError);
  int32_t SetTimeout(JSContext* aCx, mozilla::dom::Function& aFunction,
                     int32_t aTimeout,
                     const mozilla::dom::Sequence<JS::Value>& aArguments,
                     mozilla::ErrorResult& aError);
  int32_t SetTimeout(JSContext* aCx, const nsAString& aHandler,
                     int32_t aTimeout,
                     const mozilla::dom::Sequence<JS::Value>& /* unused */,
                     mozilla::ErrorResult& aError);
  void ClearTimeout(int32_t aHandle);
  int32_t SetInterval(JSContext* aCx, mozilla::dom::Function& aFunction,
                      const mozilla::dom::Optional<int32_t>& aTimeout,
                      const mozilla::dom::Sequence<JS::Value>& aArguments,
                      mozilla::ErrorResult& aError);
  int32_t SetInterval(JSContext* aCx, const nsAString& aHandler,
                      const mozilla::dom::Optional<int32_t>& aTimeout,
                      const mozilla::dom::Sequence<JS::Value>& /* unused */,
                      mozilla::ErrorResult& aError);
  void ClearInterval(int32_t aHandle);
  void Atob(const nsAString& aAsciiBase64String, nsAString& aBinaryData,
            mozilla::ErrorResult& aError);
  void Btoa(const nsAString& aBinaryData, nsAString& aAsciiBase64String,
            mozilla::ErrorResult& aError);
  mozilla::dom::DOMStorage* GetSessionStorage(mozilla::ErrorResult& aError);
  mozilla::dom::DOMStorage* GetLocalStorage(mozilla::ErrorResult& aError);
  mozilla::dom::Selection* GetSelectionOuter();
  mozilla::dom::Selection* GetSelection(mozilla::ErrorResult& aError);
  mozilla::dom::IDBFactory* GetIndexedDB(mozilla::ErrorResult& aError);
  already_AddRefed<nsICSSDeclaration>
    GetComputedStyle(mozilla::dom::Element& aElt, const nsAString& aPseudoElt,
                     mozilla::ErrorResult& aError);
  already_AddRefed<mozilla::dom::MediaQueryList> MatchMediaOuter(const nsAString& aQuery);
  already_AddRefed<mozilla::dom::MediaQueryList> MatchMedia(const nsAString& aQuery,
                                                            mozilla::ErrorResult& aError);
  nsScreen* GetScreen(mozilla::ErrorResult& aError);
  void MoveToOuter(int32_t aXPos, int32_t aYPos, mozilla::ErrorResult& aError, bool aCallerIsChrome);
  void MoveTo(int32_t aXPos, int32_t aYPos, mozilla::ErrorResult& aError);
  void MoveByOuter(int32_t aXDif, int32_t aYDif, mozilla::ErrorResult& aError, bool aCallerIsChrome);
  void MoveBy(int32_t aXDif, int32_t aYDif, mozilla::ErrorResult& aError);
  void ResizeToOuter(int32_t aWidth, int32_t aHeight, mozilla::ErrorResult& aError, bool aCallerIsChrome);
  void ResizeTo(int32_t aWidth, int32_t aHeight,
                mozilla::ErrorResult& aError);
  void ResizeByOuter(int32_t aWidthDif, int32_t aHeightDif, mozilla::ErrorResult& aError, bool aCallerIsChrome);
  void ResizeBy(int32_t aWidthDif, int32_t aHeightDif,
                mozilla::ErrorResult& aError);
  void Scroll(double aXScroll, double aYScroll);
  void Scroll(const mozilla::dom::ScrollToOptions& aOptions);
  void ScrollTo(double aXScroll, double aYScroll);
  void ScrollTo(const mozilla::dom::ScrollToOptions& aOptions);
  void ScrollBy(double aXScrollDif, double aYScrollDif);
  void ScrollBy(const mozilla::dom::ScrollToOptions& aOptions);
  void ScrollByLines(int32_t numLines,
                     const mozilla::dom::ScrollOptions& aOptions);
  void ScrollByPages(int32_t numPages,
                     const mozilla::dom::ScrollOptions& aOptions);
  void MozScrollSnap();
  void GetInnerWidth(JSContext* aCx, JS::MutableHandle<JS::Value> aValue,
                     mozilla::ErrorResult& aError);
  void SetInnerWidth(JSContext* aCx, JS::Handle<JS::Value> aValue,
                     mozilla::ErrorResult& aError);
  void GetInnerHeight(JSContext* aCx, JS::MutableHandle<JS::Value> aValue,
                     mozilla::ErrorResult& aError);
  void SetInnerHeight(JSContext* aCx, JS::Handle<JS::Value> aValue,
                      mozilla::ErrorResult& aError);
  double GetScrollXOuter();
  double GetScrollX(mozilla::ErrorResult& aError);
  double GetPageXOffset(mozilla::ErrorResult& aError)
  {
    return GetScrollX(aError);
  }
  double GetScrollYOuter();
  double GetScrollY(mozilla::ErrorResult& aError);
  double GetPageYOffset(mozilla::ErrorResult& aError)
  {
    return GetScrollY(aError);
  }
  void MozRequestOverfill(mozilla::dom::OverfillCallback& aCallback, mozilla::ErrorResult& aError);
  void GetScreenX(JSContext* aCx, JS::MutableHandle<JS::Value> aValue,
                  mozilla::ErrorResult& aError);
  void SetScreenX(JSContext* aCx, JS::Handle<JS::Value> aValue,
                  mozilla::ErrorResult& aError);
  void GetScreenY(JSContext* aCx, JS::MutableHandle<JS::Value> aValue,
                  mozilla::ErrorResult& aError);
  void SetScreenY(JSContext* aCx, JS::Handle<JS::Value> aValue,
                  mozilla::ErrorResult& aError);
  void GetOuterWidth(JSContext* aCx, JS::MutableHandle<JS::Value> aValue,
                     mozilla::ErrorResult& aError);
  void SetOuterWidth(JSContext* aCx, JS::Handle<JS::Value> aValue,
                     mozilla::ErrorResult& aError);
  void GetOuterHeight(JSContext* aCx, JS::MutableHandle<JS::Value> aValue,
                      mozilla::ErrorResult& aError);
  void SetOuterHeight(JSContext* aCx, JS::Handle<JS::Value> aValue,
                      mozilla::ErrorResult& aError);
  int32_t RequestAnimationFrame(mozilla::dom::FrameRequestCallback& aCallback,
                                mozilla::ErrorResult& aError);
  void CancelAnimationFrame(int32_t aHandle, mozilla::ErrorResult& aError);
#ifdef MOZ_WEBSPEECH
  mozilla::dom::SpeechSynthesis*
    GetSpeechSynthesis(mozilla::ErrorResult& aError);
  bool HasActiveSpeechSynthesis();
#endif
  already_AddRefed<nsICSSDeclaration>
    GetDefaultComputedStyle(mozilla::dom::Element& aElt,
                            const nsAString& aPseudoElt,
                            mozilla::ErrorResult& aError);
  void SizeToContentOuter(mozilla::ErrorResult& aError, bool aCallerIsChrome);
  void SizeToContent(mozilla::ErrorResult& aError);
  mozilla::dom::Crypto* GetCrypto(mozilla::ErrorResult& aError);
  mozilla::dom::U2F* GetU2f(mozilla::ErrorResult& aError);
  nsIControllers* GetControllersOuter(mozilla::ErrorResult& aError);
  nsIControllers* GetControllers(mozilla::ErrorResult& aError);
  mozilla::dom::Element* GetRealFrameElementOuter();
  mozilla::dom::Element* GetRealFrameElement(mozilla::ErrorResult& aError);
  float GetMozInnerScreenXOuter();
  float GetMozInnerScreenX(mozilla::ErrorResult& aError);
  float GetMozInnerScreenYOuter();
  float GetMozInnerScreenY(mozilla::ErrorResult& aError);
  float GetDevicePixelRatioOuter();
  float GetDevicePixelRatio(mozilla::ErrorResult& aError);
  int32_t GetScrollMinX(mozilla::ErrorResult& aError);
  int32_t GetScrollMinY(mozilla::ErrorResult& aError);
  int32_t GetScrollMaxX(mozilla::ErrorResult& aError);
  int32_t GetScrollMaxY(mozilla::ErrorResult& aError);
  bool GetFullScreenOuter();
  bool GetFullScreen(mozilla::ErrorResult& aError);
  void SetFullScreenOuter(bool aFullScreen, mozilla::ErrorResult& aError);
  void SetFullScreen(bool aFullScreen, mozilla::ErrorResult& aError);
  void BackOuter(mozilla::ErrorResult& aError);
  void Back(mozilla::ErrorResult& aError);
  void ForwardOuter(mozilla::ErrorResult& aError);
  void Forward(mozilla::ErrorResult& aError);
  void HomeOuter(mozilla::ErrorResult& aError);
  void Home(mozilla::ErrorResult& aError);
  bool FindOuter(const nsAString& aString, bool aCaseSensitive, bool aBackwards,
                 bool aWrapAround, bool aWholeWord, bool aSearchInFrames,
                 bool aShowDialog, mozilla::ErrorResult& aError);
  bool Find(const nsAString& aString, bool aCaseSensitive, bool aBackwards,
            bool aWrapAround, bool aWholeWord, bool aSearchInFrames,
            bool aShowDialog, mozilla::ErrorResult& aError);
  uint64_t GetMozPaintCountOuter();
  uint64_t GetMozPaintCount(mozilla::ErrorResult& aError);

  bool ShouldResistFingerprinting();

  mozilla::dom::MozSelfSupport* GetMozSelfSupport(mozilla::ErrorResult& aError);

  already_AddRefed<nsIDOMWindow> OpenDialogOuter(JSContext* aCx,
                                                 const nsAString& aUrl,
                                                 const nsAString& aName,
                                                 const nsAString& aOptions,
                                                 const mozilla::dom::Sequence<JS::Value>& aExtraArgument,
                                                 mozilla::ErrorResult& aError);
  already_AddRefed<nsIDOMWindow> OpenDialog(JSContext* aCx,
                                            const nsAString& aUrl,
                                            const nsAString& aName,
                                            const nsAString& aOptions,
                                            const mozilla::dom::Sequence<JS::Value>& aExtraArgument,
                                            mozilla::ErrorResult& aError);
  nsresult
  OpenDialog(const nsAString& aUrl, const nsAString& aName,
             const nsAString& aOptions,
             nsISupports* aExtraArgument, nsIDOMWindow** _retval) override;
  already_AddRefed<nsIDOMWindow>
    GetContentInternal(mozilla::ErrorResult& aError, bool aUnprivilegedCaller);
  void GetContentOuter(JSContext* aCx,
                       JS::MutableHandle<JSObject*> aRetval,
                       mozilla::ErrorResult& aError);
  void GetContent(JSContext* aCx,
                  JS::MutableHandle<JSObject*> aRetval,
                  mozilla::ErrorResult& aError);
  already_AddRefed<nsIDOMWindow> GetContent()
  {
    MOZ_ASSERT(IsOuterWindow());
    mozilla::ErrorResult ignored;
    nsCOMPtr<nsIDOMWindow> win =
      GetContentInternal(ignored, /* aUnprivilegedCaller = */ false);
    ignored.SuppressException();
    return win.forget();
  }

  void Get_content(JSContext* aCx,
                   JS::MutableHandle<JSObject*> aRetval,
                   mozilla::ErrorResult& aError)
  {
    if (mDoc) {
      mDoc->WarnOnceAbout(nsIDocument::eWindow_Content);
    }
    GetContent(aCx, aRetval, aError);
  }

  already_AddRefed<mozilla::dom::Promise>
  CreateImageBitmap(const mozilla::dom::ImageBitmapSource& aImage,
                    mozilla::ErrorResult& aRv);

  already_AddRefed<mozilla::dom::Promise>
  CreateImageBitmap(const mozilla::dom::ImageBitmapSource& aImage,
                    int32_t aSx, int32_t aSy, int32_t aSw, int32_t aSh,
                    mozilla::ErrorResult& aRv);

  // ChromeWindow bits.  Do NOT call these unless your window is in
  // fact an nsGlobalChromeWindow.
  uint16_t WindowState();
  nsIBrowserDOMWindow* GetBrowserDOMWindowOuter();
  nsIBrowserDOMWindow* GetBrowserDOMWindow(mozilla::ErrorResult& aError);
  void SetBrowserDOMWindowOuter(nsIBrowserDOMWindow* aBrowserWindow);
  void SetBrowserDOMWindow(nsIBrowserDOMWindow* aBrowserWindow,
                           mozilla::ErrorResult& aError);
  void GetAttention(mozilla::ErrorResult& aError);
  void GetAttentionWithCycleCount(int32_t aCycleCount,
                                  mozilla::ErrorResult& aError);
  void SetCursorOuter(const nsAString& aCursor, mozilla::ErrorResult& aError);
  void SetCursor(const nsAString& aCursor, mozilla::ErrorResult& aError);
  void Maximize(mozilla::ErrorResult& aError);
  void Minimize(mozilla::ErrorResult& aError);
  void Restore(mozilla::ErrorResult& aError);
  void NotifyDefaultButtonLoaded(mozilla::dom::Element& aDefaultButton,
                                 mozilla::ErrorResult& aError);
  nsIMessageBroadcaster* GetMessageManager(mozilla::ErrorResult& aError);
  nsIMessageBroadcaster* GetGroupMessageManager(const nsAString& aGroup,
                                                mozilla::ErrorResult& aError);
  void BeginWindowMove(mozilla::dom::Event& aMouseDownEvent,
                       mozilla::dom::Element* aPanel,
                       mozilla::ErrorResult& aError);

  void GetDialogArgumentsOuter(JSContext* aCx, JS::MutableHandle<JS::Value> aRetval,
                               mozilla::ErrorResult& aError);
  void GetDialogArguments(JSContext* aCx, JS::MutableHandle<JS::Value> aRetval,
                          mozilla::ErrorResult& aError);
  void GetReturnValueOuter(JSContext* aCx, JS::MutableHandle<JS::Value> aReturnValue,
                           mozilla::ErrorResult& aError);
  void GetReturnValue(JSContext* aCx, JS::MutableHandle<JS::Value> aReturnValue,
                      mozilla::ErrorResult& aError);
  void SetReturnValueOuter(JSContext* aCx, JS::Handle<JS::Value> aReturnValue,
                           mozilla::ErrorResult& aError);
  void SetReturnValue(JSContext* aCx, JS::Handle<JS::Value> aReturnValue,
                      mozilla::ErrorResult& aError);

  void GetInterface(JSContext* aCx, nsIJSID* aIID,
                    JS::MutableHandle<JS::Value> aRetval,
                    mozilla::ErrorResult& aError);

  already_AddRefed<nsWindowRoot> GetWindowRootOuter();
  already_AddRefed<nsWindowRoot> GetWindowRoot(mozilla::ErrorResult& aError);

protected:
  // Web IDL helpers

  // Redefine the property called aPropName on this window object to be a value
  // property with the value aValue, much like we would do for a [Replaceable]
  // property in IDL.
  void RedefineProperty(JSContext* aCx, const char* aPropName,
                        JS::Handle<JS::Value> aValue,
                        mozilla::ErrorResult& aError);

  // Implementation guts for our writable IDL attributes that are really
  // supposed to be readonly replaceable.
  typedef int32_t (nsGlobalWindow::*WindowCoordGetter)(mozilla::ErrorResult&);
  typedef void (nsGlobalWindow::*WindowCoordSetter)(int32_t,
                                                    mozilla::ErrorResult&);
  void GetReplaceableWindowCoord(JSContext* aCx, WindowCoordGetter aGetter,
                                 JS::MutableHandle<JS::Value> aRetval,
                                 mozilla::ErrorResult& aError);
  void SetReplaceableWindowCoord(JSContext* aCx, WindowCoordSetter aSetter,
                                 JS::Handle<JS::Value> aValue,
                                 const char* aPropName,
                                 mozilla::ErrorResult& aError);
  // And the implementations of WindowCoordGetter/WindowCoordSetter.
  int32_t GetInnerWidthOuter(mozilla::ErrorResult& aError);
  int32_t GetInnerWidth(mozilla::ErrorResult& aError);
  void SetInnerWidthOuter(int32_t aInnerWidth, mozilla::ErrorResult& aError, bool aCallerIsChrome);
  void SetInnerWidth(int32_t aInnerWidth, mozilla::ErrorResult& aError);
  int32_t GetInnerHeightOuter(mozilla::ErrorResult& aError);
  int32_t GetInnerHeight(mozilla::ErrorResult& aError);
  void SetInnerHeightOuter(int32_t aInnerHeight, mozilla::ErrorResult& aError, bool aCallerIsChrome);
  void SetInnerHeight(int32_t aInnerHeight, mozilla::ErrorResult& aError);
  int32_t GetScreenXOuter(mozilla::ErrorResult& aError);
  int32_t GetScreenX(mozilla::ErrorResult& aError);
  void SetScreenXOuter(int32_t aScreenX, mozilla::ErrorResult& aError, bool aCallerIsChrome);
  void SetScreenX(int32_t aScreenX, mozilla::ErrorResult& aError);
  int32_t GetScreenYOuter(mozilla::ErrorResult& aError);
  int32_t GetScreenY(mozilla::ErrorResult& aError);
  void SetScreenYOuter(int32_t aScreenY, mozilla::ErrorResult& aError, bool aCallerIsChrome);
  void SetScreenY(int32_t aScreenY, mozilla::ErrorResult& aError);
  int32_t GetOuterWidthOuter(mozilla::ErrorResult& aError);
  int32_t GetOuterWidth(mozilla::ErrorResult& aError);
  void SetOuterWidthOuter(int32_t aOuterWidth, mozilla::ErrorResult& aError, bool aCallerIsChrome);
  void SetOuterWidth(int32_t aOuterWidth, mozilla::ErrorResult& aError);
  int32_t GetOuterHeightOuter(mozilla::ErrorResult& aError);
  int32_t GetOuterHeight(mozilla::ErrorResult& aError);
  void SetOuterHeightOuter(int32_t aOuterHeight, mozilla::ErrorResult& aError, bool aCallerIsChrome);
  void SetOuterHeight(int32_t aOuterHeight, mozilla::ErrorResult& aError);

  // Array of idle observers that are notified of idle events.
  nsTObserverArray<IdleObserverHolder> mIdleObservers;

  // Idle timer used for function callbacks to notify idle observers.
  nsCOMPtr<nsITimer> mIdleTimer;

  // Idle fuzz time added to idle timer callbacks.
  uint32_t mIdleFuzzFactor;

  // Index in mArrayIdleObservers
  // Next idle observer to notify user idle status
  int32_t mIdleCallbackIndex;

  // If false then the topic is "active"
  // If true then the topic is "idle"
  bool mCurrentlyIdle;

  // Set to true when a fuzz time needs to be applied
  // to active notifications to the idle observer.
  bool mAddActiveEventFuzzTime;

  nsCOMPtr <nsIIdleService> mIdleService;

  RefPtr<mozilla::dom::WakeLock> mWakeLock;

  static bool sIdleObserversAPIFuzzTimeDisabled;

  friend class HashchangeCallback;
  friend class mozilla::dom::BarProp;

  // Object Management
  virtual ~nsGlobalWindow();
  void DropOuterWindowDocs();
  void CleanUp();
  void ClearControllers();
  // Outer windows only.
  void FinalClose();

  inline void MaybeClearInnerWindow(nsGlobalWindow* aExpectedInner)
  {
    if(mInnerWindow == aExpectedInner) {
      mInnerWindow = nullptr;
    }
  }

  void FreeInnerObjects(bool aForDocumentOpen = false);
  nsGlobalWindow *CallerInnerWindow();

  // Only to be called on an inner window.
  // aDocument must not be null.
  void InnerSetNewDocument(JSContext* aCx, nsIDocument* aDocument);

  // Inner windows only.
  nsresult DefineArgumentsProperty(nsIArray *aArguments);

  // Get the parent, returns null if this is a toplevel window
  nsIDOMWindow* GetParentInternal();

  // popup tracking
  bool IsPopupSpamWindow()
  {
    if (IsInnerWindow() && !mOuterWindow) {
      return false;
    }

    return GetOuterWindowInternal()->mIsPopupSpam;
  }

  void SetPopupSpamWindow(bool aPopup)
  {
    if (IsInnerWindow() && !mOuterWindow) {
      NS_ERROR("SetPopupSpamWindow() called on inner window w/o an outer!");

      return;
    }

    GetOuterWindowInternal()->mIsPopupSpam = aPopup;
  }

  // Window Control Functions

  // Outer windows only.
  virtual nsresult
  OpenNoNavigate(const nsAString& aUrl,
                 const nsAString& aName,
                 const nsAString& aOptions,
                 nsIDOMWindow** _retval) override;

private:
  /**
   * @param aUrl the URL we intend to load into the window.  If aNavigate is
   *        true, we'll actually load this URL into the window. Otherwise,
   *        aUrl is advisory; OpenInternal will not load the URL into the
   *        new window.
   *
   * @param aName the name to use for the new window
   *
   * @param aOptions the window options to use for the new window
   *
   * @param aDialog true when called from variants of OpenDialog.  If this is
   *        true, this method will skip popup blocking checks.  The aDialog
   *        argument is passed on to the window watcher.
   *
   * @param aCalledNoScript true when called via the [noscript] open()
   *        and openDialog() methods.  When this is true, we do NOT want to use
   *        the JS stack for things like caller determination.
   *
   * @param aDoJSFixups true when this is the content-accessible JS version of
   *        window opening.  When true, popups do not cause us to throw, we save
   *        the caller's principal in the new window for later consumption, and
   *        we make sure that there is a document in the newly-opened window.
   *        Note that this last will only be done if the newly-opened window is
   *        non-chrome.
   *
   * @param aNavigate true if we should navigate to the provided URL, false
   *        otherwise.  When aNavigate is false, we also skip our can-load
   *        security check, on the assumption that whoever *actually* loads this
   *        page will do their own security check.
   *
   * @param argv The arguments to pass to the new window.  The first
   *        three args, if present, will be aUrl, aName, and aOptions.  So this
   *        param only matters if there are more than 3 arguments.
   *
   * @param aExtraArgument Another way to pass arguments in.  This is mutually
   *        exclusive with the argv/argc approach.
   *
   * @param aLoadInfo to be passed on along to the windowwatcher.
   * @param aForceNoOpener if true, will act as if "noopener" were passed in
   *                       aOptions, but without affecting any other window
   *                       features.
   *
   * @param aJSCallerContext The calling script's context. This must be null
   *        when aCalledNoScript is true.
   *
   * @param aReturn [out] The window that was opened, if any.
   *
   * Outer windows only.
   */
  nsresult OpenInternal(const nsAString& aUrl,
                                    const nsAString& aName,
                                    const nsAString& aOptions,
                                    bool aDialog,
                                    bool aContentModal,
                                    bool aCalledNoScript,
                                    bool aDoJSFixups,
                                    bool aNavigate,
                                    nsIArray *argv,
                                    nsISupports *aExtraArgument,
				                            nsIDocShellLoadInfo* aLoadInfo,
				                            bool aForceNoOpener,
                                    nsIPrincipal *aCalleePrincipal,
                                    JSContext *aJSCallerContext,
                                    nsIDOMWindow **aReturn);

public:
  // Timeout Functions
  // Language agnostic timeout function (all args passed).
  // |interval| is in milliseconds.
  nsresult SetTimeoutOrInterval(nsIScriptTimeoutHandler *aHandler,
                                int32_t interval,
                                bool aIsInterval, int32_t* aReturn);
  int32_t SetTimeoutOrInterval(JSContext* aCx,
                               mozilla::dom::Function& aFunction,
                               int32_t aTimeout,
                               const mozilla::dom::Sequence<JS::Value>& aArguments,
                               bool aIsInterval, mozilla::ErrorResult& aError);
  int32_t SetTimeoutOrInterval(JSContext* aCx, const nsAString& aHandler,
                               int32_t aTimeout, bool aIsInterval,
                               mozilla::ErrorResult& aError);
  void ClearTimeoutOrInterval(int32_t aTimerID);

  // JS specific timeout functions (JS args grabbed from context).
  nsresult ResetTimersForNonBackgroundWindow();

  // The timeout implementation functions.
  void RunTimeout(nsTimeout *aTimeout);
  void RunTimeout() { RunTimeout(nullptr); }
  // Return true if |aTimeout| was cleared while its handler ran.
  bool RunTimeoutHandler(nsTimeout* aTimeout, nsIScriptContext* aScx);
  // Return true if |aTimeout| needs to be reinserted into the timeout list.
  bool RescheduleTimeout(nsTimeout* aTimeout, const TimeStamp& now,
                         bool aRunningPendingTimeouts);

  void ClearAllTimeouts();
  // Insert aTimeout into the list, before all timeouts that would
  // fire after it, but no earlier than mTimeoutInsertionPoint, if any.
  void InsertTimeoutIntoList(nsTimeout *aTimeout);
  static void TimerCallback(nsITimer *aTimer, void *aClosure);
  static void TimerNameCallback(nsITimer* aTimer, void* aClosure, char* aBuf,
                                size_t aLen);

  // Helper Functions
  already_AddRefed<nsIDocShellTreeOwner> GetTreeOwner();
  already_AddRefed<nsIBaseWindow> GetTreeOwnerWindow();
  already_AddRefed<nsIWebBrowserChrome> GetWebBrowserChrome();
  nsresult SecurityCheckURL(const char *aURL);
  bool IsPrivateBrowsing();

  bool PopupWhitelisted();
  PopupControlState RevisePopupAbuseLevel(PopupControlState);
  void     FireAbuseEvents(const nsAString &aPopupURL,
                           const nsAString &aPopupWindowName,
                           const nsAString &aPopupWindowFeatures);
  void FireOfflineStatusEventIfChanged();

  bool GetIsPrerendered();

  // Inner windows only.
  nsresult ScheduleNextIdleObserverCallback();
  uint32_t GetFuzzTimeMS();
  nsresult ScheduleActiveTimerCallback();
  uint32_t FindInsertionIndex(IdleObserverHolder* aIdleObserver);
  virtual nsresult RegisterIdleObserver(nsIIdleObserver* aIdleObserverPtr) override;
  nsresult FindIndexOfElementToRemove(nsIIdleObserver* aIdleObserver,
                                      int32_t* aRemoveElementIndex);
  virtual nsresult UnregisterIdleObserver(nsIIdleObserver* aIdleObserverPtr) override;

  // Inner windows only.
  nsresult FireHashchange(const nsAString &aOldURL, const nsAString &aNewURL);

  void FlushPendingNotifications(mozFlushType aType);

  // Outer windows only.
  void EnsureReflowFlushAndPaint();
  void CheckSecurityWidthAndHeight(int32_t* width, int32_t* height, bool aCallerIsChrome);
  void CheckSecurityLeftAndTop(int32_t* left, int32_t* top, bool aCallerIsChrome);

  // Outer windows only.
  // Arguments to this function should have values in app units
  void SetCSSViewportWidthAndHeight(nscoord width, nscoord height);
  // Arguments to this function should have values in device pixels
  nsresult SetDocShellWidthAndHeight(int32_t width, int32_t height);

  static bool CanSetProperty(const char *aPrefName);

  static void MakeScriptDialogTitle(nsAString &aOutTitle);

  // Outer windows only.
  bool CanMoveResizeWindows(bool aCallerIsChrome);

  // If aDoFlush is true, we'll flush our own layout; otherwise we'll try to
  // just flush our parent and only flush ourselves if we think we need to.
  // Outer windows only.
  mozilla::CSSPoint GetScrollXY(bool aDoFlush);

  int32_t GetScrollBoundaryOuter(mozilla::Side aSide);

  // Outer windows only.
  nsresult GetInnerSize(mozilla::CSSIntSize& aSize);
  nsIntSize GetOuterSize(mozilla::ErrorResult& aError);
  void SetOuterSize(int32_t aLengthCSSPixels, bool aIsWidth,
                    mozilla::ErrorResult& aError, bool aCallerIsChrome);
  nsRect GetInnerScreenRect();

  void ScrollTo(const mozilla::CSSIntPoint& aScroll,
                const mozilla::dom::ScrollOptions& aOptions);

  bool IsFrame()
  {
    return GetParentInternal() != nullptr;
  }

  // Outer windows only.
  // If aLookForCallerOnJSStack is true, this method will look at the JS stack
  // to determine who the caller is.  If it's false, it'll use |this| as the
  // caller.
  bool WindowExists(const nsAString& aName, bool aLookForCallerOnJSStack);

  already_AddRefed<nsIWidget> GetMainWidget();
  nsIWidget* GetNearestWidget() const;

  void Freeze()
  {
    NS_ASSERTION(!IsFrozen(), "Double-freezing?");
    mIsFrozen = true;
    NotifyDOMWindowFrozen(this);
  }

  void Thaw()
  {
    mIsFrozen = false;
    NotifyDOMWindowThawed(this);
  }

  bool IsInModalState();

  // Convenience functions for the many methods that need to scale
  // from device to CSS pixels or vice versa.  Note: if a presentation
  // context is not available, they will assume a 1:1 ratio.
  int32_t DevToCSSIntPixels(int32_t px);
  int32_t CSSToDevIntPixels(int32_t px);
  nsIntSize DevToCSSIntPixels(nsIntSize px);
  nsIntSize CSSToDevIntPixels(nsIntSize px);

  virtual void SetFocusedNode(nsIContent* aNode,
                              uint32_t aFocusMethod = 0,
                              bool aNeedsFocus = false) override;

  virtual uint32_t GetFocusMethod() override;

  virtual bool ShouldShowFocusRing() override;

  virtual void SetKeyboardIndicators(UIStateChangeType aShowAccelerators,
                                     UIStateChangeType aShowFocusRings) override;
  virtual void GetKeyboardIndicators(bool* aShowAccelerators,
                                     bool* aShowFocusRings) override;

  // Inner windows only.
  void UpdateCanvasFocus(bool aFocusChanged, nsIContent* aNewContent);

public:
  virtual already_AddRefed<nsPIWindowRoot> GetTopWindowRoot() override;

protected:
  static void NotifyDOMWindowDestroyed(nsGlobalWindow* aWindow);
  void NotifyWindowIDDestroyed(const char* aTopic);

  static void NotifyDOMWindowFrozen(nsGlobalWindow* aWindow);
  static void NotifyDOMWindowThawed(nsGlobalWindow* aWindow);

  void ClearStatus();

  virtual void UpdateParentTarget() override;

  inline int32_t DOMMinTimeoutValue() const;

  void InitializeShowFocusRings();

  // Clear the document-dependent slots on our JS wrapper.  Inner windows only.
  void ClearDocumentDependentSlots(JSContext* aCx);

  // Inner windows only.
  already_AddRefed<mozilla::dom::StorageEvent>
  CloneStorageEvent(const nsAString& aType,
                    const RefPtr<mozilla::dom::StorageEvent>& aEvent,
                    mozilla::ErrorResult& aRv);

public:
  // Outer windows only.
  nsDOMWindowList* GetWindowList();

protected:
  // Helper for getComputedStyle and getDefaultComputedStyle
  already_AddRefed<nsICSSDeclaration>
    GetComputedStyleHelperOuter(mozilla::dom::Element& aElt,
                                const nsAString& aPseudoElt,
                                bool aDefaultStylesOnly);
  already_AddRefed<nsICSSDeclaration>
    GetComputedStyleHelper(mozilla::dom::Element& aElt,
                           const nsAString& aPseudoElt,
                           bool aDefaultStylesOnly,
                           mozilla::ErrorResult& aError);
  nsresult GetComputedStyleHelper(nsIDOMElement* aElt,
                                  const nsAString& aPseudoElt,
                                  bool aDefaultStylesOnly,
                                  nsIDOMCSSStyleDeclaration** aReturn);

  // Outer windows only.
  void PreloadLocalStorage();

  // Returns CSS pixels based on primary screen.  Outer windows only.
  mozilla::CSSIntPoint GetScreenXY(mozilla::ErrorResult& aError);

  nsGlobalWindow* InnerForSetTimeoutOrInterval(mozilla::ErrorResult& aError);

  void PostMessageMozOuter(JSContext* aCx, JS::Handle<JS::Value> aMessage,
                           const nsAString& aTargetOrigin,
                           JS::Handle<JS::Value> aTransfer,
                           mozilla::ErrorResult& aError);
  void PostMessageMoz(JSContext* aCx, JS::Handle<JS::Value> aMessage,
                      const nsAString& aTargetOrigin,
                      JS::Handle<JS::Value> aTransfer,
                      mozilla::ErrorResult& aError);

  already_AddRefed<nsIVariant>
    ShowModalDialogOuter(const nsAString& aUrl, nsIVariant* aArgument,
                         const nsAString& aOptions, mozilla::ErrorResult& aError);

  already_AddRefed<nsIVariant>
    ShowModalDialog(const nsAString& aUrl, nsIVariant* aArgument,
                    const nsAString& aOptions, mozilla::ErrorResult& aError);

  // Ask the user if further dialogs should be blocked, if dialogs are currently
  // being abused. This is used in the cases where we have no modifiable UI to
  // show, in that case we show a separate dialog to ask this question.
  bool ConfirmDialogIfNeeded();

  // Helper called after moving/resizing, to update docShell's presContext
  // if we have caused a resolution change by moving across monitors.
  void CheckForDPIChange();

private:
  // Fire the JS engine's onNewGlobalObject hook.  Only used on inner windows.
  void FireOnNewGlobalObject();

  void DisconnectEventTargetObjects();

  // Called only on outer windows to compute the value that will be returned by
  // IsSecureContext() for the inner window that corresponds to aDocument.
  bool ComputeIsSecureContext(nsIDocument* aDocument);

protected:
  // This member is also used on both inner and outer windows, but
  // for slightly different purposes. On inner windows it means the
  // inner window is held onto by session history and should not
  // change. On outer windows it means that the window is in a state
  // where we don't want to force creation of a new inner window since
  // we're in the middle of doing just that.
  bool                          mIsFrozen : 1;

  // These members are only used on outer window objects. Make sure
  // you never set any of these on an inner object!
  bool                          mFullScreen : 1;
  bool                          mFullscreenMode : 1;
  bool                          mIsClosed : 1;
  bool                          mInClose : 1;
  // mHavePendingClose means we've got a termination function set to
  // close us when the JS stops executing or that we have a close
  // event posted.  If this is set, just ignore window.close() calls.
  bool                          mHavePendingClose : 1;
  bool                          mHadOriginalOpener : 1;
  bool                          mOriginalOpenerWasSecureContext : 1;
  bool                          mIsPopupSpam : 1;

  // Indicates whether scripts are allowed to close this window.
  bool                          mBlockScriptedClosingFlag : 1;

  // Window offline status. Checked to see if we need to fire offline event
  bool                          mWasOffline : 1;

  // Represents whether the inner window's page has had a slow script notice.
  // Only used by inner windows; will always be false for outer windows.
  // This is used to implement Telemetry measures such as SLOW_SCRIPT_PAGE_COUNT.
  bool                          mHasHadSlowScript : 1;

  // Track what sorts of events we need to fire when thawed
  bool                          mNotifyIdleObserversIdleOnThaw : 1;
  bool                          mNotifyIdleObserversActiveOnThaw : 1;

  // Indicates whether we're in the middle of creating an initializing
  // a new inner window object.
  bool                          mCreatingInnerWindow : 1;

  // Fast way to tell if this is a chrome window (without having to QI).
  bool                          mIsChrome : 1;

  // Hack to indicate whether a chrome window needs its message manager
  // to be disconnected, since clean up code is shared in the global
  // window superclass.
  bool                          mCleanMessageManager : 1;

  // Indicates that the current document has never received a document focus
  // event.
  bool                   mNeedsFocus : 1;
  bool                   mHasFocus : 1;

  // whether to show keyboard accelerators
  bool                   mShowAccelerators : 1;

  // whether to show focus rings
  bool                   mShowFocusRings : 1;

  // when true, show focus rings for the current focused content only.
  // This will be reset when another element is focused
  bool                   mShowFocusRingForContent : 1;

  // true if tab navigation has occurred for this window. Focus rings
  // should be displayed.
  bool                   mFocusByKeyOccurred : 1;

  // Inner windows only.
  // Indicates whether this window wants gamepad input events
  bool                   mHasGamepad : 1;
#ifdef MOZ_GAMEPAD
  nsCheapSet<nsUint32HashKey> mGamepadIndexSet;
  nsRefPtrHashtable<nsUint32HashKey, mozilla::dom::Gamepad> mGamepads;
  bool mHasSeenGamepadInput;
#endif

  // whether we've sent the destroy notification for our window id
  bool                   mNotifiedIDDestroyed : 1;
  // whether scripts may close the window,
  // even if "dom.allow_scripts_to_close_windows" is false.
  bool                   mAllowScriptsToClose : 1;

  nsCOMPtr<nsIScriptContext>    mContext;
  nsWeakPtr                     mOpener;
  nsCOMPtr<nsIControllers>      mControllers;

  // For |window.arguments|, via |openDialog|.
  nsCOMPtr<nsIArray>            mArguments;

  // For |window.dialogArguments|, via |showModalDialog|.
  RefPtr<DialogValueHolder> mDialogArguments;

  // Only used in the outer.
  RefPtr<DialogValueHolder> mReturnValue;

  RefPtr<mozilla::dom::Navigator> mNavigator;
  RefPtr<nsScreen>            mScreen;
  RefPtr<nsDOMWindowList>     mFrames;
  // All BarProps are inner window only.
  RefPtr<mozilla::dom::BarProp> mMenubar;
  RefPtr<mozilla::dom::BarProp> mToolbar;
  RefPtr<mozilla::dom::BarProp> mLocationbar;
  RefPtr<mozilla::dom::BarProp> mPersonalbar;
  RefPtr<mozilla::dom::BarProp> mStatusbar;
  RefPtr<mozilla::dom::BarProp> mScrollbars;
  RefPtr<nsDOMWindowUtils>    mWindowUtils;
  nsString                      mStatus;
  nsString                      mDefaultStatus;
  RefPtr<nsGlobalWindowObserver> mObserver; // Inner windows only.
  RefPtr<mozilla::dom::Crypto>  mCrypto;
  RefPtr<mozilla::dom::U2F> mU2F;
  RefPtr<mozilla::dom::cache::CacheStorage> mCacheStorage;
  RefPtr<mozilla::dom::Console> mConsole;
  // We need to store an nsISupports pointer to this object because the
  // mozilla::dom::External class doesn't exist on b2g and using the type
  // forward declared here means that ~nsGlobalWindow wouldn't compile because
  // it wouldn't see the ~External function's declaration.
  nsCOMPtr<nsISupports>         mExternal;

  RefPtr<mozilla::dom::MozSelfSupport> mMozSelfSupport;

  RefPtr<mozilla::dom::DOMStorage> mLocalStorage;
  RefPtr<mozilla::dom::DOMStorage> mSessionStorage;

  // These member variable are used only on inner windows.
  RefPtr<mozilla::EventListenerManager> mListenerManager;
  // mTimeouts is generally sorted by mWhen, unless mTimeoutInsertionPoint is
  // non-null.  In that case, the dummy timeout pointed to by
  // mTimeoutInsertionPoint may have a later mWhen than some of the timeouts
  // that come after it.
  mozilla::LinkedList<nsTimeout> mTimeouts;
  // If mTimeoutInsertionPoint is non-null, insertions should happen after it.
  // This is a dummy timeout at the moment; if that ever changes, the logic in
  // ResetTimersForNonBackgroundWindow needs to change.
  nsTimeout*                    mTimeoutInsertionPoint;
  uint32_t                      mTimeoutPublicIdCounter;
  uint32_t                      mTimeoutFiringDepth;
  RefPtr<nsLocation>          mLocation;
  RefPtr<nsHistory>           mHistory;

  // These member variables are used on both inner and the outer windows.
  nsCOMPtr<nsIPrincipal> mDocumentPrincipal;

  typedef nsTArray<RefPtr<mozilla::dom::StorageEvent>> nsDOMStorageEventArray;
  nsDOMStorageEventArray mPendingStorageEvents;

  uint32_t mTimeoutsSuspendDepth;

  // the method that was used to focus mFocusedNode
  uint32_t mFocusMethod;

  uint32_t mSerial;

#ifdef DEBUG
  bool mSetOpenerWindowCalled;
  nsCOMPtr<nsIURI> mLastOpenedURI;
#endif

#ifdef MOZ_B2G
  bool mNetworkUploadObserverEnabled;
  bool mNetworkDownloadObserverEnabled;
#endif // MOZ_B2G

  bool mCleanedUp;

  nsCOMPtr<nsIDOMOfflineResourceList> mApplicationCache;

  nsAutoPtr<nsJSThingHashtable<nsPtrHashKey<nsXBLPrototypeHandler>, JSObject*> > mCachedXBLPrototypeHandlers;

  // mSuspendedDoc is only set on outer windows. It's useful when we get matched
  // EnterModalState/LeaveModalState calls, in which case the outer window is
  // responsible for unsuspending events on the document. If we don't (for
  // example, if the outer window is closed before the LeaveModalState call),
  // then the inner window whose mDoc is our mSuspendedDoc is responsible for
  // unsuspending it.
  nsCOMPtr<nsIDocument> mSuspendedDoc;

  RefPtr<mozilla::dom::IDBFactory> mIndexedDB;

  // This counts the number of windows that have been opened in rapid succession
  // (i.e. within dom.successive_dialog_time_limit of each other). It is reset
  // to 0 once a dialog is opened after dom.successive_dialog_time_limit seconds
  // have elapsed without any other dialogs.
  uint32_t                      mDialogAbuseCount;

  // This holds the time when the last modal dialog was shown. If more than
  // MAX_DIALOG_LIMIT dialogs are shown within the time span defined by
  // dom.successive_dialog_time_limit, we show a checkbox or confirmation prompt
  // to allow disabling of further dialogs from this window.
  TimeStamp                     mLastDialogQuitTime;

  // This flag keeps track of whether dialogs are
  // currently enabled on this window.
  bool                          mAreDialogsEnabled;

  nsTHashtable<nsPtrHashKey<mozilla::DOMEventTargetHelper> > mEventTargetObjects;

  nsTArray<uint32_t> mEnabledSensors;

#if defined(MOZ_WIDGET_ANDROID) || defined(MOZ_WIDGET_GONK)
  nsAutoPtr<mozilla::dom::WindowOrientationObserver> mOrientationChangeObserver;
#endif

#ifdef MOZ_WEBSPEECH
  // mSpeechSynthesis is only used on inner windows.
  RefPtr<mozilla::dom::SpeechSynthesis> mSpeechSynthesis;
#endif

  // This is the CC generation the last time we called CanSkip.
  uint32_t mCanSkipCCGeneration;

  // The VRDevies for this window
  nsTArray<RefPtr<mozilla::dom::VRDevice>> mVRDevices;

  friend class nsDOMScriptableHelper;
  friend class nsDOMWindowUtils;
  friend class mozilla::dom::PostMessageEvent;
  friend class DesktopNotification;

  static WindowByIdTable* sWindowsById;
  static bool sWarnedAboutWindowInternal;
};

inline nsISupports*
ToSupports(nsGlobalWindow *p)
{
    return static_cast<nsIDOMEventTarget*>(p);
}

inline nsISupports*
ToCanonicalSupports(nsGlobalWindow *p)
{
    return static_cast<nsIDOMEventTarget*>(p);
}

/*
 * nsGlobalChromeWindow inherits from nsGlobalWindow. It is the global
 * object created for a Chrome Window only.
 */
class nsGlobalChromeWindow : public nsGlobalWindow,
                             public nsIDOMChromeWindow
{
public:
  // nsISupports
  NS_DECL_ISUPPORTS_INHERITED

  // nsIDOMChromeWindow interface
  NS_DECL_NSIDOMCHROMEWINDOW

  static already_AddRefed<nsGlobalChromeWindow> Create(nsGlobalWindow *aOuterWindow);

  void DisconnectAndClearGroupMessageManagers()
  {
    for (auto iter = mGroupMessageManagers.Iter(); !iter.Done(); iter.Next()) {
      nsIMessageBroadcaster* mm = iter.UserData();
      if (mm) {
        static_cast<nsFrameMessageManager*>(mm)->Disconnect();
      }
    }
    mGroupMessageManagers.Clear();
  }

protected:
  explicit nsGlobalChromeWindow(nsGlobalWindow *aOuterWindow)
    : nsGlobalWindow(aOuterWindow),
      mGroupMessageManagers(1)
  {
    mIsChrome = true;
    mCleanMessageManager = true;
  }

  ~nsGlobalChromeWindow()
  {
    MOZ_ASSERT(mCleanMessageManager,
               "chrome windows may always disconnect the msg manager");

    DisconnectAndClearGroupMessageManagers();

    if (mMessageManager) {
      static_cast<nsFrameMessageManager *>(
        mMessageManager.get())->Disconnect();
    }

    mCleanMessageManager = false;
  }

public:
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(nsGlobalChromeWindow,
                                           nsGlobalWindow)

  using nsGlobalWindow::GetBrowserDOMWindow;
  using nsGlobalWindow::SetBrowserDOMWindow;
  using nsGlobalWindow::GetAttention;
  using nsGlobalWindow::GetAttentionWithCycleCount;
  using nsGlobalWindow::SetCursor;
  using nsGlobalWindow::Maximize;
  using nsGlobalWindow::Minimize;
  using nsGlobalWindow::Restore;
  using nsGlobalWindow::NotifyDefaultButtonLoaded;
  using nsGlobalWindow::GetMessageManager;
  using nsGlobalWindow::GetGroupMessageManager;
  using nsGlobalWindow::BeginWindowMove;

  nsCOMPtr<nsIBrowserDOMWindow> mBrowserDOMWindow;
  nsCOMPtr<nsIMessageBroadcaster> mMessageManager;
  nsInterfaceHashtable<nsStringHashKey, nsIMessageBroadcaster> mGroupMessageManagers;
};

/*
 * nsGlobalModalWindow inherits from nsGlobalWindow. It is the global
 * object created for a modal content windows only (i.e. not modal
 * chrome dialogs).
 */
class nsGlobalModalWindow : public nsGlobalWindow,
                            public nsIDOMModalContentWindow
{
public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIDOMMODALCONTENTWINDOW

  static already_AddRefed<nsGlobalModalWindow> Create(nsGlobalWindow *aOuterWindow);

protected:
  explicit nsGlobalModalWindow(nsGlobalWindow *aOuterWindow)
    : nsGlobalWindow(aOuterWindow)
  {
    mIsModalContentWindow = true;
  }

  ~nsGlobalModalWindow() {}
};

/* factory function */
inline already_AddRefed<nsGlobalWindow>
NS_NewScriptGlobalObject(bool aIsChrome, bool aIsModalContentWindow)
{
  RefPtr<nsGlobalWindow> global;

  if (aIsChrome) {
    global = nsGlobalChromeWindow::Create(nullptr);
  } else if (aIsModalContentWindow) {
    global = nsGlobalModalWindow::Create(nullptr);
  } else {
    global = nsGlobalWindow::Create(nullptr);
  }

  return global.forget();
}

#endif /* nsGlobalWindow_h___ */
