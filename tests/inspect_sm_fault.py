from pathlib import Path
import struct
import sys
sys.path.insert(0, 'tests/runtime/analysis_deps')
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

binary = Path('tests/runtime/v05_server/cstrike/addons/sourcemod/bin/x64/sourcemod.2.css.dll').read_bytes()
pe = struct.unpack_from('<I', binary, 60)[0]
count, opt = struct.unpack_from('<H', binary, pe + 6)[0], struct.unpack_from('<H', binary, pe + 20)[0]
base = struct.unpack_from('<Q', binary, pe + 24 + 24)[0]
rva = 0x17331
for i in range(count):
    row = pe + 24 + opt + i * 40
    size, start, rawsize, offset = struct.unpack_from('<4I', binary, row + 8)
    if start <= rva < start + max(size, rawsize):
        raw = offset + rva - start
        for op in Cs(CS_ARCH_X86, CS_MODE_64).disasm(binary[raw-33:raw+64], base+rva-33):
            print(f'{op.address-base:08X} {op.mnemonic:8} {op.op_str}')
