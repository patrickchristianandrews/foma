"""Fetch, validate and describe the external DX7 banks (mene311/fm1-banks).

Library used by package_pocket_wave.py. Offline test mode: set DX7_SOURCE_DIR to a folder of .syx files.
Invalid files are rejected and reported; they never abort the whole package.
"""
import base64, hashlib, html, json, os, urllib.request
from pathlib import Path

REPO = 'mene311/fm1-banks'
API = 'https://api.github.com/repos/' + REPO
HEADERS = {'User-Agent': 'Pocket-Wave-packager', 'Accept': 'application/vnd.github+json'}

def _get(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=HEADERS), timeout=60) as r:
        return r.read()

def validate(b):
    if len(b) != 4104: return 'length %d, expected 4104' % len(b)
    if bytes(b[:6]) != bytes([240, 67, 0, 9, 32, 0]) or b[-1] != 247: return 'unexpected DX7 bulk-dump header or terminator'
    if any(x > 127 for x in b[6:4103]): return 'byte above 127 in voice data or checksum'
    if ((-sum(b[6:4102])) & 127) != b[4102]: return 'checksum mismatch'
    return None

def names(b):
    return [''.join(chr(c) if 32 <= c < 127 else ' ' for c in b[6 + i*128 + 118:6 + i*128 + 128]).strip() or '(blank)' for i in range(32)]

def collect():
    """Return (revision, [(filename, bytes)], license_text, readme_text)."""
    src = os.environ.get('DX7_SOURCE_DIR')
    if src:
        files = [(p.name, p.read_bytes()) for p in sorted(Path(src).glob('*.syx'))]
        return 'local-test', files, 'LOCAL TEST LICENSE', 'LOCAL TEST README'
    meta = json.loads(_get(API))
    revision = json.loads(_get(API + '/commits/' + meta['default_branch']))['sha']
    raw = 'https://raw.githubusercontent.com/' + REPO + '/' + revision + '/'
    entries = json.loads(_get(API + '/contents/banks?ref=' + revision))
    entries = sorted([e for e in entries if e['type'] == 'file' and e['name'].lower().endswith('.syx')], key=lambda e: e['name'])
    files = []
    for e in entries:
        try: files.append((e['name'], _get(raw + 'banks/' + e['name'])))
        except Exception as ex: files.append((e['name'], b''))
    return revision, files, _get(raw + 'LICENSE').decode('utf-8', 'replace'), _get(raw + 'README.md').decode('utf-8', 'replace')

def build(dest, back_link):
    """Write banks, catalog, bundle and notices into dest. Returns a report dict."""
    dest = Path(dest); dest.mkdir(parents=True, exist_ok=True)
    revision, files, lic, readme = collect()
    ok, rejected, manifest, bundle, rows = [], [], [], [], []
    for name, b in files:
        err = validate(b) if b else 'download failed'
        if err: rejected.append({'file': name, 'reason': err}); continue
        (dest / name).write_bytes(b)
        ns = names(b); title = name.removesuffix('.syx').removeprefix('FM-1_').replace('_', ' ').title()
        manifest.append({'file': name, 'voices': ns, 'sha256': hashlib.sha256(b).hexdigest(), 'bytes': len(b)})
        bundle.append({'file': name.removesuffix('.syx'), 'theme': title, 'names': ns, 'b64': base64.b64encode(b).decode('ascii')})
        rows.append('<article><h2>' + html.escape(title) + '</h2><a download href="' + html.escape(name, quote=True) + '">Download .syx</a><ol>' + ''.join('<li>' + html.escape(n) + '</li>' for n in ns) + '</ol></article>')
        ok.append(name)
    (dest / 'UPSTREAM-LICENSE').write_text(lic, encoding='utf-8'); (dest / 'UPSTREAM-README.md').write_text(readme, encoding='utf-8')
    (dest / 'manifest.json').write_text(json.dumps({'repository': REPO, 'revision': revision, 'banks_valid': len(ok), 'banks_rejected': rejected, 'voice_entries': 32 * len(ok), 'banks': manifest}, indent=2), encoding='utf-8')
    (dest / 'banks_bundle.json').write_text(json.dumps(bundle), encoding='utf-8')
    rej = ''.join('\n  - %s: %s' % (r['file'], r['reason']) for r in rejected) or ' none'
    (dest / 'READ-ME-FIRST.txt').write_text(
        'EXTERNAL DX7 BANKS - COMPANION FILES, NOT INSIDE THE POCKET WAVE FIRMWARE\n\n'
        '%d valid banks x 32 voices = %d voice entries (not necessarily unique sounds).\nRejected files:%s\n'
        'Source: https://github.com/%s  revision %s\n\n'
        'Upstream MIT terms cover the repository tooling, NOT the curated patch data.\nPatch authors keep their rights; public availability is not a licence.\n'
        'Do not assume permission for commercial redistribution. See UPSTREAM-LICENSE.\n\n'
        'The Pocket Wave prototype uses a simple two-operator FM engine. It CANNOT import, play or save these banks.\n'
        'Use firmware with DX7 import support and a compatible librarian. Loading patches can overwrite slots: back up first.\n'
        'This catalog only browses and downloads files; it never sends MIDI. SHA-256 hashes: manifest.json.\n'
        % (len(ok), 32 * len(ok), rej, REPO, revision), encoding='utf-8')
    page = ('<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Pocket Wave - External DX7 Banks</title>'
            '<style>body{background:#140c09;color:#ffe8d2;font:16px/1.5 system-ui;margin:24px}a{color:#7dffe0}input{padding:12px;width:min(90%,600px);border-radius:10px}'
            'main{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:18px}article{background:#2c1811;border:1px solid #6a3828;border-radius:16px;padding:16px}'
            'h2{font-size:20px}li{font-family:monospace}aside{background:#432114;padding:16px;border-radius:12px}</style>'
            '<a href="' + back_link + '">Back to the FM-1 Installer</a><h1>External DX7 Bank Library</h1>'
            '<p>%d banks &middot; %d voice entries. Companion downloads, not sounds inside the native Pocket Wave engine.</p>'
            '<aside>The prototype cannot play these files. Use DX7-compatible firmware and a compatible librarian. Tooling MIT does not license the patch data. '
            '<a href="READ-ME-FIRST.txt">Read limitations</a> &middot; <a href="UPSTREAM-LICENSE">Upstream licence</a></aside>'
            '<p><a download href="banks_bundle.json">Download bundle</a> &middot; <a href="manifest.json">Hashes and revision</a></p>'
            '<input id="q" placeholder="Filter banks or patch names"><main>' % (len(ok), 32 * len(ok)) + ''.join(rows) +
            '</main><script>document.getElementById("q").oninput=function(e){var q=e.target.value.toLowerCase();document.querySelectorAll("article").forEach(function(a){a.hidden=a.textContent.toLowerCase().indexOf(q)<0})}</script></html>')
    (dest / 'index.html').write_text(page, encoding='utf-8')
    return {'valid': len(ok), 'rejected': rejected, 'revision': revision}
