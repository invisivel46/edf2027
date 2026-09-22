"""Give every inventoried indirect call an instruction locator and target-load evidence."""
import csv,json,re,sqlite3,struct
from collections import defaultdict,Counter
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
OUT=ROOT/'out/renderer-inventory'; GHIDRA=ROOT/'out/ghidra/renderer-inventory'
ANALYSIS=Path('D:/roms2/edf2027-analysis')
def read(path,delimiter=','):
 with path.open(encoding='utf-8-sig',newline='') as s: return list(csv.DictReader(s,delimiter=delimiter))
def write(name,items,fields):
 with (OUT/name).open('w',newline='',encoding='utf-8') as s:
  w=csv.DictWriter(s,fieldnames=fields); w.writeheader(); w.writerows(items)
db=sqlite3.connect((ANALYSIS/'edfdb.sqlite').as_uri()+'?mode=ro&immutable=1',uri=True)
image=(ANALYSIS/'guest_image.bin').read_bytes()
instructions=defaultdict(list)
for fn,address,raw in db.execute("select func,addr,raw from instrs where mn='bcctr' order by addr"):
 instructions[fn].append((address,raw))
calls=defaultdict(list)
for row in read(OUT/'complete-indirect-sites.csv'): calls[row['Caller']].append(row)
tables=read(OUT/'verified-vtable-pointers.csv')
slot_functions=defaultdict(set)
for row in tables:
 if row['pointer_matches']=='True': slot_functions[int(row['slot'])].add(row['function'])
sources={}; result=[]; contexts=[]; mismatches=[]
for fn,entries in sorted(calls.items()):
 entries.sort(key=lambda x:int(x['SourceOffset']))
 native=instructions[fn]
 for index,row in enumerate(entries):
  filename=row['File']; source=sources.setdefault(filename,(ROOT/'generated/default'/filename).read_text())
  offset=int(row['SourceOffset']); before=source[max(0,offset-3500):offset]
  address=''; raw_verified=False
  if len(native)==len(entries):
   at,raw=native[index]; address=f'{at:08X}'; raw_verified=struct.unpack_from('>I',image,at-0x82000000)[0]==(raw&0xffffffff)
  else:
   # Include resolved switch bctr sites when matching database instruction order.
   start=source.rfind('DEFINE_REX_FUNC('+fn,0,offset)
   if start<0: start=source.rfind('void '+fn,0,offset)
   branches=list(re.finditer(r'// bctr(?:l)?\s*\n',source[start:offset])) if start>=0 else []
   if branches and len(branches)<=len(native):
    at,raw=native[len(branches)-1]; address=f'{at:08X}'
    raw_verified=struct.unpack_from('>I',image,at-0x82000000)[0]==(raw&0xffffffff)
   # Emitted call return address gives the preceding four-byte PPC call site.
   lr=re.search(r'ctx\.lr = 0x([0-9A-Fa-f]+);\s*$',before)
   if lr and not raw_verified:
    at=int(lr[1],16)-4; address=f'{at:08X}'
    raw=struct.unpack_from('>I',image,at-0x82000000)[0]
    raw_verified=(raw&0xfc0007fe)==0x4c000420
  if not raw_verified: mismatches.append(dict(function=fn,address=address,reason='missing or unverified instruction locator'))
  comments=re.findall(r'// ([^\n]+)',before)[-24:]
  ctr=next((re.fullmatch(r'mtctr r(\d+)',c.strip()) for c in reversed(comments) if re.fullmatch(r'mtctr r(\d+)',c.strip())),None)
  load=''; displacement=''; chain=''; kind='register/function-pointer callback; target population unresolved'
  if ctr:
   register=ctr[1]
   for pos in range(len(comments)-1,-1,-1):
    match=re.fullmatch(r'lwz r'+register+r',(-?\d+)\(r(\d+)\)',comments[pos].strip())
    if match:
     load=comments[pos]; displacement=int(match[1]); base=match[2]
     prior=next((c for c in reversed(comments[:pos]) if re.match(r'(?:lwz|mr|addi|lis) r'+base+r',',c)), '')
     chain=prior+' -> '+load
     if re.fullmatch(r'lwz r'+base+r',0\(r\d+\)',prior.strip()) and displacement>=0 and displacement%4==0:
      kind='vtable-slot load pattern; receiver population unresolved'
     else: kind='constant-offset target load; object/vtable role unresolved'
     break
  slot=displacement//4 if isinstance(displacement,int) and displacement>=0 and displacement%4==0 else ''
  line=source.count('\n',0,offset)+1
  result.append(dict(function=fn,address=address,raw_instruction_verified=raw_verified,
    source=f'generated/default/{filename}:{line}',classification=kind,
    target_load=load,load_chain=chain,byte_offset=displacement,possible_slot=slot,
    image_table_candidates=len(slot_functions[slot]) if slot!='' else 0,
    resolution='unresolved receiver/callback population; candidate slot is not a call-target proof'))
  contexts.append(f'## {fn} / {address} / {filename}:{line}\n'+ '\n'.join(comments)+'\n')
write('indirect-site-ledger.csv',result,list(result[0]))
write('indirect-locator-gaps.csv',mismatches,['function','address','reason'])
(OUT/'indirect-instruction-contexts.md').write_text('\n'.join(contexts),encoding='utf-8')
functions=read(OUT/'complete-function-inventory.csv')
ghidra={x['address'].upper():x for x in read(GHIDRA/'functions.tsv','\t')}
gaps=[]
for function in functions:
 address=function['function'][4:]
 function['ghidra_function_present']=address in ghidra
 function['ghidra_body_bytes']=ghidra.get(address,{}).get('bytes','')
 if address not in ghidra: gaps.append(function)
write('complete-function-inventory.csv',functions,list(functions[0]))
write('ghidra-function-gaps.csv',gaps,list(functions[0]))
summary=dict(indirect_sites=len(result),verified_instruction_locators=sum(x['raw_instruction_verified'] for x in result),
 locator_gaps=len(mismatches),target_load_patterns=dict(Counter(x['classification'] for x in result)),
 ghidra_functions=len(ghidra),emitted_functions=len(functions),missing_ghidra_functions=len(gaps))
(OUT/'reconciliation.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps(summary,indent=2))
