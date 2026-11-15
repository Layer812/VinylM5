# Simulated NVS binary size and checksum with 56 nested UTF-8 folders; no music assets.
import struct
names=['/']
for i in range(12):
    parent=f'/Album_{i:02d}_日本語タイトル'
    names.append(parent)
    for j in range(4):names.append(parent+f'/Disc_{j:02d}')
assert len(names)==61
body=bytearray();hash_=2166136261
for path in names:
    b=path.encode('utf-8'); item=struct.pack('<HBB',len(b),min(2,path.count('/')),1 if path.count('/')>1 else 0)+b
    assert len(b)<512
    body.extend(item)
    for v in item:hash_=((hash_^v)*16777619)&0xFFFFFFFF
assert len(body)+24<6144,(len(body)+24)
# no fabricated startup timings: tests only check encoding constraints.
assert (hash_!=0) and len(names)<192
print('PASS R16 cache: 61 UTF-8 directory paths, 6144-byte NVS budget, FNV checksum, no path truncation')
