# Continuous Heart Rate Monitoring

## Introduction

Enable always-on heart rate monitoring on the PineTime so the user can glance at their current heart rate without waiting for a measurement cycle. The system continuously collects heart rate data in the background, maintains a rolling average, and persists historical data in 5-minute interval buckets for later review.

## Requirements

### 1. Continuous Background Measurement

**User Story:** As a PineTime user, I want heart rate measurements to run continuously in the background, so that a current reading is always available without manual intervention.

**Acceptance Criteria:**

1. When the feature is enabled, the heart rate sensor SHALL take measurements continuously regardless of screen state (on/off/AOD).
2. The system SHALL maintain a rolling current BPM value that reflects the most recent successful measurement.
3. The system SHALL expose an on/off toggle in the existing Heart Rate settings screen so the user can disable continuous monitoring to save battery.
4. When continuous monitoring is disabled, the system SHALL revert to the existing behavior (manual start/stop or configurable background intervals).

### 2. Instant Heart Rate Display

**User Story:** As a PineTime user, I want to see my current heart rate immediately when I open the Heart Rate app, so that I don't have to wait for a measurement to complete.

**Acceptance Criteria:**

1. When continuous monitoring is enabled and the Heart Rate app is opened, the app SHALL immediately display the most recent BPM value (or an average over the last measurement window).
2. The app SHALL indicate the age of the displayed value (e.g., "measured 12s ago") so the user knows how fresh the reading is.
3. If no measurement has been taken yet (e.g., just enabled), the app SHALL show a "Measuring..." state and display the first result as soon as available.
4. Watch faces that display heart rate SHALL also show the continuously-updated value without requiring the HR app to be open.

### 3. Historical Data Storage (5-Minute Intervals)

**User Story:** As a PineTime user, I want my heart rate history stored in 5-minute buckets, so that I can review trends over time.

**Acceptance Criteria:**

1. The system SHALL compute the average BPM for each 5-minute interval and persist it to storage.
2. Historical data SHALL be stored in the external SPI NOR flash (LittleFS) to survive reboots.
3. The system SHALL store a minimum of 48 hours of history (576 five-minute buckets).
4. When storage is full, the system SHALL overwrite the oldest entries (circular buffer behavior).
5. Each stored entry SHALL contain: timestamp (epoch or relative), average BPM, and min/max BPM for the interval.
6. If the sensor has no valid readings during a 5-minute interval (e.g., watch removed), the entry SHALL be marked as invalid/missing rather than storing a zero.

### 4. Heart Rate History Viewer

**User Story:** As a PineTime user, I want to view my heart rate history on the watch, so that I can see trends without needing a phone.

**Acceptance Criteria:**

1. The Heart Rate app SHALL include a history view showing recent 5-minute averages as a line chart.
2. The chart SHALL plot BPM on the Y-axis and time on the X-axis, rendered using LVGL chart primitives.
3. The user SHALL be able to scroll through time by swiping left/right to view older/newer data.
4. The chart SHALL visually indicate gaps where no valid data was recorded (e.g., watch removed).

### 5. BLE Exposure

**User Story:** As a PineTime user, I want my heart rate data available over Bluetooth, so that companion apps can track my heart rate continuously.

**Acceptance Criteria:**

1. The existing BLE Heart Rate Service SHALL continue to send notifications with updated BPM values during continuous monitoring.
2. A custom BLE GATT service SHALL expose the historical 5-minute interval data so companion apps can pull heart rate history.
3. The custom service SHALL allow clients to read historical entries by index or time range.
4. The custom service SHALL support notifications so clients can be informed when new 5-minute buckets are committed.

### 6. Power Management

**User Story:** As a PineTime user, I want continuous monitoring to be power-aware, so that my battery doesn't drain too quickly.

**Acceptance Criteria:**

1. The system SHALL allow the user to choose between continuous monitoring and the existing interval-based options in settings.
2. When the watch detects no skin contact (ambient light spike from the sensor), the system SHALL pause measurements and retry periodically (e.g., every 30 seconds) to conserve power.
3. The system SHOULD document expected battery impact so users can make informed choices.

## Open Questions

- What is the acceptable battery life reduction for continuous monitoring? (The HRS3300 at 12.5mA LED current will have significant draw.)
- Should the history chart show min/max range bands around the average line, or just the average?
