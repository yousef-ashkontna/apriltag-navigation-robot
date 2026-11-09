#pragma once
#include <stdint.h>

namespace cameraRuntime {
void observeMotorState(bool maintenance, uint32_t now);
bool maintenanceReady();
bool busy();
bool beginWork();
void endWork();
void setupOta();
void handleOta();
} // namespace cameraRuntime
