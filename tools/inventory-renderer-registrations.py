"""Record verified instruction evidence for startup renderer registration chains."""
import csv,json,re,sqlite3,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent;OUT=ROOT/'out/renderer-inventory'
db=sqlite3.connect('file:D:/roms2/edf2027-analysis/edfdb.sqlite?mode=ro&immutable=1',uri=True)
image=Path('D:/roms2/edf2027-analysis/guest_image.bin').read_bytes()
script=(ROOT/'tools/ghidra/RendererRegistration.java').read_text()
functions=re.findall(r'0x([0-9A-F]{8})L',script)
instructions=[];calls=[]
for fn in functions:
 for addr,raw,mn,target in db.execute('select addr,raw,mn,target from instrs where func=? order by addr',('sub_'+fn,)):
  assert struct.unpack_from('>I',image,addr-0x82000000)[0]==raw&0xffffffff,f'image mismatch {addr:x}'
  instructions.append(dict(function=fn,address=f'{addr:08X}',raw=f'{raw&0xffffffff:08X}',mnemonic=mn,verified=True))
  if target and (raw&0xfc000001)==0x48000001:
   calls.append(dict(function=fn,site=f'{addr:08X}',target=f'{target:08X}',verified=True))
def write(name,rows):
 with (OUT/name).open('w',newline='',encoding='utf-8') as stream:
  writer=csv.DictWriter(stream,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
write('registration-instructions.csv',instructions);write('registration-direct-calls.csv',calls)
startup=[x for x in calls if x['function']=='820B1F60' and x['target']=='821A68C8']
ctors=[('820A9618','82001648'),('82170488','820118D4'),('82195AB8','82015E1C'),('821E4C08','82020360')]
assert len(startup)==4
rows=[]
for site,(ctor,table) in zip(startup,ctors):
 preceding=[x for x in calls if x['function']=='820B1F60' and x['target']==ctor and x['site']<site['site']]
 assert preceding,ctor
 rows.append(dict(startup='820B1F60',constructor=ctor,constructor_call=preceding[-1]['site'],table=table,
  registration_call=site['site'],register_function='821A68C8',insert_function='8219EA48',
  destination='manager+2228 list; sentinel at manager+2232; node object at+12',
  evidence='Ghidra registration/*.c and raw-verified registration-instructions.csv; PPC stack shared-pointer copy reviewed',
  scope='startup path established; arbitrary writes and indirect registration callers not excluded'))
write('startup-listener-registration.csv',rows)
families=[]
factory_specs=[
 ('world','820A53D8','82113028','820072D4',2),
 ('world','820A5448','820B6068','82002624',2),
 ('world','820A54B8','820D6C40','8200427C',2),
 ('world','820A5528','820D4928','82003FBC',2),
 ('world','820A5598','820B3620','82002214',2),
 ('camera','820BF180','820D44D8','82003FA4',4),
 ('camera','820BF0D0','820D2578','82003E28',4),
 ('camera','820CEEC8','8217BED0','82012C5C',4)]
for family,factory,ctor,table,slot in factory_specs:
 helper='821A1628' if family=='world' else '8218E7A8'
 ctor_site=next(x['site'] for x in calls if x['function']==factory and x['target']==ctor)
 insertion=next(x['site'] for x in calls if x['function']==factory and x['target']==helper)
 table_value=struct.unpack_from('>I',image,int(table,16)+slot*4-0x82000000)[0]
 label=db.execute('select name from classes where tbl=?',(int(table,16),)).fetchone()[0]
 families.append(dict(family=family,factory=factory,constructor=ctor,table=table,class_name_testimony=label,
  constructor_site=ctor_site,insertion_site=insertion,insertion_helper=helper,frame_slot=slot,frame_target=f'{table_value:08X}',
  destination='manager+44; embedded object+8 node' if family=='world' else 'manager+32 pending list; 821A5A10 moves object+4 node to manager+0',
  status='factory insertion path verified; exhaustive runtime population not proven'))
write('world-camera-factory-registration.csv',families)
core=list(csv.DictReader((OUT/'core-callback-candidates.csv').open(encoding='utf-8-sig')))
for entry in families:
 site='821A51D8' if entry['family']=='world' else '821A5290'
 if not any(r['site']==site and r['table']==entry['table'] for r in core):
  core.append(dict(site=site,receiver='world list' if entry['family']=='world' else 'view list',slot=entry['frame_slot'],
   table=entry['table'],class_name_testimony=entry['class_name_testimony'],candidate_target='sub_'+entry['frame_target'],
   pointer_verified=True,status=entry['status']))
for row in core:
 row['registration_evidence']='not yet traced'
 if row['site'] in ('821A5264','821A5368') and row['table'] in {x[1] for x in ctors}:
  row['registration_evidence']='startup object constructed then inserted via821A68C8/8219EA48; see startup-listener-registration.csv'
  row['status']='startup registration chain and table slot verified; exhaustive runtime receiver population not proven'
 if row['site'] in ('821A5158','821A52A8','821A52E4','821A52F8') and row['table']=='82001EF0':
  row['registration_evidence']='820B1F60 constructs820B1028 and passes shared pointer to821A4980; setter writes manager+132'
  row['status']='startup backend assignment and table slot verified; later replacements not excluded'
 for entry in families:
  if row['table']==entry['table'] and row['site']==('821A51D8' if entry['family']=='world' else '821A5290'):
   row['registration_evidence']=f'factory {entry["factory"]} -> constructor {entry["constructor"]} -> insertion {entry["insertion_site"]}; world-camera-factory-registration.csv'
   row['status']=entry['status']
write('core-callback-candidates.csv',core)
summary=dict(functions=len(functions),raw_verified_instructions=len(instructions),direct_calls=len(calls),startup_listener_registrations=len(rows),world_camera_factory_paths=len(families),
 direct_registration_callers=[x[0] for x in db.execute("select caller from calls where callee='sub_821A68C8'")],
 limitation='Direct-call database is not a proof of absence of indirect callers or unmodeled list writes.')
(OUT/'registration-summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(json.dumps(summary,indent=2))
