"""Read the statistics-dispatch table from the immutable guest image."""
import csv, hashlib, json, struct
from pathlib import Path

root = Path(__file__).resolve().parent.parent
out = root / 'out/renderer-inventory'
image_path = Path('D:/roms2/edf2027-analysis/guest_image.bin')
image = image_path.read_bytes()
assert hashlib.sha256(image).hexdigest() == '91922ff98eeab36a595673f773123530d67489f1614dd6a5ba758e3e455efb1a'
rows = []
for index in range(16):
    address = 0x82552AA0 + 12 * index
    mask, report, selector = struct.unpack_from('>III', image, address - 0x82000000)
    rows.append(dict(address=f'{address:08X}', mask=f'{mask:08X}',
        report_id=f'{report:08X}', selector=selector,
        invokes_profiling_poller=7 <= selector <= 16,
        native_case6_replacement=selector == 6))
assert [row['selector'] for row in rows] == [0, *range(3, 18)]
with (out / 'profiling-dispatch-table.csv').open('w', newline='', encoding='utf-8') as stream:
    writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)
summary = dict(table='82552AA0', rows=len(rows),
    poller_selectors=[r['selector'] for r in rows if r['invokes_profiling_poller']],
    image_sha256=hashlib.sha256(image).hexdigest(),
    limitation='Static table values and reviewed dispatcher routing; not runtime enablement or callback population.')
(out / 'profiling-dispatch-summary.json').write_text(json.dumps(summary, indent=2)+'\n')
print(json.dumps(summary, indent=2))
