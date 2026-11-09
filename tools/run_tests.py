"""Run hardware-independent checks with g++ and gcc (MinGW on Windows)."""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
os.chdir(ROOT)
OUT = ROOT / ".pio" / "host-tests"
OUT.mkdir(parents=True, exist_ok=True)
SUFFIX = ".exe" if os.name == "nt" else ""

def run(*args, **kwargs):
    subprocess.run([str(x) for x in args], check=True, **kwargs)

run(sys.executable, "tools/generate_tag.py")
control = OUT / ("test_control" + SUFFIX)
run("g++", "-std=c++11", "-Wall", "-Wextra", "-Werror", "-pedantic", "-Iinclude",
    "test/test_control.cpp", "-o", control)
run(control)
lib = ROOT / "lib/AprilTagESP32/src"
sources = [lib / f for f in ("apriltag.c", "apriltag_pose.c", "apriltag_quad_thresh.c", "tag36h11.c")]
sources += sorted((lib / "common").glob("*.c"))
run("gcc", "-std=gnu99", "-O2", "-I" + str(ROOT / "test/host"), "-I" + str(lib),
    "-c", *sources, cwd=OUT)
objects = [OUT / (source.stem + ".o") for source in sources]
detector = OUT / ("test_detector" + SUFFIX)
run("gcc", "-std=gnu99", "-O2", "-Itest/host", "-I" + str(lib), "test/test_detector.c", *objects,
    "-o", detector, "-lm", "-lpthread")
for tag_id in range(4):
    run(detector, ROOT / f".pio/test-tag-{tag_id}.pgm", tag_id)
calibration = OUT / ("test_camera_calibration" + SUFFIX)
run("g++", "-std=gnu++11", "-O2", "-Iinclude", "-Itest/host", "-I" + str(lib),
    "test/test_camera_calibration.cpp", *objects, "-o", calibration, "-lm", "-lpthread")
run(calibration)
print("All host checks passed. Hardware calibration and on-board timing remain to be verified.")
