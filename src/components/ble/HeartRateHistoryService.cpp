#include "components/ble/HeartRateHistoryService.h"
#include "components/heartrate/HeartRateController.h"
#include "components/ble/NimbleController.h"
#include <nrf_log.h>

using namespace Pinetime::Controllers;

constexpr ble_uuid128_t HeartRateHistoryService::serviceUuid;
constexpr ble_uuid128_t HeartRateHistoryService::historyEntryUuid;
constexpr ble_uuid128_t HeartRateHistoryService::entryCountUuid;
constexpr ble_uuid128_t HeartRateHistoryService::readIndexUuid;

namespace {
  int HistoryServiceCallback(uint16_t /*conn_handle*/,
                             uint16_t attr_handle,
                             struct ble_gatt_access_ctxt* ctxt,
                             void* arg) {
    auto* service = static_cast<HeartRateHistoryService*>(arg);
    return service->OnRequest(attr_handle, ctxt);
  }
}

HeartRateHistoryService::HeartRateHistoryService(
  NimbleController& nimble,
  Controllers::HeartRateController& heartRateController)
  : nimble {nimble},
    heartRateController {heartRateController},
    characteristicDefinition {
      {.uuid = &historyEntryUuid.u,
       .access_cb = HistoryServiceCallback,
       .arg = this,
       .flags = BLE_GATT_CHR_F_READ,
       .val_handle = &historyEntryHandle},
      {.uuid = &entryCountUuid.u,
       .access_cb = HistoryServiceCallback,
       .arg = this,
       .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
       .val_handle = &entryCountHandle},
      {.uuid = &readIndexUuid.u,
       .access_cb = HistoryServiceCallback,
       .arg = this,
       .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE,
       .val_handle = &readIndexHandle},
      {0}},
    serviceDefinition {
      {.type = BLE_GATT_SVC_TYPE_PRIMARY,
       .uuid = &serviceUuid.u,
       .characteristics = characteristicDefinition},
      {0}} {
}

void HeartRateHistoryService::Init() {
  int res = ble_gatts_count_cfg(serviceDefinition);
  ASSERT(res == 0);
  res = ble_gatts_add_svcs(serviceDefinition);
  ASSERT(res == 0);
}

int HeartRateHistoryService::OnRequest(uint16_t attributeHandle,
                                       ble_gatt_access_ctxt* context) {
  if (attributeHandle == historyEntryHandle) {
    auto entry = heartRateController.GetHistory().GetEntry(readIndex);
    int res = os_mbuf_append(context->om,
                             reinterpret_cast<uint8_t*>(&entry),
                             sizeof(entry));
    return (res == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
  }

  if (attributeHandle == entryCountHandle) {
    uint16_t count = heartRateController.GetHistory().EntryCount();
    int res = os_mbuf_append(context->om,
                             reinterpret_cast<uint8_t*>(&count),
                             sizeof(count));
    return (res == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
  }

  if (attributeHandle == readIndexHandle) {
    if (context->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
      uint16_t newIndex = 0;
      os_mbuf_copydata(context->om, 0, sizeof(newIndex), &newIndex);
      readIndex = newIndex;
      return 0;
    }
    int res = os_mbuf_append(context->om,
                             reinterpret_cast<uint8_t*>(&readIndex),
                             sizeof(readIndex));
    return (res == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
  }

  return 0;
}

void HeartRateHistoryService::OnNewHistoryEntry() {
  if (!entryCountNotificationEnabled) {
    return;
  }

  uint16_t count = heartRateController.GetHistory().EntryCount();
  auto* om = ble_hs_mbuf_from_flat(
    reinterpret_cast<uint8_t*>(&count), sizeof(count));

  uint16_t connectionHandle = nimble.connHandle();
  if (connectionHandle == 0 || connectionHandle == BLE_HS_CONN_HANDLE_NONE) {
    return;
  }

  ble_gattc_notify_custom(connectionHandle, entryCountHandle, om);
}

void HeartRateHistoryService::SubscribeNotification(uint16_t attributeHandle) {
  if (attributeHandle == entryCountHandle) {
    entryCountNotificationEnabled = true;
  }
}

void HeartRateHistoryService::UnsubscribeNotification(uint16_t attributeHandle) {
  if (attributeHandle == entryCountHandle) {
    entryCountNotificationEnabled = false;
  }
}
