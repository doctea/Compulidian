# Compulidian

Made a start porting my euclidean rhythm generator + modulation libraries to a [Music Thing Workshop Computer](https://github.com/tomWhitwell/Workshop_Computer) card.

https://github.com/doctea/Compulidian
- Direct firmware download (UF2): https://github.com/doctea/Compulidian/raw/refs/heads/main/firmware/firmware.uf2

Its a platformio project for vscode, with Arduino and the earlephilhower RP2040 core.

- Uses my [parameters](https://github.com/doctea/parameters), [midihelpers](https://github.com/doctea/midihelpers) and [seqlib](https://github.com/doctea/seqlib) libraries.
- Uses (patched) [uClock](https://github.com/midilab/uClock) for clock generation.
- Sample player adapted from [rheslip/Raspberry-Pi-Pico-Eurorack-Drum-Sample-Player](https://github.com/rheslip/Raspberry-Pi-Pico-Eurorack-Drum-Sample-Player).

See `platformio.ini` `build_flags` and `include/Config.h` for some settings.

## Basic features

- Multiple independent Euclidean tracks for kick, snare, hi-hat, etc etc (actually ~16 tracks internally).
- Plays audio samples out of Audio Out 1 (currently an 808 kit).
- Currently 4 gate outputs (via Computer's 2x pulse and 2x cv outputs) are fed from kick, snare and 2x hi-hat tracks
- Outputs a clock gate (16th notes) on Audio Out 2.
- Defaults to a four-on-the-floor style pattern
- Tracks are split into two groups, each group has its own 'global density' parameter controlled by Main knob and X knob, and modulated via CV1 and CV2.  Go from none to a very hectic number of pulses.
- Last bar of every 4-bar phrase plays a variation fill.
- At start of every phrase (4 bars), pattern resets to the default pattern but increases seed by 1 so get a new slight variation.

### CV, Knob and Switch controls

| Input             | Function                                                                                                        |
| ----------------- | --------------------------------------------------------------------------------------------------------------- |
| Main knob + CV1   | Control density multiplier of the first bank of Euclidean tracks (kick, clap, snare, toms...), from 0x to 1.5x  |
| X knob + CV2      | Control density multiplier of the second bank of Euclidian tracks (hihats...), from 0x to 1.5x                  |
| Y knob            | Controls tempo of internal clock, from 30BPM to 180 (disabled when slaved to external clock).                   |
| Switch UP (hold)  | Freeze the random seed, so same pattern will continue playing.  Fills and density still take effect.            |
| Switch DOWN (mom) | Mutes new notes by default, or selects a sample bank when configured below.                                    |
| Audio In 1 (CV)   | Bipolar CV controls shuffle amount (negative CV inverts the shuffle)                                            |
| Audio In 2 (CV)   | Bipolar CV controls sampleplayer pitch (negative lower, positive higher)                                        |

### Standalone bank and switch setup

On the 16 MB firmware, hold the momentary switch DOWN during power-up until
the bank LED blinks, then release it. Playback and clock servicing continue
throughout setup.

- Main selects a sample bank; only detected valid banks are offered.
- X selects the momentary switch role: low = Mute, high = Bank Select.
- The first five LEDs represent banks 0-4, one LED per bank. The first LED
  means bank 0, the compiled-in kit. The sixth LED is off for Mute and on
  for Bank Select.
- Neither knob changes the existing selection until deliberately moved.
- Tap the momentary switch DOWN and release to save and exit. All LEDs
  briefly light on success; a failed bank load blinks them and leaves setup
  open with the previous bank and role intact.

Mute is the default: holding DOWN and turning knobs remains the normal
mute-and-adjust performance gesture. With Bank Select enabled, holding DOWN
temporarily borrows Main to choose a bank without muting new notes. Release
to load and remember that bank. Selection does not load banks while the knob
is moving. Bank loading fades the output briefly to silence and back; existing
sample tails stop at the fade and are not carried across as a crossfade.
The latched UP position still freezes the random seed in either role.

Borrowed knobs retain their previous density contribution until returned to
their previous position (pickup); CV modulation continues. Main uses pickup
after bank selection, and both Main and X use it after startup setup.

The same role is available under **Settings > Momentary switch** in
the web manager. Changes apply live; **Save settings** makes them survive a
reboot. Older firmware leaves the role control disabled. Startup setup is
always available, regardless of the saved role.

### CV clock input

On firmware built with `ENABLE_CLOCK_INPUT_CV`, Pulse 2 receives clock and
Pulse 1 receives reset. In the web manager, **Settings > CV clock input
(Pulse 2)** selects 1, 2, 4, 8, 12 or 24 PPQN (pulses per quarter note).
The default is 1 PPQN. Changes apply live when external CV clock is active;
the selected rate is retained when switching clock sources. USB MIDI clock
always uses 24 PPQN. Use **Save settings** to retain the CV rate after reboot.
The selector is disabled on older firmware or builds without CV clock input.

Two additional Settings toggles control Pulse 1 RESET behavior:

- **RESET required to enter CV clock mode**, default ON: RESET selects CV clock;
  clock pulses alone do not take over from internal or USB MIDI clock. OFF lets
  incoming CV clock pulses select CV mode without a reset.
- **RESET also rewinds song position / seed**, default ON: RESET returns to
  phrase zero, restoring the initial phrase-derived seed unless seed lock is held.
  OFF aligns to the current phrase start without discarding the phrase index or
  seed progression. At an already-due phrase boundary it uses the next phrase.
  Both modes release held notes before repositioning. Neither changes the base seed.

RESET also starts playback if stopped or paused; playback waits for the next
CV clock pulse at the selected phrase position.

**Settings > Euclidean** also exposes **Fills enabled** (default ON), **Mutation
amount** (0-8, default 3), **Reset before mutation** (default ON), and **Add Card
ID to seed** (default OFF). The latter adds a stable folded card identity, not
another phrase offset; phrase progression and seed lock retain their existing
roles. Changes affect subsequent fills/mutations without restarting playback.
Reset before mutation restores unlocked pattern defaults before phrase mutations;
it does not reset transport, phrase position or the seed.
All six options apply live and are persisted by **Save settings**. Legacy settings
load with the old behavior; older web clients preserve the new options.

### Clock Diagnostics

The web manager's **Clock** tab shows set BPM, engine BPM, edge-derived CV BPM,
pulse/reset activity, strict/stalled state, timing, input queue losses, accepted
source pulse counts and expected/delivered sync ticks. CV raw counts and queue
losses are cumulative since boot, including events that could not be queued.
Active-source counters restart with a clock reset. Reentry values are current
handler depths, not cumulative dropped-event totals. Snapshots span asynchronous
cores and are diagnostic observations, not an atomic electrical-edge trace.

Polling runs every 500 ms while Clock is visible or recording, skips active
transfers, and never sends clock/start/reset messages. Activity lights indicate
count changes between snapshots, not individual high-rate pulses. **Record trace**,
reproduce the issue, then stop and download JSON. Recording stops at 1,200 samples
(about ten minutes without pauses); it preserves all captured samples rather than
silently discarding old ones. Capture the external source's BPM, PPQN and RESET
pattern alongside the file. Disconnect stops recording but keeps the download.

Detected CV tempo is `60000000 / (interval_us * selected_PPQN)`. The current engine
supports 1-500 BPM; an out-of-range estimate is flagged rather than hidden by the
engine's tempo limits. A narrow electrical pulse missed by audio-rate sampling
cannot appear in these software counters. The RP2040 fast-clock failure was
resolved by updating the existing uClock timer period instead of cancelling and
reallocating alarms on rate changes, and confirmed on hardware. The diagnostics
remain available to distinguish input loss, queue loss and stalled tick delivery.

The serial `clock-state` command prints the same categories. Serial
`clock-external-cv-ppqn <1|2|4|8|12|16|24>` applies a live, unsaved PPQN change.

#### Clock Status Protocol

Protocol v2 request `0x1B` has no payload; its queued `0x47` response carries a
version-1, 140-byte payload. Header bytes 0-14 are: version, CV capability, source
(0 internal, 1 USB MIDI, 2 CV, 3 other), playing, waiting, strict, stalled, engine
state, configured CV PPQN, reset-required, reset-song-position, fills, mutation
amount, reset-before-mutation, Card-ID seed enabled.

The remaining 25 fields are uint32 values encoded as five least-significant-first
7-bit bytes each: uptime ms, set milli-BPM, engine milli-BPM, tick, effective seed
bits, raw pulses, raw resets, queue losses, raw pulse age us, reset age us, interval
us, minimum interval us, maximum interval us, queue lag us, maximum lag us, active
pulses, rejected pulses, delivered sync ticks, expected sync ticks, pending engine
ticks, last accepted interval us, engine pulse age us, internal reentry depth,
external reentry depth, active input PPQN. Never-observed raw ages are UINT32_MAX.
Configuration responses now have 156 bytes: the six options append at 150-155.
SET_CONFIG appends the same group at request bytes 22-27; partial groups or invalid
booleans/counts are rejected before bank/settings changes. Legacy prefixes remain
unchanged. The effective seed is interpreted as signed 32-bit by the browser.

### Web bank workflow

Serve `web/` over localhost and open `compulidian_manager.html` in a
Web MIDI-capable browser, then connect to the device. For example:

```sh
python3 -m http.server 8082 --directory web
```

Open `http://localhost:8082/compulidian_manager.html`.

- **Playing bank** changes playback immediately without changing the editor.
  **Save settings** remembers the playing bank, switch role, CV clock and Euclidean options
  across reboot. This does not save bank edits or sample tags.
- **Edit bank** automatically loads one of the four editable bank slots.
  Empty slots start with an unassigned draft. Browsing does not switch playback.
  Leaving a modified draft requires confirmation; failed reads keep it intact.
- **Save bank** writes the editor contents to that slot. If the slot is
  playing, its sound updates immediately. **Discard** reloads its saved contents.
- **Copy to...** writes the current editor contents, including unsaved edits,
  into another editable slot. It leaves the source draft, edit selection and
  playing bank number unchanged. Existing destinations require confirmation;
  copying onto the playing bank updates its sound immediately.
- **Play bank** selects the saved editor bank for playback. Save edits first.
  Bank 0 remains the fixed compiled-in kit, available in **Playing bank**.
- **Samples > Save tags** saves sample types and tags separately. Refreshes
  preserve unsaved tag edits. **Export staged samples** exports only the
  staged audio library, not the installed samples or bank configurations.
- Sample updates are still downloaded as UF2 files and installed manually.
  Full storage replacement and MIDI diagnostics are secondary device tools.

The Device tab shows the current fixed input routing. Configurable CV and
knob routing is not implemented yet; stored routing fields are retained when
saving the supported settings. Unsaved drafts survive reconnects within the
open page, but are not an offline backup. Closing or reloading warns about
unsaved work.

### MIDI features

- If a MIDI start message is received over USB MIDI then the internal clock is disabled and playback will be slaved to the external USB MIDI clock instead.
- If a USB MIDI host is connected then the drum patterns are sent back to the host on channel 10.

### Serial console commands

- Connect via serial to access debug console.
- Press `enter` after entering one of these commands to do the appropriate action.
- Backspace/delete etc don't currently work so don't make any typos!
- Press `tab`+ `enter` to repeat the previous command.

| Command           | Function                                                                    |
| ----------------- | --------------------------------------------------------------------------- |
| `l`               | List tracks and sample names                                                |
| `p <output_name>` | Trigger the pattern of the given name (eg Kick, Stick, Clap..)              |
| `s`               | Toggle sample interpolation (trade off audio quality vs CPU)                |
| `c`               | Toggle between audio generation in interrupt (HQ) vs main loop 'dirty' mode |
| `v`               | Toggle voice/sample volume honouring (doesn't seem to make much diff)       |
| `f`               | Toggle fills on/off                                                         |
| `V`		    | Output build date/time and branch/commit info				  |

## Known problems

- (IMPROVED by reducing samplerate) Seems like ADC reads are a bit wobbly when higher CPU used; reducing number of voices seems to improve stability
- (WORKED AROUND by dropping samplerate) Having to overclock to 220mhz to get 16 voices to play within sample time.
- Modulating shuffle seems to upset the clock timing, maybe?
- Intermittent crashing when attempting to output to Serial
- Receiving external MIDI clock seems to glitch/freeze/miss clocks for a few seconds after MIDI start is received

## Future plans

- all the modulation possibilities need to be made to connect to sensible things;
- make modulation/knob/parameter mapping configurable (maybe via WebUSB?)
- slave to external CV clock and reset
- option to send LFOs/envelopes out on CV, instead of using them as gates?
- i also have some code for generating scale-quantised bassline patterns based on the generated euclidian patterns if that would be interesting to anyone
  - or could maybe output a bassline on the second audio output
- enable selection of initial seed so can choose many variations of the default pattern?  maybe use unique card serial number for this?
- add more default patterns than four-on-floor that can be chosen somehow (eg breakbeat patterns)

open to collaboration and welcome feedback 🙂



----


couple more ideas before i forget:-
- allow multiple samples to be assigned to same midi note -> round robin / random
