#!/usr/bin/env bash
# Called from bootstrapper CMake POST_BUILD with TARGET_ELF set.
set -euo pipefail

elf="${TARGET_ELF:?TARGET_ELF not set}"
[[ -f "${elf}" ]] || { echo "missing ${elf}" >&2; exit 1; }

# Original (decompressed) size for unpacker
if stat -f%z "${elf}" >/dev/null 2>&1; then
  stat -f%z "${elf}" > "${elf}.lzma.size"
else
  stat -c%s "${elf}" > "${elf}.lzma.size"
fi

# Keep a copy — some lzma tools replace/remove the input
cp -f "${elf}" "${elf}.pre-lzma"

if command -v lzma >/dev/null 2>&1; then
  # Prefer writing alongside without deleting the original if -k is supported
  if lzma -h 2>&1 | grep -q -- ' -k'; then
    lzma -f -9 -k "${elf}"
  else
    # May rename elf -> elf.lzma
    lzma -f -9 "${elf}" || true
  fi
elif command -v xz >/dev/null 2>&1; then
  xz -F lzma -f -9 -c "${elf}.pre-lzma" > "${elf}.lzma"
else
  echo "lzma or xz required" >&2
  exit 1
fi

# Normalize to ${elf}.lzma and restore ${elf}
if [[ -f "${elf}.lzma" ]]; then
  :
elif [[ -f "${elf}" ]] && ! cmp -s "${elf}" "${elf}.pre-lzma" 2>/dev/null; then
  # Input was replaced in-place with compressed data under same name
  mv "${elf}" "${elf}.lzma"
fi

if [[ ! -f "${elf}.lzma" ]]; then
  echo "failed to produce ${elf}.lzma" >&2
  exit 1
fi

# Always leave uncompressed elf present for debugging / re-pack
mv -f "${elf}.pre-lzma" "${elf}"

echo "packed: ${elf}.lzma (size file: ${elf}.lzma.size)"
