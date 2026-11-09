"""Build and upload one or both boards over the AprilRobot Wi-Fi network."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import time
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
BOARDS = {
    "esp32cam": ("esp32cam_ota", "http://192.168.4.250/calibration"),
    "nodemcu": ("nodemcu_ota", "http://192.168.4.1/api/state"),
}


def api(url, post=False):
    req = urllib.request.Request(url, data=b"" if post else None)
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open(req, timeout=2) as response:
        return json.load(response)


def platformio():
    command = shutil.which("pio") or shutil.which("platformio")
    if command:
        return command
    for relative in (".platformio/penv/Scripts/platformio.exe", ".platformio/penv/bin/platformio"):
        candidate = Path.home() / relative
        if candidate.exists():
            return str(candidate)
    raise RuntimeError("PlatformIO was not found. Run this from a PlatformIO terminal.")


def enter_maintenance():
    state = api("http://192.168.4.1/api/state")
    deadline = (state["now"] + 600) & 0xFFFFFFFF
    api(f"http://192.168.4.1/api/maintenance?enabled=1&deadline={deadline}", post=True)


def wait_for(description, check, timeout=75):
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        try:
            if check():
                return
        except (OSError, ValueError, KeyError):
            pass
        time.sleep(.5)
    raise RuntimeError(f"Timed out waiting for {description}. Check power, UART and the Wi-Fi connection.")


def camera_ready():
    state = api("http://192.168.4.1/api/state")
    return state["maintenance"] and state["cameraMaintenance"] and not state["updateBusy"]


def upload(board, pio):
    env, address = BOARDS[board]
    enter_maintenance()
    if board == "esp32cam":
        wait_for("camera maintenance acknowledgement", camera_ready, timeout=10)
    before = api(address)["bootId"]
    print(f"Uploading {board}. Keep both boards powered.", flush=True)
    subprocess.run([pio, "run", "-e", env, "-t", "upload"], cwd=ROOT, check=True)
    print("Waiting for restart. Rejoin AprilRobot if your computer leaves the network.", flush=True)
    wait_for(board + " restart", lambda: api(address)["bootId"] != before)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("board", choices=("esp32cam", "nodemcu", "both"))
    parser.add_argument("--dry-run", action="store_true", help="show commands without contacting the robot")
    args = parser.parse_args()
    # Update the camera first: updating the NodeMCU restarts the Wi-Fi access point.
    targets = ("esp32cam", "nodemcu") if args.board == "both" else (args.board,)
    try:
        pio = platformio()
        for board in targets:
            if args.dry_run:
                print(f"{pio} run -e {BOARDS[board][0]} -t upload")
            else:
                upload(board, pio)
        if not args.dry_run:
            print("Upload complete. Finish maintenance if shown, then Connect controls in the app.")
    except (OSError, RuntimeError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Upload stopped: {error}\nUse the wired upload procedure if this firmware is not installed yet.\n")


if __name__ == "__main__":
    main()
