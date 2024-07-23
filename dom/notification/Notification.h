/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_notification_h__
#define mozilla_dom_notification_h__

#include "mozilla/DOMEventTargetHelper.h"
#include "mozilla/dom/NotificationBinding.h"

#include "nsIObserver.h"

#include "nsCycleCollectionParticipant.h"

class nsIPrincipal;
class nsIStructuredCloneContainer;
class nsIVariant;

namespace mozilla {
namespace dom {


class NotificationObserver;
class Promise;

class Notification : public DOMEventTargetHelper
{
  friend class NotificationTask;
  friend class NotificationPermissionRequest;
  friend class NotificationObserver;
  friend class NotificationStorageCallback;

public:
  IMPL_EVENT_HANDLER(click)
  IMPL_EVENT_HANDLER(show)
  IMPL_EVENT_HANDLER(error)
  IMPL_EVENT_HANDLER(close)

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_CLASS_INHERITED(Notification, DOMEventTargetHelper)

  static already_AddRefed<Notification> Constructor(const GlobalObject& aGlobal,
                                                    const nsAString& aTitle,
                                                    const NotificationOptions& aOption,
                                                    ErrorResult& aRv);
  void GetID(nsAString& aRetval) {
    aRetval = mID;
  }

  void GetTitle(nsAString& aRetval)
  {
    aRetval = mTitle;
  }

  NotificationDirection Dir()
  {
    return mDir;
  }

  void GetLang(nsAString& aRetval)
  {
    aRetval = mLang;
  }

  void GetBody(nsAString& aRetval)
  {
    aRetval = mBody;
  }

  void GetTag(nsAString& aRetval)
  {
    aRetval = mTag;
  }

  void GetIcon(nsAString& aRetval)
  {
    aRetval = mIconUrl;
  }

  void SetStoredState(bool val)
  {
    mIsStored = val;
  }

  bool IsStored()
  {
    return mIsStored;
  }

  nsIStructuredCloneContainer* GetDataCloneContainer();

  static void RequestPermission(const GlobalObject& aGlobal,
                                const Optional<OwningNonNull<NotificationPermissionCallback> >& aCallback,
                                ErrorResult& aRv);

  static NotificationPermission GetPermission(const GlobalObject& aGlobal,
                                              ErrorResult& aRv);

  static already_AddRefed<Promise> Get(const GlobalObject& aGlobal,
                                       const GetNotificationOptions& aFilter,
                                       ErrorResult& aRv);

  void Close();

  nsPIDOMWindow* GetParentObject()
  {
    return GetOwner();
  }

  virtual JSObject* WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto) override;

  void GetData(JSContext* aCx, JS::MutableHandle<JS::Value> aRetval);

  void InitFromJSVal(JSContext* aCx, JS::Handle<JS::Value> aData, ErrorResult& aRv);

  void InitFromBase64(JSContext* aCx, const nsAString& aData, ErrorResult& aRv);

protected:
  Notification(const nsAString& aID, const nsAString& aTitle, const nsAString& aBody,
               NotificationDirection aDir, const nsAString& aLang,
               const nsAString& aTag, const nsAString& aIconUrl,
               const NotificationBehavior& aBehavior, nsPIDOMWindow* aWindow);

  static already_AddRefed<Notification> CreateInternal(nsPIDOMWindow* aWindow,
                                                       const nsAString& aID,
                                                       const nsAString& aTitle,
                                                       const NotificationOptions& aOptions);

  void ShowInternal();
  void CloseInternal();

  static NotificationPermission GetPermissionInternal(nsISupports* aGlobal,
                                                      ErrorResult& rv);

  static const nsString DirectionToString(NotificationDirection aDirection)
  {
    switch (aDirection) {
    case NotificationDirection::Ltr:
      return NS_LITERAL_STRING("ltr");
    case NotificationDirection::Rtl:
      return NS_LITERAL_STRING("rtl");
    default:
      return NS_LITERAL_STRING("auto");
    }
  }

  static const NotificationDirection StringToDirection(const nsAString& aDirection)
  {
    if (aDirection.EqualsLiteral("ltr")) {
      return NotificationDirection::Ltr;
    }
    if (aDirection.EqualsLiteral("rtl")) {
      return NotificationDirection::Rtl;
    }
    return NotificationDirection::Auto;
  }

  static nsresult GetOrigin(nsPIDOMWindow* aWindow, nsString& aOrigin);

  void GetAlertName(nsAString& aRetval)
  {
    aRetval = mAlertName;
  }

  nsString mID;
  nsString mTitle;
  nsString mBody;
  NotificationDirection mDir;
  nsString mLang;
  nsString mTag;
  nsString mIconUrl;
  nsCOMPtr<nsIStructuredCloneContainer> mDataObjectContainer;
  NotificationBehavior mBehavior;

  // It's null until GetData is first called
  nsCOMPtr<nsIVariant> mData;

  nsString mAlertName;

  bool mIsClosed;

  // We need to make a distinction between the notification being closed i.e.
  // removed from any pending or active lists, and the notification being
  // removed from the database. NotificationDB might fail when trying to remove
  // the notification.
  bool mIsStored;

  static uint32_t sCount;

private:
  virtual ~Notification();

  nsIPrincipal* GetPrincipal();
};

} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_notification_h__

