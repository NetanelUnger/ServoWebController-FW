"""Embed the reviewed, bounded qualification guest (not a file upload API)."""
from pathlib import Path
import hashlib
import sys
data = Path(sys.argv[1]).read_bytes()
assert data[:8] == b'\0asm\1\0\0\0' and len(data) <= 2048
rows = [', '.join(f'0x{v:02x}' for v in data[i:i+16]) for i in range(0, len(data), 16)]
Path(sys.argv[2]).write_text(
    '/* Generated from tools/wamr_board/guest.c. SHA256 ' + hashlib.sha256(data).hexdigest() + ' */\n'
    'static const uint8_t robot_guest[] = {\n    ' + ',\n    '.join(rows) + '\n};\n', encoding='utf-8')
print(f'Embedded {len(data)} bytes')
