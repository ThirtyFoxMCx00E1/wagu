#!/usr/bin/env python3
"""Add / replace a PNG in src/ui/embedded_assets.h:  python3 tools/embed_asset.py assets/MP/rp_hud.png"""
import sys, os, re
H = os.path.join(os.path.dirname(__file__), '..', 'src', 'ui', 'embedded_assets.h')
for path in sys.argv[1:]:
    name = os.path.basename(path); sym = 'k_emb_' + re.sub(r'\W', '_', name)
    data = open(path, 'rb').read()
    s = open(H).read()
    s = re.sub(r'static const uint8_t %s\[\d+\] = \{.*?\n\};\n' % sym, '', s, flags=re.S)
    s = re.sub(r'    \{ "%s", .*?\},\n' % re.escape(name), '', s)
    rows = [','.join(str(b) for b in data[i:i+24]) + ',' for i in range(0, len(data), 24)]
    arr = 'static const uint8_t %s[%d] = {\n    %s\n};\n' % (sym, len(data), '\n    '.join(rows))
    s = s.replace('struct EmbeddedAsset {', arr + 'struct EmbeddedAsset {', 1)
    s = s.replace('static const EmbeddedAsset k_EmbeddedAssets[] = {\n', 'static const EmbeddedAsset k_EmbeddedAssets[] = {\n    { "%s", %s, %d },\n' % (name, sym, len(data)), 1)
    open(H, 'w').write(s)
    print('embedded', name, len(data))
