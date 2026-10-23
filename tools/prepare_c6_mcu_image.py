"""Extract and validate the pinned M5Stack Tab5 C6 MCU application for SDIO OTA.

This only prepares files on the PC. It never opens a serial port or sends commands.
The merged 0x0 image must not be sent directly to an OTA application partition.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

SOURCE_URL = ('https://raw.githubusercontent.com/m5stack/M5Tab5-UserDemo/main/'
              'platforms/tab5/wifi_c6_fw/'
              'ESP32C6-WiFi-SDIO-Interface-V1.4.1-96bea3a_0x0.bin')
MERGED_SHA256 = '1173bf86ab86c8dbd3f9863d10b15c2e46f57000b2b77c803e2b79bd0a3fa38e'


def prepare(merged):
    if hashlib.sha256(merged).hexdigest() != MERGED_SHA256:
        raise ValueError('Merged firmware differs from the pinned official image')
    partitions = []
    for at in range(0x8000, 0x9000, 32):
        row = merged[at:at + 32]
        if row[:2] != b'\xaaP':
            break
        _, kind, subtype, address, size, label, flags = struct.unpack('<HBBII16sI', row)
        partitions.append(dict(type=kind, subtype=subtype, offset=address,
                               size=size, label=label.split(b'\0')[0].decode('ascii')))
    slots = [p for p in partitions if p['type'] == 0 and p['subtype'] in (16, 17)]
    if len(slots) != 2:
        raise ValueError('Pinned image must contain two OTA application partitions')
    slot = next(p for p in slots if p['subtype'] == 16)
    start = slot['offset']
    data = merged[start:]
    if len(data) < 24 or data[0] != 0xe9 or not 1 <= data[1] <= 16:
        raise ValueError('Invalid ESP application image header')
    chip = struct.unpack_from('<H', data, 12)[0]
    if chip != 13:
        raise ValueError('Application image must target ESP32-C6 (chip ID 13)')
    pos, checksum = 24, 0xef
    for _ in range(data[1]):
        if pos + 8 > len(data):
            raise ValueError('Truncated segment header')
        address, count = struct.unpack_from('<II', data, pos)
        pos += 8
        if count > len(data) - pos:
            raise ValueError('Truncated application segment')
        for byte in data[pos:pos + count]:
            checksum ^= byte
        pos += count
    checksum_at = ((pos + 1 + 15) // 16) * 16 - 1
    if checksum_at >= len(data) or data[checksum_at] != checksum:
        raise ValueError('Application segment checksum mismatch')
    end = checksum_at + 1
    if data[23] != 1 or end + 32 > len(data):
        raise ValueError('Pinned application must include its appended SHA-256')
    if hashlib.sha256(data[:end]).digest() != data[end:end + 32]:
        raise ValueError('Application embedded SHA-256 mismatch')
    app = data[:end + 32]
    if len(app) > min(p['size'] for p in slots):
        raise ValueError('Application exceeds the pinned image OTA slot capacity')
    # esp_app_desc follows the first image segment header.
    if struct.unpack_from('<I', data, 32)[0] != 0xabcd5432:
        raise ValueError('ESP application descriptor not found')
    def cstr(offset, size):
        return data[offset:offset + size].split(b'\0')[0].decode('ascii')
    version, project, idf = cstr(48, 32), cstr(80, 32), cstr(144, 32)
    if version != '96bea3a' or project != 'network_adapter':
        raise ValueError('Unexpected pinned MCU application identity')
    report = dict(source_url=SOURCE_URL, merged_sha256=MERGED_SHA256,
                  app_sha256=hashlib.sha256(app).hexdigest(), app_bytes=len(app),
                  merged_app_offset=start, chip_id=chip, version=version,
                  project=project, idf_version=idf, merged_partitions=partitions,
                  target='ESP-Hosted-MCU Tab5 application',
                  device_partition_capacity_verified=False)
    return app, report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--input', required=True, type=Path)
    p.add_argument('--output', required=True, type=Path)
    args = p.parse_args()
    app, report = prepare(args.input.read_bytes())
    # Do not silently replace a differently prepared application.
    if args.output.exists() and args.output.read_bytes() != app:
        raise ValueError('Output already exists with different content')
    args.output.write_bytes(app)
    args.output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
