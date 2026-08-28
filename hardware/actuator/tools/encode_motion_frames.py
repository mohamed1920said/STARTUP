"""Encode SOLIDWORKS Motion Study BMP frames into a portable GIF preview."""

from __future__ import annotations

import hashlib
import sys
from pathlib import Path

from PIL import Image


def main() -> int:
    root = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path.cwd()
    motion_dir = root / "hardware" / "actuator" / "cad" / "motion"
    frames_dir = motion_dir / "frames"
    frame_paths = sorted(frames_dir.glob("frame_*.bmp"))
    if len(frame_paths) != 37:
        raise RuntimeError(f"Expected 37 SOLIDWORKS frames, found {len(frame_paths)}")

    unique = {hashlib.sha256(path.read_bytes()).hexdigest() for path in frame_paths}
    if len(unique) < 5:
        raise RuntimeError(f"Motion preview is visually static: only {len(unique)} unique frames")

    frames: list[Image.Image] = []
    for path in frame_paths:
        with Image.open(path) as source:
            frame = source.convert("RGB").resize((960, 540), Image.Resampling.LANCZOS)
            frames.append(frame.copy())

    gif_path = motion_dir / "AMR_Valve_Open_Close.gif"
    frames[0].save(
        gif_path,
        save_all=True,
        append_images=frames[1:],
        duration=167,
        loop=0,
        optimize=True,
        disposal=2,
    )

    poster_path = motion_dir / "AMR_Valve_Open_Close_Poster.png"
    frames[len(frames) // 2].save(poster_path, optimize=True)
    for path in frame_paths:
        path.unlink()
    print(f"GIF={gif_path}")
    print(f"POSTER={poster_path}")
    print(f"FRAMES={len(frames)} UNIQUE={len(unique)} SIZE={frames[0].size}")
    print("RAW_BMP_FRAMES_REMOVED=True")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
