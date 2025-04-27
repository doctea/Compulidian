# Compulidian

Made a start porting my euclidean rhythm generator + modulation libraries to a (Music Thing Workshop Computer)[https://github.com/tomWhitwell/Workshop_Computer] card.

https://github.com/doctea/Compulidian / https://github.com/doctea/Compulidian/raw/refs/heads/main/firmware/firmware.uf2

Its an platformio project for vscode, with Arduino and the earlephilhower RP2040 core.

Uses my (parameters)[https://github.com/doctea/parameters], (midihelpers)[https://github.com/doctea/midihelpers] and (seqlib)[https://github.com/doctea/seqlib] libraries.  Uses (uClock)[https://github.com/midilab/uClock] for clock generation.  Borrows heavily from (rheslip/Raspberry-Pi-Pico-Eurorack-Drum-Sample-Player)[https://github.com/rheslip/Raspberry-Pi-Pico-Eurorack-Drum-Sample-Player] for the sample player, but intend to replace this with something a bit more robust and easier to replace the samples on.

See `platform.io` `build_flags` and `include/Config.h` for some settings.

## Basic features

- Main knob controls lower tracks density (kick, clap, snare, toms...).
- X knob controls upper tracks density (hihats...).
- Y knob controls BPM, from 30 to 180 (disabled when slaved to external clock).
- has multiple independent tracks for kick, snare, hi-hat, etc etc (actually ~20 tracks internally)
- plays lofi audio samples out of both audio outputs (currently an 808 kit).
- currently 4 gate outputs (via Computer's 2x pulse and 2x cv outputs) are fed from kick, snare and 2x hi-hat tracks
- defaults to a four-on-the-floor style pattern
- tracks are split into two groups, each group has its own 'global density' parameter controlled by Main knob and X knob, and modulated via CV1 and CV2.  Go from none to very hectic number of pulses.
- last bar of every 4-bar phrase plays a variation.
- at start of every phrase (4 bars), pattern resets to the default pattern but increases seed by 1 so get a new slight variation.
- putting the switch UP locks the current pattern seed (fills still play, can still adjust/modulate the density, but pattern seed won't change).
- holding the switch DOWN mutes the sequencer so no new notes will play.  when muted the LEDs will flash on the beat so you can time drops and cut-outs better.
- if a MIDI start message is received over USB MIDI then the internal clock is disabled and playback will be slaved to the external USB MIDI clock instead.
- if a USB MIDI host is connected then the drum patterns are sent back to the host on channel 10.

### Serial console commands

- Connect via serial to access debug console.
- Type `p <output name>` and press enter to trigger the pattern of that name (Kick, Stick, Clap, Snare, Cymbal 1, Tamb, HiTom, LoTom, PHH, OHH, CHH, Cymbal 2, Splash, Vibra, Ride Bell, Ride Cymbal)
- Type `I` and press enter to toggle ParameterInput console display

## Future plans

- convert to a ComputerCard to take advantage of normalisation probes, better input reading, and better sound production
- known problem: very lofi audio quality with bad aliasing problems
- all the modulation possibilities need to be made to connect to sensible things, basic proof of concept atm
- enable to slave to external CV clock and reset
- send CV LFOs/envelopes instead of gates?
- i also have some code for generating scale-quantised bassline patterns based on the generated euclidian patterns if that would be interesting to anyone
  - or could maybe output a bassline on the second output
- enable selection of initial seed so can choose many variations of the default pattern?
- add more default patterns than four-on-floor that can be chosen somehow (eg breakbeat patterns)

open to collaboration and welcome feedback 🙂
