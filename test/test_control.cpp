#include <cassert>
#include <cmath>
#include <iostream>
#include "wheel_control.h"
#include "maintenance.h"
using namespace robot;

bool stopped(Drive d) {
    return d.left == 0 && d.right == 0;
}

struct Rig {
    Receiver rx;
    Navigator nav;
    Command command;
    Observation o;
    uint32_t now = 0;

    Rig() {
        command.mode = Mode::AUTO;
        o.session = 123;
        o.flags = CAMERA_OK | TARGET_VISIBLE;
        o.forwardMm = 1000;
        o.frameAgeMs = 10;
    }

    Drive step(uint32_t dt = 20, bool newFrame = true) {
        now += dt;
        ++o.sequence;
        if (newFrame)
            ++o.frame;
        assert(rx.accept(o, now));
        return nav.update(rx, now, command);
    }

    void arm() {
        command.enabled = false;
        for (int i = 0; i < 30; ++i)
            assert(stopped(step()));
        command.enabled = true;
        ++command.armId;
        for (int i = 0; i <= 100; ++i)
            step();
        assert(nav.state == State::TRACK);
    }
};

void protocolTests() {
    Observation a, b;
    a.flags = CAMERA_OK | TARGET_VISIBLE;
    a.session = 0xabcdef01;
    a.sequence = UINT32_MAX;
    a.frame = 40;
    a.frameAgeMs = 99;
    a.rightMm = -234;
    a.forwardMm = 1234;
    a.margin10 = 723;
    a.pollToken = 987;
    a.targetId = 3;
    a.requestId = 99;
    a.visibleMask = 15;
    uint8_t bytes[PACKET_SIZE];
    encode(a, bytes);
    assert(decode(bytes, sizeof(bytes), b));
    assert(b.rightMm == -234 && b.forwardMm == 1234 && b.session == a.session);
    assert(b.sequence == UINT32_MAX && b.margin10 == 723 && b.frameAgeMs == 99 &&
           b.pollToken == 987);
    assert(b.targetId == 3 && b.requestId == 99 && b.visibleMask == 15);
    StreamFrame<PACKET_SIZE> parser;
    int frames = 0;
    for (int i = 0; i < 80; ++i)
        assert(!parser.push(uint8_t(i), 'A', 0));
    for (size_t i = 0; i < sizeof(bytes); ++i)
        if (parser.push(bytes[i], 'A', 10))
            ++frames;
    assert(frames == 1);
    for (size_t i = 0; i < 10; ++i)
        parser.push(bytes[i], 'A', 20);
    for (size_t i = 10; i < sizeof(bytes); ++i)
        assert(!parser.push(bytes[i], 'A', 200));
    for (size_t i = 0; i < sizeof(bytes); ++i)
        if (parser.push(bytes[i], 'A', 220))
            ++frames;
    assert(frames == 2);
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        bytes[i] ^= 1;
        assert(!decode(bytes, sizeof(bytes), b));
        bytes[i] ^= 1;
    }
    assert(!decode(bytes, sizeof(bytes) - 1, b));
    bytes[2] = 2; // Firmware from before app arming must not interoperate silently.
    seal(bytes, sizeof(bytes));
    assert(!decode(bytes, sizeof(bytes), b));
    a.forwardMm = -1;
    encode(a, bytes);
    assert(!decode(bytes, sizeof(bytes), b));
    Receiver rx;
    assert(rx.accept(a, UINT32_MAX - 50));
    assert(!rx.accept(a, 10));
    assert(rx.connected(25, 100));
    assert(!rx.connected(1000, 100));
    a.sequence = 0;
    assert(rx.accept(a, 25));
    a.sequence = UINT32_MAX;
    assert(!rx.accept(a, 30));
    a.session++;
    assert(rx.accept(a, 50));
    a.frameAgeMs = UINT32_MAX - 2;
    ++a.sequence;
    assert(rx.accept(a, 60));
    assert(rx.age(100) == UINT32_MAX);
    assert(newer(0, UINT32_MAX) && !newer(7, 7));
    uint8_t poll[POLL_SIZE];
    Poll query, decoded;
    query.token = 1234;
    query.targetId = 3;
    query.requestId = 9;
    query.maintenance = true;
    uint32_t elapsed = 0;
    encodePoll(query, poll);
    assert(decodePoll(poll, sizeof(poll), decoded) && decoded.token == 1234 &&
           decoded.targetId == 3 && decoded.requestId == 9 && decoded.maintenance);
    poll[4] ^= 1;
    assert(!decodePoll(poll, sizeof(poll), decoded));
    encodePoll(query, poll);
    poll[12] = 2;
    seal(poll, sizeof(poll));
    assert(!decodePoll(poll, sizeof(poll), decoded));
    PollWindow window;
    assert(!window.roundTrip(0, 0, 300, elapsed));
    window.issue(123, UINT32_MAX - 10);
    assert(window.roundTrip(123, 9, 300, elapsed) && elapsed == 20);
    assert(!window.roundTrip(123, 500, 300, elapsed));
    assert(!window.roundTrip(124, 9, 300, elapsed));
    for (uint32_t i = 0; i < 17; ++i)
        window.issue(i, i * 20);
    assert(!window.roundTrip(0, 350, 1000, elapsed));
    assert(window.roundTrip(16, 350, 300, elapsed));
}

void navigationTests() {
    {
        Rig r;
        for (int i = 0; i < 200; ++i)
            assert(stopped(r.step()));
        assert(r.nav.state == State::DISARMED);
    }
    {
        Rig r;
        r.arm();
        Drive d = r.step();
        assert(d.left > 0 && d.right > 0);
        r.o.rightMm = 500;
        d = r.step();
        assert(d.left > 0 && d.right < 0); // target right -> clockwise
        r.o.rightMm = -500;
        d = r.step();
        assert(d.left < 0 && d.right > 0);
        r.command.enabled = false;
        assert(stopped(r.step()));
        assert(r.nav.state == State::DISARMED);
    }
    {
        Rig r;
        r.arm();
        assert(stopped(r.nav.update(r.rx, r.now + cfg::LINK_TIMEOUT_MS + 1, r.command)));
        assert(r.nav.state == State::FAULT);
        for (int i = 0; i < 200; ++i)
            assert(stopped(r.step())); // cannot rearm the old app session
        r.command.enabled = false;
        r.arm();
        assert(r.nav.state == State::TRACK);
    }
    {
        Rig r;
        r.arm();
        r.o.frameAgeMs = cfg::VISION_TIMEOUT_MS + 1;
        assert(stopped(r.step()));
        assert(r.nav.state == State::WAIT_VISION);
    }
    {
        Rig r;
        r.arm();
        r.o.flags &= ~CAMERA_OK;
        assert(stopped(r.step()));
        assert(r.nav.state == State::WAIT_VISION);
    }
    {
        Rig r;
        r.arm();
        ++r.o.session;
        assert(stopped(r.step()));
        assert(r.nav.state == State::FAULT);
    }
    {
        Rig r;
        r.arm();
        r.o.flags |= OBSTACLE_LEFT;
        Drive d = r.step();
        assert(d.left > 0 && d.right < 0);
        r.o.flags |= OBSTACLE_RIGHT;
        assert(stopped(r.step()));
        assert(r.nav.state == State::BLOCKED);
        for (int i = 0; i < 130; ++i) {
            assert(stopped(r.step()));
        }
        assert(r.nav.state == State::BLOCKED);
    }
    {
        Rig r;
        r.arm();
        r.o.flags |= OBSTACLE_RIGHT;
        Drive d = r.step();
        assert(d.left < 0 && d.right > 0);
        r.o.flags &= ~OBSTACLE_RIGHT;
        for (int i = 0; i < 25; ++i)
            assert(stopped(r.step(20, false)));
        assert(!stopped(r.step()));
    }
    {
        Rig r;
        r.arm();
        r.o.forwardMm = 360;
        assert(stopped(r.step()));
        for (int i = 0; i < 10; ++i)
            assert(stopped(r.step(20, false)));
        assert(r.nav.state != State::ARRIVED);
        r.step();
        r.step();
        assert(r.nav.state == State::ARRIVED);
        r.o.forwardMm = 1000;
        assert(stopped(r.step()));
        assert(r.nav.state == State::ARRIVED);
    }
    {
        Rig r;
        r.arm();
        r.o.flags &= ~TARGET_VISIBLE;
        assert(stopped(r.step()));
        Drive d;
        for (int i = 0; i < 26; ++i)
            d = r.step();
        assert(d.left < 0 && d.right > 0);
        for (int i = 0; i < 1500; ++i) {
            r.step();
        }
        assert(r.nav.state == State::SEARCH_TIMEOUT);
    }
    {
        Rig r;
        r.now = UINT32_MAX - 1000;
        r.arm();
        assert(!stopped(r.step()));
    }
    Drive limited = differential(2, 4);
    assert(std::fabs(limited.left) <= cfg::MAX_WHEEL_MPS + 1e-6f);
    assert(std::fabs(limited.right) <= cfg::MAX_WHEEL_MPS + 1e-6f);
}

void wheelTests() {
    WheelControl w(0.01f);
    w.update(0.1f, 0, 0);
    assert(w.pwm > 0 && w.direction == 1);
    w.update(-0.1f, 1, 20);
    assert(w.pwm == 0);
    w.update(-0.1f, 1, 200);
    assert(w.pwm == 0);
    w.update(-0.1f, 1, 220);
    assert(w.pwm > 0 && w.direction == -1);
    for (uint32_t t = 240; t < 2100; t += 20)
        w.update(-0.1f, 1, t);
    assert(w.stalled && w.pwm == 0);
    w.resetFault();
    w.update(0, 1, 2200);
    assert(!w.stalled && w.pwm == 0);
    WheelControl moving(0.01f);
    for (uint32_t t = 0; t <= 5000; t += 20)
        moving.update(0.1f, t / 100, t);
    assert(!moving.stalled && std::fabs(moving.speed - 0.1f) < 0.001f);
    assert(moving.pwm >= 0 && moving.pwm <= cfg::PWM_MAX);
    WheelControl rollover(0.01f);
    rollover.update(0, UINT32_MAX - 1, UINT32_MAX - 100);
    rollover.update(0, 0, 100);
    assert(rollover.speed > 0.09f && rollover.speed < 0.11f);
    WheelControl interrupted(0.01f);
    for (uint32_t t = 0; t < 6000; t += 20)
        interrupted.update((t % 1000) < 500 ? .1f : 0, 0, t);
    assert(interrupted.stalled); // search pauses must not conceal a dead encoder
}

void phoneTests() {
    PhoneControl p;
    assert(p.claim(123, 100));
    assert(!p.claim(456, 200));
    assert(!p.authorize(456, 1, 500, 200));
    assert(!p.authorize(123, 1, 199, 200));
    assert(!p.authorize(123, 1, 200 + cfg::COMMAND_MAX_AHEAD_MS + 1, 200));
    assert(p.authorize(123, 1, 500, 200));
    assert(p.navigate(3));
    assert(p.command.targetId == 3);
    assert(!p.navigate(4));
    assert(!p.authorize(123, 1, 500, 220));
    assert(!p.authorize(123, 0, 500, 220));
    p.manual();
    assert(p.drive(-100, 0, 250));
    assert(!p.drive(101, 0, 250));
    p.tick(200 + cfg::PHONE_TIMEOUT_MS + 1);
    assert(!p.owned && p.command.mode == Mode::IDLE);
    assert(!p.authorize(123, 2, 1400, 1100));
    assert(p.claim(456, 1100));
    p.stop();
    assert(!p.authorize(456, 1, 1400, 1150));
    assert(p.claim(789, UINT32_MAX - 20));
    assert(p.authorize(789, UINT32_MAX, 30, UINT32_MAX - 10));
    assert(p.authorize(789, 0, 30, 0));
    {
        Rig r;
        r.arm();
        r.command.mode = Mode::MANUAL;
        ++r.command.requestId;
        r.o.flags = 0;
        r.o.frameAgeMs = UINT32_MAX;
        r.command.forward = 100;
        r.command.manualMs = r.now + 20;
        assert(!stopped(r.step())); // no camera required
        r.step(cfg::MANUAL_TIMEOUT_MS + 1);
        assert(stopped(r.step()));
        r.o.flags |= OBSTACLE_LEFT | OBSTACLE_RIGHT;
        r.command.manualMs = r.now + 20;
        assert(stopped(r.step()));
        r.command.forward = -100;
        r.command.manualMs = r.now + 20;
        Drive d = r.step();
        assert(d.left < 0 && d.right < 0);
        r.command.forward = 0;
        r.command.turn = 100;
        r.command.manualMs = r.now + 20;
        assert(stopped(r.step()));
        r.o.flags = OBSTACLE_RIGHT;
        r.command.manualMs = r.now + 20;
        d = r.step();
        assert(d.left < 0 && d.right > 0);
        r.nav.fault("stall");
        ++r.command.requestId;
        r.command.manualMs = r.now + 20;
        assert(stopped(r.step()));
    }
    {
        Rig r;
        r.arm();
        r.command.targetId = 3;
        ++r.command.requestId;
        assert(stopped(r.step()));
        assert(r.nav.state == State::WAIT_VISION);
        r.o.targetId = 3;
        assert(stopped(r.step()));
        r.o.requestId = r.command.requestId;
        assert(!stopped(r.step()));
        r.command.targetId = 0;
        ++r.command.requestId;
        r.o.targetId = 0;
        assert(stopped(r.step())); // old request for same tag cannot steer
        r.o.requestId = r.command.requestId;
        assert(!stopped(r.step()));
    }
    {
        Rig r;
        r.arm();
        r.command.mode = Mode::IDLE;
        ++r.command.requestId;
        assert(stopped(r.step()));
        assert(r.nav.state == State::IDLE);
    }
}

void appArmingTests() {
    for (int failure = 0; failure < 4; ++failure) {
        Rig r;
        for (int i = 0; i < 200; ++i)
            assert(stopped(r.step())); // Booting with a working link does not arm.
        r.arm();
        if (failure == 0) {
            assert(stopped(r.nav.update(r.rx, r.now + cfg::LINK_TIMEOUT_MS + 1, r.command)));
        } else if (failure == 1) {
            ++r.o.session;
            assert(stopped(r.step()));
        } else if (failure == 2) {
            r.nav.fault("left encoder/motor stalled");
        } else {
            r.o.flags |= UPDATE_BUSY;
            assert(stopped(r.step()));
            r.o.flags &= ~UPDATE_BUSY;
        }
        assert(r.nav.state == State::FAULT);
        ++r.command.requestId;
        r.o.requestId = r.command.requestId;
        for (int i = 0; i < 120; ++i)
            assert(stopped(r.step())); // A new destination is not a new control session.
        r.arm();
        assert(!stopped(r.step()));
        r.command.enabled = false;
        assert(stopped(r.step()));
        r.command.enabled = true;
        assert(stopped(r.step())); // Reusing a stopped arm ID must not enable motion.
    }
    PhoneControl phone;
    assert(!phone.command.enabled);
    assert(phone.claim(99, 0));
    assert(phone.command.enabled && phone.command.mode == Mode::IDLE);
    const uint32_t firstArm = phone.command.armId;
    phone.manual();
    assert(phone.drive(100, 0, 10));
    phone.setMaintenance(true);
    assert(!phone.owned && !phone.command.enabled && phone.command.mode == Mode::IDLE);
    assert(!phone.claim(100, 20) && !phone.navigate(0) && !phone.drive(100, 0, 20));
    phone.stop();
    assert(phone.maintenance); // Stop cannot accidentally leave maintenance.
    phone.setMaintenance(false);
    assert(!phone.command.enabled);
    assert(phone.claim(100, 30) && phone.command.armId != firstArm);
    phone.tick(30 + cfg::PHONE_TIMEOUT_MS + 1);
    assert(!phone.owned && !phone.command.enabled);
}

void maintenanceTests() {
    Maintenance gate;
    assert(!gate.ready(0));
    for (uint32_t now = 0; now < 500; now += 20) {
        gate.observe(true, now);
        assert(!gate.ready(now));
    }
    gate.observe(true, 500);
    assert(gate.ready(500));
    assert(!gate.ready(801));
    gate.observe(true, 820);
    assert(!gate.ready(820)); // Lost polls restart the settling period.
    for (uint32_t now = 840; now <= 1340; now += 20)
        gate.observe(true, now);
    assert(gate.ready(1340));
    gate.observe(false, 1360);
    assert(!gate.ready(1360));
    gate.observe(true, 1380);
    assert(!gate.ready(1380));
    Maintenance rollover;
    for (uint32_t i = 0; i <= 600; i += 20)
        rollover.observe(true, UINT32_MAX - 300 + i);
    assert(rollover.ready(299));
}

int main() {
    appArmingTests();
    maintenanceTests();
    protocolTests();
    navigationTests();
    wheelTests();
    phoneTests();
    std::cout << "PASS: UART, four-tag selection, phone leases, manual recovery, navigation and "
                 "wheel control\n";
}
