# Settings — every option in one schema

Every tunable in Alumbra (buffer sizes, ABR parameters, caption styling, ad timeouts, protocol options, logging,
…) is declared once here, with type, default, range, platform availability and documentation. Code generation
produces the C++, TypeScript, Kotlin and Swift accessors and the settings reference page.

Precedence (defaults → build → app → user → per-source) and the schema technology are open definition **D-07**.
