#!/usr/bin/env python3
"""De-block a UF2 into its contiguous payload bytes (stdout).

UF2 = sequence of 512-byte blocks: 32-byte header, payload_size at offset
16 (uint32 LE, typically 256), payload starting at offset 32. Strings that
span block payloads are split on disk — concatenate payloads FIRST, then
run `strings`/grep. This is the de-block rule that caught two bad logging
builds on 2026-06-11 (keysmith tracker / docs 2026-06-10 §5).

Usage: uf2_payload.py <firmware.uf2> > payload.bin
"""

import sys
import struct

UF2_MAGIC0 = 0x0A324655  # "UF2\n"
UF2_MAGIC1 = 0x9E5D5157


def main() -> int:
    if len(sys.argv) != 2:
        sys.stderr.write(__doc__)
        return 2
    with open(sys.argv[1], "rb") as f:
        data = f.read()
    if len(data) % 512 != 0:
        sys.stderr.write(f"not a UF2: size {len(data)} not a multiple of 512\n")
        return 1
    out = sys.stdout.buffer
    blocks = 0
    for off in range(0, len(data), 512):
        block = data[off : off + 512]
        magic0, magic1, _flags, _addr, payload_size = struct.unpack_from("<5I", block, 0)
        if magic0 != UF2_MAGIC0 or magic1 != UF2_MAGIC1:
            sys.stderr.write(f"bad block magic at offset {off}\n")
            return 1
        if payload_size > 476:
            sys.stderr.write(f"implausible payload_size {payload_size} at offset {off}\n")
            return 1
        out.write(block[32 : 32 + payload_size])
        blocks += 1
    sys.stderr.write(f"de-blocked {blocks} blocks\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
