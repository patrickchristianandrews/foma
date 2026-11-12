from pathlib import Path
import shutil
import zipfile

root = Path(__file__).resolve().parents[1]
web = root / 'build/web'
if not (web / 'firmware.fwsc').is_file():
    raise SystemExit('No compiled firmware.fwsc: refusing to package an incomplete build')
html = web / 'index.html'
s = html.read_text()
a = '        <a href="#emulator" data-tab="emulator">Emulator</a>\n        <a href="#install" data-tab="install">Install</a>'
b = '        <a href="#install" data-tab="install">FM-1 Installer</a>\n        <a href="#emulator" data-tab="emulator">Emulator</a>'
if a not in s:
    raise SystemExit('Unexpected installer markup; review layout before packaging')
s = s.replace(a, b).replace('https://github.com/konsumer/foma" target=', 'https://github.com/patrickchristianandrews/foma" target=')
start = s.index('      <!-- ---------------------------------------------------------------- emulator -->')
middle = s.index('      <!-- ---------------------------------------------------------------- install -->')
end = s.index('      <!-- ---------------------------------------------------------------- rescue -->')
s = s[:start] + s[middle:end] + s[start:middle] + s[end:]
s = s.replace('<section id="tab-install" class="tab" hidden>', '<section id="tab-install" class="tab">').replace('<section id="tab-emulator" class="tab">', '<section id="tab-emulator" class="tab" hidden>')
s = s.replace('<h1>Install on the FM-1</h1>', '<h1>Pocket Wave — FM-1 Installer</h1><p><strong>Experimental build, not hardware-tested.</strong> Back up your device and test the Emulator before installing. This native prototype does not contain the full web Studio or its DX7 bank collection.</p><p><a href="firmware.fwsc" download>Download compiled FM-1 firmware</a> · <a href="CONTROLS.txt">Controls and limitations</a></p>')
html.write_text(s)
app = web / 'app.js'
s = app.read_text()
if "location.hash || '#emulator'" not in s:
    raise SystemExit('Unexpected tab selection code')
app.write_text(s.replace("location.hash || '#emulator'", "location.hash || '#install'"))
controls = '''POCKET WAVE — EXPERIMENTAL NATIVE FM-1 PROTOTYPE

PLAY: start/stop. REC: toggle overdub.
SELECT encoder: channel. PRESETS encoder: generated style.
KNOB1: tempo. SEL: mute selected channel. ARP: regenerate patterns.
SEQ: next section. SAVE: save project while stopped.
Keys play the selected channel; with REC enabled they write its pattern.
Drum channel: keys cycle kick/snare/hat/open hat.

8 channels, five sections, fixed 32-bar arrangement, four styles,
simple two-operator FM synthesis, synthesized drums, overlapping color faces.
No external DX7 banks, full browser Studio, 35-sound library, USB audio,
Bluetooth or external MIDI clock in this prototype.

No hardware test has been performed. Successful compilation does not establish
real-time performance, audio quality, or safe operation on your device.
Back up existing device data before installing. Read Rescue first.
'''
(web / 'CONTROLS.txt').write_text(controls)
pkg = root / 'build/pocket-wave-package'
if pkg.exists():
    shutil.rmtree(pkg)
pkg.mkdir()
shutil.copytree(web, pkg / 'installer')
shutil.copytree(root / 'src', pkg / 'source/src')
shutil.copy2(root / 'firmware.json', pkg / 'source/firmware.json')
for name in ('LICENSE', 'LICENSE.md', 'LICENSING.md', 'THIRD_PARTY.md'):
    if (root / name).is_file():
        shutil.copy2(root / name, pkg / name)
(pkg / 'START-HERE.txt').write_text('POCKET WAVE PACKAGE\n\nThe installer is the first tab and default screen.\nTo serve locally: cd installer; python3 -m http.server 8000\nThen open http://localhost:8000 in Chrome or Edge.\nDo not open index.html with file://: fetch, audio and MIDI may fail.\nTest Emulator first. No firmware is installed until you explicitly click Install.\nThis package includes the native prototype and emulator, not the separate full web Studio.\nFull framework/source: https://github.com/patrickchristianandrews/foma\nBuild revision is in BUILD-REVISION.txt.\n')
import os
(pkg / 'BUILD-REVISION.txt').write_text(os.environ.get('GITHUB_SHA', 'local') + '\n')
archive = root / 'build/Pocket-Wave-complete.zip'
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
    for p in sorted(pkg.rglob('*')):
        if p.is_file():
            z.write(p, 'Pocket-Wave/' + str(p.relative_to(pkg)))
print(str(archive))
