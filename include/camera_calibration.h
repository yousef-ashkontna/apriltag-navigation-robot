#pragma once
#include <math.h>
#include <stdint.h>
#include "robot_config.h"

namespace robot {
struct CameraCalibration {
    uint32_t version = 1;
    uint32_t width = cfg::IMAGE_WIDTH, height = cfg::IMAGE_HEIGHT;
    uint32_t hmirror = cfg::CAMERA_HMIRROR, vflip = cfg::CAMERA_VFLIP;
    float fx = cfg::CAMERA_FX, fy = cfg::CAMERA_FY;
    float cx = cfg::CAMERA_CX, cy = cfg::CAMERA_CY;
    float k1 = 0, k2 = 0, p1 = 0, p2 = 0, k3 = 0;
    float rms = 0;

    bool valid() const {
        return version == 1 && width == cfg::IMAGE_WIDTH && height == cfg::IMAGE_HEIGHT &&
               hmirror == cfg::CAMERA_HMIRROR && vflip == cfg::CAMERA_VFLIP && isfinite(fx) &&
               fx >= 50 && fx <= 2000 && isfinite(fy) && fy >= 50 && fy <= 2000 && isfinite(cx) &&
               cx >= 0 && cx < width && isfinite(cy) && cy >= 0 && cy < height && isfinite(k1) &&
               fabsf(k1) <= 5 && isfinite(k2) && fabsf(k2) <= 5 && isfinite(k3) && fabsf(k3) <= 5 &&
               isfinite(p1) && fabsf(p1) <= 1 && isfinite(p2) && fabsf(p2) <= 1 && isfinite(rms) &&
               rms >= 0 && rms <= 1.5f;
    }

    // OpenCV's five-coefficient lens model, in normalized camera coordinates.
    void distort(float x, float y, float& dx, float& dy) const {
        const float r2 = x * x + y * y;
        const float radial = 1 + r2 * (k1 + r2 * (k2 + r2 * k3));
        dx = x * radial + 2 * p1 * x * y + p2 * (r2 + 2 * x * x);
        dy = y * radial + p1 * (r2 + 2 * y * y) + 2 * p2 * x * y;
    }

    bool undistort(float u, float v, float& outU, float& outV) const {
        const float xd = (u - cx) / fx, yd = (v - cy) / fy;
        float x = xd, y = yd;
        for (int i = 0; i < 25; ++i) {
            const float r2 = x * x + y * y;
            const float radial = 1 + r2 * (k1 + r2 * (k2 + r2 * k3));
            if (!isfinite(radial) || radial < 0.1f)
                return false;
            const float tx = 2 * p1 * x * y + p2 * (r2 + 2 * x * x);
            const float ty = p1 * (r2 + 2 * y * y) + 2 * p2 * x * y;
            x = (xd - tx) / radial;
            y = (yd - ty) / radial;
        }
        float checkX, checkY;
        distort(x, y, checkX, checkY);
        if (!isfinite(x) || !isfinite(y) || !isfinite(checkX) || !isfinite(checkY) ||
            hypotf((checkX - xd) * fx, (checkY - yd) * fy) > 0.05f)
            return false;
        outU = x * fx + cx;
        outV = y * fy + cy;
        return true;
    }
};
} // namespace robot
