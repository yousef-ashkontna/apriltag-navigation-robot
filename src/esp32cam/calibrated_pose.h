#pragma once
#include "camera_calibration.h"
extern "C" {
#include "apriltag.h"
#include "common/homography.h"
}

// Pose estimation needs both the corrected corners and a matching homography.
inline bool correctTagCorners(apriltag_detection_t* tag,
                              const robot::CameraCalibration& calibration) {
    const float corners[4][2] = {{-1, 1}, {1, 1}, {1, -1}, {-1, -1}};
    float corrected[4][2];
    for (int i = 0; i < 4; ++i) {
        if (!calibration.undistort(tag->p[i][0], tag->p[i][1], corrected[i][0], corrected[i][1]))
            return false;
    }
    zarray_t* points = zarray_create(sizeof(float[4]));
    if (!points)
        return false;
    for (int i = 0; i < 4; ++i) {
        float pair[4] = {corners[i][0], corners[i][1], corrected[i][0], corrected[i][1]};
        zarray_add(points, pair);
    }
    matd_t* homography = homography_compute(points, HOMOGRAPHY_COMPUTE_FLAG_SVD);
    zarray_destroy(points);
    if (!homography)
        return false;
    for (int i = 0; i < 9; ++i) {
        if (!isfinite(homography->data[i])) {
            matd_destroy(homography);
            return false;
        }
    }
    matd_destroy(tag->H);
    tag->H = homography;
    for (int i = 0; i < 4; ++i) {
        tag->p[i][0] = corrected[i][0];
        tag->p[i][1] = corrected[i][1];
    }
    homography_project(tag->H, 0, 0, &tag->c[0], &tag->c[1]);
    return true;
}
