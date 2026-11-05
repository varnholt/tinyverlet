"""Turn a testbed recording into an animated GIF.

    tinyverlet_testbed --scene "curtain in the wind" --record frames --frames 96 --fps 24
    python tools/make_gif.py frames curtain.gif --fps 24

Needs Pillow (pip install pillow).
"""

import argparse
from pathlib import Path

from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("frames", type=Path, help="directory with frame_0000.png ...")
    parser.add_argument("output", type=Path, help="GIF to write")
    parser.add_argument("--fps", type=float, default=24.0)
    parser.add_argument("--width", type=int, default=0, help="scale to this width (0 keeps the size)")
    parser.add_argument("--colors", type=int, default=96)
    args = parser.parse_args()

    files = sorted(args.frames.glob("frame_*.png"))
    if not files:
        raise SystemExit(f"no frames in {args.frames}")

    frames = []
    for file in files:
        image = Image.open(file).convert("RGB")
        if args.width and image.width != args.width:
            image = image.resize((args.width, round(image.height * args.width / image.width)), Image.LANCZOS)
        frames.append(image)

    # one palette for the whole clip avoids flicker between frames
    palette = frames[len(frames) // 2].quantize(colors=args.colors, method=Image.Quantize.MEDIANCUT)
    quantized = [frame.quantize(palette=palette, dither=Image.Dither.NONE) for frame in frames]

    quantized[0].save(
        args.output,
        save_all=True,
        append_images=quantized[1:],
        duration=round(1000 / args.fps),
        loop=0,
        optimize=True,
    )
    print(f"{args.output}: {len(frames)} frames, {args.output.stat().st_size // 1024} KB")


if __name__ == "__main__":
    main()
