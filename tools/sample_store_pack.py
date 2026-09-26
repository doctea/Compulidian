#!/usr/bin/env python3
from __future__ import annotations
"""
sample_store_pack.py — Compulidian content-addressed sample store packer
=========================================================================

Phase B PC-side tool: converts a directory of WAV files (or a JSON manifest)
into a binary image of the device's content-addressed SampleStore (see
include/audio/sample_store.h), with optional UF2 wrapping for direct flash
upload (same picoboot/drag-and-drop mechanism as the legacy wav2bank.py).

Each sample is identified by a content hash computed here (over filename,
sample count and a PCM checksum) — the device never computes/verifies this
hash itself, it only stores and compares it (see sample_store.h).

Usage examples
--------------
# Auto-scan a directory, write the sample store image for a 16 MB card:
  python sample_store_pack.py samples/ --output store.bin

# Same, but wrap in UF2 for drag-and-drop or picoboot upload:
  python sample_store_pack.py samples/ --format uf2 --output store.uf2

# Use a JSON manifest for fine-grained control (same format as wav2bank.py):
  python sample_store_pack.py my_kit.json --format uf2 --output store.uf2

Output
------
Besides the binary/UF2 image, a JSON side-car ("<output>.map.json") is
written mapping each sample's original filename to its content_hash (hex) —
this is what a future bank-config tool will use to reference samples by
hash (Phase C).
"""

import argparse
import json
import struct
import sys
import zlib
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
FLASH_BANK_SIZE_16MB = 0x00300000   # 3 MB per bank on 16 MB cards
FLASH_MAX_BANKS_16MB = 4

FLASH_SAMPLESTORE_OFFSET = FLASH_BANKS_OFFSET
FLASH_SAMPLESTORE_SIZE_16MB = FLASH_BANK_SIZE_16MB * FLASH_MAX_BANKS_16MB  # 12 MB

# ---------------------------------------------------------------------------
# Sample store format constants (must match include/audio/sample_store.h)
# ---------------------------------------------------------------------------
SAMPLESTORE_MAGIC        = 0x73746F72   # 'stor'
SAMPLESTORE_VERSION      = 1
MAX_SAMPLESTORE_ENTRIES  = 256
SAMPLESTORE_ENTRY_SIZE   = 48
SAMPLESTORE_INDEX_SIZE   = 0x00004000   # 16 KB
SAMPLESTORE_HEADER_FIXED_SIZE = 32      # magic+version+num_entries+next_free+reserved[16]
SAMPLESTORE_ENTRY_FLAG_USED = 0x01

FLASH_SAMPLESTORE_DATA_OFFSET = FLASH_SAMPLESTORE_OFFSET + SAMPLESTORE_INDEX_SIZE

# ---------------------------------------------------------------------------
# UF2 constants (identical to wav2bank.py)
# ---------------------------------------------------------------------------
UF2_BLOCK_SIZE   = 512
UF2_DATA_SIZE    = 256
UF2_MAGIC_START0 = 0x0A324655
UF2_MAGIC_START1 = 0x9E5D5157
UF2_MAGIC_END    = 0x0AB16F30
RP2040_FAMILY_ID = 0xE48BFF56


# ---------------------------------------------------------------------------
# WAV loading (same behaviour as wav2bank.py)
# ---------------------------------------------------------------------------

def load_wav_as_int16(path: Path, target_rate: int | None) -> tuple[np.ndarray, int]:
    """Load a WAV file, mix to mono, optionally resample, return (int16 array, rate)."""
    data, rate = sf.read(str(path), dtype='float32', always_2d=True)

    if data.shape[1] > 1:
        data = data.mean(axis=1)
    else:
        data = data[:, 0]

    if target_rate is not None and rate != target_rate:
        g = gcd(target_rate, rate)
        up   = target_rate // g
        down = rate // g
        data = resample_poly(data, up, down).astype(np.float32)
        rate = target_rate

    peak = np.abs(data).max()
    if peak > 0:
        data = data * (0.95 / peak)

    samples = (data * 32767.0).clip(-32768, 32767).astype(np.int16)
    return samples, rate


# ---------------------------------------------------------------------------
# Content hashing
# ---------------------------------------------------------------------------

def fnv1a_32(data: bytes) -> int:
    h = 0x811C9DC5
    for b in data:
        h ^= b
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h


def compute_content_hash(name: str, num_samples: int, pcm: bytes) -> int:
    """Content-addressed hash over {original_filename, original_length, PCM checksum}."""
    pcm_crc = zlib.crc32(pcm) & 0xFFFFFFFF
    blob = name.encode('utf-8', errors='replace')
    blob += struct.pack('<I', num_samples)
    blob += struct.pack('<I', pcm_crc)
    return fnv1a_32(blob)


# ---------------------------------------------------------------------------
# Packing
# ---------------------------------------------------------------------------

def pack_entry_header(content_hash: int, data_offset: int, num_samples: int,
                       sample_rate: int, bit_depth: int, name: str) -> bytes:
    """Pack one SampleStoreEntryHeader (48 bytes)."""
    name_bytes = name.encode('ascii', errors='replace')[:23]
    raw = struct.pack('<IIIIBB2x24s4x',
        content_hash, data_offset, num_samples, sample_rate,
        bit_depth, SAMPLESTORE_ENTRY_FLAG_USED,
        name_bytes)
    assert len(raw) == SAMPLESTORE_ENTRY_SIZE, f"entry size mismatch: {len(raw)}"
    return raw


def build_sample_store(samples_spec: list[dict], target_rate: int | None,
                        verbose: bool = False) -> tuple[bytes, dict]:
    """
    Build the raw sample store binary (16 KB index + PCM data).

    samples_spec items: { file: Path, name: str }

    Returns (image_bytes, name_to_hash_map).
    """
    if not samples_spec:
        raise ValueError("Sample store must contain at least one sample")
    if len(samples_spec) > MAX_SAMPLESTORE_ENTRIES:
        raise ValueError(f"Sample store may have at most {MAX_SAMPLESTORE_ENTRIES} samples")

    entries: list[bytes] = []
    pcm_blocks: list[bytes] = []
    name_to_hash: dict[str, str] = {}
    byte_offset = 0

    for spec in samples_spec:
        path = spec['file']
        name = spec['name']
        if verbose:
            print(f"  Loading {path} …", end=' ', flush=True)

        samples, rate = load_wav_as_int16(path, target_rate)
        pcm = samples.tobytes()
        content_hash = compute_content_hash(name, len(samples), pcm)

        if verbose:
            dur = len(samples) / rate
            print(f"{len(samples)} samples @ {rate} Hz ({dur:.2f}s)  hash=0x{content_hash:08X}")

        entries.append(pack_entry_header(
            content_hash=content_hash,
            data_offset=byte_offset,
            num_samples=len(samples),
            sample_rate=rate,
            bit_depth=16,
            name=name,
        ))
        pcm_blocks.append(pcm)
        name_to_hash[name] = f"0x{content_hash:08X}"
        byte_offset += len(pcm)

    num = len(entries)
    pcm_data = b''.join(pcm_blocks)

    header = struct.pack('<IIII16x',
        SAMPLESTORE_MAGIC, SAMPLESTORE_VERSION, num, len(pcm_data))
    for e in entries:
        header += e
    header += b'\x00' * (SAMPLESTORE_ENTRY_SIZE * (MAX_SAMPLESTORE_ENTRIES - num))
    assert len(header) == SAMPLESTORE_HEADER_FIXED_SIZE + SAMPLESTORE_ENTRY_SIZE * MAX_SAMPLESTORE_ENTRIES
    header += b'\x00' * (SAMPLESTORE_INDEX_SIZE - len(header))
    assert len(header) == SAMPLESTORE_INDEX_SIZE

    return header + pcm_data, name_to_hash


# ---------------------------------------------------------------------------
# UF2 helpers (identical approach to wav2bank.py)
# ---------------------------------------------------------------------------

def make_uf2_block(target_addr: int, data: bytes, block_idx: int, total_blocks: int) -> bytes:
    assert len(data) <= UF2_DATA_SIZE
    payload = data.ljust(UF2_DATA_SIZE, b'\x00')

    block = struct.pack('<IIIIII',
        UF2_MAGIC_START0, UF2_MAGIC_START1,
        0x00002000, target_addr, UF2_DATA_SIZE, block_idx)
    block += struct.pack('<I', total_blocks)
    block += struct.pack('<I', RP2040_FAMILY_ID)
    block += payload
    block += b'\x00' * (UF2_BLOCK_SIZE - len(block) - 4)
    block += struct.pack('<I', UF2_MAGIC_END)
    assert len(block) == UF2_BLOCK_SIZE
    return block


def binary_to_uf2(data: bytes, base_addr: int) -> bytes:
    pad_len = (UF2_DATA_SIZE - len(data) % UF2_DATA_SIZE) % UF2_DATA_SIZE
    data += b'\xff' * pad_len

    total_blocks = len(data) // UF2_DATA_SIZE
    out = b''
    for i in range(total_blocks):
        chunk = data[i * UF2_DATA_SIZE:(i + 1) * UF2_DATA_SIZE]
        addr  = base_addr + i * UF2_DATA_SIZE
        out  += make_uf2_block(addr, chunk, i, total_blocks)
    return out


# ---------------------------------------------------------------------------
# Input loading
# ---------------------------------------------------------------------------

def load_manifest(manifest_path: Path, override_rate: int | None) -> tuple[list[dict], int | None]:
    with open(manifest_path) as f:
        manifest = json.load(f)

    base_dir = manifest_path.parent
    target_rate = override_rate if override_rate is not None else manifest.get('target_rate')

    specs = []
    for item in manifest.get('samples', []):
        specs.append({
            'file': base_dir / item['file'],
            'name': item.get('name', Path(item['file']).stem[:23]),
        })
    return specs, target_rate


def load_directory(dir_path: Path, override_rate: int | None) -> tuple[list[dict], int | None]:
    wav_files = sorted(dir_path.glob('*.wav')) + sorted(dir_path.glob('*.WAV'))
    if not wav_files:
        raise FileNotFoundError(f"No WAV files found in {dir_path}")

    specs = [{'file': wav, 'name': wav.stem[:23]} for wav in wav_files[:MAX_SAMPLESTORE_ENTRIES]]
    return specs, override_rate


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description='Pack WAV files into a Compulidian content-addressed sample store image (Phase B).')
    parser.add_argument('input',
        help='Directory of WAV files or path to a JSON manifest.')
    parser.add_argument('--output', '-o', required=True,
        help='Output file path.')
    parser.add_argument('--format', '-f', choices=['bin', 'uf2'], default='bin',
        help='Output format: raw binary (bin) or UF2 for flashing (uf2). Default: bin.')
    parser.add_argument('--flash-size', type=int, default=16, metavar='MB',
        help='Total flash size in MB. Only 16 MB cards support the sample store. Default: 16.')
    parser.add_argument('--rate', '-r', type=int, default=None, metavar='HZ',
        help='Resample all inputs to this rate (e.g. 22050 or 44100). Default: keep original.')
    parser.add_argument('--verbose', '-v', action='store_true',
        help='Print per-sample info.')

    args = parser.parse_args()

    if args.flash_size <= 2:
        print("Error: the sample store requires a 16 MB card (2 MB boards have no headroom).", file=sys.stderr)
        sys.exit(1)

    inp = Path(args.input)
    if not inp.exists():
        print(f"Error: {inp} does not exist.", file=sys.stderr)
        sys.exit(1)

    if inp.is_dir():
        specs, target_rate = load_directory(inp, args.rate)
    elif inp.suffix.lower() == '.json':
        specs, target_rate = load_manifest(inp, args.rate)
    else:
        print("Error: input must be a directory or a .json manifest.", file=sys.stderr)
        sys.exit(1)

    print(f"Sample store: {len(specs)} sample(s)")
    if target_rate:
        print(f"Target sample rate: {target_rate} Hz")

    image, name_to_hash = build_sample_store(specs, target_rate, verbose=args.verbose)

    max_region_size = FLASH_SAMPLESTORE_SIZE_16MB
    if len(image) > max_region_size:
        print(f"Error: sample store image ({len(image)} bytes) exceeds the "
              f"available region ({max_region_size} bytes).", file=sys.stderr)
        sys.exit(1)

    total_kb = len(image) / 1024
    print(f"Sample store image size: {total_kb:.1f} KB "
          f"({SAMPLESTORE_INDEX_SIZE // 1024} KB index + {(len(image) - SAMPLESTORE_INDEX_SIZE) / 1024:.1f} KB PCM)")

    out_path = Path(args.output)

    if args.format == 'bin':
        out_path.write_bytes(image)
    else:  # uf2
        base_addr = XIP_BASE + FLASH_SAMPLESTORE_OFFSET
        print(f"Target XIP address: 0x{base_addr:08X}")
        out_path.write_bytes(binary_to_uf2(image, base_addr))

    map_path = out_path.with_suffix(out_path.suffix + '.map.json')
    map_path.write_text(json.dumps(name_to_hash, indent=2))

    print(f"Wrote {out_path}")
    print(f"Wrote {map_path} (filename -> content_hash map, for Phase C bank config tooling)")


if __name__ == '__main__':
    main()
