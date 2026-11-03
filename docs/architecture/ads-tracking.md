# VAST tracking

The built-in VAST tracking session maps provider lifecycle events to the
VAST impression, creative and interaction URLs. It sends URLs through the
shared beacon dispatcher supplied by the host integration. A session records
once-only events by ad and URL; pause and resume remain repeatable. Progress
events retain their VAST offsets and fire when the provider playhead reaches a
time or percentage offset. Moving the playhead backwards does not clear the
once-only record.

The session expands macro values before dispatch. Macro values use RFC 3986
percent encoding. Unknown playheads and unavailable values are `-1`; missing
device and application identifiers are withheld as `-2`. The deprecated
`CONTENTPLAYHEAD` macro uses the media playhead. Cache-busting and timestamp
values can be supplied for deterministic tests, and otherwise are generated
when the URL is expanded.

Application bundle and limited-ad-tracking values are supplied through
`ads.tracking.app_bundle` and `ads.tracking.limit_ad_tracking`. The engine does
not derive identifiers from the device or invent an application identifier.
Resolved wrapper error URLs are retained on the inline ad, so reporting an
error dispatches to each retained error URL with the same error code.
