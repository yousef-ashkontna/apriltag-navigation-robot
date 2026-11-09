#include <Arduino.h>
#include <WiFi.h>
#include <esp_camera.h>
#include <esp_timer.h>
#include <esp_system.h>
#include <math.h>
#include "robot_config.h"
#include "robot_protocol.h"
#include "camera_service.h"
#include "calibrated_pose.h"
#include "camera_runtime.h"
extern "C" {
#include "apriltag.h"
#include "apriltag_pose.h"
#include "tag36h11.h"
}

namespace {
struct Vision {
    bool ready = false, target = false;
    uint32_t frame = 0, requestId = 0;
    uint8_t targetId = 0, visibleMask = 0;
    int64_t captureUs = 0;
    int32_t rightMm = 0, forwardMm = 0;
    uint16_t margin10 = 0;
};

Vision vision;
robot::Poll requested;
portMUX_TYPE visionMux = portMUX_INITIALIZER_UNLOCKED;
apriltag_detector_t* detector = nullptr;
apriltag_family_t* family = nullptr;
uint32_t frameCounter = 0;
bool initialized = false;

void publish(const Vision& value) {
    portENTER_CRITICAL(&visionMux);
    vision = value;
    portEXIT_CRITICAL(&visionMux);
}

// Stop on the first blocked reading. Wait for a steady clear signal before moving again.
class ObstacleInput {
public:
    explicit ObstacleInput(uint8_t pin) : pin_(pin) {}

    bool read(uint32_t now) {
        const bool hit = (digitalRead(pin_) == (cfg::IR_ACTIVE_LOW ? LOW : HIGH));
        if (hit) {
            clearTiming_ = false;
            blocked_ = true;
        } else {
            if (!clearTiming_) {
                clearSince_ = now;
                clearTiming_ = true;
            }
            if (now - clearSince_ >= cfg::IR_CLEAR_DEBOUNCE_MS)
                blocked_ = false;
        }
        return blocked_;
    }

private:
    uint8_t pin_;
    bool blocked_ = true, clearTiming_ = false;
    uint32_t clearSince_ = 0;
};

void handleSensorsAndUart(void*) {
    const uint32_t session = esp_random();
    uint32_t sequence = 0, pollReceivedMs = 0;
    bool havePoll = false;
    robot::Poll poll;
    robot::StreamFrame<robot::POLL_SIZE> parser;
    ObstacleInput left(cfg::IR_LEFT_PIN), right(cfg::IR_RIGHT_PIN);
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        const uint32_t now = millis();
        const bool blockedLeft = left.read(now), blockedRight = right.read(now);
        for (int i = 0; i < 256 && Serial.available(); ++i) {
            if (!parser.push(uint8_t(Serial.read()), 'Q', now))
                continue;
            robot::Poll incoming;
            if (robot::decodePoll(parser.bytes, robot::POLL_SIZE, incoming) &&
                (!havePoll || robot::newer(incoming.token, poll.token))) {
                poll = incoming;
                pollReceivedMs = now;
                havePoll = true;
                cameraRuntime::observeMotorState(poll.maintenance, now);
                portENTER_CRITICAL(&visionMux);
                requested = poll;
                portEXIT_CRITICAL(&visionMux);
            }
        }
        if (havePoll && now - pollReceivedMs <= cfg::LINK_TIMEOUT_MS) {
            Vision snapshot;
            portENTER_CRITICAL(&visionMux);
            snapshot = vision;
            portEXIT_CRITICAL(&visionMux);
            robot::Observation observation;
            observation.session = session;
            observation.sequence = ++sequence;
            observation.pollToken = poll.token;
            observation.frame = snapshot.frame;
            observation.requestId = snapshot.requestId;
            observation.targetId = snapshot.targetId;
            observation.visibleMask = snapshot.visibleMask;
            if (snapshot.ready)
                observation.flags |= robot::CAMERA_OK;
            if (snapshot.target)
                observation.flags |= robot::TARGET_VISIBLE;
            if (blockedLeft)
                observation.flags |= robot::OBSTACLE_LEFT;
            if (blockedRight)
                observation.flags |= robot::OBSTACLE_RIGHT;
            if (cameraRuntime::maintenanceReady())
                observation.flags |= robot::MAINTENANCE;
            if (cameraRuntime::busy())
                observation.flags |= robot::UPDATE_BUSY;
            if (snapshot.captureUs > 0) {
                const int64_t age = (esp_timer_get_time() - snapshot.captureUs) / 1000;
                observation.frameAgeMs = age >= 0 && age < UINT32_MAX ? uint32_t(age) : UINT32_MAX;
            }
            observation.rightMm = snapshot.rightMm;
            observation.forwardMm = snapshot.forwardMm;
            observation.margin10 = snapshot.margin10;
            uint8_t bytes[robot::PACKET_SIZE];
            robot::encode(observation, bytes);
            if (Serial.availableForWrite() >= int(sizeof(bytes)))
                Serial.write(bytes, sizeof(bytes));
        } else
            havePoll = false;
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(cfg::PACKET_PERIOD_MS));
    }
}

bool setupCamera() {
    camera_config_t cameraConfig = {};
    cameraConfig.ledc_channel = LEDC_CHANNEL_0;
    cameraConfig.ledc_timer = LEDC_TIMER_0;
    // Camera connections on the AI-Thinker ESP32-CAM.
    cameraConfig.pin_d0 = 5;
    cameraConfig.pin_d1 = 18;
    cameraConfig.pin_d2 = 19;
    cameraConfig.pin_d3 = 21;
    cameraConfig.pin_d4 = 36;
    cameraConfig.pin_d5 = 39;
    cameraConfig.pin_d6 = 34;
    cameraConfig.pin_d7 = 35;
    cameraConfig.pin_xclk = 0;
    cameraConfig.pin_pclk = 22;
    cameraConfig.pin_vsync = 25;
    cameraConfig.pin_href = 23;
    cameraConfig.pin_sccb_sda = 26;
    cameraConfig.pin_sccb_scl = 27;
    cameraConfig.pin_pwdn = 32;
    cameraConfig.pin_reset = -1;
    cameraConfig.xclk_freq_hz = 20000000;
    cameraConfig.pixel_format = PIXFORMAT_GRAYSCALE;
    cameraConfig.frame_size = FRAMESIZE_QVGA;
    cameraConfig.fb_location = CAMERA_FB_IN_PSRAM;
    cameraConfig.fb_count = 1;
    cameraConfig.grab_mode = CAMERA_GRAB_LATEST;
    const esp_err_t error = esp_camera_init(&cameraConfig);
    if (error != ESP_OK) {
        return false;
    }
    sensor_t* sensor = esp_camera_sensor_get();
    if (!sensor || sensor->id.PID != OV2640_PID) {
        return false;
    }
    sensor->set_hmirror(sensor, cfg::CAMERA_HMIRROR);
    sensor->set_vflip(sensor, cfg::CAMERA_VFLIP);
    sensor->set_exposure_ctrl(sensor, 1);
    sensor->set_gain_ctrl(sensor, 1);
    return true;
}

bool isUsableTag(const apriltag_detection_t* detection) {
    if ((detection->id < 0 || detection->id >= cfg::TAG_COUNT) || detection->hamming != 0 ||
        !isfinite(detection->decision_margin) ||
        detection->decision_margin < cfg::MIN_DECISION_MARGIN)
        return false;
    for (int i = 0; i < 4; ++i) {
        const int next = (i + 1) % 4;
        if (!isfinite(detection->p[i][0]) || !isfinite(detection->p[i][1]) ||
            detection->p[i][0] < 2 || detection->p[i][0] > cfg::IMAGE_WIDTH - 3 ||
            detection->p[i][1] < 2 || detection->p[i][1] > cfg::IMAGE_HEIGHT - 3 ||
            hypotf(detection->p[i][0] - detection->p[next][0],
                   detection->p[i][1] - detection->p[next][1]) < cfg::MIN_TAG_EDGE_PX)
            return false;
    }
    return true;
}
} // namespace

void setup() {
    Serial.setRxBufferSize(512);
    Serial.begin(cfg::LINK_BAUD);
    Serial.setDebugOutput(false);
    pinMode(4, OUTPUT);
    digitalWrite(4, LOW);
    pinMode(cfg::IR_LEFT_PIN, INPUT_PULLUP);
    pinMode(cfg::IR_RIGHT_PIN, INPUT_PULLUP);
    if (xTaskCreatePinnedToCore(
            handleSensorsAndUart, "sensors_link", 4096, nullptr, 2, nullptr, 0) != pdPASS) {
        return;
    }
    // Keep network updating available even if the camera or PSRAM fails to start.
    cameraService::begin();
    cameraRuntime::setupOta();
    if (!psramFound() || ESP.getPsramSize() < 2 * 1024 * 1024) {
        return;
    }
    if (!setupCamera())
        return;
    family = tag36h11_create();
    detector = apriltag_detector_create();
    if (!family || !detector) {
        return;
    }
    apriltag_detector_add_family_bits(detector, family, 0); // exact codes only, lower memory
    detector->nthreads = 1;
    detector->quad_decimate = 1.5f;
    detector->quad_sigma = 0.0f;
    detector->refine_edges = true;
    detector->decode_sharpening = 0.25f;
    detector->debug = false;
    initialized = true;
}

void loop() {
    cameraRuntime::handleOta();
    if (cameraRuntime::busy()) {
        delay(10);
        return;
    }
    if (!initialized) {
        delay(10);
        return;
    }
    Vision result;
    result.frame = ++frameCounter;
    portENTER_CRITICAL(&visionMux);
    result.targetId = requested.targetId;
    result.requestId = requested.requestId;
    portEXIT_CRITICAL(&visionMux);
    camera_fb_t* frameBuffer = esp_camera_fb_get();
    if (!frameBuffer) {
        publish(result);
        delay(100);
        return;
    }
    result.captureUs =
        int64_t(frameBuffer->timestamp.tv_sec) * 1000000LL + frameBuffer->timestamp.tv_usec;
    if (frameBuffer->format != PIXFORMAT_GRAYSCALE || frameBuffer->width != cfg::IMAGE_WIDTH ||
        frameBuffer->height != cfg::IMAGE_HEIGHT ||
        frameBuffer->len < frameBuffer->width * frameBuffer->height) {
        esp_camera_fb_return(frameBuffer);
        publish(result);
        delay(100);
        return;
    }
    cameraService::publishFrame(frameBuffer, result.frame, result.captureUs);
    image_u8_t image = {int(frameBuffer->width),
                        int(frameBuffer->height),
                        int(frameBuffer->width),
                        frameBuffer->buf};
    zarray_t* detections = apriltag_detector_detect(detector, &image);
    if (detections) {
        result.ready = true;
        apriltag_detection_t* selected = nullptr;
        int matches = 0;
        for (int i = 0; i < zarray_size(detections); ++i) {
            apriltag_detection_t* detection;
            zarray_get(detections, i, &detection);
            if (isUsableTag(detection)) {
                result.visibleMask |= uint8_t(1u << detection->id);
                if (detection->id == result.targetId) {
                    selected = detection;
                    ++matches;
                }
            }
        }
        const auto& calibration = cameraService::calibration();
        if (matches == 1 && correctTagCorners(selected, calibration)) {
            apriltag_detection_info_t info = {};
            info.det = selected;
            info.tagsize = cfg::TAG_SIZE_M;
            info.fx = calibration.fx;
            info.fy = calibration.fy;
            info.cx = calibration.cx;
            info.cy = calibration.cy;
            apriltag_pose_t pose = {};
            const float error = estimate_tag_pose(&info, &pose);
            if (pose.t && pose.R) {
                const float x = MATD_EL(pose.t, 0, 0), z = MATD_EL(pose.t, 2, 0);
                const float range = hypotf(x, z);
                if (isfinite(error) && error <= cfg::MAX_POSE_ERROR && isfinite(x) && isfinite(z) &&
                    z >= 0.05f && range <= cfg::MAX_TARGET_RANGE_M) {
                    result.target = true;
                    result.rightMm = lroundf(x * 1000);
                    result.forwardMm = lroundf(z * 1000);
                    result.margin10 = uint16_t(fminf(selected->decision_margin * 10, 65535));
                }
            }
            if (pose.R)
                matd_destroy(pose.R);
            if (pose.t)
                matd_destroy(pose.t);
        }
        apriltag_detections_destroy(detections);
    }
    esp_camera_fb_return(frameBuffer);
    publish(result);
    delay(10); // let idle/watchdog tasks run between detections
}
