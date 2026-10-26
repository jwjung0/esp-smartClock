#!/usr/bin/env python3
"""Regenerate src/loading_gif.c from assets/loading.gif"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
data = (root / "assets" / "loading.gif").read_bytes()
if data[:6] not in (b"GIF87a", b"GIF89a"):
    raise SystemExit("assets/loading.gif is not a GIF")

(root / "src" / "loading_gif.h").write_text(
    f"""#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {{
#endif

extern const uint8_t loading_gif_data[{len(data)}];
enum {{ kLoadingGifSize = {len(data)} }};

#ifdef __cplusplus
}}
#endif
"""
)

lines = [
    '#include "loading_gif.h"',
    "",
    "/* Keep in flash (.rodata), not DRAM */",
    f"const uint8_t loading_gif_data[{len(data)}] "
    '__attribute__((section(".rodata"))) = {',
]
for i in range(0, len(data), 16):
    chunk = data[i : i + 16]
    lines.append("  " + ", ".join(f"0x{b:02X}" for b in chunk) + ",")
lines.append("};")
lines.append("")
(root / "src" / "loading_gif.c").write_text("\n".join(lines))
print(f"embedded {len(data)} bytes -> src/loading_gif.c")
