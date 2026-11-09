# Calibrating the camera

Do this once after mounting and focusing the camera. A computer runs the calibration calculation; after that, the ESP32-CAM keeps the values in flash and the robot runs on its own. Moving the focus ring, changing the image size or flipping the image means calibrating again.

## Take the pictures

1. Upload the updated firmware to both boards. Open the phone app and tap **Updates & calibration**. Wait for **Both boards ready**. Disconnect motor power while handling the calibration board.
2. Print [CALIBRATION_BOARD.svg](CALIBRATION_BOARD.svg) in landscape at **100% / actual size**. Each square must measure **20 mm**. There are 10 by 7 squares, giving 9 by 6 inner corners. Keep the white border and stick the sheet to something flat.
3. With internet available, install the Python packages from the project folder:

   ```powershell
   python -m pip install -r tools/calibration_requirements.txt
   ```

4. Power both boards and connect the computer to **AprilRobot**, password **AprilRobot2640**. The camera joins that network at `192.168.4.250`. Pause the phone's camera view while collecting pictures.
5. Run:

   ```powershell
   python tools/calibrate_camera.py capture
   ```

6. A camera window opens. Hold the printed board still, with all its inner corners visible. Press **Space** to save a picture. Take 20 views: near and far, toward each edge, tilted left/right and up/down. The script rejects views that are too similar. Press **Q** to finish early; at least 15 usable views are needed.

The preview is enlarged for convenience, but the saved images are the original 320 × 240 grayscale frames. Green corner marks are only drawn on the preview.

After collecting the pictures, the script fits the lens, checks the result, writes `calibration/camera.json`, and uploads it to the ESP32-CAM. It also keeps the pictures and a `calibration/report.json` with the error for each view. If the fit is poor, it explains what needs improving instead of uploading the result.

Stay in maintenance mode and **restart the ESP32-CAM** after saving. The phone page should then say **Saved lens calibration in use**. Tap **Finish maintenance** before reconnecting controls. Check the reported tag distance against a ruler at several distances before trying an automatic approach.

## Keeping or restoring the values

Back up `calibration/camera.json`. It contains `fx`, `fy`, `cx`, `cy`, the lens-distortion coefficients, image orientation and fit error. The firmware loads the saved values at startup, corrects the detected tag corners for distortion, and uses them to estimate distance and bearing. No firmware rebuild is needed after calibration.

To solve the same pictures again without connecting to the robot:

```powershell
python tools/calibrate_camera.py solve
```

To restore a saved calibration, with **Updates & calibration** enabled in the app:

```powershell
python tools/calibrate_camera.py upload calibration/camera.json
python tools/calibrate_camera.py status
```

Restart the camera after restoring, then finish maintenance in the app. Normal firmware uploads retain its settings; erasing flash removes them. If no compatible saved calibration exists, the camera uses the starting estimates in `robot_config.h`, and the phone page says it has not been calibrated.

For a new set of pictures, use a new folder:

```powershell
python tools/calibrate_camera.py --directory calibration/second-try capture
```

Use `capture --no-upload` to keep the result on the computer for review. Avoid mixing pictures from different focus settings or camera orientations. The printed square size is fixed at 20 mm in the script. The fit estimates k1, k2, p1 and p2; k3 stays zero because it is hard to estimate reliably from these small images. A low fit error is useful, but the ruler check still matters.

## Camera view on the phone

Open the normal controls at [192.168.4.1](http://192.168.4.1). The camera view appears in both destination and manual modes. It requests a new grayscale JPEG up to twice a second; the actual rate depends on AprilTag processing and Wi-Fi reception. It is a movement preview, not smooth video. The original, distorted view is shown; only tag coordinates are corrected for navigation.

Pictures travel directly from the ESP32-CAM to the phone over Wi-Fi. Commands and sensor feedback between boards still use UART. **No wiring changes are needed.** Pause the view if you do not need it. A missing or old picture is cleared and labelled, so a frozen image is not mistaken for a live view. Picture failure alone does not disable manual controls; their existing connection timeout and the wired sensor checks still apply.

If controls work but the picture does not, check that both boards use the same Wi-Fi name/password, the camera has working PSRAM and sufficient power, and the phone stayed on the robot's network. Open [the camera settings](http://192.168.4.250/calibration) to check access. The app must be opened using `http://192.168.4.1` for its cross-origin camera requests. Keep the SD card slot empty, as before.

The camera HTTP task serves copied frames, so a slow viewer does not hold the camera driver buffer. The IR/UART task runs independently at a higher task priority. Camera timing, power stability and UART watchdog behaviour with the preview open still need checking on the assembled robot.

The calibration follows [OpenCV's camera-calibration method](https://docs.opencv.org/4.x/dc/dbb/tutorial_py_calibration.html). JPEG conversion uses the [Espressif camera driver](https://github.com/espressif/esp32-camera).
