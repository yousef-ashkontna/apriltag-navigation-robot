#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace robot {
enum Flag : uint8_t {
    CAMERA_OK = 1,
    TARGET_VISIBLE = 2,
    OBSTACLE_LEFT = 4,
    OBSTACLE_RIGHT = 8,
    MAINTENANCE = 16,
    UPDATE_BUSY = 32
};

struct Observation {
    uint8_t flags = 0, targetId = 0, visibleMask = 0;
    uint32_t session = 0, sequence = 0, frame = 0, frameAgeMs = UINT32_MAX, pollToken = 0,
             requestId = 0;
    int32_t rightMm = 0, forwardMm = 0;
    uint16_t margin10 = 0;

    bool has(Flag flag) const {
        return (flags & flag) != 0;
    }
};

struct Poll {
    uint32_t token = 0, requestId = 0;
    uint8_t targetId = 0;
    bool maintenance = false;
};

constexpr uint8_t PROTOCOL_VERSION = 3;
constexpr size_t PACKET_SIZE = 42, POLL_SIZE = 15;

inline uint16_t crc16(const uint8_t* data, size_t length) {
    uint16_t crc = 0xffff;
    while (length--) {
        crc ^= uint16_t(*data++) << 8;
        for (int i = 0; i < 8; ++i)
            crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1);
    }
    return crc;
}

inline void put32(uint8_t* packet, uint32_t value) {
    for (int i = 0; i < 4; ++i)
        packet[i] = uint8_t(value >> (8 * i));
}

inline uint32_t get32(const uint8_t* packet) {
    return uint32_t(packet[0]) | (uint32_t(packet[1]) << 8) | (uint32_t(packet[2]) << 16) |
           (uint32_t(packet[3]) << 24);
}

inline int32_t signed32(uint32_t value) {
    return value <= INT32_MAX ? int32_t(value) : -1 - int32_t(UINT32_MAX - value);
}

inline void seal(uint8_t* packet, size_t length) {
    const uint16_t checksum = crc16(packet, length - 2);
    packet[length - 2] = uint8_t(checksum);
    packet[length - 1] = uint8_t(checksum >> 8);
}

inline bool validCrc(const uint8_t* packet, size_t length) {
    return crc16(packet, length - 2) ==
           (uint16_t(packet[length - 2]) | (uint16_t(packet[length - 1]) << 8));
}

inline void encode(const Observation& observation, uint8_t* packet) {
    packet[0] = 'A';
    packet[1] = 'R';
    packet[2] = PROTOCOL_VERSION;
    packet[3] = observation.flags;
    put32(packet + 4, observation.session);
    put32(packet + 8, observation.sequence);
    put32(packet + 12, observation.frame);
    put32(packet + 16, observation.frameAgeMs);
    put32(packet + 20, uint32_t(observation.rightMm));
    put32(packet + 24, uint32_t(observation.forwardMm));
    put32(packet + 28, observation.pollToken);
    packet[32] = uint8_t(observation.margin10);
    packet[33] = uint8_t(observation.margin10 >> 8);
    packet[34] = observation.targetId;
    packet[35] = observation.visibleMask;
    put32(packet + 36, observation.requestId);
    seal(packet, PACKET_SIZE);
}

inline bool decode(const uint8_t* packet, size_t length, Observation& observation) {
    if (length != PACKET_SIZE || packet[0] != 'A' || packet[1] != 'R' ||
        packet[2] != PROTOCOL_VERSION || (packet[3] & ~63) || packet[34] >= 4 ||
        (packet[35] & ~15) || !validCrc(packet, length))
        return false;
    Observation decoded;
    decoded.flags = packet[3];
    decoded.session = get32(packet + 4);
    decoded.sequence = get32(packet + 8);
    decoded.frame = get32(packet + 12);
    decoded.frameAgeMs = get32(packet + 16);
    decoded.rightMm = signed32(get32(packet + 20));
    decoded.forwardMm = signed32(get32(packet + 24));
    decoded.pollToken = get32(packet + 28);
    decoded.margin10 = uint16_t(packet[32]) | (uint16_t(packet[33]) << 8);
    decoded.targetId = packet[34];
    decoded.visibleMask = packet[35];
    decoded.requestId = get32(packet + 36);
    if (decoded.has(TARGET_VISIBLE) &&
        (!decoded.has(CAMERA_OK) || decoded.forwardMm < 50 || decoded.forwardMm > 10000 ||
         decoded.rightMm < -10000 || decoded.rightMm > 10000))
        return false;
    observation = decoded;
    return true;
}

inline void encodePoll(const Poll& request, uint8_t* packet) {
    packet[0] = 'Q';
    packet[1] = 'R';
    packet[2] = PROTOCOL_VERSION;
    packet[3] = request.targetId;
    put32(packet + 4, request.token);
    put32(packet + 8, request.requestId);
    packet[12] = request.maintenance ? 1 : 0;
    seal(packet, POLL_SIZE);
}

inline bool decodePoll(const uint8_t* packet, size_t length, Poll& request) {
    if (length != POLL_SIZE || packet[0] != 'Q' || packet[1] != 'R' ||
        packet[2] != PROTOCOL_VERSION || packet[3] >= 4 || packet[12] > 1 ||
        !validCrc(packet, length))
        return false;
    request.targetId = packet[3];
    request.token = get32(packet + 4);
    request.requestId = get32(packet + 8);
    request.maintenance = packet[12] == 1;
    return true;
}

inline bool newer(uint32_t incoming, uint32_t previous) {
    // Subtracting unsigned counters also handles the jump from UINT32_MAX back to zero.
    const uint32_t difference = incoming - previous;
    return difference && difference < 0x80000000u;
}

// Slide through the incoming bytes until a complete packet passes its CRC check.
template <size_t N> class StreamFrame {
public:
    uint8_t bytes[N] = {};

    bool push(uint8_t byte, uint8_t magic, uint32_t now) {
        if (count_ && now - lastMs_ > 100)
            count_ = 0;
        lastMs_ = now;
        if (count_ == N) {
            memmove(bytes, bytes + 1, N - 1);
            --count_;
        }
        bytes[count_++] = byte;
        return count_ == N && bytes[0] == magic && bytes[1] == 'R' &&
               bytes[2] == PROTOCOL_VERSION && validCrc(bytes, N);
    }

private:
    size_t count_ = 0;
    uint32_t lastMs_ = 0;
};

class PollWindow {
public:
    void issue(uint32_t token, uint32_t now) {
        tokens_[next_] = token;
        times_[next_] = now;
        valid_[next_] = true;
        next_ = (next_ + 1) % 16;
    }

    bool roundTrip(uint32_t token, uint32_t now, uint32_t maxMs, uint32_t& elapsed) const {
        for (int i = 0; i < 16; ++i)
            if (valid_[i] && tokens_[i] == token) {
                elapsed = now - times_[i];
                return elapsed <= maxMs;
            }
        return false;
    }

private:
    uint32_t tokens_[16] = {}, times_[16] = {};
    bool valid_[16] = {};
    uint8_t next_ = 0;
};

class Receiver {
public:
    Observation latest;
    bool present = false;
    uint32_t receivedMs = 0;

    bool accept(const Observation& observation, uint32_t now) {
        if (present && observation.session == latest.session &&
            !newer(observation.sequence, latest.sequence))
            return false;
        latest = observation;
        receivedMs = now;
        present = true;
        return true;
    }

    bool connected(uint32_t now, uint32_t timeout) const {
        return present && now - receivedMs <= timeout;
    }

    uint32_t age(uint32_t now) const {
        const uint32_t elapsedMs = now - receivedMs;
        return latest.frameAgeMs > UINT32_MAX - elapsedMs ? UINT32_MAX
                                                          : latest.frameAgeMs + elapsedMs;
    }
};
} // namespace robot
