import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

import av_sync
import verify_clip


ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOL = pathlib.Path(__file__).with_name("av_sync.py")


def capture(offset_ms, drift_ppm=0.0, count=120):
    events = []
    for index in range(count):
        media_time = index * 1000.0
        audio_time = media_time
        video_time = audio_time + offset_ms + media_time * drift_ppm / 1_000_000.0
        events.append({
            "media_time_ms": media_time,
            "video_present_ms": video_time,
            "audio_audible_ms": audio_time,
            "phase": "steady_state",
        })
    return {
        "schema": 1,
        "metadata": {"route": "synthetic", "display": "synthetic"},
        "calibration": {"offset_ms": 0, "uncertainty_ms": 0.1},
        "events": events,
    }


class AvSyncTest(unittest.TestCase):
    def test_clip_probe_validates_container_codecs_rates(self):
        for container, format_name, video_codec, audio_codec in (
            ("mp4", "mov,mp4", "h264", "aac"),
            ("ts", "mpegts", "h264", "aac"),
            ("webm", "matroska,webm", "vp9", "vorbis"),
        ):
            with self.subTest(container=container):
                verify_clip.verify({
                    "format": {"format_name": format_name},
                    "streams": [
                        {"codec_type": "video", "codec_name": video_codec,
                         "r_frame_rate": "25/1", "avg_frame_rate": "25/1"},
                        {"codec_type": "audio", "codec_name": audio_codec,
                         "sample_rate": "48000"},
                    ],
                }, container, "25", 48000)

    def test_positive_offset_is_measured_within_quarter_of_target(self):
        document = capture(40)
        _, events, uncertainty = self.read(document)
        report = av_sync.summarise(events, uncertainty, 50)
        self.assertLess(abs(report["mean_offset_ms"] - 40), 5)
        self.assertAlmostEqual(report["mean_offset_ms"], 40)

    def test_negative_offset_is_measured_within_quarter_of_target(self):
        document = capture(-40)
        _, events, uncertainty = self.read(document)
        report = av_sync.summarise(events, uncertainty, 50)
        self.assertLess(abs(report["mean_offset_ms"] + 40), 5)
        self.assertAlmostEqual(report["mean_offset_ms"], -40)

    def test_d02_target_and_hard_failure_bounds_are_distinct(self):
        for offset, hard_failure in ((20, False), (40, False), (41, True),
                                     (-20, False), (-60, False), (-61, True)):
            with self.subTest(offset=offset):
                document = capture(offset, count=2)
                _, events, uncertainty = self.read(document)
                report = av_sync.summarise(events, uncertainty, 50)
                self.assertEqual(report["hard_failure"], hard_failure)
                self.assertEqual(report["target_exceeded"], abs(offset) > 20)

    def test_skewed_audio_rate_is_reported_as_drifting(self):
        document = capture(0, drift_ppm=1000)
        _, events, uncertainty = self.read(document)
        report = av_sync.summarise(events, uncertainty, 50)
        steady = report["phases"]["steady_state"]
        self.assertTrue(steady["drifting"])
        self.assertGreater(steady["drift_slope_ppm"], 999)
        self.assertTrue(report["drifting"])
        self.assertGreater(report["drift_slope_ppm"], 999)

    def test_transient_settle_time_is_measured_against_twenty_ms_target(self):
        document = capture(30, count=6)
        document["events"] = document["events"][:3]
        for event in document["events"]:
            event["phase"] = "startup"
        document["events"][2]["video_present_ms"] -= 15
        _, events, uncertainty = self.read(document)
        report = av_sync.summarise(events, uncertainty, 50)
        startup = report["phases"]["startup"]
        self.assertTrue(startup["settled_within_20ms"])
        self.assertAlmostEqual(startup["settle_time_ms"], 1992.5)

    def test_capture_calibration_is_subtracted(self):
        document = capture(12)
        document["calibration"]["offset_ms"] = 9
        _, events, uncertainty = self.read(document)
        report = av_sync.summarise(events, uncertainty, 50)
        self.assertAlmostEqual(report["mean_offset_ms"], 3)

    def test_cli_writes_json_and_svg_report(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = root / "capture.json"
            output = root / "report.json"
            plot = root / "report.svg"
            source.write_text(json.dumps(capture(3)), encoding="utf-8")
            result = subprocess.run([
                sys.executable, str(TOOL), "analyse", str(source),
                "--json", str(output), "--plot", str(plot), "--enforce",
            ], capture_output=True, text=True, check=False, cwd=ROOT)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(output.read_text(encoding="utf-8"))
            self.assertAlmostEqual(report["mean_offset_ms"], 3)
            self.assertIn("<svg", plot.read_text(encoding="utf-8"))

    def test_bluetooth_route_is_reported_without_failing_enforcement(self):
        document = capture(100)
        document["metadata"]["route"] = "Bluetooth headphones"
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = root / "capture.json"
            output = root / "report.json"
            source.write_text(json.dumps(document), encoding="utf-8")
            result = subprocess.run([
                sys.executable, str(TOOL), "analyse", str(source),
                "--json", str(output), "--enforce",
            ], capture_output=True, text=True, check=False, cwd=ROOT)
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(output.read_text(encoding="utf-8"))
            self.assertTrue(report["gating_exempt"])
            self.assertTrue(report["target_exceeded"])

    def test_media_time_can_reset_after_a_seek(self):
        document = capture(0, count=2)
        document["events"][1]["media_time_ms"] = 0
        _, events, uncertainty = self.read(document)
        report = av_sync.summarise(events, uncertainty, 50)
        self.assertAlmostEqual(report["mean_offset_ms"], 0)

    def test_non_monotonic_capture_times_are_rejected(self):
        document = capture(0, count=2)
        document["events"][1]["video_present_ms"] = 0
        document["events"][1]["audio_audible_ms"] = 0
        with self.assertRaises(av_sync.CaptureError):
            self.read(document)

    def test_invalid_phase_type_is_rejected(self):
        document = capture(0, count=1)
        document["events"][0]["phase"] = []
        with self.assertRaises(av_sync.CaptureError):
            self.read(document)

    def test_flash_and_beep_onsets_are_detected_from_signal_levels(self):
        document = {
            "schema": 1,
            "calibration": {"offset_ms": 0, "uncertainty_ms": 0.1},
            "frames": [
                {"media_time_ms": 0, "present_ms": 0, "luma": 0.1},
                {"media_time_ms": 50, "present_ms": 50, "luma": 0.1},
                {"media_time_ms": 100, "present_ms": 100, "luma": 0.95},
                {"media_time_ms": 150, "present_ms": 150, "luma": 0.1},
                {"media_time_ms": 200, "present_ms": 200, "luma": 0.95},
                {"media_time_ms": 250, "present_ms": 250, "luma": 0.1},
            ],
            "audio": {
                "sample_rate_hz": 48000,
                "first_sample_position": 0,
                "first_sample_capture_ms": 0,
                "windows": [
                    {"sample_position": 0, "level": 0.01},
                    {"sample_position": 2400, "level": 0.01},
                    {"sample_position": 4800, "level": 0.5},
                    {"sample_position": 7200, "level": 0.01},
                    {"sample_position": 9600, "level": 0.5},
                    {"sample_position": 12000, "level": 0.01},
                ],
            },
        }
        _, events, _ = self.read(document)
        self.assertEqual(len(events), 2)
        self.assertEqual([event["video_present_ms"] for event in events], [100, 200])
        self.assertEqual([event["audio_audible_ms"] for event in events], [100, 200])

    def read(self, document):
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "capture.json"
            source.write_text(json.dumps(document), encoding="utf-8")
            return av_sync.read_capture(source)


if __name__ == "__main__":
    unittest.main()
