import functools
import http.server
import pathlib
import subprocess
import sys
import threading


class RangeHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, format, *args):
        return

    def do_GET(self):
        filename = pathlib.Path(self.path.lstrip("/"))
        if filename.name != self.path.lstrip("/"):
            self.send_error(404)
            return
        media = pathlib.Path(self.directory) / filename
        if not media.is_file():
            self.send_error(404)
            return
        content = media.read_bytes()
        requested = self.headers.get("Range")
        start = 0
        end = len(content) - 1
        if requested is not None:
            unit, value = requested.split("=", 1)
            if unit != "bytes":
                self.send_error(416)
                return
            bounds = value.split("-", 1)
            start = int(bounds[0])
            if bounds[1]:
                end = min(int(bounds[1]), end)
            if start > end or start >= len(content):
                self.send_error(416)
                return
            self.send_response(206)
            self.send_header("Content-Range", f"bytes {start}-{end}/{len(content)}")
        else:
            self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(end - start + 1))
        self.end_headers()
        self.wfile.write(content[start : end + 1])


def main():
    executable = sys.argv[1]
    fixtures = pathlib.Path(sys.argv[2])
    handler = functools.partial(RangeHandler, directory=str(fixtures))
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    server_thread = threading.Thread(target=server.serve_forever)
    server_thread.start()
    try:
        address, port = server.server_address
        for filename in (
            "progressive_faststart.mp4",
            "h264_aac.ts",
            "webm_vp8_vorbis.webm",
        ):
            url = f"http://{address}:{port}/{filename}"
            result = subprocess.run(
                [executable, "--headless", url],
                capture_output=True,
                check=False,
                timeout=15,
            )
            if result.returncode != 0:
                sys.stderr.write(result.stderr.decode(errors="replace"))
                return result.returncode or 1
    finally:
        server.shutdown()
        server.server_close()
        server_thread.join()
    return 0


if __name__ == "__main__":
    sys.exit(main())
