# Compulidian

Made a start porting my euclidean rhythm generator + modulation libraries to a (Music Thing Workshop Computer)[https://github.com/tomWhitwell/Workshop_Computer] card.

https://github.com/doctea/Compulidian / https://github.com/doctea/Compulidian/raw/refs/heads/main/firmware/firmware.uf2

Its an platformio project for vscode, with Arduino and the earlephilhower RP2040 core.

Uses my (parameters)[https://github.com/doctea/parameters], (midihelpers)[https://github.com/doctea/midihelpers] and (seqlib)[https://github.com/doctea/seqlib] libraries.  Uses (uClock)[https://github.com/midilab/uClock] for clock generation.  Borrows heavily from (rheslip/Raspberry-Pi-Pico-Eurorack-Drum-Sample-Player)[https://github.com/rheslip/Raspberry-Pi-Pico-Eurorack-Drum-Sample-Player] for the sample player, but intend to replace this with something a bit more robust and easier to replace the samples on.

See `platform.io` `build_flags` and `include/Config.h` for some settings.

## Basic features

- Multiple independent Euclidean tracks for kick, snare, hi-hat, etc etc (actually ~16 tracks internally).
- Plays audio samples out of both audio outputs (currently an 808 kit).
- Currently 4 gate outputs (via Computer's 2x pulse and 2x cv outputs) are fed from kick, snare and 2x hi-hat tracks
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
| Switch DOWN (mom) | Mutes the sequencer so no new notes will play.  LEDs will flash on the beat.                                    |
| Audio In 1        | Bipolar CV controls shuffle amount (negative CV inverts the shuffle)                                            |
| Audio In 2        | Bipolar CV controls sampleplayer pitch (negative lower, positive higher)                                        |

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

## Problems

- Seems like ADC reads are a bit wobbly when higher CPU used; reducing number of voices seems to improve stability
- Having to overclock to 220mhz to get 16 voices to play within sample time.
- Modulating shuffle seems to upset the clock timing, maybe?

## Future plans

- (in progress, currently a bastardised hybrid) convert to a ComputerCard to take advantage of normalisation probes, better input reading, and better sound production
- very lofi audio quality with bad aliasing problems when in 'main loop dirty mode' -- has some charm to it so could use this as an effect
- all the modulation possibilities need to be made to connect to sensible things;
- make modulation/knob/parameter mapping configurable (maybe via WebUSB?)
- use audio inputs as extra CV inputs
- slave to external CV clock and reset
- option to send LFOs/envelopes out on CV, instead of using them as gates?
- i also have some code for generating scale-quantised bassline patterns based on the generated euclidian patterns if that would be interesting to anyone
  - or could maybe output a bassline on the second output
- enable selection of initial seed so can choose many variations of the default pattern?
- add more default patterns than four-on-floor that can be chosen somehow (eg breakbeat patterns)

open to collaboration and welcome feedback 🙂
