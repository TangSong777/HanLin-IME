"""Render the HanLin badge from the locally installed YueHei font."""

import argparse
from io import BytesIO
import os
from pathlib import Path
import struct

from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.recordingPen import RecordingPen
from fontTools.pens.t2CharStringPen import T2CharStringPen
from fontTools.ttLib import TTFont
from PIL import Image, ImageChops, ImageDraw, ImageFont


SIZES = (16, 20, 24, 25, 32, 40, 48, 64, 96, 128, 256)
TASKBAR_SIZES = (16, 20, 24, 32)
PAPER = "#F8F8F6"
FRAME = "#CF6266"
WATER = "#AA333C"
WOOD = "#000000"


def component_fonts(font_path):
    with TTFont(font_path, recalcBBoxes=False, recalcTimestamp=False) as font:
        glyph_name = font.getBestCmap()[ord("淋")]
        glyph = font.getGlyphSet()[glyph_name]
        width = glyph.width
        outline = RecordingPen()
        glyph.draw(outline)
        contours = []
        for operator, points in outline.value:
            if operator == "moveTo":
                contour = RecordingPen()
            getattr(contour, operator)(*points)
            if operator == "closePath":
                bounds = BoundsPen(None)
                contour.replay(bounds)
                contours.append((bounds.bounds, contour))

        # The lower water stroke and the first wood stroke overlap in x.
        # Assign full contours so both strokes retain their own color.
        contours.sort(key=lambda entry: entry[0][0])
        water, wood = contours[:3], contours[3:]
        if (len(contours) != 9 or max(bounds[0] for bounds, _ in water) >= wood[0][0][0]
                or max(bounds[2] for bounds, _ in water) >= wood[1][0][0]):
            raise ValueError("Expected the three water strokes and six wood contours of YueHei 淋")

        table = font["CFF "].cff
        charstrings = table.topDictIndex[0].CharStrings
        private = charstrings[glyph_name].private
        for component in (water, wood):
            pen = T2CharStringPen(width, None)
            for _, contour in component:
                contour.replay(pen)
            charstrings[glyph_name] = pen.getCharString(private=private, globalSubrs=table.GlobalSubrs)
            mask_font = BytesIO()
            font.save(mask_font)
            yield mask_font.getvalue()


def render(size, water_font, wood_font):
    scale = 8
    n = size * scale
    taskbar = size in TASKBAR_SIZES
    inset = 0 if taskbar else max(2, n // 40)
    radius = round(n * (0.18 if taskbar else 0.22))
    border = max(round(scale * 1.35), round(n * 0.06)) if taskbar else max(scale, n // 24)
    badge = Image.new("RGBA", (n, n))
    ImageDraw.Draw(badge).rounded_rectangle(
        (inset, inset, n - inset - 1, n - inset - 1), radius,
        fill=PAPER, outline=FRAME, width=border,
    )

    stroke = max(2, n // 65) if taskbar else max(1, n // 110)
    masks = []
    for data in (water_font, wood_font):
        mask = Image.new("L", (2 * n, 2 * n))
        face = ImageFont.truetype(BytesIO(data), n)
        ImageDraw.Draw(mask).text((n // 3, n // 3), "淋", 255, font=face, stroke_width=stroke)
        masks.append(mask)

    bounds = ImageChops.lighter(*masks).getbbox()
    width, height = bounds[2] - bounds[0], bounds[3] - bounds[1]
    max_width, max_height = (0.78, 0.78) if taskbar else (0.64, 0.65)
    ratio = min(n * max_width / width, n * max_height / height)
    fitted = (round(width * ratio), round(height * ratio))
    position = ((n - fitted[0]) // 2, (n - fitted[1]) // 2)
    for mask, color in zip(masks, (WATER, WOOD)):
        badge.paste(color, position, mask.crop(bounds).resize(fitted, Image.Resampling.BILINEAR))
    return badge.resize((size, size), Image.Resampling.BOX)


def write_ico(path, frames):
    directory = bytearray(struct.pack("<HHH", 0, 1, len(frames)))
    offset = len(directory) + 16 * len(frames)
    for size, png in frames.items():
        directory.extend(struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(png), offset))
        offset += len(png)
    path.write_bytes(directory + b"".join(frames.values()))


def main():
    src = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--font", type=Path, default=Path(os.environ.get("LOCALAPPDATA", "")) /
                        "Microsoft/Windows/Fonts/ZaoZiGongFangYueHeiChangGuiTi-1.otf")
    args = parser.parse_args()
    water_font, wood_font = component_fonts(args.font)
    frames = {}
    for size in SIZES:
        png = BytesIO()
        render(size, water_font, wood_font).save(png, format="PNG", optimize=True)
        frames[size] = png.getvalue()
    write_ico(src / "HanLin/icons/hanlin.ico", frames)
    (src / "tools/hanlin-word-icon.png").write_bytes(frames[48])


if __name__ == "__main__":
    main()
