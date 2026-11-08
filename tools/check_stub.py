#!/usr/bin/env python3
"""Check the raw-copy stub has no relocation and entrypoints remain in order."""
import struct
import sys
from pathlib import Path
b = Path(sys.argv[1]).read_bytes()
assert b[:6] == b'\x7fELF\x02\x01', 'need little-endian ELF64'
shoff = struct.unpack_from('<Q', b, 40)[0]
entsize, count, strindex = struct.unpack_from('<HHH', b, 58)
sections = [struct.unpack_from('<IIQQQQIIQQ', b, shoff+i*entsize) for i in range(count)]
strings = sections[strindex]
strtab = b[strings[4]:strings[4]+strings[5]]
def cstr(buf, start):
    return buf[start:buf.index(b'\0', start)].decode()
index = next(i for i,s in enumerate(sections) if cstr(strtab,s[0]) == '.text.gastub')
section = sections[index]
assert 0 < section[5] <= 0x8000, 'stub exceeds injector bound'
for s in sections:
    assert not (s[1] in (4,9) and s[7] == index and s[5]), 'stub has relocations'
symbols = {}
for s in sections:
    if s[1] != 2:
        continue
    st = sections[s[6]]
    names = b[st[4]:st[4]+st[5]]
    for pos in range(s[4], s[4]+s[5], s[9]):
        name, info, other, ndx, value, size = struct.unpack_from('<IBBHQQ',b,pos)
        if ndx == index and name:
            symbols[cstr(names,name)] = (value,size)
start = symbols['ga_stub_read_state'][0]
end = symbols['ga_stub_end'][0]
assert start == 0 and 0 < end < section[5], 'stub markers reordered'
for name in ('ga_stub_read_state', 'ga_stub_read_state_ext', 'ga_stub_read', 'ga_stub_read_ext', 'ga_stub_set_vibration', 'ga_stub_set_vibration_mode', 'ga_stub_feedback'):
    value,size = symbols[name]
    assert start <= value and value+size <= end, f'{name} outside copied block'
print(f'PASS: self-contained stub, {end} copied bytes, all entrypoints in block')
