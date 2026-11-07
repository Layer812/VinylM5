# SPDX-License-Identifier: GPL-3.0-or-later
from pathlib import Path
import hashlib
import struct
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from flash_verified import (read_csv_partitions,read_binary_partitions,
    check_layout,verify_ota_unselected,check_firmware,check_device_id,VerifyError)
ROOT=Path(__file__).resolve().parents[1]
def must_fail(fn,*args):
    try:
        fn(*args)
    except VerifyError:
        return
    raise AssertionError("FAIL-CLOSED check did not reject invalid input")
def fake_table(parts):
    data=bytearray(b"\xff"*0x1000)
    for i,(name,t,sub,ofs,size) in enumerate(parts):
        struct.pack_into("<HBBII16sI",data,32*i,0x50AA,t,sub,ofs,size,name.encode(),0)
    end=32*len(parts)
    struct.pack_into("<H",data,end,0xEBEB)
    data[end+16:end+32]=hashlib.md5(data[:end]).digest()
    return bytes(data)
expected=read_csv_partitions(ROOT/"partitions.csv")
assert read_binary_partitions(fake_table(expected))==expected
addr,size,ota=check_layout(expected,expected)
assert addr==0x10000 and size==0x300000 and ota[3]==0xe000
must_fail(check_layout,sorted(expected+[("ota_0",0,16,0x20000,0x4f0000)]),expected)
verify_ota_unselected(b"\xff"*0x2000)
b=bytearray(b"\xff"*0x2000)
b[40]=0
must_fail(verify_ota_unselected,bytes(b))
with tempfile.TemporaryDirectory() as tmp:
    path=Path(tmp)/"firmware.bin"
    data=bytearray(80)
    data[0]=0xe9
    data[1]=1
    data[2]=0
    data[3]=0x40
    struct.pack_into("<H",data,12,9)
    struct.pack_into("<II",data,24,0x3c000000,32)
    path.write_bytes(data)
    assert check_firmware(path,addr,size)[0]==80
    data[2]=2
    path.write_bytes(data)
    must_fail(check_firmware,path,addr,size)
    data[2]=0
    data[12]=2
    path.write_bytes(data)
    must_fail(check_firmware,path,addr,size)
    data[12]=9
    path.write_bytes(data)
    must_fail(check_firmware,path,addr,64)
check_device_id("Chip is ESP32-S3 (revision 0.2)\nDetected flash size: 16MB")
must_fail(check_device_id,"Chip is ESP32-S3\nDetected flash size: 8MB")
ini=(ROOT/"platformio.ini").read_text()
assert "-DBOARD_ID=30" in ini
cmd=(ROOT/"RUN_VINYLM5_R12_BUILD_FLASH_COM16_APP_ONLY.cmd").read_text()
assert "flash_verified.py" in cmd and "COM16" in cmd
assert "write_flash" not in cmd and "erase_flash" not in cmd
print("PASS: StopWatch R1 layout, chip, flash, BIN and OTA fail-closed checks")
