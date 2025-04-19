# Compulidian

made a start porting my euclidean rhythm generator + modulation libraries to a Computer card 

https://github.com/doctea/Compulidian / https://github.com/doctea/Compulidian/raw/refs/heads/main/firmware/firmware.uf2

its a platformio project for vscode.

## basic features

- has multiple independent tracks for kick, snare, hi-hat, etc etc (actually 20 tracks internally)
- currently 4 gate outputs (via Computer's 2x pulse and 2x cv outputs) are fed from kick, snare and 2x hi-hat tracks
- defaults to a four-on-the-floor style pattern
- 'pulses' parameter of all tracks are multiplied by a global 'density' parameter (which is modulated via CV and/or knobs), so it goes from none/very few pulses up to 1.5x default pulses
- last bar of every 4-bar phrase it randomises parameters slightly to create a fill before falling
- at start of every phrase it resets back to the default pattern and 'X' random mutations are applied
- number of mutations 'X' can be controlled by a knob/CV

## future plans

- all the modulation possibilities need to be made to connect to sensible things, basic proof of concept atm
- enable to slave to external USB MIDI clock and also send the drum patterns out over USB MIDI
- enable to slave to external CV clock and reset
- could add some configuration capabilities via the Computer switches - or maybe make it mute, or fill on momentary?
- make it play audio of drums instead of sending CV triggers (if anyone could show me how to get started playing samples then that would be great!)
- send CV LFOs/envelopes instead of gates?
- i also have some code for generating scale-quantised bassline patterns based on the generated euclidian patterns if that would be interesting to anyone
- enable selection of initial seed so can choose many variations of the default pattern
- add more default patterns than four-on-floor that can be chosen via knob or switch or something

open to collaboration and welcome feedback 🙂
