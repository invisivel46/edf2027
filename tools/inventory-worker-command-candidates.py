"""Find static worker-command candidates; a byte pattern is not a runtime root."""
import csv, hashlib, json, re, sqlite3, struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'out/renderer-inventory'
image = Path('D:/roms2/edf2027-analysis/guest_image.bin').read_bytes()
assert hashlib.sha256(image).hexdigest() == '91922ff98eeab36a595673f773123530d67489f1614dd6a5ba758e3e455efb1a'
db = sqlite3.connect('file:D:/roms2/edf2027-analysis/edfdb.sqlite?mode=ro&immutable=1', uri=True)
functions = dict(db.execute('select addr,name from funcs'))
rows = []
offset = 0
while True:
    offset = image.find(bytes.fromhex('81000000'), offset)
    if offset < 0:
        break
    if offset % 4 == 0 and offset + 12 <= len(image):
        address = 0x82000000 + offset
        callback, argument = struct.unpack_from('>II', image, offset + 4)
        refs = list(db.execute('select func,kind from datarefs where addr=?', (address,)))
        rows.append(dict(address=f'{address:08X}', callback=f'{callback:08X}',
                         argument=f'{argument:08X}', callback_function=functions.get(callback, ''),
                         exact_marker_references=' | '.join(f'{fn}:{kind}' for fn, kind in refs),
                         status='raw pattern only; command storage and execution unproven'))
    offset += 1
with (OUT / 'worker-command-candidates.csv').open('w', newline='', encoding='utf-8') as stream:
    writer = csv.DictWriter(stream, fieldnames=['address','callback','argument','callback_function','exact_marker_references','status'])
    writer.writeheader()
    writer.writerows(rows)
literal_rows = []
for source in sorted((ROOT / 'generated/default').glob('*.cpp')):
    function = ''
    for line_number, line in enumerate(source.read_text(encoding='utf-8-sig').splitlines(), 1):
        match = re.match(r'DEFINE_REX_FUNC\((\w+)\)', line)
        if match:
            function = match[1]
        if re.search(r'-2130706432|0x81000000|2164260864', line):
            literal_rows.append(dict(function=function, source=f'{source.relative_to(ROOT)}:{line_number}', text=line.strip()))
with (OUT / 'worker-command-literal-sites.csv').open('w', newline='', encoding='utf-8') as stream:
    writer = csv.DictWriter(stream, fieldnames=['function','source','text'])
    writer.writeheader()
    writer.writerows(literal_rows)
summary = dict(aligned_marker_matches=len(rows), known_function_followers=sum(bool(r['callback_function']) for r in rows),
               exact_generated_literal_sites=len(literal_rows),
               limitation='Static image pattern scan only; dynamic construction, copied templates, aliases and computed constants are not excluded.')
(OUT / 'worker-command-candidates-summary.json').write_text(json.dumps(summary, indent=2)+'\n')
print(json.dumps(summary, indent=2))
print(json.dumps([r for r in rows if r['callback_function']], indent=2))
