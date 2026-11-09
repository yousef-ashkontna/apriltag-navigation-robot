# Wiring and first run: UART + phone app

Driving is enabled from **Connect controls** in the phone app. There is no RUN switch. After a fault, fix the cause and connect controls again. Upload this version to **both boards** because the UART protocol has changed.

## 1. Changes from the previous version

The controllers use hardware UART at **115200 baud, 8N1**. The NodeMCU hosts the phone controls. The ESP32-CAM joins its Wi-Fi at **192.168.4.250** to send camera pictures directly to the phone; commands and sensor feedback still use UART. Move L298N **IN3 from D7 to the NodeMCU TX/GPIO1 header**, and **IN4 from D8 to D4/GPIO2**. D7/D8 carry UART. Keep encoders on D1/D2, front IR sensors on camera GPIO13/14. Leave camera GPIO15 unconnected.

This avoids runtime contention with the NodeMCU CP2102 transmitter: the firmware calls `Serial.swap()` to move its UART away from its normal USB pins. The camera UART is shared with programming, so **disconnect the runtime UART wires before a wired upload to either board**.

The code assumes an AI-Thinker ESP32-CAM with working PSRAM, OV2640, and an L298N. OV2640 identifies the camera sensor, not the carrier pinout; confirm the carrier layout if your board is a different model.

## 2. Complete NodeMCU table

Left/right are viewed looking forward toward the caster.

| Signal | NodeMCU label | GPIO |
| --- | --- | --- |
| Left wheel encoder OUT | D1 | 5 |
| Right wheel encoder OUT | D2 | 4 |
| L298N IN1: left direction | D0 | 16 |
| L298N IN2: left direction | D3 | 0 |
| L298N ENA: left speed | D5 | 14 |
| L298N IN3: right direction | **TX** | **1** |
| L298N IN4: right direction | **D4** | **2** |
| L298N ENB: right speed | D6 | 12 |
| Receive from ESP32-CAM U0T/TX | **D7** | **13** |
| Transmit to ESP32-CAM U0R/RX | **D8** | **15** |
| RX header | Leave unconnected during operation | 3 |
| SD2 / SD3 and other flash pins | Leave unconnected | 9 / 10 / flash bus |

Connect left motor to OUT1/OUT2 and right motor to OUT3/OUT4. Remove the **ENA and ENB jumper caps** and put a **10 kΩ resistor from each enable input to GND**. This holds the motors off before firmware starts, even though boot messages or boot straps can change direction-pin levels. Do not confuse these with the separate L298N `5V-EN` regulator jumper.

D3/GPIO0 and D4/GPIO2 must be high at reset; D8/GPIO15 must be low. The NodeMCU's onboard resistors normally provide these levels. Do not override them with external pull resistors or circuitry. D4's onboard LED may change with right-motor direction; it is not a dedicated status light.

## 3. Runtime UART and camera wiring

| NodeMCU | ESP32-CAM |
| --- | --- |
| D8 / GPIO15 / swapped UART TX | U0R / GPIO3 / RX |
| D7 / GPIO13 / swapped UART RX | U0T / GPIO1 / TX |
| GND | GND |

These are 3.3 V TTL signals. No RS-232 converter and no I2C pull-ups are needed. Keep wires short, preferably below about 20 cm, with a ground wire alongside them and away from motor leads. Do not connect UART signals while one board is unpowered; power both from the intended regulated arrangement to avoid back-powering through GPIOs. The crossed connections above are for normal operation, not the NodeMCU-as-programmer method below.

| Camera peripheral | ESP32-CAM pin |
| --- | --- |
| Front left IR digital OUT | GPIO13 |
| Front right IR digital OUT | GPIO14 |
| GPIO15 | Leave unconnected |
| Regulated supply | 5 V and GND |

If you fitted the old RUN switch, disconnect power and remove its wire from **ESP32-CAM GPIO15**, its switch connection to **3.3 V**, and its external **10 kΩ GPIO15-to-GND resistor**. Do not jumper GPIO15 to 3.3 V. Leave the rest of the wiring unchanged, including the two L298N enable pull-downs.

With the wheels raised, check that the app's wired link is connected. Tap **Connect controls**, wait two seconds, then select a destination or hold a manual arrow. Stop, closing the page, losing the phone connection or a fault disables the session. Fix any fault and tap Connect controls again; restoring a cable alone does not resume motion.

Do not use a microSD card. Leave camera GPIO12 and GPIO2 unconnected, do not use PSRAM-associated GPIO16, and keep GPIO4 free (flash LED stays off). Remove camera GPIO0-to-GND after programming because GPIO0 becomes the camera clock.

## 4. Power and logic levels

- Both MCUs require **3.3 V GPIO levels**. IR and encoder outputs must not drive 5 V into them. Power sensor modules at 3.3 V if they support it, or use appropriate level shifting for 5 V modules. Check onboard pull-ups on open-collector outputs; a module pulling OUT up to 5 V is not made safe by adding another pull-up to 3.3 V.
- Default front IR polarity is active LOW. With app controls disconnected, the app should show both clear; blocking one should show the corresponding side blocked. Adjust `IR_ACTIVE_LOW` if the real modules use the opposite polarity. Disconnected active-low outputs may appear clear because of pull-ups, so test each sensor physically.
- Use regulated **5 V with at least 1 A capacity for the camera**, plus enough additional capacity for the NodeMCU and other logic. Power the NodeMCU through its supported regulated 5 V input/USB arrangement, never 5 V into a 3.3 V pin.
- Power motors from a supply suited to their ratings and L298N voltage drop, not either MCU regulator. Battery/motor ratings were not provided, so motor supply voltage is not assumed.
- Verify the L298N module's `5V-EN` regulator jumper arrangement. On common modules, remove it when feeding regulated external 5 V to the logic terminal. Never tie two regulator outputs together or rely on an unknown L298N regulator to supply the camera.
- Join grounds for both boards, driver, sensors, encoders and supplies. Route motor-current returns separately from camera logic returns. Good motor suppression and local bulk capacitance can help prevent resets.
- During flashing, disconnect motor power and avoid tying an external 5 V rail directly to USB 5 V. Use one source per rail and common ground.

## 5. Programming

Install this version once by wire, then use the [Wi-Fi upload guide](WIFI_UPDATES.md). UART wires stay connected during Wi-Fi uploads.

Both wired environments are in the same PlatformIO project. Build with `pio run`; upload each separately. Rebuild/upload **both boards** after shared protocol changes.

### NodeMCU via USB

Disconnect both runtime UART wires and the L298N IN3 wire from NodeMCU TX/GPIO1. Disconnect motor power. Plug in USB:

```powershell
pio device list
pio run -e nodemcu -t upload --upload-port COM5
```

Replace COM5. Restore wiring afterwards. Normal runtime diagnostics are now in the phone app and `/api/state`, **not the USB serial monitor**; UART0 is swapped and the TX header is a motor output.

### ESP32-CAM via a USB-to-UART adapter

Disconnect the NodeMCU-to-camera UART wires first. The adapter must use 3.3 V TX/RX logic.

| Adapter/supply | Camera |
| --- | --- |
| Adapter TX | U0R / RX |
| Adapter RX | U0T / TX |
| Adapter GND | GND |
| External regulated 5 V | 5 V |
| External supply GND | GND |
| Temporary boot jumper | GPIO0 to GND |

Leave adapter VCC disconnected when externally powering the camera. Hold GPIO0 low and reset/power-cycle the camera, then:

```powershell
pio run -e esp32cam -t upload --upload-port COM6
```

If stuck at `Connecting...`, reset again with GPIO0 grounded. After success, **remove GPIO0-to-GND and reset again**. Remove the programmer's UART leads before restoring the runtime UART wires. Do not leave two transmitters driving camera RX.

Runtime UART traffic is binary. Ordinary serial monitors will show unreadable bytes, not application logs. Use the app's status/diagnostic endpoint once both boards are connected.

### ESP32-CAM using the NodeMCU's CP2102

1. Disconnect motor power, both runtime UART wires, and the NodeMCU's robot signal wiring, especially IN3 on its TX header.
2. Hold **NodeMCU EN to GND** so its ESP8266 cannot drive the UART. Plug the NodeMCU into USB.
3. Connect **NodeMCU RX header to camera U0R/RX**, **NodeMCU TX header to camera U0T/TX**, and GND to GND. Here RX-to-RX and TX-to-TX are intentional: NodeMCU header names are from the ESP8266 perspective, and the CP2102 TX connects to that RX header.
4. Supply the camera with regulated external 5 V and common GND. Do not connect the two 5 V rails together for this method.
5. Use the GPIO0/reset/upload sequence above, selecting `-e esp32cam` with the NodeMCU's COM port.
6. Remove programming UART wires, NodeMCU EN-to-GND and camera GPIO0-to-GND. Restore the runtime wiring table and reset both.

The NodeMCU firmware is retained while its CPU is disabled. Its Wi-Fi app is unavailable during this programming method. If a clone routes UART differently, use a separate adapter.

## 6. Four destinations and manual recovery

Print `PRINT_TAGS.html` at **100% / actual size** with browser print headers/footers disabled. It contains four A4 pages. Alternatively print the individual `tag36h11_id0_100mm.svg` through `id3` files. Measure the outer black square: it must be 100 mm with the current `TAG_SIZE_M`. Preserve the white margin and use one instance of each ID in the robot's area. All four tags currently use the same physical size.

The app labels destinations 01–04 and explicitly shows actual IDs 0–3. Place tags upright, flat and around camera height. Mount the camera level, facing the caster, near the robot centerline. No map or pre-entered tag coordinates are required.

Connect your phone to **AprilRobot**, password **AprilRobot2640**, then open **http://192.168.4.1**. Keep the no-internet Wi-Fi connection; disable automatic switching away from it if necessary. Tap Connect controls; after the two-second delay choose a destination or manual drive. No physical arming switch is needed.

Manual mode works without a detected tag and even without initialized camera vision if the IR/UART task is alive. Hold forward/back/left/right; release to stop. Front IRs block forward motion and turns toward an obstacle; reverse is available, so watch the rear. Selecting Destinations stops manual motion until a destination is tapped. Selecting Manual stops autonomous motion. Search timeout, arrival and visual failure can be recovered from through the app. Electrical link loss, camera reboot and motor/encoder stall disable the control session. Fix the cause, then reconnect controls in the app.

Keep the page visible and phone awake. Closing/backgrounding the page, losing focus, losing Wi-Fi or pressing STOP revokes the session. Reconnecting does not resume a previous drive command automatically. One browser owns controls at a time; another can always stop it.

## 7. Geometry and calibration

| Setting | Current value | Basis |
| --- | --- | --- |
| Left/right wheel diameter | 0.065 m | 65 mm wheel diameter in supplied chassis image |
| Left/right encoder PPR | **20** | Confirmed by user; counts RISING edges only |
| Wheel track | **0.158 m, provisional** | Diagram's 158 mm dimension; verify tire center-to-center distance |
| Tag outer black edge | 0.100 m | Generated print size; measure after printing |
| Camera fx/fy | 280 pixels, provisional | Requires calibration at 320x240 |
| Camera cx/cy | 160/120 pixels, provisional | Requires calibration at the same resolution/orientation |

The diagram also shows chassis length 200 mm and widths 150/100 mm. Those body dimensions do not determine differential-drive wheel track. Its 233.2 g figure is a kit figure and is not treated as assembled robot mass. At 65 mm and 20 PPR, one rising edge represents about **10.21 mm** of wheel travel before slip.

With app controls disconnected, turn each wheel by hand and inspect `http://192.168.4.1/api/state`: `ticks` should increase on the correct side by 20 for one full wheel turn. Turning ten revolutions should give about 200 counts. Do not double PPR to 40: firmware counts one edge per pulse. Direction is inferred from commands because these are single-channel encoders.

Follow the [camera calibration guide](CAMERA_CALIBRATION.md) to collect chessboard pictures and save the lens settings to ESP32-CAM flash. Saved values replace the provisional fx/fy/cx/cy above after a camera restart. The pose calculation corrects tag corners for radial and tangential distortion and rebuilds their homography. Verify the result at several measured lens-to-tag distances. `xMm` should be positive to the robot's right and negative to its left. Fix camera mounting/orientation if steering signs are wrong, then recalibrate.

The live API also exposes camera readiness, frame age, IR flags, encoder counts, PWM values and decision margin. Do not attach a text serial logger to the runtime UART. PWM/feedforward/PI values in `robot_config.h` remain starting estimates and need tuning on the actual motors, battery and floor.

## 8. First tests, wheels raised

1. Verify power levels, common GND, motor EN jumpers removed and all three 10 kΩ pull-downs fitted. Motors must remain off during either board's reset.
2. Confirm app Wired link is Connected, Connect controls and Stop change Drive controls between Enabled and Disabled, IR sides/polarity match the physical sensors, and both encoders count correctly.
3. Present each printed tag separately with motor power disconnected. The matching app tile should show In view. Select each destination and verify its pose is used; other visible IDs should not become the target.
4. Arm and choose Manual with wheels raised. Hold forward, verify both wheels move forward, then release. Correct a reversed motor by swapping its two output wires while powered down or changing its `*_MOTOR_INVERT` setting, not both.
5. Test left/right turns, reverse, Stop, phone screen lock and Wi-Fi loss. Motor movement must stop; reconnecting alone must not resume it.
6. Disconnect one runtime UART wire while moving with wheels raised. The link watchdog must stop both motors. Restore the wire and reconnect controls before issuing a new movement command.
7. Cover both IR sensors: forward should stop. In manual mode, reverse should remain available; only test it when the path behind is clear. With no tags visible, manual movement must still work.
8. Disconnect one encoder signal during a low-power raised-wheel test. After about 1.8 seconds of cumulative powered time without pulses, both motors should stop with a stall fault. Do not restrain an energized wheel by hand.
9. Place a target 0.6–1 m ahead in a clear area for the first floor run. Start slowly, keep motor power accessible, and measure actual stopping distance. Then test obstacle reactions and destination changes.

The 20 mm arrival tolerance applies to estimated camera range, not guaranteed physical accuracy. Low-resolution speed measurements are averaged over 200 ms. Wheel slip, calibration, camera latency, caster resistance and coasting affect performance. Front sensors cannot detect side/rear obstacles or drop-offs, and a pivot sweeps the whole chassis.

## 9. Troubleshooting

| Symptom | Check |
| --- | --- |
| Wired link lost | D8 → camera RX, D7 ← camera TX, shared GND, both boards powered and flashed with this version |
| Camera upload fails | Runtime UART disconnected, GPIO0 low during reset, GPIO2/12 free, adequate 5 V supply |
| NodeMCU upload fails | IN3 disconnected from TX header; normal USB upload before runtime `Serial.swap()` |
| NodeMCU does not boot | GPIO0/2 high and GPIO15 low at reset; no sensor connected to flash pins |
| USB serial unreadable/silent | Expected: UART is binary and NodeMCU runtime UART is swapped; use app/API diagnostics |
| App will not load | Connected to AprilRobot; use HTTP, not HTTPS; stay on Wi-Fi without internet |
| Controls busy | Another page owns control; stop it or wait for its heartbeat to expire |
| Manual works, autonomy waits | Camera/PSRAM/init failure, stale frames, or selection not acknowledged; inspect cameraReady/age |
| Tag not detected | Correct ID/family, clear white border, focus, lighting, image orientation and sufficient tag size |
| Robot stuck after search | Choose Manual to reposition; then choose the intended destination again |
| Robot faults after link/stall | Fix wiring/power/mechanics, then tap Connect controls again |

UART routing and boot constraints are documented by [ESP8266 Arduino](https://arduino-esp8266.readthedocs.io/en/stable/reference.html), [Espressif ESP8266 boot selection](https://docs.espressif.com/projects/esptool/en/latest/esp8266/advanced-topics/boot-mode-selection.html), and [Espressif ESP32 boot selection](https://docs.espressif.com/projects/esptool/en/latest/esp32/advanced-topics/boot-mode-selection.html).
