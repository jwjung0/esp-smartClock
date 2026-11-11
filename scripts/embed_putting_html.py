#!/usr/bin/env python3
"""Embed web/putting.html into src/putting_html.h."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
src = (ROOT / "web" / "putting.html").read_text(encoding="utf-8")
if ")PUTHTML" in src:
    raise SystemExit("delimiter )PUTHTML found in HTML")
out = ROOT / "src" / "putting_html.h"
out.write_text(
    "#pragma once\n"
    "// Auto-generated from web/putting.html — do not edit by hand.\n"
    "// Run: python3 scripts/embed_putting_html.py\n"
    'static const char PUTTING_HTML[] = R"PUTHTML(\n'
    f"{src}\n"
    ')PUTHTML";\n',
    encoding="utf-8",
)
print(f"wrote {out}")
