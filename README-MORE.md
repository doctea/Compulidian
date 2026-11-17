# Compulidian: Detailed Reference

This companion guide keeps the operational and development details out of the quick-start [README](README.md).

## Standalone bank and switch setup

On 16 MB firmware, hold Switch DOWN during power-up until the bank LED blinks, then release it. Playback continues throughout setup.

- Turn Main to choose a detected, valid sample bank. Bank 0 is the built-in kit; banks 1-4 are editable banks.
- Turn X to choose the Switch DOWN role: low is Mute, high is Bank Select.
- The first five LEDs show banks 0-4. The sixth LED is off for Mute and on for Bank Select.
- The knobs do not change a selection until you move them. Tap and release Switch DOWN to save and exit. All LEDs briefly light on success; a failed bank load blinks the LEDs and leaves setup open with the previous selection.

Mute is the default role, so holding DOWN and turning knobs remains the usual mute-and-adjust gesture. With Bank Select enabled, hold DOWN and turn Main to choose a bank without muting; release to load and remember it. Borrowed knobs use pickup: their previous density contribution remains until the knob returns to its former position, while CV modulation continues.

The role is also available in **Settings > Momentary switch** in the web manager. Changes apply live; **Save settings** persists them. Older firmware may not expose this setting. Startup setup remains available regardless of the saved role.

## Clock, reset and phrase behavior

### Clock sources

- The Y knob sets the internal tempo from 30 to 180 BPM.
- USB MIDI clock uses 24 PPQN and takes over when USB MIDI transport starts.
- On firmware built with CV clock input, Pulse In 2 receives clock and Pulse In 1 receives RESET. **Settings > CV clock input (Pulse 2)** selects 1, 2, 4, 8, 12 or 24 PPQN; the default is 1. Changes apply live and are retained across clock-source changes. Use **Save settings** to persist them across reboot.
- A RESET starts playback if stopped or paused. Playback begins at phase zero on the next CV clock pulse.

### RESET and Euclidean settings

In **Settings**, **RESET required to enter CV clock mode** is on by default. When on, RESET selects CV clock; clock pulses alone do not take over from internal or USB MIDI clock. Turn it off to let incoming CV clock pulses select CV mode directly.

**RESET mode** defaults to **Resync**. All modes reset playback phase and release held notes:

- **Resync** preserves phrase progression.
- **Next Phrase** also advances to the next phrase.
- **Restart Song** returns progression to phrase zero.

The **Euclidean** settings are **Fills enabled** (on by default), **Mutation amount** (0-8, default 3), **Reset before mutation** (on by default), and **Add Card ID to seed** (off by default). Card ID adds a stable folded card identity; it is not another phrase offset. Reset before mutation restores unlocked pattern defaults before applying phrase mutations, without resetting transport, phrase position or seed. Changes affect subsequent fills and mutations without restarting playback.

RESET mode, RESET-required, and the four Euclidean options apply live and persist with **Save settings**. CV PPQN is saved there as well. Settings from the previous development schema migrate the RESET mode to Resync once; other valid extended options are retained.

Fills and phrase mutations follow musical progression rather than playback phase, so frequent RESETs do not starve them. Holding Switch UP freezes the phrase contribution to the seed, but does not stop fills or progression. Card ID remains an independent seed input.

## Web manager workflows

Connect the device by USB and open the deployed [Compulidian web manager](https://compulidian-production.up.railway.app/) in a Web MIDI-capable browser. To run a local copy instead:

```sh
python3 -m http.server 8082 --directory web
```

Then open `http://localhost:8082/compulidian_manager.html`.

- **Playing bank** changes playback without changing the editor. **Save settings** remembers the playing bank, switch role, CV clock and Euclidean options. It does not save bank edits or sample tags.
- **Edit bank** loads one of four editable slots. Empty slots start as unassigned drafts. Browsing does not switch playback. Leaving a modified draft asks for confirmation; failed reads leave the draft intact.
- **Save bank** writes the editor contents to that slot. If it is playing, its sound updates immediately. **Discard** reloads the saved contents.
- **Copy to...** copies current editor contents, including unsaved edits, to another editable slot. Existing destinations require confirmation. Copying to the playing bank updates its sound immediately.
- **Play bank** selects the saved editor bank for playback; save edits first. Bank 0 is the fixed kit and is selected from **Playing bank**.
- **Samples > Save tags** saves sample types and tags separately. Refreshes preserve unsaved tag edits. **Export staged samples** exports only the staged audio library, not installed samples or bank configurations.
- Sample updates are downloaded as UF2 files and installed manually. Full storage replacement and MIDI diagnostics are secondary tools.

The Device tab shows fixed input routing; configurable CV and knob routing is not implemented. Unsaved drafts survive reconnects in the open page but are not an offline backup. Closing or reloading warns about unsaved work.

## Clock diagnostics

The web manager's **Clock** tab shows configured and engine BPM, edge-derived CV BPM, pulse/reset activity, strict or stalled state, timing, input-queue losses, accepted source pulses, and expected versus delivered sync ticks. CV raw counts and queue losses are cumulative since boot, including events that could not be queued. Active-source counters restart on clock reset. Reentry values show current handler depth, not cumulative dropped events. Snapshots span asynchronous cores and are diagnostic observations, not an atomic electrical-edge trace.

Polling runs every 500 ms while Clock is visible or recording. It skips active transfers and does not send clock, start or reset messages. Activity lights indicate changes between snapshots, not individual high-rate pulses. Use **Record trace**, reproduce the issue, then stop and download JSON. Recording stops at 1,200 samples (about ten minutes without pauses) and preserves captured samples. Include the external source's BPM, PPQN and RESET pattern when reporting an issue. Disconnect stops recording but keeps the download.

Detected CV tempo is `60000000 / (interval_us * selected_PPQN)`. The engine supports 1-500 BPM; out-of-range estimates are flagged rather than hidden by tempo limits. Pulses too narrow for audio-rate sampling cannot appear in software counters. The RP2040 fast-clock issue was resolved by updating the existing uClock timer period instead of cancelling and reallocating alarms on rate changes; hardware testing confirmed the fix.

The serial `clock-state` command prints the same diagnostic categories. `clock-external-cv-ppqn <1|2|4|8|12|16|24>` applies a live, unsaved PPQN change.

### Clock status protocol

Protocol v2 request `0x1B` has no payload. Its queued `0x47` response carries a version-1, 140-byte payload. Header bytes 0-14 are: version, CV capability, source (0 internal, 1 USB MIDI, 2 CV, 3 other), playing, waiting, strict, stalled, engine state, configured CV PPQN, reset-required, reset mode (0 Resync, 1 Restart Song, 2 Next Phrase), fills, mutation amount, reset-before-mutation, and Card-ID seed enabled.

The remaining 25 fields are uint32 values, each encoded in five least-significant-first 7-bit bytes: uptime ms, set milli-BPM, engine milli-BPM, tick, effective seed bits, raw pulses, raw resets, queue losses, raw pulse age us, reset age us, interval us, minimum interval us, maximum interval us, queue lag us, maximum lag us, active pulses, rejected pulses, delivered sync ticks, expected sync ticks, pending engine ticks, last accepted interval us, engine pulse age us, internal reentry depth, external reentry depth, and active input PPQN. Never-observed raw ages are `UINT32_MAX`.

Configuration responses have 156 bytes; the six reset/Euclidean options append at bytes 150-155. SET_CONFIG appends the same group at request bytes 22-27. Partial groups and invalid reset modes, booleans or counts are rejected before bank or settings changes. The browser interprets the effective seed as signed 32-bit.

## MIDI and serial console

When a USB MIDI host is connected, drum patterns are sent back on channel 10. USB MIDI echoes are discarded while the host is unavailable, including queued notes on disconnect; reconnecting does not replay old events. Local samples, gates and LEDs continue without a host or serial terminal.

Connect to the serial console and enter `help` for the available commands. Press Enter to run a command; Tab then Enter repeats the previous command. Backspace and Delete are not supported.

| Command | Action |
| --- | --- |
| `l` | List tracks and sample names |
| `p <output_name>` | Trigger a named pattern, for example `p Kick` |
| `s` | Toggle sample interpolation |
| `c` | Toggle audio generation between interrupt and main-loop modes |
| `v` | Toggle voice/sample volume honoring |
| `f` | Toggle fills |
| `V` | Print build date/time and branch/commit information |

## Development and release builds

This is a PlatformIO project for VS Code, using Arduino and the Earle Philhower RP2040 core. Dependencies include the local `parameters`, `foundling`, `midihelpers`, `saveloadlib` and `seqlib` libraries, plus patched uClock and TinyUSB dependencies. See `platformio.ini` build flags and `include/Config.h` for configuration.

The pinned ComputerCard reference, local driver changes and optional legacy ADC divider are documented in [ComputerCard-notes.md](ComputerCard-notes.md).

In VS Code, run **Tasks: Run Task > Build release firmware** to build the three release environments. The task stages these files:

- `firmware/firmware_rpipico.uf2`
- `firmware/firmware_rpipico_16mb.uf2`
- `firmware/firmware_rpipico_16mb_with_cv_clock_input.uf2`

Nothing is uploaded. Existing release files are not changed if a build fails.
