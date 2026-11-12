#!/usr/bin/env python3
"""Build the complete Pocket Wave package (run after tools/build.py has produced build/web/firmware.fwsc).

Output: build/Pocket-Wave-complete.zip, build/Pocket-Wave.fwsc, build/RELEASE-NOTES.md
Order inside the ZIP: START-HERE, installer/ (FM-1 Installer is the first tab and default screen), source/, browser-toy/.
Fails loudly if the firmware is missing or the installer markup is not what we expect.
"""
import hashlib, json, os, re, shutil, sys, urllib.request, zipfile
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import package_dx7

ROOT = Path(os.environ.get('POCKET_ROOT', Path(__file__).resolve().parents[1]))
WEB = ROOT / 'build/web'
OUT = ROOT / 'build'
PKG = OUT / 'pocket-wave-package'
TOY_URL = 'https://raw.githubusercontent.com/patrickchristianandrews/RandomCandyLand.com/main/public/pocket-wave/index.html'
OWNER_REPO = 'https://github.com/patrickchristianandrews/foma'

def die(msg): raise SystemExit('PACKAGING STOPPED: ' + msg)
def sub1(pattern, repl, s, flags=0, what=''):
    s2, n = re.subn(pattern, lambda m: repl, s, count=1, flags=flags)
    if n != 1: die('installer markup changed, could not find ' + (what or pattern))
    return s2

fw = WEB / 'firmware.fwsc'
if not fw.is_file() or fw.stat().st_size < 1024: die('no compiled firmware.fwsc in build/web (build failed or incomplete)')

# ---- installer page: FM-1 Installer first, default screen ----
html = (WEB / 'index.html').read_text(encoding='utf-8')
html = sub1(r'<a href="#emulator" data-tab="emulator">Emulator</a>\s*<a href="#install" data-tab="install">Install</a>',
            '<a href="#install" data-tab="install">FM-1 Installer</a>\n        <a href="#emulator" data-tab="emulator">Emulator</a>', html, what='navigation links')
marks = {k: re.search(r'[ \t]*<!--\s*-+\s*' + k + r'\s*-->', html) for k in ('emulator', 'install', 'rescue')}
if not all(marks.values()): die('section markers not found')
e, i, r = (marks[k].start() for k in ('emulator', 'install', 'rescue'))
if not (e < i < r): die('unexpected section order')
end = html.index('</main>', r)
html = html[:e] + html[i:r] + html[e:i] + html[r:end] + html[end:]
html = sub1(r'<section id="tab-install" class="tab"\s+hidden>', '<section id="tab-install" class="tab">', html, what='install section')
html = sub1(r'<section id="tab-emulator" class="tab">', '<section id="tab-emulator" class="tab" hidden>', html, what='emulator section')
html = html.replace('https://github.com/konsumer/foma', OWNER_REPO)
heading = ('<h1>Pocket Wave &mdash; FM-1 Installer</h1>'
           '<p><strong>Experimental build, not tested on a real FM-1.</strong> Back up your device and try the Emulator tab first. '
           'This is the native prototype: it does not contain the full browser Studio or play the DX7 banks.</p>'
           '<p><a href="firmware.fwsc" download>Download the firmware file</a> &middot; <a href="CONTROLS.txt">Controls and limits</a> &middot; '
           '<a href="dx7-banks/index.html">Browse the DX7 bank library</a> &middot; <a href="../browser-toy/index.html">Open the browser toy</a></p>')
html = sub1(r'<h1>\s*Install on the FM-1\s*</h1>', heading, html, what='installer heading')
(WEB / 'index.html').write_text(html, encoding='utf-8')
js = (WEB / 'app.js').read_text(encoding='utf-8')
js = sub1(r"location\.hash \|\| '#emulator'", "location.hash || '#install'", js, what='default tab')
(WEB / 'app.js').write_text(js, encoding='utf-8')

(WEB / 'CONTROLS.txt').write_text(
'POCKET WAVE - EXPERIMENTAL NATIVE FM-1 PROTOTYPE (not tested on hardware)\n\n'
'PLAY start/stop   REC toggle overdub   SELECT knob: channel   PRESETS knob: style (Synth, Funk, Ballad, House)\n'
'KNOB1 tempo   SEL mute selected channel   ARP regenerate   SEQ next section   SAVE project (only while stopped)\n'
'Keys play the selected channel; with REC on they write its pattern. On the drum channel keys cycle kick, snare, hat, open hat.\n\n'
'8 channels, 5 sections, a fixed 32-bar arrangement, simple two-operator FM voices and synthesised drums, eight floating colour faces.\n'
'Not included: DX7 playback, the 35-sound library, the full browser Studio, USB audio, Bluetooth, MIDI clock.\n'
'Back up your device and read the Rescue tab before installing.\n', encoding='utf-8')

# ---- assemble package ----
if PKG.exists(): shutil.rmtree(PKG)
PKG.mkdir(parents=True)
shutil.copytree(WEB, PKG / 'installer')
(PKG / 'source').mkdir()
shutil.copytree(ROOT / 'src', PKG / 'source/src')
shutil.copy2(ROOT / 'firmware.json', PKG / 'source/firmware.json')
for n in ('LICENSE', 'LICENSE.md', 'LICENSING.md', 'THIRD_PARTY.md'):
    if (ROOT / n).is_file(): shutil.copy2(ROOT / n, PKG / n)

# ---- DX7 banks ----
rep = package_dx7.build(PKG / 'installer/dx7-banks', '../index.html#install')

# ---- browser toy (privacy-clean copy without site analytics) ----
toy_note = 'not included (could not be downloaded)'
try:
    src = os.environ.get('TOY_SOURCE')
    toy = Path(src).read_text(encoding='utf-8') if src else urllib.request.urlopen(urllib.request.Request(TOY_URL, headers=package_dx7.HEADERS), timeout=60).read().decode('utf-8')
    toy = re.sub(r'<script async src="https://www\.googletagmanager\.com/gtag/js[^"]*"></script>\s*', '', toy)
    toy = re.sub(r'<script src="/analytics\.js[^"]*"></script>\s*', '', toy)
    toy = re.sub(r'<!-- Google Tag Manager -->.*?<!-- End Google Tag Manager -->\s*', '', toy, flags=re.S)
    toy = re.sub(r'<!-- Google Tag Manager \(noscript\) -->.*?<!-- End Google Tag Manager \(noscript\) -->\s*', '', toy, flags=re.S)
    toy = toy.replace('href="/"', 'href="../installer/index.html"').replace('href="/icon.svg"', 'href="data:,"').replace('href="/apple-touch-icon.png"', 'href="data:,"')
    if 'googletagmanager' in toy or 'analytics.js' in toy: raise ValueError('analytics tags still present')
    (PKG / 'browser-toy').mkdir(); (PKG / 'browser-toy/index.html').write_text(toy, encoding='utf-8'); toy_note = 'included (browser-toy/index.html)'
except Exception as ex:
    toy_note = 'not included: %s' % ex

sha = hashlib.sha256(fw.read_bytes()).hexdigest()
rev = os.environ.get('GITHUB_SHA', 'local')
(PKG / 'BUILD-REVISION.txt').write_text('source commit: %s\nfirmware sha256: %s\nDX7 banks valid: %d, rejected: %d\nbrowser toy: %s\n' % (rev, sha, rep['valid'], len(rep['rejected']), toy_note), encoding='utf-8')
(PKG / 'START-HERE.txt').write_text(
'POCKET WAVE - COMPLETE PACKAGE (experimental, not tested on a real FM-1)\n\n'
'1. Open a terminal in the installer folder and run:  python3 -m http.server 8000   (Windows: py -m http.server 8000)\n'
'2. Open http://localhost:8000 in Chrome or Edge. The FM-1 Installer is the first tab and the default screen.\n'
'3. Try the Emulator tab first. Back up your FM-1, read the Rescue tab, then press Install. Nothing is installed until you click it.\n'
'Do not double-click index.html: fetch, audio and MIDI need a web address.\n\n'
'Contents\n  installer/            FM-1 installer, emulator, rescue tools, firmware.fwsc\n'
'  installer/dx7-banks/  %d external DX7 banks (companion files; the prototype cannot play them)\n'
'  source/               firmware source and configuration\n  browser-toy/          the in-browser song toy (%s)\n\n'
'Verify the firmware: sha256 %s\nSee BUILD-REVISION.txt and installer/dx7-banks/READ-ME-FIRST.txt.\n' % (rep['valid'], toy_note, sha), encoding='utf-8')

zpath = OUT / 'Pocket-Wave-complete.zip'
with zipfile.ZipFile(zpath, 'w', zipfile.ZIP_DEFLATED) as z:
    z.write(PKG / 'START-HERE.txt', 'Pocket-Wave/START-HERE.txt')   # first entry
    for p in sorted(PKG.rglob('*')):
        if p.is_file() and p.name != 'START-HERE.txt': z.write(p, 'Pocket-Wave/' + str(p.relative_to(PKG)))
shutil.copy2(fw, OUT / 'Pocket-Wave.fwsc')
(OUT / 'RELEASE-NOTES.md').write_text(
'## Pocket Wave (experimental)\n\n**Not tested on a real FM-1.** Back up your device first; the installer includes rescue tools.\n\n'
'- `Pocket-Wave-complete.zip`: FM-1 Installer (first tab), emulator, rescue tools, firmware, source, %d external DX7 banks, browser toy.\n'
'- `Pocket-Wave.fwsc`: the firmware file alone.\n\nThe DX7 banks are companion files; the prototype cannot play them. Banks rejected by validation: %d.\n\n'
'firmware sha256: `%s`\nsource commit: `%s`\n' % (rep['valid'], len(rep['rejected']), sha, rev), encoding='utf-8')
print('OK', zpath, 'banks valid:', rep['valid'], 'rejected:', len(rep['rejected']), '| toy:', toy_note)
