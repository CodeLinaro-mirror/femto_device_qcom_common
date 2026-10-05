#!/bin/bash
# SPDX-License-Identifier: BSD-3-Clause-Clear
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.

# generate_dlog.sh
# Scans vendor/bin, vendor/lib and vendor/lib64 for ELF files (executables
# and shared libraries) with dlog/.dlog sections, generates:
#   - elf_offset_dlog.bin  (build-ID -> dlog section offset table)
#   - dlog.bin             (concatenated dlog section payloads, local copy only)
#
# Both files are written to the tools/ directory alongside this script.
# Only elf_offset_dlog.bin is then copied to vendor/bin to be included in
# the device image.  dlog.bin stays local for offline decode use only.
#
# Usage:
#   generate_dlog.sh <vendor_bin_dir>
#
# Or standalone:
#   ANDROID_PRODUCT_OUT=out/target/product/gen4_gvm \
#       device/qcom/common/dlog/generate_dlog.sh

set -euo pipefail

# Resolve the script's own directory so paths are always absolute
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DLOG_SCRIPT="${SCRIPT_DIR}/read_elf_dlog.py"

# Allow caller to pass vendor/bin dir as $1, otherwise derive from ANDROID_PRODUCT_OUT
if [ -n "${1:-}" ]; then
    VENDOR_BIN_DIR="$1"
elif [ -n "${ANDROID_PRODUCT_OUT:-}" ]; then
    VENDOR_BIN_DIR="${ANDROID_PRODUCT_OUT}/vendor/bin"
else
    echo "ERROR: pass vendor/bin dir as \$1 or set ANDROID_PRODUCT_OUT"
    exit 1
fi

if [ ! -d "${VENDOR_BIN_DIR}" ]; then
    echo "ERROR: vendor/bin directory not found: ${VENDOR_BIN_DIR}"
    exit 1
fi

VENDOR_DIR="$(dirname "$(realpath "${VENDOR_BIN_DIR}")")"

# Both bins generated locally in tools/ — dlog.bin never goes into the image
ELF_OUTPUT="${SCRIPT_DIR}/elf_offset_dlog.bin"
DLOG_OUTPUT="${SCRIPT_DIR}/dlog.bin"

echo "dlog: scanning ${VENDOR_DIR}"
echo "dlog: writing ${ELF_OUTPUT}"
echo "dlog: writing ${DLOG_OUTPUT} (local copy only, not part of image)"

# Requires Python 3.6+ (f-strings, struct). The script self-validates the
# version and exits with a clear error if the interpreter is too old.
# Uses python3 from PATH so the Android build environment's prebuilt (or host)
# python3 is picked up correctly.
python3 "${DLOG_SCRIPT}" "${VENDOR_DIR}" "${ELF_OUTPUT}" "${DLOG_OUTPUT}"

# Copy only elf_offset_dlog.bin into vendor/bin to be part of the device image
cp "${ELF_OUTPUT}" "${VENDOR_BIN_DIR}/elf_offset_dlog.bin"
echo "dlog: elf_offset_dlog.bin copied to ${VENDOR_BIN_DIR} (image)"

# Copy dlog.bin to vendor/lib64 for ecrm accessibility
VENDOR_LIB64_DIR="${VENDOR_DIR}/lib64"
if [ -d "${VENDOR_LIB64_DIR}" ]; then
    cp "${DLOG_OUTPUT}" "${VENDOR_LIB64_DIR}/dlog.bin"
    echo "dlog: dlog.bin copied to ${VENDOR_LIB64_DIR} (ecrm)"
else
    echo "WARNING: ${VENDOR_LIB64_DIR} not found, skipping dlog.bin copy"
fi
echo "dlog: dlog.bin also kept locally at ${DLOG_OUTPUT} (offline decode only)"

