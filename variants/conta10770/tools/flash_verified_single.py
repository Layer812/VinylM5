#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Layer812
"""VinylM5 R13 post-migration APP-only updater: single active ota_0.
Never migrate partitions here: use migrate_layout_once.py only for initial conversion.
"""
from __future__ import annotations
import argparse
import binascii
import csv
import hashlib
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import zlib

FLASH_BYTES = 16 * 1024 * 1024
PARTITION_TABLE_OFFSET = 0x8000  # Compiled R12 partition-table location; fail on mismatch.
PARTITION_TABLE_SIZE = 0x1000
PARTITION_ENTRY_LEN = 32
APP_IMAGE_MAGIC = 0xE9
ESP32_S3_IMAGE_CHIP_ID = 9

class VerifyError(Exception):
    pass


def need(condition: bool, message: str) -> None:
    if not condition:
        raise VerifyError(message)


def read_csv_partitions(path: Path) -> list[tuple[str, int, int, int, int]]:
    typemap = {'app': 0, 'data': 1}
    subtype = {(0, 'factory'): 0, (0, 'ota_0'): 0x10,
               (1, 'ota'): 0, (1, 'nvs'): 2, (1, 'spiffs'): 0x82,
               (1, 'fat'): 0x81, (1, 'phy'): 0x01, (1, 'coredump'): 0x03}
    parts = []
    with path.open(encoding='utf-8-sig', newline='') as f:
        for fields in csv.reader(line for line in f if line.strip() and not line.lstrip().startswith('#')):
            fields = [field.strip() for field in fields]
            need(len(fields) >= 5, 'Malformed partitions.csv row')
            name, t, st, ofs, size = fields[:5]
            typ = typemap.get(t.lower(), None)
            if typ is None:
                typ = int(t, 0)
            sub = subtype.get((typ, st.lower()), None)
            if sub is None and typ == 0 and st.lower().startswith('ota_'):
                sub = 0x10 + int(st[4:])
            if sub is None:
                sub = int(st, 0)
            need(ofs and size, f'Explicit offset and size required for {name}')
            parts.append((name, typ, sub, int(ofs, 0), int(size, 0)))
    need(len(parts) > 0, 'No expected partitions')
    return sorted(parts)


def read_binary_partitions(raw: bytes) -> list[tuple[str, int, int, int, int]]:
    need(len(raw) == PARTITION_TABLE_SIZE, 'Incomplete partition table read')
    parts = []
    found_md5 = False
    for pos in range(0, len(raw), PARTITION_ENTRY_LEN):
        entry = raw[pos:pos + PARTITION_ENTRY_LEN]
        magic = struct.unpack_from('<H', entry)[0]
        if magic == 0xFFFF:
            break
        if magic == 0xEBEB:
            need(not found_md5, 'Duplicate partition MD5 record')
            found_md5 = True
            expected_md5 = hashlib.md5(raw[:pos]).digest()
            need(entry[16:32] == expected_md5, 'Partition table MD5 is invalid')
            continue
        need(magic == 0x50AA and not found_md5,
             f'Invalid partition table entry at +0x{pos:x}: 0x{magic:04x}')
        typ, sub, ofs, size = struct.unpack_from('<BBII', entry, 2)
        flags, = struct.unpack_from('<I', entry, 28)
        need(flags == 0, 'Partition flags are not supported by this build: refusing write')
        label = entry[12:28].split(b'\x00', 1)[0]
        try:
            name = label.decode('ascii')
        except UnicodeError as exc:
            raise VerifyError('Non-ASCII partition label') from exc
        need(name and ofs and size and ofs + size <= FLASH_BYTES,
             f'Invalid partition size/offset: {name}')
        parts.append((name, typ, sub, ofs, size))
    need(parts, 'Partition table could not be parsed')
    return sorted(parts)


def check_layout(actual: list, expected: list) -> tuple[list, tuple, tuple]:
    need(actual == expected,
         'Actual device layout does not match the OTA build. STOP: no flash.\n'
         f'  Device:   {actual}\n  Project:  {expected}')
    apps = sorted((e for e in actual if e[1] == 0), key=lambda x: x[2])
    need(len(apps) == 1 and apps[0][2] == 0x10 and apps[0][3:5] == (0x20000, 0x300000),
         'Exactly one ota_0 app is required; migrate using separate CMD')
    for app in apps:
        need(app[3] % 0x10000 == 0, 'OTA APP offset not 64KiB aligned')
        need(app[4] >= 0x100000 and app[3] + app[4] <= FLASH_BYTES, 'OTA APP range invalid')
    otas = [e for e in actual if e[1] == 1 and e[2] == 0]
    music = [e for e in actual if e[:3] == ('storage', 1, 0x81)]
    need(len(otas) == 1 and otas[0][4] == 0x2000,
         'otadata partition not found or wrong size')
    need(len(music) == 1 and music[0][3:5] == (0x320000, 0xAE0000),
         'Expected 10.875 MiB FAT storage partition not found')
    return apps, otas[0], music[0]


def select_active_ota(raw: bytes, apps: list) -> tuple[int, int, int]:
    """ESP-IDF OTA selection: 32B record at start of each 4KiB sector.

    Mirrors bootloader_common_get_active_otadata() for confirmed entries.
    Refuses NEW/PENDING_VERIFY (rollback can change next boot), and unknown
    states. When no valid selection is present, does not infer ota_0 fallback.
    """
    need(len(raw) == 0x2000, 'Incomplete otadata read')
    valid = []
    for idx, offset in enumerate((0, 0x1000)):
        seq, = struct.unpack_from('<I', raw, offset)
        state, crc = struct.unpack_from('<II', raw, offset + 24)
        if seq in (0, 0xFFFFFFFF) or state in (3, 4):
            continue
        # ESP-IDF bootloader_common_ota_select_crc =
        # esp_rom_crc32_le(UINT32_MAX, &ota_seq, sizeof(ota_seq)); use CRC32 seed 0xFFFFFFFF.
        if crc != (zlib.crc32(raw[offset:offset + 4], 0xFFFFFFFF) & 0xFFFFFFFF):
            # A damaged inactive record is ignored by the bootloader; a single
            # valid record can still establish an unambiguous boot selection.
            continue
        # VALID=2 or UNDEFINED=0xffffffff. NEW/PENDING could roll back.
        need(state in (2, 0xFFFFFFFF),
             f'otadata[{idx}] state={state} is not rollback-safe; abort')
        valid.append((seq, idx, state))
    need(valid, 'No confirmed OTA selection: refusing to guess which slot boots')
    valid.sort(key=lambda r: r[0], reverse=True)
    if len(valid) > 1:
        need(valid[0][0] != valid[1][0], 'Ambiguous equal OTA sequence values')
    seq, record, state = valid[0]
    slot = (seq - 1) % len(apps)
    app = next((e for e in apps if e[2] == 0x10 + slot), None)
    need(app is not None, f'Active OTA slot {slot} not found in partition table')
    return slot, record, state


def verify_storage_fat(raw: bytes, capacity: int) -> str:
    """Check a *raw* FAT12/FAT16 volume without modifying any flash data.

    Filesystem type is determined from the BPB's cluster count, never the
    unreliable FATXX text label. Wear-levelled FFat and FAT32 must NOT be
    accessed with the raw sector writer used by VinylM5.
    """
    need(len(raw) == 512, 'Incomplete storage boot sector')
    signature = raw[:64].hex(' ')
    print(f'STORAGE CHECK: boot-sector first 64 bytes: {signature}', flush=True)
    if raw[510:512] != b'\x55\xaa':
        if raw == b'\xff' * 512:
            # A freshly flashed device has an UNFORMATTED storage partition.
            # APP writing does not touch this partition, so a FAT boot sector
            # must NOT be required to install the player for the first time.
            # DO NOT infer that the *whole partition* is blank from this:
            # firmware verifies the entire partition before user-authorized
            # first-time FAT16 formatting.
            print('FIRST USE: erased storage boot sector; APP-only write permitted.', flush=True)
            print('FIRST USE: full storage is NOT assumed blank; no formatting during flash.', flush=True)
            return 'UNFORMATTED_BOOT'
        if raw == b'\x00' * 512:
            msg = 'Storage first sector is zero; partition is NOT proven empty'
        else:
            msg = ('No raw FAT boot signature. May be wear-levelled FFat, LittleFS, '
                   'other data, or an unsupported layout')
        raise VerifyError(msg + ': not FAT; no storage changes')
    need(raw[0] in (0xEB, 0xE9) and raw[21] >= 0xF0,
         'Boot signature present but FAT boot jump/media fields are invalid; STOP')
    bps, = struct.unpack_from('<H', raw, 11)
    spc = raw[13]
    reserved, = struct.unpack_from('<H', raw, 14)
    fats = raw[16]
    root, = struct.unpack_from('<H', raw, 17)
    fat_secs, = struct.unpack_from('<H', raw, 22)
    total, = struct.unpack_from('<H', raw, 19)
    if not total:
        total, = struct.unpack_from('<I', raw, 32)
    if (bps != 512 or not spc or spc > 128 or spc & (spc-1)
        or fats not in (1, 2) or not (16 <= root <= 1024)
        or not reserved or not fat_secs or not (0 < total <= capacity // 512)):
        label = raw[54:62].decode('ascii', 'replace')
        raise VerifyError(f'Unsupported raw storage BPB (bps={bps}, spc={spc}, '
                          f'root={root}, fats={fats}, fatSectors={fat_secs}, '
                          f'volume-label-field={label!r}). Not forcing FAT16.')
    first_data = reserved + fats * fat_secs + ((root * 32 + 511) // 512)
    need(first_data < total, 'Raw FAT volume has invalid data region')
    count = (total - first_data) // spc
    need(count >= 1, 'Raw FAT volume has no data clusters')
    if count >= 65525:
        raise VerifyError('Storage appears FAT32; VinylM5 raw reader needs FAT12/16. STOP.')
    bits = 12 if count < 4085 else 16
    need(((count+2)*3+1)//2 <= fat_secs*512 if bits == 12
         else (count+2)*2 <= fat_secs*512,
         'FAT table is too short for computed cluster count')
    print(f'PASS raw FAT{bits}: {count} clusters, {spc} sectors/cluster, '
          f'{total} sectors. No storage bytes will be written.', flush=True)
    return f'FAT{bits}'

def check_firmware(path: Path, address: int, capacity: int) -> tuple[int, str]:
    data = path.read_bytes()
    need(len(data) >= 64, 'Firmware BIN is too small')
    need(data[0] == APP_IMAGE_MAGIC, 'Firmware has invalid ESP image magic')
    # For APP-only writes to ota_0/ota_1, ESP32-S3 ROM flash-interface
    # settings come from the *existing bootloader*, NOT this APP header.
    # The four defined image modes are all legal. Do not overwrite this
    # header or the bootloader merely to force the APP header to QIO.
    # Espressif esptool v4: --flash_mode only alters bootloader images at 0x0.
    flash_modes = {0: 'QIO', 1: 'QOUT', 2: 'DIO', 3: 'DOUT'}
    flash_mode = flash_modes.get(data[2])
    need(flash_mode is not None,
         f'Invalid ESP32-S3 application image flash mode byte: 0x{data[2]:02x}')
    print(f'PASS application BIN header: flash mode={flash_mode} '
          '(APP-only; existing bootloader Flash configuration remains unchanged)',
          flush=True)
    segments = data[1]
    need(1 <= segments <= 16, 'Firmware segment count is implausible')
    need((data[3] >> 4) == 4, 'Firmware header flash size is not 16MB')
    chip, = struct.unpack_from('<H', data, 12)
    need(chip == ESP32_S3_IMAGE_CHIP_ID,
         f'Firmware is not built for ESP32-S3 (header chip_id={chip})')
    # Verify that every described image segment fits inside the file.
    p = 24
    for i in range(segments):
        need(p + 8 <= len(data), f'Missing image segment #{i} header')
        load_addr, size = struct.unpack_from('<II', data, p)
        need(size < FLASH_BYTES and p + 8 + size <= len(data),
             f'Invalid image segment #{i} length')
        p += 8 + size
    need(len(data) <= capacity and address + len(data) <= FLASH_BYTES,
         f'BIN exceeds active APP partition capacity ({len(data)} > {capacity})')
    sha = hashlib.sha256(data).hexdigest()
    return len(data), sha


def run_esptool(python: str, port: str, baud: int, before: str, after: str,
                operation: list[str], *, capture: bool = False) -> str:
    cmd = [python, '-m', 'esptool', '--chip', 'esp32s3', '--port', port,
           '--baud', str(baud), '--before', before, '--after', after] + operation
    result = subprocess.run(cmd, text=True, capture_output=True, errors='replace')
    output = (result.stdout or '') + (result.stderr or '')
    if output:
        print(output.rstrip(), flush=True)
    need(result.returncode == 0, f'esptool failed ({result.returncode}); no further flash actions')
    return output


def check_device_id(text: str) -> None:
    need(bool(re.search(r'\b(?:Chip is|Chip type:|Chip:\s*)\s*ESP32-S3\b', text, re.I)),
         'Could not positively verify ESP32-S3 chip identity')
    m = re.search(r'Detected flash size:\s*(\d+)\s*MB', text, re.I)
    need(m is not None and int(m.group(1)) == 16,
         'Could not positively verify a 16MB flash chip')


def execute(args: argparse.Namespace) -> None:
    need(re.fullmatch(r'COM[1-9]\d*', args.port, re.I) is not None,
         'COM port must be explicit (e.g. COM16)')
    need(args.baud in (115200, 230400, 460800), 'Unexpected baud rate')
    expected = read_csv_partitions(Path(args.partitions))
    # Local BIN header check precedes any write; final capacity check uses device.
    need(Path(args.firmware).is_file(), 'Firmware BIN not found')
    print('READ ONLY: checking ESP32-S3 identity, flash capacity, partitions, otadata...')
    id_text = run_esptool(args.python, args.port, args.baud,
                          'default_reset', 'no_reset', ['flash_id'])
    check_device_id(id_text)
    with tempfile.TemporaryDirectory(prefix='vinylm5_preflash_') as directory:
        table_file = Path(directory) / 'partitions.bin'
        run_esptool(args.python, args.port, args.baud, 'no_reset', 'no_reset',
                    ['read_flash', f'0x{PARTITION_TABLE_OFFSET:x}',
                     f'0x{PARTITION_TABLE_SIZE:x}', str(table_file)])
        actual = read_binary_partitions(table_file.read_bytes())
        apps, ota, music = check_layout(actual, expected)
        _, _, _, ota_address, ota_size = ota
        ota_file = Path(directory) / 'otadata.bin'
        run_esptool(args.python, args.port, args.baud, 'no_reset', 'no_reset',
                    ['read_flash', f'0x{ota_address:x}', f'0x{ota_size:x}', str(ota_file)])
        slot, record, state = select_active_ota(ota_file.read_bytes(), apps)
        app = next(e for e in apps if e[2] == 0x10 + slot)
        address, capacity = app[3], app[4]
        # Verify recorded music layout without changing its contents.
        music_file = Path(directory) / 'storage_boot.bin'
        run_esptool(args.python, args.port, args.baud, 'no_reset', 'no_reset',
                    ['read_flash', f'0x{music[3]:x}', '0x200', str(music_file)])
        # Storage contents do NOT affect the safety of an APP-ONLY write.
        # The firmware verifies the full partition at boot and will NEVER
        # erase other data without the physical right-button action. A bad
        # BPB must not trap a first-time user before the app even boots.
        try:
            fs_type = verify_storage_fat(music_file.read_bytes(), music[4])
        except VerifyError as error:
            fs_type = 'OTHER_DATA_OR_UNKNOWN'
            print(f'STORAGE NOTE (not an APP-write error): {error}', flush=True)
            print('APP install may continue; no storage bytes will be written.', flush=True)
        n, sha = check_firmware(Path(args.firmware), address, capacity)
        print(f'PASS physical single-app map; storage state: {fs_type}; no writes to music')
        print(f'PASS selected otadata[{record}], seq chooses ota_{slot}, state={state}')
        print(f'PASS firmware: {n} bytes; SHA256 {sha}')
        print(f'PASS next boot APP selected at 0x{address:x}, capacity 0x{capacity:x}')
        print('APP-ONLY WRITE (R13): preserve bootloader, partition, NVS, otadata, music and coredump.')
        run_esptool(args.python, args.port, args.baud, 'no_reset', 'hard_reset',
                    ['write_flash', f'0x{address:x}', str(args.firmware)])
    print('PASS: single APP written and verified by esptool; other partitions unchanged.')


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--python', default=sys.executable)
    parser.add_argument('--port', default='COM16')
    parser.add_argument('--baud', type=int, default=460800)
    parser.add_argument('--firmware', required=True)
    parser.add_argument('--partitions', required=True)
    args = parser.parse_args()
    try:
        execute(args)
    except (VerifyError, OSError, ValueError, subprocess.SubprocessError) as exc:
        print(f'ABORT: {exc}\nNO APP FLASH PERFORMED unless an explicit APP-write step has already begun.',
              file=sys.stderr)
        return 1
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
