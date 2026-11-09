#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include "mobile_server.h"
#include "navigation.h"
#include "wheel_control.h"

namespace {
// GPIO numbers are used here; the board labels are listed beside them.
constexpr uint8_t LEFT_EN = 14, RIGHT_EN = 12;         // D5, D6; external 10k pulldowns
constexpr uint8_t LEFT_IN1 = 16, LEFT_IN2 = 0;         // D0, D3
constexpr uint8_t RIGHT_IN1 = 1, RIGHT_IN2 = 2;        // TX header, D4 (UART0 is swapped away)
constexpr uint8_t LEFT_ENCODER = 5, RIGHT_ENCODER = 4; // D1, D2
constexpr float PI_F = 3.14159265359f;
constexpr float LEFT_METRES_PER_PULSE =
    PI_F * cfg::LEFT_WHEEL_DIAMETER_M / cfg::LEFT_PULSES_PER_REV;
constexpr float RIGHT_METRES_PER_PULSE =
    PI_F * cfg::RIGHT_WHEEL_DIAMETER_M / cfg::RIGHT_PULSES_PER_REV;
volatile uint32_t leftTicks = 0, rightTicks = 0, leftEdgeUs = 0, rightEdgeUs = 0;
robot::PhoneControl phone;
robot::StreamFrame<robot::PACKET_SIZE> serialParser;
robot::Receiver receiver;
robot::PollWindow polls;
uint32_t pollToken = 0, lastPollMs = 0;
robot::Navigator navigator;
robot::WheelControl leftWheel(LEFT_METRES_PER_PULSE), rightWheel(RIGHT_METRES_PER_PULSE);
uint32_t lastControlMs = 0;
float odomX = 0, odomY = 0, odomHeading = 0;
uint32_t odomLeft = 0, odomRight = 0;
String buildStatusJson();
bool canConnect();
bool changeMaintenance(bool enabled);
MobileServer mobile(phone, buildStatusJson, canConnect, changeMaintenance);
bool otaListening = false, otaActive = false;
uint32_t wheelArmId = 0;
uint32_t bootId = 0;

void IRAM_ATTR onLeftEdge() {
    const uint32_t now = micros();
    if (now - leftEdgeUs >= cfg::ENCODER_DEBOUNCE_US) {
        ++leftTicks;
        leftEdgeUs = now;
    }
}

void IRAM_ATTR onRightEdge() {
    const uint32_t now = micros();
    if (now - rightEdgeUs >= cfg::ENCODER_DEBOUNCE_US) {
        ++rightTicks;
        rightEdgeUs = now;
    }
}

void applyMotor(
    uint8_t en, uint8_t in1, uint8_t in2, const robot::WheelControl& wheel, bool invert) {
    if (wheel.pwm == 0) {
        analogWrite(en, 0);
        digitalWrite(in1, LOW);
        digitalWrite(in2, LOW);
        return;
    }
    const bool forward = (wheel.direction > 0) != invert;
    digitalWrite(in1, forward ? HIGH : LOW);
    digitalWrite(in2, forward ? LOW : HIGH);
    analogWrite(en, wheel.pwm);
}

void updateMotorOutputs() {
    applyMotor(LEFT_EN, LEFT_IN1, LEFT_IN2, leftWheel, cfg::LEFT_MOTOR_INVERT);
    applyMotor(RIGHT_EN, RIGHT_IN1, RIGHT_IN2, rightWheel, cfg::RIGHT_MOTOR_INVERT);
}

void stopMotors() {
    leftWheel.stop(millis());
    rightWheel.stop(millis());
    updateMotorOutputs();
}

bool canConnect() {
    return !phone.maintenance && !otaActive && receiver.connected(millis(), cfg::LINK_TIMEOUT_MS) &&
           !receiver.latest.has(robot::MAINTENANCE) && !receiver.latest.has(robot::UPDATE_BUSY);
}

bool changeMaintenance(bool enabled) {
    if (otaActive || (!enabled && (!receiver.connected(millis(), cfg::LINK_TIMEOUT_MS) ||
                                   receiver.latest.has(robot::UPDATE_BUSY))))
        return false;
    phone.setMaintenance(enabled);
    stopMotors();
    return true;
}

void handleOta() {
    if (phone.maintenance && !otaListening) {
        ArduinoOTA.begin(false); // Fixed IP uploads; no discovery traffic needed.
        otaListening = true;
    } else if (!phone.maintenance && otaListening) {
        ArduinoOTA.end();
        otaListening = false;
    }
    if (otaListening) {
        stopMotors();
        ArduinoOTA.handle();
    }
}

void receivePackets() {
    for (int i = 0; i < 256 && Serial.available(); ++i) {
        const uint32_t now = millis();
        if (!serialParser.push(uint8_t(Serial.read()), 'A', now))
            continue;
        robot::Observation observation;
        uint32_t roundTrip;
        if (robot::decode(serialParser.bytes, robot::PACKET_SIZE, observation) &&
            polls.roundTrip(observation.pollToken, now, cfg::LINK_TIMEOUT_MS, roundTrip)) {
            observation.frameAgeMs = observation.frameAgeMs > UINT32_MAX - roundTrip
                                         ? UINT32_MAX
                                         : observation.frameAgeMs + roundTrip;
            receiver.accept(observation, now);
        }
    }
}

void sendPoll(uint32_t now) {
    if (now - lastPollMs < cfg::PACKET_PERIOD_MS)
        return;
    lastPollMs = now;
    robot::Poll request;
    request.token = ++pollToken;
    request.requestId = phone.command.requestId;
    request.targetId = phone.command.targetId;
    request.maintenance = phone.maintenance && !leftWheel.pwm && !rightWheel.pwm;
    uint8_t bytes[robot::POLL_SIZE];
    robot::encodePoll(request, bytes);
    if (Serial.availableForWrite() >= int(sizeof(bytes)) &&
        Serial.write(bytes, sizeof(bytes)) == sizeof(bytes))
        polls.issue(pollToken, now);
}

void updateOdometry(uint32_t left, uint32_t right) {
    const float leftDistance = (left - odomLeft) * LEFT_METRES_PER_PULSE * leftWheel.direction;
    const float rightDistance = (right - odomRight) * RIGHT_METRES_PER_PULSE * rightWheel.direction;
    odomLeft = left;
    odomRight = right;
    const float distance = (leftDistance + rightDistance) * 0.5f,
                headingChange = (rightDistance - leftDistance) / cfg::WHEEL_TRACK_M;
    odomX += distance * cosf(odomHeading + headingChange * 0.5f);
    odomY += distance * sinf(odomHeading + headingChange * 0.5f);
    odomHeading = atan2f(sinf(odomHeading + headingChange), cosf(odomHeading + headingChange));
}

String buildStatusJson() {
    const uint32_t now = millis();
    const auto& observation = receiver.latest;
    const bool link = receiver.connected(now, cfg::LINK_TIMEOUT_MS);
    const bool fresh =
        link && observation.has(robot::CAMERA_OK) && receiver.age(now) <= cfg::VISION_TIMEOUT_MS;
    const bool target = fresh && observation.has(robot::TARGET_VISIBLE) &&
                        observation.targetId == phone.command.targetId &&
                        observation.requestId == phone.command.requestId;
    uint32_t left, right;
    noInterrupts();
    left = leftTicks;
    right = rightTicks;
    interrupts();
    String json;
    json.reserve(800);
    json = "{\"bootId\":" + String(bootId) + ",\"now\":" + String(now) + ",\"state\":\"" +
           robot::stateName(navigator.state) + "\",\"reason\":\"" + navigator.reason;
    json += "\",\"mode\":\"";
    json += robot::modeName(phone.command.mode);
    json += "\",\"target\":" + String(phone.command.targetId);
    json += ",\"owned\":";
    json += phone.owned ? "true" : "false";
    json += ",\"link\":";
    json += link ? "true" : "false";
    json += ",\"enabled\":";
    json += phone.command.enabled ? "true" : "false";
    json += ",\"canConnect\":";
    json += canConnect() ? "true" : "false";
    json += ",\"maintenance\":";
    json += phone.maintenance ? "true" : "false";
    json += ",\"cameraMaintenance\":";
    json += (link && observation.has(robot::MAINTENANCE)) ? "true" : "false";
    json += ",\"updateBusy\":";
    json += (otaActive || (link && observation.has(robot::UPDATE_BUSY))) ? "true" : "false";
    json += ",\"visible\":" + String(fresh ? observation.visibleMask : 0) + ",\"range\":";
    json +=
        target ? String(hypotf(observation.rightMm, observation.forwardMm) * 0.001f, 3) : "null";
    json += ",\"age\":" + String(receiver.age(now)) + ",\"ir\":[" +
            String(observation.has(robot::OBSTACLE_LEFT) ? 1 : 0) + "," +
            String(observation.has(robot::OBSTACLE_RIGHT) ? 1 : 0) + "]";
    json += ",\"cameraReady\":";
    json += fresh ? "true" : "false";
    json += ",\"xMm\":" + String(observation.rightMm) +
            ",\"zMm\":" + String(observation.forwardMm) +
            ",\"margin\":" + String(observation.margin10 / 10.0f, 1);
    json += ",\"ticks\":[" + String(left) + "," + String(right) + "],\"pwm\":[" +
            String(leftWheel.pwm) + "," + String(rightWheel.pwm) + "]}";
    return json;
}
} // namespace

void setup() {
    // Remove the ENA/ENB jumpers and fit 10k pull-downs to keep motors off during reset.
    digitalWrite(LEFT_EN, LOW);
    digitalWrite(RIGHT_EN, LOW);
    pinMode(LEFT_EN, OUTPUT);
    pinMode(RIGHT_EN, OUTPUT);
    analogWriteRange(1023);
    analogWriteFreq(1000);
    Serial.setRxBufferSize(512);
    Serial.begin(cfg::LINK_BAUD);
    Serial.swap();
    Serial.setDebugOutput(false);
    // UART0: D7/GPIO13 RX, D8/GPIO15 TX. GPIO1 is now a motor-direction output.
    for (uint8_t pin : {LEFT_IN1, LEFT_IN2, RIGHT_IN1, RIGHT_IN2}) {
        digitalWrite(pin, LOW);
        pinMode(pin, OUTPUT);
    }
    updateMotorOutputs();
    pollToken = os_random();
    bootId = os_random();
    pinMode(LEFT_ENCODER, INPUT_PULLUP);
    pinMode(RIGHT_ENCODER, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(LEFT_ENCODER), onLeftEdge, RISING);
    attachInterrupt(digitalPinToInterrupt(RIGHT_ENCODER), onRightEdge, RISING);
    WiFi.persistent(false);
    WiFi.mode(WIFI_AP);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    const bool configOk = WiFi.softAPConfig(
        IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
    if (!configOk || !WiFi.softAP(cfg::WIFI_SSID, cfg::WIFI_PASSWORD, 6, false, 4)) {
        navigator.fault("phone network failed; reset to retry");
        while (true)
            delay(1000);
    }
    mobile.begin();
    ArduinoOTA.setHostname("april-motors");
    ArduinoOTA.setPort(8266);
    ArduinoOTA.setPassword(ROBOT_OTA_PASSWORD);
    ArduinoOTA.onStart([]() {
        phone.setMaintenance(true);
        otaActive = true;
        stopMotors();
    });
    ArduinoOTA.onError([](ota_error_t) {
        otaActive = false;
        phone.setMaintenance(true);
        stopMotors();
    });
}

void loop() {
    receivePackets();
    const uint32_t now = millis();
    phone.tick(now);
    if (!phone.command.enabled || phone.maintenance)
        stopMotors();
    if (phone.command.enabled && wheelArmId != phone.command.armId) {
        wheelArmId = phone.command.armId;
        leftWheel.resetFault();
        rightWheel.resetFault();
    }
    if (now - lastControlMs >= cfg::CONTROL_PERIOD_MS) {
        const uint32_t elapsed = now - lastControlMs;
        lastControlMs = now;
        uint32_t left, right;
        noInterrupts();
        left = leftTicks;
        right = rightTicks;
        interrupts();
        updateOdometry(left, right);
        if (elapsed > cfg::LINK_TIMEOUT_MS && (leftWheel.pwm || rightWheel.pwm))
            navigator.fault("control loop overrun");
        const robot::Drive drive = navigator.update(receiver, now, phone.command);
        if (navigator.state == robot::State::DISARMED) {
            leftWheel.resetFault();
            rightWheel.resetFault();
        }
        leftWheel.update(drive.left, left, now);
        rightWheel.update(drive.right, right, now);
        if (leftWheel.stalled || rightWheel.stalled) {
            navigator.fault(leftWheel.stalled ? "left encoder/motor stalled"
                                              : "right encoder/motor stalled");
            leftWheel.stop(now);
            rightWheel.stop(now);
        }
        updateMotorOutputs();
        if (navigator.state == robot::State::FAULT && phone.owned)
            phone.stop();
    }
    sendPoll(now);
    mobile.tick(millis());
    handleOta();
    delay(1);
}
