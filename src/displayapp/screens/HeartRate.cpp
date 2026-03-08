#include "displayapp/screens/HeartRate.h"
#include <lvgl/lvgl.h>
#include <components/heartrate/HeartRateController.h>
#include <components/datetime/DateTimeController.h>
#include <components/settings/Settings.h>

#include "displayapp/DisplayApp.h"
#include "displayapp/InfiniTimeTheme.h"

using namespace Pinetime::Applications::Screens;

namespace {
  const char* ToString(Pinetime::Controllers::HeartRateController::States s) {
    switch (s) {
      case Pinetime::Controllers::HeartRateController::States::NotEnoughData:
        return "Not enough data,\nplease wait...";
      case Pinetime::Controllers::HeartRateController::States::NoTouch:
        return "No touch detected";
      case Pinetime::Controllers::HeartRateController::States::Running:
        return "Measuring...";
      case Pinetime::Controllers::HeartRateController::States::Stopped:
        return "Stopped";
    }
    return "";
  }

  void btnStartStopEventHandler(lv_obj_t* obj, lv_event_t event) {
    auto* screen = static_cast<HeartRate*>(obj->user_data);
    screen->OnStartStopEvent(event);
  }
}

HeartRate::HeartRate(Controllers::HeartRateController& heartRateController,
                     Controllers::DateTime& dateTimeController,
                     Controllers::Settings& settingsController,
                     System::SystemTask& systemTask)
  : heartRateController {heartRateController},
    dateTimeController {dateTimeController},
    settingsController {settingsController},
    wakeLock(systemTask) {

  CreateMainView();
  taskRefresh = lv_task_create(RefreshTaskCallback, 100, LV_TASK_PRIO_MID, this);
}

HeartRate::~HeartRate() {
  lv_task_del(taskRefresh);
  lv_obj_clean(lv_scr_act());
}

void HeartRate::CreateMainView() {
  bool isHrRunning = heartRateController.State() != Controllers::HeartRateController::States::Stopped;
  bool isContinuous = settingsController.GetHeartRateBackgroundMeasurementInterval().has_value() &&
                      settingsController.GetHeartRateBackgroundMeasurementInterval().value() == 0;

  label_hr = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_font(label_hr, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &jetbrains_mono_76);

  if (isHrRunning || isContinuous) {
    lv_obj_set_style_local_text_color(label_hr, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::highlight);
  } else {
    lv_obj_set_style_local_text_color(label_hr, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::lightGray);
  }

  uint8_t currentBpm = heartRateController.HeartRate();
  if (currentBpm > 0 && (isHrRunning || isContinuous)) {
    lv_label_set_text_fmt(label_hr, "%03d", currentBpm);
  } else {
    lv_label_set_text_static(label_hr, "---");
  }

  lv_obj_align(label_hr, nullptr, LV_ALIGN_CENTER, 0, -40);

  label_bpm = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_text_static(label_bpm, "Heart rate BPM");
  lv_obj_align(label_bpm, label_hr, LV_ALIGN_OUT_TOP_MID, 0, -20);

  label_status = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(label_status, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_GRAY);

  if (isContinuous && currentBpm > 0) {
    uint32_t lastTime = heartRateController.LastMeasurementTime();
    if (lastTime > 0) {
      auto now = dateTimeController.CurrentDateTime();
      auto epoch = std::chrono::system_clock::to_time_t(
        std::chrono::time_point_cast<std::chrono::system_clock::duration>(now));
      uint32_t ago = static_cast<uint32_t>(epoch) - lastTime;
      if (ago < 5) {
        lv_label_set_text_static(label_status, "just now");
      } else if (ago < 60) {
        lv_label_set_text_fmt(label_status, "%lu s ago", ago);
      } else {
        lv_label_set_text_fmt(label_status, "%lu min ago", ago / 60);
      }
    } else {
      lv_label_set_text_static(label_status, "Continuous");
    }
  } else {
    lv_label_set_text_static(label_status, ToString(heartRateController.State()));
  }

  lv_obj_align(label_status, label_hr, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

  if (!isContinuous) {
    btn_startStop = lv_btn_create(lv_scr_act(), nullptr);
    btn_startStop->user_data = this;
    lv_obj_set_height(btn_startStop, 50);
    lv_obj_set_event_cb(btn_startStop, btnStartStopEventHandler);
    lv_obj_align(btn_startStop, nullptr, LV_ALIGN_IN_BOTTOM_MID, 0, 0);

    label_startStop = lv_label_create(btn_startStop, nullptr);
    UpdateStartStopButton(isHrRunning);
    if (isHrRunning) {
      wakeLock.Lock();
    }
  }
}

void HeartRate::CreateChartView() {
  chartTitle = lv_label_create(lv_scr_act(), nullptr);
  lv_label_set_text_static(chartTitle, "HR History");
  lv_obj_set_style_local_text_color(chartTitle, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::highlight);
  lv_obj_align(chartTitle, nullptr, LV_ALIGN_IN_TOP_MID, 0, 5);

  chart = lv_chart_create(lv_scr_act(), nullptr);
  lv_obj_set_size(chart, 220, 160);
  lv_obj_align(chart, nullptr, LV_ALIGN_CENTER, 0, 5);
  lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
  lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
  lv_chart_set_point_count(chart, chartPoints);
  lv_chart_set_range(chart, 40, 200);
  lv_obj_set_style_local_bg_opa(chart, LV_CHART_PART_BG, LV_STATE_DEFAULT, LV_OPA_TRANSP);

  chartSeries = lv_chart_add_series(chart, LV_COLOR_RED);

  chartTimeLabel = lv_label_create(lv_scr_act(), nullptr);
  lv_obj_set_style_local_text_color(chartTimeLabel, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_GRAY);
  lv_obj_align(chartTimeLabel, nullptr, LV_ALIGN_IN_BOTTOM_MID, 0, -5);

  PopulateChart();
}

void HeartRate::PopulateChart() {
  auto& history = heartRateController.GetHistory();
  uint16_t startIndex = chartPageOffset * pointsPerPage;

  lv_chart_init_points(chart, chartSeries, LV_CHART_POINT_DEF);

  uint16_t available = history.EntryCount();
  for (int i = chartPoints - 1; i >= 0; i--) {
    uint16_t idx = startIndex + static_cast<uint16_t>(i);
    if (idx < available) {
      auto entry = history.GetEntry(idx);
      if (entry.flags & Controllers::HeartRateHistory::flagValid) {
        chartSeries->points[chartPoints - 1 - i] = entry.avgBpm;
      } else {
        chartSeries->points[chartPoints - 1 - i] = LV_CHART_POINT_DEF;
      }
    } else {
      chartSeries->points[chartPoints - 1 - i] = LV_CHART_POINT_DEF;
    }
  }
  lv_chart_refresh(chart);

  uint16_t endMinutes = chartPageOffset * pointsPerPage * 5;
  uint16_t startMinutes = endMinutes + chartPoints * 5;
  if (endMinutes == 0) {
    lv_label_set_text_fmt(chartTimeLabel, "%dh ago - now", startMinutes / 60);
  } else {
    lv_label_set_text_fmt(chartTimeLabel, "%dh - %dh ago", startMinutes / 60, endMinutes / 60);
  }
  lv_obj_align(chartTimeLabel, nullptr, LV_ALIGN_IN_BOTTOM_MID, 0, -5);
}

void HeartRate::DestroyCurrentView() {
  lv_obj_clean(lv_scr_act());
  label_hr = nullptr;
  label_bpm = nullptr;
  label_status = nullptr;
  btn_startStop = nullptr;
  label_startStop = nullptr;
  chart = nullptr;
  chartSeries = nullptr;
  chartTitle = nullptr;
  chartTimeLabel = nullptr;
}

void HeartRate::Refresh() {
  if (currentView == View::Main) {
    auto state = heartRateController.State();
    bool isContinuous = settingsController.GetHeartRateBackgroundMeasurementInterval().has_value() &&
                        settingsController.GetHeartRateBackgroundMeasurementInterval().value() == 0;

    switch (state) {
      case Controllers::HeartRateController::States::NoTouch:
      case Controllers::HeartRateController::States::NotEnoughData:
        if (label_hr != nullptr) {
          lv_label_set_text_static(label_hr, "---");
        }
        break;
      default:
        if (label_hr != nullptr) {
          if (heartRateController.HeartRate() == 0) {
            lv_label_set_text_static(label_hr, "---");
          } else {
            lv_label_set_text_fmt(label_hr, "%03d", heartRateController.HeartRate());
          }
        }
    }

    if (label_status != nullptr) {
      if (isContinuous && heartRateController.HeartRate() > 0) {
        uint32_t lastTime = heartRateController.LastMeasurementTime();
        if (lastTime > 0) {
          auto now = dateTimeController.CurrentDateTime();
          auto epoch = std::chrono::system_clock::to_time_t(
            std::chrono::time_point_cast<std::chrono::system_clock::duration>(now));
          uint32_t ago = static_cast<uint32_t>(epoch) - lastTime;
          if (ago < 5) {
            lv_label_set_text_static(label_status, "just now");
          } else if (ago < 60) {
            lv_label_set_text_fmt(label_status, "%lu s ago", ago);
          } else {
            lv_label_set_text_fmt(label_status, "%lu min ago", ago / 60);
          }
        } else {
          lv_label_set_text_static(label_status, "Continuous");
        }
      } else {
        lv_label_set_text_static(label_status, ToString(state));
      }
      lv_obj_align(label_status, label_hr, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    }
  }
}

bool HeartRate::OnTouchEvent(TouchEvents event) {
  if (event == TouchEvents::SwipeLeft && currentView == View::Main) {
    DestroyCurrentView();
    currentView = View::Chart;
    chartPageOffset = 0;
    CreateChartView();
    return true;
  }

  if (event == TouchEvents::SwipeRight && currentView == View::Chart) {
    DestroyCurrentView();
    currentView = View::Main;
    CreateMainView();
    return true;
  }

  if (currentView == View::Chart) {
    uint16_t maxPages = heartRateController.GetHistory().EntryCount() / pointsPerPage;
    if (event == TouchEvents::SwipeUp && chartPageOffset < maxPages) {
      chartPageOffset++;
      PopulateChart();
      return true;
    }
    if (event == TouchEvents::SwipeDown && chartPageOffset > 0) {
      chartPageOffset--;
      PopulateChart();
      return true;
    }
  }

  return false;
}

void HeartRate::OnStartStopEvent(lv_event_t event) {
  if (event == LV_EVENT_CLICKED) {
    if (heartRateController.State() == Controllers::HeartRateController::States::Stopped) {
      heartRateController.Enable();
      UpdateStartStopButton(heartRateController.State() != Controllers::HeartRateController::States::Stopped);
      wakeLock.Lock();
      if (label_hr != nullptr) {
        lv_obj_set_style_local_text_color(label_hr, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::highlight);
      }
    } else {
      heartRateController.Disable();
      UpdateStartStopButton(heartRateController.State() != Controllers::HeartRateController::States::Stopped);
      wakeLock.Release();
      if (label_hr != nullptr) {
        lv_obj_set_style_local_text_color(label_hr, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, Colors::lightGray);
      }
    }
  }
}

void HeartRate::UpdateStartStopButton(bool isRunning) {
  if (label_startStop == nullptr) {
    return;
  }
  if (isRunning) {
    lv_label_set_text_static(label_startStop, "Stop");
  } else {
    lv_label_set_text_static(label_startStop, "Start");
  }
}
