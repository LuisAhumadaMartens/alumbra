# Umbra — the portable C++ engine

The core of Alumbra: everything that does not depend on a platform. Pipeline and clock, sources (progressive,
HLS, DASH, low-latency, network protocols), ABR and buffering, demuxers, text tracks, the ad manager, settings
and the headless UI state machine, all behind an HTMLMediaElement-shaped API.

**Rule:** Umbra never includes a platform header. It talks to the outside world only through
[Penumbra](../penumbra/).

Planned modules are listed in the [architecture overview](../docs/architecture/overview.md#2-umbra--the-engine). Code arrives
from milestone M0.
