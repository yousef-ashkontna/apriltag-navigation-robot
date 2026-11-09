"""Calibrate the robot's OV2640 from a printed chessboard, then save it to flash."""
import argparse
import json
import math
from pathlib import Path
import urllib.error
import urllib.request

import cv2
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
CAMERA = "http://192.168.4.250"
PATTERN = (9, 6)  # inner corners: the printed board has 10 by 7 squares
SQUARE_MM = 20.0
FIELDS = ("version", "width", "height", "hmirror", "vflip", "fx", "fy", "cx", "cy",
          "k1", "k2", "p1", "p2", "k3", "rms")


def request(camera, path, data=None):
    payload = None if data is None else json.dumps(data, allow_nan=False).encode()
    req = urllib.request.Request(camera.rstrip("/") + path, data=payload,
                                 headers={"Content-Type": "application/json"} if payload else {})
    # A system proxy should never carry requests to the robot's local address.
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    try:
        with opener.open(req, timeout=3) as response:
            return response.read(), response.headers
    except urllib.error.HTTPError as error:
        raise RuntimeError(error.read().decode(errors="replace")) from error
    except urllib.error.URLError as error:
        raise RuntimeError(f"Cannot reach {camera}. Join AprilRobot Wi-Fi and power both boards.") from error


def camera_settings(camera):
    body, _ = request(camera, "/calibration")
    return json.loads(body)


def save_json(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(data, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    temporary.replace(path)


def validate(values):
    if any(key not in values or isinstance(values[key], bool) or
           not isinstance(values[key], (int, float)) or not math.isfinite(values[key])
           for key in FIELDS):
        raise ValueError("Calibration contains missing or non-numeric values")
    if (values["version"] != 1 or values["width"] != 320 or values["height"] != 240 or
            values["hmirror"] not in (0, 1) or values["vflip"] not in (0, 1)):
        raise ValueError("Expected version 1 calibration at 320 x 240")
    if not (50 <= values["fx"] <= 2000 and 50 <= values["fy"] <= 2000 and
            0 <= values["cx"] < 320 and 0 <= values["cy"] < 240 and
            0 <= values["rms"] <= 1.5 and
            all(abs(values[key]) <= 5 for key in ("k1", "k2", "k3")) and
            all(abs(values[key]) <= 1 for key in ("p1", "p2"))):
        raise ValueError("Implausible lens values; collect sharper, more varied views")


def upload(camera, values):
    validate(values)
    state = camera_settings(camera)
    if not state.get("maintenance") or state.get("busy"):
        raise ValueError("Enable Updates & calibration in the robot app before saving")
    for key in ("width", "height", "hmirror", "vflip"):
        if values[key] != state["active"][key]:
            raise ValueError(f"Camera {key} differs from the saved images. Recalibrate this orientation.")
    request(camera, "/calibration", {key: values[key] for key in FIELDS})
    state = camera_settings(camera)
    if not state["saved"] or any(not math.isclose(state["saved"][key], values[key],
                                                  rel_tol=1e-5, abs_tol=1e-6) for key in FIELDS):
        raise RuntimeError("Flash readback did not match; keep the JSON backup and try again")
    print("Saved to ESP32-CAM flash. Restart the camera to use these values, then finish maintenance in the app.")


def find_corners(image):
    found, corners = cv2.findChessboardCornersSB(
        image, PATTERN, flags=cv2.CALIB_CB_NORMALIZE_IMAGE | cv2.CALIB_CB_EXHAUSTIVE)
    return corners if found else None


def object_points():
    points = np.zeros((PATTERN[0] * PATTERN[1], 3), np.float32)
    points[:, :2] = np.mgrid[0:PATTERN[0], 0:PATTERN[1]].T.reshape(-1, 2) * SQUARE_MM
    return points


def fit_camera(image_points, metadata):
    if len(image_points) < 15:
        raise ValueError("Need at least 15 usable views; aim for 20 with different tilts and positions")
    all_corners = np.concatenate(image_points).reshape(-1, 2)
    coverage = np.ptp(all_corners, axis=0) / np.array([320, 240])
    if np.any(coverage < [0.65, 0.65]):
        raise ValueError("Move the board nearer the image edges; the samples cover too little of the frame")
    objects = [object_points() for _ in image_points]
    # k3 is poorly constrained at QVGA. Keep it at zero rather than overfitting it.
    rms, matrix, distortion, rotations, translations = cv2.calibrateCamera(
        objects, image_points, (320, 240), None, None, flags=cv2.CALIB_FIX_K3)
    per_view = []
    normals = []
    for points, rotation, translation in zip(image_points, rotations, translations):
        projected, _ = cv2.projectPoints(object_points(), rotation, translation, matrix, distortion)
        per_view.append(float(np.sqrt(np.mean(np.sum((points - projected) ** 2, axis=2)))))
        normals.append(cv2.Rodrigues(rotation)[0][:, 2])
    angular_span = max(math.acos(float(np.clip(np.dot(a, b), -1, 1)))
                       for a in normals for b in normals)
    if angular_span < math.radians(15):
        raise ValueError("The board views are too similar. Tilt it left/right and up/down, then collect again")
    if rms > 0.8 or max(per_view) > 1.2:
        raise ValueError(f"Fit is too noisy (RMS {rms:.3f} px, worst view {max(per_view):.3f} px). "
                         "Check print flatness, focus and motion blur, then collect again")
    values = {key: metadata[key] for key in ("width", "height", "hmirror", "vflip")}
    values.update(version=1, fx=float(matrix[0, 0]), fy=float(matrix[1, 1]),
                  cx=float(matrix[0, 2]), cy=float(matrix[1, 2]), rms=float(rms))
    values.update(zip(("k1", "k2", "p1", "p2", "k3"), map(float, distortion.ravel())))
    validate(values)
    return values, per_view, coverage.tolist()


def solve(directory):
    metadata = json.loads((directory / "capture.json").read_text(encoding="utf-8"))
    if (metadata["width"], metadata["height"]) != (320, 240):
        raise ValueError("Images must be captured at the robot's 320 x 240 resolution")
    image_points, names = [], []
    for path in sorted((directory / "images").glob("*.png")):
        image = cv2.imread(str(path), cv2.IMREAD_GRAYSCALE)
        if image is None or image.shape != (240, 320):
            raise ValueError(f"Wrong image size or unreadable image: {path}")
        corners = find_corners(image)
        if corners is None:
            print(f"Skipping {path.name}: board not found")
            continue
        image_points.append(corners)
        names.append(path.name)
    values, errors, coverage = fit_camera(image_points, metadata)
    save_json(directory / "camera.json", values)
    save_json(directory / "report.json", {"rms_pixels": values["rms"], "coverage": coverage,
                                         "view_errors_pixels": dict(zip(names, errors))})
    print(f"Saved {directory / 'camera.json'} — {len(names)} views, RMS {values['rms']:.3f} pixels")
    return values


def capture(camera, directory, samples):
    state = camera_settings(camera)
    if not state.get("maintenance") or state.get("busy"):
        raise ValueError("Enable Updates & calibration in the app before collecting images")
    metadata = {key: state["active"][key] for key in ("width", "height", "hmirror", "vflip")}
    image_dir = directory / "images"
    if image_dir.exists() and any(image_dir.glob("*.png")):
        raise ValueError("This folder already has images. Use 'solve' or choose a new --directory")
    image_dir.mkdir(parents=True, exist_ok=True)
    save_json(directory / "capture.json", metadata)
    print("SPACE: save a sharp view. Q: finish. Move and tilt the board between pictures.")
    accepted, last_id = [], None
    try:
        while len(accepted) < samples:
            payload, headers = request(camera, "/calibration/frame")
            age = int(headers.get("X-Frame-Age-Ms", "99999"))
            identity = (headers.get("X-Camera-Session"), headers.get("X-Frame-Id"))
            image = cv2.imdecode(np.frombuffer(payload, np.uint8), cv2.IMREAD_GRAYSCALE)
            if image is None or image.shape != (240, 320) or age > 1500:
                raise ValueError("No fresh 320 x 240 image from the camera")
            corners = find_corners(image)
            preview = cv2.cvtColor(image, cv2.COLOR_GRAY2BGR)
            if corners is not None:
                cv2.drawChessboardCorners(preview, PATTERN, corners, True)
            preview = cv2.resize(preview, (960, 720), interpolation=cv2.INTER_NEAREST)
            cv2.putText(preview, f"{len(accepted)}/{samples}   SPACE: save   Q: finish", (20, 35),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 100), 2)
            cv2.imshow("Robot camera calibration", preview)
            key = cv2.waitKey(30) & 0xFF
            if key in (ord("q"), 27):
                break
            if key != ord(" "):
                continue
            if corners is None:
                print("All 9 x 6 inner corners need to be visible and in focus.")
                continue
            if identity == last_id or any(float(np.sqrt(np.mean((corners - old) ** 2))) < 3
                                          for old in accepted):
                print("That view is too similar. Move or tilt the board a little more.")
                continue
            path = image_dir / f"view-{len(accepted) + 1:02d}.png"
            if not cv2.imwrite(str(path), image):
                raise RuntimeError(f"Could not write {path}")
            accepted.append(corners)
            last_id = identity
            print(f"Saved {path.name}")
    finally:
        cv2.destroyAllWindows()


def make_board(path):
    squares = []
    for row in range(7):
        for col in range(10):
            if (row + col) % 2 == 0:
                squares.append(f'<rect x="{10 + col * 20}" y="{10 + row * 20}" width="20" height="20"/>')
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" width="220mm" height="160mm" viewBox="0 0 220 160">'
           '<rect width="220" height="160" fill="white"/>' + "".join(squares) + '</svg>')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(svg + "\n", encoding="utf-8")
    print(f"Saved {path}. Print at actual size, landscape; each square should be 20 mm.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--camera", default=CAMERA)
    parser.add_argument("--directory", type=Path, default=ROOT / "calibration")
    sub = parser.add_subparsers(dest="command", required=True)
    collect = sub.add_parser("capture", help="collect views, solve and upload in maintenance mode")
    collect.add_argument("--samples", type=int, default=20)
    collect.add_argument("--no-upload", action="store_true")
    sub.add_parser("solve", help="solve saved images without connecting to the robot")
    restore = sub.add_parser("upload", help="restore a calibration JSON file to camera flash")
    restore.add_argument("file", type=Path)
    sub.add_parser("status", help="show the active and saved camera settings")
    sub.add_parser("board", help="write the printable chessboard SVG")
    args = parser.parse_args()
    try:
        if args.command == "board":
            make_board(ROOT / "docs" / "CALIBRATION_BOARD.svg")
        elif args.command == "status":
            print(json.dumps(camera_settings(args.camera), indent=2))
        elif args.command == "upload":
            upload(args.camera, json.loads(args.file.read_text(encoding="utf-8")))
        elif args.command == "solve":
            solve(args.directory)
        else:
            if not 15 <= args.samples <= 100:
                raise ValueError("Choose between 15 and 100 samples")
            capture(args.camera, args.directory, args.samples)
            values = solve(args.directory)
            if not args.no_upload:
                upload(args.camera, values)
    except (ValueError, RuntimeError, OSError, KeyError, cv2.error) as error:
        parser.exit(1, f"Calibration stopped: {error}\n")


if __name__ == "__main__":
    main()
