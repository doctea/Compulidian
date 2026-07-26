#!/usr/bin/env python3
from __future__ import annotations
"""
wav2bank.py — Compulidian sample bank converter
================================================

Converts a directory of WAV files (or a JSON manifest) into a Compulidian
sample bank binary, with optional UF2 wrapping for direct flash upload.

Usage examples
--------------
# Auto-scan a directory, write bank binary for slot 1 on a 16 MB card:
  python wav2bank.py samples/ --slot 1 --flash-size 16 --output my_kit.bin

# Same, but wrap in UF2 for drag-and-drop or picoboot upload:
  python wav2bank.py samples/ --slot 1 --flash-size 16 --format uf2 --output my_kit.uf2

# Use a JSON manifest for fine-grained control:
  python wav2bank.py my_kit.json --slot 1 --format uf2 --output my_kit.uf2

# Resample all inputs to 22050 Hz before packing:
  python wav2bank.py samples/ --rate 22050 --slot 1 --format uf2 --output my_kit.uf2

JSON manifest format
--------------------
{
    "bank_name": "My Drum Kit",
    "target_rate": 44100,
    "samples": [
        { "file": "kick.wav",  "midi_note": 36, "volume": 127, "name": "Kick"  },
        { "file": "snare.wav", "midi_note": 38, "volume": 100, "name": "Snare" },
        ...
    ]
}

All file paths in the manifest are relative to the manifest file's directory.
If "midi_note" is omitted the General MIDI percussion map is used where
possible; otherwise notes are assigned sequentially from 36.
"""

import argparse
import json
import os
import struct
import sys
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.signal import resample_poly
from math import gcd

# ---------------------------------------------------------------------------
# Flash layout constants (must match include/audio/flash_layout.h)
# ---------------------------------------------------------------------------
XIP_BASE             = 0x10000000
FLASH_FIRMWARE_SIZE  = 0x001F0000   # 1.9375 MB reserved for firmware
FLASH_SETTINGS_SIZE  = 0x00010000   # 64 KB reserved for settings
FLASH_BANKS_OFFSET   = FLASH_FIRMWARE_SIZE + FLASH_SETTINGS_SIZE  # 0x00200000

FLASH_BANK_SIZE_2MB  = 0           # no user banks on 2 MB board (firmware uses ~1.73 MB)
FLASH_BANK_SIZE_16MB = 0x00380000   # 3.5 MB per bank on 16 MB cards (4 banks = 14 MB)

MAX_SAMPLES_PER_BANK = 64

# ---------------------------------------------------------------------------
# Bank header constants (must match include/audio/bank_header.h)
# ---------------------------------------------------------------------------
BANK_MAGIC        = 0x636D7064   # "cmpd"
BANK_VERSION      = 1
BANK_HEADER_SIZE  = 4096         # one 4 KB flash sector

# ---------------------------------------------------------------------------
# UF2 constants
# ---------------------------------------------------------------------------
UF2_BLOCK_SIZE   = 512
UF2_DATA_SIZE    = 256
UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END    = 0x0AB16F30
RP2040_FAMILY_ID = 0xE48BFF56

# ---------------------------------------------------------------------------
# GM percussion note map (channel 10, note → name)
# ---------------------------------------------------------------------------
GM_PERCUSSION = {
    35: "Acoustic Bass Drum", 36: "Electric Bass Drum",
    37: "Side Stick",         38: "Acoustic Snare",
    39: "Hand Clap",          40: "Electric Snare",
    41: "Low Floor Tom",      42: "Closed Hi-Hat",
    43: "High Floor Tom",     44: "Pedal Hi-Hat",
    45: "Low Tom",            46: "Open Hi-Hat",
    47: "Low-Mid Tom",        48: "High-Mid Tom",
    49: "Crash Cymbal 1",     50: "High Tom",
    51: "Ride Cymbal 1",      52: "Chinese Cymbal",
    53: "Ride Bell",          54: "Tambourine",
    55: "Splash Cymbal",      56: "Cowbell",
    57: "Crash Cymbal 2",     58: "Vibraslap",
    59: "Ride Cymbal 2",      60: "High Bongo",
    61: "Low Bongo",          62: "Mute High Conga",
    63: "Open High Conga",    64: "Low Conga",
    65: "High Timbale",       66: "Low Timbale",
    67: "High Agogo",         68: "Low Agogo",
    69: "Cabasa",             70: "Maracas",
    71: "Short Whistle",      72: "Long Whistle",
    73: "Short Guiro",        74: "Long Guiro",
    75: "Claves",             76: "High Wood Block",
    77: "Low Wood Block",     78: "Mute Cuica",
    79: "Open Cuica",         80: "Mute Triangle",
    81: "Open Triangle",
}

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def load_wav_as_int16(path: Path, target_rate: int | None) -> tuple[np.ndarray, int]:
    """Load a WAV file, mix to mono, optionally resample, return (int16 array, rate)."""
    data, rate = sf.read(str(path), dtype='float32', always_2d=True)

    # Mix to mono
    if data.shape[1] > 1:
        data = data.mean(axis=1)
    else:
        data = data[:, 0]

    # Resample if requested
    if target_rate is not None and rate != target_rate:
        g = gcd(target_rate, rate)
        up   = target_rate // g
        down = rate // g
        data = resample_poly(data, up, down).astype(np.float32)
        rate = target_rate

    # Normalise to 95% of peak to avoid hard clipping
    peak = np.abs(data).max()
    if peak > 0:
        data = data * (0.95 / peak)

    # Convert to int16
    samples = (data * 32767.0).clip(-32768, 32767).astype(np.int16)
    return samples, rate


def pack_entry_header(offset: int, num_samples: int, sample_rate: int,
                      bit_depth: int, midi_note: int, volume: int,
                      name: str) -> bytes:
    """Pack one BankEntryHeader (40 bytes)."""
    name_bytes = name.encode('ascii', errors='replace')[:23].ljust(24, b'\x00')
    return struct.pack('<IIIBBBBs',
        offset, num_samples, sample_rate,
        bit_depth, midi_note, volume, 0,
        name_bytes)


def pack_entry_header_v2(offset: int, num_samples: int, sample_rate: int,
                         bit_depth: int, midi_note: int, volume: int,
                         name: str) -> bytes:
    """Pack one BankEntryHeader (40 bytes) — explicit field version."""
    raw = struct.pack('<III', offset, num_samples, sample_rate)
    raw += struct.pack('<BBBB', bit_depth, midi_note, volume, 0)
    name_bytes = name.encode('ascii', errors='replace')[:23]
    name_bytes += b'\x00' * (24 - len(name_bytes))
    raw += name_bytes
    assert len(raw) == 40, f"BankEntryHeader size mismatch: {len(raw)}"
    return raw


def pack_bank_header(bank_name: str, entries: list[bytes]) -> bytes:
    """Build the 4096-byte BankHeader."""
    num = len(entries)
    assert 0 < num <= MAX_SAMPLES_PER_BANK

    hdr = struct.pack('<III', BANK_MAGIC, BANK_VERSION, num)
    hdr += b'\x00' * 4   # reserved
    bname = bank_name.encode('ascii', errors='replace')[:31]
    bname += b'\x00' * (32 - len(bname))
    hdr += bname
    # 48 bytes so far
    assert len(hdr) == 48

    for e in entries:
        hdr += e
    # Pad remaining entry slots with zeros
    hdr += b'\x00' * 40 * (MAX_SAMPLES_PER_BANK - num)

    # Pad to BANK_HEADER_SIZE
    assert len(hdr) == 48 + 40 * MAX_SAMPLES_PER_BANK
    hdr += b'\x00' * (BANK_HEADER_SIZE - len(hdr))
    assert len(hdr) == BANK_HEADER_SIZE
    return hdr


def make_uf2_block(target_addr: int, data: bytes, block_idx: int, total_blocks: int) -> bytes:
    """Produce one 512-byte UF2 block."""
    assert len(data) <= UF2_DATA_SIZE
    payload = data.ljust(UF2_DATA_SIZE, b'\x00')  # pad short data blocks

    block = struct.pack('<IIIIII',
        UF2_MAGIC_START0,
        UF2_MAGIC_START1,
        0x00002000,       # flags: familyID present
        target_addr,
        UF2_DATA_SIZE,
        block_idx,
    )
    block += struct.pack('<I', total_blocks)
    block += struct.pack('<I', RP2040_FAMILY_ID)
    block += payload
    block += b'\x00' * (UF2_BLOCK_SIZE - len(block) - 4)
    block += struct.pack('<I', UF2_MAGIC_END)
    assert len(block) == UF2_BLOCK_SIZE
    return block


def binary_to_uf2(data: bytes, base_addr: int) -> bytes:
    """Wrap an arbitrary binary blob in UF2 blocks at base_addr."""
    # Pad to page alignment (256 bytes)
    pad_len = (UF2_DATA_SIZE - len(data) % UF2_DATA_SIZE) % UF2_DATA_SIZE
    data += b'\xff' * pad_len

    total_blocks = len(data) // UF2_DATA_SIZE
    out = b''
    for i in range(total_blocks):
        chunk = data[i * UF2_DATA_SIZE:(i + 1) * UF2_DATA_SIZE]
        addr  = base_addr + i * UF2_DATA_SIZE
        out  += make_uf2_block(addr, chunk, i, total_blocks)
    return out


def bank_xip_address(slot: int, flash_size_mb: int) -> int:
    """Return the XIP (memory-mapped) address for bank slot n (1-indexed)."""
    if flash_size_mb <= 2:
        raise ValueError("2 MB boards do not support user sample banks (firmware uses ~1.73 MB). Use --flash-size 16.")
    return XIP_BASE + FLASH_BANKS_OFFSET + (slot - 1) * FLASH_BANK_SIZE_16MB


# ---------------------------------------------------------------------------
# Core build function
# ---------------------------------------------------------------------------

def build_bank(samples_spec: list[dict], bank_name: str, target_rate: int | None,
               verbose: bool = False) -> bytes:
    """
    Build the raw bank binary (header + PCM data).

    samples_spec items:
        file       Path object to WAV file
        midi_note  int
        volume     int (0-127)
        name       str
    """
    if not samples_spec:
        raise ValueError("Bank must contain at least one sample")
    if len(samples_spec) > MAX_SAMPLES_PER_BANK:
        raise ValueError(f"Bank may have at most {MAX_SAMPLES_PER_BANK} samples")

    pcm_blocks: list[bytes] = []
    entries:    list[bytes] = []
    byte_offset = 0

    for spec in samples_spec:
        path = spec['file']
        if verbose:
            print(f"  Loading {path} …", end=' ', flush=True)

        samples, rate = load_wav_as_int16(path, target_rate)

        if verbose:
            dur = len(samples) / rate
            print(f"{len(samples)} samples @ {rate} Hz ({dur:.2f}s)")

        pcm = samples.tobytes()
        entry = pack_entry_header_v2(
            offset      = byte_offset,
            num_samples = len(samples),
            sample_rate = rate,
            bit_depth   = 16,
            midi_note   = spec.get('midi_note', 36),
            volume      = spec.get('volume', 127),
            name        = spec.get('name', path.stem[:23]),
        )
        entries.append(entry)
        pcm_blocks.append(pcm)
        byte_offset += len(pcm)

    header = pack_bank_header(bank_name, entries)
    pcm_data = b''.join(pcm_blocks)

    # Pad PCM data to a 256-byte page boundary so the next bank (if any)
    # starts on a clean page.
    pad = (256 - len(pcm_data) % 256) % 256
    pcm_data += b'\xff' * pad

    return header + pcm_data


# ---------------------------------------------------------------------------
# Input loading
# ---------------------------------------------------------------------------

def load_manifest(manifest_path: Path, override_rate: int | None) -> tuple[list[dict], str, int | None]:
    """Load a JSON manifest.  Returns (samples_spec, bank_name, target_rate)."""
    with open(manifest_path) as f:
        manifest = json.load(f)

    base_dir = manifest_path.parent
    bank_name   = manifest.get('bank_name', manifest_path.stem)
    target_rate = override_rate if override_rate is not None else manifest.get('target_rate')

    specs = []
    for item in manifest.get('samples', []):
        specs.append({
            'file':      base_dir / item['file'],
            'midi_note': item.get('midi_note', 36),
            'volume':    item.get('volume', 127),
            'name':      item.get('name', Path(item['file']).stem[:23]),
        })
    return specs, bank_name, target_rate


def load_directory(dir_path: Path, override_rate: int | None) -> tuple[list[dict], str, int | None]:
    """Auto-scan a directory for WAV files, sorted alphabetically."""
    wav_files = sorted(dir_path.glob('*.wav')) + sorted(dir_path.glob('*.WAV'))
    if not wav_files:
        raise FileNotFoundError(f"No WAV files found in {dir_path}")

    bank_name = dir_path.name
    specs = []
    for i, wav in enumerate(wav_files[:MAX_SAMPLES_PER_BANK]):
        midi_note = 36 + i  # assign sequentially from C2
        specs.append({
            'file':      wav,
            'midi_note': midi_note,
            'volume':    127,
            'name':      wav.stem[:23],
        })
    return specs, bank_name, override_rate


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description='Convert WAV files to a Compulidian sample bank binary or UF2.')
    parser.add_argument('input',
        help='Directory of WAV files or path to a JSON manifest.')
    parser.add_argument('--output', '-o', required=True,
        help='Output file path.')
    parser.add_argument('--format', '-f', choices=['bin', 'uf2'], default='bin',
        help='Output format: raw binary (bin) or UF2 for flashing (uf2). Default: bin.')
    parser.add_argument('--slot', '-s', type=int, default=1,
        help='Flash bank slot number (1-4). Used to compute the target XIP address for UF2. Default: 1.')
    parser.add_argument('--flash-size', type=int, default=2, metavar='MB',
        help='Total flash size in MB (2 for standard Pico, 16 for extended-flash card). Default: 2.')
    parser.add_argument('--rate', '-r', type=int, default=None, metavar='HZ',
        help='Resample all inputs to this rate (e.g. 22050 or 44100). Default: keep original.')
    parser.add_argument('--bank-name', '-n', default=None,
        help='Override bank display name (max 31 chars).')
    parser.add_argument('--verbose', '-v', action='store_true',
        help='Print per-sample info.')

    args = parser.parse_args()

    inp = Path(args.input)
    if not inp.exists():
        print(f"Error: {inp} does not exist.", file=sys.stderr)
        sys.exit(1)

    if inp.is_dir():
        specs, bank_name, target_rate = load_directory(inp, args.rate)
    elif inp.suffix.lower() == '.json':
        specs, bank_name, target_rate = load_manifest(inp, args.rate)
    else:
        print(f"Error: input must be a directory or a .json manifest.", file=sys.stderr)
        sys.exit(1)

    if args.bank_name:
        bank_name = args.bank_name[:31]

    if args.format == 'uf2' and not (1 <= args.slot <= 4):
        print(f"Error: --slot must be 1-4.", file=sys.stderr)
        sys.exit(1)

    print(f"Bank: '{bank_name}'  ({len(specs)} sample(s))")
    if target_rate:
        print(f"Target sample rate: {target_rate} Hz")

    bank_binary = build_bank(specs, bank_name, target_rate, verbose=args.verbose)
    total_kb = len(bank_binary) / 1024
    print(f"Bank size: {total_kb:.1f} KB")

    if args.format == 'bin':
        out = bank_binary
    else:  # uf2
        xip_addr = bank_xip_address(args.slot, args.flash_size)
        print(f"Target XIP address: 0x{xip_addr:08X}  (slot {args.slot}, {args.flash_size} MB flash)")
        out = binary_to_uf2(bank_binary, xip_addr)
        print(f"UF2 size: {len(out) // 1024:.1f} KB  ({len(out) // 512} blocks)")

    out_path = Path(args.output)
    out_path.write_bytes(out)
    print(f"Written: {out_path}")


if __name__ == '__main__':
    main()
