#!/usr/bin/env python3
"""Two-pass offline verifier for Hermes and legacy RNPS HomeUI top-nav patches.

Reads original NPXS40002 dumps, matches C++ HomeUiPatchProfiles, simulates the
runtime Hermes patches, and checks product invariants twice for consistency.

Fix A — game-close crash:
  * Fps body remains stock showFps prefix (not OnionHEN)
  * ApplicationErrorEventTrigger is the OnionHEN button host (full 77-byte body)
  * top-nav order is [Search, ApplicationErrorEventTrigger, Settings, Profile]

Fix B — focused icon (HBC side):
  * custom icon string is /system_ex/vsh_asset/onionhen.png
  * runtime SetIconSource→invertedIcon mirror keys off this path
"""

from __future__ import annotations

import hashlib
import re
import struct
import subprocess
import sys
from functools import lru_cache
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
CPP = (REPO / "source/shellui/src/homeui_top_nav_patch.cpp").read_text()
INC = (REPO / "source/shellui/src/homeui_top_nav_profiles.inc").read_text()
HERMES = bytes([0xC6, 0x1F, 0xBC, 0x03, 0xC1, 0x03, 0x19, 0x1F])
LEGACY = bytes([0xE5, 0xD1, 0x0B, 0xFB])
PLAIN_JS_PREFIX = b"/*! For license information"

# Every firmware in Sony Dumps that contains a decrypted NPXS40002 dump.
DUMP_ROOT = Path("/Users/chenpy/Projects/Person/ps5-kylin/Sony Dumps")
DUMPS = [
    # (firmware label, directory/archive, archive member or None)
    ("2.30", "2.3厚机.rar", "NPXS40002.bin"),
    ("2.50", "2.5厚机.rar", "NPXS40002.bin"),
    ("3.00", "3.0厚机.rar", "NPXS40002.bin"),
    ("3.10", "03.10厚机.rar", "NPXS40002.bin"),
    ("3.20", "03.20厚机.rar", "NPXS40002.bin"),
    ("3.21", "03.21厚机.rar", "NPXS40002.bin"),
    ("4.00", "4.0厚机.rar", "4.0厚机/NPXS40002.bin"),
    ("4.02", "4.02厚机.rar", "4.02厚机/NPXS40002.bin"),
    ("4.03", "4.03", None),
    ("4.50", "4.50", None),
    ("4.51", "4.51", None),
    ("5.00", "5.00厚机", None),
    ("5.02", "5.02厚机", None),
    ("5.10", "5.1", None),
    ("5.50", "5.50厚机", None),
    ("6.00", "6.0", None),
    ("6.02", "6.02", None),
    ("6.50", "6.50厚机", None),
    ("7.00", "7.00厚机", None),
    ("7.01", "7.01厚机", None),
    ("7.01.01", "7.01.01厚机", None),
    ("7.20", "7.20厚机", None),
    ("7.40", "7.4", None),
    ("7.60", "7.60厚机", None),
    ("7.61", "7.61", None),
    ("8.00", "8.0", None),
    ("8.20", "8.2厚机", None),
    ("8.20.02", "fw8.20.02", None),
    ("8.40", "8.4", None),
    ("8.60", "fw8.6", None),
    ("9.00", "9.00", None),
    ("9.40", "fw9.4", None),
    ("9.60", "fw9.6", None),
    ("10.00", "fw10.0", None),
    ("10.01", "10.01", None),
    ("10.20", "10.2", None),
    ("10.40", "10.4", None),
    ("10.60", "10.6", None),
    ("11.00", "11.0", None),
    ("11.20", "11.2", None),
    ("11.40", "11.4", None),
    ("11.60", "11.6", None),
    ("12.00", "12.0", None),
    ("12.02", "12.02", None),
    ("12.20", "12.2", None),
    ("12.40", "12.4", None),
    ("12.60", "12.6", None),
    ("12.70", "12.7", None),
]


@lru_cache(maxsize=None)
def read_dump_app(source: str, archive_member: str | None, app_id: str) -> bytes:
    path = DUMP_ROOT / source
    if archive_member is None:
        return (path / f"{app_id}.bin").read_bytes()
    result = subprocess.run(
        ["unar", "-q", "-o", "-", str(path), archive_member],
        check=True,
        stdout=subprocess.PIPE,
    )
    return result.stdout


def extract_array(name: str) -> bytes:
    """Read a firmware-independent constant from the patch .cpp."""
    m = re.search(
        rf"static const unsigned char {re.escape(name)}\[\] = \{{([^}}]+)\}};",
        CPP,
        re.S,
    )
    if m:
        body = m.group(1)
        hx = re.findall(r"0x([0-9a-fA-F]{2})", body)
        if hx:
            return bytes(int(x, 16) for x in hx)
        chars = re.findall(r"'((?:\\.|[^'\\]))'", body)
        out = bytearray()
        escapes = {"n": 10, "r": 13, "t": 9, "0": 0, "\\": 92, "'": 39}
        for c in chars:
            if c.startswith("\\") and len(c) == 2:
                out.append(escapes.get(c[1], ord(c[1])))
            else:
                out.append(ord(c))
        return bytes(out)

    literal = re.search(
        rf"static const unsigned char {re.escape(name)}\[\]\s*=\s*"
        r"((?:\"(?:\\.|[^\"\\])*\"\s*)+);",
        CPP,
        re.S,
    )
    if not literal:
        raise SystemExit(f"missing C++ array {name}")
    chunks = re.findall(r'\"((?:\\.|[^\"\\])*)\"', literal.group(1))
    return b"".join(
        bytes(chunk, "utf-8").decode("unicode_escape").encode("latin1")
        for chunk in chunks
    )


def extract_c_string(name: str) -> bytes:
    m = re.search(
        rf"static const char {re.escape(name)}\[\] =\s*"
        r'"((?:\\.|[^"\\])*)";',
        CPP,
        re.S,
    )
    if not m:
        raise SystemExit(f"missing C++ string {name}")
    return bytes(m.group(1), "utf-8").decode("unicode_escape").encode("latin1")


def _hex_bytes(text: str) -> bytes:
    return bytes(int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", text))


def load_profiles() -> list[dict]:
    """
    Parse kHomeUiPatchProfiles out of homeui_top_nav_profiles.inc.

    The .inc is the single source of truth for per-firmware data; this script
    used to keep a hand-maintained copy of the offsets and hashes, which meant
    adding a firmware required editing both.
    """
    profiles = []
    for entry in re.findall(r"\n    \{\n(.*?)\n    \},", INC, re.S):
        offs_text = re.search(
            r"/\* offsets \*/ \{(.*?)\n        \}", entry, re.S
        ).group(1)
        offsets = {
            m.group(1): int(m.group(2), 16)
            for m in re.finditer(r"/\* (\w+)\s+\*/ (0x[0-9a-f]+),", offs_text)
        }
        bytes_text = re.search(
            r"/\* bytes \*/ \{(.*)\n        \},", entry, re.S
        ).group(1)
        fields = {
            m.group(1): _hex_bytes(m.group(2))
            for m in re.finditer(
                r"/\* (\w+) \*/ \{(.*?)\n            \}", bytes_text, re.S
            )
        }
        has_legacy = (
            re.search(r"/\* has_legacy_button_body \*/ (true|false)", bytes_text)
            .group(1)
            == "true"
        )
        requires_image_source = (
            re.search(
                r"/\* requires_image_source_object \*/ (true|false)",
                bytes_text,
            ).group(1)
            == "true"
        )
        profiles.append({
            "name": re.search(r'/\* name\s+\*/ "([^"]+)"', entry).group(1),
            "file_length": int(
                re.search(r"/\* file_length\s+\*/ (0x[0-9a-f]+)", entry).group(1), 16
            ),
            "source_hash": _hex_bytes(
                re.search(
                    r"/\* source_hash\s+\*/ \{(.*?)\n        \}", entry, re.S
                ).group(1)
            ),
            "offsets": offsets,
            "bytes": fields,
            "legacy_button_body": (
                fields["legacy_onion_hen_button_body"] if has_legacy else None
            ),
            "requires_image_source": requires_image_source,
        })
    if not profiles:
        raise SystemExit("no profiles parsed from homeui_top_nav_profiles.inc")
    return profiles


def load_source_strategies() -> dict[str, dict]:
    strategies = {}
    pattern = re.compile(
        r"static const SourcePatchStrategy (\w+) = \{\s*"
        r"(\w+),\s*(\w+),\s*sizeof\(\w+\)(?:\s*-\s*1)?,\s*"
        r"(\w+),\s*(\w+),\s*sizeof\(\w+\) - 1,\s*"
        r"sizeof\(\w+\) - 1,\s*\};",
        re.S,
    )
    for name, old_alias, new_alias, old_source, new_source in pattern.findall(CPP):
        old_source_bytes = extract_c_string(old_source)
        new_source_prefix = extract_c_string(new_source)
        strategies[name] = {
            "old_alias": extract_array(old_alias),
            "new_alias": extract_array(new_alias),
            "old_source": old_source_bytes,
            "new_source_prefix": new_source_prefix,
            "app_error_source_size": len(old_source_bytes),
            "new_source": new_source_prefix.ljust(len(old_source_bytes), b" "),
        }
    if not strategies:
        raise SystemExit("no source HomeUI strategies parsed")
    return strategies


def load_source_profiles(strategies: dict[str, dict]) -> list[dict]:
    table = re.search(
        r"kSourceHomeUiProfiles\[\] = \{(.*?)\n\};", CPP, re.S
    )
    if not table:
        raise SystemExit("source HomeUI profile table not found")

    profiles = []
    for entry in re.findall(r"\n    \{\n(.*?)\n    \},", table.group(1), re.S):
        offsets_text = re.search(
            r"/\* offsets \*/ \{(.*?)\n        \}", entry, re.S
        ).group(1)
        offsets = {
            match.group(1): int(match.group(2), 16)
            for match in re.finditer(
                r"/\* (\w+) \*/ (0x[0-9a-fA-F]+),", offsets_text
            )
        }
        strategy_name = re.search(
            r"/\* strategy \*/ &(\w+)", entry
        ).group(1)
        strategy = strategies[strategy_name]
        profiles.append(
            {
                "name": re.search(r'/\* name \*/ "([^"]+)"', entry).group(1),
                "kind": re.search(
                    r"/\* kind \*/ SourceBundleKind::(\w+)", entry
                ).group(1),
                "strategy_name": strategy_name,
                "payload_size": int(
                    re.search(
                        r"/\* payload_size \*/ (0x[0-9a-fA-F]+)", entry
                    ).group(1),
                    16,
                ),
                **{f"{name}_offset": value for name, value in offsets.items()},
                **strategy,
            }
        )
    if not profiles:
        raise SystemExit("no source HomeUI profiles parsed")
    return profiles


PROFILES = load_profiles()
for _p in PROFILES:
    _b = _p["bytes"]
    assert (
        len(_b["stock_app_error_body"])
        == len(_b["onion_hen_button_body"])
        == len(_b["old_fps_body_prefix"])
        == 77
    ), _p["name"]

_profile_by_name = {_p["name"]: _p for _p in PROFILES}
_p900 = _profile_by_name["9.00 NPXS40002 HomeUI"]
_p116 = _profile_by_name["11.4/11.6 NPXS40002 HomeUI"]
assert not _p900["requires_image_source"], (
    "9.00 must use the 11.6 single-function AppError shape"
)
assert _p900["offsets"]["app_error_props_helper_body"] == 0
assert (
    _p900["requires_image_source"] == _p116["requires_image_source"]
)

NEW_ICON_URI = extract_array("kNewCustomIconUri")
OLD_ICON_URI = extract_array("kOldCustomIconUri")
NEW_LINK = extract_array("kNewTopNavLinkUri")
OLD_LINK = extract_array("kOldTopNavLinkUri")
NEW_TITLE = extract_array("kNewCustomTitleValue")
STOCK_DL = extract_array("kStockDownloadErrorString")
BLANK_LINK = extract_array("kLegacyBlankTopNavLinkUri")
PADDED_LINK = extract_array("kLegacyPaddedTopNavLinkUri")

LEGACY_OLD_ORDER = extract_array("kLegacyOldIconOrder")
LEGACY_NEW_ORDER = extract_array("kLegacyNewIconOrder")
LEGACY2X_OLD_ORDER = extract_array("kLegacy2xOldIconOrder")
LEGACY2X_NEW_ORDER = extract_array("kLegacy2xNewIconOrder")
SOURCE_STRATEGIES = load_source_strategies()
SOURCE_PROFILES = load_source_profiles(SOURCE_STRATEGIES)
LEGACY_PROFILES = tuple(
    profile for profile in SOURCE_PROFILES if profile["kind"] == "LegacyRnps"
)
PLAIN_JS_PROFILE = next(
    profile for profile in SOURCE_PROFILES if profile["kind"] == "PlainJs"
)
IMAGE_SOURCE_BUTTON_BODY = extract_array("kImageSourceOnionHenButtonBody")
STOCK_APP_ERROR_ON_PRESS_BODY = extract_array("kStockAppErrorOnPressBody")
IMAGE_SOURCE_PROPS_HELPER_BODY = extract_array(
    "kImageSourceIconPropsHelperBody"
)
NINE_HISTORICAL_HELPER_OFFSET = 0x149EE7

assert NEW_ICON_URI == b"/system_ex/vsh_asset/onionhen.png", NEW_ICON_URI
assert NEW_LINK == b"OnionHEN?NavUI=1", NEW_LINK
assert len(LEGACY_OLD_ORDER) == len(LEGACY_NEW_ORDER) == 37
for _source_profile in SOURCE_PROFILES:
    assert len(_source_profile["old_alias"]) == len(_source_profile["new_alias"])
    assert (
        len(_source_profile["old_source"])
        == len(_source_profile["new_source"])
        == _source_profile["app_error_source_size"]
    )
assert len(IMAGE_SOURCE_BUTTON_BODY) == 77
assert len(STOCK_APP_ERROR_ON_PRESS_BODY) == 76
assert len(IMAGE_SOURCE_PROPS_HELPER_BODY) == 76


def source_icon_orders(profile: dict) -> tuple[bytes, bytes]:
    if profile["strategy_name"] == "kLegacy2xSourceStrategy":
        return LEGACY2X_OLD_ORDER, LEGACY2X_NEW_ORDER
    return LEGACY_OLD_ORDER, LEGACY_NEW_ORDER


def locate_hbc(data: bytes) -> bytearray | None:
    if data.startswith(HERMES):
        return bytearray(data)
    if data[:8] == b"RNPSHEDR":
        off = struct.unpack_from("<I", data, 0x1C)[0]
        if off == 0 or off >= len(data):
            off = 0xB20
        if data[off : off + 8] == HERMES:
            return bytearray(data[off:])
    i = data.find(HERMES)
    return bytearray(data[i:]) if i >= 0 else None


def locate_legacy(data: bytes) -> bytearray | None:
    if data.startswith(LEGACY):
        return bytearray(data)
    if data[:8] == b"RNPSHEDR":
        off = struct.unpack_from("<I", data, 0x1C)[0]
        if off == 0 or off >= len(data):
            off = 0xB20
        if data[off : off + 4] == LEGACY:
            return bytearray(data[off:])
    return None


def locate_plain_js(data: bytes) -> bytearray | None:
    if data.startswith(PLAIN_JS_PREFIX):
        return bytearray(data)
    if data[:8] == b"RNPSHEDR":
        off = struct.unpack_from("<I", data, 0x1C)[0]
        if off == 0 or off >= len(data):
            off = 0xB20
        if data[off : off + len(PLAIN_JS_PREFIX)] == PLAIN_JS_PREFIX:
            return bytearray(data[off:])
        if data[0xB20 : 0xB20 + len(PLAIN_JS_PREFIX)] == PLAIN_JS_PREFIX:
            return bytearray(data[0xB20:])
    return None


def match_profile(hbc: bytes) -> dict | None:
    fl = struct.unpack_from("<I", hbc, 0x20)[0]
    sh = bytes(hbc[0x0C:0x20])
    ver = struct.unpack_from("<I", hbc, 0x08)[0]
    for p in PROFILES:
        if not (p["file_length"] == fl and p["source_hash"] == sh and ver == 89):
            continue
        o = p["offsets"]
        if (
            bytes(hbc[o["title_id"] : o["title_id"] + 9]) == b"NPXS40002"
            and bytes(
                hbc[
                    o["app_error_event_trigger"] : o["app_error_event_trigger"]
                    + 28
                ]
            )
            == b"ApplicationErrorEventTrigger"
            and bytes(hbc[o["navigate_to_home"] : o["navigate_to_home"] + 23])
            == b"pshomeui:navigateToHome"
        ):
            return p
    return None


def patch_at(hbc, name, off, expected_opts, replacement, notes) -> bool:
    cur = bytes(hbc[off : off + len(replacement)])
    if cur == replacement:
        notes.append(f"{name}:already")
        return True
    for e in expected_opts:
        if e is not None and cur == e:
            hbc[off : off + len(replacement)] = replacement
            notes.append(f"{name}:applied")
            return True
    notes.append(f"{name}:MISMATCH head={cur[:12].hex()}")
    return False


def apply_patch(hbc: bytearray, p: dict) -> list[str]:
    b = p["bytes"]
    o = p["offsets"]
    notes: list[str] = []
    ok = True
    ok &= patch_at(
        hbc,
        "icon_order",
        o["home_icon_order"],
        [
            b["old_icon_order"],
            b["legacy_fps_slot_icon_order"],
            b["legacy_aliased_icon_order"],
            b["app_error_icon_order"],
        ],
        b["app_error_icon_order"],
        notes,
    )
    ok &= patch_at(
        hbc,
        "fps_factory",
        o["fps_factory"],
        [b["original_fps_factory"], b["legacy_aliased_fps_factory"]],
        b["original_fps_factory"],
        notes,
    )
    fps_alts = [b["onion_hen_button_body"], b["old_fps_body_prefix"]]
    if p["legacy_button_body"] is not None:
        fps_alts.append(p["legacy_button_body"])
    ok &= patch_at(
        hbc,
        "fps_repair",
        o["fps_body"],
        fps_alts,
        b["old_fps_body_prefix"],
        notes,
    )
    target_app_body = (
        IMAGE_SOURCE_BUTTON_BODY
        if p["requires_image_source"]
        else b["onion_hen_button_body"]
    )
    app_alts = [
        b["stock_app_error_body"],
        b["onion_hen_button_body"],
        target_app_body,
    ]
    if p["legacy_button_body"] is not None:
        app_alts.append(p["legacy_button_body"])
    ok &= patch_at(
        hbc,
        "app_error_onion",
        o["app_error_body"],
        app_alts,
        target_app_body,
        notes,
    )
    if p["requires_image_source"]:
        ok &= patch_at(
            hbc,
            "app_error_image_source_helper",
            o["app_error_props_helper_body"],
            [STOCK_APP_ERROR_ON_PRESS_BODY, IMAGE_SOURCE_PROPS_HELPER_BODY],
            IMAGE_SOURCE_PROPS_HELPER_BODY,
            notes,
        )
    ok &= patch_at(
        hbc,
        "icon_uri",
        o["custom_icon_uri"],
        [OLD_ICON_URI, NEW_ICON_URI],
        NEW_ICON_URI,
        notes,
    )
    ok &= patch_at(
        hbc,
        "link_uri",
        o["top_nav_link_uri"],
        [OLD_LINK, NEW_LINK, BLANK_LINK, PADDED_LINK],
        NEW_LINK,
        notes,
    )
    ok &= patch_at(
        hbc,
        "icon_val",
        o["custom_icon_value"],
        [b["old_custom_icon_value"], b["new_custom_icon_value"]],
        b["new_custom_icon_value"],
        notes,
    )
    ok &= patch_at(
        hbc,
        "title_val",
        o["custom_title_value"],
        [b["old_custom_title_value"], NEW_TITLE],
        NEW_TITLE,
        notes,
    )
    ok &= patch_at(
        hbc,
        "download_error",
        o["download_error_string"],
        [STOCK_DL, b"OnionHEN?NavUI"],
        STOCK_DL,
        notes,
    )
    fl = p["file_length"]
    footer = fl - 20
    hbc[footer : footer + 20] = hashlib.sha1(bytes(hbc[:footer])).digest()
    if not ok:
        notes.append("APPLY_FAILED")
    return notes


def verify(hbc: bytes, p: dict, tag: str) -> list[str]:
    errs: list[str] = []
    b = p["bytes"]
    o = p["offsets"]

    order = bytes(hbc[o["home_icon_order"] : o["home_icon_order"] + 9])
    if order != b["app_error_icon_order"]:
        errs.append(f"{tag}: order not AppError-slot ({order.hex()})")

    fps = bytes(hbc[o["fps_body"] : o["fps_body"] + 77])
    app = bytes(hbc[o["app_error_body"] : o["app_error_body"] + 77])

    # Fix A — crash host must not be Fps
    if fps == b["onion_hen_button_body"] or (
        p["legacy_button_body"] is not None and fps == p["legacy_button_body"]
    ):
        errs.append(f"{tag}: FixA FAIL — Fps still OnionHEN (crash regression)")
    if fps != b["old_fps_body_prefix"]:
        errs.append(f"{tag}: FixA FAIL — Fps prefix not stock ({fps[:8].hex()})")
    target_app_body = (
        IMAGE_SOURCE_BUTTON_BODY
        if p["requires_image_source"]
        else b["onion_hen_button_body"]
    )
    if app != target_app_body:
        errs.append(
            f"{tag}: FixA FAIL — AppError not OnionHEN host ({app[:8].hex()})"
        )
    if app == b["stock_app_error_body"]:
        errs.append(f"{tag}: FixA FAIL — AppError still stock")
    if p["requires_image_source"]:
        helper_off = o["app_error_props_helper_body"]
        helper = bytes(hbc[helper_off : helper_off + 76])
        if helper != IMAGE_SOURCE_PROPS_HELPER_BODY:
            errs.append(f"{tag}: FixB FAIL — ImageSource props helper missing")
    elif p["name"] == "9.00 NPXS40002 HomeUI":
        helper = bytes(
            hbc[
                NINE_HISTORICAL_HELPER_OFFSET :
                NINE_HISTORICAL_HELPER_OFFSET + 76
            ]
        )
        if helper != STOCK_APP_ERROR_ON_PRESS_BODY:
            errs.append(f"{tag}: 9.00 Function #6298 was modified")
    factory = bytes(hbc[o["fps_factory"] : o["fps_factory"] + 5])
    if factory != b["original_fps_factory"]:
        errs.append(f"{tag}: Fps factory alias not repaired ({factory.hex()})")
    if o["fps_body"] - o["app_error_body"] != 153:
        errs.append(
            f"{tag}: AppError→Fps gap {o['fps_body'] - o['app_error_body']} != 153"
        )

    # Fix B — focus icon path present for invertedIcon mirror
    uri = bytes(hbc[o["custom_icon_uri"] : o["custom_icon_uri"] + 33])
    if uri != NEW_ICON_URI:
        errs.append(f"{tag}: FixB FAIL — icon uri not onionhen.png ({uri!r})")
    if NEW_ICON_URI not in bytes(hbc):
        errs.append(f"{tag}: FixB FAIL — onionhen.png absent from HBC")

    link = bytes(hbc[o["top_nav_link_uri"] : o["top_nav_link_uri"] + 16])
    if link != NEW_LINK:
        errs.append(f"{tag}: link not OnionHEN?NavUI=1 ({link!r})")

    de = bytes(hbc[o["download_error_string"] : o["download_error_string"] + 14])
    if de != STOCK_DL:
        errs.append(f"{tag}: download_error string broken ({de!r})")

    if bytes(hbc[o["title_id"] : o["title_id"] + 9]) != b"NPXS40002":
        errs.append(f"{tag}: NPXS40002 marker broken")

    fl = p["file_length"]
    footer = fl - 20
    if hashlib.sha1(bytes(hbc[:footer])).digest() != bytes(
        hbc[footer : footer + 20]
    ):
        errs.append(f"{tag}: footer SHA1 invalid")

    if app[:2] == bytes.fromhex("3204"):
        errs.append(f"{tag}: AppError still CreateEnvironment stock head")
    return errs


def precheck(hbc: bytes, p: dict) -> list[str]:
    errs: list[str] = []
    b = p["bytes"]
    o = p["offsets"]
    order = bytes(hbc[o["home_icon_order"] : o["home_icon_order"] + 9])
    if order not in (
        b["old_icon_order"],
        b["legacy_fps_slot_icon_order"],
        b["legacy_aliased_icon_order"],
        b["app_error_icon_order"],
    ):
        errs.append(f"pre: unexpected order {order.hex()}")
    app = bytes(hbc[o["app_error_body"] : o["app_error_body"] + 77])
    accepted_app_bodies = [b["stock_app_error_body"], b["onion_hen_button_body"]]
    if p["requires_image_source"]:
        accepted_app_bodies.append(IMAGE_SOURCE_BUTTON_BODY)
    if app not in accepted_app_bodies and not (
        p["legacy_button_body"] is not None and app == p["legacy_button_body"]
    ):
        errs.append(f"pre: AppError body unexpected ({app[:8].hex()})")
    if p["requires_image_source"]:
        helper_off = o["app_error_props_helper_body"]
        helper = bytes(hbc[helper_off : helper_off + 76])
        if helper not in (
            STOCK_APP_ERROR_ON_PRESS_BODY,
            IMAGE_SOURCE_PROPS_HELPER_BODY,
        ):
            errs.append(f"pre: AppError props helper unexpected ({helper[:8].hex()})")
    elif p["name"] == "9.00 NPXS40002 HomeUI":
        helper = bytes(
            hbc[
                NINE_HISTORICAL_HELPER_OFFSET :
                NINE_HISTORICAL_HELPER_OFFSET + 76
            ]
        )
        if helper != STOCK_APP_ERROR_ON_PRESS_BODY:
            errs.append("pre: 9.00 Function #6298 is not stock")
    fps = bytes(hbc[o["fps_body"] : o["fps_body"] + 77])
    if fps not in (b["old_fps_body_prefix"], b["onion_hen_button_body"]) and not (
        p["legacy_button_body"] is not None and fps == p["legacy_button_body"]
    ):
        errs.append(f"pre: Fps prefix unexpected ({fps[:8].hex()})")
    # Require virgin AppError/Fps/order match C++ for true original dumps.
    if (
        order == b["old_icon_order"]
        and app != b["stock_app_error_body"]
    ):
        errs.append("pre: virgin order but AppError not stock")
    # Strong check: stock body bytes equal C++ table
    if order == b["old_icon_order"]:
        if app != b["stock_app_error_body"]:
            errs.append("pre: stock dump AppError != C++ StockAppErrorBody")
        if fps != b["old_fps_body_prefix"]:
            errs.append("pre: stock dump Fps prefix != C++ OldFpsBodyPrefix")
    return errs


def match_legacy_profile(bundle: bytes) -> dict | None:
    for p in LEGACY_PROFILES:
        if len(bundle) != p["payload_size"]:
            continue
        markers = [
            (p["title_id_offset"], b"NPXS40002"),
            (p["navigate_to_home_offset"], b"pshomeui:navigateToHome"),
        ]
        if p["app_error_event_trigger_offset"]:
            markers.append(
                (
                    p["app_error_event_trigger_offset"],
                    b"ApplicationErrorEventTrigger",
                )
            )
        if all(
            bundle[offset : offset + len(marker)] == marker
            for offset, marker in markers
        ):
            return p
    return None


def precheck_legacy(bundle: bytes, p: dict) -> list[str]:
    errs: list[str] = []
    order_offset = p["icon_order_offset"]
    alias_offset = p["export_alias_offset"]
    source_offset = p["app_error_source_offset"]
    source_size = p["app_error_source_size"]
    old_order, new_order = source_icon_orders(p)
    order = bytes(bundle[order_offset : order_offset + len(old_order)])
    alias = bytes(bundle[alias_offset : alias_offset + len(p["old_alias"])])
    source = bytes(bundle[source_offset : source_offset + source_size])
    if order not in (old_order, new_order):
        errs.append(f"pre: legacy order unexpected ({order!r})")
    if alias not in (p["old_alias"], p["new_alias"]):
        errs.append(f"pre: legacy alias unexpected ({alias!r})")
    if source not in (p["old_source"], p["new_source"]):
        errs.append(f"pre: legacy AppError source unexpected ({source[:32]!r})")
    return errs


def apply_legacy(bundle: bytearray, p: dict) -> list[str]:
    notes: list[str] = []
    ok = True
    old_order, new_order = source_icon_orders(p)
    ok &= patch_at(
        bundle,
        "legacy_order",
        p["icon_order_offset"],
        [old_order, new_order],
        new_order,
        notes,
    )
    ok &= patch_at(
        bundle,
        "legacy_alias",
        p["export_alias_offset"],
        [p["old_alias"], p["new_alias"]],
        p["new_alias"],
        notes,
    )
    ok &= patch_at(
        bundle,
        "legacy_app_error",
        p["app_error_source_offset"],
        [p["old_source"], p["new_source"]],
        p["new_source"],
        notes,
    )
    if not ok:
        notes.append("APPLY_FAILED")
    return notes


def verify_legacy(
    bundle: bytes, p: dict, tag: str, original_fps_body: bytes
) -> list[str]:
    errs: list[str] = []
    order_offset = p["icon_order_offset"]
    alias_offset = p["export_alias_offset"]
    source_offset = p["app_error_source_offset"]
    source_size = p["app_error_source_size"]
    source_end = source_offset + source_size
    _, new_order = source_icon_orders(p)
    order = bytes(bundle[order_offset : order_offset + len(new_order)])
    alias = bytes(bundle[alias_offset : alias_offset + len(p["new_alias"])])
    source = bytes(bundle[source_offset:source_end])
    if order != new_order:
        errs.append(f"{tag}: legacy target icon order mismatch")
    if alias != p["new_alias"]:
        errs.append(f"{tag}: legacy App alias missing ({alias!r})")
    if source != p["new_source"]:
        errs.append(f"{tag}: legacy AppError source mismatch")
    if bytes(bundle[source_end:alias_offset]) != original_fps_body:
        errs.append(f"{tag}: legacy Fps implementation changed")
    for marker in (
        b"useInteractivePress",
        b"OnionHEN?NavUI=1",
        b'iconId:{uri:"/system_ex/vsh_asset/onionhen.png"}',
        b"onPress:e",
        b'title:""',
    ):
        if marker not in source:
            errs.append(f"{tag}: legacy source missing {marker!r}")
    if b"sendClientApplicationErrorEvent" in source:
        errs.append(f"{tag}: legacy AppError telemetry body still active")
    return errs


def match_plain_js_profile(bundle: bytes) -> dict | None:
    p = PLAIN_JS_PROFILE
    if len(bundle) != p["payload_size"] or not bundle.startswith(PLAIN_JS_PREFIX):
        return None
    markers = (
        (p["title_id_offset"], b"NPXS40002"),
        (p["app_error_event_trigger_offset"], b"ApplicationErrorEventTrigger"),
        (p["navigate_to_home_offset"], b"pshomeui:navigateToHome"),
    )
    if any(bundle[offset : offset + len(marker)] != marker for offset, marker in markers):
        return None
    return p


def precheck_plain_js(bundle: bytes, p: dict) -> list[str]:
    errs: list[str] = []
    order_offset = p["icon_order_offset"]
    alias_offset = p["export_alias_offset"]
    source_offset = p["app_error_source_offset"]
    source_size = p["app_error_source_size"]
    order = bytes(bundle[order_offset : order_offset + len(LEGACY_OLD_ORDER)])
    alias = bytes(bundle[alias_offset : alias_offset + len(p["old_alias"])])
    source = bytes(bundle[source_offset : source_offset + source_size])
    if order not in (LEGACY_OLD_ORDER, LEGACY_NEW_ORDER):
        errs.append(f"pre: plain-JS order unexpected ({order!r})")
    if alias not in (p["old_alias"], p["new_alias"]):
        errs.append(f"pre: plain-JS alias unexpected ({alias!r})")
    if source not in (p["old_source"], p["new_source"]):
        errs.append(f"pre: plain-JS AppError source unexpected ({source[:32]!r})")
    return errs


def apply_plain_js(bundle: bytearray, p: dict) -> list[str]:
    notes: list[str] = []
    ok = True
    ok &= patch_at(
        bundle,
        "plain_js_order",
        p["icon_order_offset"],
        [LEGACY_OLD_ORDER, LEGACY_NEW_ORDER],
        LEGACY_NEW_ORDER,
        notes,
    )
    ok &= patch_at(
        bundle,
        "plain_js_alias",
        p["export_alias_offset"],
        [p["old_alias"], p["new_alias"]],
        p["new_alias"],
        notes,
    )
    ok &= patch_at(
        bundle,
        "plain_js_app_error",
        p["app_error_source_offset"],
        [p["old_source"], p["new_source"]],
        p["new_source"],
        notes,
    )
    if not ok:
        notes.append("APPLY_FAILED")
    return notes


def verify_plain_js(
    bundle: bytes, p: dict, tag: str, original_fps_body: bytes
) -> list[str]:
    errs: list[str] = []
    order_offset = p["icon_order_offset"]
    alias_offset = p["export_alias_offset"]
    source_offset = p["app_error_source_offset"]
    source_size = p["app_error_source_size"]
    source_end = source_offset + source_size
    order = bytes(bundle[order_offset : order_offset + len(LEGACY_NEW_ORDER)])
    alias = bytes(bundle[alias_offset : alias_offset + len(p["new_alias"])])
    source = bytes(bundle[source_offset:source_end])
    if order != LEGACY_NEW_ORDER:
        errs.append(f"{tag}: plain-JS order not Search|App|Settings|Profile")
    if alias != p["new_alias"]:
        errs.append(f"{tag}: plain-JS App alias missing ({alias!r})")
    if source != p["new_source"]:
        errs.append(f"{tag}: plain-JS AppError source mismatch")
    if bytes(bundle[source_end:alias_offset]) != original_fps_body:
        errs.append(f"{tag}: plain-JS Fps implementation changed")
    for marker in (
        b"useInteractivePress",
        b"OnionHEN?NavUI=1",
        b'iconId:{uri:"/system_ex/vsh_asset/onionhen.png"}',
        b"onPress:e",
        b'title:""',
    ):
        if marker not in source:
            errs.append(f"{tag}: plain-JS source missing {marker!r}")
    if b"sendClientApplicationErrorEvent" in source:
        errs.append(f"{tag}: plain-JS AppError telemetry body still active")
    return errs


def run_pass(pass_id: int):
    print(f"\n========== PASS {pass_id} ==========")
    rows = []
    for name, source, archive_member in DUMPS:
        try:
            raw = read_dump_app(source, archive_member, "NPXS40002")
        except (OSError, subprocess.CalledProcessError) as exc:
            print(f"[MISS] {name}: {exc}")
            rows.append((name, False, [str(exc)]))
            continue
        legacy = locate_legacy(raw)
        if legacy is not None:
            p = match_legacy_profile(legacy)
            if not p:
                print(f"[FAIL] {name}: no legacy HomeUI profile")
                rows.append((name, False, ["no legacy profile"]))
                continue

            original = bytes(legacy)
            source_end = p["app_error_source_offset"] + p["app_error_source_size"]
            original_fps_body = original[source_end : p["export_alias_offset"]]
            errs = precheck_legacy(legacy, p)
            if locate_legacy(original) is None:
                errs.append("direct legacy payload locator failed")

            notes1 = apply_legacy(legacy, p)
            errs += verify_legacy(legacy, p, "after-patch", original_fps_body)
            if len(legacy) != len(original):
                errs.append("legacy patch changed payload length")

            allowed = set(
                range(
                    p["icon_order_offset"],
                    p["icon_order_offset"]
                    + len(source_icon_orders(p)[1]),
                )
            )
            allowed.update(
                range(
                    p["app_error_source_offset"],
                    p["app_error_source_offset"] + p["app_error_source_size"],
                )
            )
            allowed.update(
                range(
                    p["export_alias_offset"],
                    p["export_alias_offset"] + len(p["new_alias"]),
                )
            )
            unexpected = [
                offset
                for offset, (old, new) in enumerate(zip(original, legacy))
                if old != new and offset not in allowed
            ]
            if unexpected:
                errs.append(
                    "legacy bytes changed outside allowed ranges at "
                    f"0x{unexpected[0]:x}"
                )

            legacy2 = bytearray(legacy)
            notes2 = apply_legacy(legacy2, p)
            errs += verify_legacy(
                legacy2, p, "idempotent", original_fps_body
            )
            if legacy2 != legacy:
                errs.append("idempotent: legacy second pass changed bytes")
            if any(
                "MISMATCH" in x or "APPLY_FAILED" in x
                for x in notes1 + notes2
            ):
                errs.append(f"apply:{notes1}|{notes2}")
            ok = not errs
            print(f"[{'OK' if ok else 'FAIL'}] {name:12} → {p['name']}")
            if ok:
                order_text = (
                    "OnionHEN|Search|Settings|Profile"
                    if p["strategy_name"] == "kLegacy2xSourceStrategy"
                    else "Search|App|Settings|Profile"
                )
                print(f"       Legacy OK: order={order_text}; OnionHEN=useInteractivePress")
            else:
                for error in errs:
                    print(f"       {error}")
            rows.append((name, ok, errs))
            continue

        plain_js = locate_plain_js(raw)
        if plain_js is not None:
            p = match_plain_js_profile(plain_js)
            if not p:
                print(f"[FAIL] {name}: no plain-JS HomeUI profile")
                rows.append((name, False, ["no plain-JS profile"]))
                continue

            original = bytes(plain_js)
            source_end = p["app_error_source_offset"] + p["app_error_source_size"]
            original_fps_body = original[source_end : p["export_alias_offset"]]
            errs = precheck_plain_js(plain_js, p)
            if locate_plain_js(original) is None:
                errs.append("direct plain-JS payload locator failed")

            notes1 = apply_plain_js(plain_js, p)
            errs += verify_plain_js(
                plain_js, p, "after-patch", original_fps_body
            )
            if len(plain_js) != len(original):
                errs.append("plain-JS patch changed payload length")

            allowed = set(
                range(
                    p["icon_order_offset"],
                    p["icon_order_offset"] + len(LEGACY_NEW_ORDER),
                )
            )
            allowed.update(
                range(
                    p["app_error_source_offset"],
                    p["app_error_source_offset"] + p["app_error_source_size"],
                )
            )
            allowed.update(
                range(
                    p["export_alias_offset"],
                    p["export_alias_offset"] + len(p["new_alias"]),
                )
            )
            unexpected = [
                offset
                for offset, (old, new) in enumerate(zip(original, plain_js))
                if old != new and offset not in allowed
            ]
            if unexpected:
                errs.append(
                    "plain-JS bytes changed outside allowed ranges at "
                    f"0x{unexpected[0]:x}"
                )

            plain_js2 = bytearray(plain_js)
            notes2 = apply_plain_js(plain_js2, p)
            errs += verify_plain_js(
                plain_js2, p, "idempotent", original_fps_body
            )
            if plain_js2 != plain_js:
                errs.append("idempotent: plain-JS second pass changed bytes")
            if any(
                "MISMATCH" in note or "APPLY_FAILED" in note
                for note in notes1 + notes2
            ):
                errs.append(f"apply:{notes1}|{notes2}")

            ok = not errs
            print(f"[{'OK' if ok else 'FAIL'}] {name:12} → {p['name']}")
            if ok:
                print(
                    "       Plain JS OK: Fps=stock; "
                    "order=Search|App|Settings|Profile"
                )
            else:
                for error in errs:
                    print(f"       {error}")
            rows.append((name, ok, errs))
            continue

        hbc = locate_hbc(raw)
        if hbc is None:
            print(f"[FAIL] {name}: no HBC")
            rows.append((name, False, ["no HBC"]))
            continue
        fl = struct.unpack_from("<I", hbc, 0x20)[0]
        if fl <= 0 or fl > len(hbc):
            print(f"[FAIL] {name}: bad file_length 0x{fl:x}")
            rows.append((name, False, [f"bad fl 0x{fl:x}"]))
            continue
        hbc = hbc[:fl]
        p = match_profile(hbc)
        if not p:
            sh = bytes(hbc[0x0C:0x20]).hex()
            print(f"[FAIL] {name}: no profile fl=0x{fl:x} hash={sh}")
            rows.append((name, False, ["no profile"]))
            continue

        errs: list[str] = []
        errs += precheck(hbc, p)
        notes1 = apply_patch(hbc, p)
        errs += verify(hbc, p, "after-patch")

        hbc2 = bytearray(hbc)
        errs += verify(hbc2, p, "reread")
        notes2 = apply_patch(hbc2, p)
        errs += verify(hbc2, p, "idempotent")
        if hbc2 != hbc:
            errs.append("idempotent: Hermes second pass changed bytes")
        if any("MISMATCH" in x or "APPLY_FAILED" in x for x in notes1 + notes2):
            errs.append(f"apply:{notes1}|{notes2}")

        ok = not errs
        print(
            f"[{'OK' if ok else 'FAIL'}] {name:12} → {p['name']} "
            f"[{p['name']}]"
        )
        if ok:
            print(
                "       FixA OK: Fps=stock; AppError=OnionHEN; "
                "order=Search|AppError|Settings|Profile"
            )
            print(
                f"       FixB OK: icon={NEW_ICON_URI.decode()} "
                f"link={NEW_LINK.decode()}"
            )
        else:
            for e in errs:
                print(f"       {e}")
        rows.append((name, ok, errs))
    return rows


def main() -> int:
    print(f"Repo: {REPO}")
    print(f"Profiles: {len(PROFILES)}  Dumps: {len(DUMPS)}")
    print(f"NEW_ICON_URI={NEW_ICON_URI!r}")
    print(f"NEW_LINK={NEW_LINK!r}")

    r1 = run_pass(1)
    r2 = run_pass(2)

    def summarize(rows, tag: str) -> bool:
        ok = sum(1 for _, o, _ in rows if o)
        print(f"\n{tag}: {ok}/{len(rows)} OK")
        for n, o, e in rows:
            if not o:
                print(f"  FAIL {n}: {e}")
        return ok == len(rows)

    s1 = summarize(r1, "PASS1")
    s2 = summarize(r2, "PASS2")
    consistent = all(a[0] == b[0] and a[1] == b[1] for a, b in zip(r1, r2))
    print(f"\nCONSISTENT: {'YES' if consistent else 'NO'}")
    overall = s1 and s2 and consistent
    print(f"OVERALL: {'PASS' if overall else 'FAIL'}")

    print("\n=== Dump → profile ===")
    for n, ok, _ in r1:
        print(f"  {n:12} {'OK' if ok else 'FAIL'}")
    return 0 if overall else 1


if __name__ == "__main__":
    sys.exit(main())
