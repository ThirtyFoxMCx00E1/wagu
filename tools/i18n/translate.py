#!/usr/bin/env python3
# 3.52: translates tools/i18n/en.txt into every MP language -> assets/MP/lang/<code>.lng
# Machine translation (Google Translate, the free web endpoint), with a cache in tools/i18n/cache/<code>.json
# so a new version only sends the new strings. Things that must stay as they are - printf specs (%d %s %.2f),
# {RRGGBB} colours, ~y~ game text codes, \n \t, /commands, [TAGS] - are swapped for tokens first; a string
# whose tokens don't come back unchanged stays English. Arabic is shaped + put in visual order (the game
# draws left to right, no shaping).
import re, os, sys, json, time, urllib.request, urllib.parse, concurrent.futures as cf
root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
here = os.path.join(root, 'tools', 'i18n')
LANGS = [  # code, google code, native name, English name, font (phone font needed), rtl
    ('es', 'es', 'Español', 'Spanish', '', 0), ('fil', 'tl', 'Filipino', 'Filipino', '', 0), ('id', 'id', 'Bahasa Indonesia', 'Indonesian', '', 0),
    ('pt-BR', 'pt', 'Português (Brasil)', 'Portuguese (Brazil)', '', 0), ('ar', 'ar', 'العربية', 'Arabic', '', 1), ('ko', 'ko', '한국어', 'Korean', 'cjk-kr', 0),
    ('ru', 'ru', 'Русский', 'Russian', '', 0), ('zh-CN', 'zh-CN', '简体中文', 'Chinese (Simplified)', 'cjk-sc', 0), ('vi', 'vi', 'Tiếng Việt', 'Vietnamese', '', 0),
    ('ja', 'ja', '日本語', 'Japanese', 'cjk-jp', 0), ('fr', 'fr', 'Français', 'French', '', 0), ('de', 'de', 'Deutsch', 'German', '', 0),
    ('it', 'it', 'Italiano', 'Italian', '', 0), ('tr', 'tr', 'Türkçe', 'Turkish', '', 0), ('pl', 'pl', 'Polski', 'Polish', '', 0),
    ('th', 'th', 'ไทย', 'Thai', 'thai', 0), ('ms', 'ms', 'Bahasa Melayu', 'Malay', '', 0), ('uk', 'uk', 'Українська', 'Ukrainian', '', 0),
    ('nl', 'nl', 'Nederlands', 'Dutch', '', 0), ('zh-TW', 'zh-TW', '繁體中文', 'Chinese (Traditional)', 'cjk-tc', 0),
]
TOK = re.compile(r'%[-+ #0]*\d*(?:\.\d+)?[hlz]*[diuxXfFeEgGcsp]|%%|\{[0-9A-Fa-f]{6}\}|~[a-zA-Z]~|\\[nt"\\]|\\x[0-9A-Fa-f]{2}|(?<![\w/])/[a-z][\w]*|\[[A-Z][A-Z0-9 ]*\]|https?://\S+|\b[\w.-]+\.(?:txt|png|so|zip|apk|mptd|pwn|lng|ttf|ini|json)\b|files/MP\S*')

def protect(s):
    toks = []
    def rep(m): toks.append(m.group(0)); return '{%d}' % (len(toks) - 1)
    return TOK.sub(rep, s), toks

def restore(t, toks):
    found = re.findall(r'\{\s*(\d+)\s*\}', t)
    if sorted(int(x) for x in found) != list(range(len(toks))): return None
    return re.sub(r'\{\s*(\d+)\s*\}', lambda m: toks[int(m.group(1))], t)

def gtx(text, tl):
    try: return gtx_html(text, tl)
    except Exception: pass
    return gtx_single(text, tl)

def gtx_html(text, tl):
    # the batch endpoint the browser translate uses (one line = one item; it reads HTML, so < > & are escaped)
    import html
    lines = text.split('\n')
    body = json.dumps([[[html.escape(l, quote=False) for l in lines], 'en', tl], 'te_lib']).encode()
    for k in range(4):
        try:
            req = urllib.request.Request('https://translate-pa.googleapis.com/v1/translateHtml', data=body, headers={'Content-Type': 'application/json+protobuf', 'X-Goog-API-Key': 'AIzaSyATBXajvzQLTDHEQbcpq0Ihe0vWDHmO520', 'User-Agent': 'Mozilla/5.0'})
            d = json.loads(urllib.request.urlopen(req, timeout=60).read().decode('utf-8'))
            out = [html.unescape(x) for x in d[0]]
            if len(out) == len(lines): return '\n'.join(o.replace('\n', ' ') for o in out)
        except Exception:
            time.sleep(2 + k * 3)
    raise RuntimeError('translate failed')

def gtx_single(text, tl):
    q = urllib.parse.urlencode({'client': 'gtx', 'sl': 'en', 'tl': tl, 'dt': 't', 'q': text})
    for k in range(6):
        try:
            req = urllib.request.Request('https://translate.googleapis.com/translate_a/single', data=q.encode(), headers={'User-Agent': 'Mozilla/5.0', 'Content-Type': 'application/x-www-form-urlencoded;charset=UTF-8'})
            d = json.loads(urllib.request.urlopen(req, timeout=40).read().decode('utf-8'))
            return ''.join(seg[0] for seg in d[0] if seg and seg[0])
        except Exception as e:
            time.sleep(2 + k * 3)
    raise RuntimeError('translate failed')

def run_lang(L, strings):
    code, tl, native, eng, font, rtl = L
    cpath = os.path.join(here, 'cache', code + '.json')
    cache = json.load(open(cpath, encoding='utf-8')) if os.path.exists(cpath) else {}
    if OFFLINE or code == 'zh-TW' and ZH_TW_FROM_CN: todo = []   # only rewrite the .lng from the cache
    else: todo = [s for s in strings if s not in cache]
    batch, size = [], 0
    def flush(b):
        if not b: return
        prot = [protect(s) for s in b]
        out = gtx('\n'.join(p for p, _ in prot), tl).split('\n')
        if len(out) != len(b):
            if len(b) == 1: out = [' '.join(out)]
            else:
                h = len(b) // 2; flush(b[:h]); flush(b[h:]); return
        for s, (p, toks), t in zip(b, prot, out):
            r = restore(t.strip(), toks)
            if r is not None and r:
                lead = s[:len(s) - len(s.lstrip())]; trail = s[len(s.rstrip()):]
                r = lead + r.strip() + trail
                if s.lstrip()[:1].isupper() and r.lstrip()[:1].islower(): i = len(lead); r = r[:i] + r[i].upper() + r[i + 1:]
            cache[s] = r if r else s
    def safe(b):
        try: flush(b)
        except RuntimeError: pass   # the service said no: those stay English, run again later for the rest
    for s in todo:
        if size + len(s) > 3500: safe(batch); batch, size = [], 0; json.dump(cache, open(cpath, 'w', encoding='utf-8'), ensure_ascii=False)
        batch.append(s); size += len(s) + 1
    safe(batch)
    json.dump(cache, open(cpath, 'w', encoding='utf-8'), ensure_ascii=False)
    write_lng(L, strings, cache)
    same = sum(1 for s in strings if cache.get(s, s) == s)
    return code, len(todo), same

_artok = re.compile(r'%(?:%|[-+ #0]*[0-9]*(?:\.[0-9]+)?[hlzjt]*[diuxXfFeEgGscp])|\{[0-9A-Fa-f]{6}\}|\\x[0-9A-Fa-f]{2}|\\.')
def visual_ar(t):
    import arabic_reshaper
    from bidi.algorithm import get_display
    # every \n line on its own (the game splits lines before drawing); format specs, colour codes and escapes
    # become one private-use character each so the reordering can't flip them ("%d" -> "d%")
    def one(x):
        toks = []
        def sub(m): toks.append(m.group(0)); return chr(0xF0000 + len(toks) - 1)
        y = get_display(arabic_reshaper.reshape(_artok.sub(sub, x)))
        return re.sub('[\U000F0000-\U000F00FF]', lambda m: toks[ord(m.group(0)) - 0xF0000], y)
    return '\\n'.join(one(x) for x in t.split('\\n'))

_spec = re.compile(r'%(?:%|[-+ #0]*[0-9]*(?:\.[0-9]+)?[hlzjt]*[diuxXfFeEgGscp])')
def specs(s):
    return [m for m in _spec.findall(s) if m != '%%']

def write_lng(L, strings, cache):
    code, tl, native, eng, font, rtl = L
    os.makedirs(os.path.join(root, 'assets', 'MP', 'lang'), exist_ok=True)
    with open(os.path.join(root, 'assets', 'MP', 'lang', code + '.lng'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('#mp-lang 1\n#code=%s\n#name=%s\n#english=%s\n#font=%s\n#rtl=%d\n' % (code, native if not rtl else visual_ar(native), eng, font, rtl))
        for s in strings:
            t = cache.get(s, s)
            if t == s or specs(t) != specs(s): continue   # a format must keep its specs in order (printf safety)
            if '\t' in t or '\n' in t: continue
            if rtl:
                t = visual_ar(t)
                if specs(t) != specs(s): continue
            f.write(s + '\t' + t + '\n')

OFFLINE = '--offline' in sys.argv
ZH_TW_FROM_CN = '--zh-tw-from-cn' in sys.argv   # Traditional Chinese = OpenCC s2twp of the Simplified cache

if __name__ == '__main__':
    if ZH_TW_FROM_CN:
        import opencc
        cc = opencc.OpenCC('s2twp')
        src = json.load(open(os.path.join(here, 'cache', 'zh-CN.json'), encoding='utf-8'))
        json.dump({k: cc.convert(v) for k, v in src.items()}, open(os.path.join(here, 'cache', 'zh-TW.json'), 'w', encoding='utf-8'), ensure_ascii=False)
    strings = [l.rstrip('\n') for l in open(os.path.join(here, 'en.txt'), encoding='utf-8') if l.strip()]
    os.makedirs(os.path.join(here, 'cache'), exist_ok=True)
    only = [a for a in sys.argv[1:] if not a.startswith('--')]
    langs = [L for L in LANGS if not only or L[0] in only]
    with cf.ThreadPoolExecutor(4) as ex:
        for code, n, same in ex.map(lambda L: run_lang(L, strings), langs):
            print('%-6s new %4d, kept English %4d' % (code, n, same), flush=True)
    json.dump([{'code': L[0], 'name': L[2], 'english': L[3], 'font': L[4], 'rtl': L[5]} for L in LANGS], open(os.path.join(here, 'languages.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=1)
