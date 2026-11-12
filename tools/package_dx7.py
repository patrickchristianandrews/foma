from pathlib import Path
import base64
import hashlib
import html
import json
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PKG = ROOT / 'build/pocket-wave-package'
DEST = PKG / 'installer/dx7-banks'
REPO = 'mene311/fm1-banks'
API = 'https://api.github.com/repos/' + REPO
HEADERS = {'User-Agent': 'Pocket-Wave-bank-packager', 'Accept': 'application/vnd.github+json'}

def get(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=HEADERS), timeout=60) as r:
        return r.read()

def api(path):
    return json.loads(get(API + path))

def validate(b):
    if len(b) != 4104:
        raise ValueError('Expected 4104 bytes, got ' + str(len(b)))
    if b[:6] != bytes([240, 67, 0, 9, 32, 0]) or b[-1] != 247:
        raise ValueError('Unexpected DX7 bulk-dump header or terminator')
    if any(x > 127 for x in b[6:4103]):
        raise ValueError('Non-7-bit voice data or checksum')
    if ((-sum(b[6:4102])) & 127) != b[4102]:
        raise ValueError('DX7 checksum mismatch')

def names(b):
    return [''.join(chr(c) if 32 <= c < 127 else ' ' for c in b[6 + i*128 + 118:6 + i*128 + 128]).strip() or '(blank)' for i in range(32)]

if not (PKG / 'installer/firmware.fwsc').is_file():
    raise SystemExit('Run package_pocket_wave.py after a successful firmware build first')
meta = api('')
branch = meta['default_branch']
revision = api('/commits/' + branch)['sha']
raw = 'https://raw.githubusercontent.com/' + REPO + '/' + revision + '/'
entries = api('/contents/banks?ref=' + revision)
entries = sorted([x for x in entries if x['type'] == 'file' and x['name'].lower().endswith('.syx')], key=lambda x: x['name'])
if len(entries) != 26:
    raise SystemExit('Upstream bank count changed: expected 26, got ' + str(len(entries)))
DEST.mkdir(parents=True, exist_ok=True)
bundle = []
manifest = []
rows = []
for i, ent in enumerate(entries, 1):
    name = ent['name']
    b = get(raw + 'banks/' + name)
    validate(b)
    (DEST / name).write_bytes(b)
    ns = names(b)
    title = name.removesuffix('.syx').removeprefix('FM-1_').replace('_', ' ').title()
    digest = hashlib.sha256(b).hexdigest()
    manifest.append({'file': name, 'voices': ns, 'sha256': digest, 'bytes': len(b)})
    bundle.append({'file': name.removesuffix('.syx'), 'theme': title, 'names': ns, 'b64': base64.b64encode(b).decode('ascii')})
    rows.append('<article><h2>' + html.escape(title) + '</h2><a download href="' + html.escape(name, quote=True) + '">Download .syx</a><ol>' + ''.join('<li>' + html.escape(n) + '</li>' for n in ns) + '</ol></article>')
    print(str(i) + '/26 validated: ' + name)
for name in ('LICENSE', 'README.md'):
    (DEST / ('UPSTREAM-' + name)).write_bytes(get(raw + name))
(DEST / 'manifest.json').write_text(json.dumps({'repository': REPO, 'revision': revision, 'bank_count': 26, 'voice_entries': 832, 'banks': manifest}, indent=2), encoding='utf-8')
(DEST / 'banks_bundle.json').write_text(json.dumps(bundle), encoding='utf-8')
notice = '''EXTERNAL DX7 BANKS — NOT EMBEDDED IN POCKET WAVE FIRMWARE

26 banks x 32 voices = 832 voice entries, not necessarily 832 unique sounds.
Source: https://github.com/mene311/fm1-banks
Upstream revision: ''' + revision + '''

Upstream MIT terms cover repository tooling, NOT the curated patch data.
Patch authors retain their rights. See UPSTREAM-LICENSE and UPSTREAM-README.md.
Public availability is not a claim of a CC0 or MIT license for these sounds.
Do not assume permission for commercial redistribution.

The Pocket Wave native prototype uses a simple two-operator FM engine.
It CANNOT import, play, or save these DX7 banks. They are companion resources.
Use firmware with documented DX7 import support and a compatible librarian.
Loading patches can overwrite existing slots. Back up patches first.
The included catalog only browses/downloads files: it never sends MIDI.
SHA-256 hashes and upstream revision are recorded in manifest.json.
'''
(DEST / 'READ-ME-FIRST.txt').write_text(notice, encoding='utf-8')
page = '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Pocket Wave — External DX7 Banks</title><style>body{background:#140c09;color:#ffe8d2;font:16px/1.5 system-ui;margin:24px}a{color:#7dffe0}input{padding:12px;width:min(90%,600px);border-radius:10px}main{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:18px}article{background:#2c1811;border:1px solid #6a3828;border-radius:16px;padding:16px}h2{font-size:20px}li{font-family:monospace}aside{background:#432114;padding:16px;border-radius:12px}</style><a href="../index.html#install">Back to FM-1 Installer</a><h1>External DX7 Bank Library</h1><p>26 banks · 832 voice entries. These banks are companion downloads, not sounds in the native Pocket Wave engine.</p><aside>The native prototype cannot play these files. Use DX7-compatible firmware and a compatible librarian. Preserve upstream patch credits; tooling MIT does not license the patch data. <a href="READ-ME-FIRST.txt">Read limitations</a> · <a href="UPSTREAM-LICENSE">Upstream license</a></aside><p><a download href="banks_bundle.json">Download bundle</a> · <a href="manifest.json">Hashes and revision</a></p><input id="q" placeholder="Filter banks or patch names"><main>''' + ''.join(rows) + '''</main><script>document.getElementById('q').oninput=e=>{const q=e.target.value.toLowerCase();document.querySelectorAll('article').forEach(a=>{a.hidden=!a.textContent.toLowerCase().includes(q)})}</script></html>'''
(DEST / 'index.html').write_text(page, encoding='utf-8')
landing = PKG / 'installer/index.html'
s = landing.read_text(encoding='utf-8')
anchor = '<h1>Pocket Wave — FM-1 Installer</h1>'
if anchor not in s:
    raise SystemExit('Installer-first heading missing; refusing an unlinked bank package')
s = s.replace(anchor, anchor + '<p><a href="dx7-banks/index.html">Browse 26 external DX7 banks (832 voice entries)</a> — companion resources; this native prototype cannot play DX7 files.</p>', 1)
landing.write_text(s, encoding='utf-8')
with (PKG / 'START-HERE.txt').open('a', encoding='utf-8') as f:
    f.write('\nEXTERNAL BANKS: installer/dx7-banks/index.html\nIncludes 26 verified .syx banks, 832 voice entries, hashes and upstream credits.\nBanks require DX7-compatible firmware; they do not work in the Pocket Wave prototype.\n')
archive = ROOT / 'build/Pocket-Wave-complete.zip'
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
    for p in sorted(PKG.rglob('*')):
        if p.is_file():
            z.write(p, 'Pocket-Wave/' + str(p.relative_to(PKG)))
print('Packaged 26 verified banks at upstream revision ' + revision)
