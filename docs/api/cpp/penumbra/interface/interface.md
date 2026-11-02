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
Version of the port contract, currently 2.

PlayerConfig::api_version and PortLimits::api_version must equal it, and a port rejects a mismatch with
`kInvalidArgument` instead of guessing a compatible layout. A source-incompatible change, such as a new pure
virtual method, changed ownership or thread semantics, or a changed enumerator meaning, increments it. There is
no stable binary ABI: Umbra and the ports are rebuilt together with one toolchain.
