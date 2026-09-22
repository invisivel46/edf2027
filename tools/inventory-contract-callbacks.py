"""Export raw evidence for retained adapter callback dispatch and producers."""
import csv, json, sqlite3, struct
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'out/renderer-inventory'
db=sqlite3.connect('file:D:/roms2/edf2027-analysis/edfdb.sqlite?mode=ro&immutable=1',uri=True)
image=Path('D:/roms2/edf2027-analysis/guest_image.bin').read_bytes()
functions={'82139228','8214E8E0','8214EE50','8214EBA0','8213EB68','821477A8',
           '8214EC00','8214ECD8','8214EBD0','8214EAD0',
           '821D96D8','821D9600','821B94E8','821B8E48',
           '82149248','82137410','821375C0','82149A90',
           '82141440','82141340','8213FAF8','82141AB8','8213C328','8213CC20','821415A8'}
fields=[]
for fn,addr,raw,mn,d,a,imm in db.execute('select func,addr,raw,mn,d,a,imm from instrs where imm in (10828,15144) order by addr'):
    assert struct.unpack_from('>I',image,addr-0x82000000)[0] == raw&0xffffffff
    fields.append(dict(function=fn,site=f'{addr:08X}',raw=f'{raw&0xffffffff:08X}',mnemonic=mn,d=d,a=a,imm=imm))
    if raw>>26 == 36:
        functions.add(fn.removeprefix('sub_'))
instructions=[]
for fn in sorted(functions):
    for addr,raw,mn,d,a,b,imm,target in db.execute('select addr,raw,mn,d,a,b,imm,target from instrs where func=? order by addr',('sub_'+fn,)):
        assert struct.unpack_from('>I',image,addr-0x82000000)[0] == raw&0xffffffff
        instructions.append(dict(function=fn,site=f'{addr:08X}',raw=f'{raw&0xffffffff:08X}',mnemonic=mn,d=d,a=a,b=b,imm=imm,target=f'{target:08X}' if target else ''))
for filename,rows in [('contract-callback-instructions.csv',instructions),('contract-field-sites.csv',fields)]:
    with (OUT/filename).open('w',encoding='utf-8',newline='') as stream:
        writer=csv.DictWriter(stream,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
(ROOT/'out/ghidra/renderer-inventory/contracts-functions.txt').write_text('\n'.join(sorted(functions))+'\n')
reviews=[
    ('82139270','edf_native_counter_reset_cpu_tail','native_present.cpp:56',
     'monitor = Word(Word(0x82000800)); interface = Word(monitor+32); target = Word(interface+16)',
     'device+15144 bit 0x400 set; monitor zero branch still loads target from address16',
     'monitor callback target population external; native host alone does not bypass this call'),
    ('8213941C','edf_native_counter_reset_cpu_tail','native_present.cpp:295',
     'monitor = Word(Word(0x82000800)); interface = Word(monitor+32); target = Word(interface+12)',
     'device+15144 bit0x400 clear and bit0x100 set; arguments r3=0x820097D8, r4=(flags&255)<<20',
     'monitor callback target population external; profile flag writers and active monitor configuration require closure'),
    ('821394CC','edf_native_counter_reset_cpu_tail','native_present.cpp:392',
     'monitor = Word(Word(0x8200071C)); target = Word(monitor+4)',
     'monitor and target both nonzero; argument r3=0',
     'SDK default-null expectation is source-level only; configured external monitor implementation unbounded'),
    ('8214E9D4','edf_native_worker_audit','native_worker_audit.cpp:165',
     'worker = Word(entry_r3); target = Word(worker+16); command0x81000000 installs Word(command+4) and argument Word(command+8)',
     'first-level worker job with command high bit clear; r3=entry_r3,r4=Word(worker+20),r5=Word(worker+32),r6=Word(worker+24)',
     'no target allowlist in native worker wrapper; null diagnostic logs then dispatch proceeds; command producers and target population remain open')]
rows=[]
for site,adapter,source,expression,guard,remaining in reviews:
    instruction=next(r for r in instructions if r['site']==site)
    assert instruction['raw']=='4E800421',instruction
    rows.append(dict(site=site,function='sub_'+instruction['function'],adapter=adapter,
        source='out/renderer-inventory/adapters/'+source,raw=instruction['raw'],
        target_expression=expression,activation=guard,
        review_status='target provenance and local guards mapped; target population not closed',
        remaining=remaining))
with (OUT/'retained-callback-review.csv').open('w',encoding='utf-8',newline='') as stream:
    writer=csv.DictWriter(stream,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
producer_specs=[
    ('821478D8','914B0004','debug monitor interface+4','device+15144 pointer',
     'interface nonnull and Word(interface)>8','external interface receives alias to profiling flags'),
    ('821478DC','912B0008','debug monitor interface+8','device+15148 pointer',
     'interface nonnull and Word(interface)>8','external interface receives alias to callback result'),
    ('821478E8','914B0018','debug monitor interface+24','device+20000 pointer',
     'interface nonnull','external interface receives alias to frame counter'),
    ('821478EC','912B001C','debug monitor interface+28','device+20004 pointer',
     'interface nonnull','external interface receives alias to frame timing'),
    ('8214EA0C','917F0010','worker+16','Word(command+4)',
     'Word(command)==0x81000000','callback installed from command stream without target allowlist'),
    ('8214EA18','917F0014','worker+20','Word(command+8)',
     'Word(command)==0x81000000','callback argument installed; next command advances by12 bytes')]
producer_rows=[]
for site,raw,destination,value,guard,effect in producer_specs:
    instruction=next(r for r in instructions if r['site']==site)
    assert instruction['raw']==raw
    producer_rows.append(dict(function='sub_'+instruction['function'],site=site,raw=raw,
        destination=destination,value=value,guard=guard,effect=effect,
        limitation='observed producer/escape only; no assertion that later aliases or other command producers are absent'))
with (OUT/'retained-callback-producers.csv').open('w',encoding='utf-8',newline='') as stream:
    writer=csv.DictWriter(stream,fieldnames=list(producer_rows[0]));writer.writeheader();writer.writerows(producer_rows)
known={addr:name for name,addr in db.execute('select name,addr from funcs')}
candidates=[]
for offset in range(0,len(image)-11,4):
    if image[offset:offset+4] != b'\x81\x00\x00\x00':continue
    target,argument=struct.unpack_from('>II',image,offset+4)
    if target in known:
        candidates.append(dict(address=f'{0x82000000+offset:08X}',callback=known[target],argument=f'{argument:08X}',
            review='aligned opcode plus known function pointer; command use not established'))
with (OUT/'worker-static-command-candidates.csv').open('w',encoding='utf-8',newline='') as stream:
    writer=csv.DictWriter(stream,fieldnames=['address','callback','argument','review']);writer.writeheader();writer.writerows(candidates)
print(json.dumps(dict(static_command_candidates=candidates),indent=2))
print(json.dumps(dict(functions=sorted(functions),verified_instructions=len(instructions),field_sites=fields),indent=2))
