"""Check all static atlas links without requiring a browser."""
from pathlib import Path
import html
import json
import re
from urllib.parse import unquote

root=Path(__file__).resolve().parents[1]/'out/renderer-atlas'
errors=[];count=0
pages=sorted(list(root.glob('*.html'))+list((root/'functions').glob('*.html')))
for page in pages:
    for href in re.findall(r'href="([^"]+)"',page.read_text(encoding='utf-8')):
        href=html.unescape(unquote(href.split('#')[0]))
        if not href or '://' in href:continue
        count+=1
        if not (page.parent/href).exists():errors.append(dict(page=str(page),href=href))
report=dict(passed=not errors,pages=len(pages),links=count,errors=errors)
(root/'link-audit.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
if errors:raise SystemExit(1)
