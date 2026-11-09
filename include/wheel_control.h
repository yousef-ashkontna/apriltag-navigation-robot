#pragma once
#include "navigation.h"

namespace robot {
// Each encoder has one output, so we take the direction from the motor command.
class WheelControl {
public:
    explicit WheelControl(float metresPerPulse) : metresPerPulse_(metresPerPulse) {}

    int pwm = 0;
    int direction = 0;
    float speed = 0; // unsigned speed magnitude, m/s
    bool stalled = false;

    void resetFault() {
        stalled = false;
        poweredNoPulseMs_ = 0;
    }

    void stop(uint32_t now) {
        if (pwm != 0)
            stoppedSince_ = now;
        pwm = 0;
        integral_ = 0;
    }

    void update(float target, uint32_t ticks, uint32_t now) {
        if (!initialized_) {
            initialized_ = true;
            lastMs_ = windowMs_ = now;
            lastTicks_ = windowTicks_ = ticks;
            stoppedSince_ = now;
        }
        const uint32_t elapsed = now - lastMs_;
        if (ticks != lastTicks_)
            poweredNoPulseMs_ = 0;
        else if (pwm > 0)
            poweredNoPulseMs_ += elapsed;
        lastTicks_ = ticks;
        lastMs_ = now;
        if (now - windowMs_ >= cfg::SPEED_WINDOW_MS) {
            speed = (ticks - windowTicks_) * metresPerPulse_ * 1000.0f / (now - windowMs_);
            windowMs_ = now;
            windowTicks_ = ticks;
        }
        if (poweredNoPulseMs_ >= cfg::STALL_TIMEOUT_MS)
            stalled = true;
        if (stalled || fabsf(target) < 0.005f) {
            stop(now);
            return;
        }
        const int requestedDirection = target > 0 ? 1 : -1;
        if (direction != 0 && direction != requestedDirection) {
            stop(now);
            if (now - stoppedSince_ < cfg::REVERSAL_PAUSE_MS)
                return;
        }
        direction = requestedDirection;
        const float error = fabsf(target) - speed;
        const float candidateIntegral =
            clamp(integral_ + error * elapsed * 0.001f, -cfg::INTEGRAL_LIMIT, cfg::INTEGRAL_LIMIT);
        const float base =
            cfg::PWM_START + cfg::PWM_FEEDFORWARD * fabsf(target) + cfg::SPEED_KP * error;
        const float candidate = base + cfg::SPEED_KI * candidateIntegral;
        // Stop adding to the integral when PWM hits a limit, unless the error brings it back.
        if ((candidate >= 0 && candidate <= cfg::PWM_MAX) ||
            (candidate > cfg::PWM_MAX && error < 0) || (candidate < 0 && error > 0))
            integral_ = candidateIntegral;
        pwm = int(clamp(base + cfg::SPEED_KI * integral_, 0, cfg::PWM_MAX));
    }

private:
    float metresPerPulse_, integral_ = 0;
    bool initialized_ = false;
    uint32_t lastMs_ = 0, lastTicks_ = 0, windowMs_ = 0, windowTicks_ = 0;
    uint32_t stoppedSince_ = 0, poweredNoPulseMs_ = 0;
};
} // namespace robot
