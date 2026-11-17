# Compulidian

A mutating Euclidean drum machine for the [Music Thing Workshop Computer](https://github.com/tomWhitwell/Workshop_Computer), with sample playback, CV control and clocking.

> Beta firmware, updated 17 November 2026. Please report issues through the [project page](https://github.com/doctea/Compulidian).

## Get started

Choose the firmware for your card:

- 16 MB, CV clock enabled: [Download UF2](https://github.com/doctea/Compulidian/blob/main/firmware/firmware_rpipico_16mb_with_cv_clock_input.uf2)
- 16 MB, CV clock disabled: [Download UF2](https://github.com/doctea/Compulidian/blob/main/firmware/firmware_rpipico_16mb.uf2)
- 2 MB, untested: [Download UF2](https://github.com/doctea/Compulidian/blob/main/firmware/firmware_rpipico.uf2)

Connect the card to your computer and open the [Compulidian web manager](https://compulidian-production.up.railway.app/) in a Web MIDI-capable browser such as Chrome. Use it to choose or edit sample banks and change settings.

## What it does

- Generates around 16 independent Euclidean drum patterns, with phrase changes and a variation fill on the last bar of each four-bar phrase.
- Plays the built-in 808 kit or user sample banks through Audio Out 1. Audio Out 2 outputs a 16th-note clock gate; four pulse/CV outputs provide drum gates.
- Main and X control the density of two track groups, with CV1 and CV2 modulation. Audio In 1 controls shuffle; Audio In 2 controls sample pitch.
- Syncs to its internal tempo, USB MIDI clock, or CV clock on firmware that supports CV clock input.

## Performance controls

| Control | Action |
| --- | --- |
| Main + CV1 | First group of drum patterns: density |
| X + CV2 | Second group of drum patterns: density |
| Y | Internal tempo, 30-180 BPM |
| Switch UP (hold) | Locks the pattern seed; fills and density changes continue |
| Switch DOWN | Mutes new notes; can be configured for sample-bank selection |
| Audio In 1 / 2 | Shuffle / sample pitch |
| Pulse In 1 / 2 | CV clock reset / clock, when supported by the firmware |

## Use the web manager

Connect over USB, open the web manager, and choose a **Playing bank** to change the sound. Use **Edit bank** to edit one of the user banks. Settings apply live; use **Save settings** to keep them after reboot. Bank edits and sample tags have their own save actions.

On 16 MB firmware, you can also hold Switch DOWN while powering on to choose a bank and set the switch role without the web manager. Playback continues during setup.

## More information

See [README-MORE.md](README-MORE.md) for detailed bank setup, clock and RESET options, web-manager workflows, diagnostics, serial commands, and build instructions. Hardware and driver notes are in [ComputerCard-notes.md](ComputerCard-notes.md).

Known issue: ADC readings can fluctuate, most noticeably as sample-pitch variation. Feedback and collaboration are welcome.
