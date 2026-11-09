"""Embed the offline phone app in flash; also run automatically before NodeMCU builds."""
from pathlib import Path
if "__file__" in globals():
    root = Path(__file__).resolve().parents[1]
else:
    Import("env")
    root = Path(env.subst("$PROJECT_DIR"))
html = (root / "web/index.html").read_text(encoding="utf-8")
assert ')MOBILE"' not in html
header = '#pragma once\n#include <Arduino.h>\nstatic const char MOBILE_PAGE[] PROGMEM = R"MOBILE(' + html + ')MOBILE";\n'
target = root / "include/mobile_page.h"
if not target.exists() or target.read_text(encoding="utf-8") != header:
    target.write_text(header, encoding="utf-8")
