# AprilTag robot

A two-wheel robot with a front caster that drives toward a tag selected from a phone. It uses an ESP32-CAM to find the tag and an ESP8266 NodeMCU to control the motors. The wheel encoders provide speed feedback, and two IR sensors at the front detect obstacles.

The boards talk over UART at 115200 baud. The NodeMCU creates a Wi-Fi network for the phone controls, so the robot can run without a laptop or router.

The ESP32-CAM joins the same network to send pictures to the phone. The app shows its view in both driving modes, with a pause button and a warning if pictures stop arriving. No extra wiring is needed.

## Hardware

- ESP8266 NodeMCU with CP2102
- AI-Thinker ESP32-CAM with OV2640 and PSRAM
- L298N motor driver
- Two DC motors with 65 mm wheels and 20 PPR encoders
- Two front IR sensors
- A suitable power supply

The wheel spacing is set to 158 mm as an estimate. Measure between the tire centers and update `WHEEL_TRACK_M` before testing. Calibrate the camera and tune the motor-control gains on the assembled robot.

The connections and power details are in [the wiring guide](docs/WIRING_AND_SETUP.md). Follow that pin table: D7 and D8 carry the NodeMCU UART, while its TX header is used for a motor-direction signal. Remove the L298N ENA/ENB jumpers and fit the pull-down resistors shown in the guide.

## Uploading the code

Open the project in PlatformIO and build both boards:

```powershell
pio run
```

Then upload each one, replacing the COM ports with the ports on your computer:

```powershell
pio run -e nodemcu -t upload --upload-port COM5
pio run -e esp32cam -t upload --upload-port COM6
```

For the initial wired upload, disconnect motor power and the runtime UART wires. The ESP32-CAM needs a USB-to-UART adapter, or the NodeMCU's CP2102, with GPIO0 grounded during reset. Remove that jumper and reset the camera after uploading. The wiring guide covers both programming methods.

The phone page is included in the NodeMCU firmware; it doesn't need a separate upload.

Install this version on both boards once using the cables. Later, connect your computer to AprilRobot and run:

```powershell
python tools/upload_wifi.py both
```

This stops driving, updates the camera first, then updates the NodeMCU. You can use `esp32cam` or `nodemcu` instead of `both`. The [Wi-Fi upload guide](docs/WIFI_UPDATES.md) explains the password, manual commands and recovery steps.

## Driving the robot

1. Connect your phone to **AprilRobot** using the password **AprilRobot2640**.
2. Open [192.168.4.1](http://192.168.4.1) and tap **Connect controls**. Stay on the Wi-Fi network if the phone says it has no internet.
3. Connect controls enables driving after a two-second delay. No RUN switch is needed.
4. Choose one of the four destinations, or open **Manual drive** and hold an arrow button.

The destinations use tag36h11 IDs 0, 1, 2 and 3. [Print the tags here](docs/PRINT_TAGS.html) at 100% scale. Each outer black square should measure 100 mm; keep the white border around it.

In automatic mode, the robot turns toward the selected tag and approaches it, aiming to stop about 35 cm away. If it cannot see the tag, it scans for up to 30 seconds and then stops. Use manual drive to reposition it and select the destination again. It has no map of the room, so it may need help getting around larger obstacles.

Release a manual-drive button to stop. **STOP ROBOT** cancels the current control session. Keep the page open and the phone awake while driving; losing the connection stops the robot. After a UART failure, camera restart or encoder stall, fix the problem and tap Connect controls again.

There are no rear sensors. Check behind the robot before reversing. Start testing with the wheels raised, and allow for the wheels coasting after motor power is cut.

## Camera calibration

Use [calibrate_camera.py](tools/calibrate_camera.py) to take pictures of a printed chessboard and work out the lens settings. It saves a JSON backup and writes the values to the ESP32-CAM's flash. After a restart, the camera uses them for tag distance and steering calculations.

This step needs a computer once; driving still works without one. The [camera guide](docs/CAMERA_CALIBRATION.md) has the printable board, commands and steps. Use **Updates & calibration** in the app while collecting pictures and saving settings. This keeps the motors disabled.

## Working on the project

The two firmware entry points are [NodeMCU main.cpp](src/nodemcu/main.cpp) and [ESP32-CAM main.cpp](src/esp32cam/main.cpp). Wheel dimensions, encoder counts, speed limits and camera settings are in [robot_config.h](include/robot_config.h). The phone interface is in [web/index.html](web/index.html).

The camera uses the [AprilTag ESP32 library](https://github.com/raspiduino/apriltag-esp32), included under `lib/AprilTagESP32` with its license.

To run the existing checks:

```powershell
python tools/run_tests.py
python -m unittest discover -s test -p "test_*.py"
node tools/test_mobile.cjs
```

The firmware checks need Python and GCC/G++. The calibration tests also need the packages in `tools/calibration_requirements.txt`. The phone tests use Playwright and a browser with a simulated robot and camera.

Both firmware builds and the desktop tests pass. The code has not been tested on the assembled robot yet, so motor direction, sensor readings, camera calibration and stopping distance still need to be checked before a floor run.
