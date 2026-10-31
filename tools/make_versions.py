#!/usr/bin/env python3
# 3.04.6: README.md changelog -> docs/versions.json (the website's version history). Run before a release.
import json, re, os
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
text = open(os.path.join(root, 'README.md'), encoding='utf-8').read()
cl = text.split('## Changelog', 1)[1]
m = re.search(r'\n## ', cl)
if m: cl = cl[:m.start()]
out, cur = [], None
SUPPORTED = ['3.36', '3.35.2', '3.35.1', '3.35', '3.34', '3.32', '3.31', '3.30', '3.29', '3.28.2', '3.28.1', '3.28', '3.27', '3.26', '3.25', '3.24', '3.23.6', '3.23.5', '3.23.4', '3.23.3', '3.23.2', '3.23.1', '3.23', '3.22.1', '3.22', '3.21.9', '3.21.8', '3.21.7', '3.21.6', '3.21.5', '3.21.4', '3.21.3', '3.21.2', '3.21.1', '3.21', '3.11.6', '3.11.5', '3.11.4', '3.11.3', '3.11.2', '3.11.1', '3.11', '3.10', '3.04.6']   # 3.11 (3.10: versions that can still play (and be downloaded); the rest are blocked
for line in cl.splitlines():
    h = re.match(r'^- \*\*([0-9][0-9.]*)\*\*:?\s*(.*)$', line)
    if h:
        cur = {'v': h.group(1), 'summary': h.group(2).strip(), 'items': []}
        out.append(cur); continue
    if cur is None: continue
    s = line.strip()
    if not s: continue
    if s.startswith('- '):
        cur['items'].append(s[2:].strip())
    elif cur['items']:
        cur['items'][-1] += ' ' + s
    else:
        cur['summary'] = (cur['summary'] + ' ' + s).strip()
json.dump({'latest': out[0]['v'] if out else '', 'supported': SUPPORTED, 'versions': out}, open(os.path.join(root, 'docs', 'versions.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=0)
print(len(out), 'versions, latest', out[0]['v'] if out else '-')
