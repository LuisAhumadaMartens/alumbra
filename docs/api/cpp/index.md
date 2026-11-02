\mainpage Alumbra C++ API reference

This reference covers every header that Alumbra installs:

- the **Penumbra interface** (namespace penumbra, `penumbra/interface/…`): time, rational, sample, stream and
  decoded-output value types that cross the porting contract, the v1 contract value types, and the thread
  creation hook that ports implement;
- **Umbra's base library** (namespace umbra::base, `umbra/base/…`): the error catalogue, structured error details
  and URL redaction for diagnostics.

Installed headers use only the C++20 standard library and other installed Alumbra headers. Internal headers,
such as the abstract port interfaces, the task runners and the Abseil-based status helpers, are not installed
and are not part of this reference; the guides below describe them.

## Conventions

- Fallible value operations return `std::optional` or a check enumeration instead of throwing.
- Times are microseconds with explicit unknown and infinite sentinels; container timestamps stay exact.
- Byte buffers are shared, immutable and reference-counted, so samples reach a port without copies.
- `AbslStringify` hooks let Abseil formatting and logging print values without the headers including Abseil.

## Examples

Each example is a complete program. CI compiles it against the installed package at exactly C++20 and runs it as
a test, so the examples cannot drift from the API:

- penumbra/interface/media_time.h: converting container timestamps;
- penumbra/interface/encoded_sample.h: sharing sample bytes and validating encryption metadata;
- umbra/base/url_redactor.h: redacting URLs and credential fields;
- umbra/base/error_catalogue.h: looking up catalogue metadata.

## Guides

- [Architecture overview](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/overview.md)
- [Media types](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/media-types.md)
- [Penumbra interface](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/port-contract.md)
- [Decoder chain](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/decoder-chain.md)
- [Status, detail and diagnostic privacy](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/architecture/errors.md)
- [Error catalogue](https://github.com/LuisAhumadaMartens/alumbra/blob/main/docs/reference/error-catalogue.md)

The reference documents the `main` branch. Version 0.0.0 and the package coordinates are provisional, and there
is no stable binary interface yet. Its pages are styled with
[doxygen-awesome-css](https://github.com/jothepro/doxygen-awesome-css), published under the MIT licence.
