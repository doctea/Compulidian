#!/usr/bin/env python3
from __future__ import annotations
"""
bank_config_pack.py — Compulidian per-bank config file packer (Phase C)
========================================================================

Writes a small saveloadlib-format text file (see include/audio/bank_config.h,
BankConfigHost) that BankManager loads from LittleFS at "/save/bank_N.txt".
Each slot in the file references a sample already present in the shared
content-addressed SampleStore (see sample_store_pack.py) by content_hash -
this tool does not touch any WAV/PCM data itself.

Input is a small JSON spec, e.g.:

  {
    "bank_name": "Kit 1",
    "slots": [
      {"voice": 0, "sample": "kick01", "volume": 220, "note": 36},
      {"voice": 1, "sample_id": "0x1A2B3C4D", "volume": 255, "note": 42},
      {"voice": 2, "compiled_voice": 3, "volume": 200, "note": 38}
    ]
  }

"sample" is resolved to a content_hash via a "<store>.map.json" side-car
produced by sample_store_pack.py (pass its path with --map, or let this tool
auto-discover "<input>.map.json"/a sibling "*.map.json" next to the spec).
"sample_id" may be given directly as a hex string instead, taking priority
over "sample" if both are present. "compiled_voice" references a built-in
(compiled-into-firmware) sample by its voice index instead of anything in
the SampleStore - if a slot's assigned sample_id is unresolvable (e.g. later
deleted from the store), the device now always falls back to that voice's
own compiled-in sample automatically, so "compiled_voice" is only needed to
deliberately pick a *different* voice's built-in sample, or a built-in
sample by choice rather than as a fallback.

Usage
-----
  python bank_config_pack.py kit1.json --map store.bin.map.json -o bank_1.txt

The output file can then be copied onto the device's LittleFS partition at
"/save/bank_1.txt" (numbering matches the SysEx/serial-console bank slot,
1..FLASH_MAX_BANKS) by whatever filesystem-image tooling the project uses
for LittleFS uploads.
"""

import argparse
import json
import struct
import sys
from pathlib import Path

# Must match include/audio/bank_config.h
BANK_CONFIG_MAX_SLOTS = 32
BANK_CONFIG_SLOT_FLAG_COMPILED_INDEX = 0x01
BANK_NAME_MAX = 24  # includes the terminating NUL

SLOT_CONFIG_STRUCT = struct.Struct('<IBBB')  # sample_id, volume, midi_note, flags
assert SLOT_CONFIG_STRUCT.size == 7, "SlotConfig must pack to 7 bytes, matching bank_config.h"


def resolve_map(input_path: Path, explicit_map: str | None) -> dict:
    if explicit_map:
        map_path = Path(explicit_map)
    else:
        candidate = input_path.with_suffix(input_path.suffix + '.map.json')
        map_path = candidate if candidate.exists() else None

    if map_path is None:
        return {}
    if not map_path.exists():
        print(f"Error: sample map {map_path} does not exist.", file=sys.stderr)
        sys.exit(1)
    with open(map_path) as f:
        return json.load(f)


def parse_sample_id(slot: dict, name_to_hash: dict, voice: int) -> tuple[int, bool]:
    """Returns (sample_id, is_compiled_index)."""
    if 'compiled_voice' in slot:
        # Wire encoding is 1-based so 0 stays reserved for "unassigned".
        return int(slot['compiled_voice']) + 1, True
    if 'sample_id' in slot:
        return int(slot['sample_id'], 0), False
    if 'sample' in slot:
        name = slot['sample']
        if name not in name_to_hash:
            print(f"Error: voice {voice}: sample '{name}' not found in the sample map "
                  f"(pass --map, or check the name matches sample_store_pack.py's output).",
                  file=sys.stderr)
            sys.exit(1)
        return int(name_to_hash[name], 0), False
    print(f"Error: voice {voice}: slot needs one of 'sample_id', 'sample', or 'compiled_voice'.", file=sys.stderr)
    sys.exit(1)


def build_slots_bytes(spec: dict, name_to_hash: dict) -> bytes:
    slots = [bytes(SLOT_CONFIG_STRUCT.size)] * BANK_CONFIG_MAX_SLOTS  # all-zero = unassigned
    slots = list(slots)

    for slot in spec.get('slots', []):
        voice = int(slot['voice'])
        if voice < 0 or voice >= BANK_CONFIG_MAX_SLOTS:
            print(f"Error: voice {voice} is out of range (0..{BANK_CONFIG_MAX_SLOTS - 1}).",
                  file=sys.stderr)
            sys.exit(1)

        sample_id, is_compiled = parse_sample_id(slot, name_to_hash, voice)
        volume = int(slot.get('volume', 255))
        note = int(slot.get('note', 0))
        flags = BANK_CONFIG_SLOT_FLAG_COMPILED_INDEX if is_compiled else 0

        if not (0 <= volume <= 255):
            print(f"Error: voice {voice}: volume must be 0..255.", file=sys.stderr)
            sys.exit(1)
        if not (0 <= note <= 255):
            print(f"Error: voice {voice}: note must be 0..255.", file=sys.stderr)
            sys.exit(1)

        slots[voice] = SLOT_CONFIG_STRUCT.pack(sample_id & 0xFFFFFFFF, volume, note, flags)

    return b''.join(slots)


def build_name_bytes(bank_name: str) -> bytes:
    name_bytes = bank_name.encode('ascii', errors='replace')[:BANK_NAME_MAX - 1]
    return name_bytes.ljust(BANK_NAME_MAX, b'\x00')


def write_config_file(out_path: Path, slots_bytes: bytes, name_bytes: bytes) -> None:
    lines = [
        "#saveloadlib_version=bank_config_pack.py",
        "#saveloadlib_format=1",
        "#saveloadlib_scope=0x02:SL_SCOPE_PROJECT",
        f"slots={slots_bytes.hex()}",
        f"name={name_bytes.hex()}",
    ]
    out_path.write_text('\n'.join(lines) + '\n')


def main():
    parser = argparse.ArgumentParser(
        description='Pack a bank config JSON spec into the saveloadlib text file BankManager reads from LittleFS (Phase C).')
    parser.add_argument('input', help='Path to the bank config JSON spec.')
    parser.add_argument('--output', '-o', required=True,
        help='Output path, e.g. bank_1.txt (copy onto the device as /save/bank_N.txt).')
    parser.add_argument('--map', '-m', default=None,
        help='Path to a "<store>.map.json" side-car from sample_store_pack.py '
             '(auto-discovered as "<input>.map.json" if omitted).')

    args = parser.parse_args()

    input_path = Path(args.input)
    if not input_path.exists():
        print(f"Error: {input_path} does not exist.", file=sys.stderr)
        sys.exit(1)

    with open(input_path) as f:
        spec = json.load(f)

    name_to_hash = resolve_map(input_path, args.map)

    slots_bytes = build_slots_bytes(spec, name_to_hash)
    name_bytes = build_name_bytes(spec.get('bank_name', ''))

    used = sum(1 for i in range(BANK_CONFIG_MAX_SLOTS)
               if slots_bytes[i * SLOT_CONFIG_STRUCT.size:i * SLOT_CONFIG_STRUCT.size + 4] != b'\x00\x00\x00\x00')
    print(f"Bank '{spec.get('bank_name', '')}': {used} slot(s) assigned "
          f"(of {BANK_CONFIG_MAX_SLOTS} available)")

    out_path = Path(args.output)
    write_config_file(out_path, slots_bytes, name_bytes)
    print(f"Wrote {out_path}")


if __name__ == '__main__':
    main()
