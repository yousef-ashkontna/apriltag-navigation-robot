# Third-party AprilTag library

Source: https://github.com/raspiduino/apriltag-esp32

Pinned commit: `5413a2926e26922f44ca12d49d58d84b22b9002d`.

BSD 2-Clause license: see LICENSE.md and individual source headers.
The upstream source directory is vendored so firmware does not need a floating Git dependency.
The added library.json builds only tag36h11 plus the detector/common sources for ESP32.
The port contains tag36h11 IDs 0 through 34, not all 587 upstream IDs.

Local change: the functions declared/defined in `apriltag_pose.h` and
`apriltag_pose.c` use normal flash placement instead of `IRAM_ATTR`. With Wi-Fi
and the camera HTTP server enabled, the original placement overflows ESP32
instruction RAM. Pose estimation runs in the normal camera task, not an ISR;
the detector, drivers and their interrupt placement are unchanged.
