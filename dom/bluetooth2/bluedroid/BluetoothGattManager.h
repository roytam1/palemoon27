/* -*- Mode: c++; c-basic-offset: 2; indent-tabs-mode: nil; tab-width: 40 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_bluetooth_bluetoothgattmanager_h__
#define mozilla_dom_bluetooth_bluetoothgattmanager_h__

#include "BluetoothCommon.h"
#include "BluetoothInterface.h"
#include "BluetoothProfileManagerBase.h"

BEGIN_BLUETOOTH_NAMESPACE

class BluetoothReplyRunnable;

class BluetoothGattManager final : public nsIObserver
                                 , public BluetoothGattNotificationHandler
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  static BluetoothGattManager* Get();
  static void InitGattInterface(BluetoothProfileResultHandler* aRes);
  static void DeinitGattInterface(BluetoothProfileResultHandler* aRes);
  virtual ~BluetoothGattManager();

  void Connect(const nsAString& aAppUuid,
               const nsAString& aDeviceAddr,
               BluetoothReplyRunnable* aRunnable);

  void Disconnect(const nsAString& aAppUuid,
                  const nsAString& aDeviceAddr,
                  BluetoothReplyRunnable* aRunnable);

  void UnregisterClient(int aClientIf,
                        BluetoothReplyRunnable* aRunnable);

private:
  class CleanupResultHandler;
  class CleanupResultHandlerRunnable;
  class InitGattResultHandler;
  class RegisterClientResultHandler;
  class UnregisterClientResultHandler;
  class ConnectResultHandler;
  class DisconnectResultHandler;

  BluetoothGattManager();

  void HandleShutdown();

  void RegisterClientNotification(int aStatus,
                                  int aClientIf,
                                  const BluetoothUuid& aAppUuid) override;

  void ScanResultNotification(
    const nsAString& aBdAddr, int aRssi,
    const BluetoothGattAdvData& aAdvData) override;

  void ConnectNotification(int aConnId,
                           int aStatus,
                           int aClientIf,
                           const nsAString& aBdAddr) override;

  void DisconnectNotification(int aConnId,
                              int aStatus,
                              int aClientIf,
                              const nsAString& aBdAddr) override;

  void SearchCompleteNotification(int aConnId, int aStatus) override;

  void SearchResultNotification(int aConnId,
                                const BluetoothGattServiceId& aServiceId)
                                override;

  void GetCharacteristicNotification(
    int aConnId, int aStatus,
    const BluetoothGattServiceId& aServiceId,
    const BluetoothGattId& aCharId,
    int aCharProperty) override;

  void GetDescriptorNotification(
    int aConnId, int aStatus,
    const BluetoothGattServiceId& aServiceId,
    const BluetoothGattId& aCharId,
    const BluetoothGattId& aDescriptorId) override;

  void GetIncludedServiceNotification(
    int aConnId, int aStatus,
    const BluetoothGattServiceId& aServiceId,
    const BluetoothGattServiceId& aIncludedServId) override;

  void RegisterNotificationNotification(
    int aConnId, int aIsRegister, int aStatus,
    const BluetoothGattServiceId& aServiceId,
    const BluetoothGattId& aCharId) override;

  void NotifyNotification(int aConnId,
                          const BluetoothGattNotifyParam& aNotifyParam)
                          override;

  void ReadCharacteristicNotification(int aConnId,
                                      int aStatus,
                                      const BluetoothGattReadParam& aReadParam)
                                      override;

  void WriteCharacteristicNotification(
    int aConnId, int aStatus,
    const BluetoothGattWriteParam& aWriteParam) override;

  void ReadDescriptorNotification(int aConnId,
                                  int aStatus,
                                  const BluetoothGattReadParam& aReadParam)
                                  override;

  void WriteDescriptorNotification(int aConnId,
                                   int aStatus,
                                   const BluetoothGattWriteParam& aWriteParam)
                                   override;

  void ExecuteWriteNotification(int aConnId, int aStatus) override;

  void ReadRemoteRssiNotification(int aClientIf,
                                  const nsAString& aBdAddr,
                                  int aRssi,
                                  int aStatus) override;

  void ListenNotification(int aStatus, int aServerIf) override;

  static bool mInShutdown;
};

END_BLUETOOTH_NAMESPACE

#endif
