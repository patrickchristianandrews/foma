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
fm1.tap(SAVE);
const beforeSeqPreset=fm1.store();
fm1.turn(1,1);
fm1.tap(SAVE);
assert.notEqual(fm1.store()[648+7],beforeSeqPreset[648+7],'SEQ ALGORITHM selects actual character instrument');
fm1.advance(2500);
fm1.tap(5); // LFO / Motion
fm1.advance(160);
const motionPage=fm1.frame();
fm1.tap(4); // ENV opens the same editor
fm1.advance(160);
assert.deepEqual(fm1.frame(),motionPage,'ENV and LFO share one motion editor');
fm1.tap(EDIT); // compatibility shortcut to same page
assert(fm1.led(5)&&fm1.led(4),'motion and envelope LEDs indicate shared editor');
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
assert(weather.led(PLAY), 'offline Earth weather music starts without Wi-Fi');
assert(weather.advance(1500), 'offline weather produces audio');
weather.tap(PLAY);
assert(!weather.led(PLAY), 'offline weather stops');
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
assert.notDeepEqual(weather.frame(),radioFrame,'fifth HOME screen is Song Library');
weather.tap(PLAY);
assert(weather.led(PLAY),'catalog song starts');
assert(weather.advance(1800),'catalog source notes produce audio');
weather.tap(PLAY);
assert(!weather.led(PLAY),'catalog stops');
weather.tap(HOME);
assert(!weather.led(PLAY),'HOME returns from library to stage');
weather.tap(SAVE);
assert.deepEqual(weather.store(), first, 'weather travel and playback preserve the saved arrangement');
// Third HOME screen: real instrument presets and a track-only remix.
const lab=await device(first);
lab.advance(850);
lab.tap(HOME);
lab.tap(HOME);
lab.tap(SAVE);
const beforeLab=lab.store();
lab.turn(SELECT,1);
lab.turn(1,1); // ALGORITHM
lab.tap(SAVE);
const afterInstrument=lab.store();
const selected=afterInstrument[6];
assert.notEqual(afterInstrument[648+selected],beforeLab[648+selected],'character instrument preset changes and saves on Song Lab');
lab.turn(KNOB4,3); // CLASSIC -> TRACK
lab.tap(ARP);
lab.tap(SAVE);
const remixed=lab.store();
let changed=0;
for(let part=0;part<5;part++)for(let ch=0;ch<8;ch++)for(let n=0;n<16;n++){
 const index=8+(part*8+ch)*16+n;
 if(ch===selected)changed+=Number(remixed[index]!==afterInstrument[index]);
 else assert.equal(remixed[index],afterInstrument[index],'track remix leaves other characters unchanged');
}
assert(changed>0,'track remix changes selected character pattern');
assert.equal(remixed[5],afterInstrument[5],'track remix preserves tempo');
assert.deepEqual(remixed.slice(656,685),afterInstrument.slice(656,685),'track remix preserves harmony');
// Individual pattern regeneration preserves every other byte of the project.
lab.turn(KNOB4,3); // TRACK -> ONE SEQUENCE
lab.tap(OCT_UP); // Verse
lab.tap(SAVE);
const preOne=lab.store();
lab.tap(ARP);
lab.tap(SAVE);
const postOne=lab.store();
const targetStart=8+(8+selected)*16;
assert.notDeepEqual(postOne.slice(targetStart,targetStart+16),preOne.slice(targetStart,targetStart+16),'one sequence changes');
assert.deepEqual(postOne.slice(0,targetStart),preOne.slice(0,targetStart),'earlier patterns and settings preserved');
assert.deepEqual(postOne.slice(targetStart+16),preOne.slice(targetStart+16),'later patterns, harmony, instruments and mix preserved');
lab.tap(SEQ);
lab.turn(KNOB4,2); // Chorus from initial Intro
lab.tap(SAVE);
const preSeq=lab.store();
lab.tap(ARP);
lab.tap(SAVE);
const postSeq=lab.store();
const seqStart=8+(2*8+selected)*16;
assert.notDeepEqual(postSeq.slice(seqStart,seqStart+16),preSeq.slice(seqStart,seqStart+16),'SEQ ARP regenerates selected pattern directly');
assert.deepEqual(postSeq.slice(0,seqStart),preSeq.slice(0,seqStart));
assert.deepEqual(postSeq.slice(seqStart+16),preSeq.slice(seqStart+16));
console.log('FM-1 emulator boot, waves, Tidbit, section mixing, transport, project migration and weather-screen checks passed.');
