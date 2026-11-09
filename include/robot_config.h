#pragma once
#include <stdint.h>

// Both boards use these settings. Rebuild both after changing them.
namespace cfg {
constexpr char WIFI_SSID[] = "AprilRobot";
constexpr char WIFI_PASSWORD[] = "AprilRobot2640";
constexpr uint32_t LINK_BAUD = 115200;
constexpr uint32_t PHONE_TIMEOUT_MS = 800;
constexpr uint32_t MANUAL_TIMEOUT_MS = 350;
constexpr uint32_t COMMAND_MAX_AHEAD_MS = 700;
constexpr float MANUAL_FORWARD_MPS = 0.08f;
constexpr float MANUAL_REVERSE_MPS = 0.06f;
constexpr float MANUAL_TURN_RADPS = 0.50f;
constexpr uint32_t PACKET_PERIOD_MS = 20;
constexpr uint32_t LINK_TIMEOUT_MS = 300;
constexpr uint32_t VISION_TIMEOUT_MS = 1000;
constexpr uint32_t ARM_DELAY_MS = 2000;
constexpr uint32_t MAINTENANCE_SETTLE_MS = 500;

// Nominal wheel dimensions. Measure the tire spacing before the first floor run.
constexpr float LEFT_WHEEL_DIAMETER_M = 0.065f;
constexpr float RIGHT_WHEEL_DIAMETER_M = 0.065f;
constexpr float WHEEL_TRACK_M = 0.158f;      // estimate; measure between the tire centers
constexpr float LEFT_PULSES_PER_REV = 20.0f; // 20 PPR, counting rising edges only
constexpr float RIGHT_PULSES_PER_REV = 20.0f;
constexpr bool LEFT_MOTOR_INVERT = false;
constexpr bool RIGHT_MOTOR_INVERT = false;
constexpr uint32_t ENCODER_DEBOUNCE_US = 500;

constexpr uint32_t CONTROL_PERIOD_MS = 20;
constexpr uint32_t SPEED_WINDOW_MS =
    200;                     // average several encoder pulses for a steadier speed reading
constexpr int PWM_MAX = 750; // the PWM range is set to 0..1023 in setup()
constexpr float PWM_START = 280.0f;
constexpr float PWM_FEEDFORWARD = 1800.0f; // PWM per m/s
constexpr float SPEED_KP = 700.0f;
constexpr float SPEED_KI = 450.0f;
constexpr float INTEGRAL_LIMIT = 0.35f;
constexpr uint32_t REVERSAL_PAUSE_MS = 200;
constexpr uint32_t STALL_TIMEOUT_MS = 1800;
constexpr float MAX_WHEEL_MPS = 0.15f;
constexpr float MAX_FORWARD_MPS = 0.10f;
constexpr float MAX_TURN_RADPS = 0.65f;
constexpr float BEARING_KP = 1.2f;
constexpr float RANGE_KP = 0.45f;
constexpr float ALIGN_FIRST_RAD = 0.25f;
constexpr float ARRIVAL_BEARING_RAD = 0.12f;
constexpr float STOP_RANGE_M = 0.35f; // camera-to-tag ground-plane range
constexpr float ARRIVAL_RANGE_TOLERANCE_M = 0.02f;
constexpr uint8_t ARRIVAL_FRAMES = 3;
constexpr uint32_t SEARCH_TIMEOUT_MS = 30000;
constexpr uint32_t SEARCH_TURN_MS = 500;
constexpr uint32_t SEARCH_REST_MS = 500;
constexpr uint32_t AVOID_TIMEOUT_MS = 2500;
constexpr uint32_t OBSTACLE_CLEAR_MS = 400;
constexpr float SEARCH_TURN_RADPS = 0.50f;
constexpr float AVOID_TURN_RADPS = 0.55f;

// Flash calibration overrides these lens estimates. See tools/calibrate_camera.py.
// Changing resolution or orientation requires a new calibration.
constexpr int IMAGE_WIDTH = 320;
constexpr int IMAGE_HEIGHT = 240;
constexpr int TAG_COUNT = 4;         // phone destinations: tag36h11 IDs 0, 1, 2, 3
constexpr float TAG_SIZE_M = 0.100f; // outer black square, without the white margin
constexpr float CAMERA_FX = 280.0f;  // starting estimate in pixels; needs calibration
constexpr float CAMERA_FY = 280.0f;
constexpr float CAMERA_CX = 160.0f;
constexpr float CAMERA_CY = 120.0f;
constexpr bool CAMERA_HMIRROR = false;
constexpr bool CAMERA_VFLIP = false;
constexpr float MIN_DECISION_MARGIN = 35.0f;
constexpr float MAX_POSE_ERROR = 0.01f;
constexpr float MIN_TAG_EDGE_PX = 14.0f;
constexpr float MAX_TARGET_RANGE_M = 4.0f;

// These pins also connect to the SD slot, so leave the SD card out.
constexpr uint8_t IR_LEFT_PIN = 13;
constexpr uint8_t IR_RIGHT_PIN = 14;
constexpr bool IR_ACTIVE_LOW = true;
constexpr uint32_t IR_CLEAR_DEBOUNCE_MS = 100;

static_assert(TAG_COUNT == 4, "This protocol and phone interface expose four destinations");
static_assert(WHEEL_TRACK_M > 0 && LEFT_PULSES_PER_REV > 0 && RIGHT_PULSES_PER_REV > 0,
              "Geometry and encoder counts must be positive");
} // namespace cfg
