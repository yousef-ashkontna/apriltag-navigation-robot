#include "camera_runtime.h"
#include "maintenance.h"
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>

namespace cameraRuntime {
namespace {
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
robot::Maintenance gate;
bool working = false, reportBusy = false, listening = false, uploading = false;

bool lockWork(bool showBusy) {
    portENTER_CRITICAL(&mux);
    const bool allowed = gate.ready(millis()) && !working;
    if (allowed) {
        working = true;
        reportBusy = showBusy;
    }
    portEXIT_CRITICAL(&mux);
    return allowed;
}
} // namespace

void observeMotorState(bool maintenance, uint32_t now) {
    portENTER_CRITICAL(&mux);
    gate.observe(maintenance, now);
    portEXIT_CRITICAL(&mux);
}

bool maintenanceReady() {
    portENTER_CRITICAL(&mux);
    const bool ready = gate.ready(millis());
    portEXIT_CRITICAL(&mux);
    return ready;
}

bool busy() {
    portENTER_CRITICAL(&mux);
    const bool value = working && reportBusy;
    portEXIT_CRITICAL(&mux);
    return value;
}

bool beginWork() {
    return lockWork(true);
}

void endWork() {
    portENTER_CRITICAL(&mux);
    working = false;
    reportBusy = false;
    portEXIT_CRITICAL(&mux);
}

void setupOta() {
    ArduinoOTA.setHostname("april-camera");
    ArduinoOTA.setPort(3232);
    ArduinoOTA.setPassword(ROBOT_OTA_PASSWORD);
    ArduinoOTA.setMdnsEnabled(false);
    ArduinoOTA.onStart([]() {
        uploading = true;
        portENTER_CRITICAL(&mux);
        reportBusy = true;
        portEXIT_CRITICAL(&mux);
    });
    ArduinoOTA.onError([](ota_error_t) { uploading = false; });
}

void handleOta() {
    const bool allowed = maintenanceReady() && WiFi.status() == WL_CONNECTED;
    if (!allowed) {
        if (listening) {
            ArduinoOTA.end();
            listening = false;
        }
        return;
    }
    if (!listening) {
        ArduinoOTA.begin();
        listening = true;
    }
    // Also excludes simultaneous calibration writes from the HTTP task.
    if (lockWork(false)) {
        ArduinoOTA.handle();
        if (!uploading)
            endWork();
    }
}
} // namespace cameraRuntime
