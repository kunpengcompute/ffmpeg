#!/usr/bin/env python3
"""Generate ten fresh random YUV420P frames for each input resolution."""

import argparse
import os
from pathlib import Path


FRAME_COUNT = 10
SIZES = ((1920, 1080), (1920, 1280), (3840, 2160))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output_dir", nargs="?", type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    # Map fresh random bytes to limited range: Y=16..235, U/V=16..240.
    y_table = bytes(16 + value * 220 // 256 for value in range(256))
    uv_table = bytes(16 + value * 225 // 256 for value in range(256))
    for width, height in SIZES:
        y_size = width * height
        uv_size = y_size // 4
        path = args.output_dir / f"input_{width}x{height}.yuv"
        with path.open("wb") as output:
            for _ in range(FRAME_COUNT):
                output.write(os.urandom(y_size).translate(y_table))
                output.write(os.urandom(uv_size).translate(uv_table))
                output.write(os.urandom(uv_size).translate(uv_table))

        print(f"Generated: {path} (YUV420P, {FRAME_COUNT} frames, {path.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
