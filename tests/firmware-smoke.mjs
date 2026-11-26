import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';

const wasm = await WebAssembly.compile(await readFile(process.argv[2]));
const EDIT = 6, HOME = 8, SAVE = 9, ARP = 10, SEQ = 11, PLAY = 12, REC = 13;
const OCT_UP = 1;
const SELECT = 0, KNOB1 = 2, KNOB4 = 5;
const FRAME_LEN = 240 * 240;

async function device(saved) {
  const panel = (await WebAssembly.instantiate(wasm)).exports;
  if (saved) {
    new Uint8Array(panel.memory.buffer, panel.web_store(), saved.length).set(saved);
    panel.web_store_set_len(saved.length);
  }
  panel.web_boot();
  return {
    frame: () => new Uint16Array(panel.memory.buffer, panel.web_fb(), FRAME_LEN).slice(),
    store: () => new Uint8Array(panel.memory.buffer, panel.web_store(), panel.web_store_len()).slice(),
    advance(ms) {
      let audible = false;
      const due = panel.web_now() + ms;
      while (panel.web_now() < due) {
        panel.web_render(512);
        const left = new Float32Array(panel.memory.buffer, panel.web_out_l(), 512);
        assert(left.every(Number.isFinite), 'finite audio');
        audible ||= left.some(value => value !== 0);
      }
      return audible;
    },
    tap(n) {
      panel.web_buttons(1 << n);
      panel.web_buttons(0);
      this.advance(15);
    },
    hold(n, duration) {
      panel.web_buttons(1 << n);
      this.advance(duration);
      panel.web_buttons(0);
      this.advance(15);
    },
    turn(n, count) {
      panel.web_encoder(n, count);
      this.advance(15);
    },
    audition() {
      panel.web_keys(1);
      panel.web_keys(0);
      this.advance(60);
      const left = new Float32Array(panel.memory.buffer, panel.web_out_l(), 512);
      const right = new Float32Array(panel.memory.buffer, panel.web_out_r(), 512);
      return [left.some(value => value !== 0), right.some(value => value !== 0)];
    },
    led: n => Boolean(panel.web_led_buttons() & (1 << n)),
  };
}

const fm1 = await device();
const startup = fm1.frame();
assert(!fm1.led(HOME));
fm1.advance(350);
assert.notDeepEqual(fm1.frame(), startup, 'boot wave moves across the screen');
fm1.advance(500);
assert(fm1.led(HOME), 'boot enters Stage');
const stage = fm1.frame();
fm1.advance(110);
const waves = fm1.frame();
fm1.advance(1600);
const planeA=fm1.frame();
fm1.advance(400);
const planeB=fm1.frame();
assert.notDeepEqual(planeA.subarray(48*240,90*240),planeB.subarray(48*240,90*240),'banner plane moves across the upper sky');
assert.notDeepEqual(waves.subarray(120*240,170*240),stage.subarray(120*240,170*240),'swimmers animate around foreground waterline');

fm1.turn(SELECT, 7);
fm1.tap(SEQ);
assert(fm1.led(SEQ), 'Steps page opens');
fm1.advance(160);
const tidbit = fm1.frame();
const portraitColors = new Set();
for (let y = 151; y < 169; ++y)
  for (let x = 213; x < 237; ++x) portraitColors.add(tidbit[y*240+x]);
assert(portraitColors.size >= 12, 'Tidbit is a full-color portrait on Steps');
fm1.tap(REC);
assert(fm1.led(REC), 'Record control');
fm1.tap(REC);
fm1.tap(EDIT);
fm1.tap(EDIT);
fm1.turn(KNOB1, 1);
fm1.turn(KNOB4, -100);
assert.deepEqual(fm1.audition(), [true,false], 'edited track pans hard left in Verse');
fm1.advance(2500);
fm1.turn(KNOB1, 1);
assert.deepEqual(fm1.audition(), [true,true], 'the same track stays centered in Chorus');
fm1.tap(SAVE);
const first = fm1.store();
assert.equal(new DataView(first.buffer).getUint32(0,true),12347,'new save contains arrangement and section sound');
assert.equal(new Int8Array(first.buffer)[688 + (8 + 7) * 6 + 5], -100, 'Verse pan persists');
fm1.tap(ARP);
fm1.tap(SAVE);
assert.notDeepEqual(fm1.store(), first, 'New Song replaces the saved composition after SAVE');
fm1.tap(PLAY);
assert(fm1.led(PLAY), 'Play control');
assert(fm1.advance(1000), 'generated song produces audio');
fm1.tap(PLAY);
assert(!fm1.led(PLAY), 'Stop control');

const rebooted = await device(first);
rebooted.tap(HOME);
rebooted.tap(SAVE);
assert.deepEqual(rebooted.store(), first, 'full song and instrument settings survive reboot');
const earlierDraft = first.slice(0,688);
new DataView(earlierDraft.buffer).setUint32(0,12346,true);
const older = await device(earlierDraft);
older.tap(HOME);
older.tap(SAVE);
assert.deepEqual(older.store().slice(4,688), earlierDraft.slice(4), 'prior draft retains arrangement and instruments');
assert(older.store().slice(688).every(value => value === 0), 'prior draft starts with neutral section sounds');
const legacy = first.slice(0, 656);
new DataView(legacy.buffer).setUint32(0,12345,true);
const migrated = await device(legacy);
migrated.tap(HOME);
migrated.tap(SAVE);
assert.deepEqual(migrated.store().slice(4,656), legacy.slice(4), 'legacy steps and presets survive migration');

const weather = await device(first);
weather.advance(850);
weather.tap(PLAY);
assert(weather.led(PLAY), 'Stage starts before switching to Weather');
weather.tap(HOME);
assert(weather.led(PLAY), 'entering Weather lets the Stage song keep playing');
weather.tap(PLAY);
assert(!weather.led(PLAY), 'PLAY stops the Stage song from Weather even without a reading');
weather.advance(3000);
assert(!weather.advance(250), 'Stage playback stays stopped after its sound has decayed');
const weatherHome = weather.frame();
weather.tap(PLAY);
assert(!weather.led(PLAY), 'weather mode waits for live data on Earth');
weather.tap(OCT_UP);
assert.notDeepEqual(weather.frame(), weatherHome, 'OCT+ moves to another destination');
weather.hold(OCT_UP, 3100);
weather.tap(PLAY);
assert(weather.led(PLAY), 'Moon mode plays without weather');
assert(weather.advance(2200), 'Moon mode produces audio');
weather.tap(HOME);
const detailsFrame=weather.frame();
assert(!weather.led(PLAY), 'leaving Moon stops weather playback');
weather.tap(HOME);
assert.notDeepEqual(weather.frame(),detailsFrame,'fourth HOME page is distinct from song details');
const radioFrame=weather.frame();
weather.tap(HOME);
assert.notDeepEqual(weather.frame(),radioFrame,'HOME returns from radio to stage');
weather.tap(SAVE);
assert.deepEqual(weather.store(), first, 'weather travel and playback preserve the saved arrangement');
console.log('FM-1 emulator boot, waves, Tidbit, section mixing, transport, project migration and weather-screen checks passed.');
