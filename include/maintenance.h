#pragma once
#include "robot_config.h"

namespace robot {
// The camera may write flash only after fresh UART polls confirm the motors are disabled.
class Maintenance {
public:
    void observe(bool requested, uint32_t now) {
        if (!requested || !present_ || now - received_ > cfg::LINK_TIMEOUT_MS) {
            since_ = now;
            requested_ = requested;
        } else if (!requested_) {
            since_ = now;
            requested_ = true;
        }
        received_ = now;
        present_ = true;
    }

    bool ready(uint32_t now) const {
        return present_ && requested_ && now - received_ <= cfg::LINK_TIMEOUT_MS &&
               now - since_ >= cfg::MAINTENANCE_SETTLE_MS;
    }

private:
    bool requested_ = false, present_ = false;
    uint32_t since_ = 0, received_ = 0;
};
} // namespace robot
