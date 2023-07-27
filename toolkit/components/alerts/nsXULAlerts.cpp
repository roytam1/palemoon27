/* -*- Mode: C++; tab-width: 2; indent-tabs-mode:nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsXULAlerts.h"

#include "nsAutoPtr.h"
#include "mozilla/LookAndFeel.h"
#include "mozilla/dom/Notification.h"
#include "mozilla/unused.h"
#include "nsIServiceManager.h"
#include "nsAlertsUtils.h"
#include "nsISupportsArray.h"
#include "nsISupportsPrimitives.h"
#include "nsPIDOMWindow.h"
#include "nsIWindowWatcher.h"

using namespace mozilla;
using mozilla::dom::NotificationTelemetryService;

#define ALERT_CHROME_URL "chrome://global/content/alerts/alert.xul"

NS_IMPL_ISUPPORTS(nsXULAlertObserver, nsIObserver)

NS_IMETHODIMP
nsXULAlertObserver::Observe(nsISupports* aSubject, const char* aTopic,
                            const char16_t* aData)
{
  if (!strcmp("alertfinished", aTopic)) {
    nsIDOMWindow* currentAlert = mXULAlerts->mNamedWindows.GetWeak(mAlertName);
    // The window in mNamedWindows might be a replacement, thus it should only
    // be removed if it is the same window that is associated with this listener.
    if (currentAlert == mAlertWindow) {
      mXULAlerts->mNamedWindows.Remove(mAlertName);
    }
  }

  nsresult rv = NS_OK;
  if (mObserver) {
    rv = mObserver->Observe(aSubject, aTopic, aData);
  }
  return rv;
}

nsresult
nsXULAlerts::ShowAlertNotification(const nsAString& aImageUrl, const nsAString& aAlertTitle,
                                   const nsAString& aAlertText, bool aAlertTextClickable,
                                   const nsAString& aAlertCookie, nsIObserver* aAlertListener,
                                   const nsAString& aAlertName, const nsAString& aBidi,
                                   const nsAString& aLang, nsIPrincipal* aPrincipal,
                                   bool aInPrivateBrowsing)
{
  if (mDoNotDisturb) {
    if (!aInPrivateBrowsing) {
      RefPtr<NotificationTelemetryService> telemetry =
        NotificationTelemetryService::GetInstance();
      if (telemetry) {
        // Record the number of unique senders for XUL alerts. The OS X and
        // libnotify backends will fire `alertshow` even if "do not disturb"
        // is enabled. In that case, `NotificationObserver` will record the
        // sender.
        Unused << NS_WARN_IF(NS_FAILED(telemetry->RecordSender(aPrincipal)));
      }
    }
    return NS_OK;
  }

  nsCOMPtr<nsIWindowWatcher> wwatch(do_GetService(NS_WINDOWWATCHER_CONTRACTID));

  nsCOMPtr<nsISupportsArray> argsArray;
  nsresult rv = NS_NewISupportsArray(getter_AddRefs(argsArray));
  NS_ENSURE_SUCCESS(rv, rv);

  // create scriptable versions of our strings that we can store in our nsISupportsArray....
  nsCOMPtr<nsISupportsString> scriptableImageUrl (do_CreateInstance(NS_SUPPORTS_STRING_CONTRACTID));
  NS_ENSURE_TRUE(scriptableImageUrl, NS_ERROR_FAILURE);

  scriptableImageUrl->SetData(aImageUrl);
  rv = argsArray->AppendElement(scriptableImageUrl);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsISupportsString> scriptableAlertTitle (do_CreateInstance(NS_SUPPORTS_STRING_CONTRACTID));
  NS_ENSURE_TRUE(scriptableAlertTitle, NS_ERROR_FAILURE);

  scriptableAlertTitle->SetData(aAlertTitle);
  rv = argsArray->AppendElement(scriptableAlertTitle);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsISupportsString> scriptableAlertText (do_CreateInstance(NS_SUPPORTS_STRING_CONTRACTID));
  NS_ENSURE_TRUE(scriptableAlertText, NS_ERROR_FAILURE);

  scriptableAlertText->SetData(aAlertText);
  rv = argsArray->AppendElement(scriptableAlertText);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsISupportsPRBool> scriptableIsClickable (do_CreateInstance(NS_SUPPORTS_PRBOOL_CONTRACTID));
  NS_ENSURE_TRUE(scriptableIsClickable, NS_ERROR_FAILURE);

  scriptableIsClickable->SetData(aAlertTextClickable);
  rv = argsArray->AppendElement(scriptableIsClickable);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsISupportsString> scriptableAlertCookie (do_CreateInstance(NS_SUPPORTS_STRING_CONTRACTID));
  NS_ENSURE_TRUE(scriptableAlertCookie, NS_ERROR_FAILURE);

  scriptableAlertCookie->SetData(aAlertCookie);
  rv = argsArray->AppendElement(scriptableAlertCookie);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsISupportsPRInt32> scriptableOrigin (do_CreateInstance(NS_SUPPORTS_PRINT32_CONTRACTID));
  NS_ENSURE_TRUE(scriptableOrigin, NS_ERROR_FAILURE);

  int32_t origin =
    LookAndFeel::GetInt(LookAndFeel::eIntID_AlertNotificationOrigin);
  scriptableOrigin->SetData(origin);

  rv = argsArray->AppendElement(scriptableOrigin);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsISupportsString> scriptableBidi (do_CreateInstance(NS_SUPPORTS_STRING_CONTRACTID));
  NS_ENSURE_TRUE(scriptableBidi, NS_ERROR_FAILURE);

  scriptableBidi->SetData(aBidi);
  rv = argsArray->AppendElement(scriptableBidi);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsISupportsString> scriptableLang (do_CreateInstance(NS_SUPPORTS_STRING_CONTRACTID));
  NS_ENSURE_TRUE(scriptableLang, NS_ERROR_FAILURE);

  scriptableLang->SetData(aLang);
  rv = argsArray->AppendElement(scriptableLang);
  NS_ENSURE_SUCCESS(rv, rv);

  // Alerts with the same name should replace the old alert in the same position.
  // Provide the new alert window with a pointer to the replaced window so that
  // it may take the same position.
  nsCOMPtr<nsISupportsInterfacePointer> replacedWindow = do_CreateInstance(NS_SUPPORTS_INTERFACE_POINTER_CONTRACTID, &rv);
  NS_ENSURE_TRUE(replacedWindow, NS_ERROR_FAILURE);
  nsIDOMWindow* previousAlert = mNamedWindows.GetWeak(aAlertName);
  replacedWindow->SetData(previousAlert);
  replacedWindow->SetDataIID(&NS_GET_IID(nsIDOMWindow));
  rv = argsArray->AppendElement(replacedWindow);
  NS_ENSURE_SUCCESS(rv, rv);

  // Add an observer (that wraps aAlertListener) to remove the window from
  // mNamedWindows when it is closed.
  nsCOMPtr<nsISupportsInterfacePointer> ifptr = do_CreateInstance(NS_SUPPORTS_INTERFACE_POINTER_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  RefPtr<nsXULAlertObserver> alertObserver = new nsXULAlertObserver(this, aAlertName, aAlertListener);
  nsCOMPtr<nsISupports> iSupports(do_QueryInterface(alertObserver));
  ifptr->SetData(iSupports);
  ifptr->SetDataIID(&NS_GET_IID(nsIObserver));
  rv = argsArray->AppendElement(ifptr);
  NS_ENSURE_SUCCESS(rv, rv);

  // The source contains the host and port of the site that sent the
  // notification. It is empty for system alerts.
  nsCOMPtr<nsISupportsString> scriptableAlertSource (do_CreateInstance(NS_SUPPORTS_STRING_CONTRACTID));
  NS_ENSURE_TRUE(scriptableAlertSource, NS_ERROR_FAILURE);
  nsAutoString source;
  nsAlertsUtils::GetSourceHostPort(aPrincipal, source);
  scriptableAlertSource->SetData(source);
  rv = argsArray->AppendElement(scriptableAlertSource);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIDOMWindow> newWindow;
  nsAutoCString features("chrome,dialog=yes,titlebar=no,popup=yes");
  if (aInPrivateBrowsing) {
    features.AppendLiteral(",private");
  }
  rv = wwatch->OpenWindow(0, ALERT_CHROME_URL, "_blank", features.get(),
                          argsArray, getter_AddRefs(newWindow));
  NS_ENSURE_SUCCESS(rv, rv);

  mNamedWindows.Put(aAlertName, newWindow);
  alertObserver->SetAlertWindow(newWindow);

  return NS_OK;
}

nsresult
nsXULAlerts::SetManualDoNotDisturb(bool aDoNotDisturb)
{
  mDoNotDisturb = aDoNotDisturb;
  return NS_OK;
}

nsresult
nsXULAlerts::GetManualDoNotDisturb(bool* aRetVal)
{
  *aRetVal = mDoNotDisturb;
  return NS_OK;
}

nsresult
nsXULAlerts::CloseAlert(const nsAString& aAlertName)
{
  nsIDOMWindow* alert = mNamedWindows.GetWeak(aAlertName);
  nsCOMPtr<nsPIDOMWindow> domWindow = do_QueryInterface(alert);
  if (domWindow) {
    MOZ_ASSERT(domWindow->IsOuterWindow());
    domWindow->DispatchCustomEvent(NS_LITERAL_STRING("XULAlertClose"));
  }
  return NS_OK;
}

