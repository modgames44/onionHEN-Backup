#!/usr/bin/env python3
"""Verify Settings label patches and runtime icon redirection.

The runtime patch must preserve Hermes shared string storage: only the Debug
Settings label and the HBC footer may change. The stock Settings icon asset is
never overwritten; ShellUI redirects its URI only while hooks are active.
Legacy bundles use profiled, equal-length label replacements and keep the
stock icon id untouched.
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
DUMP_ROOT = Path("/Users/chenpy/Projects/Person/ps5-kylin/Sony Dumps")
SETTINGS_CPP = (
    REPO / "source/shellui/src/settings_bundle_patch.cpp"
).read_text()
EMPTY_SETTINGS_DUMPS = (
    ("2.30", "2.3厚机.rar", "NPXS40008.bin"),
    ("2.50", "2.5厚机.rar", "NPXS40008.bin"),
)
LEGACY_DUMPS = (
    ("3.00", "3.0厚机.rar", "NPXS40008.bin"),
    ("3.10", "03.10厚机.rar", "NPXS40008.bin"),
    ("3.20", "03.20厚机.rar", "NPXS40008.bin"),
    ("3.21", "03.21厚机.rar", "NPXS40008.bin"),
    ("4.00", "4.0厚机.rar", "4.0厚机/NPXS40008.bin"),
    ("4.02", "4.02厚机.rar", "4.02厚机/NPXS40008.bin"),
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
)
HERMES_DUMPS = (
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
)
HERMES_MAGIC = bytes.fromhex("c61fbc03c103191f")
LEGACY_MAGIC = bytes.fromhex("e5d10bfb")
FOOTER_SIZE = 20
OLD_LABEL = ("\u2605Debug Settings").encode("utf-16le")
NEW_LABEL = ("\u2605OnionHEN Tools").encode("utf-16le")
LEGACY_OLD_LABEL = ("\u2605Debug Settings").encode()
LEGACY_NEW_LABEL = ("\u2605OnionHEN Tools").encode()
LEGACY_OLD_ICON = b"icon_setting"
COMMON_PROTECTED_STRINGS = (
    b"icon_setting",
    b"_settingInstance",
)
VERSION_PROTECTED_STRINGS = {
    "9.00": (
        b"assets/src/modules/devices/hunt/buttonAssignments/assets/icon",
    ),
    "9.40": (
        b"assets/src/modules/devices/hunt/buttonAssignments/assets/icon",
    ),
    "9.60": (
        b"assets/src/modules/devices/hunt/buttonAssignments/assets/icon",
    ),
    "11.60": (b"avatar-appear-offline-icon",),
}


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


def load_legacy_profiles() -> list[dict]:
    table = re.search(
        r"kLegacySettingsProfiles\[\] = \{(.*?)\n\};", SETTINGS_CPP, re.S
    )
    if not table:
        raise SystemExit("legacy Settings profile table not found")
    profiles = []
    for name, payload_size, label_offset, icon_offset in re.findall(
        r'\{"([^"]+)",\s*(0x[0-9a-fA-F]+),\s*'
        r'(0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+)\}',
        table.group(1),
    ):
        profiles.append(
            {
                "name": name,
                "payload_size": int(payload_size, 16),
                "label_offset": int(label_offset, 16),
                "icon_offset": int(icon_offset, 16),
            }
        )
    if not profiles:
        raise SystemExit("no legacy Settings profiles parsed")
    return profiles


LEGACY_PROFILES = load_legacy_profiles()


def find_all(data: bytes, needle: bytes) -> list[int]:
    offsets: list[int] = []
    start = 0
    while True:
        offset = data.find(needle, start)
        if offset < 0:
            return offsets
        offsets.append(offset)
        start = offset + 1


def load_hbc(dump: tuple[str, str, str | None]) -> bytearray:
    version, source, archive_member = dump
    raw = read_dump_app(source, archive_member, "NPXS40008")
    hbc_offset = raw.find(HERMES_MAGIC)
    if hbc_offset < 0:
        raise ValueError(f"{version}: Hermes magic not found")
    available = raw[hbc_offset:]
    if len(available) < 0x24:
        raise ValueError(f"{version}: truncated HBC header")
    file_length = struct.unpack_from("<I", available, 0x20)[0]
    if file_length < FOOTER_SIZE or file_length > len(available):
        raise ValueError(f"{version}: invalid HBC file length 0x{file_length:x}")
    return bytearray(available[:file_length])


def footer_is_valid(hbc: bytes) -> bool:
    footer_offset = len(hbc) - FOOTER_SIZE
    return hashlib.sha1(hbc[:footer_offset]).digest() == hbc[footer_offset:]


def apply_settings_patch(hbc: bytearray) -> bool:
    label_offset = hbc.find(OLD_LABEL)
    if label_offset < 0:
        return False
    hbc[label_offset : label_offset + len(OLD_LABEL)] = NEW_LABEL
    footer_offset = len(hbc) - FOOTER_SIZE
    hbc[footer_offset:] = hashlib.sha1(hbc[:footer_offset]).digest()
    return True


def verify_hermes_version(dump: tuple[str, str, str | None]) -> list[str]:
    version = dump[0]
    errors: list[str] = []
    original = load_hbc(dump)
    if not footer_is_valid(original):
        errors.append("stock footer SHA-1 is invalid")
    if original.count(OLD_LABEL) != 1:
        errors.append(f"stock label count is {original.count(OLD_LABEL)}, expected 1")

    version_protected = VERSION_PROTECTED_STRINGS.get(version, ())
    protected_strings = COMMON_PROTECTED_STRINGS + version_protected
    protected_offsets = {
        needle: find_all(original, needle) for needle in protected_strings
    }
    for needle, offsets in protected_offsets.items():
        if not offsets:
            errors.append(f"stock protected string missing: {needle!r}")

    icon_offset = original.find(b"icon_setting")
    icon_range = range(icon_offset, icon_offset + len(b"icon_setting"))
    for collateral in version_protected + (b"_settingInstance",):
        collateral_offset = original.find(collateral)
        collateral_range = range(
            collateral_offset, collateral_offset + len(collateral)
        )
        if (
            icon_range.stop <= collateral_range.start
            or collateral_range.stop <= icon_range.start
        ):
            errors.append(f"expected shared storage not found: {collateral!r}")

    patched = bytearray(original)
    changed = apply_settings_patch(patched)
    if not changed:
        errors.append("first patch did not replace the label")
        return errors
    if not footer_is_valid(patched):
        errors.append("patched footer SHA-1 is invalid")

    label_offset = original.find(OLD_LABEL)
    footer_offset = len(original) - FOOTER_SIZE
    allowed = set(range(label_offset, label_offset + len(OLD_LABEL)))
    allowed.update(range(footer_offset, len(original)))
    unexpected = [
        offset
        for offset, (old, new) in enumerate(zip(original, patched))
        if old != new and offset not in allowed
    ]
    if unexpected:
        errors.append(
            f"bytes changed outside label/footer, first offset=0x{unexpected[0]:x}"
        )

    for needle, offsets in protected_offsets.items():
        if find_all(patched, needle) != offsets:
            errors.append(f"protected string changed: {needle!r}")
    if b"onionh_sicon" in patched:
        errors.append("deprecated onionh_sicon id appeared in Hermes HBC")

    second_pass = bytearray(patched)
    if apply_settings_patch(second_pass):
        errors.append("second patch unexpectedly replaced another label")
    if second_pass != patched:
        errors.append("second patch was not idempotent")

    print(
        f"[{'PASS' if not errors else 'FAIL'}] {version} NPXS40008 "
        f"footer={patched[-FOOTER_SIZE:].hex()}"
    )
    return errors


def locate_legacy_payload(data: bytes | bytearray) -> int | None:
    if data[: len(LEGACY_MAGIC)] == LEGACY_MAGIC:
        return 0
    if data[:8] != b"RNPSHEDR" or len(data) < 0x20:
        return None
    declared = struct.unpack_from("<I", data, 0x1C)[0]
    for offset in dict.fromkeys((declared, 0xB20, 0xB30)):
        if 0 < offset <= len(data) - len(LEGACY_MAGIC):
            if data[offset : offset + len(LEGACY_MAGIC)] == LEGACY_MAGIC:
                return offset
    return None


def match_legacy_profile(payload: bytes | bytearray) -> dict | None:
    for profile in LEGACY_PROFILES:
        if len(payload) != profile["payload_size"]:
            continue
        label_offset = profile["label_offset"]
        icon_offset = profile["icon_offset"]
        label = bytes(payload[label_offset : label_offset + len(LEGACY_OLD_LABEL)])
        icon = bytes(payload[icon_offset : icon_offset + len(LEGACY_OLD_ICON)])
        if label not in (LEGACY_OLD_LABEL, LEGACY_NEW_LABEL):
            continue
        if icon != LEGACY_OLD_ICON:
            continue
        return profile
    return None


def apply_legacy_settings(
    data: bytearray,
) -> tuple[dict | None, int | None, int, int]:
    payload_offset = locate_legacy_payload(data)
    if payload_offset is None:
        return None, None, 0, 0
    payload = data[payload_offset:]
    profile = match_legacy_profile(payload)
    if profile is None:
        return None, payload_offset, 0, 0

    label_count = 0
    label_offset = payload_offset + profile["label_offset"]
    if data[label_offset : label_offset + len(LEGACY_OLD_LABEL)] == LEGACY_OLD_LABEL:
        data[label_offset : label_offset + len(LEGACY_OLD_LABEL)] = LEGACY_NEW_LABEL
        label_count = 1
    return profile, payload_offset, label_count, 0


def verify_legacy_version(dump: tuple[str, str, str | None]) -> list[str]:
    version, source, archive_member = dump
    errors: list[str] = []
    original = read_dump_app(source, archive_member, "NPXS40008")
    payload_offset = locate_legacy_payload(original)
    if payload_offset is None:
        return ["legacy payload not found"]
    original_payload = original[payload_offset:]
    profile = match_legacy_profile(original_payload)
    if profile is None:
        return [f"no profile for payload size 0x{len(original_payload):x}"]

    if locate_legacy_payload(original_payload) != 0:
        errors.append("direct legacy payload locator failed")
    old_label_offsets = find_all(original_payload, LEGACY_OLD_LABEL)
    if len(old_label_offsets) != 2:
        errors.append(
            f"stock UTF-8 label count is {len(old_label_offsets)}, expected 2"
        )
    if profile["label_offset"] not in old_label_offsets:
        errors.append("profile does not target a stock Debug Settings label")
    if original_payload.find(LEGACY_OLD_ICON) != profile["icon_offset"]:
        errors.append("profile icon offset does not match stock icon_setting")

    patched = bytearray(original)
    matched, patched_offset, label_count, icon_count = apply_legacy_settings(patched)
    if matched != profile or patched_offset != payload_offset:
        errors.append("container patch matched the wrong profile or payload offset")
    if (label_count, icon_count) != (1, 0):
        errors.append(
            f"first patch counts label={label_count} icon={icon_count}, expected 1/0"
        )
    if len(patched) != len(original):
        errors.append("legacy patch changed container length")

    label_start = payload_offset + profile["label_offset"]
    icon_start = payload_offset + profile["icon_offset"]
    allowed = set(range(label_start, label_start + len(LEGACY_OLD_LABEL)))
    unexpected = [
        offset
        for offset, (old, new) in enumerate(zip(original, patched))
        if old != new and offset not in allowed
    ]
    if unexpected:
        errors.append(
            f"legacy bytes changed outside label at 0x{unexpected[0]:x}"
        )
    if bytes(patched[icon_start : icon_start + len(LEGACY_OLD_ICON)]) != LEGACY_OLD_ICON:
        errors.append("legacy patch changed stock icon id")

    patched_payload = bytes(patched[payload_offset:])
    remaining_old_labels = find_all(patched_payload, LEGACY_OLD_LABEL)
    expected_remaining = [
        offset for offset in old_label_offsets if offset != profile["label_offset"]
    ]
    if remaining_old_labels != expected_remaining:
        errors.append("non-menu Debug Settings text changed")

    direct = bytearray(original_payload)
    _, direct_offset, direct_labels, direct_icons = apply_legacy_settings(direct)
    if direct_offset != 0 or (direct_labels, direct_icons) != (1, 0):
        errors.append("direct payload patch did not apply exactly once")
    if direct != patched[payload_offset:]:
        errors.append("direct and RNPS-container patch results differ")

    second_pass = bytearray(patched)
    _, _, second_labels, second_icons = apply_legacy_settings(second_pass)
    if (second_labels, second_icons) != (0, 0):
        errors.append("legacy second pass was not idempotent")
    if second_pass != patched:
        errors.append("legacy second pass changed bytes")

    print(
        f"[{'PASS' if not errors else 'FAIL'}] {version} NPXS40008 "
        f"profile={profile['name']} label=0x{profile['label_offset']:x} "
        f"icon=0x{profile['icon_offset']:x}"
    )
    return errors


def verify_source_contract() -> list[str]:
    errors: list[str] = []
    bootstrapper = (REPO / "source/bootstrapper/source/main.cpp").read_text()
    hook_functions = (REPO / "source/shellui/src/hook_functions.cpp").read_text()
    if any(
        needle in SETTINGS_CPP
        for needle in ("kLegacyNewIcon", "onionh_sicon", "onionhen_sicon")
    ):
        errors.append("Settings bundle still rewrites the icon id")
    if any(
        needle in bootstrapper
        for needle in ("NPXS40008/assets", "texture/icon_setting.png")
    ):
        errors.append("bootstrapper still overwrites the stock Settings icon")
    if (
        "icon_setting" not in hook_functions
        or "/system_ex/vsh_asset/onionhen.png" not in hook_functions
    ):
        errors.append("Settings icon URI interception is missing")
    if any(
        needle in hook_functions
        for needle in ("onionh_sicon", "onionhen_sicon")
    ):
        errors.append("Settings icon hook still contains deprecated icon ids")
    return errors


def main() -> int:
    all_errors = verify_source_contract()
    for dump in EMPTY_SETTINGS_DUMPS:
        version, source, archive_member = dump
        try:
            raw = read_dump_app(source, archive_member, "NPXS40008")
            errors = [] if not raw else [f"expected empty dump, got {len(raw)} bytes"]
        except (OSError, subprocess.CalledProcessError) as exc:
            errors = [str(exc)]
        if not errors:
            print(f"[SKIP] {version} NPXS40008 dump is empty")
        all_errors.extend(f"{version}: {error}" for error in errors)
    for dump in LEGACY_DUMPS:
        version = dump[0]
        try:
            errors = verify_legacy_version(dump)
        except (OSError, ValueError, struct.error, subprocess.CalledProcessError) as exc:
            errors = [str(exc)]
        all_errors.extend(f"{version}: {error}" for error in errors)
    for dump in HERMES_DUMPS:
        version = dump[0]
        try:
            errors = verify_hermes_version(dump)
        except (OSError, ValueError, struct.error, subprocess.CalledProcessError) as exc:
            errors = [str(exc)]
        all_errors.extend(f"{version}: {error}" for error in errors)

    if all_errors:
        for error in all_errors:
            print(f"ERROR: {error}")
        print("OVERALL: FAIL")
        return 1
    print("OVERALL: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
