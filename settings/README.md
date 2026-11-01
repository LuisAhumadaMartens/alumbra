# Settings — every option in one schema

Every tunable in Alumbra (buffer sizes, ABR parameters, caption styling, ad timeouts, protocol options, logging,
…) is declared once here, with type, default, range, platform availability and documentation. Code generation
produces the C++, TypeScript, Kotlin and Swift accessors and the settings reference page.

[ADR-0017](../docs/adr/0017-settings-schema-and-configuration.md) accepts TOML as the schema format and defines
the precedence from lowest to highest: compiled default → port default → integrator configuration → user
preference → per-source override. An integrator lock excludes the user layer for that key.

The generated contract and remaining implementation details are tracked in
[the settings and token implementation contract](../docs/architecture/adr-0017-0019-implementation-contract.md)
and in [#53](https://github.com/LuisAhumadaMartens/alumbra/issues/53),
[#54](https://github.com/LuisAhumadaMartens/alumbra/issues/54) and
[#160](https://github.com/LuisAhumadaMartens/alumbra/issues/160). In particular, `live` changes do not require
a reload or player restart, but owning components observe them through asynchronous sequence notifications.
