\file penumbra/interface/interface.h
Version of the Penumbra port contract.

\namespace penumbra
Penumbra interface: the value types and hooks shared by Umbra and the platform ports.

These headers use only the C++20 standard library and never include Umbra or platform headers. The abstract
player, component and platform-hook interfaces that use these types are internal and not installed; the
[port contract](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/port-contract.md)
documents every call, and [media types](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/media-types.md)
explains the design of the value types.

\var penumbra::kApiVersion
Version of the port contract, currently 10.

Version 2 replaced the decoder factory calls with decoder candidates and added the HTTP transport and stream-provider interfaces to the port. Version 3 added port-provided audio and video buffer budgets and a normal memory-pressure signal. Version 4 added the optional fallback-demuxer slot to the port. Version 5 added VP8, Vorbis and Theora codec identifiers. Version 6 added H.264, HEVC and AAC sample framing to StreamInfo. Version 7 adds the optional AES-128-CBC crypto provider to the port. Version 8 carries H.273 colour, HDR static metadata and Dolby Vision configuration in `VideoInfo`, and HDR dynamic metadata on encoded samples. Version 9 adds `PlayerConfig.allow_preroll` and `Port::SetControlTaskRunner` for control-sequence binding and preroll before playback. Version 10 adds the caption preference provider and change observer.
PlayerConfig::api_version and PortLimits::api_version must equal it, and a port rejects a mismatch with
`kInvalidArgument` instead of guessing a compatible layout. A source-incompatible change, such as a new pure
virtual method, changed ownership or thread semantics, or a changed enumerator meaning, increments it. There is
no stable binary ABI: Umbra and the ports are rebuilt together with one toolchain.
