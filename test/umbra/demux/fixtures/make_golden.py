#!/usr/bin/env python3
import json
import pathlib
import subprocess
import sys


def probe(path, entries, section):
    result = subprocess.run(
        ["ffprobe", "-v", "error", "-of", "json", f"-show_{section}",
         "-show_entries", entries, str(path)],
        check=True, capture_output=True, text=True)
    return json.loads(result.stdout)


def main():
    if len(sys.argv) < 2:
        print("usage: make_golden.py FILE...", file=sys.stderr)
        return 2
    for name in sys.argv[1:]:
        path = pathlib.Path(name)
        streams = probe(path, "stream=index,codec_name,time_base,start_pts", "streams")["streams"]
        packets = probe(path, "packet=stream_index,pts,dts,duration,size,flags", "packets")["packets"]
        lines = []
        for stream in streams:
            index = stream["index"]
            lines.append(f"stream {index} {stream['codec_name']} {stream['time_base']}")
            for packet in packets:
                if packet["stream_index"] != index:
                    continue
                key = "K" if "K" in packet["flags"] else "_"
                lines.append(" ".join(str(packet.get(field, "NA")) for field in ("pts", "dts", "duration", "size")) + " " + key)
        path.with_suffix(path.suffix + ".packets").write_text("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
