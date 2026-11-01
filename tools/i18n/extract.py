#!/usr/bin/env python3
# 3.52: collects every text the player can see (menus, launcher, chat messages, HUD, server messages)
# from the C++ sources into tools/i18n/en.txt (one string per line, C escapes kept as \n \t \\).
# Debug log lines, file names, command ids, ini keys and animation names are left out.
import re, os, glob, json
root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
src = os.path.join(root, 'src')
files = sorted(glob.glob(src + '/*.cpp') + glob.glob(src + '/*.inc') + glob.glob(src + '/ui/*.cpp') + glob.glob(src + '/game/*.cpp') + [src + '/net/server.cpp'])
SKIP_LINE = re.compile(r'logger->|Log\(\s*"|LOGI|LOGE|__android_log|dlsym|dlopen|getenv|fopen|fprintf\(\s*f|strcmp|strcasecmp|strncmp|strstr\(|sscanf|cfg->|Bind\(|PlayAnim|LoadAnim|RequestAnim|GetStringOf|#include|#define|static_assert|aml->|Game::Text\w*Gxt|GetPluginFile|HasMod|ImGui::PushID|BeginChild\("##|InvisibleButton\("##')
LIT = re.compile(r'"((?:[^"\\\n]|\\.)*)"')

def strip_comments(t):
    out = []; i = 0; n = len(t)
    while i < n:
        c = t[i]
        if c == '"' or c == "'":
            q = c; j = i + 1
            while j < n and t[j] != q:
                j += 2 if t[j] == '\\' else 1
            out.append(t[i:j + 1]); i = j + 1
        elif t.startswith('//', i):
            j = t.find('\n', i); i = n if j < 0 else j
        elif t.startswith('/*', i):
            j = t.find('*/', i + 2); seg = t[i:(n if j < 0 else j + 2)]
            out.append('\n' * seg.count('\n')); i = n if j < 0 else j + 2
        else:
            out.append(c); i += 1
    return ''.join(out)

def want(s):
    if not re.search(r'[A-Za-z]{2}', s): return False
    if s.startswith('_Z') or s.startswith('_ZN'): return False                       # C++ symbols
    if ' ' not in s.strip() and re.search(r'[a-z][A-Z]|[A-Za-z]\d|\d[A-Za-z]', s): return False   # CamelCase / code names
    if re.fullmatch(r'[A-Za-z0-9_./\-:%]+', s) and not re.search(r'[A-Z][a-z]', s) and not ' ' in s: return False   # ids, paths, commands
    if re.fullmatch(r'[\w./\-]+\.(png|txt|ttf|ttc|so|mp3|jar|ini|json|ifp|dat|ide|ipl|img|cs|csa|lua|zip|apk|lng|wav|ogg|bin|cfg)', s, re.I): return False
    if s.startswith('##') or s.startswith('%s/') or s.startswith('/system') or s.startswith('http') or s.startswith('com.'): return False
    if re.fullmatch(r'[A-Z0-9_]+', s): return False          # CONSTANTS / anim names in caps
    if re.fullmatch(r'[a-z]+_[a-z_0-9]+', s): return False   # snake ids
    if re.fullmatch(r'[A-Za-z]+_[A-Za-z_0-9]+', s): return False  # Anim_Names
    if '%' in s and re.fullmatch(r'[%\w.\-:|/ ]*', s) and len(re.sub(r'%[-+ #0]*\d*(?:\.\d+)?[hlz]*[diuxXfFeEgGcsp%]', '', s).strip()) < 3: return False
    return True

seen = {}
for f in files:
    t = strip_comments(open(f, encoding='utf-8', errors='ignore').read())
    for ln in t.split('\n'):
        if SKIP_LINE.search(ln): continue
        # adjacent literals ("a" "b") are one runtime string
        for grp in re.finditer(r'(?:"(?:[^"\\\n]|\\.)*"\s*)+', ln):
            parts = LIT.findall(grp.group(0))
            s = ''.join(parts)
            s = re.sub(r'##.*$', '', s)            # ImGui ids: only the part before ## is shown
            if want(s): seen.setdefault(s, os.path.relpath(f, root))
            # dialog lists / tables: every line and every column is drawn on its own too
            if '\\n' in s or '\\t' in s:
                for piece in re.split(r'\\[nt]', s):
                    if want(piece): seen.setdefault(piece, os.path.relpath(f, root))
out = os.path.join(root, 'tools', 'i18n', 'en.txt')
with open(out, 'w', encoding='utf-8') as fo:
    for s in seen: fo.write(s + '\n')
print(len(seen), 'strings,', sum(len(s) for s in seen), 'chars ->', out)
