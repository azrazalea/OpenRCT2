#!/usr/bin/env python3
"""Generate the AI Agent toolbar button sprites from agent_icon.png."""

from __future__ import annotations

import math
from pathlib import Path
from typing import Sequence

import numpy as np
from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ICON = ROOT / "agent_icon.png"
TEMPLATE_NORMAL = ROOT / "resources/g2/icons/multiplayer_toolbar.png"
TEMPLATE_PRESSED = ROOT / "resources/g2/icons/multiplayer_toolbar_pressed.png"
OUTPUT_NORMAL = ROOT / "resources/g2/icons/agent_toolbar.png"
OUTPUT_PRESSED = ROOT / "resources/g2/icons/agent_toolbar_pressed.png"

BUTTON_WIDTH = 30
BUTTON_HEIGHT = 27
MAX_GLYPH_WIDTH = 22
MAX_GLYPH_HEIGHT = 22

BACKGROUND_INDICES = {0, 248, 249, 250, 251, 252}
GLYPH_LEVELS_NORMAL = (132, 133, 134, 135, 136, 137)
GLYPH_LEVELS_PRESSED = (132, 133, 134, 134, 135, 135)

if hasattr(Image, "Resampling"):
    RESAMPLE = Image.Resampling.LANCZOS  # type: ignore[attr-defined]
else:  # pragma: no cover - legacy Pillow
    RESAMPLE = Image.LANCZOS  # type: ignore[attr-defined]


def _load_source_icon() -> Image.Image:
    if not SOURCE_ICON.exists():
        raise FileNotFoundError(f"Missing source icon: {SOURCE_ICON}")
    return Image.open(SOURCE_ICON).convert("L")


def _compute_size(width: int, height: int) -> tuple[int, int]:
    scale = min(MAX_GLYPH_WIDTH / width, MAX_GLYPH_HEIGHT / height)
    scale = min(1.0, scale)
    new_width = max(1, int(round(width * scale)))
    new_height = max(1, int(round(height * scale)))
    return new_width, new_height


def _prepare_glyph() -> np.ndarray:
    source = _load_source_icon()
    arr = np.array(source)
    mask = arr < 250
    ys, xs = np.where(mask)
    if ys.size == 0 or xs.size == 0:
        raise RuntimeError("Source icon does not contain any opaque pixels")

    bbox = (int(xs.min()), int(ys.min()), int(xs.max()) + 1, int(ys.max()) + 1)
    cropped = source.crop(bbox)
    new_size = _compute_size(*cropped.size)
    resized = cropped.resize(new_size, RESAMPLE)

    glyph = np.array(resized, dtype=np.float32)
    glyph_mask = glyph < 250
    if not glyph_mask.any():
        raise RuntimeError("Glyph mask disappeared after resizing")

    values = glyph[glyph_mask]
    min_val = float(values.min())
    max_val = float(values.max())
    norm = np.zeros_like(glyph, dtype=np.float32)
    if not math.isclose(max_val, min_val):
        norm[glyph_mask] = (values - min_val) / (max_val - min_val)
        norm[glyph_mask] = 1.0 - norm[glyph_mask]

    buckets = np.zeros_like(glyph, dtype=np.int8)
    span = len(GLYPH_LEVELS_NORMAL) - 1
    if span <= 0:
        raise RuntimeError("Not enough glyph levels configured")
    buckets[glyph_mask] = np.clip(np.rint(norm[glyph_mask] * span), 0, span).astype(np.int8)
    buckets[~glyph_mask] = -1
    return buckets


def _build_background(template_path: Path) -> Image.Image:
    template = Image.open(template_path)
    if template.mode != "P":
        template = template.convert("P")

    palette = template.getpalette()
    width, height = template.size
    src = template.load()

    background = Image.new("P", (width, height))
    background.putpalette(palette)
    dst = background.load()

    for y in range(height):
        row = [src[x, y] for x in range(width)]
        bg_positions = [idx for idx, value in enumerate(row) if value in BACKGROUND_INDICES]
        if not bg_positions:
            bg_positions = [0]
        for x in range(width):
            value = row[x]
            if value in BACKGROUND_INDICES:
                dst[x, y] = value
            else:
                nearest = min(bg_positions, key=lambda pos: abs(pos - x))
                dst[x, y] = row[nearest]
    return background


def _compose_button(
    background: Image.Image,
    glyph_levels: np.ndarray,
    palette_levels: Sequence[int],
    y_offset: int = 0,
) -> Image.Image:
    button = background.copy()
    px = button.load()
    glyph_height, glyph_width = glyph_levels.shape
    start_x = max(0, (button.width - glyph_width) // 2)
    start_y = max(0, (button.height - glyph_height) // 2 + y_offset)

    for y in range(glyph_height):
        for x in range(glyph_width):
            level = int(glyph_levels[y, x])
            if level < 0:
                continue
            px[start_x + x, start_y + y] = palette_levels[level]
    return button


def generate() -> None:
    glyph_levels = _prepare_glyph()

    background_normal = _build_background(TEMPLATE_NORMAL)
    background_pressed = _build_background(TEMPLATE_PRESSED)

    normal = _compose_button(background_normal, glyph_levels, GLYPH_LEVELS_NORMAL)
    pressed = _compose_button(background_pressed, glyph_levels, GLYPH_LEVELS_PRESSED, y_offset=1)

    normal.save(OUTPUT_NORMAL)
    pressed.save(OUTPUT_PRESSED)


if __name__ == "__main__":
    generate()
