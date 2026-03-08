#pragma once
#define min // workaround: nimble's min/max macros conflict with libstdc++
#define max
#include <host/ble_gap.h>
#undef max
#undef min
#include <cstdint>

namespace Pinetime {
  namespace Controllers {
    class HeartRateController;
    class NimbleController;

    class HeartRateHistoryService {
    public:
      HeartRateHistoryService(NimbleController& nimble, Controllers::HeartRateController& heartRateController);
      void Init();

      int OnRequest(uint16_t attributeHandle, ble_gatt_access_ctxt* context);
      void OnNewHistoryEntry();

      void SubscribeNotification(uint16_t attributeHandle);
      void UnsubscribeNotification(uint16_t attributeHandle);

    private:
      NimbleController& nimble;
      Controllers::HeartRateController& heartRateController;

      uint16_t readIndex = 0;

      static constexpr ble_uuid128_t serviceUuid = {
        .u = {.type = BLE_UUID_TYPE_128},
        .value = {0x89, 0x67, 0x45, 0x23, 0x01, 0xef,
                  0xcd, 0xab, 0x78, 0x56, 0x34, 0x12,
                  0xdd, 0xcc, 0xbb, 0xaa}
      };

      static constexpr ble_uuid128_t historyEntryUuid = {
        .u = {.type = BLE_UUID_TYPE_128},
        .value = {0x89, 0x67, 0x45, 0x23, 0x01, 0xef,
                  0xcd, 0xab, 0x78, 0x56, 0x34, 0x12,
                  0x01, 0x00, 0xbb, 0xaa}
      };

      static constexpr ble_uuid128_t entryCountUuid = {
        .u = {.type = BLE_UUID_TYPE_128},
        .value = {0x89, 0x67, 0x45, 0x23, 0x01, 0xef,
                  0xcd, 0xab, 0x78, 0x56, 0x34, 0x12,
                  0x02, 0x00, 0xbb, 0xaa}
      };

      static constexpr ble_uuid128_t readIndexUuid = {
        .u = {.type = BLE_UUID_TYPE_128},
        .value = {0x89, 0x67, 0x45, 0x23, 0x01, 0xef,
                  0xcd, 0xab, 0x78, 0x56, 0x34, 0x12,
                  0x03, 0x00, 0xbb, 0xaa}
      };

      struct ble_gatt_chr_def characteristicDefinition[4];
      struct ble_gatt_svc_def serviceDefinition[2];

      uint16_t historyEntryHandle;
      uint16_t entryCountHandle;
      uint16_t readIndexHandle;

      bool entryCountNotificationEnabled = false;
    };
  }
}
