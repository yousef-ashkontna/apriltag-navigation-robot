# Uploading over Wi-Fi

Both boards now accept password-protected firmware uploads on the robot's own network. You still compile the code on your computer. The robot does not need a router or internet connection for the upload.

## Install it once by cable

Upload this version to **both boards** using the existing wired procedure. The NodeMCU uses USB; the ESP32-CAM uses its TX/RX programming connection with GPIO0 grounded during reset. Disconnect motor power and the runtime UART leads for these wired uploads, then restore the normal wiring and remove the GPIO0 jumper.

The first wired installation is necessary because the previous code has no OTA receiver. It also installs the ESP32-CAM's new partition table: `min_spiffs.csv`, with two firmware slots. Sending just an application image cannot replace the old `huge_app.csv` table.

Camera calibration stays in the NVS area, whose address and size are unchanged. Keep your calibration JSON backup anyway. There is no need to erase flash for this change.

## Upload the next changes

Build once while internet is available so PlatformIO can download anything it needs. Then power both boards, connect your computer to **AprilRobot** (password **AprilRobot2640**), and run this from the project folder:

```powershell
python tools/upload_wifi.py both
```

The script enters maintenance mode, which disables driving and cancels any current command. It waits for the camera to acknowledge that the motors are disabled, updates the camera, waits for its restart, and then updates the NodeMCU. The Wi-Fi network briefly disappears when the NodeMCU restarts; rejoin it if your computer switches networks.

For just one board:

```powershell
python tools/upload_wifi.py esp32cam
python tools/upload_wifi.py nodemcu
```

Leave the runtime UART connected for Wi-Fi uploads. It carries the maintenance acknowledgement. Keep power connected until the upload finishes. The app may show a lost connection or an unavailable picture during an update; the motors remain disabled. Afterward, tap **Finish maintenance** if it is still shown, then **Connect controls** when ready to drive. A restart never restores an old driving command.

## Using PlatformIO directly

Open [the app](http://192.168.4.1), tap **Updates & calibration**, and wait for **Both boards ready**. Then use either command:

```powershell
pio run -e esp32cam_ota -t upload
pio run -e nodemcu_ota -t upload
```

Update the camera first if updating both, because the NodeMCU supplies their Wi-Fi network. The wired environments remain `esp32cam` and `nodemcu`; the `_ota` environments select network uploads.

| Board | Address | OTA port |
| --- | --- | --- |
| NodeMCU | 192.168.4.1 | 8266 |
| ESP32-CAM | 192.168.4.250 | 3232 |

The upload password is **AprilRobotUpdate2640**, set once under `[robot] ota_password` in `platformio.ini`. That setting supplies both the firmware and uploader. You can choose your own before the initial wired installation; use letters and digits so shell quoting stays simple. If changing it later, the running firmware still expects the old password during that upload. The easiest way to change both boards' passwords together is a wired upload.

## If an upload fails

- **No response:** join AprilRobot, install the new firmware by wire first if needed, and check that maintenance is enabled. For a camera upload, check the UART connection too.
- **Authentication failed:** the upload password must match the firmware currently on that board.
- **Connect/receive timeout:** allow PlatformIO/Python through the computer's firewall on this local network. The uploader sends a UDP invitation and receives a TCP connection back from the board. Avoid a VPN or another adapter routing `192.168.4.x` elsewhere.
- **Failed halfway:** keep driving disabled, restore a stable power/network connection and retry. If the board no longer starts its network service, use the wired procedure to recover it.
- **Camera picture unavailable:** the OTA service starts even if camera initialization fails, so it can still accept an update when its Wi-Fi and UART are working.

The physical RUN switch is no longer used. With power off, remove its GPIO15 wire, switch wire to 3.3 V, and external GPIO15 pull-down from the ESP32-CAM. Leave GPIO15 unconnected. Keep the L298N ENA/ENB pull-downs; those hold the motors off during boot.

This uses [ArduinoOTA on ESP8266](https://arduino-esp8266.readthedocs.io/en/latest/ota_updates/readme.html) and the ESP32 core's ArduinoOTA support. The camera layout follows [Espressif's OTA partition requirements](https://docs.espressif.com/projects/arduino-esp32/en/latest/tutorials/partition_table.html).
