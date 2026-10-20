from io import BytesIO
from pathlib import Path
import hashlib
import math
import re
import struct
import xml.etree.ElementTree as ET
import zipfile

from package_assets import BACKGROUND_SOURCE, ICON_SOURCE, decode_zomg

CATALOG_SOURCE = "assets/unpack/achievements.txt"
ENGLISH_SOURCE = "assets/unpack/en.strings"
LIBRARY_SOURCE = "lib/armeabi-v7a/libgame.so"
STAR_SOURCE = "assets/Ach_Star@2x.png"
CATALOG_KEY_ADDRESS = 0xCCF433
# Native trophy IDs require four uppercase letters followed by five digits.
# Keep this registered identity independent of the app title ID.
COMMUNICATION_ID = "PLGV00001"
SET_CAPACITY = 128
EXPECTED_COUNT = 167
EXPECTED_SET_COUNT = 2
TROPHY_SET_VERSION = "01.01"
SFM_SIGNATURE = ("<!--Sce-Np-Trophy-Signature: " + "x" * 320 + "-->\n").encode("ascii")
REQUIRED_FIELDS = {"title", "desc", "points", "isnew", "fbid"}


def _catalog_key(library: bytes) -> bytes:
    """Extract the supported library's resource-stream key from its ELF segment."""
    if len(library) < 52 or library[:7] != b"\x7fELF\x01\x01\x01":
        raise ValueError("Unsupported original library ELF format")
    phoff = struct.unpack_from("<I", library, 28)[0]
    phsize, phcount = struct.unpack_from("<HH", library, 42)
    if phsize != 32 or not 0 < phcount < 128:
        raise ValueError("Unsupported original library program headers")
    if phoff + phsize * phcount > len(library):
        raise ValueError("Truncated original library program headers")
    for index in range(phcount):
        kind, offset, address, _, length, _, _, _ = struct.unpack_from(
            "<8I", library, phoff + index * phsize
        )
        if kind == 1 and address <= CATALOG_KEY_ADDRESS:
            start = offset + CATALOG_KEY_ADDRESS - address
            if CATALOG_KEY_ADDRESS + 32 <= address + length:
                if start + 32 > len(library):
                    raise ValueError("Truncated original achievement stream key")
                return library[start : start + 32]
    raise ValueError("Original achievement stream key is missing")


class _CatalogReader:
    """Read the game's per-read XOR resource stream, with strict bounds checks."""

    def __init__(self, data: bytes, key: bytes):
        if not data.startswith(b":) ") or len(key) != 32:
            raise ValueError("Unsupported original achievement stream")
        self.data = data
        self.key = key
        self.position = 3

    def read(self, size: int) -> bytes:
        if not 0 <= size <= len(self.data) - self.position:
            raise ValueError("Truncated original achievement stream")
        value = bytes(
            self.data[self.position + index] ^ self.key[(size + index) & 31]
            for index in range(size)
        )
        self.position += size
        return value

    def number(self) -> int:
        return struct.unpack("<I", self.read(4))[0]

    def string(self) -> str:
        return self.read(self.number()).decode("utf-8")


def read_achievement_catalog(data: bytes, library: bytes) -> list[dict[str, str]]:
    """Decode the complete flattened original catalog in its original order."""
    reader = _CatalogReader(data, _catalog_key(library))
    record_count = reader.number()
    if not 1 <= record_count <= 10000:
        raise ValueError("Invalid original achievement record count")
    achievements = []
    identifiers = set()
    current = None
    for index in range(record_count):
        key, value, depth = reader.string(), reader.string(), reader.read(1)[0]
        if index == 0:
            if (key, value, depth) != ("achievements", "", 0):
                raise ValueError("Invalid original achievement catalog root")
        elif depth == 1 and key.isascii() and key.isdecimal() and not value:
            if key in identifiers:
                raise ValueError(f"Duplicate original achievement identifier: {key}")
            identifiers.add(key)
            current = {"id": key}
            achievements.append(current)
        elif depth == 2 and current is not None and key in REQUIRED_FIELDS:
            if key in current:
                raise ValueError(f"Duplicate achievement field: {current['id']}/{key}")
            current[key] = value
        else:
            raise ValueError(f"Unsupported original achievement record at {index}")
    if reader.position != len(data):
        raise ValueError("Trailing data in original achievement catalog")
    if len(achievements) != EXPECTED_COUNT:
        raise ValueError(
            f"Expected all {EXPECTED_COUNT} original achievements; got {len(achievements)}"
        )
    if math.ceil(len(achievements) / SET_CAPACITY) != EXPECTED_SET_COUNT:
        raise ValueError("Too many original achievements for the packaged trophy sets")
    for achievement in achievements:
        if set(achievement) != REQUIRED_FIELDS | {"id"}:
            raise ValueError(f"Incomplete original achievement: {achievement['id']}")
        if (
            not achievement["title"].strip()
            or not achievement["desc"].strip()
            or not achievement["points"].isdecimal()
            or achievement["isnew"] not in {"0", "1"}
            or not achievement["fbid"].isdecimal()
        ):
            raise ValueError(f"Invalid original achievement: {achievement['id']}")
        for field, limit in (("title", 127), ("desc", 1023)):
            text = achievement[field]
            if len(text.encode("utf-8")) > limit or any(
                ord(character) < 32 and character not in "\t\n\r" for character in text
            ):
                raise ValueError(f"Invalid trophy {field}: {achievement['id']}")
    return achievements


def _configuration(
    achievements: list[dict[str, str]], set_number: int, *, configuration_only: bool
) -> bytes:
    root = ET.Element("trophyconf", {"version": "1.1", "platform": "psp2", "policy": "large"})
    for name, text in (
        ("npcommid", f"{COMMUNICATION_ID}_{set_number:02d}"),
        ("trophyset-version", TROPHY_SET_VERSION),
    ):
        ET.SubElement(root, name).text = text
    ET.SubElement(root, "parental-level", {"license-area": "default"}).text = "0"
    if not configuration_only:
        ET.SubElement(root, "title-name").text = (
            "Plague Inc." if set_number == 0 else "Plague Inc. (2)"
        )
        ET.SubElement(root, "title-detail").text = (
            f"Original game achievements ({set_number + 1}/{EXPECTED_SET_COUNT})."
        )
    for trophy_id, achievement in enumerate(achievements):
        # Original scores use different scales across expansions. Use the same
        # grade for every original achievement instead of inventing new tiers.
        trophy = ET.SubElement(
            root,
            "trophy",
            {"id": f"{trophy_id:03d}", "hidden": "no", "ttype": "B", "pid": "-1"},
        )
        if not configuration_only:
            ET.SubElement(trophy, "name").text = achievement["title"]
            ET.SubElement(trophy, "detail").text = achievement["desc"]
    ET.indent(root, space="  ")
    # Both the configuration manifest and the default localized metadata need
    # the literal development marker used by working NoTrpDrm Vita packs.
    return SFM_SIGNATURE + ET.tostring(root, encoding="utf-8") + b"\n"


def pack_trophy_archive(files: list[tuple[str, bytes]]) -> bytes:
    """Serialize a Vita version-2 TRP with aligned payloads and its SHA-1 digest."""
    # Reserve the header and table, then append each file at an aligned offset.
    archive = bytearray(64 + 64 * len(files))
    names = set()
    for index, (name, content) in enumerate(files):
        encoded = name.encode("ascii")
        if not encoded or len(encoded) >= 32 or name in names or "/" in name:
            raise ValueError(f"Invalid trophy archive filename: {name}")
        names.add(name)
        archive.extend(bytes(-len(archive) % 16))
        struct.pack_into(
            ">32sQQI12s", archive, 64 + index * 64, encoded, len(archive), len(content), 0, b""
        )
        archive.extend(content)
    archive.extend(bytes(-len(archive) % 16))
    struct.pack_into(
        ">IIQIII20s16s", archive, 0, 0xDCA24D00, 2, len(archive), len(files), 64, 0, b"", b""
    )
    archive[0x1C:0x30] = hashlib.sha1(archive).digest()
    return bytes(archive)


def validate_trophy_archive(
    archive: bytes, achievements: list[dict[str, str]], set_number: int
) -> None:
    """Validate container ranges, XML mapping and all PNG sizes before writing."""
    from PIL import Image

    if len(archive) < 64:
        raise ValueError("Truncated trophy archive")
    magic, version, length, count, entry_size, flags, digest, reserved = struct.unpack_from(
        ">IIQIII20s16s", archive
    )
    if (magic, version, length, count, entry_size, flags) != (
        0xDCA24D00,
        2,
        len(archive),
        len(achievements) + 3,
        64,
        0,
    ) or any(reserved):
        raise ValueError("Invalid trophy archive header")
    unsigned = bytearray(archive)
    unsigned[0x1C:0x30] = bytes(20)
    if hashlib.sha1(unsigned).digest() != digest:
        raise ValueError("Invalid trophy archive digest")
    expected_names = ["TROPCONF.SFM", "TROP.SFM", "ICON0.PNG"] + [
        f"TROP{index:03d}.PNG" for index in range(len(achievements))
    ]
    files = {}
    cursor = 64 + count * entry_size
    if cursor > len(archive):
        raise ValueError("Truncated trophy archive entries")
    for index, expected_name in enumerate(expected_names):
        name, offset, size, entry_flags, entry_reserved = struct.unpack_from(
            ">32sQQI12s", archive, 64 + index * 64
        )
        if (
            name != expected_name.encode("ascii").ljust(32, b"\0")
            or offset != ((cursor + 15) & ~15)
            or any(archive[cursor:offset])
            or size == 0
            or offset + size > len(archive)
            or entry_flags
            or any(entry_reserved)
        ):
            raise ValueError(f"Invalid trophy archive entry: {expected_name}")
        files[expected_name] = archive[offset : offset + size]
        cursor = offset + size
    if len(archive) != ((cursor + 15) & ~15) or any(archive[cursor:]):
        raise ValueError("Invalid final trophy archive padding")
    for name in ("TROPCONF.SFM", "TROP.SFM"):
        if not files[name].startswith(SFM_SIGNATURE):
            raise ValueError(f"Invalid Vita trophy development signature: {name}")
        try:
            root = ET.fromstring(files[name])
        except ET.ParseError as error:
            raise ValueError(f"Invalid trophy metadata XML: {name}") from error
        if (
            root.tag != "trophyconf"
            or root.attrib != {"version": "1.1", "platform": "psp2", "policy": "large"}
            or root.findtext("npcommid") != f"{COMMUNICATION_ID}_{set_number:02d}"
            or root.findtext("trophyset-version") != TROPHY_SET_VERSION
            or root.findtext("parental-level") != "0"
            or root.find("parental-level").attrib != {"license-area": "default"}
        ):
            raise ValueError(f"Invalid Vita trophy configuration: {name}")
        configuration_only = name == "TROPCONF.SFM"
        if configuration_only:
            if root.find("title-name") is not None or root.find("title-detail") is not None:
                raise ValueError("Trophy text belongs in localized TROP.SFM")
        elif not root.findtext("title-name") or not root.findtext("title-detail"):
            raise ValueError("Missing localized trophy set metadata")
        trophies = root.findall("trophy")
        if len(trophies) != len(achievements) or not 0 < len(trophies) <= SET_CAPACITY:
            raise ValueError("Invalid trophy configuration count")
        for index, (trophy, achievement) in enumerate(zip(trophies, achievements)):
            if trophy.attrib != {"id": f"{index:03d}", "hidden": "no", "ttype": "B", "pid": "-1"}:
                raise ValueError(f"Invalid original trophy mapping: {achievement['id']}")
            if configuration_only:
                if len(trophy) or (trophy.text is not None and trophy.text.strip()):
                    raise ValueError("Trophy configuration manifest must omit localized text")
            elif (
                trophy.findtext("name") != achievement["title"]
                or trophy.findtext("detail") != achievement["desc"]
            ):
                raise ValueError(f"Invalid original trophy text: {achievement['id']}")
    for name in expected_names[2:]:
        size = (320, 176) if name == "ICON0.PNG" else (240, 240)
        data = files[name]
        if data[:16] != b"\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR" or struct.unpack(
            ">IIBBBBB", data[16:29]
        ) != (*size, 8, 6, 0, 0, 0):
            raise ValueError(f"Invalid trophy PNG layout: {name}")
        with Image.open(BytesIO(data)) as image:
            image.load()
            if image.size != size or image.mode != "RGBA":
                raise ValueError(f"Invalid trophy image: {name}")


def _artwork(
    background_data: bytes, app_icon_data: bytes, original_star: bytes
) -> tuple[bytes, bytes]:
    try:
        from PIL import Image, ImageOps
    except ImportError as error:
        raise RuntimeError("Pillow is missing; install the Python build requirements") from error

    def png(image) -> bytes:
        buffer = BytesIO()
        image.convert("RGBA").save(buffer, format="PNG", compress_level=9)
        return buffer.getvalue()

    background = decode_zomg(background_data, (960, 640))
    with Image.open(BytesIO(app_icon_data)) as image:
        if image.size != (192, 192):
            raise ValueError("Unsupported original app icon size")
        app_icon = image.convert("RGBA")
    # The achievement sprite uses an old WebP alpha format that current libwebp
    # cannot decode. Its ordinary VP8 color plane is compatible and already has
    # a black background, so use it on an opaque black trophy icon.
    if (
        original_star[:4] != b"RIFF"
        or original_star[8:12] != b"WEBP"
        or struct.unpack_from("<I", original_star, 4)[0] + 8 != len(original_star)
    ):
        raise ValueError("Unsupported original achievement star format")
    cursor = 12
    color_plane = None
    while cursor + 8 <= len(original_star):
        kind, length = struct.unpack_from("<4sI", original_star, cursor)
        end = cursor + 8 + length
        if end > len(original_star):
            raise ValueError("Truncated original achievement image chunk")
        if kind == b"VP8 ":
            if color_plane is not None:
                raise ValueError("Duplicate original achievement image color plane")
            color_plane = original_star[cursor:end]
        cursor = end + (length & 1)
    if color_plane is None or cursor != len(original_star):
        raise ValueError("Incomplete original achievement image")
    compatible_star = b"RIFF" + struct.pack("<I", 4 + len(color_plane)) + b"WEBP" + color_plane
    with Image.open(BytesIO(compatible_star)) as image:
        if image.size != (61, 59) or image.mode != "RGB":
            raise ValueError("Unsupported original achievement star color plane")
        star = image.convert("RGBA")

    banner = ImageOps.fit(background, (320, 176), Image.Resampling.LANCZOS)
    app_icon = app_icon.resize((160, 160), Image.Resampling.LANCZOS)
    banner.alpha_composite(app_icon, (80, 8))

    icon = Image.new("RGBA", (240, 240), (0, 0, 0, 255))
    star = star.resize((180, round(180 * star.height / star.width)), Image.Resampling.LANCZOS)
    icon.alpha_composite(star, ((240 - star.width) // 2, (240 - star.height) // 2))
    return png(banner), png(icon)


def generate_trophy_assets(apk: Path, output: Path) -> None:
    """Write trophy_catalog.h and both trophy packs."""
    if re.fullmatch(r"[A-Z]{4}[0-9]{5}", COMMUNICATION_ID) is None:
        raise ValueError("Native Vita trophy IDs require four uppercase letters and five digits")

    source_names = [
        CATALOG_SOURCE,
        ENGLISH_SOURCE,
        LIBRARY_SOURCE,
        STAR_SOURCE,
        ICON_SOURCE,
        BACKGROUND_SOURCE,
    ]
    with zipfile.ZipFile(apk) as original:
        sources = {name: original.read(name) for name in source_names}
    # The supported original stores its English source text in the catalog;
    # en.strings intentionally contains no translation overrides.
    if sources[ENGLISH_SOURCE]:
        raise ValueError("Unsupported original English translation overrides")
    achievements = read_achievement_catalog(sources[CATALOG_SOURCE], sources[LIBRARY_SOURCE])
    banner, icon = _artwork(sources[BACKGROUND_SOURCE], sources[ICON_SOURCE], sources[STAR_SOURCE])

    generated = {}
    for set_number in range(EXPECTED_SET_COUNT):
        subset = achievements[set_number * SET_CAPACITY : (set_number + 1) * SET_CAPACITY]
        files = [
            ("TROPCONF.SFM", _configuration(subset, set_number, configuration_only=True)),
            ("TROP.SFM", _configuration(subset, set_number, configuration_only=False)),
            ("ICON0.PNG", banner),
        ]
        files.extend((f"TROP{index:03d}.PNG", icon) for index in range(len(subset)))
        archive = pack_trophy_archive(files)
        validate_trophy_archive(archive, subset, set_number)
        generated[f"trophy/{COMMUNICATION_ID}_{set_number:02d}/TROPHY.TRP"] = archive

    entries = "\n".join(f'    {{"{achievement["id"]}"}},' for achievement in achievements)
    header = f"""/* Generated from the user's supported APK; do not commit. */
#ifndef PORT_GENERATED_TROPHY_CATALOG_H
#define PORT_GENERATED_TROPHY_CATALOG_H

#define PORT_TROPHY_COUNT {len(achievements)}
#define PORT_TROPHY_SET_COUNT {EXPECTED_SET_COUNT}
#define PORT_TROPHY_SET_CAPACITY {SET_CAPACITY}
#define PORT_TROPHY_COMMUNICATION_ID "{COMMUNICATION_ID}"

#ifdef PORT_TROPHY_INCLUDE_CATALOG
typedef struct {{
    const char *game_id;
}} PortTrophyCatalogEntry;

static const PortTrophyCatalogEntry port_trophy_catalog[PORT_TROPHY_COUNT] = {{
{entries}
}};
#endif

#endif
"""
    generated["trophy_catalog.h"] = header.encode("utf-8")
    for name, data in generated.items():
        destination = output / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
