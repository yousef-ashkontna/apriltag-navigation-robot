import importlib.util
from pathlib import Path
import subprocess
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("upload_wifi", ROOT / "tools/upload_wifi.py")
uploader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(uploader)


class UploadTest(unittest.TestCase):
    def test_camera_requires_ack_and_restart(self):
        events = []

        def wait(description, check, timeout=75):
            events.append(description)
            self.assertTrue(check())

        with patch.object(uploader, "enter_maintenance", side_effect=lambda: events.append("stop")), \
                patch.object(uploader, "camera_ready", return_value=True), \
                patch.object(uploader, "api", side_effect=[{"bootId": 1}, {"bootId": 2}]), \
                patch.object(uploader, "wait_for", side_effect=wait), \
                patch.object(uploader.subprocess, "run") as run:
            uploader.upload("esp32cam", "pio")
            run.assert_called_once_with(["pio", "run", "-e", "esp32cam_ota", "-t", "upload"],
                                        cwd=ROOT, check=True)
        self.assertEqual(events, ["stop", "camera maintenance acknowledgement", "esp32cam restart"])

    def test_failed_transfer_does_not_report_restart(self):
        with patch.object(uploader, "enter_maintenance"), \
                patch.object(uploader, "api", return_value={"bootId": 1}), \
                patch.object(uploader, "wait_for") as wait, \
                patch.object(uploader.subprocess, "run", side_effect=subprocess.CalledProcessError(1, "pio")):
            with self.assertRaises(subprocess.CalledProcessError):
                uploader.upload("nodemcu", "pio")
            wait.assert_not_called()

    def test_maintenance_uses_fresh_deadline(self):
        with patch.object(uploader, "api", side_effect=[{"now": 0xFFFFFF00}, {"ok": True}]) as api:
            uploader.enter_maintenance()
            self.assertEqual(api.call_args.args[0],
                             "http://192.168.4.1/api/maintenance?enabled=1&deadline=344")
            self.assertEqual(api.call_args.kwargs, {"post": True})


if __name__ == "__main__":
    unittest.main()
