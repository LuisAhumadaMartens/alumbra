# Ad parser conformance fixtures

The XML fixtures in this directory are authored for Alumbra and contain no
third-party response bodies. They cover structures represented by the public
[IAB VAST samples](https://github.com/InteractiveAdvertisingBureau/VAST_Samples)
and Google's [IMA sample ad tags](https://developers.google.com/interactive-media-ads/docs/sdks/html5/client-side/tags).
The IAB repository provides no redistribution licence, so its files are
referenced by URL and are not copied here. Google's tags are also recorded as
live inputs by the scheduled drift workflow.

The desktop conformance tests assert the VAST model, the VMAP schedule, selected
media and tracking URLs. The macro fixture checks substitution independently
of network requests. Media URLs use the reserved `example.test` domain and are
never fetched.
