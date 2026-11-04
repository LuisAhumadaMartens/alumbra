import argparse
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]
FUZZ = ROOT / "test" / "fuzz"
FIXTURES = ROOT / "test" / "umbra" / "demux" / "fixtures"


def vint(value):
    for length in range(1, 9):
        maximum = (1 << (7 * length)) - 2
        if value <= maximum:
            encoded = bytearray(value.to_bytes(length, "big"))
            encoded[0] |= 1 << (8 - length)
            return bytes(encoded)
    raise ValueError(value)


def element(identifier, payload):
    return identifier + vint(len(payload)) + payload


def crc32_mpeg(data):
    crc = 0xFFFFFFFF
    for value in data:
        crc ^= value << 24
        for _ in range(8):
            if crc & 0x80000000:
                crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF
            else:
                crc = (crc << 1) & 0xFFFFFFFF
    return crc.to_bytes(4, "big")


def section(table, body):
    length = len(body) + 4
    header = bytes((table, 0xB0 | (length >> 8), length & 0xFF))
    content = header + body
    return content + crc32_mpeg(content)


def wrapped_demux(file_bytes, selector):
    header = bytes((selector,)) + b"AL82" + len(file_bytes).to_bytes(2, "little")
    return header + file_bytes + bytes((0, 1, 2, 3))


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-root", type=pathlib.Path, default=FUZZ)
    output = parser.parse_args().output_root
    fixture_seeds = (
        ("fmp4_fragment_fuzzer", "fragmented_sidx_seek", "fragmented_sidx.mp4"),
        ("mp4_demuxer_fuzzer", "progressive_seek", "progressive_faststart.mp4"),
        ("ts_demuxer_fuzzer", "h264_aac_seek", "h264_aac.ts"),
        ("mkv_demuxer_fuzzer", "webm_seek", "webm_vp9_opus.webm"),
    )
    for target, name, fixture in fixture_seeds:
        content = (FIXTURES / fixture).read_bytes()
        write(output / target / "corpus" / name, wrapped_demux(content, 0x20))

    pat_body = bytes.fromhex("0001c100000001e100")
    pmt_body = bytes.fromhex("0001c10000e101f0001be101f000")
    write(output / "ts_psi_fuzzer" / "corpus" / "pat", section(0, pat_body))
    write(output / "ts_psi_fuzzer" / "corpus" / "pmt", section(2, pmt_body))
    packets = (FIXTURES / "h264_aac.ts").read_bytes()[:188 * 24]
    write(output / "ts_psi_fuzzer" / "corpus" / "packetized_tables", packets)

    ebml_header = element(bytes.fromhex("1a45dfa3"), element(bytes.fromhex("4282"), b"webm") + element(bytes.fromhex("42f7"), b"\x01"))
    info_payload = element(bytes.fromhex("2ad7b1"), b"\x0f\x42\x40")
    info_payload += element(bytes.fromhex("4489"), bytes.fromhex("4059000000000000"))
    info = element(bytes.fromhex("1549a966"), info_payload)
    track_payload = element(bytes.fromhex("d7"), b"\x01")
    track_payload += element(bytes.fromhex("83"), b"\x01")
    track_payload += element(bytes.fromhex("86"), b"V_VP9")
    track = element(bytes.fromhex("ae"), track_payload)
    tracks = element(bytes.fromhex("1654ae6b"), track)
    cue_positions = element(bytes.fromhex("f7"), b"\x01")
    cue_positions += element(bytes.fromhex("f1"), b"\x00")
    cue_track = element(bytes.fromhex("b7"), cue_positions)
    cue = element(bytes.fromhex("b3"), b"\x00") + cue_track
    cues = element(bytes.fromhex("1c53bb6b"), element(bytes.fromhex("bb"), cue))
    block_payload = element(bytes.fromhex("a1"), bytes.fromhex("8100008000"))
    block_payload += element(bytes.fromhex("9b"), b"\x01")
    block_group = element(bytes.fromhex("a0"), block_payload)
    write(output / "ebml_fuzzer" / "corpus" / "ebml_header", ebml_header)
    write(output / "ebml_fuzzer" / "corpus" / "info_tracks_cues", info + tracks + cues)
    write(output / "ebml_fuzzer" / "corpus" / "block_group", block_group)


if __name__ == "__main__":
    main()
