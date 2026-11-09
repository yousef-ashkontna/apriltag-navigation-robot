#include <assert.h>
#include <stdio.h>
#include "camera_calibration.h"
#include "../src/esp32cam/calibrated_pose.h"
extern "C" {
#include "apriltag_pose.h"
}

int main() {
    robot::CameraCalibration lens;
    assert(lens.valid());
    lens.fx = 292;
    lens.fy = 286;
    lens.cx = 157;
    lens.cy = 123;
    lens.k1 = -0.22f;
    lens.k2 = 0.07f;
    lens.p1 = 0.001f;
    lens.p2 = -0.002f;
    for (int v = 10; v < 240; v += 20) {
        for (int u = 10; u < 320; u += 20) {
            float x, y, correctedU, correctedV;
            lens.distort((u - lens.cx) / lens.fx, (v - lens.cy) / lens.fy, x, y);
            assert(lens.undistort(
                x * lens.fx + lens.cx, y * lens.fy + lens.cy, correctedU, correctedV));
            assert(hypotf(correctedU - u, correctedV - v) < 0.01f);
        }
    }
    // A known tag pose projected through a distorted lens should recover its translation.
    const float corners[4][2] = {{-1, 1}, {1, 1}, {1, -1}, {-1, -1}};
    apriltag_detection_t tag = {};
    for (int i = 0; i < 4; ++i) {
        float x, y;
        lens.distort(
            (0.18f + corners[i][0] * 0.05f) / 0.8f, (0.03f + corners[i][1] * 0.05f) / 0.8f, x, y);
        tag.p[i][0] = x * lens.fx + lens.cx;
        tag.p[i][1] = y * lens.fy + lens.cy;
    }
    assert(correctTagCorners(&tag, lens));
    apriltag_detection_info_t info = {&tag, 0.1f, lens.fx, lens.fy, lens.cx, lens.cy};
    apriltag_pose_t pose = {};
    const float error = estimate_tag_pose(&info, &pose);
    assert(isfinite(error) && error < 0.001f && pose.t && pose.R);
    assert(fabsf(MATD_EL(pose.t, 0, 0) - 0.18f) < 0.005f);
    assert(fabsf(MATD_EL(pose.t, 1, 0) - 0.03f) < 0.005f);
    assert(fabsf(MATD_EL(pose.t, 2, 0) - 0.8f) < 0.005f);
    matd_destroy(tag.H);
    matd_destroy(pose.R);
    matd_destroy(pose.t);

    auto bad = lens;
    bad.fx = NAN;
    assert(!bad.valid());
    bad = lens;
    bad.width = 640;
    assert(!bad.valid());
    bad = lens;
    bad.hmirror = !cfg::CAMERA_HMIRROR;
    assert(!bad.valid());
    bad = lens;
    bad.rms = 3;
    assert(!bad.valid());
    float x, y;
    assert(!lens.undistort(NAN, 120, x, y));
    printf("PASS: lens validation, distortion correction and calibrated AprilTag pose\n");
}
