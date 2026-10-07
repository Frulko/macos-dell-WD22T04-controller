"""Reconstruct the TPS6699 ROM carried in the locally extracted Dell updater.

Static file processing only. Never sends commands to hardware.
"""
from pathlib import Path
import struct
import sys


def unpack(data, offset):
    chunks = []
    sequence = 1
    while True:
        if offset + 8 > len(data):
            raise ValueError("Truncated TI block header")
        number, size, tag, family = struct.unpack_from("<HHHH", data, offset)
        if number != sequence or not 0 < size <= 0x4000 or (tag, family) != (0xff, 0x77):
            raise ValueError(f"Unexpected TI block at {offset:#x}")
        offset += 8
        if offset + size > len(data):
            raise ValueError("Truncated TI block payload")
        chunks.append(data[offset:offset + size])
        offset += size
        if size < 0x4000:
            return b"".join(chunks)
        sequence += 1


def unpack_initial_ram(data, offset, size):
    """Decode the literal/zero/back-reference stream used by ROM routine 0x2659e."""
    out = bytearray()

    def byte():
        nonlocal offset
        if offset >= len(data):
            raise ValueError("Truncated RAM initialization stream")
        value = data[offset]
        offset += 1
        return value

    if offset < 0 or size < 0 or size > 0x10000:
        raise ValueError("Invalid RAM initialization bounds")
    while len(out) < size:
        tag = byte()
        literals = (tag & 7) or byte()
        count = (tag >> 4) or byte()
        if not literals:
            raise ValueError("Invalid literal count")
        copied = count + 2 if tag & 8 else count
        if len(out) + literals - 1 + copied > size:
            raise ValueError("RAM initialization exceeds destination")
        for _ in range(literals - 1):
            out.append(byte())
        if tag & 8:
            distance = byte()
            if not 0 < distance <= len(out):
                raise ValueError("Invalid RAM back-reference")
            for _ in range(copied):
                out.append(out[-distance])
        else:
            out.extend(bytes(count))
    return bytes(out)


def self_test():
    first = struct.pack("<HHHH", 1, 0x4000, 0xff, 0x77) + b"a" * 0x4000
    second = struct.pack("<HHHH", 2, 4, 0xff, 0x77) + b"last"
    assert unpack(first + second, 0) == b"a" * 0x4000 + b"last"
    for invalid in [first + second[:-1], first + b"\x03" + second[1:], first[:7]]:
        try:
            unpack(invalid, 0)
        except ValueError:
            continue
        raise AssertionError("Malformed block accepted")
    assert unpack_initial_ram(b"\x23ab", 0, 4) == b"ab\0\0"
    assert unpack_initial_ram(b"\x1bab\x02", 0, 5) == b"ababa"
    assert unpack_initial_ram(b"\x00\x03\x02ab", 0, 4) == b"ab\0\0"
    for data, size in [(b"\x23a", 4), (b"\x19\x01", 3),
                       (b"\x1bab\x00", 5), (b"\x23ab", 3), (b"\x00\0\1", 1)]:
        try:
            unpack_initial_ram(data, 0, size)
        except ValueError:
            continue
        raise AssertionError("Malformed RAM stream accepted")
    print("OK: TI blocks and RAM literals, zeros, overlapping copies, invalid streams")


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        self_test()
    elif sys.argv[1:]:
        sys.exit("Usage: python3 research/unpack-ti-pd.py [--self-test]")
    else:
        directory = Path(__file__).resolve().parent / "extracted/dock"
        rom = unpack((directory / "[0]").read_bytes(), 0x6eb0ed)
        stack, reset = struct.unpack_from("<II", rom)
        if stack != 0x20006000 or reset != 0xdd:
            sys.exit("Unexpected TPS6699 vector table")
        destination = directory / "tps6699-rom.bin"
        destination.write_bytes(rom)
        print(f"{destination}: {len(rom)} bytes")
        source, address, size, decoder = struct.unpack_from("<IIII", rom, 0x27050)
        if (source, address, size, decoder) != (0x27090, 0x200004c0, 0x1d0, 0x2659e):
            sys.exit("Unexpected TPS6699 RAM initialization descriptor")
        ram = unpack_initial_ram(rom, source, size)
        for port in range(2):
            offset = port * 0x50 + 2 * 16
            svid = struct.unpack_from("<I", ram, offset)[0]
            callback = struct.unpack_from("<I", ram, offset + 12)[0]
            if (svid, callback) != (0x413c, 0x21f81):
                sys.exit("Unexpected Dell callback in initial RAM")
            print(f"Port {port}: SVID {svid:04x}, Thumb callback {callback:#x}")
        destination = directory / "tps6699-initial-ram.bin"
        destination.write_bytes(ram)
        print(f"{destination}: {len(ram)} bytes at {address:#x}")
