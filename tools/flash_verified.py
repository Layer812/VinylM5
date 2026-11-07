#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Layer812
"""VinylM5 R12: fail-closed, physical APP-only flashing with read-only checks."""
from __future__ import annotations
import argparse
import csv
import hashlib
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

FLASH_BYTES = 16 * 1024 * 1024
TABLE_OFFSET = 0x8000
TABLE_BYTES = 0x1000

class VerifyError(Exception):
    pass

def need(condition, message):
    if not condition:
        raise VerifyError(message)

def read_csv_partitions(path):
    types = {"app": 0, "data": 1}
    subtypes = {(0,"factory"):0,(1,"ota"):0,(1,"nvs"):2,(1,"spiffs"):0x82,(1,"fat"):0x81}
    parts = []
    with Path(path).open(encoding="utf-8-sig", newline="") as f:
        for fields in csv.reader(line for line in f if line.strip() and not line.lstrip().startswith("#")):
            fields = [x.strip() for x in fields]
            need(len(fields) >= 5, "Malformed partition row")
            name, ty, sub, off, size = fields[:5]
            ty = types.get(ty.lower(), int(ty,0) if ty.lower() not in types and ty[:1].isdigit() else -1)
            need(ty in (0,1), "Unexpected partition type")
            sub_name = sub.lower()
            if ty == 0 and sub_name.startswith("ota_"):
                sub = 0x10 + int(sub_name[4:])
            else:
                sub = subtypes.get((ty,sub_name), None)
                if sub is None:
                    try:
                        sub = int(sub_name,0)
                    except ValueError as exc:
                        raise VerifyError("Unknown partition subtype") from exc
            off, size = int(off,0), int(size,0)
            need(name and off and size and off + size <= FLASH_BYTES,
                 "Invalid partition offset / size")
            parts.append((name,ty,sub,off,size))
    need(parts, "Missing build partition table")
    return sorted(parts)

def read_binary_partitions(raw):
    need(len(raw) == TABLE_BYTES, "Incomplete physical partition table")
    parts = []
    found_md5 = False
    for pos in range(0,len(raw),32):
        entry = raw[pos:pos+32]
        magic = struct.unpack_from("<H",entry)[0]
        if magic == 0xFFFF:
            break
        if magic == 0xEBEB:
            need(not found_md5, "Duplicate partition MD5")
            found_md5 = True
            need(entry[16:32] == hashlib.md5(raw[:pos]).digest(),
                 "Partition table MD5 mismatch")
            continue
        need(magic == 0x50AA and not found_md5, "Invalid partition entry")
        ty,sub,off,size = struct.unpack_from("<BBII",entry,2)
        flags, = struct.unpack_from("<I",entry,28)
        need(flags == 0, "Unsupported / encrypted partition flags")
        name = entry[12:28].split(b"\x00",1)[0].decode("ascii")
        need(name and off and size and off+size <= FLASH_BYTES,
             "Invalid physical partition extent")
        parts.append((name,ty,sub,off,size))
    need(parts, "No physical partitions")
    return sorted(parts)

def check_layout(actual,expected):
    need(actual == expected,
         "Physical partitions differ from this factory/storage build. "
         "ABORT; do not flash OTA hardware using a factory build. "
         f"Actual={actual}; expected={expected}")
    apps = [p for p in actual if p[1] == 0]
    otadata = [p for p in actual if p[1] == 1 and p[2] == 0]
    need(len(apps) == 1 and apps[0][2] == 0,
         "Exactly one factory APP is required for this R12 build")
    need(len(otadata) == 1, "otadata missing or ambiguous")
    _,_,_,addr,size = apps[0]
    need(addr % 0x10000 == 0, "APP not 64KiB aligned")
    return addr,size,otadata[0]

def verify_ota_unselected(raw):
    need(len(raw) == 0x2000, "Incomplete physical otadata")
    # Erased otadata is the only unambiguous factory boot state we permit.
    # No CRC guessing, OTA slot selection, erasure or NVS modification.
    need(all(b == 0xFF for b in raw),
         "otadata not fully erased; next boot target cannot be safely "
         "established. NO WRITE. Investigate separately.")

def check_firmware(path,address,capacity):
    data = Path(path).read_bytes()
    need(len(data) >= 64 and data[0] == 0xE9,
         "Invalid / missing ESP application image")
    count = data[1]
    need(1 <= count <= 16, "Invalid segment count")
    need(data[2] == 0, "Flash mode mismatch: expected QIO")
    need((data[3] >> 4) == 4, "Image flash-size header not 16MB")
    chip, = struct.unpack_from("<H",data,12)
    need(chip == 9, f"Image target chip is not ESP32-S3: {chip}")
    pos = 24
    for i in range(count):
        need(pos + 8 <= len(data), f"Truncated segment header {i}")
        load,size = struct.unpack_from("<II",data,pos)
        need(size < FLASH_BYTES and pos + 8 + size <= len(data),
             f"Truncated segment {i}")
        pos += 8 + size
    need(len(data) <= capacity and address + len(data) <= FLASH_BYTES,
         "Firmware exceeds verified APP partition capacity")
    return len(data),hashlib.sha256(data).hexdigest()

def esptool(python,port,baud,before,after,*commands):
    cmd = [python,"-m","esptool","--chip","esp32s3","--port",port,
           "--baud",str(baud),"--before",before,"--after",after,*commands]
    proc = subprocess.run(cmd,text=True,capture_output=True,errors="replace")
    output = (proc.stdout or "") + (proc.stderr or "")
    if output.strip():
        print(output.rstrip(),flush=True)
    need(proc.returncode == 0, f"esptool failed: {proc.returncode}")
    return output

def check_device_id(text):
    need(bool(re.search(r"\b(?:Chip is|Chip type:|Chip:\s*)\s*ESP32-S3\b",
                        text,re.I)), "Device not positively verified as ESP32-S3")
    match = re.search(r"Detected flash size:\s*(\d+)\s*MB",text,re.I)
    need(match is not None and int(match.group(1)) == 16,
         "Physical flash capacity not positively verified as 16MB")

def execute(args):
    need(re.fullmatch(r"COM[1-9]\d*",args.port,re.I) is not None,
         "Specify COM port (default COM16)")
    need(args.baud in (115200,230400,460800), "Unexpected baud")
    expected = read_csv_partitions(args.partitions)
    need(Path(args.firmware).is_file(), "Missing firmware.bin")
    print("READ-ONLY PRECHECK: chip, flash, partitions, otadata",flush=True)
    identity = esptool(args.python,args.port,args.baud,
                       "default_reset","no_reset","flash_id")
    check_device_id(identity)
    with tempfile.TemporaryDirectory(prefix="vinylm5_preflash_") as tmp:
        ptfile = str(Path(tmp)/"partitions.bin")
        esptool(args.python,args.port,args.baud,"no_reset","no_reset",
                "read_flash",f"0x{TABLE_OFFSET:x}",f"0x{TABLE_BYTES:x}",ptfile)
        actual = read_binary_partitions(Path(ptfile).read_bytes())
        address,capacity,ota = check_layout(actual,expected)
        need(ota[4] >= 0x2000, "otadata partition too small")
        otafile = str(Path(tmp)/"otadata.bin")
        esptool(args.python,args.port,args.baud,"no_reset","no_reset",
                "read_flash",f"0x{ota[3]:x}","0x2000",otafile)
        verify_ota_unselected(Path(otafile).read_bytes())
        size,sha = check_firmware(args.firmware,address,capacity)
        print(f"PASS: factory APP 0x{address:x} capacity 0x{capacity:x}; "
              f"ESP32-S3, 16MB; {size} bytes SHA256={sha}",flush=True)
        print("APP-ONLY WRITE: no bootloader/partition/otadata/NVS/music erase")
        esptool(args.python,args.port,args.baud,"no_reset","hard_reset",
                "write_flash",f"0x{address:x}",str(args.firmware))
    print("PASS: APP written; other partitions unchanged")

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--python",default=sys.executable)
    parser.add_argument("--port",default="COM16")
    parser.add_argument("--baud",type=int,default=460800)
    parser.add_argument("--firmware",required=True)
    parser.add_argument("--partitions",required=True)
    args = parser.parse_args()
    try:
        execute(args)
        return 0
    except (VerifyError,OSError,ValueError,subprocess.SubprocessError) as exc:
        print(f"ABORT: {exc}\nNo unverified flash operation permitted.",file=sys.stderr)
        return 1

if __name__ == "__main__":
    raise SystemExit(main())
