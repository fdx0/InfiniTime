#pragma once

#include <cstdint>
#include <chrono>
#include "displayapp/screens/Screen.h"
#include "systemtask/SystemTask.h"
#include "systemtask/WakeLock.h"
#include "Symbols.h"
#include <lvgl/src/lv_core/lv_style.h>
#include <lvgl/src/lv_core/lv_obj.h>

namespace Pinetime {
  namespace Controllers {
    class HeartRateController;
    class DateTime;
    class Settings;
  }

  namespace Applications {
    namespace Screens {

      class HeartRate : public Screen {
      public:
        HeartRate(Controllers::HeartRateController& heartRateController,
                  Controllers::DateTime& dateTimeController,
                  Controllers::Settings& settingsController,
                  System::SystemTask& systemTask);
        ~HeartRate() override;

        void Refresh() override;

        void OnStartStopEvent(lv_event_t event);
        bool OnTouchEvent(TouchEvents event) override;

      private:
        Controllers::HeartRateController& heartRateController;
        Controllers::DateTime& dateTimeController;
        Controllers::Settings& settingsController;
        Pinetime::System::WakeLock wakeLock;

        enum class View { Main, Chart };
        View currentView = View::Main;
        uint8_t chartPageOffset = 0;

        static constexpr uint8_t chartPoints = 24;
        static constexpr uint8_t pointsPerPage = 24;

        void CreateMainView();
        void CreateChartView();
        void DestroyCurrentView();
        void UpdateStartStopButton(bool isRunning);
        void PopulateChart();

        lv_obj_t* label_hr = nullptr;
        lv_obj_t* label_bpm = nullptr;
        lv_obj_t* label_status = nullptr;
        lv_obj_t* btn_startStop = nullptr;
        lv_obj_t* label_startStop = nullptr;

        lv_obj_t* chart = nullptr;
        lv_chart_series_t* chartSeries = nullptr;
        lv_obj_t* chartTitle = nullptr;
        lv_obj_t* chartTimeLabel = nullptr;

        lv_task_t* taskRefresh;
      };
    }

    template <>
    struct AppTraits<Apps::HeartRate> {
      static constexpr Apps app = Apps::HeartRate;
      static constexpr const char* icon = Screens::Symbols::heartBeat;

      static Screens::Screen* Create(AppControllers& controllers) {
        return new Screens::HeartRate(controllers.heartRateController,
                                     controllers.dateTimeController,
                                     controllers.settingsController,
                                     *controllers.systemTask);
      };

      static bool IsAvailable(Pinetime::Controllers::FS& /*filesystem*/) {
        return true;
      };
    };
  }
}
