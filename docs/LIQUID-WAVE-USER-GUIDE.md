# Liquid Wave FM-1 user guide

This guide covers the Liquid Wave firmware in development for the M-VAVE FM-1. The FM-1 version uses its own synth and screen. The Random Candy Land browser version runs independently.

## Install and recover

1. Save any projects, presets, and samples you want to keep using your current firmware's backup or export tools. Different firmware may use different formats.
2. Use a desktop computer with Chrome or Edge, a USB data cable, and a charged FM-1. Close applications and browser tabs using the synth's MIDI port.
3. Open the Liquid Wave installer. Use its **Emulator** tab to try the controls, then read the **Rescue** tab.
4. Use **FM-1 Installer** and allow MIDI access. Keep the FM-1 powered and plugged in until it restarts. Wait for the wave to finish or press **HOME** or **PLAY** to skip it.
5. If the update is interrupted, follow the installer's Rescue instructions. If the app fails to start, the installer documents USB rescue by holding **OCT-** while powering on. Keep a compatible backup firmware package available.

[Liquid Wave and its installer](https://randomcandyland.com/liquid-wave/)

## Make and play a song

1. Turn **PRESETS** to choose Synthwave, Funk, Ballad, or House. Changing style generates a new song in that style.
2. Press **ARP** for another song. It generates a tempo, key, chord progressions, individual track patterns, and a new 32-bar arrangement.
3. Press **PLAY** to start or stop. Starting playback begins at the arrangement's start. Re-generating during playback starts the new arrangement from the beginning.
4. Turn **KNOB 1** to set the tempo manually. Press **SEL** to mute or unmute the selected part.
5. Press **SAVE** while stopped to keep the current song and its instruments. The firmware loads that project at startup. A new save replaces the stored project.

The sequencer has Intro, Verse, Chorus, Bridge, and Outro sections. Each section has 16 editable steps per track and four bars of chord changes. The eight performers are Beat Bear (drums), Bass Cat (bass), Chord Bunny (chords), Lead Chick (lead), Pad Cloud (pads), Arp Alien (arpeggios), Extra Ghost (accents), and Tidbit (an additional melodic part).

## Edit and perform

| Control | What it does |
| --- | --- |
| **HOME** | Opens the singing stage with three moving waves and all eight performers. |
| **SELECT** dial | Chooses a track. Turning it on the stage also opens Mix. |
| **SEL** button | Toggles mute on the selected track. |
| **SEQ** | Opens the step editor. Press again to toggle looping. Use **KNOB 4** to select and loop a section. |
| **REC** | Toggles overdub. Keys write to the current playback step or, while stopped, to the step assigned to the key. |
| **Keys** | Audition the selected part. On Steps with REC off, they toggle steps. Beat Bear keys cycle kick, snare, closed hat, and open hat. |
| **OCT- / OCT+** | Shift the playing and sequenced pitch by an octave, within two octaves up or down. |
| **EDIT** | Opens Sound Edit. Turn **ALGORITHM** to choose from 36 melodic patches or eight Beat Bear kits. Turn it while stopped to audition the choices. |
| **ENV** | Opens Envelope. Use the tail control to adjust the selected track's decay. |
| **LFO** | Opens Motion. Knob 3 adds motion to melodic tracks. |
| **FX** | Opens the selected track's drive control. |
| **GLO** | Opens Global for the software master level, transposition, and octave. |
| **MASTER** | Physical output volume. |

| Page | Knob 1 | Knob 2 | Knob 3 | Knob 4 |
| --- | --- | --- | --- | --- |
| Stage, Mix, Envelope, Sound Edit | Tempo | Track level | FM depth or drum drive | Decay tail |
| Motion | Tempo | Track level | Motion or drum drive | Decay tail |
| FX | Tempo | Track level | Drive | Decay tail |
| Global | Tempo | Software master | Transposition | Octave |
| Steps | Tempo | Step cursor | Step note or drum combination | Section and loop |

Outside Sound Edit, **ALGORITHM** adjusts the track's FM ratio or Beat Bear's drum drive. Knob tweaks last until power-off or until you change the instrument. Saved songs retain their patterns, tempo, harmonies, arrangement, mutes, selected track, and preset choices.

## Give each section its own sound

1. Open **EDIT**, then press **EDIT** again to open **SECTION SOUND**.
2. Turn **KNOB 1** to choose Intro, Verse, Chorus, Bridge, or Outro. Turn **SELECT** to choose one of the eight tracks.
3. Turn **KNOB 2** to select level, pitch, FM depth or drum drive, decay, FM ratio, or stereo pan. Turn **KNOB 3** to change the selected setting. **KNOB 4** always controls pan, and **ALGORITHM** adjusts the FM ratio on melodic tracks.
4. Play a key to hear the selected section's settings, or press **PLAY** to hear them in the arrangement. Press **SAVE** while stopped to keep all section settings.

Values are offsets from the track's base sound. Set an offset to zero to restore the base sound for that section. A change to the Chorus has no effect on the Verse. Stereo pan is most apparent through stereo headphones.

## Bluetooth MIDI

The Bluetooth MIDI build advertises a MIDI endpoint after startup. Use the Bluetooth MIDI device picker in your host or music app to connect. Incoming note-on messages play the selected sound as a decaying hit. MIDI channel 10 triggers Beat Bear. Play with the panel or the sequencer while connected. The browser emulator does not model the Bluetooth radio.

## Troubleshooting

- If the unit is silent, check the **MASTER** knob, the software master in **GLO**, the selected track's level, and the **SEL** mute light.
- If you hear a repeated section, press **SEQ** again to turn section looping off. **PLAY** restarts the arrangement.
- If you need to preserve an older saved song, audition and document it before replacing the single saved slot. Earlier Liquid Wave projects load with their original harmony rules.
- If an update fails, use the installer's Rescue tab. Do not run an updater while another MIDI application is using the FM-1.
- Native Wi-Fi, live weather and time, destination travel, and weather performance effects remain under development.
