# Ad obstruction registry

The `umbra::ads` module owns the view declarations used during an ad break. A
renderer declares each view handle, its role, its purpose, its reason and its
bounds before the break starts. Every view needs an explicit role. Friendly
obstructions also need a purpose. Reasons contain
1 to 50 letters, digits or spaces, matching the IMA friendly obstruction
contract.

Use the friendly role and `media_controls`, `close_ad`, `not_visible` or
`other` as its purpose for transient views that may cover the ad on native
platforms. Do not declare watermarks, dialogs or other persistent UI as
friendly obstructions. Mark such UI with the `never_shown_over_ads` role and
keep it outside the ad container.

At break start, forward mode registers each friendly obstruction once with the
provider. At break end, it unregisters every forwarded view once. A failed
registration rolls back the entries already forwarded. Record-only mode keeps
the declarations for the built-in desktop provider. Enforce-no-overlap mode
rejects a break when any declared view intersects the ad container;
this is the IMA HTML5 geometry rule because that SDK has no obstruction
registration API.

The registry exposes rectangles for declared views and the active ad container
through `DebugRegions()`. A renderer can use those values to outline registered
and forbidden regions in its debug overlay.

The module is enabled with `ALUMBRA_BUILD_ADS`; it defaults to the UI build
setting. The provider interface is a small seam for the Android and Apple IMA
ports. Mapping from these purposes to each SDK's enum belongs in those ports.
