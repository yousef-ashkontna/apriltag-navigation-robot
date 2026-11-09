"""Check the OpenCV solve and the upload workflow without a physical camera."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("calibrate_camera", ROOT / "tools/calibrate_camera.py")
calibration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(calibration)


class CalibrationTest(unittest.TestCase):
    def setUp(self):
        self.metadata = dict(width=320, height=240, hmirror=0, vflip=0)
        self.matrix = np.array([[290., 0, 157], [0, 285, 122], [0, 0, 1]])
        self.distortion = np.array([-.18, .045, .001, -.002, 0.])
        self.points = []
        rng = np.random.default_rng(42)
        for _ in range(35):
            rotation = rng.uniform([-.55, -.55, -.2], [.55, .55, .2])
            translation = rng.uniform([-135, -100, 240], [-25, -5, 340])
            points, _ = cv2.projectPoints(calibration.object_points(), rotation, translation,
                                          self.matrix, self.distortion)
            if (points[:, :, 0].min() > 2 and points[:, :, 0].max() < 318 and
                    points[:, :, 1].min() > 2 and points[:, :, 1].max() < 238):
                self.points.append(points.astype(np.float32))

    def test_known_lens(self):
        values, errors, coverage = calibration.fit_camera(self.points, self.metadata)
        self.assertAlmostEqual(values["fx"], 290, delta=.1)
        self.assertAlmostEqual(values["fy"], 285, delta=.1)
        self.assertAlmostEqual(values["k1"], -.18, delta=.001)
        self.assertAlmostEqual(values["p2"], -.002, delta=.0001)
        self.assertLess(max(errors), .01)
        self.assertTrue(min(coverage) > .65)

    def test_reject_bad_samples_and_values(self):
        with self.assertRaises(ValueError):
            calibration.fit_camera(self.points[:5], self.metadata)
        with self.assertRaises(ValueError):
            calibration.fit_camera([self.points[0]] * 20, self.metadata)
        values, _, _ = calibration.fit_camera(self.points, self.metadata)
        for key, value in (("fx", float("nan")), ("width", 640), ("rms", 4), ("p1", 2)):
            with self.assertRaises(ValueError):
                calibration.validate({**values, key: value})

    def test_saved_file_and_upload(self):
        values, _, _ = calibration.fit_camera(self.points, self.metadata)
        temporary_root = ROOT / ".pio" / "calibration-tests"
        temporary_root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=temporary_root) as directory:
            path = Path(directory) / "camera.json"
            calibration.save_json(path, values)
            self.assertEqual(json.loads(path.read_text()), values)
        state = {"active": values, "saved": values, "maintenance": True, "busy": False}
        with patch.object(calibration, "camera_settings", return_value=state), \
                patch.object(calibration, "request") as request:
            calibration.upload("http://camera", values)
            request.assert_called_once_with("http://camera", "/calibration", values)
        with patch.object(calibration, "camera_settings", return_value={**state, "maintenance": False}), \
                patch.object(calibration, "request") as request:
            with self.assertRaises(ValueError):
                calibration.upload("http://camera", values)
            request.assert_not_called()
        with patch.object(calibration, "camera_settings", return_value={**state, "saved": None}):
            with patch.object(calibration, "request"), self.assertRaises(RuntimeError):
                calibration.upload("http://camera", values)

    def test_printed_board_detects(self):
        # Match CALIBRATION_BOARD.svg: 10 x 7 squares with a white border.
        image = np.full((240, 320), 255, np.uint8)
        for row in range(7):
            for col in range(10):
                if (row + col) % 2 == 0:
                    image[50 + row * 20:70 + row * 20, 60 + col * 20:80 + col * 20] = 0
        self.assertIsNotNone(calibration.find_corners(image))


if __name__ == "__main__":
    unittest.main()
