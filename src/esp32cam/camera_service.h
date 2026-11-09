#pragma once
#include <esp_camera.h>
#include "camera_calibration.h"

namespace cameraService {
// Settings stay fixed until restart. HTTP uploads write the next boot's settings.
const robot::CameraCalibration& calibration();
void begin();
void publishFrame(const camera_fb_t* frame, uint32_t sequence, int64_t captureUs);
} // namespace cameraService
