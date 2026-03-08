# Continuous Heart Rate Monitoring

## Introduction

Continuous heart rate monitoring keeps the HRS3300 sensor running at all times so a current BPM reading is always available without waiting for a measurement cycle. Historical data is stored in 5-minute interval buckets on flash, viewable on-device as a line chart and retrievable over BLE.

## Enabling

Open **Settings > Heart Rate** and select **Cont**. This is the same setting that already existed — it now activates true continuous measurement.

When disabled (or set to any interval like 30s, 1m, etc.), the system reverts to the original behavior: measurements only run when the HR app is open or at the configured background interval.

## How It Works

### Measurement

The `HeartRateTask` has two new states:

| State | Sensor | When |
|-------|--------|------|
| `ContinuousMeasuring` | On | Continuous mode active, skin contact detected |
| `ContinuousPaused` | Off | Continuous mode active, no skin contact |

In `ContinuousMeasuring`, the sensor polls identically to `ForegroundMeasuring` — the PPG algorithm processes samples at 10 Hz, producing a BPM reading every ~6.4 seconds. The difference is that sleep/wake transitions do not stop the sensor.

When the PPG detects an ambient light spike (watch removed from wrist), the sensor powers down and the task enters `ContinuousPaused`. After 30 seconds it re-enables the sensor and retries. This loop continues until contact is restored.

### Instant Display

When continuous mode is active and the HR app is opened, the most recent BPM value is displayed immediately — no "Measuring..." wait. A freshness label shows how recently the reading was taken:

- "just now" (< 5 seconds)
- "12 s ago" (< 60 seconds)
- "3 min ago" (>= 60 seconds)

The Start/Stop button is hidden in continuous mode since measurement is managed by the background setting.

### History Storage

Every BPM reading is accumulated into 5-minute buckets. At each bucket boundary, the system writes a summary to `/hr_history.dat` on the external SPI flash.

**Storage format:**

```
Offset 0:     Header (8 bytes)
Offset 8:     Entry[0] (8 bytes)
Offset 16:    Entry[1] (8 bytes)
...
Offset 4616:  Entry[575] (8 bytes)
Total: 4624 bytes
```

**Header (8 bytes):**

| Offset | Type | Field |
|--------|------|-------|
| 0 | uint32_t | version (currently `1`) |
| 4 | uint16_t | writeIndex (next write position in ring) |
| 6 | uint16_t | count (total valid entries, max 576) |

**Entry (8 bytes):**

| Offset | Type | Field |
|--------|------|-------|
| 0 | uint32_t | timestamp (UNIX epoch seconds, start of 5-min window) |
| 4 | uint8_t | avgBpm (average BPM for the interval) |
| 5 | uint8_t | minBpm (minimum BPM) |
| 6 | uint8_t | maxBpm (maximum BPM) |
| 7 | uint8_t | flags (bit 0: valid data) |

The buffer is circular. When all 576 slots are full, the oldest entry is overwritten. 576 entries at 5-minute intervals = 48 hours of history.

If the sensor has no valid readings during a 5-minute interval (watch removed), the entry is written with `flags = 0` and all BPM fields set to zero.

History survives reboots — the header and entries are loaded from flash on startup.

### Chart View

Swipe left from the main HR screen to view the history chart. The chart displays a line graph of average BPM over time using LVGL's `lv_chart` widget.

- **Default view:** Last 2 hours (24 data points at 5-minute intervals)
- **Swipe up:** Scroll back in time by 2 hours
- **Swipe down:** Scroll forward in time by 2 hours
- **Swipe right:** Return to the main HR view

The time range label at the bottom shows the current window (e.g., "2h ago - now" or "4h - 2h ago").

Gaps where no data was recorded are shown as breaks in the line.

## BLE Heart Rate History Service

A custom BLE GATT service exposes the stored history for companion app sync.

### Service

UUID: `aabbccdd-1234-5678-abcd-ef0123456789`

### Characteristics

#### History Entry (UUID aabb0001-1234-5678-abcd-ef0123456789)

Read-only. Returns the 8-byte `Entry` struct at the position specified by the Read Index characteristic.

Byte layout matches the on-disk entry format:

| Byte | Type | Field |
|------|------|-------|
| 0-3 | uint32_t | timestamp (little-endian UNIX epoch) |
| 4 | uint8_t | avgBpm |
| 5 | uint8_t | minBpm |
| 6 | uint8_t | maxBpm |
| 7 | uint8_t | flags (bit 0 = valid) |

#### Entry Count (UUID aabb0002-1234-5678-abcd-ef0123456789)

Read + Notify. Returns a `uint16_t` (2 bytes, little-endian) with the number of valid history entries available.

Subscribe to notifications to be informed when a new 5-minute bucket is committed.

#### Read Index (UUID aabb0003-1234-5678-abcd-ef0123456789)

Read + Write. A `uint16_t` (2 bytes, little-endian) that controls which entry the History Entry characteristic returns. Index 0 is the newest entry, index 1 is the second newest, and so on.

### Companion App Usage

To retrieve all history:

1. Read **Entry Count** to get the number of available entries.
2. For each index from `0` to `count - 1`:
   a. Write the index to **Read Index**.
   b. Read **History Entry** to get the 8-byte entry.
3. Subscribe to **Entry Count** notifications to receive updates when new data arrives.

## Power Considerations

Continuous monitoring keeps the HRS3300 sensor LED active at 12.5mA drive current. This has a measurable impact on battery life compared to interval-based or disabled modes.

The no-contact detection mitigates power waste when the watch is not worn — the sensor pauses after detecting ambient light and only retries every 30 seconds.

Users can switch between continuous and interval-based modes at any time via the Heart Rate settings screen.

## Source Files

| File | Role |
|------|------|
| `src/components/heartrate/HeartRateHistory.h/cpp` | Circular buffer storage and 5-min bucket accumulation |
| `src/components/heartrate/HeartRateController.h/cpp` | Coordinates between task, UI, BLE, and history |
| `src/heartratetask/HeartRateTask.h/cpp` | Sensor lifecycle and state machine |
| `src/displayapp/screens/HeartRate.h/cpp` | UI: instant display + chart view |
| `src/components/ble/HeartRateHistoryService.h/cpp` | Custom BLE service for history retrieval |
