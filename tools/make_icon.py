#!/usr/bin/env python3
"""Generate the JCleaner application icon.

Design:
  - rounded-square tile with a coffee-toned diagonal gradient
  - warm highlight in the upper-left corner
  - cream "JCleaner" wordmark on larger sizes
  - bold "J" monogram on small sizes (16-64 px) for legibility
  - transparent corners, multi-resolution .ico for Windows

Outputs:
  resources/app.ico (16/32/64/128 px, used by the application)
"""

from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[1]
RESOURCES = ROOT / "resources"

ICON_SIZES = (16, 32, 64, 128)
FULL_TEXT_MIN_SIZE = 128

# Coffee palette
LATTE = (186, 128, 86)
COFFEE = (112, 78, 54)
ESPRESSO = (36, 22, 14)
CREAM = (255, 248, 238)
SHADOW = (16, 8, 3)

FONT_CANDIDATES = (
    Path(r"C:\Windows\Fonts\segoeuib.ttf"),   # Segoe UI Bold
    Path(r"C:\Windows\Fonts\seguisb.ttf"),    # Segoe UI Semibold
    Path(r"C:\Windows\Fonts\arialbd.ttf"),    # Arial Bold
)


def lerp(a: tuple[int, int, int], b: tuple[int, int, int], t: float) -> tuple[int, int, int]:
    return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))


def pick_font() -> Path:
    for candidate in FONT_CANDIDATES:
        if candidate.exists():
            return candidate
    raise FileNotFoundError("No suitable bold system font found")


def gradient_background(px: int) -> Image.Image:
    """Diagonal latte -> coffee -> espresso gradient with a warm upper-left glow."""
    small = 128
    img = Image.new("RGB", (small, small))
    pixels = img.load()

    for y in range(small):
        ny = y / (small - 1)
        for x in range(small):
            nx = x / (small - 1)
            t = (nx + ny) * 0.5
            t = t * t * (3.0 - 2.0 * t)  # smoothstep

            if t < 0.5:
                color = lerp(LATTE, COFFEE, t / 0.5)
            else:
                color = lerp(COFFEE, ESPRESSO, (t - 0.5) / 0.5)

            # Soft radial highlight around (0.26, 0.20)
            dx = nx - 0.26
            dy = ny - 0.20
            dist = math.hypot(dx, dy)
            glow = max(0.0, 1.0 - dist / 0.62) ** 2 * 0.18
            pixels[x, y] = tuple(
                min(255, int(c + (255 - c) * glow)) for c in color
            )

    return img.resize((px, px), Image.Resampling.BICUBIC)


def rounded_mask(px: int, ratio: float = 0.22) -> Image.Image:
    mask = Image.new("L", (px, px), 0)
    draw = ImageDraw.Draw(mask)
    radius = int(px * ratio)
    draw.rounded_rectangle((0, 0, px - 1, px - 1), radius=radius, fill=255)
    return mask


def add_inner_border(img: Image.Image, size: int) -> Image.Image:
    inset = max(1, int(size * 0.014))
    width = max(1, int(size * 0.006))
    radius = max(2, int(size * 0.22) - inset)
    overlay = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(overlay).rounded_rectangle(
        (inset, inset, size - 1 - inset, size - 1 - inset),
        radius=radius,
        outline=(255, 244, 230, 46),
        width=width,
    )
    return Image.alpha_composite(img, overlay)


def fit_font(draw: ImageDraw.ImageDraw, text: str, font_path: Path,
             max_width: float, max_height: float, start: int) -> ImageFont.FreeTypeFont:
    """Binary-search the largest font size that fits the given box."""
    best = ImageFont.truetype(str(font_path), 1)
    low, high = 1, max(1, start)
    while low <= high:
        mid = (low + high) // 2
        font = ImageFont.truetype(str(font_path), mid)
        bbox = draw.textbbox((0, 0), text, font=font)
        width = bbox[2] - bbox[0]
        height = bbox[3] - bbox[1]
        if width <= max_width and height <= max_height:
            best = font
            low = mid + 1
        else:
            high = mid - 1
    return best


def render(size: int) -> Image.Image:
    scale = 4
    canvas = size * scale
    font_path = pick_font()

    img = gradient_background(canvas).convert("RGBA")
    img.putalpha(rounded_mask(canvas))
    if size >= 64:
        img = add_inner_border(img, canvas)

    text = "JCleaner" if size >= FULL_TEXT_MIN_SIZE else "J"
    full_text = text == "JCleaner"

    max_width = canvas * (0.82 if full_text else 0.52)
    max_height = canvas * (0.27 if full_text else 0.50)
    font = fit_font(
        ImageDraw.Draw(img), text, font_path,
        max_width, max_height,
        int(canvas * (0.60 if full_text else 0.62)),
    )

    bbox = ImageDraw.Draw(img).textbbox((0, 0), text, font=font)
    text_width = bbox[2] - bbox[0]
    text_height = bbox[3] - bbox[1]
    x = (canvas - text_width) / 2 - bbox[0]
    y = (canvas - text_height) / 2 - bbox[1]

    # Soft drop shadow for the wordmark keeps it readable over the gradient
    if full_text:
        shadow = Image.new("RGBA", img.size, (0, 0, 0, 0))
        offset_x = max(1, int(canvas * 0.006))
        offset_y = max(1, int(canvas * 0.010))
        ImageDraw.Draw(shadow).text(
            (x + offset_x, y + offset_y), text, font=font, fill=(*SHADOW, 120)
        )
        shadow = shadow.filter(ImageFilter.GaussianBlur(max(1, canvas * 0.008)))
        img = Image.alpha_composite(img, shadow)

    ImageDraw.Draw(img).text((x, y), text, font=font, fill=(*CREAM, 255))
    return img.resize((size, size), Image.Resampling.LANCZOS)


def main() -> None:
    RESOURCES.mkdir(parents=True, exist_ok=True)

    frames = [render(size) for size in ICON_SIZES]
    by_size = dict(zip(ICON_SIZES, frames))

    base = by_size[128]
    base.save(
        RESOURCES / "app.ico",
        format="ICO",
        sizes=[(size, size) for size in ICON_SIZES],
        append_images=[by_size[size] for size in ICON_SIZES if size != 128],
    )

    print(f"wrote {RESOURCES / 'app.ico'} ({len(frames)} frames)")


if __name__ == "__main__":
    main()
