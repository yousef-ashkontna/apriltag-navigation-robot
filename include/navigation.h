#pragma once
#include <math.h>
#include "robot_config.h"
#include "robot_protocol.h"
#include "phone_control.h"

namespace robot {
inline float clamp(float value, float low, float high) {
    return fmaxf(low, fminf(value, high));
}
enum class State {
    DISARMED,
    ARMING,
    IDLE,
    MANUAL,
    WAIT_VISION,
    SEARCH_TIMEOUT,
    SEARCH,
    TRACK,
    AVOID,
    BLOCKED,
    ARRIVED,
    FAULT
};

inline const char* stateName(State state) {
    switch (state) {
    case State::DISARMED:
        return "DISARMED";
    case State::ARMING:
        return "ARMING";
    case State::IDLE:
        return "IDLE";
    case State::MANUAL:
        return "MANUAL";
    case State::WAIT_VISION:
        return "WAIT_VISION";
    case State::SEARCH_TIMEOUT:
        return "SEARCH_TIMEOUT";
    case State::SEARCH:
        return "SEARCH";
    case State::TRACK:
        return "TRACK";
    case State::AVOID:
        return "AVOID";
    case State::BLOCKED:
        return "BLOCKED";
    case State::ARRIVED:
        return "ARRIVED";
    default:
        return "FAULT";
    }
}

struct Drive {
    float left = 0, right = 0;
};

inline Drive differential(float forwardSpeed, float turnRate) {
    // Positive turn rate turns left: slow the left wheel and speed up the right.
    Drive wheelSpeeds;
    wheelSpeeds.left = forwardSpeed - turnRate * cfg::WHEEL_TRACK_M * 0.5f;
    wheelSpeeds.right = forwardSpeed + turnRate * cfg::WHEEL_TRACK_M * 0.5f;
    const float largest = fmaxf(fabsf(wheelSpeeds.left), fabsf(wheelSpeeds.right));
    if (largest > cfg::MAX_WHEEL_MPS) {
        wheelSpeeds.left *= cfg::MAX_WHEEL_MPS / largest;
        wheelSpeeds.right *= cfg::MAX_WHEEL_MPS / largest;
    }
    return wheelSpeeds;
}

class Navigator {
public:
    State state = State::DISARMED;
    const char* reason = "connect controls in the app";

    void fault(const char* message) {
        state = State::FAULT;
        reason = message;
        armed_ = false;
    }

    Drive update(const Receiver& receiver, uint32_t now, const Command& command) {
        if (!receiver.connected(now, cfg::LINK_TIMEOUT_MS)) {
            if (armed_)
                fault("camera link lost; reconnect controls after fixing it");
            return {};
        }
        const Observation& observation = receiver.latest;
        if (hasCameraSession_ && session_ != observation.session && armed_)
            fault("camera restarted; reconnect controls");
        session_ = observation.session;
        hasCameraSession_ = true;
        if (observation.has(MAINTENANCE) || observation.has(UPDATE_BUSY)) {
            if (command.enabled)
                fault("camera is in maintenance; reconnect controls when finished");
            return {};
        }
        if (!command.enabled) {
            armed_ = false;
            arrivalCount_ = 0;
            searching_ = avoiding_ = waitingForNewFrame_ = false;
            if (state != State::FAULT) {
                state = State::DISARMED;
                reason = "connect controls in the app";
            }
            return {};
        }
        // Only a new Connect controls session can clear a latched fault.
        if (command.armId != lastArmId_) {
            lastArmId_ = command.armId;
            armed_ = true;
            armSince_ = now;
            arrivalCount_ = 0;
            searching_ = avoiding_ = waitingForNewFrame_ = false;
            state = State::ARMING;
        }
        if (!armed_ || state == State::FAULT)
            return {};
        if (command.requestId != lastRequestId_) {
            lastRequestId_ = command.requestId;
            arrivalCount_ = 0;
            searching_ = avoiding_ = waitingForNewFrame_ = false;
            state = State::IDLE;
        }
        if (now - armSince_ < cfg::ARM_DELAY_MS) {
            state = State::ARMING;
            reason = "arming delay";
            return {};
        }
        if (command.mode == Mode::IDLE) {
            state = State::IDLE;
            reason = "choose a destination or manual mode";
            return {};
        }
        const bool left = observation.has(OBSTACLE_LEFT), right = observation.has(OBSTACLE_RIGHT);
        if (command.mode == Mode::MANUAL) {
            // Manual recovery only needs the sensors and UART, not a visible tag.
            state = State::MANUAL;
            reason = "hold a drive button";
            if (now - command.manualMs > cfg::MANUAL_TIMEOUT_MS)
                return {};
            const float forwardSpeed =
                clamp(command.forward / 100.0f, -1, 1) *
                (command.forward < 0 ? cfg::MANUAL_REVERSE_MPS : cfg::MANUAL_FORWARD_MPS);
            const float turnRate = clamp(command.turn / 100.0f, -1, 1) * cfg::MANUAL_TURN_RADPS;
            if (((left || right) && forwardSpeed > 0) || (left && turnRate > 0) ||
                (right && turnRate < 0)) {
                state = State::BLOCKED;
                reason = "front IR blocks this direction; reverse is available";
                return {};
            }
            return differential(forwardSpeed, turnRate);
        }
        if (state == State::ARRIVED || state == State::SEARCH_TIMEOUT)
            return {};
        if (!observation.has(CAMERA_OK) || receiver.age(now) > cfg::VISION_TIMEOUT_MS ||
            observation.targetId != command.targetId ||
            observation.requestId != command.requestId) {
            arrivalCount_ = 0;
            state = State::WAIT_VISION;
            reason = "waiting for fresh selected-tag frame; manual mode is available";
            return {};
        }
        if (left || right) {
            arrivalCount_ = 0;
            searching_ = false;
            if (!avoiding_) {
                avoiding_ = true;
                avoidSince_ = now;
            }
            clearSince_ = now;
            if (now - avoidSince_ >= cfg::AVOID_TIMEOUT_MS) {
                state = State::BLOCKED;
                reason = "obstacle did not clear; use manual recovery";
                return {};
            }
            if (left && right) {
                state = State::BLOCKED;
                reason = "both front IRs blocked";
                return {};
            }
            state = State::AVOID;
            reason = "turning away from front IR";
            return differential(0, left ? -cfg::AVOID_TURN_RADPS : cfg::AVOID_TURN_RADPS);
        }
        if (avoiding_) {
            state = State::AVOID;
            reason = "waiting for clear front";
            if (now - clearSince_ < cfg::OBSTACLE_CLEAR_MS)
                return {};
            avoiding_ = false;
            waitingForNewFrame_ = true;
            avoidedFrame_ = observation.frame;
            avoidFinishedMs_ = now;
        }
        if (waitingForNewFrame_) {
            // The robot has turned, so the image taken before avoidance is no longer useful.
            if (observation.frame == avoidedFrame_ || receiver.age(now) > now - avoidFinishedMs_)
                return {};
            waitingForNewFrame_ = false;
        }
        if (!observation.has(TARGET_VISIBLE)) {
            arrivalCount_ = 0;
            if (!searching_) {
                searching_ = true;
                searchSince_ = now;
            }
            if (now - searchSince_ > cfg::SEARCH_TIMEOUT_MS) {
                state = State::SEARCH_TIMEOUT;
                reason = "tag not found; use manual or choose a destination again";
                return {};
            }
            state = State::SEARCH;
            reason = "scanning for target tag";
            const uint32_t phase =
                (now - searchSince_) % (cfg::SEARCH_REST_MS + cfg::SEARCH_TURN_MS);
            return phase < cfg::SEARCH_REST_MS ? Drive{} : differential(0, cfg::SEARCH_TURN_RADPS);
        }
        searching_ = false;
        const float x = observation.rightMm * 0.001f, z = observation.forwardMm * 0.001f;
        const float range = hypotf(x, z), bearing = atan2f(x, z);
        state = State::TRACK;
        reason = "approaching target";
        if (range <= cfg::STOP_RANGE_M + cfg::ARRIVAL_RANGE_TOLERANCE_M &&
            fabsf(bearing) <= cfg::ARRIVAL_BEARING_RAD) {
            if (arrivalCount_ == 0 || arrivalFrame_ != observation.frame) {
                ++arrivalCount_;
                arrivalFrame_ = observation.frame;
            }
            if (arrivalCount_ >= cfg::ARRIVAL_FRAMES) {
                state = State::ARRIVED;
                reason = "destination reached; choose the next destination";
            }
            return {};
        }
        arrivalCount_ = 0;
        const float turnRate =
            clamp(-cfg::BEARING_KP * bearing, -cfg::MAX_TURN_RADPS, cfg::MAX_TURN_RADPS);
        const float forwardSpeed =
            fabsf(bearing) > cfg::ALIGN_FIRST_RAD
                ? 0.0f
                : clamp(cfg::RANGE_KP * (range - cfg::STOP_RANGE_M), 0.0f, cfg::MAX_FORWARD_MPS);
        return differential(forwardSpeed, turnRate);
    }

private:
    bool armed_ = false, hasCameraSession_ = false;
    bool searching_ = false, avoiding_ = false, waitingForNewFrame_ = false;
    uint32_t session_ = 0, lastArmId_ = 0, armSince_ = 0, searchSince_ = 0;
    uint32_t lastRequestId_ = 0;
    uint32_t avoidSince_ = 0, clearSince_ = 0, avoidedFrame_ = 0, arrivalFrame_ = 0,
             avoidFinishedMs_ = 0;
    uint8_t arrivalCount_ = 0;
};
} // namespace robot
