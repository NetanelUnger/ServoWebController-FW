"""Build/validate the NatiLab OTA v2 container (standard library only)."""
import argparse
import binascii
import hashlib
import json
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAGIC = b'NSOTA2\0\0'
TARGET = 0x35414257
ACTIVE = 0x08006000
MAX_IMAGE = 488 * 1024 - 16
HEADER_SIZE = 64


def crc32(data):
    return binascii.crc32(data) & 0xffffffff


def validate_image(image):
    n = len(image)
    if n < 0x170 or n > MAX_IMAGE or n % 16:
        raise ValueError('Image size/alignment outside the OTA slot')
    sp, pc = struct.unpack_from('<II', image)
    tag = struct.unpack_from('<I', image, 0x160)[0] - ACTIVE
    if not (0x20000020 <= sp <= 0x20020000 and sp % 8 == 0 and pc & 1
            and ACTIVE <= (pc & ~1) < ACTIVE + n):
        raise ValueError('Invalid STM32WBA5 application vectors/link address')
    if not (0x164 <= tag <= n - 4 and tag % 4 == 0
            and (tag + 19) // 16 * 16 == n):
        raise ValueError('Invalid OTA tail pointer')
    if struct.unpack_from('<I', image, tag)[0] != 0x94448A29:
        raise ValueError('Missing OTA completion tag')
    if any(b != 255 for b in image[tag + 4:]):
        raise ValueError('Unexpected bytes after the OTA tail')


def unpack_package(data):
    if len(data) < HEADER_SIZE or data[:8] != MAGIC:
        raise ValueError('Select a .ota.bin package produced by build_ota.bat')
    fmt, header, target, address, length, checksum = struct.unpack_from('<HHIIII', data, 8)
    if (fmt, header, target, address) != (2, HEADER_SIZE, TARGET, ACTIVE):
        raise ValueError('Unsupported OTA format, board or application address')
    if any(data[52:60]) or crc32(data[:60]) != struct.unpack_from('<I', data, 60)[0]:
        raise ValueError('Damaged OTA header')
    raw_version = data[28:52]
    version = raw_version.split(b'\0', 1)[0].decode('ascii')
    if not re.fullmatch(r'[A-Za-z0-9.+_-]{1,23}', version) or raw_version != version.encode().ljust(24, b'\0'):
        raise ValueError('Invalid firmware version')
    image = data[HEADER_SIZE:]
    if length != len(image) or crc32(image) != checksum:
        raise ValueError('Damaged/truncated OTA payload')
    validate_image(image)
    return {'version': version, 'length': length, 'crc32': f'{checksum:08X}',
            'sha256': hashlib.sha256(data).hexdigest(), 'target': 'STM32WBA5MMG',
            'application_address': f'0x{ACTIVE:08X}', 'protocol': 2}, image


def pack_image(image, version):
    image += b'\xff' * (-len(image) % 16)
    validate_image(image)
    if not re.fullmatch(r'[A-Za-z0-9.+_-]{1,23}', version):
        raise ValueError('Version must be 1-23 ASCII letters, digits or .+_-')
    h = bytearray(HEADER_SIZE)
    h[:8] = MAGIC
    struct.pack_into('<HHIIII', h, 8, 2, HEADER_SIZE, TARGET, ACTIVE, len(image), crc32(image))
    h[28:28 + len(version)] = version.encode('ascii')
    struct.pack_into('<I', h, 60, crc32(h[:60]))
    return bytes(h) + image


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--inspect', type=Path)
    p.add_argument('--input', type=Path, default=ROOT / 'STM32CubeIDE/Debug/BleServoController.bin')
    p.add_argument('--output', type=Path, default=ROOT / 'artifacts/ota')
    args = p.parse_args()
    if args.inspect:
        info, _ = unpack_package(args.inspect.read_bytes())
    else:
        version = re.search(r'#define FIRMWARE_VERSION "([^"]+)"',
                            (ROOT / 'Core/Inc/firmware_version.h').read_text()).group(1)
        package = pack_image(args.input.read_bytes(), version)
        info, _ = unpack_package(package)
        args.output.mkdir(parents=True, exist_ok=True)
        path = (args.output / f'NatiLab-{version}-{info["crc32"]}.ota.bin').resolve()
        path.write_bytes(package)
        info['path'] = str(path)
        info['file_uri'] = path.as_uri()
        path.with_suffix('.json').write_text(json.dumps(info, indent=2) + '\n')
        (args.output / 'latest.json').write_text(json.dumps(info, indent=2) + '\n')
    print(json.dumps(info, indent=2))


if __name__ == '__main__':
    main()
