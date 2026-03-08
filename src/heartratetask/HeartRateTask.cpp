#include "heartratetask/HeartRateTask.h"
#include <drivers/Hrs3300.h>
#include <components/heartrate/HeartRateController.h>
#include <limits>

#include "utility/Math.h"

using namespace Pinetime::Applications;

namespace {
  constexpr TickType_t backgroundMeasurementTimeLimit = 30 * configTICK_RATE_HZ;
}

bool HeartRateTask::IsContinuousMode() const {
  auto interval = settings.GetHeartRateBackgroundMeasurementInterval();
  return interval.has_value() && interval.value() == 0;
}

std::optional<TickType_t> HeartRateTask::BackgroundMeasurementInterval() const {
  auto interval = settings.GetHeartRateBackgroundMeasurementInterval();
  if (!interval.has_value()) {
    return std::nullopt;
  }
  if (interval.value() == 0) {
    return std::nullopt;
  }
  return interval.value() * configTICK_RATE_HZ;
}

bool HeartRateTask::BackgroundMeasurementNeeded() const {
  auto backgroundPeriod = BackgroundMeasurementInterval();
  if (!backgroundPeriod.has_value()) {
    return false;
  }
  return xTaskGetTickCount() - lastMeasurementTime >= backgroundPeriod.value();
};

TickType_t HeartRateTask::CurrentTaskDelay() {
  auto backgroundPeriod = BackgroundMeasurementInterval();
  TickType_t currentTime = xTaskGetTickCount();
  auto CalculateSleepTicks = [&]() {
    TickType_t elapsed = currentTime - measurementStartTime;
    static_assert((configTICK_RATE_HZ / 2ULL) * (std::numeric_limits<decltype(count)>::max() + 1ULL) *
                      static_cast<uint64_t>((Pinetime::Controllers::Ppg::deltaTms)) <
                    std::numeric_limits<uint32_t>::max(),
                  "Overflow");
    TickType_t elapsedTarget = Utility::RoundedDiv(
      static_cast<uint32_t>(configTICK_RATE_HZ / 2) * (static_cast<uint32_t>(count) + 1U) *
        static_cast<uint32_t>((Pinetime::Controllers::Ppg::deltaTms)),
      static_cast<uint32_t>(1000 / 2));
    if (count == std::numeric_limits<decltype(count)>::max()) {
      count = 0;
      measurementStartTime = currentTime;
    }
    if (elapsedTarget > elapsed) {
      return elapsedTarget - elapsed;
    }
    return static_cast<TickType_t>(0);
  };
  switch (state) {
    case States::Disabled:
      return portMAX_DELAY;
    case States::Waiting:
      if (!backgroundPeriod.has_value()) {
        return portMAX_DELAY;
      }
      if (currentTime - lastMeasurementTime < backgroundPeriod.value()) {
        return backgroundPeriod.value() - (currentTime - lastMeasurementTime);
      }
      return 0;
    case States::BackgroundMeasuring:
    case States::ForegroundMeasuring:
    case States::ContinuousMeasuring:
      return CalculateSleepTicks();
    case States::ContinuousPaused: {
      TickType_t elapsed = currentTime - contactLostTime;
      if (elapsed >= contactRetryDelay) {
        return 0;
      }
      return contactRetryDelay - elapsed;
    }
  }
  return portMAX_DELAY;
}

HeartRateTask::HeartRateTask(Drivers::Hrs3300& heartRateSensor,
                             Controllers::HeartRateController& controller,
                             Controllers::Settings& settings)
  : heartRateSensor {heartRateSensor}, controller {controller}, settings {settings} {
}

void HeartRateTask::Start() {
  messageQueue = xQueueCreate(10, 1);
  controller.SetHeartRateTask(this);

  if (pdPASS != xTaskCreate(HeartRateTask::Process, "Heartrate", 500, this, 1, &taskHandle)) {
    APP_ERROR_HANDLER(NRF_ERROR_NO_MEM);
  }
}

void HeartRateTask::Process(void* instance) {
  auto* app = static_cast<HeartRateTask*>(instance);
  app->Work();
}

void HeartRateTask::Work() {
  lastMeasurementTime = xTaskGetTickCount();
  valueCurrentlyShown = false;

  while (true) {
    TickType_t delay = CurrentTaskDelay();
    Messages msg;
    States newState = state;

    if (xQueueReceive(messageQueue, &msg, delay) == pdTRUE) {
      switch (msg) {
        case Messages::GoToSleep:
          if (state == States::Disabled) {
            break;
          }
          if (state == States::ContinuousMeasuring ||
              state == States::ContinuousPaused) {
            break;
          }
          if (IsContinuousMode()) {
            newState = States::ContinuousMeasuring;
          } else if (BackgroundMeasurementNeeded()) {
            newState = States::BackgroundMeasuring;
          } else {
            newState = States::Waiting;
          }
          break;
        case Messages::WakeUp:
          if (state == States::Disabled) {
            break;
          }
          if (state == States::ContinuousMeasuring ||
              state == States::ContinuousPaused) {
            break;
          }
          if (IsContinuousMode()) {
            newState = States::ContinuousMeasuring;
          } else {
            newState = States::ForegroundMeasuring;
          }
          break;
        case Messages::Enable:
          if (IsContinuousMode()) {
            newState = States::ContinuousMeasuring;
          } else {
            newState = States::ForegroundMeasuring;
          }
          valueCurrentlyShown = false;
          break;
        case Messages::Disable:
          newState = States::Disabled;
          break;
      }
    }

    if (newState == States::Waiting && BackgroundMeasurementNeeded()) {
      newState = States::BackgroundMeasuring;
    } else if (newState == States::BackgroundMeasuring && !BackgroundMeasurementNeeded()) {
      newState = States::Waiting;
    }

    if (state == States::ContinuousPaused && newState == States::ContinuousPaused) {
      TickType_t elapsed = xTaskGetTickCount() - contactLostTime;
      if (elapsed >= contactRetryDelay) {
        newState = States::ContinuousMeasuring;
      }
    }

    bool wasMeasuring = (state == States::ForegroundMeasuring ||
                         state == States::BackgroundMeasuring ||
                         state == States::ContinuousMeasuring);
    bool willMeasure = (newState == States::ForegroundMeasuring ||
                        newState == States::BackgroundMeasuring ||
                        newState == States::ContinuousMeasuring);

    if (willMeasure && !wasMeasuring) {
      StartMeasurement();
    } else if (!willMeasure && wasMeasuring) {
      StopMeasurement();
    }
    state = newState;

    if (state == States::ForegroundMeasuring ||
        state == States::BackgroundMeasuring ||
        state == States::ContinuousMeasuring) {
      HandleSensorData();
      count++;
    }
  }
}

void HeartRateTask::PushMessage(HeartRateTask::Messages msg) {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xQueueSendFromISR(messageQueue, &msg, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void HeartRateTask::StartMeasurement() {
  heartRateSensor.Enable();
  ppg.Reset(true);
  vTaskDelay(100);
  measurementSucceeded = false;
  count = 0;
  measurementStartTime = xTaskGetTickCount();
}

void HeartRateTask::StopMeasurement() {
  heartRateSensor.Disable();
  ppg.Reset(true);
  vTaskDelay(100);
}

void HeartRateTask::HandleSensorData() {
  auto sensorData = heartRateSensor.ReadHrsAls();
  int8_t ambient = ppg.Preprocess(sensorData.hrs, sensorData.als);
  int bpm = ppg.HeartRate();

  if (ambient > 0) {
    ppg.Reset(true);
    controller.Update(Controllers::HeartRateController::States::NotEnoughData, bpm);
    bpm = 0;
    valueCurrentlyShown = false;

    if (state == States::ContinuousMeasuring) {
      contactLostTime = xTaskGetTickCount();
      StopMeasurement();
      state = States::ContinuousPaused;
      return;
    }
  }

  if (bpm == -1) {
    ppg.Reset(false);
    bpm = 0;
    controller.Update(Controllers::HeartRateController::States::Running, bpm);
    valueCurrentlyShown = false;
  } else if (bpm == -2) {
    bpm = 0;
    if (!valueCurrentlyShown) {
      controller.Update(Controllers::HeartRateController::States::NotEnoughData, bpm);
    }
  }

  if (bpm != 0) {
    if (state == States::BackgroundMeasuring &&
        xTaskGetTickCount() - measurementStartTime < backgroundMeasurementTimeLimit) {
      lastMeasurementTime = measurementStartTime;
    } else {
      lastMeasurementTime = xTaskGetTickCount();
    }
    measurementSucceeded = true;
    valueCurrentlyShown = true;
    controller.Update(Controllers::HeartRateController::States::Running, bpm);
    return;
  }

  if (xTaskGetTickCount() - measurementStartTime > backgroundMeasurementTimeLimit) {
    if (!measurementSucceeded) {
      controller.Update(Controllers::HeartRateController::States::Running, 0);
      valueCurrentlyShown = false;
    }
    if (state == States::BackgroundMeasuring) {
      lastMeasurementTime = xTaskGetTickCount() - backgroundMeasurementTimeLimit;
    } else if (state == States::ContinuousMeasuring) {
      measurementSucceeded = false;
      count = 0;
      measurementStartTime = xTaskGetTickCount();
    } else {
      lastMeasurementTime = xTaskGetTickCount();
    }
  }
}
