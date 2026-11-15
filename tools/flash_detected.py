#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Layer812
"""R14 adaptive APP-only installer (no partition or otadata writes).

PROBE reads the target's actual partition table + otadata, produces a
LOCAL BUILD-TIME ONLY CSV. FLASH repeats the probe, compares immutable
identity/table/selected slot, verifies the complete BIN, then writes only
that exact active application partition. Unknown boot selection fails closed.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

from flash_verified_single import (
    FLASH_BYTES, PARTITION_TABLE_OFFSET, PARTITION_TABLE_SIZE, VerifyError,
    read_binary_partitions, check_device_id, check_firmware, need,
    select_active_ota, verify_storage_fat,
)

ROOT = Path(__file__).resolve().parent.parent
CSV = ROOT / 'partitions_device_generated.csv'
STATE = ROOT / '.vinylm5_device_probed.json'


def _esptool_style(python: str) -> str:
    """Detect CLI spelling without touching the device (esptool 4 vs 5)."""
    code = ("import esptool; "
            "v=getattr(esptool,'__version__',None); "
            "v=v or __import__('importlib.metadata',fromlist=['version']).version('esptool'); "
            "print(v)")
    env = os.environ.copy()
    env['PYTHONIOENCODING'] = 'utf-8'
    env['PYTHONUTF8'] = '1'
    p = subprocess.run([python, '-c', code], capture_output=True, text=True,
                       encoding='utf-8', errors='replace', env=env)
    version = ((p.stdout or '') + (p.stderr or '')).strip()
    m = re.search(r'\b([45])\.\d+', version)
    need(p.returncode == 0 and m is not None,
         f'Unknown esptool version ({version[-200:]}); refusing Flash access')
    return 'legacy' if m.group(1) == '4' else 'modern'


def esptool(python, port, *command, before='no-reset', after='no-reset'):
    style = _esptool_style(python)
    # esptool v4.9: default_reset/no_reset/hard_reset, flash_id/read_flash/write_flash
    # esptool v5.3: default-reset/no-reset/hard-reset, flash-id/read-flash/write-flash
    if style == 'legacy':
        before, after = before.replace('-', '_'), after.replace('-', '_')
        command = tuple(part.replace('-', '_') if part in
                        ('flash-id', 'read-flash', 'write-flash') else part
                        for part in command)
    args = [python, '-m', 'esptool', '--chip', 'esp32s3', '--port', port,
            '--baud', '460800', '--before', before, '--after', after, *command]
    env = os.environ.copy()
    env['PYTHONIOENCODING'] = 'utf-8'
    env['PYTHONUTF8'] = '1'
    p = subprocess.run(args, capture_output=True, text=True,
                       encoding='utf-8', errors='replace', env=env)
    out = (p.stdout or '') + (p.stderr or '')
    if p.returncode:
        raise VerifyError('COM/Flash read failed; no APP write.\n' + out[-800:])
    return out


def read_flash(python, port, offset, n):
    with tempfile.TemporaryDirectory(prefix='vinylm5_read_') as td:
        name = Path(td) / 'read.bin'
        esptool(python, port, 'read-flash', hex(offset), hex(n), str(name))
        data = name.read_bytes()
        need(len(data) == n, f'Flash read incomplete at {offset:#x}')
        return data


def validate_partition_map(parts):
    """Never infer an APP from a fixed offset or another board's layout."""
    need(parts, 'No valid partition table')
    ordered = sorted(parts, key=lambda p: p[3])
    for p in ordered:
        name, typ, sub, offset, size = p
        need(offset % 4096 == 0 and size % 4096 == 0,
             f'Partition alignment invalid: {name} {offset:#x}+{size:#x}')
        need(offset >= 0x9000 and offset + size <= FLASH_BYTES,
             f'Partition outside 16MB Flash: {name}')
        if typ == 0:
            need(offset % 0x10000 == 0, f'APP partition must start on 64KB boundary: {name}')
        need(len(name) <= 15, 'Partition label too long')
    for a, b in zip(ordered, ordered[1:]):
        need(a[3] + a[4] <= b[3],
             f'Overlapping partitions: {a[0]} / {b[0]}')
    apps = sorted([p for p in parts if p[1] == 0], key=lambda p: p[2])
    need(apps, 'No APP partition')
    ota = [p for p in parts if p[1] == 1 and p[2] == 0]
    storage = [p for p in parts if p[0] == 'storage']
    need(len(storage) <= 1, 'Duplicate storage partitions')
    if storage:
        need(storage[0][1:3] == (1, 0x81),
             'storage exists but is not a raw FAT partition; refusing to re-interpret data')
    else:
        print('NOTE: No internal FAT storage; microSD playback only, no data formatting.', flush=True)
    factories = [p for p in apps if p[2] == 0]
    ota_apps = [p for p in apps if p[2] >= 0x10]
    need(len(factories) <= 1 and all(p[2] == 0 or p[2] >= 0x10 for p in apps),
         'Unknown APP partition type; no boot target can be selected')
    if factories:
        need(factories[0][0] in ('factory', 'app0'), 'Unrecognized factory APP label')
    if not ota_apps:
        need(len(factories) == 1, 'No factory APP')
        return apps, None, storage, 'factory'
    need(len(ota) == 1 and ota[0][4] == 0x2000,
         'OTA APP requires exactly one 8KB otadata partition')
    need(len(ota_apps) <= 16 and
         all(p[2] == 0x10 + i for i, p in enumerate(ota_apps)),
         'OTA slots must be contiguous, from ota_0, with no unknown APP types')
    return apps, ota[0], storage, 'mixed' if factories else 'ota'


def probe(python, port):
    info = esptool(python, port, 'flash-id', before='default-reset')
    check_device_id(info)
    mac_match = re.search(r'\bMAC:\s*([0-9a-f:]{17})\b', info, re.I)
    need(mac_match is not None, 'Cannot verify physical target MAC')
    mac = mac_match.group(1).lower()
    data = read_flash(python, port, PARTITION_TABLE_OFFSET, PARTITION_TABLE_SIZE)
    parts = read_binary_partitions(data)
    print('DEVICE PARTITION MAP (read-only):', flush=True)
    for name,typ,sub,ofs,sz in sorted(parts,key=lambda p:p[3]):
        print(f'  {name:15s} type={typ:02x}/{sub:02x}  {ofs:#08x} + {sz:#08x}', flush=True)
    apps, ota, storage, kind = validate_partition_map(parts)
    if kind in ('ota', 'mixed'):
        seq = read_flash(python, port, ota[3], ota[4])
        if kind == 'mixed' and seq == b'\xff' * len(seq):
            app = next(p for p in apps if p[2] == 0)
            print('PASS: erased otadata; factory fallback is unambiguous', flush=True)
        else:
            ota_apps = [p for p in apps if p[2] >= 0x10]
            slot, record, state = select_active_ota(seq, ota_apps)
            app = next(p for p in apps if p[2] == 0x10 + slot)
            print(f'PASS: otadata[{record}] selects ota_{slot}; state={state}', flush=True)
    else:

        app = apps[0]
        print('PASS: factory-only layout (no OTA data or alternate apps)', flush=True)
    need(app[4] >= 0x100000, 'Selected application partition is smaller than 1MB')
    if storage:
        # Info only: storage contents are not a precondition for APP-only updates.
        sector = read_flash(python, port, storage[0][3], 512)
        try:
            formatted = verify_storage_fat(sector, storage[0][4])
        except VerifyError as ex:
            formatted = 'UNKNOWN_OR_EMPTY'
            print(f'NOTE: storage unchanged ({ex})', flush=True)
        print(f'STORAGE: {storage[0][3]:#x} + {storage[0][4]:#x} [{formatted}]', flush=True)
    app_image = read_flash(python, port, app[3], 32)
    need(app_image[0] == 0xE9,
         'Selected boot APP header is not valid; refusing to overwrite an uncertain boot state')
    print(f'SELECTED APP: {app[0]} {app[3]:#x} size {app[4]:#x} (read-only)', flush=True)
    return {
        'mac': mac,
        'partitions_sha256': hashlib.sha256(data).hexdigest(),
        'parts': [list(p) for p in parts],
        'app_name': app[0],
        'app_offset': app[3],
        'app_capacity': app[4],
        'kind': kind,
    }


def csv_for_platformio(parts):
    lines = ['# GENERATED FROM ACTUAL DEVICE; BUILD INPUT ONLY. NEVER FLASH THIS CSV.',
             '# Name,Type,SubType,Offset,Size,Flags']
    for name,typ,sub,ofs,sz in sorted(parts,key=lambda p:p[3]):
        lines.append(f'{name},0x{typ:x},0x{sub:x},0x{ofs:x},0x{sz:x},')
    return '\n'.join(lines) + '\n'


def run(args):
    need(re.fullmatch(r'COM[1-9][0-9]*', args.port, re.I),
         'COM port should be COM13, COM16, etc.')
    if args.probe:
        state = probe(args.python, args.port)
        # The CSV affects local compilation only; the device's partition table
        # is never written. The runtime uses esp_partition_find_first().
        CSV.write_text(csv_for_platformio(state['parts']), encoding='ascii')
        STATE.write_text(json.dumps(state, indent=2) + '\n', encoding='utf-8')
        print(f'PASS: physical partition layout captured for build, MAC={state["mac"]}', flush=True)
        return
    need(args.flash, 'Specify exactly one of --probe or --flash')
    need(STATE.exists() and CSV.exists(), 'Missing probe record; do not flash')
    previous = json.loads(STATE.read_text(encoding='utf-8'))
    need(CSV.read_text(encoding='ascii') == csv_for_platformio(previous['parts']),
         'Build-time partition CSV changed after hardware probe')
    state = probe(args.python, args.port)
    for key in ('mac', 'partitions_sha256', 'parts', 'app_name',
                'app_offset', 'app_capacity', 'kind'):
        need(state[key] == previous[key],
             f'Hardware identity / selected boot APP changed since build: {key}; no write')
    need(args.firmware and Path(args.firmware).is_file(), 'Built firmware BIN missing')
    n, sha = check_firmware(Path(args.firmware), state['app_offset'], state['app_capacity'])
    print(f'PASS: ESP32-S3, 16MB, MAC={state["mac"]}, image={n} bytes', flush=True)
    print(f'PASS: firmware SHA256 {sha}', flush=True)
    print(f'APP ONLY: {state["app_name"]} at 0x{state["app_offset"]:x}; no bootloader, partition, otadata, or music changes', flush=True)
    out = esptool(args.python, args.port, 'write-flash', hex(state['app_offset']),
                  str(Path(args.firmware).resolve()), after='hard-reset')
    print(out.rstrip(), flush=True)
    print(f'PASS: only {state["app_name"]} application written.', flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument('--probe', action='store_true')
    mode.add_argument('--flash', action='store_true')
    ap.add_argument('--python', default=sys.executable)
    ap.add_argument('--port', default='COM13')
    ap.add_argument('--firmware')
    args = ap.parse_args()
    try:
        run(args)
    except (VerifyError, OSError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f'ABORT: {exc}\nNO APP FLASH PERFORMED unless APP write explicitly started.', file=sys.stderr)
        raise SystemExit(1)

if __name__ == '__main__':
    main()
