#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "apriltag.h"
#include "apriltag_pose.h"
#include "tag36h11.h"

int main(int argc, char** argv) {
    assert(argc == 3);
    const int expectedId = atoi(argv[2]);
    apriltag_family_t* family = tag36h11_create();
    apriltag_detector_t* detector = apriltag_detector_create();
    apriltag_detector_add_family_bits(detector, family, 0);
    detector->quad_decimate = 1.5f;
    detector->nthreads = 1;
    image_u8_t* image = image_u8_create_from_pnm(argv[1]);
    assert(image && image->width == 320 && image->height == 240);
    // Exercises the actual vendored detector and verifies the printed tag bits.
    for (int iteration = 0; iteration < 20; ++iteration) {
        zarray_t* detections = apriltag_detector_detect(detector, image);
        assert(zarray_size(detections) == 1);
        apriltag_detection_t* d;
        zarray_get(detections, 0, &d);
        assert(d->id == expectedId && d->hamming == 0 && d->decision_margin > 35);
        apriltag_detection_info_t info = {d, 0.1f, 280, 280, 160, 120};
        apriltag_pose_t pose = {0};
        float err = estimate_tag_pose(&info, &pose);
        assert(isfinite(err) && err < 0.01f);
        assert(fabsf(MATD_EL(pose.t, 0, 0)) < 0.005f);
        assert(fabsf(MATD_EL(pose.t, 2, 0) - 0.35f) < 0.01f);
        matd_destroy(pose.R);
        matd_destroy(pose.t);
        // Changing principal point simulates the same tag to the camera's right.
        info.cx = 100;
        estimate_tag_pose(&info, &pose);
        assert(MATD_EL(pose.t, 0, 0) > 0.05f);
        matd_destroy(pose.R);
        matd_destroy(pose.t);
        apriltag_detections_destroy(detections);
    }
    for (int y = 0; y < image->height; ++y)
        for (int x = 0; x < image->width; ++x)
            image->buf[y * image->stride + x] = 255;
    zarray_t* blank = apriltag_detector_detect(detector, image);
    assert(zarray_size(blank) == 0);
    apriltag_detections_destroy(blank);
    image_u8_destroy(image);
    apriltag_detector_destroy(detector);
    tag36h11_destroy(family);
    printf("PASS: printed tag %d, pose range/sign, repeated frames, blank image\n", expectedId);
    return 0;
}
