#pragma once
#include "robot_config.h"
#include "robot_protocol.h"

namespace robot {
enum class Mode : uint8_t { IDLE, AUTO, MANUAL };

inline const char* modeName(Mode mode) {
    return mode == Mode::AUTO ? "AUTO" : mode == Mode::MANUAL ? "MANUAL" : "IDLE";
}

struct Command {
    bool enabled = false;
    uint32_t armId = 0;
    Mode mode = Mode::IDLE;
    uint8_t targetId = 0;
    // A new request ID lets us reject camera results from the previous destination.
    uint32_t requestId = 0, manualMs = 0;
    int forward = 0, turn = 0; // -100..100; positive turn = LEFT
};

class PhoneControl {
public:
    Command command;
    bool owned = false;
    bool maintenance = false;
    uint32_t token = 0;

    void stop() {
        command.enabled = false;
        command.mode = Mode::IDLE;
        command.forward = command.turn = 0;
        ++command.requestId;
        owned = false;
    }

    void setMaintenance(bool enabled) {
        stop();
        maintenance = enabled;
    }

    void tick(uint32_t now) {
        if (owned && now - lastCommandMs_ > cfg::PHONE_TIMEOUT_MS)
            stop();
    }

    bool claim(uint32_t newToken, uint32_t now) {
        tick(now);
        if (owned || maintenance || newToken == 0)
            return false;
        stop();
        token = newToken;
        owned = true;
        command.enabled = true;
        ++command.armId;
        lastCommandMs_ = now;
        haveSequence_ = false;
        return true;
    }

    bool authorize(uint32_t suppliedToken, uint32_t sequence, uint32_t deadline, uint32_t now) {
        tick(now);
        // The phone uses the time from /api/state. Ignore commands that arrive too late.
        if (!owned || suppliedToken != token || deadline - now > cfg::COMMAND_MAX_AHEAD_MS ||
            (haveSequence_ && !newer(sequence, sequence_)))
            return false;
        haveSequence_ = true;
        sequence_ = sequence;
        lastCommandMs_ = now;
        return true;
    }

    bool navigate(uint8_t id) {
        if (!owned || !command.enabled || id >= cfg::TAG_COUNT)
            return false;
        command.targetId = id;
        command.mode = Mode::AUTO;
        command.forward = command.turn = 0;
        ++command.requestId;
        return true;
    }

    bool manual() {
        if (!owned || !command.enabled)
            return false;
        command.mode = Mode::MANUAL;
        command.forward = command.turn = 0;
        ++command.requestId;
        return true;
    }

    bool drive(int forward, int turn, uint32_t now) {
        if (!owned || !command.enabled || command.mode != Mode::MANUAL || forward < -100 ||
            forward > 100 || turn < -100 || turn > 100)
            return false;
        command.forward = forward;
        command.turn = turn;
        command.manualMs = now;
        return true;
    }

private:
    uint32_t lastCommandMs_ = 0, sequence_ = 0;
    bool haveSequence_ = false;
};
} // namespace robot
