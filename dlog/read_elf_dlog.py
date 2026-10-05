# SPDX-License-Identifier: BSD-3-Clause-Clear
# Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.

import os
import sys
import struct
import subprocess
import shutil

# Requires Python 3.6+ (f-strings). Fail early with a clear message.
# Note: this guard uses %-formatting so it works on Python 3.0–3.5 too
# (f-strings elsewhere would cause a SyntaxError before an f-string guard runs).
if sys.version_info < (3, 6):
    sys.exit("ERROR: Python 3.6+ required, got %d.%d" % sys.version_info[:2])

# ELF inspection: readelf is used for build-id extraction (it handles any
# architecture).  Section data is read directly from the ELF binary using
# Python struct parsing — no objcopy needed (the host objcopy often cannot
# handle cross-architecture ELFs like aarch64).
READELF = os.environ.get("DLOG_READELF", "readelf")
# Timeout for readelf invocations — prevents a corrupt/crafted ELF from
# hanging the build indefinitely. Override with DLOG_READELF_TIMEOUT (seconds).
READELF_TIMEOUT = int(os.environ.get("DLOG_READELF_TIMEOUT", "30"))

# Verify readelf is available before processing any files — a missing tool
# causes every ELF to be silently skipped with no actionable error.
if not shutil.which(READELF):
    sys.exit(f"ERROR: '{READELF}' not found. Install binutils or set DLOG_READELF.")

# Offset-table layout: a little-endian uint32 entry count, a newline separator,
# then one "buildidhex:offset\n" line per ELF. Must match dlog.c / DlogService.c.
COUNT_FIELD_SIZE = 4          # struct.pack('<I', ...)
COUNT_SEPARATOR_SIZE = 1      # trailing b'\n'
# Sanity cap on the generated table; mirrors MAX_SHM_SIZE in dlog_lib/src/dlog.c
# and DLOG_SHM_MAX_SIZE in service/inc/DlogService.h. All three must agree.
MAX_SHM_SIZE = 512 * 1024

# Generated outputs — never treat these as scan inputs.
OUTPUT_BINS = ('elf_offset_dlog.bin', 'dlog.bin')


def is_elf64_file(file_path):
    """Return True only for ELF64 files (EI_CLASS == ELFCLASS64 == 2).

    libdlog is LOCAL_MULTILIB := 64 so only 64-bit consumers resolve dlog
    offsets at runtime. Scanning lib32 / ELF32 variants of the same library
    would duplicate every string in dlog.bin (same source, different build-id
    per ABI) and produce dead entries that are never looked up.
    TODO(v2): remove this filter when ELF32 support is added to the C reader.
    """
    try:
        with open(file_path, 'rb') as f:
            ident = f.read(6)
            # bytes 0-3: ELF magic; byte 4: EI_CLASS (1=32-bit, 2=64-bit)
            return ident[:4] == b'\x7fELF' and ident[4] == 2
    except Exception:
        return False


def get_build_id(path):
    """Read the GNU build-id via readelf; returns the hex string or None."""
    try:
        out = subprocess.run(
            [READELF, "-n", path],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            universal_newlines=True,
            timeout=READELF_TIMEOUT,
            # Force C locale so "Build ID:" label is always English regardless
            # of the host system locale.
            env={**os.environ, "LC_ALL": "C"},
        ).stdout
    except subprocess.TimeoutExpired:
        print(f"Skipping {path}: readelf timed out after {READELF_TIMEOUT}s")
        return None
    except Exception as e:
        print(f"Skipping {path}: readelf failed: {e}")
        return None

    for line in out.splitlines():
        if "Build ID:" in line:
            return line.split("Build ID:", 1)[1].strip().lower()
    return None


def get_dlog_data(path):
    """Extract the raw bytes of the 'dlog' (or '.dlog') section directly from
    the ELF file using Python struct parsing.  This avoids objcopy which may
    not support cross-architecture ELFs (e.g. host x86 objcopy vs aarch64 ELF)."""
    try:
        with open(path, 'rb') as f:
            # Read ELF ident
            e_ident = f.read(16)
            if e_ident[:4] != b'\x7fELF':
                return None

            ei_class = e_ident[4]  # 1 = 32-bit, 2 = 64-bit
            ei_data = e_ident[5]   # 1 = little-endian, 2 = big-endian

            if ei_data == 1:
                endian = '<'
            elif ei_data == 2:
                endian = '>'
            else:
                return None

            if ei_class == 1:
                # ELF32: e_shoff at offset 32 (4 bytes), e_shentsize at 46, e_shnum at 48, e_shstrndx at 50
                f.seek(32)
                e_shoff = struct.unpack(endian + 'I', f.read(4))[0]
                f.seek(46)
                e_shentsize = struct.unpack(endian + 'H', f.read(2))[0]
                e_shnum = struct.unpack(endian + 'H', f.read(2))[0]
                e_shstrndx = struct.unpack(endian + 'H', f.read(2))[0]
                sh_fmt = endian + 'IIIIIIIIII'  # 10 x uint32
                sh_name_idx = 0
                sh_offset_idx = 4
                sh_size_idx = 5
            elif ei_class == 2:
                # ELF64: e_shoff at offset 40 (8 bytes), e_shentsize at 58, e_shnum at 60, e_shstrndx at 62
                f.seek(40)
                e_shoff = struct.unpack(endian + 'Q', f.read(8))[0]
                f.seek(58)
                e_shentsize = struct.unpack(endian + 'H', f.read(2))[0]
                e_shnum = struct.unpack(endian + 'H', f.read(2))[0]
                e_shstrndx = struct.unpack(endian + 'H', f.read(2))[0]
                sh_fmt = endian + 'IIQQQQIIQQ'  # Elf64_Shdr
                sh_name_idx = 0
                sh_offset_idx = 4
                sh_size_idx = 5
            else:
                return None

            if e_shoff == 0 or e_shnum == 0:
                return None

            # SHN_XINDEX (0xffff) means the real index is in sh_link of section 0;
            # we don't handle that extended form — skip gracefully rather than
            # seeking to a bogus offset. Also guard against an out-of-range index.
            if e_shstrndx == 0xffff or e_shstrndx >= e_shnum:
                print(f"Skipping {path}: unsupported or out-of-range e_shstrndx={e_shstrndx:#x}")
                return None

            # Validate e_shentsize matches the expected struct layout before
            # unpacking — a mismatch means f.read(e_shentsize) would give the
            # wrong number of bytes and struct.unpack would raise struct.error.
            if e_shentsize != struct.calcsize(sh_fmt):
                return None

            # Read the section header string table to resolve section names
            shstrtab_offset_in_file = e_shoff + e_shstrndx * e_shentsize
            f.seek(shstrtab_offset_in_file)
            shstrtab_hdr = struct.unpack(sh_fmt, f.read(e_shentsize))
            shstrtab_off = shstrtab_hdr[sh_offset_idx]
            shstrtab_size = shstrtab_hdr[sh_size_idx]
            f.seek(shstrtab_off)
            shstrtab = f.read(shstrtab_size)

            # Walk section headers looking for 'dlog' or '.dlog'
            for i in range(e_shnum):
                f.seek(e_shoff + i * e_shentsize)
                shdr = struct.unpack(sh_fmt, f.read(e_shentsize))
                name_offset = shdr[sh_name_idx]
                # Extract null-terminated name from string table.
                # Use find() not index() — index() raises ValueError on a
                # truncated/malformed strtab, which the outer except catches
                # and silently skips the whole file instead of just this entry.
                end = shstrtab.find(b'\x00', name_offset)
                if end == -1:
                    continue
                sec_name = shstrtab[name_offset:end].decode('utf-8', errors='replace')

                if sec_name in ('dlog', '.dlog'):
                    sec_offset = shdr[sh_offset_idx]
                    sec_size = shdr[sh_size_idx]
                    if sec_size == 0:
                        continue
                    f.seek(sec_offset)
                    return f.read(sec_size)

    except Exception as e:
        print(f"Skipping {path}: ELF parse error: {e}")
        return None
    return None


def find_elf_files_with_dlog(directory):
    # Returns a list of (path, dlog_data) tuples sorted by path. The dlog
    # section is parsed exactly once here and handed to extract_multiple() —
    # re-parsing every ELF in extract_multiple() would double the parse cost
    # of a full vendor image.
    elf_files = []
    seen_realpaths = set()  # cycle guard for followlinks=True

    for root, _, files in os.walk(directory, followlinks=True):
        for name in files:
            path = os.path.join(root, name)

            # Deduplicate symlinks that point to the same underlying file.
            real = os.path.realpath(path)
            if real in seen_realpaths:
                continue
            seen_realpaths.add(real)

            # Skip generated output bins
            if name in OUTPUT_BINS:
                continue

            # Skip symbol files only; shared libraries ARE included because
            # the per-library dlog model requires each .so to have an entry
            # in elf_offset_dlog.bin (looked up at runtime via dladdr).
            if name.endswith('.sym'):
                continue

            if not is_elf64_file(path):
                continue

            dlog_data = get_dlog_data(path)
            if dlog_data is None:
                continue

            print(f"Accepted ELF with dlog: {path}")
            elf_files.append((path, dlog_data))

    return sorted(elf_files, key=lambda e: e[0])


def extract_multiple(elf_entries, elf_table_file, dlog_table_file):
    entries = []
    seen_build_ids = set()

    for path, dlog_data in elf_entries:
        build_id_hex = get_build_id(path)
        if build_id_hex is None:
            print(f"Warning: No build ID found in {path}, skipping")
            continue

        # dlog_data was already parsed (and validated non-None) by the finder.
        if build_id_hex in seen_build_ids:
            print(f"Warning: Duplicate build ID seen in {path}, skipping duplicate")
            continue

        seen_build_ids.add(build_id_hex)
        entries.append({
            'build_id_hex': build_id_hex,
            'dlog_data': dlog_data,
            'path': path,
        })

    if not entries:
        print("No valid ELF files with dlog/.dlog and build ID found.")
        return 1

    offsets = []
    current_offset = 0
    for entry in entries:
        offsets.append(current_offset)
        current_offset += len(entry['dlog_data'])

    header_lines = []
    for entry, offset in zip(entries, offsets):
        header_lines.append(f"{entry['build_id_hex']}:{offset}\n")

    header_final = ''.join(header_lines)
    total_elf = len(entries)
    assert_msg = "count/lines mismatch — elf_count would be wrong"
    if total_elf != len(header_lines):
        sys.exit(f"ERROR: {assert_msg} ({total_elf} vs {len(header_lines)})")

    print(f"total ELF : {total_elf}")

    written = COUNT_FIELD_SIZE + COUNT_SEPARATOR_SIZE + \
        len(header_final.encode('utf-8'))
    if written > MAX_SHM_SIZE:
        print(f"ERROR: elf_offset_dlog.bin is {written} bytes — exceeds "
              f"MAX_SHM_SIZE ({MAX_SHM_SIZE} bytes). Runtime will reject the "
              f"segment. Increase MAX_SHM_SIZE in dlog_lib/src/dlog.c, "
              f"DLOG_SHM_MAX_SIZE in service/inc/DlogService.h, "
              f"and MAX_SHM_SIZE here.")
        return 1

    with open(elf_table_file, 'wb') as out_f:
        out_f.write(struct.pack('<I', total_elf))
        out_f.write(b'\n')  # COUNT_SEPARATOR
        out_f.write(header_final.encode('utf-8'))

    dlog_payload_size = sum(len(e['dlog_data']) for e in entries)
    with open(dlog_table_file, 'wb') as out_f:
        for entry in entries:
            out_f.write(entry['dlog_data'])

    print(f"Wrote {len(entries)} entries to {elf_table_file}")
    print(f"Header size: {len(header_final.encode('utf-8'))} bytes")
    print(f"Total dlog payload size: {dlog_payload_size} bytes")
    # dlog.bin is local-only (not deployed), but warn if it grows very large.
    if dlog_payload_size > 10 * 1024 * 1024:
        print(f"WARNING: dlog.bin is {dlog_payload_size // (1024*1024)} MiB — "
              f"consider auditing large dlog sections in the image.")

    return 0


if __name__ == "__main__":
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <input_directory> <elf_table_bin> <dlog_table_bin>")
        sys.exit(1)

    input_dir = sys.argv[1]
    elf_table_file = sys.argv[2]
    dlog_table_file = sys.argv[3]

    elf_files = find_elf_files_with_dlog(input_dir)
    if not elf_files:
        print("No ELF files with dlog/.dlog section found in the directory.")
        sys.exit(1)

    rc = extract_multiple(elf_files, elf_table_file, dlog_table_file)
    sys.exit(rc)
