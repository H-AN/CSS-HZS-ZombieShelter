"""Read entity-model material associations; never modify the BSP."""
import json
from pathlib import Path
import re
import struct

p = Path('tests/runtime/v05_server/cstrike/maps/zsh_desertplant_cso_v1_d.bsp')
b = p.read_bytes()

def lump(n):
    off, size, _, _ = struct.unpack_from('<4i', b, 8 + n * 16)
    return b[off:off + size]

entities = [dict(re.findall(r'"([^"]+)"\s*"([^"]*)"', row))
            for row in re.findall(r'\{([^{}]*)\}', lump(0).decode('utf-8', errors='replace'))]
strings, table, data, infos, faces, models = (lump(n) for n in (43, 44, 2, 6, 7, 14))

def material(face):
    info = struct.unpack_from('<h', faces, face * 56 + 10)[0]
    td = struct.unpack_from('<i', infos, info * 72 + 68)[0]
    name = struct.unpack_from('<i', data, td * 32 + 12)[0]
    off = struct.unpack_from('<i', table, name * 4)[0]
    return strings[off:strings.index(b'\0', off)].decode('ascii', errors='replace')

rows = []
for e in entities:
    if not e.get('model', '').startswith('*'):
        continue
    index = int(e['model'][1:])
    first, count = struct.unpack_from('<2i', models, index * 48 + 40)
    row = dict(e)
    row['materials'] = sorted({material(n) for n in range(first, first + count)})
    row['bounds'] = struct.unpack_from('<6f', models, index * 48)
    rows.append(row)
target = Path('tests/reports/v05/followup/map_brushes.json')
target.parent.mkdir(parents=True, exist_ok=True)
target.write_text(json.dumps(rows, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(rows, ensure_ascii=False, indent=2))
