#!/usr/bin/env python3
"""Derive private Vita artwork from the supported, user-supplied APK.

Asset preparation verifies the APK before calling generate_package_assets(). No
original artwork is stored in the repository. Pillow is needed for preparation.
Image constraints and LiveArea structure follow the VitaSDK samples:
https://github.com/vitasdk/samples#notes-on-images
https://github.com/vitasdk/samples/tree/master/pretty_livearea
"""

from io import BytesIO
from pathlib import Path
import struct
import zipfile
import zlib

ICON_SOURCE = "res/drawable-xxxhdpi-v4/icon.png"
BACKGROUND_SOURCE = "assets/settings_bg@2x.png"
PNG_SIZES = {
    "icon0.png": (128, 128),
    "pic0.png": (960, 544),
    "bg0.png": (840, 500),
}
TEMPLATE = """<?xml version="1.0" encoding="utf-8"?>

<livearea style="a1" format-ver="01.00" content-rev="4">
    <livearea-background>
        <image>bg0.png</image>
    </livearea-background>

    <gate>
        <startup-image>../../pic0.png</startup-image>
    </gate>
</livearea>
"""


def decode_zomg(data: bytes, expected_size: tuple[int, int]):
    """Read the supported game's padded, zlib-compressed RGBA image container."""
    from PIL import Image

    if len(data) < 36:
        raise ValueError("Truncated original image")
    magic, flags, layout, width, height, visible_width, visible_height, depth, bits = struct.unpack(
        "<4s8I", data[:36]
    )
    if (
        (magic, flags, layout, depth, bits) != (b"ZOMG", 0, 1, 8, 32)
        or (width, height) != (1024, 1024)
        or (visible_width, visible_height) != expected_size
    ):
        raise ValueError("Unsupported original image layout")
    pixels = zlib.decompress(data[36:])
    if len(pixels) != width * height * 4:
        raise ValueError("Invalid original image data size")
    return Image.frombytes("RGBA", (width, height), pixels).crop(
        (0, 0, visible_width, visible_height)
    )


def save_vita_png(image, path: Path) -> None:
    """Opaque, noninterlaced PNG-8 with a 256-color dithered palette."""
    from PIL import Image

    image = image.convert("RGB")
    palette = image.quantize(colors=256, method=Image.Quantize.MEDIANCUT)
    indexed = image.quantize(palette=palette, dither=Image.Dither.FLOYDSTEINBERG)
    indexed.save(path, format="PNG", bits=8, compress_level=9)


def validate_package_assets(output: Path) -> None:
    """Reject incorrect dimensions, truecolor/alpha PNGs and oversized images."""
    from PIL import Image

    for name, size in PNG_SIZES.items():
        path = output / name
        data = path.read_bytes()
        if (
            len(data) < 33
            or data[:16] != b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR"
            or struct.unpack(">IIBBBBB", data[16:29]) != (*size, 8, 3, 0, 0, 0)
            or len(data) > 420 * 1024
        ):
            raise ValueError(f"Invalid Vita PNG layout or size: {path}")
        with Image.open(path) as image:
            image.load()
            if image.mode != "P" or "transparency" in image.info:
                raise ValueError(f"Vita artwork must be indexed and opaque: {path}")
    if (output / "template.xml").read_bytes() != TEMPLATE.replace("\n", "\r\n").encode():
        raise ValueError("Invalid LiveArea template")


def generate_package_assets(apk: Path, output: Path) -> None:
    """Write package graphics; pic0.png also backs the loading screen and gate."""
    try:
        from PIL import Image, ImageOps
    except ImportError as error:
        raise RuntimeError("Pillow is missing; install the Python build requirements") from error

    output.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(apk) as archive:
        with Image.open(BytesIO(archive.read(ICON_SOURCE))) as source:
            if source.size != (192, 192):
                raise ValueError("Unsupported original app icon size")
            rgba = source.convert("RGBA")
            icon = Image.new("RGB", rgba.size, (0, 0, 0))
            icon.paste(rgba, mask=rgba.getchannel("A"))
        background = decode_zomg(archive.read(BACKGROUND_SOURCE), (960, 640)).convert("RGB")

    for name, size in PNG_SIZES.items():
        if name == "icon0.png":
            image = icon.resize(size, Image.Resampling.LANCZOS)
        else:
            image = ImageOps.fit(background, size, Image.Resampling.LANCZOS)
        save_vita_png(image, output / name)

    (output / "template.xml").write_bytes(TEMPLATE.replace("\n", "\r\n").encode())
    validate_package_assets(output)
