#!/usr/bin/env python3
import pathlib
import sys

CLUSTER_ID = bytes.fromhex("1f43b675")
UNKNOWN_SIZE = bytes.fromhex("01ffffffffffffff")


def vint_length(first):
    length = 1
    mask = 0x80
    while length <= 8 and not first & mask:
        mask >>= 1
        length += 1
    return length


def element_header(data, position):
    id_length = vint_length(data[position])
    size_length = vint_length(data[position + id_length])
    size_start = position + id_length
    size = data[size_start] & (0xFF >> size_length)
    for byte in data[size_start + 1:size_start + size_length]:
        size = (size << 8) | byte
    return id_length, size_length, size


def convert(data):
    segment = data.index(bytes.fromhex("18538067"))
    id_length, size_length, size = element_header(data, segment)
    result = bytearray(data[:segment + id_length])
    result += UNKNOWN_SIZE
    position = segment + id_length + size_length
    end = min(len(data), position + size)
    while position < end:
        id_length, size_length, size = element_header(data, position)
        body = position + id_length + size_length
        if data[position:position + id_length] == CLUSTER_ID:
            result += CLUSTER_ID + UNKNOWN_SIZE + data[body:body + size]
        else:
            result += data[position:body + size]
        position = body + size
    return bytes(result)


def main():
    if len(sys.argv) != 3:
        print("usage: make_unknown_size.py INPUT OUTPUT", file=sys.stderr)
        return 2
    converted = convert(pathlib.Path(sys.argv[1]).read_bytes())
    pathlib.Path(sys.argv[2]).write_bytes(converted)
    return 0


if __name__ == "__main__":
    sys.exit(main())
